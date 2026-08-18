#ifndef GAGGIMATE_SIM

#include "SleepAnimation.h"
#include <Arduino.h>
#include <display/drivers/common/Display.h>
#include <display/ui/default/bganim/BgAnim.h>
#include <esp32s3/rom/cache.h> // Cache_WriteBack_Addr / Cache_Invalidate_Addr around the direct push
#include <esp_async_memcpy.h>
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
// Rows rendered/pushed per chunk, and how many of those chunks are in flight.
// Push cost is per byte rather than per call, so band height does not change
// what a frame costs to push -- but it does set the handoff count, and 8 rows
// (60 cross-core semaphore round trips per frame instead of 30) cost ~1.3 ms
// and dropped plasma 43.0 -> 40.6 fps.
//
// 8 rows across two slots is chosen against internal SRAM, which is the
// binding constraint: internal SRAM is what WiFi/BLE/TLS draw from at runtime,
// and the web server needs a contiguous 2,872 B of it per send round. 8 x 2 is
// 15,360 B; the handoff cost above is the price of the row count, and the slot
// count is argued separately at NUM_SLOTS in the header.
//
// Note on the fps figure quoted above: it comes from a 16-vs-8 bench, which is
// where the 30-vs-60 handoff counts come from. The 12-vs-8 step this constant
// actually took has never been benched directly; scaling the measured number
// linearly puts it nearer 0.9 ms than 1.3 ms, so 40.6 fps is a conservative
// floor rather than a measurement of the current configuration.
constexpr int BAND_H = 8;
// Headroom for the snapshot's ext draw size (shadows etc. extend the render
// area past the object on every side).
constexpr int OVERLAY_EXT_MARGIN = 16;
// Granularity of the overlay occupancy bitmap: 1 << 5 = 32 pixels per bit.
constexpr int OVERLAY_BLOCK_SHIFT = 5;
// Scrim grid resolution: 1 << 2 = one cell per 4x4 panel pixels.
constexpr int SCRIM_SHIFT = 2;
// How far the halo reaches past the outermost widget pixel, in cells: two
// 3-wide max passes carry coverage two cells out, and the 3-tap smoothing pass
// carries a fraction of it one further. 3 cells is 12 pixels.
constexpr int SCRIM_REACH_CELLS = 3;

// One separable 3-tap pass over a byte grid, either a max (dilate) or a
// 1-2-1 average (smooth), with the edges clamped rather than wrapped.
//
// `lineStep`/`step` are what let one function do both directions: horizontally
// a line is a row (lineStep = grid width, step = 1), vertically a line is a
// column (lineStep = 1, step = grid width). Six calls build the whole field, so
// this is the only place the halo shape is defined.
void scrimTap3(const uint8_t *src, uint8_t *dst, int lines, int n, int lineStep, int step, bool useMax) {
    for (int l = 0; l < lines; l++) {
        const uint8_t *sp = src + static_cast<size_t>(l) * lineStep;
        uint8_t *dp = dst + static_cast<size_t>(l) * lineStep;
        for (int i = 0; i < n; i++) {
            const int a = sp[static_cast<size_t>(i > 0 ? i - 1 : 0) * step];
            const int b = sp[static_cast<size_t>(i) * step];
            const int c = sp[static_cast<size_t>(i < n - 1 ? i + 1 : n - 1) * step];
            int v;
            if (useMax) {
                v = a > b ? a : b;
                if (c > v) {
                    v = c;
                }
            } else {
                v = (a + 2 * b + c) >> 2;
            }
            dp[static_cast<size_t>(i) * step] = static_cast<uint8_t>(v);
        }
    }
}

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Per-channel RGB565 alpha blend (a: 0..255 foreground opacity).
// always_inline rather than plain inline: at -O2 GCC has repeatedly declined to
// inline same-file helpers in this codebase, and a real call in a per-pixel loop
// costs a windowed-ABI register rotation on top of the call itself. The /255
// divisions are not divisions -- GCC strength-reduces each to a multiply-high
// and a shift, confirmed in the disassembly.
__attribute__((always_inline)) inline uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t a) {
    // One lane per channel, because the packed red+blue lane this replaces was
    // wrong. That form multiplied (c & 0xF81F) by an 8-bit alpha and relied on
    // the two fields staying clear of each other. They do not: blue's product
    // reaches 31 * 256 = 7936, thirteen bits wide, while red sits only eleven
    // bits above it, so blue's top two bits land inside red's field and red's
    // bottom two land inside blue's. Red survives -- the intrusion is worth at
    // most 3 out of the 256 it is about to be divided by -- but blue comes out
    // as blue + ((red_product & 3) << 3) mod 32, wrong for 75% of alpha values
    // and wrapped low nearly every time. Exhaustively: 50% of all
    // (fg, bg, alpha) triples came out wrong, worst case blue off by 24 of 31.
    //
    // On white text over a dark background that is red and green at full
    // coverage and blue at nothing, i.e. a yellow rim on every antialiased
    // pixel. The trick is sound with a 5-bit alpha (31 * 32 = 992, ten bits,
    // clear of red); it did not survive alpha being promoted to eight bits.
    //
    // Only edge pixels arrive here at all -- the caller stores fully opaque
    // pixels directly and skips fully transparent ones -- so this function IS
    // the antialiasing, and getting it wrong shows up nowhere else.
    //
    // Scaling by 256 rather than 255 keeps the divide a shift; each lane now
    // owns its whole 32-bit word, so the bound that matters is only that a
    // single field cannot overflow it. Red is the widest at 0xF800 * 256 =
    // 16,252,928, well inside 32 bits.
    const uint32_t inv = 256u - a;
    const uint32_t r = (((fg & 0xF800u) * a) + ((bg & 0xF800u) * inv)) >> 8;
    const uint32_t g = (((fg & 0x07E0u) * a) + ((bg & 0x07E0u) * inv)) >> 8;
    const uint32_t b = (((fg & 0x001Fu) * a) + ((bg & 0x001Fu) * inv)) >> 8;
    return static_cast<uint16_t>((r & 0xF800u) | (g & 0x07E0u) | (b & 0x001Fu));
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
    // Until the animation has covered the screen once, the rows an interlaced
    // frame skips still hold the previous screen's pixels, so the first frames
    // go out whole.
    warmupFrames.store(3);
    const int w = display->width();
    const int h = display->height();
    for (int i = 0; i < NUM_SLOTS; i++) {
        if (bandBuf[i] == nullptr) {
            // 64-byte aligned and DMA-capable, because on the direct path these
            // are the transfer source and esp_async_memcpy rejects anything
            // else. The fallback keeps the ordinary push path working if the
            // aligned allocator cannot find a contiguous block.
#ifdef GM_ANIM_BENCH
            // Only the bench build can reach the GDMA push path (benchSetDma is
            // compiled out otherwise), and only that path needs these to be a
            // legal transfer source.
            bandBuf[i] = static_cast<uint16_t *>(
                heap_caps_aligned_alloc(64, w * BAND_H * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
            if (bandBuf[i] == nullptr)
#endif
            {
                bandBuf[i] = static_cast<uint16_t *>(allocPreferInternal(w * BAND_H * sizeof(uint16_t)));
            }
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
#ifdef GM_ANIM_BENCH
    if (dmaScratch == nullptr) {
        // Diagnostic destination for dmaMode 4 only. PSRAM, 64-byte aligned,
        // band-sized, and read by nothing -- so a transfer into it is
        // indistinguishable from a framebuffer band push except for where the
        // bytes land.
        dmaScratch = static_cast<uint16_t *>(
            heap_caps_aligned_alloc(64, w * BAND_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
#endif
    computeChords(w, h);
    if (overlayCap == 0) {
        overlayCap = static_cast<uint32_t>(w + 2 * OVERLAY_EXT_MARGIN) * (h + 2 * OVERLAY_EXT_MARGIN) * 3;
        for (auto &ov : overlays) {
            // Snapshot pixels only fit in PSRAM (~700 KB each); the tiny span
            // tables prefer SRAM. Read a couple times per frame — well within
            // the PSRAM budget that the LVGL composite path blew.
            ov.buf = static_cast<uint8_t *>(ps_malloc(overlayCap));
            // PSRAM, not SRAM: these are indexed once per row by the
            // composite (spanMin[y], spanMax[y], rowBlocks[y]) -- roughly 1,440
            // reads across a whole frame -- so they are nowhere near a
            // per-pixel path, and 7,680 B of internal DRAM matters far more to
            // the network stack than their latency does here.
            ov.spanMin = static_cast<int16_t *>(ps_malloc(h * sizeof(int16_t)));
            ov.spanMax = static_cast<int16_t *>(ps_malloc(h * sizeof(int16_t)));
            ov.rowBlocks = static_cast<uint32_t *>(ps_malloc(h * sizeof(uint32_t)));
            ov.blendMin = static_cast<int16_t *>(ps_malloc(h * sizeof(int16_t)));
            ov.blendMax = static_cast<int16_t *>(ps_malloc(h * sizeof(int16_t)));
            ov.blendBlocks = static_cast<uint32_t *>(ps_malloc(h * sizeof(uint32_t)));
            ov.scrimW = (w + (1 << SCRIM_SHIFT) - 1) >> SCRIM_SHIFT;
            ov.scrimH = (h + (1 << SCRIM_SHIFT) - 1) >> SCRIM_SHIFT;
            const size_t cells = static_cast<size_t>(ov.scrimW) * ov.scrimH;
            ov.scrimSrc = static_cast<uint8_t *>(ps_malloc(cells));
            ov.scrim = static_cast<uint8_t *>(ps_malloc(cells));
            // Zeroed because a publish only rewrites the cell rows LVGL
            // redrew; every other cell has to start out meaning "no widget
            // here" rather than whatever the allocator handed back.
            if (ov.scrimSrc != nullptr) {
                memset(ov.scrimSrc, 0, cells);
            }
            if (ov.scrim != nullptr) {
                memset(ov.scrim, 0, cells);
            }
            // ov.scrim is the single sentinel the composite and the publish
            // path test, so it must not be non-null unless the whole scrim
            // apparatus is present. The scrim is a refinement, not a
            // requirement: losing it costs legibility on bright themes and
            // nothing else, so it degrades on its own rather than joining
            // overlayOk and taking the animation down with it.
            if (ov.scrimSrc == nullptr || ov.blendMin == nullptr || ov.blendMax == nullptr ||
                ov.blendBlocks == nullptr) {
                free(ov.scrim);
                ov.scrim = nullptr;
            }
        }
        if (scrimTmp == nullptr) {
            scrimTmp = static_cast<uint8_t *>(
                ps_malloc(static_cast<size_t>(overlays[0].scrimW) * overlays[0].scrimH));
        }
        if (scrimTmp == nullptr) {
            for (auto &ov : overlays) {
                free(ov.scrim);
                ov.scrim = nullptr;
            }
        }
    }
    bool overlayOk = true;
    for (auto &ov : overlays) {
        overlayOk = overlayOk && ov.buf != nullptr && ov.spanMin != nullptr && ov.spanMax != nullptr &&
                    ov.rowBlocks != nullptr;
    }
    bool pipelineOk = true;
    for (int i = 0; i < NUM_SLOTS; i++) {
        pipelineOk = pipelineOk && bandBuf[i] != nullptr && bandReady[i] != nullptr && bandFree[i] != nullptr;
    }
    if (halfBuf == nullptr) {
        pipelineOk = false;
    }
    if (!pipelineOk || !overlayOk) {
        log_e("SleepAnimation: buffer allocation failed (bandBuf=%p/%p/%p overlayOk=%d)", bandBuf[0], bandBuf[1],
              bandBuf[2], overlayOk);
        return;
    }
    // Reset the pipeline: both cursors to slot 0, any signal left over from a
    // previous run drained, both slots marked free. DefaultUI stops and
    // restarts the animation on every standby transition, so a stale bandReady
    // or a cursor left on slot 1 would desynchronise the two tasks and push a
    // band that was never rendered.
    renderSlot = 0;
    for (int i = 0; i < NUM_SLOTS; i++) {
        while (xSemaphoreTake(static_cast<SemaphoreHandle_t>(bandReady[i]), 0) == pdTRUE) {
        }
        while (xSemaphoreTake(static_cast<SemaphoreHandle_t>(bandFree[i]), 0) == pdTRUE) {
        }
        xSemaphoreGive(static_cast<SemaphoreHandle_t>(bandFree[i]));
    }
    // Ask the panel for its framebuffer. It may refuse (the probe validates the
    // driver's struct layout and fails closed), in which case the pipeline runs
    // exactly as before through the push task. The engine itself is installed
    // later, on the render task, because esp_intr_alloc binds the completion
    // handler to whichever core calls it and the render task is the one that
    // waits on it.
    dmaActive = false;
    dmaInstallTried = false;
    fbDirect = dmaWanted.load() ? display->directFrameBuffer() : nullptr;
    if (fbDirect != nullptr) {
        // Whatever LVGL last drew is still sitting in dirty cache lines over
        // this region. Those must reach PSRAM before DMA starts writing there,
        // or a later eviction drops a stale line on top of a rendered band.
        Cache_WriteBack_Addr(reinterpret_cast<uint32_t>(fbDirect), static_cast<uint32_t>(w) * h * 2);
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

// Bumped from the completion interrupt, so a plain volatile rather than the
// std::atomic the other counters use: a fetch_add on this target can land in a
// libatomic helper that is not in IRAM, and this handler can run with the flash
// cache disabled. There is one animation instance, and the only readers are the
// bench endpoint and the drain loop in stop().
static volatile uint32_t g_sleepAnimDmaDone = 0;

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
    for (int i = 0; i < NUM_SLOTS; i++) {
        if (bandReady[i] != nullptr) {
            xSemaphoreGive(static_cast<SemaphoreHandle_t>(bandReady[i]));
        }
    }
    const unsigned long pushDeadline = millis() + 500;
    while (!pushStopped && millis() < pushDeadline) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (dmaActive) {
        // A transfer may still be reading a band buffer, and the next start()
        // hands those same buffers straight back to the render task.
        const unsigned long drain = millis() + 200;
        while (dmaIssued.load() != g_sleepAnimDmaDone && millis() < drain) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        if (fbDirect != nullptr && display != nullptr) {
            // DMA wrote the framebuffer behind the cache, so the CPU's view of
            // it is stale but not dirty. When LVGL resumes, a partial redraw
            // writes only its own rectangle, and any 32-byte line it touches is
            // written back whole -- resurrecting old animation pixels in the
            // bytes it did not write. Dropping the lines forces a refetch.
            // Discarding rather than writing back is correct precisely because
            // nothing has CPU-dirtied this region since start() flushed it.
            Cache_Invalidate_Addr(reinterpret_cast<uint32_t>(fbDirect),
                                  static_cast<uint32_t>(display->width()) * display->height() * 2);
        }
        dmaActive = false;
        display->setDirectWriter(false);
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

static bool IRAM_ATTR sleepAnimBandDone(async_memcpy_t, async_memcpy_event_t *, void *arg) {
    g_sleepAnimDmaDone++;
    if (arg == nullptr) {
        return false; // a non-final chunk: nothing to release yet
    }
    BaseType_t woken = pdFALSE;
    // Releases the slot the transfer has finished reading. The render task
    // waits on this same semaphore before refilling that slot, so the transfer
    // overlaps the next band's render and the only synchronisation left in the
    // direct path is one give per band.
    xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(arg), &woken);
    return woken == pdTRUE;
}

#ifdef GM_ANIM_BENCH
uint32_t SleepAnimation::benchDmaCompleted() const { return g_sleepAnimDmaDone; }

bool SleepAnimation::benchBandsInternal() const {
    for (int i = 0; i < NUM_SLOTS; i++) {
        const uintptr_t a = reinterpret_cast<uintptr_t>(bandBuf[i]);
        if (a == 0 || (a >= 0x3C000000u && a < 0x3E000000u)) {
            return false;
        }
    }
    return true;
}
#endif // GM_ANIM_BENCH

namespace {
// esp_intr_alloc binds the handler to whichever core calls it, and there is no
// argument to say otherwise -- so the only way to choose is to call from a task
// already pinned where the interrupt should land. This one-shot task exists for
// that and nothing else.
struct DmaInstallReq {
    async_memcpy_config_t cfg;
    async_memcpy_t handle;
    esp_err_t err;
    SemaphoreHandle_t done;
};

void dmaInstallTaskEntry(void *arg) {
    auto *req = static_cast<DmaInstallReq *>(arg);
    req->err = esp_async_memcpy_install(&req->cfg, &req->handle);
    xSemaphoreGive(req->done);
    vTaskDelete(nullptr);
}
} // namespace

bool SleepAnimation::installDmaOnRenderCore() {
    if (dmaHandle != nullptr) {
        return true;
    }
    if (dmaInstallTried) {
        return false; // do not retry a failed install once a frame
    }
    dmaInstallTried = true;
    async_memcpy_config_t cfg = ASYNC_MEMCPY_DEFAULT_CONFIG();
    // The engine splits a transfer into descriptors of its own internal size,
    // and backlog is how many it can hold. One band is 480 x BAND_H x 2 bytes;
    // size the pool for every slot being in flight at once, with slack.
    cfg.backlog = 64; // descriptors, ~12 B each; far more than the pipeline can have in flight
    cfg.sram_trans_align = 4;
    // 64 is what this tree asked for everywhere, and the register field it lands
    // in documents only 16 and 32 as valid on esp32s3 (gdma_struct.h:212,
    // "2/3:reserved"). Poking the channel back to 32 at runtime changed nothing
    // visible, so this is not the cause of the shear -- but there is no reason
    // to keep programming a reserved value.
    cfg.psram_trans_align = 32;
    async_memcpy_t h = nullptr;
    esp_err_t err = ESP_FAIL;
    // The RGB panel is created from setup(), which Arduino runs on core 1, so
    // the driver's own ISR lives there; the render task is pinned to core 1 too.
    // Installing from here would put a ~1,300/s completion interrupt on exactly
    // the core that has to service the panel's. Install from core 0 instead.
    DmaInstallReq req{cfg, nullptr, ESP_FAIL, xSemaphoreCreateBinary()};
    if (req.done != nullptr) {
        TaskHandle_t installer = nullptr;
        if (xTaskCreatePinnedToCore(dmaInstallTaskEntry, "DmaInstall", 3072, &req, 3, &installer,
                                    DMA_ISR_CORE) == pdPASS) {
            xSemaphoreTake(req.done, pdMS_TO_TICKS(2000));
            err = req.err;
            h = req.handle;
        }
        vSemaphoreDelete(req.done);
    }
    if (err != ESP_OK) {
        log_w("SleepAnimation: async memcpy install failed (%d), using the push task", static_cast<int>(err));
        return false;
    }
    dmaHandle = h;
    return true;
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
        if (job.mode != 0) {
            // esp_lcd takes a rectangle and no stride, so the rows that go out
            // cannot be one call. Mode 2 sends them two at a time -- at half
            // resolution a pair is one source row, contiguous in the
            // framebuffer -- which is half the calls of mode 1 for the same
            // bytes. The rows left alone keep the previous frame.
            const int step = job.mode == 2 ? 2 : 1;
            const int stride = job.x1 - job.x0;
            for (int y = job.y0; y + step <= job.y1; y += step) {
                if ((((job.mode == 2 ? (y >> 1) : y) ^ job.parity) & 1) != 0) {
                    continue;
                }
                display->pushColors(job.x0, static_cast<int16_t>(y), job.x1, static_cast<int16_t>(y + step),
                                    bandBuf[slot] + static_cast<size_t>(y - job.y0) * stride);
            }
        } else {
            display->pushColors(job.x0, job.y0, job.x1, job.y1, bandBuf[slot]);
        }
#ifdef GM_ANIM_BENCH
        accPushUs += static_cast<uint64_t>(esp_timer_get_time() - t0);
#endif
        xSemaphoreGive(static_cast<SemaphoreHandle_t>(bandFree[slot]));
        slot = (slot + 1) % NUM_SLOTS;
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

void SleepAnimation::publishOverlay(int w, int h, int rowY0, int rowY1) {
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
    if (rowY0 < 0) {
        rowY0 = 0;
    }
    if (rowY1 > panelH) {
        rowY1 = panelH;
    }
    const bool doScrim = ov.scrim != nullptr && scrimTmp != nullptr;
    const int sw = ov.scrimW;
    if (doScrim) {
        // Widen the range to whole scrim cells. A cell's value is the peak alpha
        // of the 4 rows it covers, so it can only be rebuilt from all 4 of them
        // -- clearing a cell row and then refilling it from a partial range
        // would drop the coverage the other rows contributed. The extra rows
        // cost one more pass over alpha the buffer already holds, and their span
        // tables come out identical to what was there.
        rowY0 &= ~((1 << SCRIM_SHIFT) - 1);
        rowY1 = (rowY1 + (1 << SCRIM_SHIFT) - 1) & ~((1 << SCRIM_SHIFT) - 1);
        if (rowY1 > panelH) {
            rowY1 = panelH;
        }
        const int cellY0 = rowY0 >> SCRIM_SHIFT;
        const int cellY1 = ((rowY1 + (1 << SCRIM_SHIFT) - 1) >> SCRIM_SHIFT);
        memset(ov.scrimSrc + static_cast<size_t>(cellY0) * sw, 0, static_cast<size_t>(cellY1 - cellY0) * sw);
    }
    for (int y = rowY0; y < rowY1; y++) {
        int16_t mn = -1;
        int16_t mx = -1;
        uint32_t blocks = 0;
        const int sy = y + yoff;
        if (sy >= 0 && sy < h) {
            const uint8_t *a = ov.buf + (static_cast<size_t>(sy) * w + xoff) * 3 + 2;
            uint8_t *cell = doScrim ? ov.scrimSrc + static_cast<size_t>(y >> SCRIM_SHIFT) * sw : nullptr;
            for (int x = 0; x < panelW; x++, a += 3) {
                if (*a != 0) {
                    if (mn < 0) {
                        mn = static_cast<int16_t>(x);
                    }
                    mx = static_cast<int16_t>(x);
                    blocks |= 1u << (x >> OVERLAY_BLOCK_SHIFT);
                    if (cell != nullptr) {
                        // Peak, not average: the halo is meant to cover the gaps
                        // between strokes and inside glyph counters, and those
                        // are exactly where an average would fade it out.
                        uint8_t &c = cell[x >> SCRIM_SHIFT];
                        if (*a > c) {
                            c = *a;
                        }
                    }
                }
            }
        }
        ov.spanMin[y] = mn;
        ov.spanMax[y] = mx;
        ov.rowBlocks[y] = blocks;
    }
    if (doScrim) {
        buildScrim(ov, panelW, panelH);
    }
    overlayFront.store(back);
}

// Turns the per-cell coverage in ov.scrimSrc into the halo the composite reads,
// then widens the span tables to cover it.
//
// Whole-grid rather than incremental. The dilate spreads coverage 3 cells in
// every direction, so a partial rebuild would have to run over the refreshed
// rows plus a margin and would still get the seam wrong wherever the margin
// itself was stale. The grid is 120x120, and six passes over it cost far less
// than the single pass over the 230 KB alpha plane that just ran.
void SleepAnimation::buildScrim(Overlay &ov, int panelW, int panelH) {
    const int sw = ov.scrimW;
    const int sh = ov.scrimH;
    // Dilate: two 3-wide max passes per axis, so coverage reaches 2 cells out
    // in every direction and diagonals get the same reach as the axes.
    scrimTap3(ov.scrimSrc, scrimTmp, sh, sw, sw, 1, true);
    scrimTap3(scrimTmp, ov.scrim, sh, sw, sw, 1, true);
    scrimTap3(ov.scrim, scrimTmp, sw, sh, 1, sw, true);
    scrimTap3(scrimTmp, ov.scrim, sw, sh, 1, sw, true);
    // Smooth, so the scrim's own edge is a gradient rather than a visible
    // rectangle sitting on the animation.
    scrimTap3(ov.scrim, scrimTmp, sh, sw, sw, 1, false);
    scrimTap3(scrimTmp, ov.scrim, sw, sh, 1, sw, false);

    // The widened tables. Derived from the glyph tables and the known reach
    // rather than by scanning the halo: a row's halo comes from glyph pixels
    // within SCRIM_REACH_CELLS rows of it, so the union of those rows' spans
    // grown by the reach is guaranteed to contain it. Block bits grow by a whole
    // 32-pixel block on each side, which is coarser than the reach needs but
    // costs only a few extra transparent pixels in the composite.
    const int reach = SCRIM_REACH_CELLS << SCRIM_SHIFT;
    for (int y = 0; y < panelH; y++) {
        int lo = y - reach;
        int hi = y + reach;
        if (lo < 0) {
            lo = 0;
        }
        if (hi > panelH - 1) {
            hi = panelH - 1;
        }
        int mn = -1;
        int mx = -1;
        uint32_t blocks = 0;
        for (int r = lo; r <= hi; r++) {
            if (ov.spanMin[r] < 0) {
                continue;
            }
            int a = ov.spanMin[r] - reach;
            int b = ov.spanMax[r] + reach;
            if (a < 0) {
                a = 0;
            }
            if (b > panelW - 1) {
                b = panelW - 1;
            }
            if (mn < 0 || a < mn) {
                mn = a;
            }
            if (b > mx) {
                mx = b;
            }
            const uint32_t g = ov.rowBlocks[r];
            blocks |= g | (g << 1) | (g >> 1);
        }
        ov.blendMin[y] = static_cast<int16_t>(mn);
        ov.blendMax[y] = static_cast<int16_t>(mx);
        ov.blendBlocks[y] = blocks;
    }
}

void SleepAnimation::taskEntry(void *arg) {
    auto *self = static_cast<SleepAnimation *>(arg);
    self->renderLoop();
    self->stopped = true;
    vTaskDelete(nullptr);
}

void SleepAnimation::renderLoop() {
    // Installed here, not in start(): esp_intr_alloc binds the completion
    // handler to the core that calls it, and this is the task that waits on it.
    if (fbDirect != nullptr && installDmaOnRenderCore()) {
        dmaActive = true;
        // From here the panel's own pushColors must stop trusting its cached
        // view of the framebuffer, because this task is about to start writing
        // it behind the cache.
        display->setDirectWriter(true);
        log_i("SleepAnimation: direct framebuffer push active");
    }
    uint32_t fpsFrames = 0;
    unsigned long fpsWindowStart = millis();
    while (running) {
        const int64_t frameStart = esp_timer_get_time();
        renderFrame();
        // Once per frame, not once per band: every band of a frame must push
        // the same parity or the two halves of the picture drift apart.
        frameParity++;
        const uint32_t warm = warmupFrames.load();
        if (warm > 0) {
            warmupFrames.store(warm - 1);
        }
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
    // benchOnly pins the sweep to a single animation. A full pass is 13 dwells
    // of 6 s, so iterating on one animation's inner loop otherwise costs about
    // 90 s of waiting per measurement, nearly all of it spent measuring the
    // twelve animations that did not change.
    const int pin = benchOnly.load();
    const int next = (pin >= 0 && pin < count) ? pin : (id + 1) % count;
    if (next == 0 || pin >= 0) {
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
        // Hand back the outgoing animation's tables before the incoming one
        // asks for its own. Two reasons, and the second is a correctness one.
        //
        // Memory: animations allocate lazily and used to hold their tables for
        // the rest of the boot, so the fleet's internal-SRAM cost was
        // sum-over-animations against a fixed ceiling. Releasing here caps it
        // at max-over-animations and stops registration order from deciding
        // which animations get SRAM.
        //
        // Correctness: per-row and per-column tables are sized from the w/h of
        // the init() that allocated them, behind an `if (ptr == nullptr)`
        // guard. This branch already fires on a resolution change, but that
        // guard made the re-init a no-op, leaving band() to walk a 240-entry
        // table across 480 columns. Freeing first makes the reallocation real.
        //
        // Safe to free here specifically: this runs on the render task, which
        // is the only task that calls init(), frame() or band().
        // residentAnimId, not initializedAnimId: start() clears the latter to
        // force this branch, and gating on it would skip the release on the
        // first frame after every restart -- leaving init() to no-op against
        // non-null pointers and, after a resolution change, leaving band() to
        // walk tables sized for the previous resolution.
        if (residentAnimId >= 0) {
            const BgAnimation &prev = bg_animation(residentAnimId);
            if (prev.release != nullptr) {
                prev.release();
                residentAnimId = -1;
            }
        }
        // Before init(), not after it succeeds. init() is what allocates, and
        // it can allocate several tables and then fail on a later one, leaving
        // the earlier pointers live. Recording residency only on success would
        // orphan those: the next pass would skip release(), init() would skip
        // reallocating the surviving buffers because they are non-null, and at
        // a larger resolution frame() would write past the end of one sized for
        // the smaller. Marking it resident up front costs nothing when init()
        // succeeds and makes the failure recoverable.
        if (anim.release != nullptr) {
            residentAnimId = id;
        }
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
        // Decided once per band and used twice: the blend skips rows this frame
        // will not push, and the push job carries the parity. Interlacing only
        // applies to the two-task push path -- the direct path writes the
        // framebuffer itself and has no per-row call to skip.
        // One decision per band, read by the render, the blend and the push, so
        // the three cannot disagree about which rows this frame owns. Two things
        // switch it off beyond the feature flag:
        //
        // warmupFrames -- until the animation has covered the screen once, the
        // rows an interlaced frame skips still hold whatever the previous screen
        // left in the framebuffer. Gating only the push would send those rows
        // while the render and blend were still skipping them, which pushes
        // stale pixels: the opposite of what the warm-up is for.
        //
        // An odd row count -- a pair cannot be half a row. The expand loop
        // truncates at rows >> 1 and the push loop stops at y + 2 <= y1, so the
        // odd row would be neither written nor sent. 480/8 leaves no partial
        // band today, so this is a guard rather than a live case.
        const bool oddBand = (rows & 1) != 0;
        const bool bandInterlaced = interlace.load() && !(dmaActive && dmaMode.load() != 0) &&
                                    warmupFrames.load() == 0 && !(half && oddBand);
        const int parityNow = static_cast<int>(frameParity & 1u);
        // At half resolution the unit is a row pair, one source row expanded;
        // anywhere else it is a single row.
        const bool pairMode = bandInterlaced && half;
        const bool renderSkip = pairMode && renderHalf.load();
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
#if GM_BENCH_LOCK_ONE_BAND
        const bool lockThisBand = (y0 / BAND_H) == static_cast<int>(benchLockBand);
#else
        constexpr bool lockThisBand = false;
#endif
        BENCH_T0(tBand);
        if (half) {
            // Render rows/2 half-width rows, then expand 2x in both axes.
            const int hrows = rows >> 1;
            const int srcBase = y0 >> 1;
            // One band() call per source row instead of one for the whole band,
            // so the rows this frame will not push are never computed. The
            // contract takes a row count (BgAnim.h) and the last band of the
            // screen already passes a short one, so this is within it; what it
            // relies on is that no animation carries state from one call to the
            // next, which is true of all thirteen -- each derives its row terms
            // from the absolute y it is handed.
            const bool splitRender = renderSkip && hrows > 0;
            if (lockThisBand) {
                vTaskSuspendAll();
            }
            if (splitRender) {
                for (int sr = 0; sr < hrows; sr++) {
                    if (((srcBase + sr) & 1) != parityNow) {
                        continue;
                    }
                    anim.band(halfBuf + static_cast<size_t>(sr) * rw, srcBase + sr, 1, rw, tMs, p);
                }
            } else {
                anim.band(halfBuf, srcBase, hrows, rw, tMs, p);
            }
            if (lockThisBand) {
                xTaskResumeAll();
            }
            for (int sr = 0; sr < hrows; sr++) {
                if (splitRender && ((srcBase + sr) & 1) != parityNow) {
                    continue; // its pair is not going out, so do not expand it
                }
                const uint16_t *__restrict src = halfBuf + static_cast<size_t>(sr) * rw;
                uint32_t *__restrict d0 = reinterpret_cast<uint32_t *>(band + static_cast<size_t>(sr * 2) * w);
                uint32_t *__restrict d1 = reinterpret_cast<uint32_t *>(band + static_cast<size_t>(sr * 2 + 1) * w);
                // Both output rows in one pass. Each source pixel becomes a
                // pair, so one 32-bit value covers both copies, and writing it
                // to each row costs a second store rather than a second pass --
                // the earlier shape read d0 back to fill d1, which spent a load
                // per output word purely to re-fetch something already in a
                // register. This is the whole frame at half resolution: 240
                // rows x 240 words of avoidable loads.
                for (int i = 0; i < rw; i++) {
                    const uint32_t v = src[i];
                    const uint32_t pair = v | (v << 16);
                    d0[i] = pair;
                    d1[i] = pair;
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
#ifdef GM_ANIM_BENCH
        // Locals, not the uint64_t members: a member increment in the innermost
        // per-pixel loop is two loads, an add-with-carry and two stores, and it
        // pins `this` for the whole loop. Charging that to the blend made the
        // stage look ~2x its real cost and sent an earlier round of work at a
        // memory-layout problem the blend did not have.
        uint32_t spanPxLocal = 0;
        uint32_t blendPxLocal = 0;
#endif
        // Text scrim strength, Q8. Zero whenever the user has it off or the
        // grids could not be allocated, and that zero is what makes the scrim
        // free when unused -- the row loop then walks the narrow glyph spans and
        // the inner loop's scrim branch is never taken.
        const int scrim = (ov != nullptr && ov->scrim != nullptr) ? scrimQ8.load() : 0;
        // Which span tables drive the walk. The halo reaches 12 pixels past the
        // glyphs and into rows that hold no glyph at all, so with the scrim on
        // the widened tables have to be the ones iterated or the halo would be
        // clipped at the glyph bounding span and at 32-pixel block edges.
        const int16_t *rowMin = (ov == nullptr) ? nullptr : (scrim != 0 ? ov->blendMin : ov->spanMin);
        const int16_t *rowMax = (ov == nullptr) ? nullptr : (scrim != 0 ? ov->blendMax : ov->spanMax);
        const uint32_t *rowBlk = (ov == nullptr) ? nullptr : (scrim != 0 ? ov->blendBlocks : ov->rowBlocks);
        // Rows this frame will not push are thrown away, so compositing
        // widgets into them is wasted. Both rows of a pushed pair still need it.
        for (int y = y0; y < y0 + rows; y++) {
            if (bandInterlaced && ((((pairMode ? (y >> 1) : y) ^ parityNow) & 1) != 0)) {
                continue;
            }
            if (ov != nullptr && rowMin[y] >= 0) {
                // Composite the standby widgets over the plasma (span-limited:
                // only pixels the snapshot actually covers).
                const int spanLo = rowMin[y];
                const int spanHi = rowMax[y];
                // One row of the scrim grid, or nullptr when the scrim is off.
                // Hoisted out of the inner loop: the row's cell index does not
                // change across it, and the branch on this pointer is the whole
                // per-pixel cost of the feature when it is disabled.
                const uint8_t *scRow =
                    scrim != 0 ? ov->scrim + static_cast<size_t>(y >> SCRIM_SHIFT) * ov->scrimW : nullptr;
                // Only 37% of the pixels between spanMin and spanMax are
                // actually non-transparent -- the widgets are scattered across
                // the row, and the span is just their bounding extent. Reading
                // the other 63% cost more than blending them: the overlay is
                // three unaligned byte loads per pixel out of PSRAM, measured
                // at 13.2 MB/s and ~59 cycles per span pixel. rowBlocks marks
                // which 32-pixel blocks contain any alpha at all, built during
                // the alpha scan that publishOverlay already runs, so the
                // render task can skip empty stretches without touching them.
                uint32_t blocks = rowBlk[y];
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
                spanPxLocal += static_cast<uint32_t>(x1 - x0 + 1);
#endif
                for (int x = x0; x <= x1; x++, px += 3, dst++) {
                    const uint8_t a = px[2];
                    // LV_IMG_CF_TRUE_COLOR_ALPHA @16bpp, LV_COLOR_16_SWAP=0:
                    // little-endian RGB565 followed by an alpha byte.
                    if (a == 255) {
                        // Opaque: the glyph covers the background outright, so
                        // scrimming it first would be work the next store
                        // throws away. Taken by most of a glyph's interior.
                        *dst = static_cast<uint16_t>(px[0] | (px[1] << 8));
#ifdef GM_ANIM_BENCH
                        blendPxLocal++;
#endif
                        continue;
                    }
                    if (scRow != nullptr) {
                        // Dim toward black before the glyph goes down. This is
                        // the point of the whole mechanism: the pixels that
                        // decide legibility are the transparent ones between
                        // strokes and inside glyph counters, which the blend
                        // below skips entirely, so keying the dim on the pixel's
                        // own alpha would leave exactly the wrong pixels bright.
                        const int dim = (scRow[x >> SCRIM_SHIFT] * scrim) >> 8;
                        if (dim > 0) {
                            *dst = blend565(0, *dst, static_cast<uint8_t>(dim > 255 ? 255 : dim));
                        }
                    }
                    if (a == 0) {
                        continue;
                    }
#ifdef GM_ANIM_BENCH
                    blendPxLocal++;
#endif
                    const uint16_t c = static_cast<uint16_t>(px[0] | (px[1] << 8));
                    *dst = blend565(c, *dst, a);
                }
                }
            }
        }
        BENCH_ACC(accBlendUs, tBlend);
#ifdef GM_ANIM_BENCH
        accSpanPx += spanPxLocal;
        accBlendPx += blendPxLocal;
#endif

        // Compact the band to just the columns the round panel actually shows.
        // Rows are written full-width by the animations; here each row's
        // visible span is moved down to a tight [0, cw) stride so pushColors
        // can take it as a rectangle. The move is always backwards within the
        // same buffer (dst offset r*cw <= src offset r*w + x0 for every r), so
        // it is safe in place and needs no second buffer.
        // The direct path leaves the band full-width and contiguous so it can
        // go out as a single transfer. Cropping would buy back the ~21% of
        // pixels outside the round panel's circle, but each cropped row is a
        // separate run in the framebuffer and would need its own descriptor to
        // step the stride. At 48 MB/s those corners cost less than the
        // descriptors and the extra pack pass would.
        // dmaMode 0 means "use the ordinary two-task push", so the direct path
        // can be turned off at runtime without a reboot -- benchSetDma only
        // takes effect at the next start(), which never happens while a sweep
        // is running.
        const bool directPush = dmaActive && dmaMode.load() != 0;
        const bool crop = cropEnabled && !directPush;
        const int bi = y0 / BAND_H;
        const int cx0 = crop ? bandX0[bi] : 0;
        const int cx1 = crop ? bandX1[bi] : w;
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
        if (directPush) {
            // One contiguous run: full-width rows are adjacent in both the band
            // buffer and the framebuffer. Every operand is 64-byte aligned by
            // construction -- the panel's framebuffer is (the probe checks it),
            // the band buffer is allocated aligned, and both the row stride
            // (480 x 2 = 960 B) and the band size are multiples of 64.
            //
            // Nothing is awaited. The transfer reads the slot on its own time
            // and its completion interrupt gives bandFree[renderSlot], which is
            // exactly what this loop takes before refilling that slot.
            BENCH_T0(tPush);
            const size_t bytes = static_cast<size_t>(w) * rows * 2;
            uint16_t *const dstRow = fbDirect + static_cast<size_t>(y0) * w;
            if (dmaMode.load() == 1) {
                // Bisect mode: same destination, same bytes, ordinary CPU copy,
                // then the identical writeback esp_lcd does on its way out of
                // draw_bitmap. Anything still wrong here is about bypassing the
                // driver, not about GDMA.
                display->lockFrameBuffer();
                memcpy(dstRow, band, bytes);
                Cache_WriteBack_Addr(reinterpret_cast<uint32_t>(dstRow), static_cast<uint32_t>(bytes));
                display->unlockFrameBuffer();
                xSemaphoreGive(static_cast<SemaphoreHandle_t>(bandFree[renderSlot]));
                BENCH_ACC(accPushUs, tPush);
                renderSlot = (renderSlot + 1) % NUM_SLOTS;
                continue;
            }
            // A DMA descriptor tops out at 4092 bytes, so a whole 12-row band
            // (11,520) has to be split. The driver's split point is not
            // necessarily a multiple of psram_trans_align, and a GDMA write to
            // PSRAM that starts unaligned lands at the wrong offset -- which is
            // exactly the shear this produced on the panel, while the identical
            // copy done by the CPU (mode 1) was clean.
            //
            // So chunk it here instead, at 4 rows: 3,840 bytes fits one
            // descriptor, is a multiple of 64, and every chunk's destination
            // (fb + row * 960) is 64-byte aligned too. Only the final chunk
            // carries the semaphore, because GDMA retires transfers in
            // submission order, so its completion means the whole band is done.
            const int mode = dmaMode.load();
            // Mode 4 discriminates "GDMA to PSRAM disturbs the panel" from "GDMA
            // to the framebuffer the panel is scanning disturbs it". Same
            // transfer sizes, same submission rate, same source buffers -- only
            // the destination changes, to a scratch band nothing reads. The
            // screen freezes on whatever the CPU path left there, which makes
            // any shear that does appear unmistakable.
            uint16_t *const dmaDst = (mode == 4 && dmaScratch != nullptr) ? dmaScratch : dstRow;
            const int chunkRows = mode == 3 ? 4 : rows;
            esp_err_t err = ESP_OK;
            for (int r0 = 0; r0 < rows && err == ESP_OK; r0 += chunkRows) {
                const int n = (r0 + chunkRows <= rows) ? chunkRows : (rows - r0);
                const bool last = (r0 + n >= rows);
                dmaIssued++;
                err = esp_async_memcpy(static_cast<async_memcpy_t>(dmaHandle), dmaDst + static_cast<size_t>(r0) * w,
                                       band + static_cast<size_t>(r0) * w, static_cast<size_t>(n) * w * 2,
                                       sleepAnimBandDone, last ? bandFree[renderSlot] : nullptr);
                if (err != ESP_OK) {
                    dmaIssued--;
                }
            }
            (void)bytes;
            if (err != ESP_OK) {
                // Not expected given the alignment guarantees, but a dropped
                // band is a visible tear: fall back to the CPU copy and release
                // the slot here, because no interrupt is coming for it.
                dmaErrors++;
                dmaIssued--;
                display->pushColors(0, y0, w, y0 + rows, band);
                xSemaphoreGive(static_cast<SemaphoreHandle_t>(bandFree[renderSlot]));
            }
            BENCH_ACC(accPushUs, tPush);
        } else {
            const uint8_t pushMode = !bandInterlaced ? 0 : (pairMode ? 2 : 1);
            pushJob[renderSlot] = {static_cast<int16_t>(cx0), static_cast<int16_t>(y0), static_cast<int16_t>(cx1),
                                   static_cast<int16_t>(y0 + rows), pushMode,
                                   static_cast<uint8_t>(parityNow)};
            xSemaphoreGive(static_cast<SemaphoreHandle_t>(bandReady[renderSlot]));
        }
        renderSlot = (renderSlot + 1) % NUM_SLOTS;
    }
}

#endif // GAGGIMATE_SIM
