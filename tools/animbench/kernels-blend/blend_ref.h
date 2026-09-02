// Ground truth for the blendRow optimization: verbatim copies of
// blend565() and blendRow() from src/display/ui/default/SleepAnimation.cpp,
// with only the names suffixed _ref and the free function rgb565()/other
// unrelated helpers omitted. Do NOT "clean up" these -- if they drift from
// the real source the proofs in this directory stop meaning anything. Line
// numbers cited below were read directly from that file (this repo, branch
// idf5) at the time this was written; re-check them if SleepAnimation.cpp
// has since moved.
//
// This file intentionally does NOT include tools/overlaybench/kernels/
// overlay_blend.h -- everything here is independently copied straight from
// the real source, and everything below in this directory builds only from
// this file, so this whole kernels-blend/ directory has zero build
// dependency on tools/overlaybench/ (which is a sibling agent's exclusive
// territory; this repo only reads it for reference during design).
#pragma once
#include <cstddef>
#include <cstdint>

namespace blendopt {

// Verbatim from SleepAnimation.cpp lines 154-184 (blend565), renamed
// blend565_ref. Per-channel RGB565 alpha blend, a: 0..255 foreground
// opacity. The source comment there (not reproduced here) explains why the
// per-channel form replaced an earlier packed-red-blue trick that broke at
// 8-bit alpha; that history is not needed to trust this arithmetic, only
// the arithmetic itself is.
__attribute__((always_inline)) inline uint16_t blend565_ref(uint16_t fg, uint16_t bg, uint8_t a) {
    const uint32_t inv = 256u - a;
    const uint32_t r = (((fg & 0xF800u) * a) + ((bg & 0xF800u) * inv)) >> 8;
    const uint32_t g = (((fg & 0x07E0u) * a) + ((bg & 0x07E0u) * inv)) >> 8;
    const uint32_t b = (((fg & 0x001Fu) * a) + ((bg & 0x001Fu) * inv)) >> 8;
    return static_cast<uint16_t>((r & 0xF800u) | (g & 0x07E0u) | (b & 0x001Fu));
}

// Verbatim from SleepAnimation.cpp lines 350-370 (blendRow), renamed
// blendRow_ref, noinline dropped (it was load-bearing for the real device
// build's register allocation, per the source comment there; irrelevant to
// a host-side correctness oracle, and dropping it lets it inline freely
// into the proof harnesses' hot loops without changing behaviour).
inline void blendRow_ref(uint16_t *__restrict dst, const uint8_t *__restrict colour, const uint32_t *__restrict runs,
                          int nRuns) {
    for (int i = 0; i < nRuns; i++) {
        const uint32_t r = runs[i];
        int x = static_cast<int>(r & 0xFFFFu);
        const int xEnd = static_cast<int>(r >> 16);
        const uint8_t *px = colour + static_cast<size_t>(x) * 3;
        for (; x < xEnd; x++, px += 3) {
            const uint32_t a = px[2];
            if (a == 0) {
                continue;
            }
            const uint16_t c = static_cast<uint16_t>(px[0] | (px[1] << 8));
            dst[x] = a == 255 ? c : blend565_ref(c, dst[x], static_cast<uint8_t>(a));
        }
    }
}

// Shared per-pixel scalar body, byte-identical to blendRow_ref's inner
// loop, factored out so every scalar fallback path in blend_model.h and
// blend_pie_kernel.cpp uses exactly this code rather than a parallel
// reimplementation.
__attribute__((always_inline)) inline void blendPixelScalar_ref(uint16_t *__restrict dst,
                                                                  const uint8_t *__restrict px, int x) {
    const uint32_t a = px[2];
    if (a == 0) {
        return;
    }
    const uint16_t c = static_cast<uint16_t>(px[0] | (px[1] << 8));
    dst[x] = a == 255 ? c : blend565_ref(c, dst[x], static_cast<uint8_t>(a));
}

} // namespace blendopt
