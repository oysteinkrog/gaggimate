#pragma once

#include <lvgl.h>
#include <stdint.h>

// Sprite cache for the meter's scale ticks (gm-qo3.3).
//
// action_on_meter_draw paints every visible tick as a round-capped
// lv_draw_line: four masks plus two circle rects per tick, about 1.2 ms
// each on the device, 14 to 20 ticks per overlay refresh, the largest
// single item inside the ~57 ms redraw (GM_UISTAT meter= and ticks=,
// 2026-09-06). A tick's shape never changes once the meter is laid out;
// only its colour does, as the value moves through the SCALE_LINES range.
// So each tick's coverage is rendered once, through the same primitive and
// the same anti-aliasing, into an A8 sprite in PSRAM, and every later draw
// is one masked fill: the same blend the line draw itself ends in, minus
// the mask building.
//
// The output matches the direct draw to within rounding. A masked fill
// writes alpha (255 * m) >> 8 for mask value m, and the sprite stores
// k + 1 for every non-zero coverage byte k it read back, so the blit's
// mask is the primitive's own coverage again ((255 * (k + 1)) >> 8 == k
// for k in 0..254). The one difference: the line primitive composites a
// round cap over the body it already drew, so a pixel under both is mixed
// twice, where the sprite holds their union and the blit mixes once. With
// LVGL's 5-bit RGB565 mix that is at most one colour step apart, on about
// 180 of a ring's pixels (sim brew screen, 2026-09-06).
//
// Cached only when no LVGL draw mask is active at draw time: a masked fill
// ignores the mask list, where the direct primitive would have applied it.
// The meter has none in practice; the check keeps the fallback exact.
namespace meterticks {

// Geometry that fixes every tick's shape and position. cx/cy/r are in
// screen coordinates, so a meter that moves gets a fresh cache.
struct Key {
    const lv_obj_t *obj;
    uint16_t cnt;
    uint16_t tickWidth;
    uint16_t tickLength;
    int16_t angleRange;
    int16_t rotation;
    lv_coord_t cx;
    lv_coord_t cy;
    lv_coord_t rEdge;
    bool operator==(const Key &o) const {
        return obj == o.obj && cnt == o.cnt && tickWidth == o.tickWidth && tickLength == o.tickLength &&
               angleRange == o.angleRange && rotation == o.rotation && cx == o.cx && cy == o.cy && rEdge == o.rEdge;
    }
};

// Renders one tick's coverage into a scratch buffer. ctx is a draw context
// whose buf_area and clip_area are the tick's box; the callback draws the
// tick exactly as the direct path would (same coords, same descriptor).
typedef void (*RenderFn)(lv_draw_ctx_t *ctx, void *user);

// Draws tick i of the meter described by key into draw_ctx at box, in
// color. Builds the sprite first when this tick has none yet. Returns
// false when the cache cannot serve this draw (a draw mask is active, the
// box is too big, no memory); the caller then draws directly.
bool draw(lv_draw_ctx_t *draw_ctx, const Key &key, int i, const lv_area_t &box, lv_color_t color, RenderFn render,
          void *user);

// Bytes of PSRAM the caches hold, for the debug endpoints.
uint32_t bytesAllocated();

} // namespace meterticks
