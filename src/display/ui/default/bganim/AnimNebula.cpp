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

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const int axF = g_axF, ayF = g_ayF;
    const int wA = g_wA, wB = g_wB, densOff = g_densOff;
    static uint8_t blendedA[256];
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
    static int ixBufs[2][256];
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

        int *ixCur = ixBufs[curBuf];
        int *ixNext = ixBufs[curBuf ^ 1];
        if (r == 0) {
            // First row of the call: no predecessor to reuse from (a rows==1
            // call always takes only this path), so interpolate rowA0 fresh.
            // Identical expression to the reuse-eligible rowA1 pass below,
            // just addressed at rowA0 -- kept as a separate copy rather than
            // a shared helper so neither loop gains a call in its body.
            int cur0 = rowA0[0];
            for (int i = 0; i < 255; i++) {
                const int nxt0 = rowA0[i + 1];
                ixCur[i] = cur0 + (((nxt0 - cur0) * axF) >> 8);
                cur0 = nxt0;
            }
            ixCur[255] = cur0 + (((rowA0[0] - cur0) * axF) >> 8);
        }
        {
            int cur1 = rowA1[0];
            for (int i = 0; i < 255; i++) {
                const int nxt1 = rowA1[i + 1];
                ixNext[i] = cur1 + (((nxt1 - cur1) * axF) >> 8);
                cur1 = nxt1;
            }
            ixNext[255] = cur1 + (((rowA1[0] - cur1) * axF) >> 8);
        }
        for (int i = 0; i < 256; i++) {
            const int va = ixCur[i];
            const int vb = ixNext[i];
            blendedA[i] = static_cast<uint8_t>(va + (((vb - va) * ayF) >> 8));
        }
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
