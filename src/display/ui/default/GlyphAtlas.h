#pragma once
// A8 glyph cache for the compositor's Text elements (gm-2cl.5).
//
// LVGL keeps its fonts as packed 4-bit coverage in flash and expands them
// through _lv_bpp4_opa_table on every draw. The render task cannot afford
// either the flash read or the unpack per frame, so the UI task copies each
// glyph it needs once into a per-font PSRAM arena as one byte of coverage per
// pixel (the same values the opa table would have produced), and the Text
// element reads the arena. Entries are never evicted: a font's arena is
// bounded and a glyph that does not fit is simply not cached, which makes its
// label ineligible for ownership (it stays on LVGL).
//
// UI task only for get(); the render task reads the arena bytes through the
// pointers a TextDesc carries and never calls into here.
#include <lvgl.h>
#include <stdint.h>

namespace glyphatlas {

struct Glyph {
    const uint8_t *a8 = nullptr; // box_w * box_h bytes, row-major, 0..255 coverage
    uint8_t w = 0, h = 0;        // box_w, box_h
    int8_t ofsX = 0, ofsY = 0;   // lv_font_glyph_dsc_t ofs_x / ofs_y
};

// Looks the glyph up, caching it on a miss. False when the font's bitmap
// format is not one this decodes (bpp 1, 2, 4 or 8, plain or compressed
// through lv_font_get_glyph_bitmap) or the arena is full.
bool get(const lv_font_t *font, uint32_t letter, Glyph &out);

// Bytes of arena in use across all fonts and the number of cached glyphs,
// for the debug endpoint.
uint32_t bytesUsed();
uint32_t glyphCount();

} // namespace glyphatlas
