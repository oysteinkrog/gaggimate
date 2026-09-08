#pragma once

// Per-object LVGL draw profile for one screen (gm-2cl.19). Bench and
// simulator builds only. `arm=1` on /api/debug/drawprof (or `drawprof=1` on
// /api/debug/anim) arms it once; the next screen root DefaultUI tunes gets a
// draw-event callback on every object and the arm clears,
// and the time between each object's DRAW_MAIN_BEGIN and DRAW_MAIN_END
// (its own drawing, children excluded) and DRAW_POST_BEGIN to DRAW_POST_END
// is summed per object. /api/debug/anim reports the entries sorted by time
// under "drawprof". Times are cumulative since the attach, so the first read
// after a page change is that page's whole snapshot plus whatever refreshes
// followed; "n" is the draw count.

#include <ArduinoJson.h>
#include <lvgl.h>

#if defined(GM_TOUCH_PROBE) || defined(GAGGIMATE_SIM)
#define GM_DRAW_PROFILE 1
#endif

namespace drawprof {

extern volatile int g_req;

#ifdef GM_DRAW_PROFILE
// Attaches to every object under root, dropping the previous screen's table.
void attach(lv_obj_t *root);
// Appends the entries, most expensive first, as an array under key.
void report(JsonDocument &doc, const char *key, int maxEntries = 24);
#else
inline void attach(lv_obj_t *) {}
inline void report(JsonDocument &, const char *, int = 24) {}
#endif

} // namespace drawprof
