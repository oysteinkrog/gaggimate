#pragma once

#include <stdint.h>

// The dial meters' tick rings as a compositor element (gm-2cl.6).
//
// A dial is an lv_meter whose scale-lines indicator recolours the ticks up
// to the current value. Through LVGL every value change is a sector
// invalidation, a draw, a snapshot and an overlay publish: about 110 ms of
// UI-task time per refresh, and PSRAM traffic that slows the render task
// while it runs. Here the render task paints the ring itself from the tick
// coverage sprites the meter's draw handler already caches
// (eez/MeterTickCache), so a value change is one descriptor write and the
// next frame.
//
// Exactness. LVGL's masked fill writes the overlay pixel with alpha
// (255 * s) >> 8 for sprite byte s (the sprite stores coverage + 1 so this
// comes back to the primitive's own coverage), and the overlay blend then
// does dst = a == 255 ? c : blend565(c, dst, a). This file does the same two
// steps with the same arithmetic, so at rest the ring matches what the
// overlay path would have composited, pixel for pixel; over a translucent
// plate the two orders differ only by rounding, because alpha compositing
// is associative.
//
// Nothing in here touches LVGL or the ESP-IDF, so the row compositor runs
// on the host as well (tools/tickringbench).
namespace tickring {

// Inclusive box, laid out like lv_area_t.
struct Box {
    int16_t x1, y1, x2, y2;
};

// One meter's tick coverage: cnt sprites of side x side bytes, one per
// tick, each drawn at boxes[i]. Owned by the tick cache; a Desc holding a
// pointer to one keeps the cache slot pinned until the element is cleared
// and the render task has stopped reading it.
struct Sprites {
    uint16_t cnt = 0;
    uint16_t side = 0;
    const Box *boxes = nullptr;
    const uint8_t *sprites = nullptr;
};

// What the UI task writes. Ticks lo <= i < hi are lit; the render task
// eases its own lo and hi toward these, and the boundary tick takes the
// mix of the two colours for the fraction it is lit.
struct Desc {
    const Sprites *ring = nullptr;
    uint16_t litColor = 0;   // RGB565
    uint16_t unlitColor = 0; // RGB565
    int16_t lo = 0;
    int16_t hi = 0;
};

// Same arithmetic as SleepAnimation.cpp's blend565: per channel, alpha
// scaled by 256 so the divide is a shift. Kept here so the host build has
// it; the two must stay identical.
inline uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t a) {
    const uint32_t inv = 256u - a;
    const uint32_t r = (((fg & 0xF800u) * a) + ((bg & 0xF800u) * inv)) >> 8;
    const uint32_t g = (((fg & 0x07E0u) * a) + ((bg & 0x07E0u) * inv)) >> 8;
    const uint32_t b = (((fg & 0x001Fu) * a) + ((bg & 0x001Fu) * inv)) >> 8;
    return static_cast<uint16_t>((r & 0xF800u) | (g & 0x07E0u) | (b & 0x001Fu));
}

// How much of tick i is lit for boundaries lo and hi: 1 inside, 0 outside,
// the covered fraction for a tick either boundary crosses.
inline float litFraction(int i, float lo, float hi) {
    const float a = lo > static_cast<float>(i) ? lo : static_cast<float>(i);
    const float b = hi < static_cast<float>(i + 1) ? hi : static_cast<float>(i + 1);
    const float f = b - a;
    return f <= 0.0f ? 0.0f : (f >= 1.0f ? 1.0f : f);
}

// The colour of tick i: lit, unlit, or the mix for a boundary tick.
uint16_t tickColor(int i, float lo, float hi, uint16_t lit, uint16_t unlit);

// Union of the tick boxes, for the element's clip box. False when cnt is 0.
bool bounds(const Sprites &s, Box &out);

// Composites row y of the ring into drow (w pixels wide) exactly as the
// overlay path would have. gain is the frame's overlay gain (Q8, 256 is
// none): a page fade scales the ring's coverage the way blendRow scales
// the overlay's, so an owned ring fades with the page it belongs to.
void compositeRow(uint16_t *drow, int y, int w, const Sprites &s, uint16_t lit, uint16_t unlit, float lo, float hi,
                  uint32_t gain = 256);

} // namespace tickring
