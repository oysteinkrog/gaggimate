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
};
#else

// Procedural standby-screen animation (classic palette-cycled plasma),
// rendered by a dedicated task directly to the panel, bypassing the LVGL
// draw pipeline. No stored assets — everything is generated at runtime, so
// the whole feature travels inside the app image and OTA updates carry it.
class SleepAnimation {
  public:
    SleepAnimation() = default;
    ~SleepAnimation();

    // Starts the render task. No-op if already running or display is null.
    void start(Display *display);
    // Signals the task to exit and blocks briefly until it has stopped.
    void stop();
    bool isActive() const { return running; }

  private:
    static void taskEntry(void *arg);
    void renderLoop();
    void renderFrame(uint32_t frame);
    void buildLuts();

    Display *display = nullptr;
    void *taskHandle = nullptr;
    std::atomic<bool> running{false};
    std::atomic<bool> stopped{true};

    uint16_t *band = nullptr;   // one horizontal band of RGB565 pixels
    int16_t *sinLut = nullptr;  // 1024-entry sine table
    uint16_t *palette = nullptr; // 256-entry RGB565 palette
    int16_t *colTerm = nullptr; // per-column plasma term, rebuilt each frame
    int16_t *rowTerm = nullptr; // per-row plasma term, rebuilt each frame
};

#endif // GAGGIMATE_SIM

#endif // SLEEPANIMATION_H
