#ifndef GAGGIMATE_SIM

#include "SleepAnimation.h"
#include <Arduino.h>
#include <display/drivers/common/Display.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <math.h>

namespace {
constexpr int BAND_H = 16;                  // rows rendered/pushed per chunk
constexpr int SIN_N = 1024;                 // sine LUT entries
constexpr int SIN_AMP = 512;                // sine LUT amplitude
constexpr uint32_t TARGET_FRAME_US = 33000; // ~30 fps cap
// Headroom for the snapshot's ext draw size (shadows etc. extend the render
// area past the object on every side).
constexpr int OVERLAY_EXT_MARGIN = 16;

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Per-channel RGB565 alpha blend (a: 0..255 foreground opacity).
uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t a) {
    const uint32_t inv = 255 - a;
    const uint32_t r = (((fg >> 11) & 0x1F) * a + ((bg >> 11) & 0x1F) * inv + 127) / 255;
    const uint32_t g = (((fg >> 5) & 0x3F) * a + ((bg >> 5) & 0x3F) * inv + 127) / 255;
    const uint32_t b = ((fg & 0x1F) * a + (bg & 0x1F) * inv + 127) / 255;
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

// Internal SRAM is deliberately scarce in this firmware (WiFi/BLE/TLS all
// compete for it) — always fall back to PSRAM rather than failing.
void *allocPreferInternal(size_t size) {
    void *p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (p == nullptr) {
        p = ps_malloc(size);
        if (p != nullptr) {
            log_w("SleepAnimation: %u B in PSRAM (internal SRAM full)", static_cast<unsigned>(size));
        }
    }
    return p;
}
} // namespace

SleepAnimation::~SleepAnimation() { stop(); }

void SleepAnimation::buildLuts() {
    if (sinLut == nullptr) {
        sinLut = static_cast<int16_t *>(allocPreferInternal(SIN_N * sizeof(int16_t)));
        for (int i = 0; sinLut != nullptr && i < SIN_N; i++) {
            sinLut[i] = static_cast<int16_t>(lroundf(sinf(i * (2.0f * static_cast<float>(M_PI) / SIN_N)) * SIN_AMP));
        }
    }
    if (palette == nullptr) {
        palette = static_cast<uint16_t *>(allocPreferInternal(256 * sizeof(uint16_t)));
        if (palette == nullptr) {
            return;
        }
        // Espresso palette: near-black -> deep brown -> caramel -> crema -> back.
        // Keyframes interpolated over the 256-entry wheel; cycling the wheel
        // animates color without touching pixel indices.
        constexpr uint8_t keys[][3] = {
            {8, 4, 2},      // near-black roast
            {54, 22, 8},    // dark espresso
            {130, 66, 22},  // caramel
            {214, 160, 92}, // crema
            {245, 226, 190},// steamed-milk highlight
            {130, 66, 22},  // back down through caramel
            {40, 16, 6},    // deep brown
        };
        constexpr int nKeys = sizeof(keys) / sizeof(keys[0]);
        for (int i = 0; i < 256; i++) {
            const float pos = i * (static_cast<float>(nKeys) / 256.0f);
            const int k0 = static_cast<int>(pos) % nKeys;
            const int k1 = (k0 + 1) % nKeys;
            const float f = pos - floorf(pos);
            // 70% brightness: the plasma is a sleep backdrop — keep it darker
            // than the widgets so the clock and status stay readable on top.
            const uint8_t r = static_cast<uint8_t>((keys[k0][0] + (keys[k1][0] - keys[k0][0]) * f) * 0.7f);
            const uint8_t g = static_cast<uint8_t>((keys[k0][1] + (keys[k1][1] - keys[k0][1]) * f) * 0.7f);
            const uint8_t b = static_cast<uint8_t>((keys[k0][2] + (keys[k1][2] - keys[k0][2]) * f) * 0.7f);
            palette[i] = rgb565(r, g, b);
        }
    }
}

void SleepAnimation::start(Display *d) {
    // !stopped: a previous task timed out its stop() and hasn't exited yet —
    // refuse to start rather than run two renderers against the same buffers.
    if (running || !stopped || d == nullptr) {
        return;
    }
    display = d;
    buildLuts();
    const int w = display->width();
    const int h = display->height();
    if (band == nullptr) {
        band = static_cast<uint16_t *>(allocPreferInternal(w * BAND_H * sizeof(uint16_t)));
    }
    if (colTerm == nullptr) {
        colTerm = static_cast<int16_t *>(allocPreferInternal(w * sizeof(int16_t)));
    }
    if (rowTerm == nullptr) {
        rowTerm = static_cast<int16_t *>(allocPreferInternal(h * sizeof(int16_t)));
    }
    if (overlayCap == 0) {
        overlayCap = static_cast<uint32_t>(w + 2 * OVERLAY_EXT_MARGIN) * (h + 2 * OVERLAY_EXT_MARGIN) * 3;
        for (auto &ov : overlays) {
            // Snapshot pixels only fit in PSRAM (~700 KB each); the tiny span
            // tables prefer SRAM. Read a couple times per frame — well within
            // the PSRAM budget that the LVGL composite path blew.
            ov.buf = static_cast<uint8_t *>(ps_malloc(overlayCap));
            ov.spanMin = static_cast<int16_t *>(allocPreferInternal(h * sizeof(int16_t)));
            ov.spanMax = static_cast<int16_t *>(allocPreferInternal(h * sizeof(int16_t)));
        }
    }
    bool overlayOk = true;
    for (auto &ov : overlays) {
        overlayOk = overlayOk && ov.buf != nullptr && ov.spanMin != nullptr && ov.spanMax != nullptr;
    }
    if (band == nullptr || sinLut == nullptr || palette == nullptr || colTerm == nullptr || rowTerm == nullptr || !overlayOk) {
        log_e("SleepAnimation: buffer allocation failed (band=%p sin=%p pal=%p col=%p row=%p overlayOk=%d)", band, sinLut, palette,
              colTerm, rowTerm, overlayOk);
        return;
    }
    running = true;
    stopped = false;
    TaskHandle_t handle = nullptr;
    // Core 1 (same as the UI task, which mostly sleeps while we run), above
    // its priority so frames win; the pacing delay keeps LVGL's touch poll fed.
    // 8 KB stack: renderFrame itself is lean, but log_i's float formatting and
    // the esp_lcd draw path both burn stack; 4 KB was within canary distance.
    if (xTaskCreatePinnedToCore(taskEntry, "SleepAnim", 8192, this, 2, &handle, 1) != pdPASS) {
        log_e("SleepAnimation: task creation failed");
        running = false;
        stopped = true;
        return;
    }
    taskHandle = handle;
    log_i("SleepAnimation: started (%dx%d)", w, h);
}

void SleepAnimation::stop() {
    if (!running) {
        return;
    }
    running = false;
    const unsigned long deadline = millis() + 500;
    while (!stopped && millis() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

uint8_t *SleepAnimation::overlayBackBuffer() {
    if (overlayCap == 0) {
        return nullptr;
    }
    return overlays[(overlayFront.load() + 1) & 1].buf;
}

void SleepAnimation::publishOverlay(int w, int h) {
    if (overlayCap == 0 || display == nullptr) {
        return;
    }
    const int back = (overlayFront.load() + 1) & 1;
    Overlay &ov = overlays[back];
    if (ov.buf == nullptr || ov.spanMin == nullptr || ov.spanMax == nullptr) {
        return;
    }
    ov.w = w;
    ov.h = h;
    const int panelW = display->width();
    const int panelH = display->height();
    // The snapshot extends past the panel by ext draw size on every side.
    const int xoff = (w - panelW) / 2;
    const int yoff = (h - panelH) / 2;
    // Span scan: one pass over the alpha channel (~230 KB) per refresh, on the
    // UI task. The render task then touches only rows/pixels that matter.
    for (int y = 0; y < panelH; y++) {
        int16_t mn = -1;
        int16_t mx = -1;
        const int sy = y + yoff;
        if (sy >= 0 && sy < h) {
            const uint8_t *a = ov.buf + (static_cast<size_t>(sy) * w + xoff) * 3 + 2;
            for (int x = 0; x < panelW; x++, a += 3) {
                if (*a != 0) {
                    if (mn < 0) {
                        mn = static_cast<int16_t>(x);
                    }
                    mx = static_cast<int16_t>(x);
                }
            }
        }
        ov.spanMin[y] = mn;
        ov.spanMax[y] = mx;
    }
    overlayFront.store(back);
}

void SleepAnimation::taskEntry(void *arg) {
    auto *self = static_cast<SleepAnimation *>(arg);
    self->renderLoop();
    self->stopped = true;
    vTaskDelete(nullptr);
}

void SleepAnimation::renderLoop() {
    uint32_t frame = 0;
    uint32_t fpsFrames = 0;
    unsigned long fpsWindowStart = millis();
    while (running) {
        const int64_t frameStart = esp_timer_get_time();
        renderFrame(frame++);
        fpsFrames++;

        const unsigned long now = millis();
        if (now - fpsWindowStart >= 10000) {
            log_i("SleepAnimation: %.1f fps", fpsFrames * 1000.0f / (now - fpsWindowStart));
            fpsFrames = 0;
            fpsWindowStart = now;
        }

        const int64_t elapsed = esp_timer_get_time() - frameStart;
        const int64_t remaining = TARGET_FRAME_US - elapsed;
        // Always yield at least one full tick so the UI task keeps polling
        // touch even when a frame overruns its budget.
        TickType_t ticks = pdMS_TO_TICKS(remaining > 1000 ? remaining / 1000 : 1);
        vTaskDelay(ticks > 0 ? ticks : 1);
    }
}

void SleepAnimation::renderFrame(uint32_t frame) {
    const int w = display->width();
    const int h = display->height();

    // Slow phase drift; independent primes keep the pattern from looping
    // visibly. Halved from the 47 fps tuning to match the ~30 fps cap.
    const int t1 = static_cast<int>(frame * 2);
    const int t2 = static_cast<int>(frame * 3 / 2);
    const int t3 = static_cast<int>(frame * 7 / 4);
    const int cycle = static_cast<int>(frame * 3 / 4); // palette rotation

    for (int x = 0; x < w; x++) {
        colTerm[x] = sinLut[(x * 5 + t1) & (SIN_N - 1)] + sinLut[(x * 2 + SIN_N - t2) & (SIN_N - 1)];
    }
    for (int y = 0; y < h; y++) {
        rowTerm[y] = sinLut[(y * 4 + t2) & (SIN_N - 1)] + sinLut[(y * 3 + t3) & (SIN_N - 1)];
    }

    // One overlay for the whole frame; a publish mid-frame lands next frame.
    const int ofi = overlayFront.load();
    const Overlay *ov = ofi >= 0 ? &overlays[ofi] : nullptr;
    const int ovXoff = ov != nullptr ? (ov->w - w) / 2 : 0;
    const int ovYoff = ov != nullptr ? (ov->h - h) / 2 : 0;

    for (int y0 = 0; y0 < h && running; y0 += BAND_H) {
        const int rows = (y0 + BAND_H <= h) ? BAND_H : (h - y0);
        uint16_t *out = band;
        for (int y = y0; y < y0 + rows; y++) {
            const int rt = rowTerm[y];
            for (int x = 0; x < w; x++) {
                const int v = colTerm[x] + rt; // range ±4*SIN_AMP
                *out++ = palette[((v >> 4) + cycle) & 255];
            }
            if (ov != nullptr && ov->spanMin[y] >= 0) {
                // Composite the standby widgets over the plasma (span-limited:
                // only pixels the snapshot actually covers).
                const int x0 = ov->spanMin[y];
                const int x1 = ov->spanMax[y];
                const uint8_t *px = ov->buf + (static_cast<size_t>(y + ovYoff) * ov->w + (x0 + ovXoff)) * 3;
                uint16_t *dst = band + static_cast<size_t>(y - y0) * w + x0;
                for (int x = x0; x <= x1; x++, px += 3, dst++) {
                    const uint8_t a = px[2];
                    if (a == 0) {
                        continue;
                    }
                    // LV_IMG_CF_TRUE_COLOR_ALPHA @16bpp, LV_COLOR_16_SWAP=0:
                    // little-endian RGB565 followed by an alpha byte.
                    const uint16_t c = static_cast<uint16_t>(px[0] | (px[1] << 8));
                    *dst = (a == 255) ? c : blend565(c, *dst, a);
                }
            }
        }
        // pushColors' width/height params are actually END coordinates — they
        // pass through unchanged to esp_lcd_panel_draw_bitmap (exclusive end).
        // Passing dimensions here asserted in rgb_panel_draw_bitmap on the
        // second band (y_start==y_end) and boot-looped sleep3/sleep4.
        display->pushColors(0, y0, w, y0 + rows, band);
    }
}

#endif // GAGGIMATE_SIM
