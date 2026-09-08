#pragma once
// The touch controller read, off the UI task (gm-2cl.4).
//
// LVGL polls the touch controller from lv_task_handler(), which runs on the
// UI task; a 35 to 180 ms LVGL pass (a page snapshot, a telemetry redraw)
// therefore delays every press by as much. This task reads the controller
// every 5 ms on core 1 at priority 2, above the UI task, keeps the latest
// sample for LVGL's read callback (touchpad_read, LV_Helper.cpp; LVGL's
// clicks, holds, repeats and unlocks are unchanged, they just see the sample
// later) and answers the press itself: the UI task publishes, once per pass,
// the clickable rectangles of the active screen (the hit map, tree order,
// with a generation), and on a press edge the task hit-tests the point
// against it and writes the press plate element at once. The plate is
// cleared on release, on press lost, and when the hit map's generation
// changes under a held press (a screen change).
//
// The task's stack is PSRAM: it never runs with the flash cache disabled
// (I2C through the driver, no NVS, no flash), the same rule the render tasks
// follow (CLAUDE.md, Internal DRAM budget). The hit map buffers are PSRAM
// too; only the header atomics and the sample are internal.
#include <stdint.h>

class Display;
class SleepAnimation;

namespace touchtask {

struct Sample {
    int16_t x = 0;
    int16_t y = 0;
    uint8_t pressed = 0;
    uint8_t synthetic = 0; // from /api/debug/tap, not the controller
    uint32_t seq = 0;      // increments per sample
    int64_t tUs = 0;       // esp_timer time the sample was read
    // The last press edge, so a press that begins and ends between two
    // LVGL reads (a quick tap during a long UI pass; every injected tap is
    // 80 ms) is still delivered: latest() hands the press point back once
    // as a pressed sample before reporting the release. pressId moves on
    // every press edge.
    uint32_t pressId = 0;
    int16_t pressX = 0;
    int16_t pressY = 0;
    int64_t pressTUs = 0;
};

struct HitRect {
    int16_t x1, y1, x2, y2;     // effective hit area: click area clipped by every ancestor's
    int16_t px1, py1, px2, py2; // object coordinates, what the plate covers
    uint16_t plate;             // 1 when a press here gets a plate (16 bits: no padding, so the
                                // publisher can memcmp two maps)
};
constexpr int kMaxHitRects = 96;

#ifndef GAGGIMATE_SIM
// Starts the task. False (and nothing changes) when it could not be created
// or already runs.
bool start(Display *display, SleepAnimation *anim, int plateElement);
bool running();
// Debug A/B: with polling off the task idles and latest() reports nothing,
// so LVGL reads the controller itself again.
void setPollEnabled(bool on);
bool pollEnabled();
// The latest sample; false when the task is not running.
bool latest(Sample &out);
// UI task only: the active screen's clickable rectangles in tree order
// (parents before children, siblings first to last), whether presses get a
// plate now, and the plate's RGB565 colour. Bumps the generation.
void publishHitMap(const HitRect *rects, int n, bool plateOn, uint16_t plateColor565, int outset);
uint32_t hitMapGeneration();
int hitMapCount();
uint32_t sampleCount();
uint32_t stackHighWaterBytes();
#else
// The simulator reads its mouse from LVGL's own callback; there is no task.
inline bool start(Display *, SleepAnimation *, int) { return false; }
inline bool running() { return false; }
inline void setPollEnabled(bool) {}
inline bool pollEnabled() { return false; }
inline bool latest(Sample &) { return false; }
inline void publishHitMap(const HitRect *, int, bool, uint16_t, int) {}
inline uint32_t hitMapGeneration() { return 0; }
inline int hitMapCount() { return 0; }
inline uint32_t sampleCount() { return 0; }
inline uint32_t stackHighWaterBytes() { return 0; }
#endif

} // namespace touchtask
