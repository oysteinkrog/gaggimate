// Shared constants and scalar primitives for the overlay-pipeline kernels.
//
// Every value and helper here is copied verbatim (same bit arithmetic, same
// constants) from src/display/ui/default/SleepAnimation.cpp so a kernel file
// in kernels/ can be dropped back into that translation unit unchanged. Do
// NOT "clean up" a constant to a rounder value -- if it drifts from the
// firmware the golden files stop meaning anything.
#pragma once
#include <cstdint>
#include <cstddef>

namespace ovb {

// -- constants, from SleepAnimation.cpp (anonymous namespace) --------------
constexpr int PANEL_W = 480;
constexpr int PANEL_H = 480;
constexpr int OVERLAY_EXT_MARGIN = 16; // snapshot extends this far past the panel on every side
constexpr int RUNS_PER_ROW = 24;
constexpr int RUN_GAP_MERGE = 4;
constexpr int HALO_GAP_MERGE_CELLS = 1;
constexpr int SCRIM_INV_NONE = 32;
constexpr int SCRIM_SHIFT = 2; // one scrim cell per 4x4 panel pixels
constexpr int SCRIM_REACH_CELLS = 3;
constexpr int SCRIM_W = (PANEL_W + (1 << SCRIM_SHIFT) - 1) >> SCRIM_SHIFT; // 120
constexpr int SCRIM_H = (PANEL_H + (1 << SCRIM_SHIFT) - 1) >> SCRIM_SHIFT; // 120
constexpr int BAND_H = 2; // panel rows rendered per render-task band call

// -- run-length helper, byte-identical to emitRun() in SleepAnimation.cpp --
inline int emitRun_ref(uint32_t *runs, int n, int x0, int x1, int gapMerge) {
    if (n > 0) {
        const uint32_t last = runs[n - 1];
        const int lastEnd = static_cast<int>(last >> 16);
        if (x0 - lastEnd <= gapMerge || n >= RUNS_PER_ROW) {
            runs[n - 1] = (last & 0xFFFFu) | (static_cast<uint32_t>(x1) << 16);
            return n;
        }
    }
    runs[n] = static_cast<uint32_t>(x0) | (static_cast<uint32_t>(x1) << 16);
    return n + 1;
}

// -- pixel arithmetic, byte-identical to SleepAnimation.cpp -----------------
__attribute__((always_inline)) inline uint16_t rgb565_ref(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

__attribute__((always_inline)) inline uint16_t blend565_ref(uint16_t fg, uint16_t bg, uint8_t a) {
    const uint32_t inv = 256u - a;
    const uint32_t r = (((fg & 0xF800u) * a) + ((bg & 0xF800u) * inv)) >> 8;
    const uint32_t g = (((fg & 0x07E0u) * a) + ((bg & 0x07E0u) * inv)) >> 8;
    const uint32_t b = (((fg & 0x001Fu) * a) + ((bg & 0x001Fu) * inv)) >> 8;
    return static_cast<uint16_t>((r & 0xF800u) | (g & 0x07E0u) | (b & 0x001Fu));
}

// inv is 0..32; 32 keeps the pixel, 0 blacks it out.
__attribute__((always_inline)) inline uint16_t scale565_ref(uint16_t c, uint32_t inv) {
    const uint32_t rb = (c & 0xF81Fu) * inv;
    const uint32_t g = (c & 0x07E0u) * inv;
    return static_cast<uint16_t>(((rb & 0x1F03E0u) | (g & 0xFC00u)) >> 5);
}

__attribute__((always_inline)) inline uint32_t scale565x2_ref(uint32_t w, uint32_t inv) {
    return scale565_ref(static_cast<uint16_t>(w), inv) |
           (static_cast<uint32_t>(scale565_ref(static_cast<uint16_t>(w >> 16), inv)) << 16);
}

} // namespace ovb
