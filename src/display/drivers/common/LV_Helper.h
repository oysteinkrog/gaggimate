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

// Planar overlay pixel format (gm-2cl.16). The animation's overlay is two
// planes, not RGB565+A8 interleaved: an RGB565 plane and a 16-bit alpha
// plane with 255 stored as 256, both gm_overlay_plane_px pixels long, the
// alpha plane starting right after the colour plane. With the alpha in a
// 16-bit lane of its own every 8-pixel group blends on the vector unit
// (fg * 256 + bg * 0 is an exact copy), which is 5x the interleaved
// kernel's speed per pixel (tools/blend_probe.py, 2026-09-07). LVGL writes
// the overlay through gm_set_px_planar; scripts/patch_lvgl_setpx_fast.py
// recognises that writer by pointer identity and inlines it in
// lv_draw_sw_blend.c, so the render task and LVGL agree on the layout
// through this one declaration.
extern "C" uint32_t gm_overlay_plane_px;
void gm_set_px_planar(lv_disp_drv_t *disp_drv, uint8_t *buf, lv_coord_t buf_w, lv_coord_t x, lv_coord_t y,
                      lv_color_t color, lv_opa_t opa);
extern "C" bool gm_disp_uses_planar_writer(const lv_disp_drv_t *drv);
static inline uint16_t gm_planar_alpha_store(lv_opa_t a) { return a == 255 ? 256 : a; }
static inline lv_opa_t gm_planar_alpha_load(uint16_t a16) { return a16 > 255 ? 255 : static_cast<lv_opa_t>(a16); }

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
// Called by touchpad_read on every press or release edge, on the UI task,
// with the deepest clickable object under the point (nullptr on release).
// DefaultUI installs the press highlight element writer here.
typedef void (*TouchHitHook)(lv_obj_t *hit, bool pressed, int16_t x, int16_t y);
extern TouchHitHook g_touchHitHook;
// True while the compositor's press highlight element is the press feedback:
// the LVGL pressed styles stand down so a target is not dimmed twice.
extern volatile bool g_pressPlateActive;
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
// fps= on /api/debug/anim: a temporary animation frame cap for A/B runs
// (0 = the stored setting). DefaultUI applies it where it re-applies the
// stored cap each pass. Not persisted.
extern volatile uint8_t g_animFpsOverride;
// Foreground motion test (uianim= on /api/debug/anim, applied by
// DefaultUI::loop on the UI task, since LVGL is single-threaded): 0 removes
// the test widget, 1 slides an opaque 120x120 rounded plate with a label
// back and forth across the current screen on an lv_anim, 2 the same at
// 60x60, 3 the 120x120 plate again but as a SleepAnimation layer moved by
// the render task (the same travel and timing as 1, so the two paths are
// directly comparable), 4 that layer parked at the far end of the travel
// for a framebuffer grab. A continuous animation is the one input that
// measures what each path can do for MOVING widgets, which telemetry (a few
// changes per second) never exercises.
extern volatile int g_uiAnimTestReq;

// /api/debug/anim?dials=0|1: whether the dial meters' tick rings go through
// the TickRing compositor element while the animation composites the
// screen (gm-2cl.6, DefaultUI::serviceDialElements), or stay with LVGL. The
// A/B for the framebuffer compare and the refresh-count measurement; the
// production value is 1.
extern volatile int g_dialElementsReq;
// texts=0|1 and textease=0|1 on /api/debug/anim: live labels as Text elements
// (DefaultUI::serviceTextElements) and the numeric easing of their values.
extern volatile int g_textElementsReq;
extern volatile int g_textEaseReq;
// Text element bookkeeping for /api/debug/anim (text_dbg): 0 labels taken,
// 2 refused by the glyph build, 3 the last build failure's step, 4 released
// for eligibility, 5 glyph-list rebuilds.
extern volatile int g_textDbg[8];
// What each Text element slot holds this pass (text_elems on /api/debug/anim):
// the label's text, the glyph box on the panel and the rebuild version.
struct TextElemDbg {
    volatile bool owned = false;
    volatile int16_t x = 0, y = 0, w = 0, h = 0;
    volatile uint16_t ver = 0;
    char text[24] = {};
};
extern TextElemDbg g_textElemDbg[6];
// The last DIRTYLOG_N dirty rectangles the overlay refresh harvested from
// LVGL (dirty_recent on /api/debug/anim): what is still invalidating on a
// screen once the elements own the live widgets. Ring, newest at
// (g_dirtyLogCount - 1) % DIRTYLOG_N.
constexpr int DIRTYLOG_N = 16;
struct DirtyLogEntry {
    int16_t x1, y1, x2, y2;
    uint32_t tMs;
};
extern DirtyLogEntry g_dirtyLog[DIRTYLOG_N];
extern volatile uint32_t g_dirtyLogCount;

// /api/debug/touchmap: the UI task walks one screen's object tree and writes
// every object (class, coords, flags, ext click pad, event count, parent) as
// a JSON array into g_touchMapBuf, so the hit rectangles LVGL will actually
// use can be reviewed off the device. g_touchMapReq is the screen id to dump
// (ScreensEnum), or 0 to dump the active screen (lv_scr_act()) without
// loading anything; g_touchMapLoad asks the UI task to switch to a non-zero
// screen first (meaningless, and never consulted, for 0). g_touchMapPending
// is the "a request is queued or being serviced" flag, kept separate from
// g_touchMapReq because 0 is now a real request rather than "idle".
// g_touchMapLen is 0 until the dump is ready. Written by the UI task, read
// by the web server task.
extern volatile int g_touchMapReq;
extern volatile bool g_touchMapLoad;
extern volatile bool g_touchMapPending;
extern char *g_touchMapBuf;
extern volatile uint32_t g_touchMapLen;

// /api/debug/touchlog: the last TOUCHLOG_N touch edges (press and release)
// with the panel point and, for a press, the object LVGL's hit test finds
// there, so a tap that missed can be compared against the hit rectangles
// (tools/touchmap.py) instead of guessed at. Written by touchpad_read on the
// UI task; entry i lives at i % TOUCHLOG_N. syn is true when the edge came
// from TouchInject (/api/debug/tap) rather than the touch controller.
struct TouchLogEntry {
    uint32_t tMs;
    int16_t x, y;
    bool press;
    bool syn;
    lv_obj_t *hit;
};
constexpr int TOUCHLOG_N = 32;
extern TouchLogEntry g_touchLog[TOUCHLOG_N];
extern volatile uint32_t g_touchLogCount;

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
