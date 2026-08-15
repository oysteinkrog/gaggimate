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
// the lookup is a plain 32-bit load with no sign-extend.
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
// zero-overhead LOOP instruction (all four loop bounds in band() -- dith
// k=8, blendedA i=255, bcTable k=128, main pixel w -- get one; verified via
// `grep -n loop xtensa-asm/AnimNebula.S`). Golden stays bit-exact. Do not
// revert this on host-number regression alone; check the .S first.
//
// Optimized: opt-nebula, 2026-08-15.

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
    // Final per-pixel color, memoized over the blend's true period (see
    // combine loop below) so the w=480-wide pixel loop degenerates to a
    // sequential table copy instead of re-running the a/b/c blend per pixel.
    static uint16_t fullColor[256];
    for (int r = 0; r < rows; r++) {
        const int y = y0 + r;
        uint16_t *row = dst + static_cast<size_t>(r) * w;
        const uint8_t *bayerRow = &BAYER8[(y & 7) * 8];
        // int (not int8_t): a plain 32-bit load, no sign-extend per pixel.
        int dith[8];
        for (int k = 0; k < 8; k++) {
            dith[k] = (static_cast<int>(bayerRow[k]) - 31) / 4;
        }
        const uint8_t *rowA0 = noise + ((y + g_ayI) & 255) * 256;
        const uint8_t *rowA1 = noise + ((y + g_ayI + 1) & 255) * 256;
        const uint8_t *rowB = noise + ((y * 2 + g_by) & 255) * 256;
        const uint8_t *rowC = noise + ((y * 4 + g_cy) & 255) * 256;

        // Build the bilinear-blended dominant octave once per row — reused
        // for the whole row via the x0 wraparound below. i=255's neighbor
        // (i+1==256) wraps to texel 0; special-cased after the loop instead
        // of masked every iteration so the loop body is a plain counted
        // walk over two pointers.
        for (int i = 0; i < 255; i++) {
            const int da0 = static_cast<int>(rowA0[i + 1]) - static_cast<int>(rowA0[i]);
            const int va = rowA0[i] + ((da0 * axF) >> 8);
            const int da1 = static_cast<int>(rowA1[i + 1]) - static_cast<int>(rowA1[i]);
            const int vb = rowA1[i] + ((da1 * axF) >> 8);
            blendedA[i] = static_cast<uint8_t>(va + (((vb - va) * ayF) >> 8));
        }
        {
            const int da0 = static_cast<int>(rowA0[0]) - static_cast<int>(rowA0[255]);
            const int va = rowA0[255] + ((da0 * axF) >> 8);
            const int da1 = static_cast<int>(rowA1[0]) - static_cast<int>(rowA1[255]);
            const int vb = rowA1[255] + ((da1 * axF) >> 8);
            blendedA[255] = static_cast<uint8_t>(va + (((vb - va) * ayF) >> 8));
        }

        // b (2x) and c (4x) are both nearest-sampled with a fixed per-pixel
        // stride (+2, +4 mod 256), so unlike a's index they walk in lockstep
        // with the pixel counter itself: bIdx/cIdx as a function of pixel
        // step repeats with period lcm(256/gcd(256,2), 256/gcd(256,4)) = 128.
        // Pack rowB[bIdx]/rowC[cIdx] into one uint16 table indexed directly
        // by that step (mod 128), same trick mandala used for two uint8
        // LUTs sharing an index — this drops rowB/rowC/bIdx/cIdx (4 live
        // registers) down to one table pointer + one index (2), freeing
        // room for wA/wB/densOff to stay resident instead of spilling.
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

        // Combine step: run the full a/b/c blend (identical math to the old
        // per-pixel loop) but only far enough to cover its true repeat
        // period, then memoize it. x0(x) = (g_axI+x)&255 has period 256;
        // step(x) = x&127 has period 128; 128 | 256 so the combined value
        // v(x) — and thus the dith term dith[x&7], since 8 | 256 too — is
        // an exact period-256 function of x. Running this loop for m in
        // [0,256) reproduces v(x) for every x via v(x) == fullColor[x&255],
        // bit-for-bit, because x0/step/dith-index at x and at (x mod 256)
        // are identical by construction (all three periods divide 256).
        // This turns the expensive blend (loads + 2 muls + shifts) from a
        // per-real-pixel cost (up to w=480/row) into a fixed 256/row cost,
        // with the real w-wide loop below reduced to a cache-resident
        // table copy.
        {
            int x0 = g_axI & 255;
            int step = 0;
            for (int m = 0; m < 256; m++) {
                const int a = blendedA[x0];
                const uint16_t bc = bcTable[step];
                const int b = bc & 0xFF;
                const int c = bc >> 8;
                const int v = c + ((((a - c) * wA) + ((b - c) * wB)) >> 6) + densOff + dith[m & 7];
                fullColor[m] = paletteExt[PAD + v];
                x0 = (x0 + 1) & 255;
                step = (step + 1) & 127;
            }
        }

        // Real per-pixel loop: sequential read from a 256-entry (512B)
        // table that stays resident in cache across the whole row, plus a
        // masked wraparound index — no blend math, no palette indirection,
        // left per pixel. This loop is cheap enough (no compute) that the
        // pixel-pair packed store (same trick as Aurora/Lava) is a clear
        // win here even though it lost a previous fidelity/register fight
        // when tried on the (much heavier) old single-pixel blend loop:
        // it halves both iteration count and store traffic.
        {
            int m = 0;
            uint16_t *rp = row;
            // NOTE (verified in xtensa-asm/AnimNebula.S): this loop does NOT
            // get GCC's Xtensa zero-overhead LOOP instruction, unlike the
            // four loops above (dith/blendedA/bcTable/combine, all constant
            // trip counts). A countdown form (tried here, mirroring the lava
            // fix in OPTIMIZE.md) did not unlock it either -- this appears
            // to be a real-compiler limit on this function (possibly on
            // hw-loop count per function, or on runtime-variable trip counts
            // this late in the body), not a code-shape problem we found a
            // fix for. It still nets a clear win over the old per-pixel
            // loop: 2 pixels/iteration halves both the taken-branch count
            // and the store count, and the body itself is pure loads/store
            // (no blend math), so the missing hw loop only taxes a cheap
            // loop. Do not assume this comment is stale if a future pass
            // finds the actual cause -- it was checked, not guessed.
            for (int i = w >> 1; i > 0; i--) {
                const uint16_t p0 = fullColor[m];
                m = (m + 1) & 255;
                const uint16_t p1 = fullColor[m];
                m = (m + 1) & 255;
                *reinterpret_cast<uint32_t *>(rp) = static_cast<uint32_t>(p0) | (static_cast<uint32_t>(p1) << 16);
                rp += 2;
            }
            if (w & 1) {
                *rp = fullColor[m];
            }
        }
    }
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
};

#endif // GAGGIMATE_SIM
