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
#define GM_DIRTY_RECT_CAP 8
int lvgl_helper_take_dirty_rects(lv_area_t *out, int maxN);
// Merge one rectangle into a fixed-capacity list: unions with anything it
// overlaps or touches (folding transitively), appends while there is room,
// and otherwise folds into the entry whose bounding box grows least. Exported
// for the overlay's per-buffer debt lists, which need the same policy.
void lvgl_helper_rect_add(lv_area_t *list, int *n, int cap, const lv_area_t &r);

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
#endif

String lvgl_helper_get_fs_filename(String filename);
const char *lvgl_helper_get_fs_filename(const char *filename);
