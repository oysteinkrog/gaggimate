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
    void setMaxFps(uint8_t fps) { maxFps.store(fps); }

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
        uint32_t achievedFps = 0; // x100, so 2997 == 29.97 fps
    };
    // Snapshot of every completed dwell. Safe to read from another task: each
    // entry is only written once, before valid flips true.
    const BenchResult *benchResults() const { return benchDone; }
    int benchCurrentAnim() const { return animId.load(); }
    uint32_t benchPassCount() const { return benchPasses; }
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
    };

    static void taskEntry(void *arg);
    void renderLoop();
    void renderFrame();

    Display *display = nullptr;
    void *taskHandle = nullptr;
    std::atomic<bool> running{false};
    std::atomic<bool> stopped{true};

    uint16_t *band = nullptr; // one horizontal band of RGB565 pixels

    // Animation selection; id and params may tear against each other for one
    // frame, which is harmless. Packed params: p[i] = (word >> 8*i) & 0xFF.
    std::atomic<uint8_t> animId{0};
    std::atomic<uint32_t> animParams{0};
    std::atomic<uint8_t> maxFps{30};
    int initializedAnimId = -1; // last id whose init() ran on the render task

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
    uint32_t accFrames = 0;
    uint32_t accMaxTotalUs = 0;
    unsigned long benchDwellStart = 0;
    uint32_t benchPasses = 0; // completed sweeps of the whole registry
    BenchResult benchDone[BENCH_MAX_ANIMS];

    void benchTick();      // called once per frame from renderLoop
    void benchFinishDwell(); // records the current animation and advances
#endif
};

#ifdef GM_ANIM_BENCH
// The running instance, so the web plugin can publish results without the
// whole UI object graph being reachable from it. Null until start() runs.
SleepAnimation *sleep_animation_bench_instance();
#endif

#endif // GAGGIMATE_SIM

#endif // SLEEPANIMATION_H
