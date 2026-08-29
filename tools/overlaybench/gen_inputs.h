// Deterministic synthetic inputs standing in for a real LVGL overlay
// snapshot. Not derived from any live UI capture -- there is no golden
// screenshot to sample from on a host build -- but shaped to match the
// coverage profile SleepAnimation.cpp's own comments quote (~14,200 candidate
// pixels/frame, ~5,300 with real coverage; a row through the clock and both
// icons emits about eight runs; RUNS_PER_ROW=24 is "well past" typical use).
//
// A fixed splitmix32 PRNG (not std::rand, whose sequence is not portable
// across libc implementations) makes every generated buffer reproducible
// across machines and compilers, which is what makes the golden/ files in
// this directory meaningful to check in.
#pragma once
#include "common.h"
#include <vector>

namespace ovb {

// One panel-width overlay buffer, RGB565LE+A8 interleaved (3 B/px), row-major,
// (PANEL_W+2*OVERLAY_EXT_MARGIN) x (PANEL_H+2*OVERLAY_EXT_MARGIN) so a caller
// can exercise the real xoff/yoff addressing publishOverlayRanges uses.
struct SyntheticOverlay {
    int w = 0, h = 0; // full buffer dims, including margin
    std::vector<uint8_t> px; // w*h*3 bytes

    // Coverage byte of pixel (x, y) in *panel* space (i.e. already offset by
    // OVERLAY_EXT_MARGIN), matching what publishOverlayRanges reads at
    // xoff=yoff=OVERLAY_EXT_MARGIN (the common case: a full-size snapshot).
    const uint8_t *panelRowAlpha(int panelY) const {
        const int sy = panelY + OVERLAY_EXT_MARGIN;
        return px.data() + (static_cast<size_t>(sy) * w + OVERLAY_EXT_MARGIN) * 3 + 2;
    }
    const uint8_t *panelRowStart(int panelY) const {
        const int sy = panelY + OVERLAY_EXT_MARGIN;
        return px.data() + (static_cast<size_t>(sy) * w + OVERLAY_EXT_MARGIN) * 3;
    }
};

// seed selects the content mix; the harness always calls this with a fixed
// seed (see bench.cpp) so golden files stay stable.
SyntheticOverlay genSyntheticOverlay(uint32_t seed);

// Scrim source grid (SCRIM_W x SCRIM_H peak-alpha cells) built the same way
// publishOverlayRanges builds it: run scanRow_ref over every panel row of a
// SyntheticOverlay with a live cellRow pointer. Kept here (not in kernels/)
// because it is input-generation, not part of the kernel under test.
std::vector<uint8_t> deriveScrimSrc(const SyntheticOverlay &ov);

// A half-resolution (PANEL_W/2 x PANEL_H/2) RGB565 source image standing in
// for a rendered background animation frame -- smooth low-frequency bands
// plus some high-frequency dither, so the expand kernel's packed stores see
// varied bit patterns rather than a constant that would let the compiler
// fold everything.
std::vector<uint16_t> genSyntheticHalfRes(uint32_t seed);

} // namespace ovb
