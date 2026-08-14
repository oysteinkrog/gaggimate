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
    uint8_t *overlayBackBuffer() { return nullptr; }
    uint32_t overlayCapacity() const { return 0; }
    void publishOverlay(int, int) {}
};
#else

// Procedural standby animation (classic palette-cycled plasma), rendered by a
// dedicated task directly to the panel in horizontal bands, bypassing the LVGL
// draw pipeline (an LVGL full-screen composite moves ~4x the PSRAM traffic and
// starves the RGB scan-out — visible as horizontal shaking). The standby
// screen's widgets (clock, status, icons) are alpha-blended into each band
// from an offscreen LVGL snapshot the UI task refreshes periodically, so the
// animation appears BEHIND the normal standby content while the standby screen
// stays the active LVGL screen (tap-to-wake keeps working). No stored assets —
// everything is generated at runtime, so OTA updates carry the whole feature.
class SleepAnimation {
  public:
    SleepAnimation() = default;
    ~SleepAnimation();

    // Starts the render task. No-op if already running or display is null.
    void start(Display *display);
    // Signals the task to exit and blocks briefly until it has stopped.
    void stop();
    bool isActive() const { return running; }

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
    void renderFrame(uint32_t frame);
    void buildLuts();

    Display *display = nullptr;
    void *taskHandle = nullptr;
    std::atomic<bool> running{false};
    std::atomic<bool> stopped{true};

    uint16_t *band = nullptr;    // one horizontal band of RGB565 pixels
    int16_t *sinLut = nullptr;   // 1024-entry sine table
    uint16_t *palette = nullptr; // 256-entry RGB565 palette
    int16_t *colTerm = nullptr;  // per-column plasma term, rebuilt each frame
    int16_t *rowTerm = nullptr;  // per-row plasma term, rebuilt each frame

    Overlay overlays[2];
    uint32_t overlayCap = 0;
    std::atomic<int> overlayFront{-1};  // -1 = nothing published yet
    std::atomic<int> overlayInUse{-1};  // overlay the render task reads this frame
};

#endif // GAGGIMATE_SIM

#endif // SLEEPANIMATION_H
