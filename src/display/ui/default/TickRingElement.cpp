#include "TickRingElement.h"

#include <stddef.h>

#if defined(ESP_PLATFORM)
#include <esp_attr.h>
#define TICKRING_IRAM IRAM_ATTR
#else
#define TICKRING_IRAM
#endif

namespace tickring {

uint16_t tickColor(int i, float lo, float hi, uint16_t lit, uint16_t unlit) {
    const float f = litFraction(i, lo, hi);
    if (f >= 1.0f) {
        return lit;
    }
    if (f <= 0.0f) {
        return unlit;
    }
    return blend565(lit, unlit, static_cast<uint8_t>(f * 255.0f + 0.5f));
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
void TICKRING_IRAM compositeRow(uint16_t *drow, int y, int w, const Sprites &s, uint16_t lit, uint16_t unlit, float lo,
                                float hi, uint32_t gain) {
    const int side = s.side;
    const bool scaled = gain < 256;
    const uint8_t *sprites = s.sprites;
    for (int i = 0; i < s.cnt; i++) {
        const Box &b = s.boxes[i];
        if (y < b.y1 || y > b.y2) {
            continue;
        }
        const uint16_t c = tickColor(i, lo, hi, lit, unlit);
        const uint8_t *row = sprites + static_cast<size_t>(i) * side * side + static_cast<size_t>(y - b.y1) * side;
        int x0 = b.x1;
        int x1 = b.x2 + 1;
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
