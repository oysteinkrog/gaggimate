#include "TickRingElement.h"

#include <stddef.h>

#if defined(ESP_PLATFORM)
#include <esp_attr.h>
#define TICKRING_IRAM IRAM_ATTR
#else
#define TICKRING_IRAM
#endif

namespace tickring {

uint16_t TICKRING_IRAM tickColor(int i, float lo, float hi, uint16_t lit, uint16_t unlit) {
    const float f = litFraction(i, lo, hi);
    if (f >= 1.0f) {
        return lit;
    }
    if (f <= 0.0f) {
        return unlit;
    }
    return blend565(lit, unlit, static_cast<uint8_t>(f * 255.0f + 0.5f));
}

void buildRowSpans(const uint8_t *sprite, int side, uint8_t *spans) {
    for (int y = 0; y < side; y++) {
        const uint8_t *row = sprite + static_cast<size_t>(y) * side;
        int first = 0;
        while (first < side && row[first] == 0) {
            first++;
        }
        int last = side;
        while (last > first && row[last - 1] == 0) {
            last--;
        }
        spans[y * 2] = static_cast<uint8_t>(first);
        spans[y * 2 + 1] = static_cast<uint8_t>(last);
    }
}

void buildRowMask(const Sprites &s, int rows, uint64_t *mask) {
    for (int y = 0; y < rows; y++) {
        mask[y] = 0;
    }
    const int cnt = s.cnt < kMaxTicks ? s.cnt : kMaxTicks;
    for (int i = 0; i < cnt; i++) {
        const Box &b = s.boxes[i];
        for (int y = b.y1; y <= b.y2; y++) {
            if (y < 0 || y >= rows) {
                continue;
            }
            if (s.spans != nullptr) {
                const uint8_t *sp = s.spans + (static_cast<size_t>(i) * s.side + (y - b.y1)) * 2;
                if (sp[0] >= sp[1]) {
                    continue;
                }
            }
            mask[y] |= 1ull << i;
        }
    }
}

bool bounds(const Sprites &s, Box &out) {
    if (s.cnt == 0 || s.boxes == nullptr) {
        return false;
    }
    out = s.boxes[0];
    for (int i = 1; i < s.cnt; i++) {
        const Box &b = s.boxes[i];
        if (b.x1 < out.x1) {
            out.x1 = b.x1;
        }
        if (b.y1 < out.y1) {
            out.y1 = b.y1;
        }
        if (b.x2 > out.x2) {
            out.x2 = b.x2;
        }
        if (b.y2 > out.y2) {
            out.y2 = b.y2;
        }
    }
    return true;
}

// In IRAM with the rest of the render loop (CLAUDE.md, UI-pipeline
// invariants): the two cores share one instruction cache and LVGL passes
// evict anything of the render task's that lives in flash.
void TICKRING_IRAM fillColors(const Sprites &s, uint16_t lit, uint16_t unlit, float lo, float hi, uint16_t *colors) {
    const int cnt = s.cnt < kMaxTicks ? s.cnt : kMaxTicks;
    for (int i = 0; i < cnt; i++) {
        colors[i] = tickColor(i, lo, hi, lit, unlit);
    }
}

void compositeRow(uint16_t *drow, int y, int w, const Sprites &s, uint16_t lit, uint16_t unlit, float lo, float hi,
                  uint32_t gain) {
    uint16_t colors[kMaxTicks];
    fillColors(s, lit, unlit, lo, hi, colors);
    compositeRowColors(drow, y, w, s, colors, gain);
}

void TICKRING_IRAM compositeRowColors(uint16_t *drow, int y, int w, const Sprites &s, const uint16_t *colors,
                                      uint32_t gain) {
    const int side = s.side;
    const bool scaled = gain < 256;
    const uint8_t *sprites = s.sprites;
    const int cnt = s.cnt < kMaxTicks ? s.cnt : kMaxTicks;
    // With a row mask, only the ticks crossing this row are visited; the
    // linear scan is the fallback and the reference the host test compares
    // the mask against.
    uint64_t pending = cnt >= 64 ? ~0ull : ((1ull << cnt) - 1);
    if (s.rowMask != nullptr) {
        if (y < 0 || y >= s.maskRows) {
            return;
        }
        pending = s.rowMask[y];
    }
    while (pending != 0) {
        const int i = __builtin_ctzll(pending);
        pending &= pending - 1;
        const Box &b = s.boxes[i];
        if (y < b.y1 || y > b.y2) {
            continue;
        }
        const int sy = y - b.y1;
        int x0 = b.x1;
        int x1 = b.x2 + 1;
        if (s.spans != nullptr) {
            const uint8_t *sp = s.spans + (static_cast<size_t>(i) * side + sy) * 2;
            if (sp[0] >= sp[1]) {
                continue;
            }
            x0 = b.x1 + sp[0];
            x1 = b.x1 + sp[1];
        }
        const uint16_t c = colors[i];
        const uint8_t *row = sprites + static_cast<size_t>(i) * side * side + static_cast<size_t>(sy) * side;
        if (x0 < 0) {
            x0 = 0;
        }
        if (x1 > w) {
            x1 = w;
        }
        for (int x = x0; x < x1; x++) {
            const uint32_t sv = row[x - b.x1];
            if (sv == 0) {
                continue;
            }
            // The alpha LVGL's masked fill would have written to the overlay
            // for this sprite byte, then the overlay blend's own step (with
            // the page gain applied first, as blendRow<true> does).
            uint32_t a = (255u * sv) >> 8;
            if (scaled) {
                a = (a * gain) >> 8;
                if (a == 0) {
                    continue;
                }
            }
            drow[x] = a == 255 ? c : blend565(c, drow[x], static_cast<uint8_t>(a));
        }
    }
}

} // namespace tickring
