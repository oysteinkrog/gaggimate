#include "GlyphAtlas.h"

#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

namespace glyphatlas {
namespace {

constexpr int kMaxFonts = 8;
constexpr int kMaxGlyphsPerFont = 96;
// A montserrat_48 digit is about 26x35 = 910 bytes; 96 of them would be
// 87 KB, but a live label uses digits, a point, a colon, a sign and a few
// unit letters, so 32 KB per font holds every glyph a screen shows.
constexpr uint32_t kArenaBytes = 32 * 1024;

struct Entry {
    uint32_t letter;
    Glyph g;
};

struct FontCache {
    const lv_font_t *font = nullptr;
    uint8_t *arena = nullptr;
    uint32_t used = 0;
    int count = 0;
    // In PSRAM with the arena: 8 fonts of 96 entries would be 12 KB of
    // internal BSS otherwise, and internal DRAM is the WiFi budget.
    Entry *entries = nullptr;
};

FontCache caches[kMaxFonts];
uint32_t totalUsed = 0;
uint32_t totalGlyphs = 0;

uint8_t *arenaAlloc(uint32_t bytes) {
#ifdef ESP_PLATFORM
    // PSRAM on purpose: internal DRAM is the WiFi budget (CLAUDE.md, Internal
    // DRAM budget) and the render task reads a glyph row at a time anyway.
    return static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
    return static_cast<uint8_t *>(malloc(bytes));
#endif
}

FontCache *cacheFor(const lv_font_t *font) {
    for (FontCache &c : caches) {
        if (c.font == font) {
            return &c;
        }
    }
    for (FontCache &c : caches) {
        if (c.font == nullptr) {
            uint8_t *block = arenaAlloc(kArenaBytes + sizeof(Entry) * kMaxGlyphsPerFont);
            if (block == nullptr) {
                return nullptr;
            }
            c.entries = reinterpret_cast<Entry *>(block);
            c.arena = block + sizeof(Entry) * kMaxGlyphsPerFont;
            c.font = font;
            c.used = 0;
            c.count = 0;
            return &c;
        }
    }
    return nullptr;
}

const uint8_t *opaTable(uint32_t bpp) {
    // The same tables lv_draw_sw_letter uses (lv_draw_sw_letter.c), so a
    // cached byte equals the mask value LVGL would have handed set_px_cb.
    static const uint8_t bpp1[2] = {0, 255};
    static const uint8_t bpp2[4] = {0, 85, 170, 255};
    static const uint8_t bpp4[16] = {0, 17, 34, 51, 68, 85, 102, 119, 136, 153, 170, 187, 204, 221, 238, 255};
    switch (bpp) {
    case 1:
        return bpp1;
    case 2:
        return bpp2;
    case 4:
        return bpp4;
    default:
        return nullptr;
    }
}

} // namespace

bool get(const lv_font_t *font, uint32_t letter, Glyph &out) {
    if (font == nullptr) {
        return false;
    }
    FontCache *c = cacheFor(font);
    if (c == nullptr) {
        return false;
    }
    for (int i = 0; i < c->count; i++) {
        if (c->entries[i].letter == letter) {
            out = c->entries[i].g;
            return true;
        }
    }
    if (c->count >= kMaxGlyphsPerFont) {
        return false;
    }
    lv_font_glyph_dsc_t g;
    // '\0' as the next letter, the way lv_draw_letter asks for the bitmap
    // descriptor: kerning only moves the pen, never the bitmap.
    if (!lv_font_get_glyph_dsc(font, &g, letter, '\0')) {
        return false;
    }
    uint32_t bpp = g.bpp;
    if (bpp == 3) {
        bpp = 4; // lv_draw_sw_letter treats 3 as 4
    }
    if (bpp != 1 && bpp != 2 && bpp != 4 && bpp != 8) {
        return false;
    }
    if (g.box_w > 255 || g.box_h > 255) {
        return false;
    }
    const uint32_t n = static_cast<uint32_t>(g.box_w) * g.box_h;
    Glyph glyph;
    glyph.w = static_cast<uint8_t>(g.box_w);
    glyph.h = static_cast<uint8_t>(g.box_h);
    glyph.ofsX = static_cast<int8_t>(g.ofs_x);
    glyph.ofsY = static_cast<int8_t>(g.ofs_y);
    if (n > 0) {
        if (c->used + n > kArenaBytes) {
            return false;
        }
        const uint8_t *bitmap = lv_font_get_glyph_bitmap(font, letter);
        if (bitmap == nullptr) {
            return false;
        }
        uint8_t *dst = c->arena + c->used;
        if (bpp == 8) {
            memcpy(dst, bitmap, n);
        } else {
            // Rows are not byte-padded: the bitmap is one continuous bit
            // stream, box_w * bpp bits per row, high bits first
            // (lv_draw_sw_letter's col_bit_row_ofs is zero for a whole box).
            const uint8_t *table = opaTable(bpp);
            const uint32_t perByte = 8 / bpp;
            const uint32_t mask = (1u << bpp) - 1u;
            for (uint32_t p = 0; p < n; p++) {
                const uint32_t byte = bitmap[p / perByte];
                const uint32_t shift = 8 - bpp * (1 + (p % perByte));
                dst[p] = table[(byte >> shift) & mask];
            }
        }
        glyph.a8 = dst;
        c->used += n;
        totalUsed += n;
    }
    c->entries[c->count].letter = letter;
    c->entries[c->count].g = glyph;
    c->count++;
    totalGlyphs++;
    out = glyph;
    return true;
}

uint32_t bytesUsed() { return totalUsed; }
uint32_t glyphCount() { return totalGlyphs; }

} // namespace glyphatlas
