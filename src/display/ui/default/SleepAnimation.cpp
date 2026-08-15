#ifndef GAGGIMATE_SIM

#include "SleepAnimation.h"
#include <Arduino.h>
#include <display/drivers/common/Display.h>
#include <display/ui/default/bganim/BgAnim.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <math.h>

namespace {
constexpr int BAND_H = 16;                  // rows rendered/pushed per chunk
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

void SleepAnimation::configure(uint8_t id, const uint8_t p[4]) {
    animId.store(id);
    animParams.store(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
                     (static_cast<uint32_t>(p[3]) << 24));
}

void SleepAnimation::start(Display *d) {
    // !stopped: a previous task timed out its stop() and hasn't exited yet —
    // refuse to start rather than run two renderers against the same buffers.
    if (running || !stopped || d == nullptr) {
        return;
    }
    display = d;
    const int w = display->width();
    const int h = display->height();
    if (band == nullptr) {
        band = static_cast<uint16_t *>(allocPreferInternal(w * BAND_H * sizeof(uint16_t)));
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
    if (band == nullptr || !overlayOk) {
        log_e("SleepAnimation: buffer allocation failed (band=%p overlayOk=%d)", band, overlayOk);
        return;
    }
    initializedAnimId = -1; // force the animation's init on the render task
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
    const int back = (overlayFront.load() + 1) & 1;
    // With two buffers, the back buffer is the front of two publishes ago; a
    // frame started just before the last publish may still be blending from
    // it. Writing into it now would tear the composited widgets — skip and
    // let the caller retry on the next UI pass (the frame is ~20 ms).
    if (overlayInUse.load() == back) {
        return nullptr;
    }
    return overlays[back].buf;
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
    uint32_t fpsFrames = 0;
    unsigned long fpsWindowStart = millis();
    while (running) {
        const int64_t frameStart = esp_timer_get_time();
        renderFrame();
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

void SleepAnimation::renderFrame() {
    const int w = display->width();
    const int h = display->height();
    const uint32_t tMs = millis();

    const int id = animId.load();
    const uint32_t packed = animParams.load();
    const uint8_t p[4] = {static_cast<uint8_t>(packed & 0xFF), static_cast<uint8_t>((packed >> 8) & 0xFF),
                          static_cast<uint8_t>((packed >> 16) & 0xFF), static_cast<uint8_t>((packed >> 24) & 0xFF)};
    const BgAnimation &anim = bg_animation(id);
    if (id != initializedAnimId) {
        if (!anim.init(w, h)) {
            log_e("SleepAnimation: init failed for animation %d (%s)", id, anim.id);
            running = false;
            return;
        }
        initializedAnimId = id;
    }
    anim.frame(tMs, w, h, p);

    // One overlay for the whole frame; a publish mid-frame lands next frame.
    // The load/store/load dance closes the race with publishOverlay: after it,
    // overlayInUse is guaranteed to name the overlay we actually read.
    int ofi;
    do {
        ofi = overlayFront.load();
        overlayInUse.store(ofi);
    } while (ofi != overlayFront.load());
    const Overlay *ov = ofi >= 0 ? &overlays[ofi] : nullptr;
    const int ovXoff = ov != nullptr ? (ov->w - w) / 2 : 0;
    const int ovYoff = ov != nullptr ? (ov->h - h) / 2 : 0;

    for (int y0 = 0; y0 < h && running; y0 += BAND_H) {
        const int rows = (y0 + BAND_H <= h) ? BAND_H : (h - y0);
        anim.band(band, y0, rows, w, tMs, p);
        for (int y = y0; y < y0 + rows; y++) {
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
