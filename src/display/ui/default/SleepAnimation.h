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
};

#endif // GAGGIMATE_SIM

#endif // SLEEPANIMATION_H
