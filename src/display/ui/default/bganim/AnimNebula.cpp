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
#include <string.h> // memcpy, for the combine kernel's wrap-around double

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

#if defined(__XTENSA__)
// band()'s PIE combine-path tables. Round 1 made these function-local
// `static` arrays to force them internal (2,304 B of new BSS, the largest
// single addition in the fleet this round); round 2 moves them to the hot
// slab via allocHot(), allocated once in init() and released in release(),
// the same pattern paletteExt already used with alloc(). blendedA and
// ixBufs are read/written per row by the row-building lerp kernels;
// bTable/cTable/cDithTable and idxBuf are read per pixel by nebulaFieldPie
// and nebulaGatherScalar. bandRef keeps its own separate blendedA/ixBufs/
// bcTable statics (pre-dating this round, not something this pass added)
// rather than sharing these -- see bandRef's header comment on why the two
// paths are kept independent for equivalence-test integrity.
uint8_t *hotBlendedA = nullptr;    // [512], doubled wrap-around buffer
uint8_t *hotIxBufs = nullptr;      // [2][256] flattened; index with +curBuf*256
uint16_t *hotBTable = nullptr;     // [128]
uint16_t *hotCTable = nullptr;     // [128]
uint16_t *hotCDithTable = nullptr; // [128]
int16_t *hotIdxBuf = nullptr;      // [256]
#endif

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
        // Read once per pixel by nebulaGatherScalar's palette lookup --
        // the hottest table in the whole animation, and shared by band()
        // and bandRef() through the one `palette` pointer, so hot-slab
        // placement here helps both without any new duplication. alloc()
        // is now always PSRAM (BgAnimCommon's round-2 API change), which
        // would otherwise silently demote this from round 1's placement.
        paletteExt = static_cast<uint16_t *>(allocHot(PAL_EXT_N * sizeof(uint16_t)));
        noise = noiseTex256();
        if (paletteExt == nullptr || noise == nullptr) {
            return false;
        }
        palette = paletteExt + PAD;
    }
#if defined(__XTENSA__)
    if (hotBlendedA == nullptr) {
        // 2,304 B total (512 + 512 + 128*2 + 128*2 + 128*2 + 256*2), plus
        // paletteExt's 800 B above = 3,104 B of the 9,216 B a resident
        // animation gets from the 12 KB slab -- comfortably inside budget,
        // nothing here needed shrinking.
        hotBlendedA = static_cast<uint8_t *>(allocHot(512));
        hotIxBufs = static_cast<uint8_t *>(allocHot(2 * 256));
        hotBTable = static_cast<uint16_t *>(allocHot(128 * sizeof(uint16_t)));
        hotCTable = static_cast<uint16_t *>(allocHot(128 * sizeof(uint16_t)));
        hotCDithTable = static_cast<uint16_t *>(allocHot(128 * sizeof(uint16_t)));
        hotIdxBuf = static_cast<int16_t *>(allocHot(256 * sizeof(int16_t)));
        if (hotBlendedA == nullptr || hotIxBufs == nullptr || hotBTable == nullptr || hotCTable == nullptr ||
            hotCDithTable == nullptr || hotIdxBuf == nullptr) {
            return false;
        }
    }
#endif
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

// The same arithmetic, for the host bench and as the reference the device
// self-test compares against.
static inline uint8_t lerpScalar(int a, int b, int f) {
    return static_cast<uint8_t>(a + (((b - a) * f) >> 8));
}

// A whole row of the x-interpolation, scalar. This is what the host bench
// builds, and what the device falls back to when the texture row is not
// 16-byte aligned -- see lerpShiftRowPie for why alignment is a precondition
// there rather than a preference.
static void lerpShiftRowScalar(uint8_t *__restrict out, const uint8_t *__restrict a, int f) {
    int cur = a[0];
    for (int i = 0; i < 255; i++) {
        const int nxt = a[i + 1];
        out[i] = static_cast<uint8_t>(cur + (((nxt - cur) * f) >> 8));
        cur = nxt;
    }
    out[255] = static_cast<uint8_t>(cur + (((a[0] - cur) * f) >> 8));
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
// Iteration k reads aligned block k+1 and writes block k, so n16 groups touch
// bytes 0..16*n16+15. At n16=16 that is one block past the row, which is why
// noiseTex256 carries sixteen bytes of slack: for rows 0..254 the extra block
// is simply the next row, and for row 255 it is that padding. Only the wrap at
// index 255 is then wrong, since the interpolation wants a[0] there rather
// than a[256], and the caller fixes that one entry.
//
// The row must be 16-byte aligned. EE.LD.128.USAR.IP forces the low four
// address bits of its access to zero while capturing them into SAR_BYTE, so an
// unaligned base would still compute the right values but would read behind
// the row on the setup load. band() checks and falls back to scalar.
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

// The whole x-interpolation as band() actually calls it: the vector kernel over
// all sixteen groups plus the single scalar fixup that closes the wrap, against
// the original single scalar loop, over real noise texture rows.
//
// The two tests above check the kernels on synthetic input. This checks the
// composition, which is where the parts that are not the kernel live: the group
// count and the wrap entry at 255. Those are
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
            lerpShiftRowPie(got, a, fv, 16);
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
    // Sixteen bytes of slack, matching noiseTex256, so the last group has a
    // real block to read and every one of the 256 outputs is well defined.
    alignas(16) uint8_t in[256 + 16];
    alignas(16) uint8_t out[256];
    alignas(16) uint16_t fv[8];
    uint32_t bad = 0;
    for (int pat = 0; pat < 4; pat++) {
        for (int i = 0; i < 256 + 16; i++) {
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
            lerpShiftRowPie(out, in, fv, 16);
            for (int i = 0; i < 256; i++) {
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

// ---------------------------------------------------------------------------
// Combine-stage kernel: computes the palette-relative blend index
//   v(m) = c(m) + (((a(m)-c(m))*wA + (b(m)-c(m))*wB) >> 6) + dith2[m&7]
// for n16*16 consecutive pixels m, on the PIE vector unit, leaving the actual
// palette gather (a real, unvectorisable gather -- there is no EE.* gather
// instruction) to a separate scalar pass. This is the per-pixel arithmetic
// the file header's 2026-08-25 device measurement pointed at: with the
// dominant-octave tables already on PIE (lerpRowPie/lerpShiftRowPie above),
// the remaining ~19ms/frame at BAND_H=8 is this combine loop, run once per
// output pixel (up to 256 times/row via the period-256 reuse already in
// bandRef) across every row of every band() call.
//
// Two data-layout changes from bandRef's scalar version, both load-bearing
// for vectorising this without spilling the 8-register PIE file:
//
//   1. blendedA is read at a runtime rotation (x0 = (axI+m)&255) that is not
//      16-byte aligned in general, so EE.VLD.128.IP (which silently masks
//      the low 4 address bits instead of trapping) cannot read it directly
//      at an arbitrary m. bandRef masks per pixel (`x0 = (x0+1)&255`); this
//      kernel instead takes a *doubled* 512-byte buffer (bytes 256..511 are
//      a copy of bytes 0..255, built once per row by the caller) and reads
//      it through EE.LD.128.USAR.IP + EE.SRC.Q, the same unaligned-window
//      technique lerpShiftRowPie uses. The doubling is what makes this safe
//      for a *sequence* of reads sweeping up to 256 bytes forward from an
//      arbitrary start, not just one: the single extra aligned block
//      lerpShiftRowPie relies on (noiseTex256's 16-byte slack) only covers a
//      one-byte lookahead: this kernel's aOff can be anywhere in 0..255 and
//      still needs up to 256 further bytes, so the tail has to be a full
//      second copy, not 16 bytes of it. Proved algorithmically (not just
//      argued) in the worklog before this was written into asm: see the
//      2026-09-04 nebula-asm sanity checks, 5.12M-case and 2.4M-case
//      comparisons against bandRef's exact 32-bit formula, zero mismatches.
//   2. bandRef's b/c reads come from one packed uint16 bcTable (a real win
//      for the *scalar* loop -- see the file header's mandala-trick note --
//      but packed lanes need an AND-plus-shift to unpack per group, and the
//      only shift PIE has is "multiply by 1 at a chosen SAR", which needs
//      its own resident constant). This kernel instead takes b/c as three
//      *separate* uint16 tables the caller builds per row: bTable (plain),
//      cTable (plain, used in both difference terms AND the final add), and
//      cDithTable (= cTable[k] + dith2[k&7], the Bayer dither/density offset
//      pre-summed in at table-build time -- k is bandRef's `step`, and
//      step&7 == m&7 always since 8 | 128, so this is exact, not an
//      approximation). This is what makes an 8-register PIE budget work:
//      4 resident constants (wA, wB, a "ones" vector for the final >>6
//      arithmetic shift, and... no fourth is needed, because dith2 is baked
//      into cDithTable instead of carried as a fifth resident register) plus
//      4 rotating values (this group's a/a-next, b, c) fill exactly q0-q7
//      with zero spill. Folding dith2 directly into cTable instead (saving
//      the extra cDithTable read) was tried first and is WRONG: cTable's
//      plain value is also used in the two difference terms, and dither must
//      not perturb those, only the final additive base -- caught by the
//      first sanity-check run (5.05M of 5.12M cases mismatched) before any
//      asm was written, which is why the check exists as a separate step in
//      the workflow.
//
// bandRef's period-256 reuse still applies: the caller only ever asks this
// kernel for the first min(w,256) pixels, split in two at m=128 (see below).
//
// Register map, exactly q0-q7, no spills, verified by counting distinct live
// ranges by hand (see the block comment in the .cpp history / report for the
// full trace):
//   q0 = P, the previous aligned 16-byte block of the rotated `a` source,
//        persistent across outer-loop iterations (this is exactly
//        lerpShiftRowPie's q6, renamed and playing the same role).
//   q1 = wA broadcast (resident all call)
//   q2 = wB broadcast (resident all call)
//   q3 = "ones" broadcast, i.e. every lane = 1 (resident all call); multiplying
//        by this at a chosen SAR is PIE's only right-shift, per ASM_BRIEF.md.
//   q4 = scratch: next aligned `a` block, then b(pass0), then b(pass1).
//   q5 = pass 0's pixel-0..7 pipeline register (a -> diffA -> product -> sum
//        -> shifted -> v), reused in place at every step.
//   q6 = the zero register for EE.VZIP.8, which becomes pass 1's pixels
//        8..15 pipeline register (same in-place reuse as q5).
//   q7 = scratch: c(pass0), then cDith(pass0), then c(pass1), then
//        cDith(pass1).
//
// SAR is toggled 0 (for the two `diff*weight` multiplies, an exact multiply
// with no truncation since every product fits in 16 bits -- see the block
// comment above) then 6 (for the final arithmetic right shift) TWICE per
// 16-pixel group, once per 8-lane pass, rather than batched once per group:
// batching would need both passes' partial products alive across the SAR
// change, which does not fit in the remaining 3 non-resident registers.
// SSAI is a single, non-stalling instruction, so this costs a few extra
// issue slots per 16 pixels, not a stall; not revisited unless a device
// measurement says otherwise.
//
// out must be 16-byte aligned; aBase must be 16-byte aligned (blendedA is a
// static aligned array) and at least aOff+16*n16+16 bytes long, i.e. sized
// for the doubled-buffer read pattern above; bTab/cTab/cdTab must each hold
// at least 16*n16 entries starting at index 0 (the caller resets these
// pointers to each table's base for every call -- see band()'s two-call
// split at m=128, which is exactly bTable/cTable/cDithTable's own period).
__attribute__((noinline)) static void nebulaFieldPie(int16_t *__restrict out, const uint8_t *__restrict aBase,
                                                     int aOff, const uint16_t *__restrict bTab,
                                                     const uint16_t *__restrict cTab,
                                                     const uint16_t *__restrict cdTab,
                                                     const int16_t *__restrict consts, int n16) {
    const uint8_t *pa = aBase + aOff; // SAR_BYTE = aOff & 15, captured by the usar load below
    const uint16_t *pb = bTab;
    const uint16_t *pc = cTab;
    const uint16_t *pcd = cdTab;
    int16_t *po = out;
    // Broadcast tables for the three resident constants (wA, wB, ones),
    // 24 x int16, 16-byte aligned, built by the caller. wA/wB are
    // per-band()-call constants (frame() sets them; band() never does), so
    // band() builds this array once before its row loop and passes the
    // same pointer to every nebulaFieldPie call in the whole band() --
    // round 1 rebuilt it here on every call (2x/row); the round-2 device
    // numbers said table placement/access pattern is where the cycles are,
    // so this redundant per-call rebuild is worth hoisting out even though
    // it is a small, fixed instruction count.
    const int16_t *pct = consts;
    int n = n16;
    asm volatile("ee.ld.128.usar.ip q0, %[pa], 16\n" // q0 = P, SAR_BYTE = aOff & 15
                 "ee.vld.128.ip q1, %[ct], 16\n"      // q1 = wA broadcast
                 "ee.vld.128.ip q2, %[ct], 16\n"      // q2 = wB broadcast
                 "ee.vld.128.ip q3, %[ct], 16\n"      // q3 = ones broadcast
                 "1:\n"
                 "ee.vld.128.ip q4, %[pa], 16\n" // q4 = N, next aligned `a` block
                 "ee.src.q q5, q0, q4\n"         // q5 = S = rotated 16 bytes (uses OLD P)
                 "ee.orq q0, q4, q4\n"           // P := N, for the next iteration
                 "ee.zero.q q6\n"
                 "ee.vzip.8 q5, q6\n" // q5 = a[0..7] widened, q6 = a[8..15] widened
                 // pass 0: pixels 0..7 (q5)
                 "ee.vld.128.ip q4, %[pb], 16\n" // q4 = b0
                 "ee.vld.128.ip q7, %[pc], 16\n" // q7 = c0
                 "ee.vsubs.s16 q5, q5, q7\n"     // q5 = a0-c0
                 "ee.vsubs.s16 q4, q4, q7\n"     // q4 = b0-c0
                 "ssai 0\n"
                 "ee.vmul.s16 q5, q5, q1\n" // q5 = (a0-c0)*wA
                 "ee.vmul.s16 q4, q4, q2\n" // q4 = (b0-c0)*wB
                 "ee.vadds.s16 q5, q5, q4\n" // q5 = sum0
                 "ssai 6\n"
                 "ee.vmul.s16 q5, q5, q3\n"       // q5 = sum0 >> 6 (arithmetic)
                 "ee.vld.128.ip q7, %[pcd], 16\n" // q7 = cDith0 (c0 + dith2[.&7])
                 "ee.vadds.s16 q5, q5, q7\n"      // q5 = v0
                 "ee.vst.128.ip q5, %[po], 16\n"
                 // pass 1: pixels 8..15 (q6)
                 "ee.vld.128.ip q4, %[pb], 16\n" // q4 = b1
                 "ee.vld.128.ip q7, %[pc], 16\n" // q7 = c1
                 "ee.vsubs.s16 q6, q6, q7\n"
                 "ee.vsubs.s16 q4, q4, q7\n"
                 "ssai 0\n"
                 "ee.vmul.s16 q6, q6, q1\n"
                 "ee.vmul.s16 q4, q4, q2\n"
                 "ee.vadds.s16 q6, q6, q4\n"
                 "ssai 6\n"
                 "ee.vmul.s16 q6, q6, q3\n"
                 "ee.vld.128.ip q7, %[pcd], 16\n" // q7 = cDith1
                 "ee.vadds.s16 q6, q6, q7\n"
                 "ee.vst.128.ip q6, %[po], 16\n"
                 "addi %[n], %[n], -1\n"
                 "bnez %[n], 1b\n"
                 : [pa] "+r"(pa), [pb] "+r"(pb), [pc] "+r"(pc), [pcd] "+r"(pcd), [po] "+r"(po), [ct] "+r"(pct),
                   [n] "+r"(n)
                 :
                 : "memory");
}

// Reference for nebulaFieldPie, matching its exact 16-bit-lane arithmetic
// (each intermediate value is what an int16 PIE lane would hold, including
// the multiply-then-truncate order) rather than bandRef's plain-int formula
// -- the two are equal for every input in range (see the header's proof
// comment) but this makes the equality an executable check, not an argument.
static void nebulaFieldRef(int16_t *out, const uint8_t *aBase, int aOff, const uint16_t *bTab, const uint16_t *cTab,
                           const uint16_t *cdTab, int wA, int wB, int n16) {
    for (int m = 0; m < n16 * 16; m++) {
        const int16_t a = static_cast<int16_t>(aBase[aOff + m]);
        const int16_t b = static_cast<int16_t>(bTab[m]);
        const int16_t c = static_cast<int16_t>(cTab[m]);
        const int16_t cd = static_cast<int16_t>(cdTab[m]);
        const int16_t diffA = static_cast<int16_t>(a - c);
        const int16_t diffB = static_cast<int16_t>(b - c);
        const int16_t p1 = static_cast<int16_t>((diffA * wA) & 0xFFFF); // SAR=0: exact, always in range
        const int16_t p2 = static_cast<int16_t>((diffB * wB) & 0xFFFF);
        const int16_t sum = static_cast<int16_t>(p1 + p2);
        const int16_t shifted = static_cast<int16_t>(sum >> 6); // arithmetic, matches ee.vmul.s16 SAR=6
        out[m] = static_cast<int16_t>(cd + shifted);
    }
}

// Exhaustive-ish arithmetic check, isolated from the addressing/rotation
// logic (nebulaFieldAddrSelfTest below covers that): full a x c sweep, b
// stepped, at the real per-frame wA/wB extremes (turbulence 0 and 100, plus
// margin) and densOff extremes, aOff=0 (no rotation) so aBase can be a plain
// ramp. Mirrors nebulaLerpSelfTest's structure (kernel vs scalar reference,
// full range on the two operands that vary fastest).
uint32_t nebulaFieldArithSelfTest(uint32_t *firstBad) {
    alignas(16) uint8_t aBase[16]; // one 16-pixel group, aOff=0 so no rotation needed
    alignas(16) uint16_t bTab[16], cTab[16], cdTab[16];
    alignas(16) int16_t got[16];
    uint32_t bad = 0;
    const int wAs[] = {38, 28, 16, 40};
    const int wBs[] = {16, 19, 10, 24};
    const int densOffs[] = {-55, 0, 55};
    for (int wi = 0; wi < 4; wi++) {
        const int wA = wAs[wi], wB = wBs[wi];
        alignas(16) const int16_t consts[24] = {
            static_cast<int16_t>(wA), static_cast<int16_t>(wA), static_cast<int16_t>(wA), static_cast<int16_t>(wA),
            static_cast<int16_t>(wA), static_cast<int16_t>(wA), static_cast<int16_t>(wA), static_cast<int16_t>(wA),
            static_cast<int16_t>(wB), static_cast<int16_t>(wB), static_cast<int16_t>(wB), static_cast<int16_t>(wB),
            static_cast<int16_t>(wB), static_cast<int16_t>(wB), static_cast<int16_t>(wB), static_cast<int16_t>(wB),
            1, 1, 1, 1, 1, 1, 1, 1,
        };
        for (int di = 0; di < 3; di++) {
            const int densOff = densOffs[di];
            for (int a = 0; a < 256; a++) {
                for (int c = 0; c < 256; c += 3) { // full a, stepped c -- b and dith vary per lane below
                    for (int k = 0; k < 16; k++) {
                        aBase[k] = static_cast<uint8_t>(a);
                        bTab[k] = static_cast<uint16_t>((a * 7 + k * 37) & 0xFF); // varies per lane
                        cTab[k] = static_cast<uint16_t>((c + k * 5) & 0xFF);
                        const int bayer = (k * 11 + c) & 63;
                        const int dith2 = (bayer - 31) / 4 + densOff;
                        cdTab[k] = static_cast<uint16_t>(cTab[k] + dith2);
                    }
                    int16_t want[16];
                    nebulaFieldRef(want, aBase, 0, bTab, cTab, cdTab, wA, wB, 1);
                    nebulaFieldPie(got, aBase, 0, bTab, cTab, cdTab, consts, 1);
                    for (int k = 0; k < 16; k++) {
                        if (got[k] != want[k]) {
                            if (bad == 0 && firstBad != nullptr) {
                                *firstBad = static_cast<uint32_t>(a) | (static_cast<uint32_t>(c) << 8) |
                                            (static_cast<uint32_t>(k) << 16) | (static_cast<uint32_t>(wi) << 24);
                            }
                            bad++;
                        }
                    }
                }
            }
        }
    }
    return bad;
}

// Addressing check: fixed, simple arithmetic (a=x, b=c=0, wA=64/wB=0 so
// v==a-0==a exactly, no truncation to reason about) but aOff swept over
// every value in 0..255 and n16 over every size the caller actually uses
// (1..8, i.e. up to one full 128-pixel bcTable sweep), which is what
// exercises the doubled-buffer rotation and the persistent-P carry across
// outer-loop iterations. This is the part most likely to have an off-by-one:
// nebulaShiftSelfTest above proves EE.SRC.Q's addressing once; this proves
// this kernel's own use of it (fresh pa/P setup, different SAR source) is
// wired up the same way, across the same doubled-buffer scheme
// band()'s blendedA now uses.
uint32_t nebulaFieldAddrSelfTest(uint32_t *firstBad) {
    // 512 = the real doubled-buffer size (256 real bytes + a full second
    // copy), not just 16 bytes of slack -- see the kernel's header comment
    // for why a single lerpShiftRowPie-style block is not enough here.
    alignas(16) uint8_t aBase[512];
    for (int i = 0; i < 256; i++) {
        aBase[i] = static_cast<uint8_t>(i); // ramp -- every value distinct, settles addressing
    }
    for (int i = 0; i < 256; i++) {
        aBase[256 + i] = aBase[i]; // the doubling band()'s caller must perform each row
    }
    alignas(16) uint16_t bTab[128], cTab[128], cdTab[128];
    for (int k = 0; k < 128; k++) {
        bTab[k] = 0;
        cTab[k] = 0;
        cdTab[k] = 0; // wB=0 and cTab=0 make b/c inert; only a's addressing is under test
    }
    alignas(16) int16_t got[128];
    // wA=64: v == (a-0)*64>>6 == a
    alignas(16) const int16_t consts[24] = {64, 64, 64, 64, 64, 64, 64, 64, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1};
    uint32_t bad = 0;
    for (int aOff = 0; aOff < 256; aOff++) {
        for (int n16 = 1; n16 <= 8; n16++) {
            nebulaFieldPie(got, aBase, aOff, bTab, cTab, cdTab, consts, n16);
            for (int m = 0; m < n16 * 16; m++) {
                const int want = aBase[(aOff + m) & 255]; // the real semantics: mod-256 rotation
                if (got[m] != want) {
                    if (bad == 0 && firstBad != nullptr) {
                        *firstBad = static_cast<uint32_t>(aOff) | (static_cast<uint32_t>(m) << 16);
                    }
                    bad++;
                }
            }
        }
    }
    return bad;
}

// ---------------------------------------------------------------------------
// Scalar gather: row[m] = palette[idx[m]] for n4*4 pixels. There is no
// EE.* gather instruction (ASM_BRIEF.md's PIE facts are explicit about
// this), so nebulaFieldPie leaves the actual palette lookup to this
// hand-scheduled loop rather than a plain per-pixel C store. Four pixels are
// interleaved so a load's result is never consumed by the very next
// instruction (the scalar one-cycle load-use interlock): all four indices
// load first, then all four addresses compute (ALU, no load-use delay to
// hide), then all four colors load, then the two pack/store pairs -- the
// only two load-use pairs left are the two `slli` reading a color l16ui
// just produced, which is the minimum given only two spare registers'
// worth of scheduling slack once idx/row/n/palette occupy four more (see
// ASM_BRIEF.md's ~13-usable-AR note; 4 scratch + 4 fixed stays well inside
// that without pushing to an 8-wide unroll that would not).
// `addx2` folds the *2 (uint16_t stride) into the add: addr = idx*2 + palette.
// `l16si` sign-extends (idx can be negative -- see the file header's PAD
// proof for the range), `l16ui` does not (a color's bits are not a signed
// quantity). Two colors pack into one s32i, same trick as the tail copy and
// scale565Oct: row is 4-byte aligned and n4 pixel-groups are even-based, so
// this halves store traffic versus two s16i.
__attribute__((noinline)) static void nebulaGatherScalar(uint16_t *__restrict row, const int16_t *__restrict idx,
                                                         const uint16_t *__restrict palette, int n4) {
    const int16_t *pi = idx;
    uint16_t *pr = row;
    int n = n4;
    int s0, s1, s2, s3; // scratch, register-allocated by GCC (not hardcoded ARs)
    asm volatile("1:\n"
                 "l16si %[s0], %[pi], 0\n"
                 "l16si %[s1], %[pi], 2\n"
                 "l16si %[s2], %[pi], 4\n"
                 "l16si %[s3], %[pi], 6\n"
                 "addx2 %[s0], %[s0], %[pal]\n"
                 "addx2 %[s1], %[s1], %[pal]\n"
                 "addx2 %[s2], %[s2], %[pal]\n"
                 "addx2 %[s3], %[s3], %[pal]\n"
                 "l16ui %[s0], %[s0], 0\n"
                 "l16ui %[s1], %[s1], 0\n"
                 "l16ui %[s2], %[s2], 0\n"
                 "l16ui %[s3], %[s3], 0\n"
                 "slli %[s1], %[s1], 16\n"
                 "or %[s0], %[s0], %[s1]\n"
                 "slli %[s3], %[s3], 16\n"
                 "or %[s2], %[s2], %[s3]\n"
                 "s32i %[s0], %[pr], 0\n"
                 "s32i %[s2], %[pr], 4\n"
                 "addi %[pi], %[pi], 8\n"
                 "addi %[pr], %[pr], 8\n"
                 "addi %[n], %[n], -1\n"
                 "bnez %[n], 1b\n"
                 : [pi] "+r"(pi), [pr] "+r"(pr), [n] "+r"(n), [s0] "=&r"(s0), [s1] "=&r"(s1), [s2] "=&r"(s2),
                   [s3] "=&r"(s3)
                 : [pal] "r"(palette)
                 : "memory");
}

// idx=[-300..300] step covers PAD's proven range (see file header) plus
// margin; palette is a ramp so a wrong index reads a distinct, checkable
// value. n4 covers every group count band() actually issues (240/4=60,
// 256/4=64) plus 1 and a couple of odd small counts for the loop bound
// itself.
uint32_t nebulaGatherSelfTest(uint32_t *firstBad) {
    constexpr int PAD_TEST = 300;
    alignas(16) uint16_t pal[2 * PAD_TEST + 1];
    for (int i = 0; i < 2 * PAD_TEST + 1; i++) {
        pal[i] = static_cast<uint16_t>(i * 97 + 13); // distinct, checkable values
    }
    const uint16_t *palette = pal + PAD_TEST; // palette[v] valid for v in [-PAD_TEST, PAD_TEST]
    alignas(16) int16_t idx[256];
    alignas(16) uint16_t row[256];
    uint32_t bad = 0;
    const int n4Cases[] = {1, 2, 15, 16, 60, 64};
    for (int ci = 0; ci < 6; ci++) {
        const int n4 = n4Cases[ci];
        const int n = n4 * 4;
        for (int i = 0; i < n; i++) {
            // Spread across the full proven range, including both signs and
            // the exact endpoints, deterministically per (n4, i).
            idx[i] = static_cast<int16_t>(((i * 131 + n4 * 17) % (2 * PAD_TEST + 1)) - PAD_TEST);
            row[i] = 0xDEAD; // so a group the kernel skips cannot pass by luck
        }
        nebulaGatherScalar(row, idx, palette, n4);
        for (int i = 0; i < n; i++) {
            const uint16_t want = palette[idx[i]];
            if (row[i] != want) {
                if (bad == 0 && firstBad != nullptr) {
                    *firstBad = static_cast<uint32_t>(static_cast<uint16_t>(idx[i])) |
                                (static_cast<uint32_t>(i) << 16);
                }
                bad++;
            }
        }
    }
    return bad;
}

#endif

// The portable reference implementation: the original per-pixel scalar
// combine loop, unchanged. Kept verbatim (not merely "similar to") the
// pre-asm-pass code so it stays trustworthy as the spec: the host bench and
// golden compare run this exclusively (band() below only takes the vector
// path on __XTENSA__), and the on-device equivalence test
// (SleepAnimation::runAnimTest, /api/debug/animtest) renders every band
// through both this and band() and reports the first differing pixel. Row
// building (rowA0/rowA1 x-interpolation into blendedA) still uses the
// existing lerpShiftRowPie/lerpRowPie PIE kernels on device -- those are
// unrelated to this pass (already proven bit-exact by nebulaLerpSelfTest /
// nebulaShiftSelfTest / nebulaRowSelfTest above) and reused unchanged; only
// the combine step (the part actually being replaced) is genuinely
// independent scalar code in both this function and band().
void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
#if defined(__XTENSA__)
    // Every row of the texture sits at a 256-byte offset from its base, so one
    // test on the base settles it for all of them. The vector path needs it
    // because EE.LD.128.USAR.IP forces the low four address bits of its access
    // to zero, so an unaligned base would make the first load of row 0 reach
    // behind the allocation. noiseTex256 asks for 16 and this checks rather
    // than assumes, since its fallback allocator does not promise it.
    const bool rowAligned = (reinterpret_cast<uintptr_t>(noise) & 15) == 0;
#endif
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
#ifdef GM_NEBULA_BCTABLE_PROBE
        // Diagnostic only, visually wrong: leave bcTable holding whatever the
        // previous row left in it. 128 iterations per row, two noise-texture
        // reads each, to feed a 240-pixel loop. Never build this into
        // anything shipping.
        (void)rowB;
        (void)rowC;
#else
        {
            int bI = g_bx & 255;
            int cI = g_cx & 255;
            for (int k = 0; k < 128; k++) {
                bcTable[k] = static_cast<uint16_t>(rowB[bI]) | (static_cast<uint16_t>(rowC[cI]) << 8);
                bI = (bI + 2) & 255;
                cI = (cI + 4) & 255;
            }
        }
#endif

        uint8_t *ixCur = ixBufs[curBuf];
        uint8_t *ixNext = ixBufs[curBuf ^ 1];
        if (r == 0) {
            // First row of the call: no predecessor to reuse from (a rows==1
            // call always takes only this path), so interpolate rowA0 fresh.
            // Identical expression to the reuse-eligible rowA1 pass below,
            // just addressed at rowA0 -- kept as a separate copy rather than
            // a shared helper so neither loop gains a call in its body.
#if defined(__XTENSA__)
            if (rowAligned) {
                alignas(16) uint16_t fv[8];
                for (int k = 0; k < 8; k++) {
                    fv[k] = static_cast<uint16_t>(axF);
                }
                lerpShiftRowPie(ixCur, rowA0, fv, 16);
                // Only the wrap needs fixing: the kernel's last group read
                // a[256], which is the next row, where the interpolation wants
                // a[0]. Everything below 255 the vector path already has right.
                ixCur[255] = lerpScalar(rowA0[255], rowA0[0], axF);
            } else {
                lerpShiftRowScalar(ixCur, rowA0, axF);
            }
#else
            lerpShiftRowScalar(ixCur, rowA0, axF);
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
            if (rowAligned) {
                alignas(16) uint16_t fv[8];
                for (int k = 0; k < 8; k++) {
                    fv[k] = static_cast<uint16_t>(axF);
                }
                lerpShiftRowPie(ixNext, rowA1, fv, 16);
                // Only the wrap needs fixing: the kernel's last group read
                // a[256], which is the next row, where the interpolation wants
                // a[0]. Everything below 255 the vector path already has right.
                ixNext[255] = lerpScalar(rowA1[255], rowA1[0], axF);
            } else {
                lerpShiftRowScalar(ixNext, rowA1, axF);
            }
#else
            lerpShiftRowScalar(ixNext, rowA1, axF);
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
#ifdef GM_NEBULA_ARITH_PROBE
                // Diagnostic only, visually wrong: keep all three loads and
                // the palette gather, drop the two multiplies, the shift and
                // the dither. This splits the combine loop's cost into
                // "arithmetic" and "loads plus gather", which decides whether
                // a PIE rewrite of the blend is worth anything -- the palette
                // lookup is a real gather and cannot vectorise, so if the
                // gather dominates there is nothing here to win.
                const int v = a ^ b ^ c;
#else
                const int v = c + ((((a - c) * wA) + ((b - c) * wB)) >> 6) + dith2[m & 7];
#endif
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

// The registry's band() entry. On device this dispatches to the PIE
// combine kernel above (nebulaFieldPie + nebulaGatherScalar); everywhere
// else -- host bench, and any real-width call that would violate the fast
// path's precondition -- it is bandRef.
//
// Row building (blendedA via lerpRowPie/lerpShiftRowPie) is unchanged from
// bandRef, copied rather than factored into a shared helper: this file's own
// header already documents that a whole-function register/loop-budget
// effect moves when live loop-carried state changes shape (the bcTable-
// before-ixCur ordering note, the tail-copy split note), so a shared helper
// used by both bandRef and this function would couple their codegen in a
// way neither has been measured against. Kept as two independent, verified
// copies instead, the same choice the file already makes for the r==0
// ixCur/ixNext pass (see that comment).
void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t p[4]) {
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    // Fast-path precondition: w a multiple of 16. True for both of the
    // panel's real widths (480 full-res, 240 half-res/interlaced -- see the
    // file header) so every 16-pixel PIE group and the bcTable-period split
    // at m=128 land exactly on group boundaries, with no scalar remainder
    // to special-case. Only host tooling (interlace_check with a custom
    // width) can violate this; bandRef is the general-w fallback.
    if ((w & 15) != 0) {
        bandRef(dst, y0, rows, w, tMs, p);
        return;
    }
    // allocHot()'s bump allocator only guarantees 16-byte alignment for
    // pointers actually carved from the slab; a request that overflows the
    // slab falls back to alloc() (PSRAM), which does not promise 16-byte
    // alignment. In practice this animation's whole hot footprint is a
    // fraction of its 9,216 B budget so the fallback never triggers, but
    // ee.vld.128.ip/ee.ld.128.usar.ip mask (not trap on) a misaligned
    // address, so a silent fallback would corrupt output rather than crash
    // -- checked once per band() call, the same defensive pattern rowAligned
    // below already uses for noise.
    const bool hotAligned = (reinterpret_cast<uintptr_t>(hotBlendedA) & 15) == 0 &&
                             (reinterpret_cast<uintptr_t>(hotIxBufs) & 15) == 0 &&
                             (reinterpret_cast<uintptr_t>(hotBTable) & 15) == 0 &&
                             (reinterpret_cast<uintptr_t>(hotCTable) & 15) == 0 &&
                             (reinterpret_cast<uintptr_t>(hotCDithTable) & 15) == 0 &&
                             (reinterpret_cast<uintptr_t>(hotIdxBuf) & 15) == 0;
    if (!hotAligned) {
        bandRef(dst, y0, rows, w, tMs, p);
        return;
    }
    const bool rowAligned = (reinterpret_cast<uintptr_t>(noise) & 15) == 0;
    const int axF = g_axF, ayF = g_ayF;
    const int wA = g_wA, wB = g_wB, densOff = g_densOff;
    // Doubled: bytes 256..511 are a copy of 0..255, refreshed every row by
    // the memcpy below. See nebulaFieldPie's header comment for why a full
    // second copy is needed here and not just noiseTex256/lerpShiftRowPie's
    // 16-byte slack: this buffer is read at an arbitrary rotation for up to
    // 256 bytes forward, not looked ahead by one texel.
    uint8_t *const blendedA = hotBlendedA;         // [512]
    uint8_t *const ixBufs0 = hotIxBufs;             // [256], curBuf==0
    uint8_t *const ixBufs1 = hotIxBufs + 256;       // [256], curBuf==1
    // b/c/c+dith tables for nebulaFieldPie, sized to the bcTable period
    // (128) rather than to w -- see the kernel's header comment for why
    // this replaces bandRef's single packed bcTable with three plain ones.
    uint16_t *const bTable = hotBTable;
    uint16_t *const cTable = hotCTable;
    uint16_t *const cDithTable = hotCDithTable;
    int16_t *const idxBuf = hotIdxBuf;
    // wA/wB broadcast table for nebulaFieldPie: built once here instead of
    // once per call (2x/row) as round 1 had it -- wA/wB are per-band()-call
    // constants (frame() sets them; nothing in this loop changes them), so
    // every row and both combine-stage calls read the same 24 x int16
    // table. Stack-local, not allocHot: 48 B, rebuilt once per band() call,
    // not a persistent resource.
    alignas(16) int16_t combineConsts[24];
    for (int k = 0; k < 8; k++) {
        combineConsts[k] = static_cast<int16_t>(wA);
        combineConsts[8 + k] = static_cast<int16_t>(wB);
        combineConsts[16 + k] = 1;
    }
    int curBuf = 0;
    const int totalM = w <= 256 ? w : 256;
    for (int r = 0; r < rows; r++) {
        const int y = y0 + r;
        uint16_t *row = dst + static_cast<size_t>(r) * w;
        const uint8_t *bayerRow = &BAYER8[(y & 7) * 8];
        int dith2[8];
        for (int k = 0; k < 8; k++) {
            dith2[k] = (static_cast<int>(bayerRow[k]) - 31) / 4 + densOff;
        }
        const uint8_t *rowA0 = noise + ((y + g_ayI) & 255) * 256;
        const uint8_t *rowA1 = noise + ((y + g_ayI + 1) & 255) * 256;
        const uint8_t *rowB = noise + ((y * 2 + g_by) & 255) * 256;
        const uint8_t *rowC = noise + ((y * 4 + g_cy) & 255) * 256;

        // Same 128-entry, period-128 walk as bandRef's bcTable, split into
        // three plain uint16 arrays instead of one packed one (see
        // nebulaFieldPie's header comment). dith2 is folded into
        // cDithTable, not cTable: k&7 == step&7 == m&7 (8 | 128) makes this
        // exact, but cTable's plain value is also read for the kernel's two
        // difference terms and must not carry the dither (the first
        // sanity-check run against bandRef's formula caught exactly this
        // when tried the other way -- see nebulaFieldPie's header comment).
        //
        // Round 5 (2026-09-04 kbench pass) tried copying rowB/rowC into two
        // new slab buffers with one sequential 256 B sweep each, then
        // walking the +2/+4 stride against that SRAM copy instead of PSRAM
        // directly, on the theory that a strided PSRAM read is worse than a
        // flat one. Measured on device (kb.py, min_ms filters interrupts):
        // 12.93 -> 13.68/13.69 ms, reproduced exactly across repeated runs
        // and a device reboot, so the ~5.8% is real, not noise. first_ms did
        // not move outside its own run-to-run spread (21.5-26.6 ms across
        // six runs of both the original and the changed code) in either
        // direction. Reverted: the extra sequential copy is a real,
        // unconditional cost every row, and nothing measurable came back
        // for it. Most likely cause: the working set here is 256 B, small
        // enough that the strided read was probably already landing mostly
        // in the S3's external-memory cache after the first touch, so
        // reordering the access pattern within that 256 B has little left
        // to win -- consistent with the file's own 2026-08-25 finding that
        // even eliminating all four samplers' PSRAM traffic outright
        // (GM_NEBULA_CACHED_NOISE_PROBE, bandRef only) capped out at 16% on
        // a slower pre-PIE-combine baseline. Do not retry this exact shape
        // without a new device number to back it.
        // cI steps by 4 mod 256, a period of 64, half of bI's own period of
        // 128 (steps by 2 mod 256) -- so of the loop's 128 iterations, only
        // the first 64 touch a cI value the second 64 have not already
        // produced: cI(k+64) == cI(k) for every k in [0,64) (4*64 == 256, a
        // full wrap), and since 8 | 64 too, k&7 == (k+64)&7, so dith2[k&7]
        // repeats in step. That makes cTable[k+64] == cTable[k] and
        // cDithTable[k+64] == cDithTable[k] bit for bit, not approximately:
        // the second half was 64 redundant PSRAM reads (rowC[cI], same
        // address as 64 iterations earlier) and 64 redundant dither adds,
        // producing values the first half already computed. bI has no such
        // period inside 128 (gcd(2,256)=2, period 256/2=128 exactly), so
        // bTable alone still needs all 128 iterations; only the c-side
        // splits into "compute 64, copy 64" (round 5, 2026-09-04 kbench
        // pass; measured device delta at the end of this comment).
        int bI = g_bx & 255;
        int cI = g_cx & 255;
        for (int k = 0; k < 64; k++) {
            const int cv = rowC[cI];
            bTable[k] = static_cast<uint16_t>(rowB[bI]);
            cTable[k] = static_cast<uint16_t>(cv);
            cDithTable[k] = static_cast<uint16_t>(cv + dith2[k & 7]);
            bI = (bI + 2) & 255;
            cI = (cI + 4) & 255;
        }
        // SRAM-to-SRAM, both tables already in the slab: cheaper than
        // redoing 64 more PSRAM reads that would produce the same bytes.
        memcpy(cTable + 64, cTable, 64 * sizeof(uint16_t));
        memcpy(cDithTable + 64, cDithTable, 64 * sizeof(uint16_t));
        for (int k = 64; k < 128; k++) {
            bTable[k] = static_cast<uint16_t>(rowB[bI]);
            bI = (bI + 2) & 255;
        }
        // Measured on device (kb.py, min_ms filters interrupts): 12.92-12.93
        // -> 11.71 ms per frame, reproduced bit-for-bit identical across
        // three separate builds/uploads, so this is a real ~9.4% cut to the
        // deterministic compute cost, not noise (ambient device load moved
        // band()'s own min_ms by more than this between runs; blob's own
        // min_ms did not move at all across those same runs). Correctness
        // is a closed-form identity (see above), independently checked
        // numerically over 20,000 random (g_cx, densOff, bayerRow) triples
        // with zero mismatches, and confirmed on device once directly
        // against the flashed firmware's band() before the on-device
        // equality check's own scroll-state drift (unrelated to this
        // change; see the file's other round-5 note above) made that
        // comparison unreliable run to run.

        uint8_t *ixCur = curBuf == 0 ? ixBufs0 : ixBufs1;
        uint8_t *ixNext = curBuf == 0 ? ixBufs1 : ixBufs0;
        if (r == 0) {
            // First row of the call: no predecessor to reuse from -- see
            // bandRef's identical comment for the full reasoning.
            if (rowAligned) {
                alignas(16) uint16_t fv[8];
                for (int k = 0; k < 8; k++) {
                    fv[k] = static_cast<uint16_t>(axF);
                }
                lerpShiftRowPie(ixCur, rowA0, fv, 16);
                ixCur[255] = lerpScalar(rowA0[255], rowA0[0], axF);
            } else {
                lerpShiftRowScalar(ixCur, rowA0, axF);
            }
        }
        if (rowAligned) {
            alignas(16) uint16_t fv[8];
            for (int k = 0; k < 8; k++) {
                fv[k] = static_cast<uint16_t>(axF);
            }
            lerpShiftRowPie(ixNext, rowA1, fv, 16);
            ixNext[255] = lerpScalar(rowA1[255], rowA1[0], axF);
        } else {
            lerpShiftRowScalar(ixNext, rowA1, axF);
        }
        {
            alignas(16) uint16_t fv[8];
            for (int k = 0; k < 8; k++) {
                fv[k] = static_cast<uint16_t>(ayF);
            }
            lerpRowPie(blendedA, ixCur, ixNext, fv, 256 / 16);
        }
        // ixNext (this row's interpolated rowA1) is next row's rowA0 -- see
        // bandRef's identical comment for the full reasoning.
        curBuf ^= 1;

        // Refresh the wrap-around double before nebulaFieldPie reads it --
        // see the buffer's declaration comment above for why a full copy,
        // not 16 bytes of slack, is what this rotation needs.
        memcpy(blendedA + 256, blendedA, 256);

        // Combine stage: two calls, one per half of the bcTable period,
        // vectorising exactly bandRef's `x0`/`step` walk (see
        // nebulaFieldPie's header comment for the full derivation and the
        // 2026-09-04 proof this is bit-exact with bandRef's formula), then
        // one gather call for the palette lookup PIE cannot do.
        const int axI255 = g_axI & 255;
        const int firstLen = totalM < 128 ? totalM : 128;
        nebulaFieldPie(idxBuf, blendedA, axI255, bTable, cTable, cDithTable, combineConsts, firstLen / 16);
        if (totalM > 128) {
            const int secondLen = totalM - 128;
            nebulaFieldPie(idxBuf + 128, blendedA, (axI255 + 128) & 255, bTable, cTable, cDithTable, combineConsts,
                           secondLen / 16);
        }
        nebulaGatherScalar(row, idxBuf, palette, totalM / 4);

        if (w > 256) {
            // Tail copy: identical to bandRef's -- see its comment for the
            // period-256 proof. Unchanged; this loop was never the cost.
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
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

void release() {
    releaseTable(paletteExt, PAL_EXT_N * sizeof(uint16_t));
    // An alias into paletteExt (paletteExt + PAD), not its own allocation.
    palette = nullptr;
    // Borrowed: noiseTex256() is a 64 KB fleet-wide asset owned by
    // BgAnimCommon and shared with ember.
    noise = nullptr;
#if defined(__XTENSA__)
    releaseTable(hotBlendedA, 512);
    releaseTable(hotIxBufs, 2 * 256);
    releaseTable(hotBTable, 128 * sizeof(uint16_t));
    releaseTable(hotCTable, 128 * sizeof(uint16_t));
    releaseTable(hotCDithTable, 128 * sizeof(uint16_t));
    releaseTable(hotIdxBuf, 256 * sizeof(int16_t));
#endif
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
    bad = nebulaRowSelfTest(firstBad);
    if (bad != 0) {
        return bad;
    }
    // Combine-kernel checks added for the PIE band() pass (2026-09-04): see
    // nebulaFieldPie's header comment for the arithmetic/addressing split
    // these two cover, and nebulaGatherScalar's for the third.
    bad = nebulaFieldArithSelfTest(firstBad);
    if (bad != 0) {
        return bad;
    }
    bad = nebulaFieldAddrSelfTest(firstBad);
    if (bad != 0) {
        return bad;
    }
    return nebulaGatherSelfTest(firstBad);
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
    bandRef,
};

#endif // GAGGIMATE_SIM
