/**
 * @file      LV_Helper.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  Shenzhen Xin Yuan Electronic Technology Co., Ltd
 * @date      2024-01-22
 *
 */
#pragma once
#include "Display.h"
#include <Arduino.h>
#include <lvgl.h>

void enable_amoled_black_theme_override(lv_disp_t *disp);
void beginLvglHelper(Display &board, bool debug = false);
// While true, LVGL flushes are dropped (rendered to the draw buffer but never
// pushed to the panel). Used while the sleep animation task owns the panel so
// widget updates can't race the plasma frames on screen.
void lvgl_helper_suppress_flush(bool suppress);

// The areas LVGL re-rendered while flushing was suppressed, in screen
// coordinates, as a list of disjoint rectangles rather than one bounding box.
// The box turned two small widgets at opposite screen corners into a
// full-screen union, and the overlay refresh paid full-screen snapshot and
// span-scan costs for every temperature tick. Returns the count written to
// out (0 when nothing was redrawn); taking the values clears the accumulator.
//
// The suppressed flush is the exact hook for this: LVGL calls it once per
// redrawn area, so what it is handed IS the invalidated region, already merged
// and clipped by LVGL's own refresh logic. Both this and the caller run on the
// UI task (lv_timer_handler), so the accumulator needs no locking.
//
// Cap of 4, not 8: every rect costs a full lv_obj_redraw tree walk, and with
// LVGL's heap in PSRAM (LV_MEM_CUSTOM_ALLOC) that walk is a random PSRAM
// pointer chase that dominates the snapshot. On the bench rig, 4 cut the
// draw stage from ~59 ms to ~51 ms per refresh with no growth in redrawn
// area (the least-growth merge keeps clips tight) and publish unchanged.
// 2 is past the floor: typical refreshes carry 3-4 disjoint widget regions,
// so the merge starts unioning across the screen (area avg ~15k -> ~103k px,
// snap ~55 -> ~90 ms measured). Do not go below 4.
#define GM_DIRTY_RECT_CAP 4
int lvgl_helper_take_dirty_rects(lv_area_t *out, int maxN);
// Merge one rectangle into a fixed-capacity list: unions with anything it
// overlaps or touches (folding transitively), appends while there is room,
// and otherwise folds into the entry whose bounding box grows least. Exported
// for the overlay's per-buffer debt lists, which need the same policy.
void lvgl_helper_rect_add(lv_area_t *list, int *n, int cap, const lv_area_t &r);

// Stamped by touchpad_read on every press/release edge (production path, not
// probe-gated). DefaultUI::loop compares it against the last telemetry pass
// start so an interaction bypasses the pass spacing.
extern volatile int64_t g_touchEdgeAtUs;
// How long after a touch edge the interaction fast paths stay open: the
// telemetry-pass and overlay-refresh gates in DefaultUI stand aside, and the
// overlay publish wakes the render task early. A window rather than an
// edge-vs-stamp compare because a release's CLICK handler only sets flags
// that are applied one pass later — by then an edge-triggered refresh has
// already re-stamped the gates, and the click's visible result would wait
// out a full gate period.
constexpr int64_t GM_TOUCH_GRACE_US = 400000;

// Foreground (overlay) refresh instrumentation and knob, production path.
// The overlay is what the widgets look like to the render task: every LVGL
// change reaches the panel through one refreshSleepOverlay() pass (snapshot
// the dirty rects into the RGB565+A8 buffer, publish the alpha runs), so the
// rate of those passes IS the foreground's refresh rate, and these counters
// are how it is measured without a probe build. ovRefreshes counts passes
// that published; the last* fields describe the most recent one.
// g_overlayMinRefreshUs is the spacing gate refreshSleepOverlay applies
// between partial refreshes (DefaultUI.h's OVERLAY_MIN_REFRESH_US is its
// boot value); ovmin= on /api/debug/anim moves it live so the ungated rate
// of the snapshot path can be measured on the device.
struct OverlayStats {
    volatile uint32_t refreshes = 0;
    volatile uint32_t lastSnapUs = 0;
    volatile uint32_t lastPubUs = 0;
    volatile uint32_t lastAreaPx = 0;
    volatile uint32_t lastClips = 0;
};
extern OverlayStats g_overlayStats;
extern volatile int64_t g_overlayMinRefreshUs;
// Foreground motion test (uianim= on /api/debug/anim, applied by
// DefaultUI::loop on the UI task, since LVGL is single-threaded): 0 removes
// the test widget, 1 slides an opaque 120x120 rounded plate with a label
// back and forth across the current screen on an lv_anim, 2 the same at
// 60x60. A continuous LVGL animation is the one input that measures what the
// snapshot path can do for MOVING widgets, which telemetry (a few changes
// per second) never exercises.
extern volatile int g_uiAnimTestReq;

#ifdef GM_TOUCH_PROBE
#include <atomic>
// Touch-to-pixel latency probe (bench builds). touchpad_read stamps the edge;
// whichever path carries the resulting redraw to the panel closes the interval
// and clears the stamp: disp_flush's direct present, or the animation's
// renderLoop when the overlay path owns the panel. These two are UI-task-only.
extern volatile int64_t g_probeEdgeUs;
extern volatile bool g_probeEdgeIsPress;
// Handoff from the overlay publish to the render task. The publish only makes
// the snapshot AVAILABLE; the composite samples it at the start of the next
// frame, so the publish copies the edge stamp here and the render task closes
// the interval at the present of the first frame that sampled it. UI task
// (core 1) writes, render task (core 0) reads-and-clears: atomic because a
// 64-bit access is two instructions on Xtensa and a cross-core torn read
// would fabricate a latency number. A stamp lost to the check-then-clear
// window still just drops one probe line, nothing more.
extern std::atomic<int64_t> g_probePublishUs;
extern std::atomic<bool> g_probePublishIsPress;
// Publish sub-stage accumulators for GM_UISTAT: span scan vs scrim rebuild.
// Written in publishOverlayRanges and read+reset by the UISTAT logger, all on
// the UI task; volatile only to keep the accumulation visible across TUs.
extern volatile int64_t g_statPubScanUs;
extern volatile int64_t g_statPubScrimUs;
// Saturation probe: when a touch edge is read, how deep into an in-flight UI
// pass it landed and how long since the previous pass ended. The indev read
// itself runs on the UI task, so an edge that spent its wait inside the touch
// controller shows up here as a tiny in-pass offset right after a long pass:
// the wait happened BEFORE the edge could even be read. Same-task access.
extern volatile int64_t g_statPassStartUs; // current pass start (0 = idle)
extern volatile int64_t g_statPassEndUs;   // previous pass end
#endif

String lvgl_helper_get_fs_filename(String filename);
const char *lvgl_helper_get_fs_filename(const char *filename);
