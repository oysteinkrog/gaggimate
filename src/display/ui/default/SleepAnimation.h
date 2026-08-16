#ifndef SLEEPANIMATION_H
#define SLEEPANIMATION_H

#include <atomic>
#include <stdint.h>

class Display;

#ifdef GAGGIMATE_SIM
// The simulator has no panel/FreeRTOS; stub the whole feature out.
class SleepAnimation {
  public:
    void start(Display *) {}
    void stop() {}
    bool isActive() const { return false; }
    void configure(uint8_t, const uint8_t *) {}
    void setMaxFps(uint8_t) {}
    uint8_t *overlayBackBuffer() { return nullptr; }
    uint32_t overlayCapacity() const { return 0; }
    void publishOverlay(int, int) {}
};
#else

// Procedural background animation engine: renders the selected registry
// animation (see bganim/BgAnim.h) with a dedicated task directly to the panel
// in horizontal bands, bypassing the LVGL draw pipeline (an LVGL full-screen
// composite moves ~4x the PSRAM traffic and starves the RGB scan-out —
// visible as horizontal shaking). The host screen's widgets are alpha-blended
// into each band from an offscreen LVGL snapshot the UI task refreshes
// periodically, so the animation appears BEHIND the normal content while the
// host screen stays the active LVGL screen (touch keeps working). No stored
// assets — everything is generated at runtime, so OTA updates carry the
// whole feature.
class SleepAnimation {
  public:
    SleepAnimation() = default;
    ~SleepAnimation();

    // Starts the render task. No-op if already running or display is null.
    void start(Display *display);
    // Signals the task to exit and blocks briefly until it has stopped.
    void stop();
    bool isActive() const { return running; }

    // Selects which registry animation renders and its 4 params (0-100 each).
    // Safe to call while running — params apply on the next frame, an id
    // change triggers the new animation's lazy init on the render task.
    void configure(uint8_t animId, const uint8_t p[4]);
    // Frame-rate cap (clamped 5-60). Lower caps cut the animation's PSRAM
    // write bandwidth — the tuning lever against scan-out underruns when the
    // panel refresh (pclk) is raised. Applies on the next frame.
#ifdef GM_ANIM_BENCH
    // Inert on the bench, like configure(): DefaultUI re-applies the stored
    // frame cap on every UI pass, and a throttled frame reports the cap
    // instead of what the pipeline actually costs.
    void setMaxFps(uint8_t) {}
#else
    void setMaxFps(uint8_t fps) { maxFps.store(fps); }
#endif

    // Overlay: an LV_IMG_CF_TRUE_COLOR_ALPHA (RGB565 + A8, 3 B/px) snapshot of
    // the standby widgets. Double-buffered: the UI task renders a snapshot
    // into overlayBackBuffer(), then publishOverlay() computes per-row alpha
    // spans and atomically flips which overlay the render task blends from.
    // Returns nullptr when the render task is still reading the back overlay
    // mid-frame (rare, ~20 ms window) — the caller just retries next UI pass.
    uint8_t *overlayBackBuffer();
    uint32_t overlayCapacity() const { return overlayCap; }
    // w/h: the snapshot's actual pixel size (may exceed the panel by the
    // object's ext draw size on each side; the blend centers it).
    void publishOverlay(int w, int h);

#ifdef GM_ANIM_BENCH
    // Bench build only. The render task walks the whole registry, dwelling on
    // each animation for BENCH_DWELL_MS at its default params, and records
    // where the frame time actually went. Timings come from the render task
    // itself, so they are real on-device costs including PSRAM latency and
    // whatever else shares the core -- not a host estimate.
    static constexpr int BENCH_MAX_ANIMS = 32;
    struct BenchResult {
        bool valid = false;
        uint32_t frames = 0;
        // Mean microseconds per frame, split by pipeline stage.
        uint32_t bandUs = 0;  // anim.frame() + anim.band() -- the animation's own cost
        uint32_t blendUs = 0; // overlay composite over the rendered bands
        uint32_t pushUs = 0;  // display->pushColors -- panel/PSRAM write
        uint32_t totalUs = 0; // sum of the above, measured end to end
        uint32_t maxTotalUs = 0;
        uint32_t waitUs = 0; // render task blocked waiting for the push task to free a slot
        uint32_t packUs = 0; // compacting each band to the round panel's visible chord
        uint32_t spanPx = 0;  // overlay pixels read per frame (span-limited)
        uint32_t blendPx = 0; // of those, how many were not fully transparent
        uint32_t achievedFps = 0; // x100, so 2997 == 29.97 fps
        // Nanoseconds per band row, measured with and without the scheduler
        // suspended on this core. Equal means the band cost is real compute;
        // unlocked >> locked means the wall-clock band timer is mostly
        // charging this task for time it spent preempted.
        uint32_t bandNsPerRow = 0;
        uint32_t bandLockedNsPerRow = 0;
    };
    // Snapshot of every completed dwell. Safe to read from another task: each
    // entry is only written once, before valid flips true.
    const BenchResult *benchResults() const { return benchDone; }
    int benchCurrentAnim() const { return animId.load(); }
    uint32_t benchPassCount() const { return benchPasses; }
    // Discards every recorded dwell and restarts the sweep from the first
    // animation. Set from the web task after changing something the timings
    // depend on (the pixel clock), so the next sweep measures the new state
    // instead of averaging across the change.
    void benchRequestReset() { benchResetPending.store(true); }
    void benchSetHalfRes(bool on) { halfRes = on; }
    bool benchHalfRes() const { return halfRes; }
    // Pin the sweep to one animation (-1 sweeps the whole registry), so a
    // change to one inner loop can be measured in one dwell instead of a full
    // 13-animation pass.
    void benchSetOnly(int id) { benchOnly.store(id); }
    int benchGetOnly() const { return benchOnly.load(); }
    // The sweep normally runs uncapped, because a throttled frame reports the
    // cap instead of the cost. This puts the cap back deliberately, for
    // measuring what a bounded duty cycle does to the rest of the system.
    void benchSetMaxFps(uint8_t fps) { maxFps.store(fps); }
    uint8_t benchMaxFps() const { return maxFps.load(); }
    // Direct-to-framebuffer push over GDMA, switchable per frame so both paths
    // can be measured on one flash. Requesting it is not the same as getting
    // it: the panel must hand over its framebuffer and the engine must install.
    void benchSetDma(bool on) { dmaWanted.store(on); }
    bool benchDmaWanted() const { return dmaWanted.load(); }
    // 0 = push task (esp_lcd draw_bitmap), 1 = CPU memcpy straight into the
    // panel's framebuffer, 2 = GDMA straight into it. Mode 1 exists purely to
    // bisect: it bypasses draw_bitmap exactly as mode 2 does but keeps the copy
    // an ordinary CPU one, so a fault that appears in 1 is about bypassing the
    // driver and a fault that appears only in 2 is about the transfer.
    void benchSetDmaMode(int m) { dmaMode.store(m); }
    void benchSetInterlace(bool on) { interlace.store(on); }
    bool benchInterlace() const { return interlace.load(); }
    int benchDmaMode() const { return dmaMode.load(); }
    bool benchDmaActive() const { return dmaActive; }
    uint32_t benchDmaIssued() const { return dmaIssued.load(); }
    uint32_t benchDmaCompleted() const;
    uint32_t benchDmaErrors() const { return dmaErrors.load(); }
    // Where the band buffers actually landed. They are the DMA source, so one
    // of them falling back to PSRAM would mean GDMA reads memory the CPU has
    // only written through the cache.
    uint32_t benchBandAddr(int i) const {
        return (i >= 0 && i < NUM_SLOTS) ? reinterpret_cast<uint32_t>(bandBuf[i]) : 0;
    }
    bool benchBandsInternal() const;
#endif

  private:
    struct Overlay {
        uint8_t *buf = nullptr;
        int w = 0;
        int h = 0;
        // Per PANEL row: first/last column with alpha > 0, or min = -1 for an
        // empty row. Lets the blend skip the ~85% of rows/pixels that are
        // plain plasma.
        int16_t *spanMin = nullptr;
        int16_t *spanMax = nullptr;
        // Bit b set => pixels [b*32, b*32+32) in this row contain some alpha.
        // 32 blocks covers a 1024-wide row, well past this panel.
        uint32_t *rowBlocks = nullptr;
    };

    static void taskEntry(void *arg);
    void renderLoop();
    void renderFrame();

    Display *display = nullptr;
    void *taskHandle = nullptr;
    std::atomic<bool> running{false};
    std::atomic<bool> stopped{true};

    // Band buffers, so the render task can fill one while the push task drains
    // another. band+blend is CPU work touching only internal SRAM; push is a
    // PSRAM write. They contend for almost nothing, so running them on separate
    // cores turns frame time from band+blend+push into max(band+blend, push)
    // plus one band of pipeline latency.
    //
    // Three, not two. Throughput is max(render, push) either way -- extra
    // slots buy nothing in steady state -- but band render time is not uniform
    // across a frame (steam's blobs sit at the bottom, fireflies are sparse at
    // the top) while push time per band is flat. With only two slots the render
    // task blocks the moment it runs ahead, so the frame costs the sum of the
    // per-band maxima rather than the max of the two totals. A third slot
    // absorbs that variance.
    static constexpr int NUM_SLOTS = 3;
    uint16_t *bandBuf[NUM_SLOTS] = {};
    void *bandReady[NUM_SLOTS] = {}; // render -> push, slot has data
    void *bandFree[NUM_SLOTS] = {};  // push -> render, slot is reusable
    // The panel rectangle each queued slot covers. pushColors takes end
    // coordinates, not extents.
    struct PushJob {
        int16_t x0, y0, x1, y1;
        // Which row parity this frame pushes. Only meaningful when interlacing
        // is on; the push task compares it against each absolute row index.
        uint8_t parity;
    };
    PushJob pushJob[NUM_SLOTS] = {};
    int renderSlot = 0; // slot the render task fills next; push task tracks its own
    bool cropEnabled = false;   // crop to the panel's circle only while push is the pacing stage
    // Push every other row, alternating parity each frame. Halves the bytes and
    // the writeback range at the cost of each row refreshing at half the frame
    // rate. Off by default until it has been looked at on the panel.
    std::atomic<bool> interlace{false};
    uint32_t frameParity = 0;
    std::atomic<bool> halfRes{false}; // render at 240x240 and double on the way out
    uint32_t frameWaitUs = 0;   // this frame's total block on the push task, drives cropEnabled
    void *pushHandle = nullptr;
    std::atomic<bool> pushStopped{true};

    // ---- direct-to-framebuffer push ------------------------------------
    // pushColors copies the band into the PSRAM framebuffer with the CPU, and a
    // CPU write to PSRAM on this part costs about twice a read: the 32-byte
    // write-allocate line is fetched before it is overwritten, so a 460 KB
    // frame moves 920 KB of bus traffic. Measured 24 MB/s that way against
    // 48 MB/s for the same bytes over GDMA, which never touches the cache.
    //
    // fbDirect is the panel's own framebuffer, or null when the panel will not
    // hand it over -- in which case dmaActive stays false and the pipeline runs
    // the ordinary push task, unchanged.
    // Off by default: the direct path garbles the panel in practice. Writing
    // the framebuffer behind the cache is only safe if nothing else writes it
    // through the cache, and LVGL still does -- its dirty lines get evicted
    // over DMA-written pixels afterwards. Flushing at start() and invalidating
    // at stop() bounds that at the edges of a run but does nothing during one.
    // Enable with /api/animbench?dma=1 to measure; do not ship it on until the
    // coherency problem is actually solved.
    std::atomic<bool> dmaWanted{false};
    std::atomic<int> dmaMode{2};
    bool dmaActive = false;      // fbDirect resolved AND the engine installed
    uint16_t *fbDirect = nullptr;
    // dmaMode 4 only: a band-sized PSRAM buffer nothing scans out, so the DMA
    // traffic can be reproduced exactly while the framebuffer is left alone.
    uint16_t *dmaScratch = nullptr;
    void *dmaHandle = nullptr;   // async_memcpy_t, installed once from the render task
    bool dmaInstallTried = false;
    std::atomic<uint32_t> dmaIssued{0};
    std::atomic<uint32_t> dmaErrors{0};
    // Core the async-memcpy completion interrupt is bound to. Deliberately not
    // the render core: the RGB panel driver's ISR is on core 1 and must not
    // queue behind ours.
    static constexpr int DMA_ISR_CORE = 0;
    bool installDmaOnRenderCore();

    // Per-band horizontal extent of the panel's inscribed circle. The panel is
    // round, so the corners of the 480x480 rectangle are never visible and
    // pushing them is wasted PSRAM bandwidth. One rectangle per band (the
    // widest row in it), since pushColors takes a rectangle.
    static constexpr int MAX_BANDS = 64; // 480 rows / BAND_H, with headroom
    int16_t bandX0[MAX_BANDS] = {};
    int16_t bandX1[MAX_BANDS] = {};
    void computeChords(int w, int h);
    static void pushTaskEntry(void *arg);
    void pushLoop();

    // Animation selection; id and params may tear against each other for one
    // frame, which is harmless. Packed params: p[i] = (word >> 8*i) & 0xFF.
    std::atomic<uint8_t> animId{0};
    std::atomic<uint32_t> animParams{0};
#ifdef GM_ANIM_BENCH
    // The bench measures what the pipeline can do, so it must not sit against
    // the shipping frame cap -- a throttled frame reports the cap, not the cost.
    std::atomic<uint8_t> maxFps{60};
#else
    std::atomic<uint8_t> maxFps{30};
#endif
    int initializedAnimId = -1; // last id whose init() ran on the render task
    bool initializedHalf = false; // resolution that init() ran at; a change re-inits
    uint16_t *halfBuf = nullptr;  // (w/2)x(BAND_H/2) scratch for half-res rendering

    Overlay overlays[2];
    uint32_t overlayCap = 0;
    std::atomic<int> overlayFront{-1};  // -1 = nothing published yet
    std::atomic<int> overlayInUse{-1};  // overlay the render task reads this frame

#ifdef GM_ANIM_BENCH
    // Accumulators for the dwell in progress; render task only, no locking.
    uint64_t accBandUs = 0;
    uint64_t accBlendUs = 0;
    uint64_t accPushUs = 0;
    uint64_t accTotalUs = 0;
    uint64_t accWaitUs = 0;
    uint64_t accPackUs = 0;
    uint64_t accSpanPx = 0;
    uint64_t accBlendPx = 0;
    uint32_t accFrames = 0;
    uint32_t accMaxTotalUs = 0;
    uint32_t benchLockBand = 0; // which band gets the suspended render, rotates per frame
    uint64_t accBandLockedUs = 0;
    uint32_t accBandLockedRows = 0;
    uint32_t accBandRows = 0;
    unsigned long benchDwellStart = 0;
    uint32_t benchPasses = 0; // completed sweeps of the whole registry
    BenchResult benchDone[BENCH_MAX_ANIMS];
    std::atomic<bool> benchResetPending{false};
    std::atomic<int> benchOnly{-1}; // -1 sweeps the registry; otherwise pin to this id

    void benchTick();      // called once per frame from renderLoop
    void benchFinishDwell(); // records the current animation and advances
#endif
};

#ifdef GM_ANIM_BENCH
// The running instance, so the web plugin can publish results without the
// whole UI object graph being reachable from it. Null until start() runs.
SleepAnimation *sleep_animation_bench_instance();

// Why the animation is or is not running. maintainSleepAnimation() has several
// gates and none of them are visible from outside the UI, which makes a
// silently idle bench impossible to diagnose over the network.
struct BenchGateState {
    bool uiInitialized = false;
    bool blocked = false;
    bool wantAnimation = false;
    bool animActive = false;
    int mode = -1;
    int screen = -1;
    unsigned long lastStartAttempt = 0;
    bool startFailed = false;
};
const BenchGateState &bench_gate_state();
#endif

#endif // GAGGIMATE_SIM

#endif // SLEEPANIMATION_H
