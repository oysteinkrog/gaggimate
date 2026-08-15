#ifndef GAGGIMATE_SIM

#include "SleepAnimation.h"
#include <Arduino.h>
#include <display/drivers/common/Display.h>
#include <display/ui/default/bganim/BgAnim.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <math.h>
#include <string.h>  // memmove, for the round-panel band compaction

// Stage timers for the bench build. These compile to nothing in a normal
// build, so the shipping render path carries no measurement overhead.
#ifdef GM_ANIM_BENCH
#define BENCH_T0(v) const int64_t v = esp_timer_get_time()
#define BENCH_ACC(acc, t0) (acc) += static_cast<uint64_t>(esp_timer_get_time() - (t0))
// How long to sit on each animation before recording it. Long enough that the
// mean is not dominated by the first frames, where the lazy LUT init runs.
constexpr unsigned long BENCH_DWELL_MS = 6000;
#define GM_BENCH_LOCK_ONE_BAND 1
#else
#define BENCH_T0(v) ((void)0)
#define BENCH_ACC(acc, t0) ((void)0)
// Constant-false, so the suspend branch in renderFrame folds away entirely in
// shipping builds rather than being compiled and never taken.
#define GM_BENCH_LOCK_ONE_BAND 0
#endif

namespace {
// Rows rendered/pushed per chunk. 8 rather than 16 halves both band buffers
// (they are the largest internal-SRAM consumers this feature has, and internal
// SRAM is what the animations' lookup tables compete for). It doubles the
// number of pushColors calls per frame, which the measurements say is free:
// push cost is per byte, not per call -- it was flat at ~23 ms across thirteen
// animations that share nothing but their byte count.
constexpr int BAND_H = 8;
// Headroom for the snapshot's ext draw size (shadows etc. extend the render
// area past the object on every side).
constexpr int OVERLAY_EXT_MARGIN = 16;
// Granularity of the overlay occupancy bitmap: 1 << 5 = 32 pixels per bit.
constexpr int OVERLAY_BLOCK_SHIFT = 5;

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
#ifdef GM_ANIM_BENCH
    // The bench owns the selection: DefaultUI re-applies the stored animation
    // on every UI pass, which would otherwise yank the sweep back to whatever
    // is saved in settings after each frame.
    (void)id;
    (void)p;
    return;
#else
    animId.store(id);
    animParams.store(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
                     (static_cast<uint32_t>(p[3]) << 24));
#endif
}

#ifdef GM_ANIM_BENCH
namespace {
SleepAnimation *g_benchInstance = nullptr;
} // namespace

SleepAnimation *sleep_animation_bench_instance() { return g_benchInstance; }
#endif

void SleepAnimation::start(Display *d) {
    // !stopped: a previous task timed out its stop() and hasn't exited yet —
    // refuse to start rather than run two renderers against the same buffers.
    if (running || !stopped || d == nullptr) {
        return;
    }
#ifdef GM_ANIM_BENCH
    g_benchInstance = this;
#endif
    display = d;
    const int w = display->width();
    const int h = display->height();
    for (int i = 0; i < 2; i++) {
        if (bandBuf[i] == nullptr) {
            bandBuf[i] = static_cast<uint16_t *>(allocPreferInternal(w * BAND_H * sizeof(uint16_t)));
        }
        if (bandReady[i] == nullptr) {
            bandReady[i] = xSemaphoreCreateBinary();
        }
        if (bandFree[i] == nullptr) {
            bandFree[i] = xSemaphoreCreateBinary();
        }
    }
    if (halfBuf == nullptr) {
        halfBuf = static_cast<uint16_t *>(allocPreferInternal((w / 2) * (BAND_H / 2) * sizeof(uint16_t)));
    }
    computeChords(w, h);
    if (overlayCap == 0) {
        overlayCap = static_cast<uint32_t>(w + 2 * OVERLAY_EXT_MARGIN) * (h + 2 * OVERLAY_EXT_MARGIN) * 3;
        for (auto &ov : overlays) {
            // Snapshot pixels only fit in PSRAM (~700 KB each); the tiny span
            // tables prefer SRAM. Read a couple times per frame — well within
            // the PSRAM budget that the LVGL composite path blew.
            ov.buf = static_cast<uint8_t *>(ps_malloc(overlayCap));
            ov.spanMin = static_cast<int16_t *>(allocPreferInternal(h * sizeof(int16_t)));
            ov.spanMax = static_cast<int16_t *>(allocPreferInternal(h * sizeof(int16_t)));
            ov.rowBlocks = static_cast<uint32_t *>(allocPreferInternal(h * sizeof(uint32_t)));
        }
    }
    bool overlayOk = true;
    for (auto &ov : overlays) {
        overlayOk = overlayOk && ov.buf != nullptr && ov.spanMin != nullptr && ov.spanMax != nullptr &&
                    ov.rowBlocks != nullptr;
    }
    bool pipelineOk = true;
    for (int i = 0; i < 2; i++) {
        pipelineOk = pipelineOk && bandBuf[i] != nullptr && bandReady[i] != nullptr && bandFree[i] != nullptr;
    }
    if (halfBuf == nullptr) {
        pipelineOk = false;
    }
    if (!pipelineOk || !overlayOk) {
        log_e("SleepAnimation: buffer allocation failed (bandBuf=%p/%p overlayOk=%d)", bandBuf[0], bandBuf[1], overlayOk);
        return;
    }
    // Reset the pipeline: both cursors to slot 0, any signal left over from a
    // previous run drained, both slots marked free. DefaultUI stops and
    // restarts the animation on every standby transition, so a stale bandReady
    // or a cursor left on slot 1 would desynchronise the two tasks and push a
    // band that was never rendered.
    renderSlot = 0;
    for (int i = 0; i < 2; i++) {
        while (xSemaphoreTake(static_cast<SemaphoreHandle_t>(bandReady[i]), 0) == pdTRUE) {
        }
        while (xSemaphoreTake(static_cast<SemaphoreHandle_t>(bandFree[i]), 0) == pdTRUE) {
        }
        xSemaphoreGive(static_cast<SemaphoreHandle_t>(bandFree[i]));
    }
    initializedAnimId = -1; // force the animation's init on the render task
    running = true;
    stopped = false;
    TaskHandle_t handle = nullptr;
    // Core 1, SAME priority as the UI task: FreeRTOS round-robins equal
    // priorities every tick, so LVGL's touch poll stays responsive even when
    // a frame overruns its budget. At priority 2 the animation preempted the
    // UI task and left it ~1 ms per 33 ms frame — standby taps took seconds
    // to register. In standby the UI task is nearly idle, so the animation
    // still gets almost the whole core; on active screens (all-screens mode)
    // it gracefully drops frames instead of starving input.
    // 8 KB stack: renderFrame itself is lean, but log_i's float formatting and
    // the esp_lcd draw path both burn stack; 4 KB was within canary distance.
    if (xTaskCreatePinnedToCore(taskEntry, "SleepAnim", 8192, this, 1, &handle, 1) != pdPASS) {
        log_e("SleepAnimation: task creation failed");
        running = false;
        stopped = true;
        return;
    }
    taskHandle = handle;

    // Push task on core 0 at priority 2. It must sit BELOW
    // Controller::loopLogicTask (core 0, priority 3) so animation work can
    // never preempt the control path, and above the default-priority-1 tasks
    // that share core 0 (Arduino loop, WiFi events, AsyncTCP), none of which
    // are time-critical. Core 1 is left exactly as it was: the render task
    // stays at priority 1 alongside the UI task, which is deliberate (see the
    // note above -- priority 2 there starved touch input).
    TaskHandle_t push = nullptr;
    pushStopped = false;
    if (xTaskCreatePinnedToCore(pushTaskEntry, "SleepPush", 4096, this, 2, &push, 0) != pdPASS) {
        log_e("SleepAnimation: push task creation failed");
        pushStopped = true;
        running = false;
        stopped = true;
        return;
    }
    pushHandle = push;
    log_i("SleepAnimation: started (%dx%d), push task on core 0", w, h);
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
    // The push task blocks on bandReady, so it needs a wakeup to observe
    // !running. Give both slots: whichever it is waiting on releases it.
    for (int i = 0; i < 2; i++) {
        if (bandReady[i] != nullptr) {
            xSemaphoreGive(static_cast<SemaphoreHandle_t>(bandReady[i]));
        }
    }
    const unsigned long pushDeadline = millis() + 500;
    while (!pushStopped && millis() < pushDeadline) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// Horizontal extent of the inscribed circle for each band. The panel is round:
// pixels outside the circle are physically not there, so pushing them spends
// PSRAM bandwidth -- the scarcest thing in this pipeline -- on invisible
// output. One rectangle per band, taken from the widest row it contains,
// because pushColors takes a rectangle and esp_lcd_panel_draw_bitmap has no
// stride parameter (verified in esp_lcd_panel_ops.h:59) so a sub-window of a
// full-width buffer would walk off each row's end into the next row's data.
void SleepAnimation::computeChords(int w, int h) {
    const float cx = (w - 1) * 0.5f;
    const float cy = (h - 1) * 0.5f;
    const float r = (w < h ? w : h) * 0.5f;
    const int bands = (h + BAND_H - 1) / BAND_H;
    for (int b = 0; b < bands && b < MAX_BANDS; b++) {
        const int y0 = b * BAND_H;
        const int y1 = (y0 + BAND_H < h ? y0 + BAND_H : h) - 1;
        // Widest row in the band is the one nearest the vertical centre.
        float dy = 0.0f;
        if (cy < y0) {
            dy = y0 - cy;
        } else if (cy > y1) {
            dy = cy - y1;
        }
        int x0 = 0, x1 = w;
        const float inside = r * r - dy * dy;
        if (inside > 0.0f) {
            const float half = sqrtf(inside);
            x0 = static_cast<int>(cx - half);
            x1 = static_cast<int>(cx + half) + 1;
            if (x0 < 0) {
                x0 = 0;
            }
            if (x1 > w) {
                x1 = w;
            }
        }
        // Keep the start even and the width even: the animations' paired
        // 32-bit stores assume 4-byte alignment, and the packing memmove below
        // is cheaper on aligned words.
        x0 &= ~1;
        if ((x1 - x0) & 1) {
            x1++;
        }
        if (x1 > w) {
            x1 = w;
        }
        bandX0[b] = static_cast<int16_t>(x0);
        bandX1[b] = static_cast<int16_t>(x1);
    }
}

void SleepAnimation::pushTaskEntry(void *arg) {
    auto *self = static_cast<SleepAnimation *>(arg);
    self->pushLoop();
    self->pushStopped = true;
    vTaskDelete(nullptr);
}

void SleepAnimation::pushLoop() {
    int slot = 0;
    while (running) {
        if (xSemaphoreTake(static_cast<SemaphoreHandle_t>(bandReady[slot]), pdMS_TO_TICKS(200)) != pdTRUE) {
            continue; // render task idle or stopping; re-check running
        }
        if (!running) {
            break;
        }
        const PushJob job = pushJob[slot];
#ifdef GM_ANIM_BENCH
        const int64_t t0 = esp_timer_get_time();
#endif
        display->pushColors(job.x0, job.y0, job.x1, job.y1, bandBuf[slot]);
#ifdef GM_ANIM_BENCH
        accPushUs += static_cast<uint64_t>(esp_timer_get_time() - t0);
#endif
        xSemaphoreGive(static_cast<SemaphoreHandle_t>(bandFree[slot]));
        slot ^= 1;
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
    if (ov.buf == nullptr || ov.spanMin == nullptr || ov.spanMax == nullptr || ov.rowBlocks == nullptr) {
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
        uint32_t blocks = 0;
        const int sy = y + yoff;
        if (sy >= 0 && sy < h) {
            const uint8_t *a = ov.buf + (static_cast<size_t>(sy) * w + xoff) * 3 + 2;
            for (int x = 0; x < panelW; x++, a += 3) {
                if (*a != 0) {
                    if (mn < 0) {
                        mn = static_cast<int16_t>(x);
                    }
                    mx = static_cast<int16_t>(x);
                    blocks |= 1u << (x >> OVERLAY_BLOCK_SHIFT);
                }
            }
        }
        ov.spanMin[y] = mn;
        ov.spanMax[y] = mx;
        ov.rowBlocks[y] = blocks;
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
#ifdef GM_ANIM_BENCH
        const uint32_t frameUs = static_cast<uint32_t>(esp_timer_get_time() - frameStart);
        accTotalUs += frameUs;
        accFrames++;
        if (frameUs > accMaxTotalUs) {
            accMaxTotalUs = frameUs;
        }
        benchTick();
#endif

        const unsigned long now = millis();
        if (now - fpsWindowStart >= 10000) {
            log_i("SleepAnimation: %.1f fps", fpsFrames * 1000.0f / (now - fpsWindowStart));
            fpsFrames = 0;
            fpsWindowStart = now;
        }

        int fps = maxFps.load();
        fps = fps < 5 ? 5 : (fps > 60 ? 60 : fps);
        const int64_t targetFrameUs = 1000000 / fps;
        const int64_t elapsed = esp_timer_get_time() - frameStart;
        const int64_t remaining = targetFrameUs - elapsed;
        // Always yield at least one full tick so the UI task keeps polling
        // touch even when a frame overruns its budget.
        TickType_t ticks = pdMS_TO_TICKS(remaining > 1000 ? remaining / 1000 : 1);
        vTaskDelay(ticks > 0 ? ticks : 1);
    }
}

#ifdef GM_ANIM_BENCH
void SleepAnimation::benchTick() {
    const unsigned long now = millis();
    if (benchResetPending.exchange(false)) {
        // Applied here, on the render task, so no dwell is half-recorded and
        // the reader never sees a torn benchDone[].
        for (int i = 0; i < BENCH_MAX_ANIMS; i++) {
            benchDone[i] = BenchResult{};
        }
        accBandUs = accBlendUs = accPushUs = accTotalUs = accWaitUs = accPackUs = 0;
        accSpanPx = accBlendPx = 0;
    accSpanPx = accBlendPx = 0;
        accFrames = 0;
        accMaxTotalUs = 0;
        accBandLockedUs = 0;
        accBandLockedRows = accBandRows = 0;
        benchPasses = 0;
        benchDwellStart = now;
        uint8_t p[4];
        bg_parse_params(nullptr, 0, p);
        animParams.store(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24));
        animId.store(0);
        log_i("animbench: results cleared, sweep restarted");
        return;
    }
    if (benchDwellStart == 0) {
        benchDwellStart = now;
        return;
    }
    if (now - benchDwellStart >= BENCH_DWELL_MS) {
        benchFinishDwell();
    }
}

void SleepAnimation::benchFinishDwell() {
    const int id = animId.load();
    const unsigned long elapsedMs = millis() - benchDwellStart;
    if (id >= 0 && id < BENCH_MAX_ANIMS && accFrames > 0) {
        BenchResult &r = benchDone[id];
        r.frames = accFrames;
        r.bandUs = static_cast<uint32_t>(accBandUs / accFrames);
        r.blendUs = static_cast<uint32_t>(accBlendUs / accFrames);
        r.pushUs = static_cast<uint32_t>(accPushUs / accFrames);
        r.totalUs = static_cast<uint32_t>(accTotalUs / accFrames);
        r.maxTotalUs = accMaxTotalUs;
        r.waitUs = static_cast<uint32_t>(accWaitUs / accFrames);
        r.packUs = static_cast<uint32_t>(accPackUs / accFrames);
        r.spanPx = static_cast<uint32_t>(accSpanPx / accFrames);
        r.blendPx = static_cast<uint32_t>(accBlendPx / accFrames);
        r.achievedFps = elapsedMs > 0 ? static_cast<uint32_t>(accFrames * 100000ULL / elapsedMs) : 0;
        // Both normalised per row so the locked sample (one band per frame)
        // is directly comparable to the unlocked one (all 30 bands).
        const uint64_t unlockedUs = accBandUs > accBandLockedUs ? accBandUs - accBandLockedUs : 0;
        const uint32_t unlockedRows = accBandRows > accBandLockedRows ? accBandRows - accBandLockedRows : 0;
        r.bandNsPerRow = unlockedRows > 0 ? static_cast<uint32_t>(unlockedUs * 1000ULL / unlockedRows) : 0;
        r.bandLockedNsPerRow =
            accBandLockedRows > 0 ? static_cast<uint32_t>(accBandLockedUs * 1000ULL / accBandLockedRows) : 0;
        r.valid = true; // publish last: readers on other tasks gate on this
        log_i("animbench: %-10s band=%u us blend=%u us push=%u us total=%u us max=%u us fps=%u.%02u",
              bg_animation(id).id, r.bandUs, r.blendUs, r.pushUs, r.totalUs, r.maxTotalUs, r.achievedFps / 100,
              r.achievedFps % 100);
    }

    accBandUs = accBlendUs = accPushUs = accTotalUs = accWaitUs = accPackUs = 0;
    accSpanPx = accBlendPx = 0;
    accFrames = 0;
    accMaxTotalUs = 0;
    accBandLockedUs = 0;
    accBandLockedRows = accBandRows = 0;
    benchDwellStart = millis();

    const int count = bg_animation_count();
    const int next = (id + 1) % count;
    if (next == 0) {
        benchPasses++;
        log_i("animbench: completed sweep %u of all %d animations", benchPasses, count);
    }
    // Each animation is measured at its own documented defaults, so a run is
    // reproducible and comparable against the host harness numbers.
    uint8_t p[4];
    bg_parse_params(nullptr, next, p);
    animParams.store(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
                     (static_cast<uint32_t>(p[3]) << 24));
    animId.store(static_cast<uint8_t>(next));
}
#endif

void SleepAnimation::renderFrame() {
    // Cropping to the round panel's visible chord trades render-side work
    // (packing each band to a tight stride) for push-side work (fewer bytes to
    // PSRAM). Since the push runs on the other core now, that trade only pays
    // when push is the stage setting the pace -- otherwise it adds ~4.6 ms to
    // the critical path to save time on a stage that already has slack.
    //
    // The render task blocking on bandFree IS the signal that push is the
    // bottleneck, so last frame's wait decides this frame's crop. Thresholds
    // are split to damp oscillation around the crossover, where both states
    // are near-optimal anyway.
    if (frameWaitUs > 1500) {
        cropEnabled = true;
    } else if (frameWaitUs < 400) {
        cropEnabled = false;
    }
    frameWaitUs = 0;
    const int w = display->width();
    const int h = display->height();
    const uint32_t tMs = millis();

    const int id = animId.load();
    const uint32_t packed = animParams.load();
    const uint8_t p[4] = {static_cast<uint8_t>(packed & 0xFF), static_cast<uint8_t>((packed >> 8) & 0xFF),
                          static_cast<uint8_t>((packed >> 16) & 0xFF), static_cast<uint8_t>((packed >> 24) & 0xFF)};
    // Half-resolution mode: the animation renders a 240x240 image and each
    // pixel is doubled on the way into the band buffer. Every animation here
    // is a smooth procedural field -- gradients, glows, warped curtains -- with
    // no text and no hard one-pixel detail, so the resolution it is computed at
    // is a quality dial rather than a correctness property. It costs a quarter
    // of the per-pixel work, which is the only thing on the critical path large
    // enough to matter for the heavy animations.
    const bool half = halfRes;
    const int rw = half ? w / 2 : w;
    const int rh = half ? h / 2 : h;
    const BgAnimation &anim = bg_animation(id);
    if (id != initializedAnimId || half != initializedHalf) {
        if (!anim.init(rw, rh)) {
            log_e("SleepAnimation: init failed for animation %d (%s)", id, anim.id);
            running = false;
            return;
        }
        initializedAnimId = id;
        initializedHalf = half;
    }
    BENCH_T0(tSetup);
    anim.frame(tMs, rw, rh, p);
    BENCH_ACC(accBandUs, tSetup);

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

#ifdef GM_ANIM_BENCH
    // Advance which band gets the suspended render (see the note by
    // lockThisBand). h/BAND_H rounded up, so the last short band is included.
    benchLockBand = (benchLockBand + 1) % static_cast<uint32_t>((h + BAND_H - 1) / BAND_H);
#endif
    for (int y0 = 0; y0 < h && running; y0 += BAND_H) {
        const int rows = (y0 + BAND_H <= h) ? BAND_H : (h - y0);
        // Wait for the push task to finish with this slot. On the first band
        // of a frame this is normally already free; mid-frame it is where the
        // render task blocks if band+blend is faster than push, which is
        // exactly the intended behaviour -- the pipeline runs at the slower
        // stage's rate rather than the sum of both.
        const int64_t tWait = esp_timer_get_time();
        const bool gotSlot =
            xSemaphoreTake(static_cast<SemaphoreHandle_t>(bandFree[renderSlot]), pdMS_TO_TICKS(1000)) == pdTRUE;
        const uint32_t waitUs = static_cast<uint32_t>(esp_timer_get_time() - tWait);
        frameWaitUs += waitUs;
#ifdef GM_ANIM_BENCH
        accWaitUs += waitUs;
#endif
        if (!gotSlot) {
            log_w("SleepAnimation: push task stalled, dropping frame");
            return;
        }
        uint16_t *const band = bandBuf[renderSlot];
        // Bench builds render ONE band per frame with the scheduler suspended
        // on this core. Aurora measures ~82 CPU cycles/pixel for a loop body
        // that looks like it should run in far less, and its max frame is 1.8x
        // its mean -- both consistent with the render task being preempted
        // mid-band rather than the loop being slow. The band timer is wall
        // clock, so preemption lands inside it and is indistinguishable from
        // compute. Task switches cannot happen inside a suspended section, so
        // the gap between per-row locked and per-row unlocked cost IS the
        // preemption share. Interrupts still run, so this bounds the effect
        // rather than eliminating it. One band only: suspending for a whole
        // frame would starve LVGL.
        //
        // WHICH band rotates every frame. Locking band 0 always confounds
        // preemption with content: animations whose cost varies down the
        // screen (steam rises from the bottom, fireflies are sparse, lava
        // blobs drift) make the top band cheap for reasons that have nothing
        // to do with the scheduler. The first cut of this measurement locked
        // band 0 and reported steam at "+191% preemption" purely from that.
        // Rotating means every band is locked equally often across a dwell, so
        // both populations cover the same pixels.
        const bool lockThisBand = GM_BENCH_LOCK_ONE_BAND && (y0 / BAND_H) == static_cast<int>(benchLockBand);
        BENCH_T0(tBand);
        if (half) {
            // Render rows/2 half-width rows, then expand 2x in both axes.
            const int hrows = rows >> 1;
            if (lockThisBand) {
                vTaskSuspendAll();
                anim.band(halfBuf, y0 >> 1, hrows, rw, tMs, p);
                xTaskResumeAll();
            } else {
                anim.band(halfBuf, y0 >> 1, hrows, rw, tMs, p);
            }
            for (int sr = 0; sr < hrows; sr++) {
                const uint16_t *__restrict src = halfBuf + static_cast<size_t>(sr) * rw;
                uint32_t *__restrict d0 = reinterpret_cast<uint32_t *>(band + static_cast<size_t>(sr * 2) * w);
                // Each source pixel becomes a pair, so one 32-bit store emits
                // both copies at once.
                for (int i = 0; i < rw; i++) {
                    const uint32_t v = src[i];
                    d0[i] = v | (v << 16);
                }
                // The second output row is identical; copy words rather than
                // re-running the expansion.
                uint32_t *__restrict d1 = reinterpret_cast<uint32_t *>(band + static_cast<size_t>(sr * 2 + 1) * w);
                for (int i = 0; i < rw; i++) {
                    d1[i] = d0[i];
                }
            }
        } else if (lockThisBand) {
            vTaskSuspendAll();
            anim.band(band, y0, rows, w, tMs, p);
            xTaskResumeAll();
        } else {
            anim.band(band, y0, rows, w, tMs, p);
        }
        BENCH_ACC(accBandUs, tBand);
#ifdef GM_ANIM_BENCH
        accBandRows += static_cast<uint32_t>(rows);
        if (lockThisBand) {
            accBandLockedUs += static_cast<uint64_t>(esp_timer_get_time() - tBand);
            accBandLockedRows += static_cast<uint32_t>(rows);
        }
#endif
        BENCH_T0(tBlend);
        for (int y = y0; y < y0 + rows; y++) {
            if (ov != nullptr && ov->spanMin[y] >= 0) {
                // Composite the standby widgets over the plasma (span-limited:
                // only pixels the snapshot actually covers).
                const int spanLo = ov->spanMin[y];
                const int spanHi = ov->spanMax[y];
                // Only 37% of the pixels between spanMin and spanMax are
                // actually non-transparent -- the widgets are scattered across
                // the row, and the span is just their bounding extent. Reading
                // the other 63% cost more than blending them: the overlay is
                // three unaligned byte loads per pixel out of PSRAM, measured
                // at 13.2 MB/s and ~59 cycles per span pixel. rowBlocks marks
                // which 32-pixel blocks contain any alpha at all, built during
                // the alpha scan that publishOverlay already runs, so the
                // render task can skip empty stretches without touching them.
                uint32_t blocks = ov->rowBlocks[y];
                while (blocks != 0) {
                    const int b = __builtin_ctz(blocks);
                    blocks &= blocks - 1;
                    int x0 = b << OVERLAY_BLOCK_SHIFT;
                    int x1 = x0 + (1 << OVERLAY_BLOCK_SHIFT) - 1;
                    if (x0 < spanLo) {
                        x0 = spanLo;
                    }
                    if (x1 > spanHi) {
                        x1 = spanHi;
                    }
                    if (x1 < x0) {
                        continue;
                    }
                const uint8_t *px = ov->buf + (static_cast<size_t>(y + ovYoff) * ov->w + (x0 + ovXoff)) * 3;
                uint16_t *dst = band + static_cast<size_t>(y - y0) * w + x0;
#ifdef GM_ANIM_BENCH
                accSpanPx += static_cast<uint32_t>(x1 - x0 + 1);
#endif
                for (int x = x0; x <= x1; x++, px += 3, dst++) {
                    const uint8_t a = px[2];
                    if (a == 0) {
                        continue;
                    }
#ifdef GM_ANIM_BENCH
                    accBlendPx++;
#endif
                    // LV_IMG_CF_TRUE_COLOR_ALPHA @16bpp, LV_COLOR_16_SWAP=0:
                    // little-endian RGB565 followed by an alpha byte.
                    const uint16_t c = static_cast<uint16_t>(px[0] | (px[1] << 8));
                    *dst = (a == 255) ? c : blend565(c, *dst, a);
                }
                }
            }
        }
        BENCH_ACC(accBlendUs, tBlend);

        // Compact the band to just the columns the round panel actually shows.
        // Rows are written full-width by the animations; here each row's
        // visible span is moved down to a tight [0, cw) stride so pushColors
        // can take it as a rectangle. The move is always backwards within the
        // same buffer (dst offset r*cw <= src offset r*w + x0 for every r), so
        // it is safe in place and needs no second buffer.
        const int bi = y0 / BAND_H;
        const int cx0 = cropEnabled ? bandX0[bi] : 0;
        const int cx1 = cropEnabled ? bandX1[bi] : w;
        const int cw = cx1 - cx0;
        BENCH_T0(tPack);
        if (cw < w) {
            // Explicit forward word copy, NOT memmove. The regions overlap
            // (same buffer) so memmove is the only correct libc call, and
            // newlib's memmove takes a byte-at-a-time path for overlap --
            // measured at 23 MB/s, which cost 18 ms/frame and cancelled the
            // entire saving the crop was meant to produce. Forward is provably
            // safe here because the destination is always below the source
            // (r*cw <= r*w + cx0 for every r, since cw <= w), and computeChords
            // forces cx0 and cw even so both pointers stay 4-byte aligned:
            // two pixels move per store.
            for (int r = 0; r < rows; r++) {
                uint32_t *__restrict dst = reinterpret_cast<uint32_t *>(band + static_cast<size_t>(r) * cw);
                const uint32_t *__restrict src =
                    reinterpret_cast<const uint32_t *>(band + static_cast<size_t>(r) * w + cx0);
                const int n = cw >> 1;
                int i = 0;
                // 8 pixels per iteration: the copy is load-use bound, so
                // batching the loads hides their latency behind each other
                // instead of stalling once per word.
                for (; i + 4 <= n; i += 4) {
                    const uint32_t a = src[i], b = src[i + 1], c = src[i + 2], d = src[i + 3];
                    dst[i] = a;
                    dst[i + 1] = b;
                    dst[i + 2] = c;
                    dst[i + 3] = d;
                }
                for (; i < n; i++) {
                    dst[i] = src[i];
                }
            }
        }
        BENCH_ACC(accPackUs, tPack);

        // pushColors' width/height params are actually END coordinates — they
        // pass through unchanged to esp_lcd_panel_draw_bitmap (exclusive end).
        // Passing dimensions here asserted in rgb_panel_draw_bitmap on the
        // second band (y_start==y_end) and boot-looped sleep3/sleep4.
        pushJob[renderSlot] = {static_cast<int16_t>(cx0), static_cast<int16_t>(y0), static_cast<int16_t>(cx1),
                         static_cast<int16_t>(y0 + rows)};
        xSemaphoreGive(static_cast<SemaphoreHandle_t>(bandReady[renderSlot]));
        renderSlot ^= 1;
    }
}

#endif // GAGGIMATE_SIM
