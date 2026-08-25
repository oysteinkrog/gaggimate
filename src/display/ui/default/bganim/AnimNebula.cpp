#ifndef GAGGIMATE_SIM

// "Nebula" — deep-space clouds from three samples of the shared tileable
// noise texture at 1x/2x/4x scale with independent drift directions (cheap
// multi-octave turbulence from one 64KB asset). The dominant 1x octave is
// bilinear-sampled to kill banding; 2x/4x are nearest. Scroll state lives in
// persistent Q8.8 accumulators whose overflow is texel-aligned, so the drift
// never jumps. Design: anim-atmosphere (Fable), 2026-08-15.
//
// Palette is stored "padded" (same trick as Ember): paletteExt has PAD clamp
// entries on each side of the real 256-entry ramp, so the per-pixel index
// (linear blend of a/b/c plus density offset plus dither) can be used to
// index paletteExt directly with no branch to clamp into [0,255] — the pad
// entries already hold the clamped edge color. Range proof: the a/b/c blend
// itself is a convex combination (wA+wB<=64, wC=64-wA-wB>=0 for all
// turbulence 0-100) so it stays within a texel or two of [0,255]; density
// offset is (p[1]-50)*1.1 in [-55,55]; dither is (bayerRow-31)/4 in [-7,8].
// Empirically swept (see tools/animbench worklog) across full param range x
// full a/b/c grid: v in [-62,318]. PAD=72 covers that with margin.
//
// The dominant-octave blend loop (blendedA, built once per row and reused
// for the whole 480px row via wraparound) special-cases the last texel
// instead of masking `(i+1)&255` every iteration — same win as below, lets
// the compiler treat it as a plain counted loop.
//
// Per-pixel dither is precomputed into a row-local int[8] (not int8_t) so
// the lookup is a plain 32-bit load with no sign-extend; densOff is folded
// into the same table (dith2 = dith + densOff) so the combine loop's
// per-pixel "+ densOff" is free.
//
// Two register-pressure ideas were tried on the real Xtensa compiler and
// both measured NEUTRAL-TO-WORSE, so they were reverted rather than kept
// for looks:
//   - An 8-wide manual unroll (lambdas, compile-time Bayer column) tanked
//     register allocation -- wA/wB/densOff and even x0/bIdx/cIdx spilled to
//     the stack every pixel (622 insns in band(), vs ~208 single-pixel).
//   - Packing wA/wB/densOff into one uint32_t to free a register: verified
//     via xtensa-asm that GCC just hoists the unpack above the loop (it's
//     loop-invariant) and still spills the three unpacked scalars to three
//     stack slots inside the loop -- same reloads, plus the unpack cost.
//     Net +5 instructions in band() for zero benefit; reverted.
// A 2-wide manual unroll with one paired 32-bit store (Aurora/Plasma/Silk/
// Lava's trick) was also tried and measured a hair faster on host (0.381 ->
// 0.377ms) -- but xtensa-asm showed it costs the REAL win: the compiler no
// longer recognizes the unrolled main loop as a simple counted loop, so it
// loses its hardware zero-overhead LOOP instruction (falls back to an
// ordinary compare-and-branch every w/2 iterations) while only the small
// per-row dith/blendedA loops keep theirs. A real per-iteration branch on
// Xtensa costs more than the store-pairing saves, so this was reverted --
// host timing said "win", the real compiler's codegen said "regression".
//
// b (2x octave) and c (4x octave) are both nearest-sampled with a fixed
// per-pixel stride (+2, +4 mod 256) -- unlike a's index (x0, stride 1),
// bIdx/cIdx as a function of pixel step repeat with period 128. They are
// packed into one uint16 table (bcTable, below) walked by that shared step
// counter, the same "two same-index uint8 tables -> one uint16 table" trick
// that won on mandala: it collapses rowB-ptr + rowC-ptr + bIdx + cIdx (4
// live values) down to one table pointer + one step index (2). This is a
// HOST REGRESSION (0.382 -> 0.436ms) because x86 was never spilling in the
// first place -- the 128-iteration per-row precompute is pure added cost
// there. But on the real compiler it is a clear win, verified in the .S:
// wA/wB/densOff now stay resident in registers for the entire main pixel
// loop (previously wA and wB were reloaded from the stack every pixel; grep
// xtensa-asm/AnimNebula.S's main loop body for "wA"/"wB" -- there are no
// stack loads for them left), and the main loop KEEPS its hardware
// zero-overhead LOOP instruction (at the time, all four loop bounds in
// band() -- dith k=8, blendedA i=255, bcTable k=128, the shared combine loop
// -- got one; see the 2026-08-17 note below for how that shared combine loop
// was later split into two, and why). Golden stays bit-exact. Do not revert
// this on host-number regression alone; check the .S first.
//
// The combine step used to write into a separate `fullColor[256]` scratch
// table, then a masked copy loop (`m = (m+1)&255`) read back through it for
// every one of w pixels to fill the row -- even the first 256, which the
// combine loop had just computed in the same order. Since v(x) == v(x mod
// 256) (see the combine-step comment below), row[0,256) after the combine
// loop already IS that one period, so `row` doubles as its own memo table:
// the combine loop writes straight into `row`, and only the entries beyond
// the first period (w-256 of them, 224 at the 480px panel width) need
// copying, via a fixed -256 pointer offset instead of a mask. This alone
// (still sharing one combine loop between the w<=256 and w>256 cases via a
// runtime bound) was a real host win (0.368 -> 0.305ms) but cost bcTable its
// hardware zero-overhead LOOP instruction in the real compiler: the new
// tail-copy loop is a 5th loop candidate in the function, and xtensa-asm
// showed GCC dropping the ternary-bounded combine loop's sibling (bcTable,
// 128 iterations) to a plain compare-and-branch to make room, even though
// nothing in bcTable's own code changed -- a whole-function register/loop
// budget effect, not a local one. Splitting the combine loop into two
// separate copies (one bounded by `w` for the w<=256 path, one by the
// literal 256 for the tail-copy path) removed the shared runtime bound and
// let all 5 loops in band() keep their hardware LOOP instruction (verified:
// `grep -n loop xtensa-asm/AnimNebula.S` inside band()'s address range).
// Only one copy ever executes per call (w decides which branch, and it does
// not change between calls to the same band() invocation), so the ~30
// duplicated instructions are flash cost, not runtime cost, while the tail
// copy's iteration count itself shrank from w/2 to (w-256)/2 (240 -> 112 at
// w=480). Net measured: 0.368 -> 0.281ms host (24%), bit-exact vs golden at
// f030/f120/f210, and separately verified that band() called once per row at
// w=240 (mimicking SleepAnimation's interlaced half-res path) reproduces the
// same first-240-columns values as the w=480 path, column for column.
//
// The 0.368/0.281 pair above was measured while tools/animbench/bench.cpp's
// BAND_H was stale at 16 (the device moved to 8 rows/band at 7fede9c9; the
// bench constant was not updated until later). Band height is not a uniform
// scale factor -- it moves different animations different directions by up
// to 9%, so old figures cannot be rescaled arithmetically. Re-measured at the
// corrected BAND_H=8: baseline (this file, pre-locality-pass below) 0.291ms,
// still comfortably under the 0.3125ms 40fps bar.
//
// 2026-08-17 (second pass): chased the locality lead GM_NEBULA_CACHED_NOISE_
// PROBE flags -- at BAND_H=8 it measures band_ms 0.291 -> ~0.242ms (~17%) for
// pinning all four noise samplers to one resident row, i.e. that fraction of
// cost is noise-texture addressing/locality on the 64KB asset, not the
// per-pixel arithmetic. The dominant octave (rowA0/rowA1, bilinear) is the
// only one of the four with real reuse available: rowA0 at row y+1 is the
// exact same physical texture row as rowA1 at row y (both are
// (y+1+ayI)&255), so its x-interpolated form is now cached in a ping-ponged
// buffer (ixBufs, below) and carried from one row to the next INSIDE a
// single band() call, cutting the octave's raw noise-row touches from 16 to
// 9 per 8-row call. This is intra-call only -- reset every call -- so it
// does not weaken the cross-call invariant one row below discusses.
// Bit-exact vs golden (all 13 animations), and separately verified via a
// throwaway harness against band() called as: one call for the whole frame,
// 4-row bands, 16-row bands, ragged 3-row bands, sequential rows==1 calls,
// and parity-skipped rows==1 calls (SleepAnimation's interlaced shape) --
// zero pixel mismatches in every shape. Host result is flat (0.291 ->
// ~0.288ms, inside the +-4% noise floor): expected and unpriced by
// construction, since the host bench keeps the whole 64KB texture resident
// in L2 the whole time, so a redundant re-read there is nearly free in a way
// a real PSRAM row fetch on the S3 is not. Building bcTable BEFORE this
// block (rather than after, where it was) was necessary to keep bcTable's
// own, unrelated loop from losing its hardware zero-overhead LOOP
// instruction to the same whole-function register/loop-budget effect noted
// above for the tail-copy split -- verified via xtensa-asm: band() went 269
// -> 301 insns, 5 -> 7 hardware loops (all seven loop candidates in the
// function keep the hardware LOOP instruction; none fell back to
// compare-and-branch), with no new libcalls.
//
// 2026-08-25: measured GM_NEBULA_CACHED_NOISE_PROBE on the device rather than
// on the host, and it closes the locality lead rather than opening it. Pinning
// all four samplers to one resident row takes band_us 22773 -> 19131 (-16%)
// and the animation 33.9 -> 38.3 fps. That is the ceiling, not an estimate of
// a fix: the probe's working set is 256 bytes, while any version that still
// draws the right picture has to reach ~25 texture rows per 8-row band. So
// perfect noise locality does not get this animation to 40 fps, and the
// remaining 19.1 ms is the per-pixel arithmetic the passes above already went
// at. Do not spend another pass on caching the noise texture.
//
// Nebula is the only one of the thirteen below 40 fps; the other twelve run
// 49.6 to 59.3. The composite stage is no longer the constraint for any of
// them (3.8 ms, flat, since the scrim moved to the PIE vector unit).
//
// Optimized: opt-nebula, 2026-08-15 + 2026-08-17 + 2026-08-17b.

#include "BgAnim.h"
#include "BgAnimCommon.h"

namespace {
using namespace bganim;

// Padding either side of the 256-entry palette ramp (see file header proof).
constexpr int PAD = 72;
constexpr int PAL_EXT_N = 256 + 2 * PAD;

uint16_t *paletteExt = nullptr; // [PAL_EXT_N]; real ramp lives at paletteExt+PAD
uint16_t *palette = nullptr;    // = paletteExt + PAD, 256 entries
const uint8_t *noise = nullptr;
uint32_t lastThemeGen = 0xFFFFFFFF;
// Q8.8 scroll accumulators — texel-aligned wraparound (65536 = 256 texels).
uint16_t sAx = 0, sAy = 0, sBx = 0, sBy = 0, sCx = 0, sCy = 0;
int g_wA = 32, g_wB = 20, g_wC = 12, g_densOff = 0;
int g_axI = 0, g_axF = 0, g_ayI = 0, g_ayF = 0, g_bx = 0, g_by = 0, g_cx = 0, g_cy = 0;

// Fills the clamp padding around the freshly-rebuilt 256-entry ramp so
// paletteExt[PAD + v] is valid for v in [-PAD, 255+PAD] with no branch.
void extendPalette() {
    const uint16_t lo = palette[0];
    const uint16_t hi = palette[255];
    for (int i = 0; i < PAD; i++) {
        paletteExt[i] = lo;
        paletteExt[PAD + 256 + i] = hi;
    }
}

bool init(int, int) {
    if (paletteExt == nullptr) {
        paletteExt = static_cast<uint16_t *>(alloc(PAL_EXT_N * sizeof(uint16_t)));
        noise = noiseTex256();
        if (paletteExt == nullptr || noise == nullptr) {
            return false;
        }
        palette = paletteExt + PAD;
    }
    lastThemeGen = 0xFFFFFFFF;
    return true;
}

void frame(uint32_t, int, int, const uint8_t p[4]) {
    if (themeGen() != lastThemeGen) {
        buildThemeRamp(palette, 256);
        extendPalette();
        lastThemeGen = themeGen();
    }
    // Per-frame deltas matched to the web preview at ~30fps: px/frame * 256.
    const float g = 1.2f * speedMul(p[0]);
    sAx += static_cast<uint16_t>(85.0f * g);
    sAy += static_cast<uint16_t>(51.0f * g);
    sBx -= static_cast<uint16_t>(145.0f * g);
    sBy += static_cast<uint16_t>(111.0f * g);
    sCx += static_cast<uint16_t>(222.0f * g);
    sCy -= static_cast<uint16_t>(179.0f * g);
    const float turb = p[2] / 100.0f;
    g_wA = static_cast<int>((0.60f - 0.15f * turb) * 64.0f);
    g_wB = static_cast<int>((0.25f + 0.05f * turb) * 64.0f);
    g_wC = 64 - g_wA - g_wB;
    g_densOff = static_cast<int>((p[1] - 50) * 1.1f);
    g_axI = sAx >> 8;
    g_axF = sAx & 0xFF;
    g_ayI = sAy >> 8;
    g_ayF = sAy & 0xFF;
    g_bx = sBx >> 8;
    g_by = sBy >> 8;
    g_cx = sCx >> 8;
    g_cy = sCy >> 8;
}

#if defined(__XTENSA__)
// out[i] = a[i] + (((b[i] - a[i]) * f) >> 8), sixteen bytes per group, on the
// ESP32-S3's PIE vector unit.
//
// Why this loop and not the pixel loop: the two dominant-octave tables run 255
// and 256 iterations per row to feed a pixel loop of w, and at the half
// resolution this panel renders at, w is 240. Removing them outright measured
// 33.6 -> 44.8 fps on the device, so they, not the per-pixel arithmetic the
// earlier passes went at and not noise locality, are where the time goes.
//
// The unit multiplies 16-bit lanes and the data is bytes, so a group widens
// then narrows:
//   EE.VZIP.8 qs0, qs1 interleaves the two registers' bytes and writes BOTH
//   of them. Zipped against a zeroed register that is a zero-extending widen:
//   the low eight bytes become eight 16-bit lanes in qs0 and the high eight
//   become eight more in qs1. One instruction for both halves, which is why
//   the zero is re-made per zip rather than kept in a register.
//   EE.VUNZIP.8 is the inverse, taking every other byte of the pair, and on
//   little-endian 16-bit lanes those are the low bytes -- the same truncation
//   the scalar store did.
//
// EE.VMUL.S16 shifts the full 32-bit product ARITHMETICALLY by SAR before
// keeping the low 16 bits, and the arithmetic part is load-bearing because
// b - a is signed. EE.VSUBS/EE.VADDS.S16 saturate, which never fires here: a
// lerp between two bytes cannot leave 0..255 for any factor in 0..255.
//
// PIE is coprocessor CP3, legal in thread context only; this runs on the
// render task. SAR is in the ordinary context frame, so the ssai is hoisted
// out of the loop and survives an interrupt.
//
// Sixteen instructions per sixteen outputs against roughly seven per output
// scalar. Checked against the scalar form over every (a, b, f) triple on the
// device, because the widen and narrow orderings and the sign of that shift
// all fail as wrong pixels rather than as a fault.
__attribute__((noinline)) static void lerpRowPie(uint8_t *__restrict out, const uint8_t *__restrict a,
                                                 const uint8_t *__restrict b, const uint16_t *__restrict fv, int n16) {
    const uint8_t *pa = a;
    const uint8_t *pb = b;
    uint8_t *po = out;
    const uint16_t *pf = fv;
    int n = n16;
    asm volatile("ee.vld.128.ip q7, %[f], 0\n" // eight copies of f, resident
                 "ssai 8\n"
                 "1:\n"
                 "ee.vld.128.ip q0, %[pa], 16\n"
                 "ee.vld.128.ip q1, %[pb], 16\n"
                 "ee.zero.q q2\n"
                 "ee.vzip.8 q0, q2\n" // q0 = a lanes 0-7, q2 = a lanes 8-15
                 "ee.zero.q q3\n"
                 "ee.vzip.8 q1, q3\n" // q1 = b lanes 0-7, q3 = b lanes 8-15
                 "ee.vsubs.s16 q4, q1, q0\n"
                 "ee.vsubs.s16 q5, q3, q2\n"
                 "ee.vmul.s16 q4, q4, q7\n" // ((b-a)*f) >> 8, arithmetic
                 "ee.vmul.s16 q5, q5, q7\n"
                 "ee.vadds.s16 q0, q0, q4\n"
                 "ee.vadds.s16 q2, q2, q5\n"
                 "ee.vunzip.8 q0, q2\n" // sixteen lanes back to sixteen bytes
                 "ee.vst.128.ip q0, %[po], 16\n"
                 "addi %[n], %[n], -1\n"
                 "bnez %[n], 1b\n"
                 : [pa] "+r"(pa), [pb] "+r"(pb), [po] "+r"(po), [f] "+r"(pf), [n] "+r"(n)
                 :
                 : "memory");
}

// out[i] = a[i] + (((a[i+1] - a[i]) * f) >> 8), the x-interpolation, sixteen
// bytes per group.
//
// Same lerp as lerpRowPie, but the second operand is the first shifted by one
// byte, and a 128-bit load cannot start at an odd address: EE.VLD.128 forces
// the low four address bits to zero. The hardware's answer is SAR_BYTE.
// EE.LD.128.USAR.IP loads the block containing an unaligned address and saves
// that address's low four bits into SAR_BYTE, and EE.SRC.Q then shifts the
// 32-byte concatenation of two consecutive aligned blocks right by SAR_BYTE
// bytes, which is the unaligned window. Pointing the setup load at a+1 makes
// SAR_BYTE 1 for the whole loop.
//
// So each iteration keeps the previous aligned block, loads the next, and
// derives the shifted vector in-register. That is one extra load and one
// EE.SRC.Q per group instead of a separate 256-byte shifted copy of the row.
//
// n16 is 15, not 16, deliberately: iteration k reads block k+1 and writes
// block k, so fifteen groups touch bytes 0..255 exactly and never read past
// the row. The last sixteen entries, including the wrap where a[256] means
// a[0], are the caller's scalar tail.
__attribute__((noinline)) static void lerpShiftRowPie(uint8_t *__restrict out, const uint8_t *__restrict a,
                                                      const uint16_t *__restrict fv, int n16) {
    const uint8_t *pa = a + 1; // sets SAR_BYTE = 1 in the setup load
    uint8_t *po = out;
    const uint16_t *pf = fv;
    int n = n16;
    asm volatile("ee.vld.128.ip q7, %[f], 0\n"
                 "ee.ld.128.usar.ip q6, %[pa], 16\n" // block 0, SAR_BYTE = 1
                 "ssai 8\n"
                 "1:\n"
                 "ee.vld.128.ip q1, %[pa], 16\n" // next aligned block
                 "ee.src.q q2, q6, q1\n"         // a[i+1 .. i+16]
                 "ee.orq q3, q1, q1\n"           // stash it for the next pass
                 "ee.zero.q q4\n"
                 "ee.vzip.8 q6, q4\n" // a lanes
                 "ee.zero.q q5\n"
                 "ee.vzip.8 q2, q5\n" // shifted lanes
                 "ee.vsubs.s16 q0, q2, q6\n"
                 "ee.vsubs.s16 q1, q5, q4\n"
                 "ee.vmul.s16 q0, q0, q7\n"
                 "ee.vmul.s16 q1, q1, q7\n"
                 "ee.vadds.s16 q6, q6, q0\n"
                 "ee.vadds.s16 q4, q4, q1\n"
                 "ee.vunzip.8 q6, q4\n"
                 "ee.vst.128.ip q6, %[po], 16\n"
                 "ee.orq q6, q3, q3\n" // next iteration's unshifted block
                 "addi %[n], %[n], -1\n"
                 "bnez %[n], 1b\n"
                 : [pa] "+r"(pa), [po] "+r"(po), [f] "+r"(pf), [n] "+r"(n)
                 :
                 : "memory");
}

// The same arithmetic, for the host bench and as the reference the device
// self-test compares against.
static inline uint8_t lerpScalar(int a, int b, int f) {
    return static_cast<uint8_t>(a + (((b - a) * f) >> 8));
}

// Every input the kernel can see: 256 values of a, by 256 of b, by 256 of f.
// Returns the mismatch count and, on the first one, packs a, b and f into
// *firstBad. Diagnostic only, and it blocks for about a second.
uint32_t nebulaLerpSelfTest(uint32_t *firstBad) {
    alignas(16) uint8_t va[16], vb[16], vo[16];
    alignas(16) uint16_t fv[8];
    uint32_t bad = 0;
    for (int f = 0; f < 256; f++) {
        for (int k = 0; k < 8; k++) {
            fv[k] = static_cast<uint16_t>(f);
        }
        for (int a = 0; a < 256; a++) {
            for (int b = 0; b < 256; b += 16) {
                for (int k = 0; k < 16; k++) {
                    va[k] = static_cast<uint8_t>(a);
                    vb[k] = static_cast<uint8_t>(b + k);
                }
                lerpRowPie(vo, va, vb, fv, 1);
                for (int k = 0; k < 16; k++) {
                    if (vo[k] != lerpScalar(a, b + k, f)) {
                        if (bad == 0 && firstBad != nullptr) {
                            *firstBad = static_cast<uint32_t>(a) | (static_cast<uint32_t>(b + k) << 8) |
                                        (static_cast<uint32_t>(f) << 16);
                        }
                        bad++;
                    }
                }
            }
        }
    }
    return bad;
}

// The whole x-interpolation as band() actually calls it: the vector kernel
// over the first fifteen groups plus the scalar tail that finishes the row and
// closes the wrap, against the original single scalar loop, over real noise
// texture rows.
//
// The two tests above check the kernels on synthetic input. This checks the
// composition, which is where the parts that are not the kernel live: the
// group count, the tail's start index, and the wrap entry at 255. Those are
// exactly the mistakes that would show up as a seam or a band in one place on
// the panel rather than as garbage everywhere, and a photograph of a moving
// animation cannot tell that apart from its own motion smear.
uint32_t nebulaRowSelfTest(uint32_t *firstBad) {
    const uint8_t *tex = noiseTex256();
    if (tex == nullptr) {
        return 0;
    }
    alignas(16) uint8_t got[256];
    uint8_t want[256];
    alignas(16) uint16_t fv[8];
    uint32_t bad = 0;
    for (int row = 0; row < 256; row += 7) { // 37 rows, spread over the texture
        const uint8_t *a = tex + static_cast<size_t>(row) * 256;
        for (int f = 0; f < 256; f += 5) { // 52 factors
            for (int k = 0; k < 8; k++) {
                fv[k] = static_cast<uint16_t>(f);
            }
            // exactly what band() does
            lerpShiftRowPie(got, a, fv, 15);
            for (int i = 240; i < 255; i++) {
                got[i] = lerpScalar(a[i], a[i + 1], f);
            }
            got[255] = lerpScalar(a[255], a[0], f);
            // exactly what band() used to do
            int cur = a[0];
            for (int i = 0; i < 255; i++) {
                const int nxt = a[i + 1];
                want[i] = static_cast<uint8_t>(cur + (((nxt - cur) * f) >> 8));
                cur = nxt;
            }
            want[255] = static_cast<uint8_t>(cur + (((a[0] - cur) * f) >> 8));
            for (int i = 0; i < 256; i++) {
                if (got[i] != want[i]) {
                    if (bad == 0 && firstBad != nullptr) {
                        *firstBad = static_cast<uint32_t>(i) | (static_cast<uint32_t>(f) << 8) |
                                    (static_cast<uint32_t>(row) << 16);
                    }
                    bad++;
                }
            }
        }
    }
    return bad;
}

// The shifted variant. Its arithmetic is the kernel above, already checked
// over every triple, so what is left to establish is that EE.SRC.Q really
// hands back a[i+1] and not some other window. A permutation is settled by
// one input whose elements are all distinct, so a ramp proves the addressing;
// the alternating and reversed patterns then exercise the sign of the
// difference, which a ramp cannot, across every factor.
uint32_t nebulaShiftSelfTest(uint32_t *firstBad) {
    alignas(16) uint8_t in[256];
    alignas(16) uint8_t out[256];
    alignas(16) uint16_t fv[8];
    uint32_t bad = 0;
    for (int pat = 0; pat < 4; pat++) {
        for (int i = 0; i < 256; i++) {
            switch (pat) {
            case 0: in[i] = static_cast<uint8_t>(i); break;               // ramp
            case 1: in[i] = static_cast<uint8_t>(255 - i); break;         // reversed
            case 2: in[i] = (i & 1) != 0 ? 255 : 0; break;               // full swing
            default: in[i] = static_cast<uint8_t>((i * 97 + 13) & 0xFF); // scattered
            }
        }
        for (int f = 0; f < 256; f++) {
            for (int k = 0; k < 8; k++) {
                fv[k] = static_cast<uint16_t>(f);
            }
            for (int i = 0; i < 256; i++) {
                out[i] = 0xAA; // so a group the kernel skips cannot pass by luck
            }
            lerpShiftRowPie(out, in, fv, 15);
            for (int i = 0; i < 240; i++) {
                if (out[i] != lerpScalar(in[i], in[i + 1], f)) {
                    if (bad == 0 && firstBad != nullptr) {
                        *firstBad = static_cast<uint32_t>(i) | (static_cast<uint32_t>(f) << 8) |
                                    (static_cast<uint32_t>(pat) << 16);
                    }
                    bad++;
                }
            }
        }
    }
    return bad;
}

#endif

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const int axF = g_axF, ayF = g_ayF;
    const int wA = g_wA, wB = g_wB, densOff = g_densOff;
    alignas(16) static uint8_t blendedA[256];
    // Ping-ponged x-interpolated copies of the dominant octave's raw noise
    // rows. rowA0(y+1) and rowA1(y) are always the SAME physical texture row
    // ((y+1+ayI)&255 either way -- see the reuse site below), so the
    // x-interpolation of "this row's rowA1" can be reused as "next row's
    // rowA0" instead of re-reading the raw 256-byte row and redoing the
    // 255-iteration interpolation walk. At BAND_H=8 that cuts the octave's
    // raw-row touches from 16 to 9 per band() call -- real PSRAM-traffic
    // reduction, not just a host-cache effect, since noiseTex256() is a 64KB
    // asset that does not fit the S3's 32KB external-memory cache.
    // `int`, not `uint8_t`: this stores the exact same intermediate va/vb
    // values the old fused loop computed and consumed immediately, just
    // deferred by one iteration -- a narrower type here would risk a
    // transient out-of-[0,255] value truncating differently than the
    // original never-stored expression did.
    // Bytes, not ints. The x-interpolated octave is a lerp between two
    // texels, so it cannot leave 0..255 for any factor in 0..255: with
    // nxt >= cur the result lands in [cur, nxt], and with nxt < cur the
    // factor's own bound keeps it above nxt. Storing it 32 bits wide spent
    // 2 KB to hold values that fit in 512 B, and it put the data at the one
    // width the vector unit's 16-bit lanes cannot consume directly. Aligned
    // because the vector kernel below loads 128 bits at a time.
    alignas(16) static uint8_t ixBufs[2][256];
    int curBuf = 0; // ixBufs[curBuf] is valid as "this row's x-interpolated rowA0"
    for (int r = 0; r < rows; r++) {
        const int y = y0 + r;
        uint16_t *row = dst + static_cast<size_t>(r) * w;
        const uint8_t *bayerRow = &BAYER8[(y & 7) * 8];
        // int (not int8_t): a plain 32-bit load, no sign-extend per pixel.
        // densOff is folded in here too (dith2 = dith + densOff) so the
        // combine loop's per-pixel "+ densOff" becomes free -- one table
        // build of 8 adds replaces 256 (or, at half-res, up to 256) adds.
        int dith2[8];
        for (int k = 0; k < 8; k++) {
            dith2[k] = (static_cast<int>(bayerRow[k]) - 31) / 4 + densOff;
        }
#ifdef GM_NEBULA_CACHED_NOISE_PROBE
        // Diagnostic only, visually wrong: pin all four samplers to one noise
        // row so the working set is 256 bytes and always cache-resident. If
        // band time collapses, the cost is PSRAM misses on the 64 KB texture
        // (alloc() puts it there, being far over the 8 KB threshold) and not
        // the per-row table math. Never build this into anything shipping.
        const uint8_t *rowA0 = noise;
        const uint8_t *rowA1 = noise;
        const uint8_t *rowB = noise;
        const uint8_t *rowC = noise;
#else
        const uint8_t *rowA0 = noise + ((y + g_ayI) & 255) * 256;
        const uint8_t *rowA1 = noise + ((y + g_ayI + 1) & 255) * 256;
        const uint8_t *rowB = noise + ((y * 2 + g_by) & 255) * 256;
        const uint8_t *rowC = noise + ((y * 4 + g_cy) & 255) * 256;
#endif

        // Build the bilinear-blended dominant octave once per row — reused
        // for the whole row via the x0 wraparound below. i=255's neighbor
        // (i+1==256) wraps to texel 0; special-cased after each interpolation
        // pass instead of masked every iteration so the loop body is a plain
        // counted walk over two pointers (2026-08-17 update: see below for
        // why there are now two such passes, not one fused pass).
        //
        // Two separable reformulations were tried in an earlier pass and both
        // reverted.
        //
        // (1) y-blend rowA0/rowA1 into a scratch 256-row first, then
        // x-interpolate that single row. Bilinear interpolation commutes
        // between x and y order, so this is the same result up to >>8
        // rounding order. Measured WORSE on both host (0.373ms vs 0.364ms
        // here) and xtensa-asm (243 insns here vs 231), because the y-blend
        // pass couldn't get a plain two-pointer walk -- rowA0 and rowA1
        // aren't necessarily adjacent in the noise texture across the y-wrap
        // boundary, so GCC derived rowA0's address from rowA1's via an extra
        // sub+add every iteration -- and it drifted off golden (mean
        // 0.004-0.02, max 9, still "OK" but non-zero vs bit-exact here). Not
        // revisited: (1) does not expose the identity (2) does, and this
        // section is about that identity now.
        //
        // (2) x-interpolate FIRST into two ping-ponged scratch rows, then
        // y-blend, exploiting the real identity: this row's rowA1 is next
        // row's rowA0, for every y including across the wrap. When first
        // tried, this was implemented as a single fused pass PLUS a second
        // full pass every row regardless of reuse (so it paid the extra
        // pass's cost every row while only sometimes collecting the read
        // savings the pass existed to enable) -- it stayed bit-exact but
        // measured band() at 243 -> 273 insns, 4 -> 5 hardware loops, and
        // 0.357 -> 0.347ms host, inside the +-4% noise floor, with a note to
        // revisit once cross-row reuse was actually wired up rather than just
        // structurally possible.
        //
        // 2026-08-17: wired up. `ixBufs` below stores va/vb, not rowA0/rowA1
        // themselves -- the interpolation for what will become "next row's
        // rowA0" is computed once (into ixNext) and carried via a ping-pong
        // buffer into the next iteration's ixCur, so a full interpolation
        // pass is skipped (not merely redirected through a scratch row) for
        // every row after the first in a multi-row call. At BAND_H=8 that
        // drops the octave's raw-row touches from 16 to 9 per band() call --
        // GM_NEBULA_CACHED_NOISE_PROBE (below) attributes roughly 17% of
        // band_ms to exactly this kind of noise-texture locality, of which
        // this reuse claims the a-octave's share. This is intra-call state
        // only: curBuf resets to ixBufs[0] at the top of every band() call
        // (declared with the call, not with static duration), and r==0 always
        // takes the "compute ixCur fresh" branch below, so it does not touch
        // the "no state carries between band() calls" invariant
        // SleepAnimation.cpp's interlaced half-res path relies on -- a
        // rows==1 call is all r==0, every call, and never reuses anything.
        // See the file header for the measured host delta and why it is
        // expected to matter more on device than the host number shows: the
        // host bench keeps the whole 64 KB texture in L2, so a redundant
        // re-read there is cheap in a way a real PSRAM row is not.
        // b (2x) and c (4x) are both nearest-sampled with a fixed per-pixel
        // stride (+2, +4 mod 256), so unlike a's index they walk in lockstep
        // with the pixel counter itself: bIdx/cIdx as a function of pixel
        // step repeats with period lcm(256/gcd(256,2), 256/gcd(256,4)) = 128.
        // Pack rowB[bIdx]/rowC[cIdx] into one uint16 table indexed directly
        // by that step (mod 128), same trick mandala used for two uint8
        // LUTs sharing an index — this drops rowB/rowC/bIdx/cIdx (4 live
        // registers) down to one table pointer + one index (2), freeing
        // room for wA/wB/densOff to stay resident instead of spilling.
        //
        // Built BEFORE the ixCur/ixNext/blendedA block below (moved there
        // 2026-08-17): bcTable and blendedA are independent -- neither reads
        // the other, both are only consumed together in the w-combine loop
        // further down -- so this ordering is a pure scheduling choice with
        // no effect on the result. It matters for codegen: with the a-octave
        // reuse buffers (ixCur/ixNext) added as two more live loop-carried
        // pointers, building bcTable AFTER them pushed bcTable's own loop
        // past GCC's register/loop budget for this function and cost it its
        // hardware zero-overhead LOOP instruction (verified via xtensa-asm:
        // fell back to `addi.n; bnez.n`), even though nothing in bcTable's
        // code changed -- the same whole-function budget effect documented
        // above for the tail-copy split. Building it first, before ixCur/
        // ixNext become live, lets bcTable's registers free up before that
        // pressure exists and restores its hardware loop.
        static uint16_t bcTable[128];
        {
            int bI = g_bx & 255;
            int cI = g_cx & 255;
            for (int k = 0; k < 128; k++) {
                bcTable[k] = static_cast<uint16_t>(rowB[bI]) | (static_cast<uint16_t>(rowC[cI]) << 8);
                bI = (bI + 2) & 255;
                cI = (cI + 4) & 255;
            }
        }

        uint8_t *ixCur = ixBufs[curBuf];
        uint8_t *ixNext = ixBufs[curBuf ^ 1];
        if (r == 0) {
            // First row of the call: no predecessor to reuse from (a rows==1
            // call always takes only this path), so interpolate rowA0 fresh.
            // Identical expression to the reuse-eligible rowA1 pass below,
            // just addressed at rowA0 -- kept as a separate copy rather than
            // a shared helper so neither loop gains a call in its body.
#if defined(__XTENSA__)
            {
                alignas(16) uint16_t fv[8];
                for (int k = 0; k < 8; k++) {
                    fv[k] = static_cast<uint16_t>(axF);
                }
                lerpShiftRowPie(ixCur, rowA0, fv, 15);
                for (int i = 240; i < 255; i++) {
                    ixCur[i] = lerpScalar(rowA0[i], rowA0[i + 1], axF);
                }
                ixCur[255] = lerpScalar(rowA0[255], rowA0[0], axF);
            }
#else
            int cur0 = rowA0[0];
            for (int i = 0; i < 255; i++) {
                const int nxt0 = rowA0[i + 1];
                ixCur[i] = static_cast<uint8_t>(cur0 + (((nxt0 - cur0) * axF) >> 8));
                cur0 = nxt0;
            }
            ixCur[255] = static_cast<uint8_t>(cur0 + (((rowA0[0] - cur0) * axF) >> 8));
#endif
        }
#ifdef GM_NEBULA_TABLE_PROBE
        // Diagnostic only, visually wrong: skip both dominant-octave tables
        // and let the combine step read whatever they last held. 255 + 256
        // iterations per row build them to serve w output pixels, and at the
        // half resolution this panel renders at, w is 240 -- so the two
        // tables cost more iterations than the pixel loop they feed. This
        // prices removing them outright, which is the ceiling for any
        // restructuring that sizes them to the output width instead of to the
        // noise texture's 256 period. Never build this into anything
        // shipping.
        (void)ayF;
#else
        {
#if defined(__XTENSA__)
            {
                alignas(16) uint16_t fv[8];
                for (int k = 0; k < 8; k++) {
                    fv[k] = static_cast<uint16_t>(axF);
                }
                lerpShiftRowPie(ixNext, rowA1, fv, 15);
                for (int i = 240; i < 255; i++) {
                    ixNext[i] = lerpScalar(rowA1[i], rowA1[i + 1], axF);
                }
                ixNext[255] = lerpScalar(rowA1[255], rowA1[0], axF);
            }
#else
            int cur1 = rowA1[0];
            for (int i = 0; i < 255; i++) {
                const int nxt1 = rowA1[i + 1];
                ixNext[i] = static_cast<uint8_t>(cur1 + (((nxt1 - cur1) * axF) >> 8));
                cur1 = nxt1;
            }
            ixNext[255] = static_cast<uint8_t>(cur1 + (((rowA1[0] - cur1) * axF) >> 8));
#endif
        }
#if defined(__XTENSA__)
        {
            alignas(16) uint16_t fv[8];
            for (int k = 0; k < 8; k++) {
                fv[k] = static_cast<uint16_t>(ayF);
            }
            lerpRowPie(blendedA, ixCur, ixNext, fv, 256 / 16);
        }
#else
        for (int i = 0; i < 256; i++) {
            const int va = ixCur[i];
            const int vb = ixNext[i];
            blendedA[i] = static_cast<uint8_t>(va + (((vb - va) * ayF) >> 8));
        }
#endif
#endif
        // ixNext (this row's interpolated rowA1) is next row's rowA0 -- see
        // the block comment further up for why that identity holds.
        curBuf ^= 1;

        // Combine step: run the full a/b/c blend (identical math to the old
        // per-pixel loop) but only far enough to cover its true repeat
        // period, then reuse it. x0(x) = (g_axI+x)&255 has period 256;
        // step(x) = x&127 has period 128; 128 | 256 so the combined value
        // v(x) — and thus the dith term dith[x&7], since 8 | 256 too — is
        // an exact period-256 function of x: v(x) == v(x mod 256) bit-for-
        // bit, because x0/step/dith-index at x and at (x mod 256) are
        // identical by construction (all three periods divide 256). This
        // turns the expensive blend (loads + 2 muls + shifts) from a
        // per-real-pixel cost (up to w=480/row) into a fixed <=256/row cost.
        //
        // The loop writes straight into `row[0, combineN)` -- there used to
        // be a separate `fullColor` scratch table plus a masked copy loop
        // that read back through it for every one of w pixels, even the
        // first 256 that the combine step had just computed in cache-
        // adjacent order. Since v(x) == v(x mod 256), row[0,256) already
        // *is* the one period the rest of the row needs; row IS the memo
        // table now, so nothing downstream needs to re-touch entries the
        // combine loop already placed correctly.
        if (w <= 256) {
            int x0 = g_axI & 255;
            int step = 0;
            for (int m = 0; m < w; m++) {
                const int a = blendedA[x0];
                const uint16_t bc = bcTable[step];
                const int b = bc & 0xFF;
                const int c = bc >> 8;
                const int v = c + ((((a - c) * wA) + ((b - c) * wB)) >> 6) + dith2[m & 7];
                row[m] = palette[v];
                x0 = (x0 + 1) & 255;
                step = (step + 1) & 127;
            }
            continue;
        }
        {
            int x0 = g_axI & 255;
            int step = 0;
            for (int m = 0; m < 256; m++) {
                const int a = blendedA[x0];
                const uint16_t bc = bcTable[step];
                const int b = bc & 0xFF;
                const int c = bc >> 8;
                const int v = c + ((((a - c) * wA) + ((b - c) * wB)) >> 6) + dith2[m & 7];
                // palette == paletteExt + PAD (set once in init()); indexing
                // through it directly folds the "+ PAD" into the pointer
                // instead of re-adding it every pixel (one fewer add/pixel).
                row[m] = palette[v];
                x0 = (x0 + 1) & 255;
                step = (step + 1) & 127;
            }
        }

        // Tail copy: for m >= 256, row[m] == row[m-256] (the period-256
        // identity above), so the remaining w-256 pixels (224 of them at
        // w=480) are a straight, unmasked copy from earlier in the SAME row
        // -- no wraparound test needed, because m-256 never revisits [256,w)
        // for any m in [256,w): m-256 < w-256 <= 256 always holds here (w is
        // the 480px panel width; the general proof for any w is: process m
        // in increasing order, so by the time m is read back at m'=m+256,
        // row[m] already holds its final value, either written directly by
        // the combine loop above or by an earlier iteration of this very
        // loop). This drops both the periodic `&255` index mask (replaced
        // by a fixed -256 pointer offset) and the old fullColor indirection,
        // and it only runs w-256 times instead of w -- at the 480px panel
        // width that is 224 copies instead of 480, cutting this loop's
        // share of band() by more than half.
        // Packed pixel-pair store (same trick as Aurora/Lava): the source
        // pair (rp-256) and dest pair (rp) are both 4-byte aligned since row
        // is aligned and both indices are even, so this is one 32-bit load
        // + one 32-bit store per 2 pixels, not two 16-bit loads.
        {
            const int tailN = w - 256;
            uint16_t *rp = row + 256;
            int i = tailN;
            while (i >= 2) {
                *reinterpret_cast<uint32_t *>(rp) = *reinterpret_cast<const uint32_t *>(rp - 256);
                rp += 2;
                i -= 2;
            }
            if (i) {
                *rp = *(rp - 256);
            }
        }
    }
}

void release() {
    releaseTable(paletteExt, PAL_EXT_N * sizeof(uint16_t));
    // An alias into paletteExt (paletteExt + PAD), not its own allocation.
    palette = nullptr;
    // Borrowed: noiseTex256() is a 64 KB fleet-wide asset owned by
    // BgAnimCommon and shared with ember.
    noise = nullptr;
    // The palette's content sentinel. init() resets it too, but a live
    // sentinel beside a null table is exactly the state this entry point
    // exists to prevent (see BgAnimCommon.h).
    lastThemeGen = 0xFFFFFFFF;
}

} // namespace

#if defined(__XTENSA__) && defined(GM_ANIM_BENCH)
// Exposed so the bench endpoint can run the vector kernel's exhaustive check
// without the animation registry having to know about it.
uint32_t nebula_lerp_self_test(uint32_t *firstBad) {
    uint32_t bad = nebulaLerpSelfTest(firstBad);
    if (bad != 0) {
        return bad;
    }
    bad = nebulaShiftSelfTest(firstBad);
    if (bad != 0) {
        return bad;
    }
    return nebulaRowSelfTest(firstBad);
}
#endif

extern const BgAnimation bg_anim_nebula;
const BgAnimation bg_anim_nebula = {
    "nebula",
    "Nebula",
    {{"speed", "Drift speed", 50}, {"density", "Density", 50}, {"turbulence", "Turbulence", 40}, {nullptr, nullptr, 0}},
    init,
    frame,
    band,
    release,
};

#endif // GAGGIMATE_SIM
