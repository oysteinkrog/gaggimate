#include "overlay_blend.h"

namespace ovb {

// Byte-identical to blendRow in SleepAnimation.cpp. noinline is load-bearing,
// not a style choice: the source comment on blendRow explains that inlined
// into its caller, the loop ran out of registers on Xtensa's windowed ABI and
// a five-instruction body cost sixty cycles. Dropping this attribute would
// make the asm dump (and any register-pressure conclusions drawn from it)
// unfaithful to what the firmware actually ships.
__attribute__((noinline)) void blendRow_ref(uint16_t *__restrict dst, const uint8_t *__restrict colour,
                                            const uint32_t *__restrict runs, int nRuns) {
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

// scrimCell, inlined (SCRIM_SHIFT==2, four pixels, spelled out like the
// firmware to keep the compiler from re-introducing the counted loop the
// real code deliberately avoids).
__attribute__((always_inline)) static inline void scrimCell_ref(uint16_t *__restrict dst, uint32_t inv, int c) {
    uint32_t *const q = reinterpret_cast<uint32_t *>(dst + (c << SCRIM_SHIFT));
    q[0] = scale565x2_ref(q[0], inv);
    q[1] = scale565x2_ref(q[1], inv);
}

// Byte-identical to scrimRow in SleepAnimation.cpp. noinline for the same
// register-pressure reason as blendRow_ref above.
__attribute__((noinline)) void scrimRow_ref(uint16_t *__restrict dst, const uint8_t *__restrict invRow,
                                            const uint32_t *__restrict haloRuns, int nHalo, int w) {
    for (int i = 0; i < nHalo; i++) {
        const uint32_t r = haloRuns[i];
        const int c0 = static_cast<int>(r & 0xFFFFu);
        int c1 = static_cast<int>(r >> 16);
        if ((c1 << SCRIM_SHIFT) > w) {
            c1 = w >> SCRIM_SHIFT;
        }
        for (int c = c0; c < c1; c++) {
            const uint32_t inv = invRow[c];
            if (inv == SCRIM_INV_NONE) {
                continue;
            }
            scrimCell_ref(dst, inv, c);
        }
    }
}

// The two-pass row body from renderFrame, scrim gated the same way (nHalo
// checked before touching invRow/haloRuns at all -- matching the firmware,
// where a whole-row-transparent cell row costs nothing beyond the branch).
// noinline: renderFrame is a large function that would not inline these
// calls either; keeping the same shape here avoids the single-TU asm dump
// (kernels compiled alone, unlike the real multi-thousand-line
// SleepAnimation.cpp) reaching a different inlining decision than the
// firmware does.
__attribute__((noinline)) void blendStage_ref(uint16_t *__restrict dst, const uint8_t *__restrict invRow,
                                              const uint32_t *__restrict haloRuns, int nHalo,
                                              const uint8_t *__restrict colour, const uint32_t *__restrict runs,
                                              int nRuns, int w) {
    if (nHalo != 0) {
        scrimRow_ref(dst, invRow, haloRuns, nHalo, w);
    }
    if (nRuns != 0) {
        blendRow_ref(dst, colour, runs, nRuns);
    }
}

const BlendStageVariant kBlendStageVariants[] = {
    {"ref_scalar", &blendStage_ref, true, false},
};
const int kBlendStageVariantCount = sizeof(kBlendStageVariants) / sizeof(kBlendStageVariants[0]);

} // namespace ovb
