// Layer 3, integration form: the real ESP32-S3 PIE (EE.*) implementation of
// blendRow_group8_model's "no lane opaque" vector-arithmetic path, written
// the way it would actually be pasted into SleepAnimation.cpp -- an
// `asm volatile` block inside a normal C++ function, following the exact
// idiom scale565Oct already uses in that file (~line 234: PIE is
// thread-context-only coprocessor CP3, q-registers are saved lazily per
// task, SAR is part of the ordinary context frame so it survives an
// interrupt or task switch -- see that comment for the full citation,
// TRM v1.8 SS1.8.128; not re-derived here since nothing about that changes
// for this kernel).
//
// Xtensa-only (guarded __XTENSA__, matching span_scan.h's
// scanRow_pie_asm and overlay_blend.cpp's blendRow_pie_asm precedent):
// cannot build for the host. Compiled and REPORTED (not linked, not run)
// with the real device toolchain -- see build_and_report.sh in this
// directory, which mirrors tools/overlaybench/asm.sh's flags exactly
// (-O2 -std=gnu++20 -mlongcalls -fno-exceptions -fno-rtti, matching
// platformio.ini's [display_common] override of the upstream -Os default
// for this file specifically) -- and never executed anywhere: no device,
// no QEMU harness was available for this pass. See blend_group8.S for the
// standalone assembler-syntax probe this is kept in lockstep with
// (verified automatically by prove_asm_match.py, not by eyeballing), and
// blend_interp.h/prove_interp.cpp for the semantic-trace proof of the
// arithmetic itself.
//
// ---------------------------------------------------------------------
// The a==255 counterexample blendGroup8General must never be handed a
// lane for (worked by hand, independent of the one in blend_model.h's
// header comment, same conclusion): fg raw R = 31 (max, i.e. fg bit-
// positioned red = 0xF800), bg raw R = 1 (bg bit-positioned red = 0x0800),
// a = 255, inv = 1. blend565_ref computes
//   r = ((0xF800*255) + (0x0800*1)) >> 8
//     = (16,189,440 + 2,048) >> 8 = 16,191,488 >> 8 = 63,248 = 0xF6F0...
// masked to 0xF800: 63248 & 0xF800 = 63248 (binary 1111 0110 1111 0000,
// top 5 bits 11110 = 30) -- raw output R = 30, not fg's 31. So even with
// bg arbitrarily close to fg, full alpha does not reproduce fg exactly
// through the weighted-average formula; blendRow_ref's `a == 255 ? c :
// blend565(...)` special case is load-bearing, and any group containing an
// a==255 lane MUST take the copy path for that lane, never this one. The
// caller (blendRow_pie_general_asm below) enforces this by construction:
// the vector-general path below is only ever reached when the group-level
// gather found zero a==255 lanes.
#if defined(__XTENSA__)
#include "blend_ref.h"
#include <cstddef>
#include <cstdint>

namespace blendopt {

// Constant table for blendGroup8General, loaded sequentially via one
// auto-incrementing pointer (a7 in blend_group8.S) exactly the way
// kPieMasks is consumed in scale565Oct -- order is load-bearing, it must
// match the sequence of `ee.vld.128.ip qN, a7, 16` calls in the asm block
// below (and in blend_group8.S, which prove_asm_match.py checks stays in
// lockstep with this file).
alignas(16) static const uint16_t kBlendGroupConsts[48] = {
    // ones (0x0001 x8): the "multiply by 1, shift by SAR" primitive used
    // both to extract a channel's raw magnitude (SAR = the channel's bit
    // position) and to do the final >>8 divide (SAR = 8) -- EE.VMUL.U16
    // only ever shifts right, so this one constant vector serves every
    // right-shift this kernel needs, regardless of the shift amount.
    1, 1, 1, 1, 1, 1, 1, 1,
    // maskR (0xF800 x8)
    0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800,
    // constR2048 (0x0800 x8): repositions raw R (0..31) back to bit 11 via
    // a multiply-by-2048 at SAR=0 (a left shift done as a multiply, since
    // there is no left-shift vector instruction in this ISA).
    2048, 2048, 2048, 2048, 2048, 2048, 2048, 2048,
    // maskG (0x07E0 x8)
    0x07E0, 0x07E0, 0x07E0, 0x07E0, 0x07E0, 0x07E0, 0x07E0, 0x07E0,
    // constG32 (0x0020 x8): repositions raw G (0..63) back to bit 5.
    32, 32, 32, 32, 32, 32, 32, 32,
    // maskB (0x001F x8): blue sits at bit 0, so this mask alone already
    // yields blue's raw magnitude -- no extraction shift and no
    // repositioning multiply are needed for this channel (see the R/G/B
    // asymmetry called out in blend_group8.S's per-channel comments).
    0x001F, 0x001F, 0x001F, 0x001F, 0x001F, 0x001F, 0x001F, 0x001F,
};

// One 8-pixel group's worth of blend565_ref, vectorised, for the case
// where NO lane in the group has a==255 (the caller guarantees this).
// Safe for a==0 lanes: the derivation in blend_model.h's header comment
// shows the raw-magnitude formula is an exact identity at a==0 (reproduces
// bg unchanged), confirmed both by prove_layer1.cpp (host) and
// prove_interp.cpp (this exact instruction sequence, semantically traced).
//
// dst8, colStage8, aStage8, invStage8 must all be 16-byte aligned:
// EE.VLD.128/EE.VST.128 force the low four address bits to zero rather
// than trapping, so misalignment here corrupts neighbouring memory
// silently instead of failing -- same warning as scale565Oct's.
// aStage8/invStage8 are uint16_t (not uint8_t): each lane's alpha/inv is
// widened to 16 bits during the (unavoidably scalar) gather in the caller,
// because EE.VMUL.U16 operates on 16-bit lanes.
//
// Register budget: q0 (fg), q1 (bg), q2 (a), q3 (inv), q7 (ones) are
// pinned for the whole function -- five of the eight q-registers, needed
// by every channel. That leaves only q4/q5/q6 free, not enough to also
// hold a full SAR-batched-by-shift-amount schedule (which would need two
// full channels' raw fg+raw bg live at once). Channels are processed
// serially instead (R, then G, then B), with q6 doubling as the running
// OR-accumulator so no channel's finished value needs a spill to memory.
// This costs more `ssai` mode changes than a batched schedule would (9
// here; see blend_group8.S's header comment for the batched alternative
// that was not used) but needs zero register spills. See the report for
// the SAR-threading risk this trades in for: get the ssai sequence wrong
// and a later multiply silently uses the wrong shift amount, which is
// exactly the class of bug prove_interp.cpp exists to catch by executing
// this exact sequence's SAR state transitions, not just eyeballing them.
__attribute__((noinline)) static void blendGroup8General(uint16_t *__restrict dst8,
                                                          const uint16_t *__restrict colStage8,
                                                          const uint16_t *__restrict aStage8,
                                                          const uint16_t *__restrict invStage8) {
    const uint16_t *rd = dst8;
    uint16_t *wr = dst8;
    const uint16_t *col = colStage8;
    const uint16_t *av = aStage8;
    const uint16_t *iv = invStage8;
    const uint16_t *ct = kBlendGroupConsts;
    // This instruction sequence (mnemonics, operand registers, ssai
    // immediates, and load/store order) is kept in exact lockstep with
    // blend_group8.S -- see prove_asm_match.py, which extracts and diffs
    // the mnemonic+operand stream from both files so they cannot silently
    // drift apart. blend_group8.S carries the full per-channel derivation
    // comments; this block intentionally does not repeat them.
    asm volatile("ee.vld.128.ip q7, %[ct], 16\n"
                 "ee.vld.128.ip q0, %[col], 16\n"
                 "ee.vld.128.ip q1, %[rd], 16\n"
                 "ee.vld.128.ip q2, %[av], 16\n"
                 "ee.vld.128.ip q3, %[iv], 16\n"
                 // ---- R channel ----
                 "ee.vld.128.ip q4, %[ct], 16\n"
                 "ee.andq q5, q0, q4\n"
                 "ee.andq q4, q1, q4\n"
                 "ssai 11\n"
                 "ee.vmul.u16 q5, q5, q7\n"
                 "ee.vmul.u16 q4, q4, q7\n"
                 "ssai 0\n"
                 "ee.vmul.u16 q5, q5, q2\n"
                 "ee.vmul.u16 q4, q4, q3\n"
                 "ee.vadds.s16 q5, q5, q4\n"
                 "ssai 8\n"
                 "ee.vmul.u16 q5, q5, q7\n"
                 "ee.vld.128.ip q4, %[ct], 16\n"
                 "ssai 0\n"
                 "ee.vmul.u16 q6, q5, q4\n"
                 // ---- G channel ----
                 "ee.vld.128.ip q4, %[ct], 16\n"
                 "ee.andq q5, q0, q4\n"
                 "ee.andq q4, q1, q4\n"
                 "ssai 5\n"
                 "ee.vmul.u16 q5, q5, q7\n"
                 "ee.vmul.u16 q4, q4, q7\n"
                 "ssai 0\n"
                 "ee.vmul.u16 q5, q5, q2\n"
                 "ee.vmul.u16 q4, q4, q3\n"
                 "ee.vadds.s16 q5, q5, q4\n"
                 "ssai 8\n"
                 "ee.vmul.u16 q5, q5, q7\n"
                 "ee.vld.128.ip q4, %[ct], 16\n"
                 "ssai 0\n"
                 "ee.vmul.u16 q5, q5, q4\n"
                 "ee.orq q6, q6, q5\n"
                 // ---- B channel ----
                 "ee.vld.128.ip q4, %[ct], 16\n"
                 "ee.andq q5, q0, q4\n"
                 "ee.andq q4, q1, q4\n"
                 "ee.vmul.u16 q5, q5, q2\n"
                 "ee.vmul.u16 q4, q4, q3\n"
                 "ee.vadds.s16 q5, q5, q4\n"
                 "ssai 8\n"
                 "ee.vmul.u16 q5, q5, q7\n"
                 "ee.orq q6, q6, q5\n"
                 "ee.vst.128.ip q6, %[wr], 16\n"
                 : [col] "+r"(col), [rd] "+r"(rd), [wr] "+r"(wr), [av] "+r"(av), [iv] "+r"(iv), [ct] "+r"(ct)
                 :
                 : "memory");
}

// Complete blendRow replacement: same run-walking structure as
// blendRow_group8_model (blend_model.h), with the vector arithmetic now
// real EE.* instructions instead of a C model, and the all-opaque path
// implemented the same way the sibling's blendRow_pie_asm already does
// (plain aligned EE.VLD.128/EE.VST.128 copy, no arithmetic to get wrong).
// noinline for the same register-pressure reason blendRow itself is
// noinline in SleepAnimation.cpp (see the comment there).
//
// GM_TOUCH_PROBE-gated per-path counters, added after the team lead's
// on-rig GM_RUNSTAT census (2026-08-31: mean run width 13.8px, not the
// ~1.7-5px this kernel's design was originally sized against; 50.4% of
// walked pixels fall inside a whole 8-aligned group) established the
// geometric ceiling but not the group-TYPE composition within it -- this
// kernel only wins on opaqueGroups/blendGroups, not mixedGroups, so the
// composition is what actually decides whether shipping it helps. These
// counters answer that question directly if/when this function itself is
// flashed (e.g. for an A/B soak), as an alternative to a separate
// classification-only census build. Not gated behind GM_ANIM_BENCH: same
// reasoning SleepAnimation.h gives for its own always-on instrument
// accessors ("the counters they expose exist in every build... putting
// them on one side of it once already broke display-bench") -- these are
// GM_TOUCH_PROBE-gated instead, matching the team lead's own framing of
// the alternative (a "GM_TOUCH_PROBE-only" classification build), so a
// touch-probe build gets group-composition truth for free from either
// path.
#ifdef GM_TOUCH_PROBE
struct BlendGroupStats {
    uint32_t opaqueGroups = 0; // every lane a==255 -> vector copy path
    uint32_t blendGroups = 0;  // no lane a==255 -> vector arithmetic path (blendGroup8General)
    uint32_t mixedGroups = 0;  // some lanes a==255, some not -> scalar fallback, no win
    uint32_t prologueEpiloguePx = 0; // pixels never eligible for any group path at all
};
// Render-task-private, no locking, same reasoning as SleepAnimation.cpp's
// other GM_TOUCH_PROBE-only accumulators (e.g. spanPxLocal): read once per
// frame/publish by whoever drains it, never touched by another task.
BlendGroupStats g_blendGroupStats;
#endif
__attribute__((noinline)) void blendRow_pie_general_asm(uint16_t *__restrict dst, const uint8_t *__restrict colour,
                                                         const uint32_t *__restrict runs, int nRuns) {
    for (int i = 0; i < nRuns; i++) {
        const uint32_t r = runs[i];
        int x = static_cast<int>(r & 0xFFFFu);
        const int xEnd = static_cast<int>(r >> 16);

        int xAlignedStart = (x + 7) & ~7;
        if (xAlignedStart > xEnd) {
            xAlignedStart = xEnd;
        }
        const int xAlignedEnd = xAlignedStart + ((xEnd - xAlignedStart) & ~7);

        {
            const uint8_t *px = colour + static_cast<size_t>(x) * 3;
#ifdef GM_TOUCH_PROBE
            g_blendGroupStats.prologueEpiloguePx += static_cast<uint32_t>(xAlignedStart - x);
#endif
            for (; x < xAlignedStart; x++, px += 3) {
                blendPixelScalar_ref(dst, px, x);
            }
        }
        for (; x < xAlignedEnd; x += 8) {
            alignas(16) uint16_t colStage[8];
            alignas(16) uint16_t aStage[8];
            alignas(16) uint16_t invStage[8];
            bool allOpaque = true;
            bool anyOpaque = false;
            {
                const uint8_t *gp = colour + static_cast<size_t>(x) * 3;
                for (int k = 0; k < 8; k++, gp += 3) {
                    colStage[k] = static_cast<uint16_t>(gp[0] | (gp[1] << 8));
                    const uint8_t a = gp[2];
                    aStage[k] = a;
                    invStage[k] = static_cast<uint16_t>(256u - a);
                    if (a == 255) {
                        anyOpaque = true;
                    } else {
                        allOpaque = false;
                    }
                }
            }
            if (allOpaque) {
#ifdef GM_TOUCH_PROBE
                g_blendGroupStats.opaqueGroups++;
#endif
                const uint16_t *src = colStage;
                uint16_t *wr = dst + x;
                asm volatile("ee.vld.128.ip q0, %[src], 16\n"
                             "ee.vst.128.ip q0, %[wr], 16\n"
                             : [src] "+r"(src), [wr] "+r"(wr)
                             :
                             : "memory");
            } else if (!anyOpaque) {
#ifdef GM_TOUCH_PROBE
                g_blendGroupStats.blendGroups++;
#endif
                blendGroup8General(dst + x, colStage, aStage, invStage);
            } else {
#ifdef GM_TOUCH_PROBE
                g_blendGroupStats.mixedGroups++;
#endif
                const uint8_t *px = colour + static_cast<size_t>(x) * 3;
                for (int k = 0; k < 8; k++, px += 3) {
                    blendPixelScalar_ref(dst, px, x + k);
                }
            }
        }
        {
            const uint8_t *px = colour + static_cast<size_t>(x) * 3;
#ifdef GM_TOUCH_PROBE
            g_blendGroupStats.prologueEpiloguePx += static_cast<uint32_t>(xEnd - x);
#endif
            for (; x < xEnd; x++, px += 3) {
                blendPixelScalar_ref(dst, px, x);
            }
        }
    }
}

} // namespace blendopt
#endif // defined(__XTENSA__)
