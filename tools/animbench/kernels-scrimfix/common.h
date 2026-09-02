// Shared constants and always-inline pixel primitives for the scrimRow fix,
// copied verbatim (same bit arithmetic, same constants) from
// src/display/ui/default/SleepAnimation.cpp. Line numbers below are exact
// as of this writing (branch idf5) -- re-check if the source moves.
//
// This is an independent re-derivation, written from the real source, not a
// copy of tools/overlaybench/kernels/overlay_blend.{h,cpp} (a sibling
// worker's file, read-only reference, never copied verbatim into this
// directory).
#pragma once
#include <cstdint>

namespace scrimfix {

// SleepAnimation.cpp:107 -- "The scrim factor is stored as 32nds of full
// brightness, so 32 means 'leave this cell alone'."
constexpr int SCRIM_INV_NONE = 32;
// SleepAnimation.cpp:110 -- "Scrim grid resolution: 1 << 2 = one cell per
// 4x4 panel pixels."
constexpr int SCRIM_SHIFT = 2;

// scale565, verbatim from SleepAnimation.cpp:196-205.
// Source comment (SleepAnimation.cpp ~186-195): "Scale an RGB565 toward
// black. inv is 0..32, i.e. 32 keeps the pixel and 0 blacks it out." -- this
// is the source's OWN documentation of the identity property this task's
// branchless substitution relies on; see the derivation in
// scrimrow-branchlessword.patch's prose and prove_scrimfix.cpp's header
// comment for the arithmetic proof, not just the assertion.
__attribute__((always_inline)) inline uint16_t scale565_ref(uint16_t c, uint32_t inv) {
    // The two products are left where the multiply puts them and the masks do
    // the shifting, so one shift serves both lanes instead of one each.
    // (c & 0xF81F) * inv leaves blue in bits 0..9 and red in bits 11..20 -- inv
    // is five bits, so they cannot reach each other -- and 0x1F03E0 picks the
    // top five of each. Green, five bits further up, comes out under 0xFC00.
    const uint32_t rb = (c & 0xF81Fu) * inv;
    const uint32_t g = (c & 0x07E0u) * inv;
    return static_cast<uint16_t>(((rb & 0x1F03E0u) | (g & 0xFC00u)) >> 5);
}

// scale565x2, verbatim from SleepAnimation.cpp:210-212.
// "Two neighbouring pixels at once. The band is 4-byte aligned and a scrim
// cell starts on an even pixel, so the pair is one aligned load and one
// aligned store where four half-word accesses stood before."
__attribute__((always_inline)) inline uint32_t scale565x2_ref(uint32_t w, uint32_t inv) {
    return scale565_ref(static_cast<uint16_t>(w), inv) |
           (static_cast<uint32_t>(scale565_ref(static_cast<uint16_t>(w >> 16), inv)) << 16);
}

// scrimCell, verbatim from SleepAnimation.cpp:388-393.
// "Spelled out rather than looped: at a four-iteration trip count the
// compiler kept the counter and the branch, which is two of every thirteen
// instructions the loop issued."
__attribute__((always_inline)) inline void scrimCell_ref(uint16_t *__restrict dst, uint32_t inv, int c) {
    static_assert(SCRIM_SHIFT == 2, "the cell body is four pixels wide");
    uint32_t *const q = reinterpret_cast<uint32_t *>(dst + (c << SCRIM_SHIFT));
    q[0] = scale565x2_ref(q[0], inv);
    q[1] = scale565x2_ref(q[1], inv);
}

} // namespace scrimfix
