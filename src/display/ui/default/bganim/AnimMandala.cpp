#ifndef GAGGIMATE_SIM

// "Mandala": N-fold rotational symmetry built from angular harmonics
// (sin(N*theta)), which are smooth and periodic by construction, no fold
// seams. Design: anim-geometric (Fable), 2026-08-15. The page entry in
// tools/animbench/web/anim_bench.html is the approved picture:
//
//   v  = sin(N*theta + rn*7*turb - 1.4t)
//      + 0.55*cos(2N*theta - rn*4.5*turb*ringScale + 0.8t)
//      + 0.3*sin(3N*theta + rn*3*turb + 0.5t)          (complexity > 60 only)
//      + 0.4*sin(rn*9 - t)
//   v  = (v/norm)*0.5 + 0.5, contrast about 0.5, times breathe, times
//        (0.30 + 0.70*vignette), then the theme ramp.
//
// Brought to the page, 2026-09-12 (gm-pciz). Until then the device drew a
// different picture: octantAngle() shifted the Q16 ratio down before the
// polynomial saw it, so the angle term was 0 or 64 everywhere and the
// N-fold symmetry never reached the panel; the radial phase step was
// turb*18 ticks per pixel where the page's is turb*1.19, fifteen times
// denser rings; the third layer and the 0.4*sin(rn*9 - t) term were
// missing, the B term was sin at 0.5 where the page has cos at 0.55, and the
// vignette went to black at the rim where the page keeps 30%. About 174,000
// of 230,400 pixels differed from the page per frame. Every one of those
// is fixed below, and the page entry now mirrors this file's integer chain
// entry for entry (the same sine tables, the same 9-bit angle and 2-pixel
// radius buckets, the same 2x2 interpolation), so the two are pixel exact.
//
// How the chain is laid out, and why:
//
// Polar map (init(), geometry only): angle and radius never depend on time
// or parameters, so they are computed once into a quadrant-symmetric map,
// (cx+1)^2 uint16 entries (~115 KB in PSRAM): bits 15..7 hold the
// octant-folded angle of (|dx|,|dy|) in 512ths of a turn (0..128 across the
// quadrant), bits 6..0 the radius in 2-pixel buckets (0..120) or the
// sentinel 0x7F for "outside the disc". The full angle is rebuilt from the
// quadrant angle and the two sign bits algebraically, folded into g_N's sign
// and one additive offset per row half, so band() pays one multiply-add per
// sample and no branch (angleQ9 = (sx ? 256 : 0) + (sx == sy ? +1 : -1)*oct;
// base = angleQ9*N = (sx ? SX_OFFSET : 0) + oct*(sx == sy ? N : -N) mod 512,
// SX_OFFSET = (N & 1) ? 256 : 0). Nine angle bits, not eight: at N = 8 an
// angle tick moves the A harmonic by 1/64 of a cycle, about 6 palette levels
// of 255 at the steepest, and about 3 px wide at the rim, under one RGB565
// step; at eight bits the same step was 13 levels and 6 px, a visible
// staircase along every petal. Seven radius bits, not eight, pay for the
// ninth angle bit inside the same 16-bit entry: two pixels of radius move
// the A harmonic by 2.5 degrees at the default complexity, about 3 levels.
// The map stays 16 bits wide because the fast path sweeps it from PSRAM
// once per sample and that sweep is the part of band time that is bus, not
// arithmetic (see the 2026-09-05 note below).
//
// Per-frame radial tables (frame()): everything that is a function of the
// radius bucket and the frame's parameters is baked into rParams[r] (bits
// 0..8 the A harmonic's phase in 512ths of a turn, 9..17 the B harmonic's,
// 18..24 the vignette-times-breathe factor Q7, 25..31 the radial
// 0.4*sin(rn*9 - t) term as a Q7 value biased by 64) and, for the third
// layer, rParamsC[r] (bits 0..8, 512ths of a turn). The bias keeps the
// packed word unsigned; it comes back out through the rescale table's base
// pointer, so the sample loop never subtracts it. Seven bits of vignette
// and seven of D, not eight, are what make the two 9-bit phases fit the
// word: a vignette step of 1/128 is under two palette levels.
//
// Per sample (mandalaIndex): base = halfOffset + oct*gN_eff, then
//   v = sinA[(base + A) & 511] + sinB[(2*base + B) & 511] + D
//     (+ sinC[(3*base + C) & 511] with three layers)
// with sinA at amplitude 127, sinB at 0.55*127, sinC at 0.3*127 and D at
// 0.4*127, so v spans [-248, 248] (two layers) or [-286, 286] (three), then
// rescaleLUT maps that span to 0..255 with the contrast expansion folded
// in, and the vignette factor scales it. Against the chain this replaced
// (two harmonics, no D) the sample costs one more load-free add and one
// shift: A and B come out of the word the way they did, the D field is the
// word's top bits. The outside-disc case falls out of the same arithmetic:
// rParams[0x7F] carries vig 0 and the D bias alone, so v collapses to 0 and
// paletteLUT[0] == themeRGB(0), the page's `outside`.
//
// Redesign, 2026-09-05 (design-mandala), kept: the per-sample chain above
// runs on two of a 2x2 output block's four pixels (its top-left corner and
// every other one to its right); the other two are linear blends of the
// pre-palette magnitude, never of the palette colour or the wrapping polar
// angle, so the interpolation cannot tear across a phase wrap. The 64x64
// centre square (INNER_BAND rows by INNER_HALF_W columns either side of
// centre, where curvature and the resampling grain were both worst) is
// redrawn at full resolution on top of the halved render. A one-row cache
// (g_sampleCache/fetchRow) carries a sampled row's magnitudes forward so a
// contiguous run of block-pairs samples each row once, not twice; every
// call shape interlace_check exercises (solitary rows, out-of-parity
// sequences, every band height) has to produce the same pixels whether or
// not that cache happens to be warm, and --shapes confirms it does. The
// fast path reads polarMapQuarter, the even/even quarter of the map (about
// 29 KB), which cut the PSRAM sweep to a quarter of its bytes; most of the
// remaining band time is the per-sample arithmetic. Production band time
// went from 42.9 ms for the exact per-pixel design to 22.9 ms for this one;
// two cheaper variants (horizontal-only interpolation at 21.3 ms, plain
// duplication at 17.1 ms) looked worse and the owner picked this picture.
// The per-frame moire guard (WRAP_GUARD, g_fineDetail) falls back to full
// resolution when a radial phase step gets close to the 2-pixel column
// pitch; at the page's rates the steepest reachable step (turb 1.35, ring
// pitch 100) is about 6 of 512 per pixel against a guard of 48, so on this
// design the guard never trips and the full-resolution path is kept for
// the day a rate is raised.
//
// Assembly, kept: interpPairKernel is the one Xtensa kernel, the block-pair
// interpolate-and-gather, a hand transcription of GCC's own schedule for
// writeInterpPair (five static instructions per call lighter, see the
// 2026-09-04 round-2 report). It reads magnitudes and the palette only, so
// nothing in this pass touched it or what it reads;
// tools/qemubench/tests/anim_mandala checks it against the scalar
// reference. Earlier rounds (a PIE decode of the map, a per-call copy of
// the map rows into SRAM) measured slower on the device and are not here.
//
// Five sliders, 2026-09-10 (gm-3vj.10): rotation, vignette, breathe,
// contrast and ring pitch. Each is 1x, or 0, at 50. Rotation ("Ring
// drift") is a constant added to both row halves' base, which with the
// angle term working is exactly a rotation by (constant / N) 512ths of a
// turn, the same as the page's spinTurns.
#include "BgAnim.h"
#include "BgAnimClock.h"
#include "BgAnimCommon.h"
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

namespace {
using namespace bganim;

// Angle resolution: 512ths of a turn, 128 across a quadrant.
constexpr int ANG_N = 512;
constexpr int ANG_MASK = ANG_N - 1;
constexpr int OCT_MAX = ANG_N / 4;
// Radius buckets: two pixels each, 0..120 at the panel's 240 px radius,
// 0x7F for outside the disc. rParams and rParamsC are sized to the 7-bit
// field so the sentinel indexes a real entry.
constexpr int R_BITS = 7;
constexpr int R_MASK = (1 << R_BITS) - 1;
constexpr int R_ENTRIES = 1 << R_BITS;
constexpr uint8_t OUTSIDE_R = R_MASK;
// Harmonic amplitudes in Q7, the page's 1, 0.55, 0.3 and 0.4.
constexpr int AMP_A = 127;
constexpr int AMP_B = 70;  // lroundf(0.55f * 127)
constexpr int AMP_C = 38;  // lroundf(0.3f * 127)
constexpr int AMP_D = 51;  // 0.4 * 127, taken as (sinA * 2) / 5 per entry
constexpr int D_BIAS = 64; // D + D_BIAS fits the word's top seven bits
constexpr int VMAX2 = AMP_A + AMP_B + AMP_D; // 248, two layers
constexpr int VMAX3 = VMAX2 + AMP_C;         // 286, three layers
constexpr int RESCALE_SPAN = 2 * VMAX3 + 1;
constexpr int RESCALE_PAD = 4;
constexpr int RESCALE_N = RESCALE_SPAN + 2 * RESCALE_PAD;

int16_t *sinA = nullptr; // Q7 sine, 512 entries
int16_t *sinB = nullptr; // 0.55 * Q7 sine, 512 entries
int16_t *sinC = nullptr; // 0.3 * Q7 sine, 512 entries
uint32_t *recipLUT = nullptr; // Q16 65536/(i+1), 0..240; init-time only
uint8_t *vigByR = nullptr;    // radius bucket -> vignette falloff Q8; frame-time only
// (v + VMAX) -> 0..255 with the contrast expansion folded in, padded on
// both ends. rescaleLUT points RESCALE_PAD entries into rescaleAlloc, the
// base release() hands back; rescaleAt points VMAX - D_BIAS entries further
// in, so mandalaIndex indexes it with the biased sum directly.
uint8_t *rescaleLUT = nullptr;
uint8_t *rescaleAlloc = nullptr;
const uint8_t *rescaleAt = nullptr;
uint16_t *paletteLUT = nullptr;
uint16_t *polarMap = nullptr; // quadrant map: (angle << 7 | radiusBucketOr0x7F), (cx+1)x(cx+1), row-major
// Every fast-path sample reads polarMap only at even ax and even ay: the
// row is always an even absolute output row, and the column walk starts
// aligned and steps by two. polarMapQuarter holds exactly those entries,
// (cx/2+1)^2 of them, filled at init() as a copy of polarMap's own values
// so the two cannot disagree. patchCentre and computeRowFull read polarMap
// directly: they sample odd coordinates too.
uint16_t *polarMapQuarter = nullptr;
// rParams[r], r a radius bucket (plus the outside sentinel): bits 0..8 the
// A harmonic's phase offset (512ths of a turn), 9..17 the B harmonic's,
// 18..24 the vignette-times-breathe factor Q7, 25..31 the radial term D
// plus D_BIAS. Rebuilt every frame except the sentinel entry, which init()
// sets once (vig 0, D bias only) and frame() never touches.
uint32_t *rParams = nullptr;
// The third layer's phase offset (512ths of a turn), read only when
// complexity is above 60. Same sentinel rule.
uint16_t *rParamsC = nullptr;
uint32_t lastThemeGen = 0xFFFFFFFF;
// Whether the one-time table fills have run. Namespace scope, not a static
// local in init(): vigByR, polarMap and polarMapQuarter are sized from cx,
// so a resolution change has to rebuild them, and release() can only reset
// this flag if it can see it.
bool tablesBuilt = false;
int g_cx = 240; // half panel width; also the map's per-axis extent (assumes cx < 255)

// One sampled row of pre-palette magnitudes, carried forward so a row-pair
// does not resample the row the pair below it will also need: keyed by
// which absolute row it holds and the width it was sampled at, checked
// before every use and recomputed on any mismatch by fetchRow below, never
// assumed valid. frame() resets g_sampleCacheY to the sentinel on every
// call, since that is the only thing that changes what sampleRowIndices(y)
// returns for a fixed y.
constexpr int NO_CACHED_ROW = INT32_MIN;
uint8_t *g_sampleCache = nullptr;
int g_sampleCacheY = NO_CACHED_ROW;
int g_sampleCacheW = 0;

void buildThemePalette() { buildThemeRamp(paletteLUT, 256); }

// The rescale table's inputs, so frame() rebuilds it only when one moves.
int lastContrastP = -1;
bool lastThreeLayers = false;
int g_vmax = VMAX2;

// (v + vmax) -> 0..255, with the contrast expansion folded in. Rebuilt on a
// contrast or layer-count change, never per frame and never per pixel.
// The expansion is about the midpoint, so 128 stays put and the ends bend:
// below 1x the rings flatten toward one mid tone, above 1x they clip to the
// palette's two ends. kC is the page's real-valued factor, (64 + p*192/50)
// / 256 up to the middle of the slider and (256 + (p-50)*768/50) / 256
// above it, exactly 1 at 50. Written through the padded base so the pad
// entries repeat the clamped end values.
void buildRescale(float kC, int vmax) {
    const int span = 2 * vmax + 1;
    for (int j = 0; j < RESCALE_N; j++) {
        int i = j - RESCALE_PAD;
        if (i < 0) {
            i = 0;
        } else if (i > span - 1) {
            i = span - 1;
        }
        const int lin = (i * 255) / (2 * vmax);
        int v = 128 + static_cast<int>(static_cast<float>(lin - 128) * kC);
        if (v < 0) {
            v = 0;
        } else if (v > 255) {
            v = 255;
        }
        rescaleLUT[j - RESCALE_PAD] = static_cast<uint8_t>(v);
    }
    rescaleAt = rescaleLUT + (vmax - D_BIAS);
}

// Octant-folded angle (0..128) for a point in the first quadrant
// (ax, ay >= 0), in 512ths of a turn. A reciprocal table and a minimax
// polynomial for atan, called (cx+1)^2 times, once, in init().
//
// recipLUT[i] is 65536 / (i + 1), so lo * recipLUT[hi - 1] is already lo / hi
// in Q16 (at most 240 * 65536, inside 32 bits), and it stays Q16 until it
// becomes a float. Until gm-3vj.48 (fixed on both branches, 2026-09-12 on
// the parameter branch) the product was shifted down by 16 as well, which
// left the integer part of lo / hi: 0 everywhere but the diagonal. Every
// pixel then got an angle of 0 or OCT_MAX, the harmonics had no angular
// term, and Symmetry changed nothing.
inline uint8_t octantAngle(int ax, int ay) {
    const bool swap = ax < ay;
    const int hi = swap ? ay : ax;
    const int lo = swap ? ax : ay;
    if (hi == 0) {
        return 0;
    }
    // hi <= cx (<=240 for the real panel), recipLUT covers that range.
    const uint32_t ratioQ16 = static_cast<uint32_t>(lo) * recipLUT[hi - 1];
    const float ratio = ratioQ16 * (1.0f / 65536.0f);
    const float ang = ratio * (0.9817f - 0.1963f * ratio * ratio); // radians, 0..pi/4
    // Truncated, not rounded: the page's octantAngle takes Math.trunc of the
    // same product, and this map has to match it entry for entry (gm-pciz).
    // gm-3vj.48 rounded here at 256 ticks a turn; at 512 the truncation
    // error is half a tick of the old scale, the same bound rounding gave.
    int oct = static_cast<int>(ang * (256.0f / 3.14159265f));      // 0..64 within octant
    if (swap) {
        oct = OCT_MAX - oct;
    }
    return static_cast<uint8_t>(oct);
}

bool init(int w, int) {
    // Every table carries its own guard and the combined check below runs
    // unconditionally, so a partial allocation failure retries cleanly on
    // the next activation.
    g_cx = w / 2;
    const int mapDim = g_cx + 1;
    // Per-sample tables go on the hot slab, with alloc() (PSRAM) as the
    // fallback so a full slab degrades this animation instead of failing
    // its init(): sinA, sinB, sinC (1,024 B each), rescaleLUT (581 B),
    // paletteLUT (512 B), rParams (512 B), rParamsC (256 B) and
    // g_sampleCache (up to 241 B, g_cx + 1), 5,174 B in all against the 9,216 B
    // per-animation slab. recipLUT and vigByR are read at init() and
    // frame() only and stay on alloc().
    if (sinA == nullptr) {
        sinA = static_cast<int16_t *>(allocHot(ANG_N * sizeof(int16_t)));
        if (sinA == nullptr) {
            sinA = static_cast<int16_t *>(alloc(ANG_N * sizeof(int16_t)));
        }
    }
    if (sinB == nullptr) {
        sinB = static_cast<int16_t *>(allocHot(ANG_N * sizeof(int16_t)));
        if (sinB == nullptr) {
            sinB = static_cast<int16_t *>(alloc(ANG_N * sizeof(int16_t)));
        }
    }
    if (sinC == nullptr) {
        sinC = static_cast<int16_t *>(allocHot(ANG_N * sizeof(int16_t)));
        if (sinC == nullptr) {
            sinC = static_cast<int16_t *>(alloc(ANG_N * sizeof(int16_t)));
        }
    }
    if (recipLUT == nullptr) {
        recipLUT = static_cast<uint32_t *>(alloc(241 * sizeof(uint32_t)));
    }
    if (vigByR == nullptr) {
        vigByR = static_cast<uint8_t *>(alloc(R_ENTRIES));
    }
    if (rescaleLUT == nullptr) {
        rescaleAlloc = static_cast<uint8_t *>(allocHot(RESCALE_N));
        if (rescaleAlloc == nullptr) {
            rescaleAlloc = static_cast<uint8_t *>(alloc(RESCALE_N));
        }
        rescaleLUT = rescaleAlloc != nullptr ? rescaleAlloc + RESCALE_PAD : nullptr;
    }
    if (paletteLUT == nullptr) {
        paletteLUT = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
        if (paletteLUT == nullptr) {
            paletteLUT = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
        }
    }
    // polarMap is ~115 KiB ((cx+1)^2 uint16 entries), a bulk table read in
    // sequential sweeps, so it goes straight to PSRAM (heap_caps_malloc with
    // MALLOC_CAP_SPIRAM, not ps_malloc, which the host shim lacks). No
    // alignment requirement: it is read with plain scalar loads.
    if (polarMap == nullptr) {
        polarMap = static_cast<uint16_t *>(
            heap_caps_malloc(static_cast<size_t>(mapDim) * mapDim * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
    }
    const int qDim = g_cx / 2 + 1;
    if (polarMapQuarter == nullptr) {
        polarMapQuarter = static_cast<uint16_t *>(
            heap_caps_malloc(static_cast<size_t>(qDim) * qDim * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
    }
    if (rParams == nullptr) {
        rParams = static_cast<uint32_t *>(allocHot(R_ENTRIES * sizeof(uint32_t)));
        if (rParams == nullptr) {
            rParams = static_cast<uint32_t *>(alloc(R_ENTRIES * sizeof(uint32_t)));
        }
    }
    if (rParamsC == nullptr) {
        rParamsC = static_cast<uint16_t *>(allocHot(R_ENTRIES * sizeof(uint16_t)));
        if (rParamsC == nullptr) {
            rParamsC = static_cast<uint16_t *>(alloc(R_ENTRIES * sizeof(uint16_t)));
        }
    }
    // This file's own per-pixel-adjacent set: read by fetchRow's cache and,
    // via its aliasing pointers, by interpPairKernel's block loop. Sized
    // off g_cx (<=240), not a fixed 256, since it only ever needs one row's
    // worth of blocks: (w + 1) / 2 of them, which is g_cx + 1 at an odd w
    // (117 at 233, where the last block is column w - 1 alone).
    if (g_sampleCache == nullptr) {
        g_sampleCache = static_cast<uint8_t *>(allocHot(static_cast<size_t>(g_cx) + 1));
        if (g_sampleCache == nullptr) {
            g_sampleCache = static_cast<uint8_t *>(alloc(static_cast<size_t>(g_cx) + 1));
        }
    }
    g_sampleCacheY = NO_CACHED_ROW;
    if (sinA == nullptr || sinB == nullptr || sinC == nullptr || recipLUT == nullptr || vigByR == nullptr ||
        rescaleLUT == nullptr || paletteLUT == nullptr || polarMap == nullptr || polarMapQuarter == nullptr ||
        rParams == nullptr || rParamsC == nullptr || g_sampleCache == nullptr) {
        return false;
    }
    if (!tablesBuilt) {
        tablesBuilt = true;
        for (int i = 0; i < ANG_N; i++) {
            const float s = sinf(i * 6.2831853f / static_cast<float>(ANG_N));
            sinA[i] = static_cast<int16_t>(lroundf(127.0f * s));
            sinB[i] = static_cast<int16_t>(lroundf(0.55f * 127.0f * s));
            sinC[i] = static_cast<int16_t>(lroundf(0.3f * 127.0f * s));
        }
        // Outside-disc sentinel slots: vig 0 zeroes v whatever the harmonic
        // indices land on, and the D field carries the bias alone so the
        // biased sum stays inside the rescale table. Set once, here. Every
        // entry starts as the sentinel, so a bucket the map cannot produce
        // (121..126) still reads a defined word until frame() runs.
        for (int r = 0; r < R_ENTRIES; r++) {
            rParams[r] = static_cast<uint32_t>(D_BIAS) << 25;
            rParamsC[r] = 0;
        }
        for (int i = 0; i < 241; i++) {
            recipLUT[i] = static_cast<uint32_t>(lroundf(65536.0f / (i + 1)));
        }
        const int rMax = g_cx / 2; // 120 buckets at cx 240
        for (int r = 0; r < R_ENTRIES; r++) {
            const int rr = r < rMax ? r : rMax;
            vigByR[r] = static_cast<uint8_t>(
                lroundf(255.0f * powf(1.0f - static_cast<float>(rr) / static_cast<float>(rMax), 0.55f)));
        }
        buildRescale(1.0f, VMAX2);
        lastContrastP = 50;
        lastThreeLayers = false;
        g_vmax = VMAX2;
        const int maxR2 = g_cx * g_cx;
        for (int ay = 0; ay < mapDim; ay++) {
            for (int ax = 0; ax < mapDim; ax++) {
                const int r2 = ax * ax + ay * ay;
                uint8_t rOrOut;
                if (r2 > maxR2) {
                    rOrOut = OUTSIDE_R;
                } else {
                    rOrOut = static_cast<uint8_t>(lroundf(sqrtf(static_cast<float>(r2)) * 0.5f));
                }
                const uint16_t oct = octantAngle(ax, ay);
                const uint16_t entry = static_cast<uint16_t>((oct << R_BITS) | rOrOut);
                polarMap[ay * mapDim + ax] = entry;
                if ((ax & 1) == 0 && (ay & 1) == 0) {
                    polarMapQuarter[(ay / 2) * qDim + (ax / 2)] = entry;
                }
            }
        }
        buildThemePalette();
        lastThemeGen = themeGen();
    }
    return true;
}

int g_N = 8;
int g_sxOffset = 0; // (g_N & 1) ? 256 : 0, see the top-of-file comment
// The `halfOffset` every sampling run is called with: the left half's is
// g_sxOffset, the right half's is 0, and the drift slider adds the same
// constant to both, which rotates the whole picture by (constant / N)
// 512ths of a turn, the page's spinTurns.
int g_halfOffL = 0;
int g_halfOffR = 0;
bool g_threeLayers = false;
int g_breatheQ8 = 256;
// Q8 scale on the vignette's fall from the centre. 256 leaves vigByR
// alone, 0 flattens it to full brightness everywhere, 512 doubles the fall.
int g_vigStrengthQ8 = 256;

// Moire guard: the steepest radial phase step, in 512ths of a turn per
// pixel, above which bandRef() renders every row at full resolution rather
// than let the halved scheme's 2-pixel column pitch alias a ring spacing
// into a cross-hatch. At the page's rates the steepest reachable step is
// about 6 (see the top-of-file comment), so this never trips on this
// design; it is kept, at no per-pixel cost, for the day a rate is raised.
constexpr float WRAP_GUARD = 48.0f;
bool g_fineDetail = false;

// Animation time (BgAnimClock.h, gm-f91g). Not reset by release(), so the
// full/half switch keeps the pattern where it was. frame() used to take
// t = tMs * 0.001 * 0.35 * speed in float, so the rotation stepped in whole
// ticks of float(tMs) after 3.1 days of uptime, jumped back to t = 0 at the
// millis() wrap, and jumped when a speed changed. The clock carries the
// speed now. The coefficients are the design's, in radians per unit of
// that old t: 1.4, 0.8 and 0.5 for the three ring phases, 1.0 for the
// radial distortion and 0.45 for the breathing. T_PER_MS converts them to
// radians per animation millisecond, and T_CAL is the 1.75 speed
// calibration (gm-33fm). The calibration sits in the rates and in pageT's
// chain rather than in the clock, because the page applies it last, to t,
// and a clock running at 1.75 rounds t differently (gm-pciz: that was 19709
// differing pixels at golden frame 210).
AnimClock g_clock;
constexpr double T_PER_MS = 0.35e-3;
constexpr double T_CAL = 1.75;
// The page's t per animation millisecond and its 512ths of a turn per
// radian, from the same float constants pageT() and frame() multiply by, so
// the rates below are the page chain's own rates and the handover at
// PAGE_EXACT_Q16 does not jump (with T_PER_MS * T_CAL in double instead, the
// phases came out about 0.1 tick apart there, a step of 8169 changed pixels in
// one millisecond against about 1200 either side).
constexpr double PAGE_T_PER_MS = static_cast<double>(0.001f) * static_cast<double>(0.35f) * T_CAL;
constexpr double PAGE_K512 = static_cast<double>(512.0f / 6.2831853f);
// A rate in 512ths of a turn per animation millisecond, as a Q48 turn rate.
constexpr uint64_t rate512(double ticksPerMs) { return oscRateQ48(ticksPerMs * (6.283185307179586 / 512.0)); }
constexpr uint64_t RATE_A = rate512(PAGE_T_PER_MS * static_cast<double>(1.4f) * PAGE_K512);
constexpr uint64_t RATE_B = rate512(PAGE_T_PER_MS * static_cast<double>(0.8f) * PAGE_K512);
constexpr uint64_t RATE_C = rate512(PAGE_T_PER_MS * static_cast<double>(0.5f) * PAGE_K512);
constexpr uint64_t RATE_D = rate512(PAGE_T_PER_MS * PAGE_K512);
constexpr uint64_t RATE_BREATHE = oscRateQ48(PAGE_T_PER_MS * static_cast<double>(0.45f));
// How long frame() follows the page's own float arithmetic (gm-pciz). The
// page forms t in float and chains float products from it, so the only way
// to draw its frames bit for bit is to run the same chain on the clock's
// time. That chain loses resolution as t grows, so past 2048 s of animation
// time (34 minutes at Speed 50, where t is near 1254 and the fastest phase
// near 1.4e5 ticks of 512 a turn, a float ulp of 0.016 tick) frame() takes
// the Q48 rates instead. The handover moves a phase by a few hundredths of a
// tick, once; the rates then never lose precision and never jump at a wrap.
constexpr uint64_t PAGE_EXACT_Q16 = 2048000ull << 16;
// The page's t from the clock: milliseconds rounded to float, then
// * 0.001f * 0.35f * 1.75f in that order. Exact whole milliseconds at Speed
// 50, where the clock runs at exactly 1.
inline float pageT(const AnimClock &c) {
    return static_cast<float>(static_cast<double>(c.simQ16) * (1.0 / 65536.0)) * 0.001f * 0.35f * 1.75f;
}
// A Q32 turn in 512ths of a turn, with its fraction: 2^-23, exact in float.
constexpr float Q32_TO_512 = 512.0f / 4294967296.0f;
static_assert(ANG_N == 512, "Q32_TO_512 and the drift shift assume 512 ticks a turn");
// The ring drift's phase as a Q32 turn. Its rate is signed (Ring drift
// below 50 turns the other way), which a Q48 oscillator rate on a clock
// that only runs forward cannot follow, so frame() integrates it from the
// clock's wall step instead. Modular, so it never overflows; not reset by
// release(), like the clock.
uint32_t g_driftQ32 = 0;

void frame(uint32_t tMs, int, int, const uint8_t p[BG_ANIM_PARAMS]) {
    if (themeGen() != lastThemeGen) {
        buildThemePalette();
        lastThemeGen = themeGen();
    }
    g_N = 4 + (static_cast<int>(p[1]) * 8 + 50) / 100;
    g_sxOffset = (g_N & 1) ? ANG_N / 2 : 0;
    // 0.35 was the base rate (T_PER_MS); 1.75 is the speed calibration
    // (gm-33fm, T_CAL). The clock runs at the Speed setting alone and every
    // motion below reads it; the drift accumulator takes the calibrated
    // speed, as before.
    const float speed = 1.75f * speedMul(p[0]);
    const uint32_t dtMs = g_clock.advance(tMs, speedMul(p[0]));
    const bool pageClock = g_clock.simQ16 < PAGE_EXACT_Q16;
    const float t = pageClock ? pageT(g_clock) : 0.0f;
    const float turb = 0.25f + (p[2] / 100.0f) * 1.1f;
    g_threeLayers = p[2] > 60;
    // Contrast, folded into rescaleLUT, together with the layer count that
    // sets the table's span. p[6] == 50 is exactly 1.
    if (static_cast<int>(p[6]) != lastContrastP || g_threeLayers != lastThreeLayers) {
        const float kC = p[6] <= 50 ? (64.0f + (p[6] * 192.0f) / 50.0f) / 256.0f
                                    : (256.0f + ((p[6] - 50) * 768.0f) / 50.0f) / 256.0f;
        g_vmax = g_threeLayers ? VMAX3 : VMAX2;
        buildRescale(kC, g_vmax);
        lastContrastP = p[6];
        lastThreeLayers = g_threeLayers;
    }
    // Ring pitch, the page's ringScale: 1 at 50, 0 at 0, 3 at 100.
    const float ringScale =
        (p[7] <= 50 ? (p[7] * 60.0f) / 50.0f : 60.0f + ((p[7] - 50) * 120.0f) / 50.0f) / 60.0f;
    // Phase steps per radius bucket (two pixels, rn = r / 120) and the time
    // phases, all in 512ths of a turn. K512 is 512 ticks per 2*pi radians.
    constexpr float K512 = 512.0f / 6.2831853f;
    const float stepA = 7.0f * turb / 120.0f * K512;
    const float stepB = 4.5f * turb * ringScale / 120.0f * K512;
    const float stepC = 3.0f * turb / 120.0f * K512;
    // The time phases, in 512ths of a turn with their fraction. While the
    // page's chain is exact they are the page's t products; after it, the
    // oscillator's Q32 turn scaled down, so they stay in [0, 512) whatever
    // the uptime. + 128 is a quarter turn: the page's cos.
    float phA, phB, phC, phD;
    if (pageClock) {
        phA = t * 1.4f * K512;
        phB = t * 0.8f * K512 + 128.0f;
        phC = t * 0.5f * K512;
        phD = 0.0f; // the page subtracts t before scaling, below
    } else {
        // Whole turns are added so every term below keeps the sign it has on
        // the page, where these phases have grown far past a turn: the int
        // cast truncates toward zero, so a term that changed sign across the
        // radius would round the other way on part of it, a one-tick shift
        // (8169 changed pixels in the handover millisecond against about 1200
        // either side). r * stepA stays under 1024 and r * stepB under 1536
        // and r * 9/120 * K512 under 768 at every param, and a multiple of
        // 512 is exact in float at these sizes.
        phA = static_cast<float>(oscTurnQ32(g_clock, RATE_A)) * Q32_TO_512 + 1024.0f;
        phB = static_cast<float>(oscTurnQ32(g_clock, RATE_B)) * Q32_TO_512 + 128.0f + 2048.0f;
        phC = static_cast<float>(oscTurnQ32(g_clock, RATE_C)) * Q32_TO_512;
        phD = static_cast<float>(oscTurnQ32(g_clock, RATE_D)) * Q32_TO_512 + 1024.0f;
    }
    // The steepest step per pixel, in 512ths: a step is per two pixels.
    float steepest = stepA > stepB ? stepA : stepB;
    if (g_threeLayers && stepC > steepest) {
        steepest = stepC;
    }
    g_fineDetail = steepest * 0.5f >= WRAP_GUARD;
    // Ring drift: p[3] == 50 is a rate of exactly 0.0f. Signed, so below 50
    // the picture turns the other way. The design's rate is 0.35 * driftRate
    // turns per unit of t; this frame's step is dtMs of wall time at the
    // clock's speed, accumulated as a Q32 turn so a slider move bends the
    // drift instead of jumping it. Masked to a turn before the multiply by
    // g_N, so the product cannot overflow.
    const float driftRate = (static_cast<int>(p[3]) - 50) / 50.0f;
    g_driftQ32 += static_cast<uint32_t>(static_cast<int64_t>(
        llround(static_cast<double>(dtMs) * speed * T_PER_MS * 0.35 * driftRate * 4294967296.0)));
    const int driftQ = static_cast<int>(g_driftQ32 >> 23) & ANG_MASK;
    const int driftOffset = (driftQ * g_N) & ANG_MASK;
    g_halfOffL = (g_sxOffset + driftOffset) & ANG_MASK;
    g_halfOffR = driftOffset;
    // Breathe depth. p[5] == 50 gives an amplitude of exactly 0.18f.
    const float breatheAmp = 0.18f * (p[5] / 50.0f);
    g_breatheQ8 = static_cast<int>((1.0f - breatheAmp + breatheAmp * fastSinRad(pageClock ? t * 0.45f : oscRad(g_clock, RATE_BREATHE))) * 256.0f);
    // Vignette depth, applied to vigByR's fall. 256 is the identity.
    g_vigStrengthQ8 = (p[4] * 512) / 100;
    // rParams (below) is about to change, which is everything
    // sampleRowIndices reads that varies frame to frame; drop any cached
    // row so the next fetchRow call for it recomputes rather than reusing
    // last frame's magnitude.
    g_sampleCacheY = NO_CACHED_ROW;

    // Per-bucket tables. Index 0x7F (outside) is deliberately not touched.
    const int rMax = g_cx / 2;
    for (int r = 0; r <= rMax; r++) {
        const float rf = static_cast<float>(r);
        const int a = static_cast<int>(rf * stepA - phA) & ANG_MASK;
        const int b = static_cast<int>(phB - rf * stepB) & ANG_MASK;
        const int c = static_cast<int>(rf * stepC + phC) & ANG_MASK;
        const int dIdx = pageClock ? static_cast<int>((rf * (9.0f / 120.0f) - t) * K512) & ANG_MASK
                                   : static_cast<int>(rf * (9.0f / 120.0f) * K512 - phD) & ANG_MASK;
        const int d = (sinA[dIdx] * 2) / 5 + D_BIAS; // 0.4 * Q7
        int vb = 255 - (((255 - vigByR[r]) * g_vigStrengthQ8) >> 8);
        if (vb < 0) {
            vb = 0;
        }
        // The page's 0.30 + 0.70 * vignette, in Q8: 76 at the rim, 255 at
        // the centre; times breathe (Q8), stored as Q7.
        const int vf = (765 + 7 * vb) / 10;
        const int vig = (vf * g_breatheQ8) >> 9;
        rParams[r] = static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 9) | (static_cast<uint32_t>(vig) << 18) |
                     (static_cast<uint32_t>(d) << 25);
        rParamsC[r] = static_cast<uint16_t>(c);
    }
}

// This file's per-sample chain up to but not including the palette gather,
// factored out so it can be cached and interpolated on its own
// (sampleRowIndices below) as well as fed straight through to a pixel.
// Returns a magnitude in [0, 253]: rescaleAt is 8-bit and vig 7-bit, so the
// product shifted down by 7 cannot reach 254. It never wraps as a function
// of position, unlike the raw sine indices, which is what makes it safe to
// linearly interpolate.
template <bool Three> inline int mandalaIndex(int oct, int r, int gN_eff, int halfOffset) {
    const int base = halfOffset + oct * gN_eff;
    const uint32_t params = rParams[r];
    const int A = static_cast<int>(params & ANG_MASK);
    const int B = static_cast<int>((params >> 9) & ANG_MASK);
    const int vig = static_cast<int>((params >> 18) & 0x7F);
    const int dB = static_cast<int>(params >> 25);
    int v = sinA[(base + A) & ANG_MASK] + sinB[(2 * base + B) & ANG_MASK] + dB;
    if (Three) {
        v += sinC[(3 * base + rParamsC[r]) & ANG_MASK];
    }
    v = rescaleAt[v];
    return (v * vig) >> 7;
}

template <bool Three> inline uint16_t mandalaPixel(int oct, int r, int gN_eff, int halfOffset) {
    return paletteLUT[mandalaIndex<Three>(oct, r, gN_eff, halfOffset)];
}

// One pixel per map entry, walking the map pointer by Step (+1 for the
// right half, -1 for the mirrored left half).
template <int Step, bool Three>
void mandalaRunSingle(uint16_t *dstPtr, const uint16_t *mapPtr, int count, int gN_eff, int halfOffset) {
    const uint16_t *const end = dstPtr + count;
    while (dstPtr < end) {
        const uint16_t entry = *mapPtr;
        mapPtr += Step;
        const int r = entry & R_MASK;
        const int oct = entry >> R_BITS;
        *dstPtr++ = mandalaPixel<Three>(oct, r, gN_eff, halfOffset);
    }
}

// Same walk, storing the pre-palette magnitude at one entry per iteration:
// the sampling half of the interpolation scheme below.
template <int Step, bool Three>
void mandalaIndexRun(uint8_t *dstPtr, const uint16_t *mapPtr, int count, int gN_eff, int halfOffset) {
    const uint8_t *const end = dstPtr + count;
    while (dstPtr < end) {
        const uint16_t entry = *mapPtr;
        mapPtr += Step;
        const int r = entry & R_MASK;
        const int oct = entry >> R_BITS;
        *dstPtr++ = static_cast<uint8_t>(mandalaIndex<Three>(oct, r, gN_eff, halfOffset));
    }
}

constexpr int MAX_BLOCKS = 256;

// Samples per row: one per output column pair, plus one for the lone last
// column at an odd width.
inline int blockCount(int w) { return (w + 1) / 2; }

// One entry per half-resolution column pair (block), in increasing output
// column order across the whole row: idx[b] is the magnitude sampled at
// output column 2b. The left and right halves are walked with opposite
// map strides exactly as the pixel paths above do, but since both walks
// fill the same array in increasing-column order, idx[b] and idx[b+1] are
// always the correct pair of neighbouring samples for interpolation, even
// at b = nBlocksL-1/nBlocksL where the two halves meet: that seam is two
// physically adjacent output columns either side of the vertical centre
// line, and blending across it is exactly what linear interpolation
// should do there. Same geometry as this file's own pre-redesign bandRef:
// cx/cy come from the w this call was made with (not from g_cx), matching
// that formula exactly. The column geometry is exact when cx is even (480,
// 240, 233); at 466 cx is odd and the sample columns sit one off it, as
// they always have there.
//
// It samples blockCount(w) = (w + 1) / 2 entries. At an odd width (233,
// the 466 px panel at half resolution) the last entry is the sample at
// column w - 1, a block with no right-hand pixel: the writers below give
// it its left pixel only (gm-sveq; before, nBlocks was w / 2 and column
// w - 1 was never written). The right half's count is what is left after
// the left half's, so the walk always fills every entry a writer reads;
// the old (w - cx) / 2 left the last of 233 entries unsampled at 466.
//
// Every caller of this function (fetchRow, always) passes an even absolute
// row, so ay below is always even, and the column walk starts aligned to
// g_cx (even) and steps by two: every entry this function ever reads is
// on the even/even grid polarMapQuarter holds, so it reads that table
// instead of polarMap: same values (copied at init(), never recomputed
// separately), a quarter of the bytes swept per row. mandalaIndexRun's
// stride is one quarter-grid column here (two full-map ax per step, same
// physical spacing as before), not two, since the table itself is already
// only the even columns.
void sampleRowIndices(uint8_t *idxOut, int y, int w) {
    const int cx = w / 2, cy = w / 2;
    const int qDim = g_cx / 2 + 1;
    const int dy = y - cy;
    const int ay = dy < 0 ? -dy : dy;
    const bool sy = dy < 0;
    const uint16_t *mapRow = polarMapQuarter + static_cast<size_t>(ay / 2) * qDim;
    const int gN_eff_left = sy ? g_N : -g_N;
    const int gN_eff_right = sy ? -g_N : g_N;
    const int nBlocksL = cx / 2;
    const int nBlocksR = blockCount(w) - nBlocksL;
    if (g_threeLayers) {
        mandalaIndexRun<-1, true>(idxOut, mapRow + g_cx / 2, nBlocksL, gN_eff_left, g_halfOffL);
        mandalaIndexRun<+1, true>(idxOut + nBlocksL, mapRow, nBlocksR, gN_eff_right, g_halfOffR);
    } else {
        mandalaIndexRun<-1, false>(idxOut, mapRow + g_cx / 2, nBlocksL, gN_eff_left, g_halfOffL);
        mandalaIndexRun<+1, false>(idxOut + nBlocksL, mapRow, nBlocksR, gN_eff_right, g_halfOffR);
    }
}

const uint8_t *fetchRow(int y, int w) {
    if (g_sampleCacheY != y || g_sampleCacheW != w) {
        sampleRowIndices(g_sampleCache, y, w);
        g_sampleCacheY = y;
        g_sampleCacheW = w;
    }
    return g_sampleCache;
}

// Top-left of each block is the real sample; top-right is the horizontal
// midpoint against the next block to the right, clamped to itself at the
// row's last block since there is no further neighbour to blend with.
// Each writer takes the row width w and reads blockCount(w) samples; at an
// odd width the last sample is column w - 1 alone and gets the block's
// left-pixel formula (its right neighbour would be column w).
void writeInterpTop(uint16_t *row, const uint8_t *a, int w) {
    const int nBlocks = blockCount(w);
    const int nPairs = w >> 1;
    for (int b = 0; b < nPairs; b++) {
        const int va = a[b];
        const int vb = (b + 1 < nBlocks) ? a[b + 1] : va;
        row[2 * b] = paletteLUT[va];
        row[2 * b + 1] = paletteLUT[(va + vb) >> 1];
    }
    if (w & 1) {
        row[w - 1] = paletteLUT[a[nPairs]];
    }
}

// Bottom row of the same block: bottom-left is the vertical midpoint
// between this block-row's sample (a) and the next block-row's sample
// (c); bottom-right is the average of the two rows' own horizontal
// midpoints, i.e. the block's centre. a and c both index the same
// increasing-column array as writeInterpTop, so the same edge clamp
// applies to each independently. Used only when a row's pair partner is
// not available in the same call (see bandRef); writeInterpPair below
// covers the ordinary contiguous case without recomputing hMidTop twice.
void writeInterpBottom(uint16_t *row, const uint8_t *a, const uint8_t *c, int w) {
    const int nBlocks = blockCount(w);
    const int nPairs = w >> 1;
    for (int b = 0; b < nPairs; b++) {
        const int va = a[b];
        const int vb = (b + 1 < nBlocks) ? a[b + 1] : va;
        const int vc = c[b];
        const int vd = (b + 1 < nBlocks) ? c[b + 1] : vc;
        const int hMidTop = (va + vb) >> 1;
        const int hMidBot = (vc + vd) >> 1;
        row[2 * b] = paletteLUT[(va + vc) >> 1];
        row[2 * b + 1] = paletteLUT[(hMidTop + hMidBot) >> 1];
    }
    if (w & 1) {
        row[w - 1] = paletteLUT[(a[nPairs] + c[nPairs]) >> 1];
    }
}

// Both rows of one block-pair in a single pass: hMidTop (the top row's own
// horizontal midpoint) is computed once here and reused for the bottom
// row's centre pixel, rather than once in writeInterpTop and again in
// writeInterpBottom. Each store is the pair's own two columns combined
// into one 32-bit write, the same trick the exact per-pixel paths use.
// BgAnim.h allows a two-row call only at an even width, so the odd tail
// below is never reached through band(); it is here so this writer reads
// and writes exactly what the two above would for any w.
void writeInterpPair(uint16_t *rowTop, uint16_t *rowBot, const uint8_t *a, const uint8_t *c, int w) {
    const int nBlocks = blockCount(w);
    const int nPairs = w >> 1;
    for (int b = 0; b < nPairs; b++) {
        const int va = a[b];
        const int vb = (b + 1 < nBlocks) ? a[b + 1] : va;
        const int vc = c[b];
        const int vd = (b + 1 < nBlocks) ? c[b + 1] : vc;
        const int hMidTop = (va + vb) >> 1;
        const int hMidBot = (vc + vd) >> 1;
        const uint16_t pTL = paletteLUT[va];
        const uint16_t pTR = paletteLUT[hMidTop];
        const uint16_t pBL = paletteLUT[(va + vc) >> 1];
        const uint16_t pBR = paletteLUT[(hMidTop + hMidBot) >> 1];
        *reinterpret_cast<uint32_t *>(rowTop + 2 * b) = static_cast<uint32_t>(pTL) | (static_cast<uint32_t>(pTR) << 16);
        *reinterpret_cast<uint32_t *>(rowBot + 2 * b) = static_cast<uint32_t>(pBL) | (static_cast<uint32_t>(pBR) << 16);
    }
    if (w & 1) {
        rowTop[w - 1] = paletteLUT[a[nPairs]];
        rowBot[w - 1] = paletteLUT[(a[nPairs] + c[nPairs]) >> 1];
    }
}

#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// Hand-written kernel for the ordinary contiguous block-pair case
// writeInterpPair above covers in C++. The two are kept bit-for-bit
// equivalent by construction; kb.py's blob-vs-blobref check on the device
// is what actually proves it, since __XTENSA__ only compiles here and
// never on the host: --shapes, the golden diff and the sanitizers all
// still exercise writeInterpPair through bandRef, never this kernel.
//
// C++'s four independent gathers per block (paletteLUT[va],
// paletteLUT[hMidTop], paletteLUT[(va+vc)>>1] and
// paletteLUT[(hMidTop+hMidBot)>>1]) measured 21.49 ms on the rig against
// interpolation's 12 ms target, and the arithmetic-only fusion already in
// writeInterpPair only bought back 6% of that, which says the cost is the
// four loads' latency, not instruction count: GCC's schedule pays a
// load-use stall on effectively every gather because each block's four
// addresses do not become available early enough to overlap the load that
// follows. This kernel interleaves the top-row and bottom-row lanes so
// each load always has independent work from the other lane to run
// underneath it: every l16ui below is followed by at least one instruction
// that does not touch its result, so the four gathers pay zero load-use
// stalls between them, at the cost of a busier register file (all 14
// usable windowed-ABI registers, a2-a15, are live).
//
// aCur/cCur carry the current block's two real samples forward from the
// previous iteration's aNext/cNext rather than reloading them, so the loop
// issues two loads per block (a[b+1], c[b+1]), not four. The trip count
// only covers blocks 0..nBlocks-2: the last block's neighbour clamp (see
// writeInterpPair's own comment) collapses hMidTop to aCur and hMidBot to
// cCur, which in turn makes pTR==pTL and pBR==pBL, so the epilogue after
// the loop is two gathers, not four, and never reads past the sample
// arrays, which is the "handle the clamp outside the loop" split: the
// loop body itself is branch-free. p1 holds nBlocks-1 (the loop trip
// count) only for the loop's own setup instruction; after that it is dead
// and reused as gather scratch, the same register-reuse idiom
// AnimCaustics.cpp's kernel documents for its own span counter.
//
// nBlocks < 2 is not handled here: `loop` does not itself check for a zero
// trip count, and nBlocks-1 == 0 would misread as a huge unsigned count.
// band() below only calls this kernel when nBlocks >= 2 and w is even,
// which is every real two-row call (BgAnim.h allows rows > 1 only at an
// even width; nBlocks is then w/2: 120, 233 or 240). The kernel writes
// nBlocks whole pairs and has no odd tail.
//
// Checked bit-exact against writeInterpPair by
// tools/qemubench/tests/anim_mandala.
__attribute__((noinline)) static void interpPairKernel(uint16_t *rowTop, uint16_t *rowBot, const uint8_t *a,
                                                         const uint8_t *c, const uint16_t *palette, int nBlocks) {
    int aCur = a[0];
    int cCur = c[0];
    const uint8_t *aPtr = a + 1;
    const uint8_t *cPtr = c + 1;
    int aNext, cNext, hMidTop, hMidBot, p0, t;
    int p1 = nBlocks - 1;
    asm volatile("loop %[p1], 2f\n"
                 "l8ui  %[aNext], %[aPtr], 0\n"
                 "l8ui  %[cNext], %[cPtr], 0\n"
                 "addi  %[aPtr], %[aPtr], 1\n"
                 "addi  %[cPtr], %[cPtr], 1\n"
                 "add   %[t], %[aCur], %[aNext]\n"
                 "srli  %[hMidTop], %[t], 1\n"
                 "add   %[t], %[cCur], %[cNext]\n"
                 "srli  %[hMidBot], %[t], 1\n"
                 "addx2 %[t], %[aCur], %[pal]\n"
                 "addx2 %[p1], %[hMidTop], %[pal]\n"
                 "l16ui %[p0], %[t], 0\n"
                 "add   %[t], %[aCur], %[cCur]\n"
                 "srli  %[t], %[t], 1\n"
                 "l16ui %[p1], %[p1], 0\n"
                 "addx2 %[t], %[t], %[pal]\n"
                 "slli  %[p1], %[p1], 16\n"
                 "or    %[p1], %[p1], %[p0]\n"
                 "s32i  %[p1], %[rowTop], 0\n"
                 "l16ui %[p0], %[t], 0\n"
                 "add   %[t], %[hMidTop], %[hMidBot]\n"
                 "srli  %[t], %[t], 1\n"
                 "addx2 %[t], %[t], %[pal]\n"
                 "l16ui %[p1], %[t], 0\n"
                 "addi  %[rowTop], %[rowTop], 4\n"
                 "slli  %[p1], %[p1], 16\n"
                 "or    %[p1], %[p1], %[p0]\n"
                 "s32i  %[p1], %[rowBot], 0\n"
                 "addi  %[rowBot], %[rowBot], 4\n"
                 "or    %[aCur], %[aNext], %[aNext]\n"
                 "or    %[cCur], %[cNext], %[cNext]\n"
                 "2:\n"
                 "addx2 %[t], %[aCur], %[pal]\n"
                 "add   %[p1], %[aCur], %[cCur]\n"
                 "l16ui %[p0], %[t], 0\n"
                 "srli  %[p1], %[p1], 1\n"
                 "addx2 %[p1], %[p1], %[pal]\n"
                 "l16ui %[p1], %[p1], 0\n"
                 "slli  %[t], %[p0], 16\n"
                 "or    %[t], %[t], %[p0]\n"
                 "s32i  %[t], %[rowTop], 0\n"
                 "slli  %[t], %[p1], 16\n"
                 "or    %[t], %[t], %[p1]\n"
                 "s32i  %[t], %[rowBot], 0\n"
                 : [aPtr] "+r"(aPtr), [cPtr] "+r"(cPtr), [aCur] "+r"(aCur), [cCur] "+r"(cCur), [rowTop] "+r"(rowTop),
                   [rowBot] "+r"(rowBot), [p1] "+r"(p1), [aNext] "=&r"(aNext), [cNext] "=&r"(cNext),
                   [hMidTop] "=&r"(hMidTop), [hMidBot] "=&r"(hMidBot), [p0] "=&r"(p0), [t] "=&r"(t)
                 : [pal] "r"(palette)
                 : "memory");
}
#endif

// Every pixel of row y through the full chain, reading polarMap. The
// moire-guard path.
void computeRowFull(uint16_t *row, int y, int w) {
    const int cx = w / 2, cy = w / 2;
    const int mapDim = g_cx + 1;
    const int dy = y - cy;
    const int ay = dy < 0 ? -dy : dy;
    const bool sy = dy < 0;
    const uint16_t *mapRow = polarMap + static_cast<size_t>(ay) * mapDim;
    const int gN_eff_left = sy ? g_N : -g_N;
    const int gN_eff_right = sy ? -g_N : g_N;
    if (g_threeLayers) {
        mandalaRunSingle<-1, true>(row, mapRow + g_cx, cx, gN_eff_left, g_halfOffL);
        mandalaRunSingle<+1, true>(row + cx, mapRow, w - cx, gN_eff_right, g_halfOffR);
    } else {
        mandalaRunSingle<-1, false>(row, mapRow + g_cx, cx, gN_eff_left, g_halfOffL);
        mandalaRunSingle<+1, false>(row + cx, mapRow, w - cx, gN_eff_right, g_halfOffR);
    }
}

constexpr int INNER_BAND = 32;
constexpr int INNER_HALF_W = 32;

// The centre square, redrawn at full resolution over the interpolated row.
void patchCentre(uint16_t *row, int y, int w) {
    const int cx = w / 2, cy = w / 2;
    const int dy = y - cy;
    const int ay = dy < 0 ? -dy : dy;
    if (ay >= INNER_BAND) {
        return;
    }
    const int k = INNER_HALF_W < cx ? INNER_HALF_W : cx;
    const int mapDim = g_cx + 1;
    const bool sy = dy < 0;
    const uint16_t *mapRow = polarMap + static_cast<size_t>(ay) * mapDim;
    const int gN_eff_left = sy ? g_N : -g_N;
    const int gN_eff_right = sy ? -g_N : g_N;
    if (g_threeLayers) {
        mandalaRunSingle<-1, true>(row + cx - k, mapRow + (g_cx - cx + k), k, gN_eff_left, g_halfOffL);
        mandalaRunSingle<+1, true>(row + cx, mapRow, k, gN_eff_right, g_halfOffR);
    } else {
        mandalaRunSingle<-1, false>(row + cx - k, mapRow + (g_cx - cx + k), k, gN_eff_left, g_halfOffL);
        mandalaRunSingle<+1, false>(row + cx, mapRow, k, gN_eff_right, g_halfOffR);
    }
}

// A row's pixels depend on its absolute y and the frame state only: a
// block pair is rendered from rows y and y+2 whether or not both of its
// rows are in this call, and a lone odd row is the bottom half of the pair
// above it.
void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const int nBlocks = blockCount(w);
    if (g_fineDetail) {
        for (int ry = 0; ry < rows; ry++) {
            computeRowFull(dst + static_cast<size_t>(ry) * w, y0 + ry, w);
        }
        return;
    }
    int ry = 0;
    while (ry < rows) {
        const int y = y0 + ry;
        uint16_t *row = dst + static_cast<size_t>(ry) * w;
        if ((y & 1) == 0 && ry + 1 < rows) {
            uint8_t top[MAX_BLOCKS];
            memcpy(top, fetchRow(y, w), static_cast<size_t>(nBlocks));
            const uint8_t *bottom = fetchRow(y + 2, w);
            writeInterpPair(row, row + w, top, bottom, w);
            patchCentre(row, y, w);
            patchCentre(row + w, y + 1, w);
            ry += 2;
            continue;
        }
        if (y & 1) {
            uint8_t top[MAX_BLOCKS];
            memcpy(top, fetchRow(y - 1, w), static_cast<size_t>(nBlocks));
            const uint8_t *bottom = fetchRow(y + 1, w);
            writeInterpBottom(row, top, bottom, w);
        } else {
            writeInterpTop(row, fetchRow(y, w), w);
        }
        patchCentre(row, y, w);
        ry++;
    }
}

// g_fineDetail (set once per frame in frame()) bypasses every saving in
// this file and renders exactly like the exact per-pixel design, for the
// reason WRAP_GUARD's comment gives; this only fires at high complexity
// settings, so it costs nothing at the animation's defaults.
//
// Below fine-detail, band() mirrors bandRef's own row classification
// (the ordinary contiguous pair, then the solo top/bottom fallback for a
// call shape that splits a pair, each followed by the center patch) so a
// row's cost can differ by call shape but never its pixels: interlace_check
// exercises exactly that, and bandRef stays the one place this logic is
// written, checked by eye against this copy. The only line that changes is
// the ordinary pair's write: interpPairKernel replaces writeInterpPair with
// the hand-asm kernel described on its own comment. nBlocks < 2 and an odd
// w are defensive only, never taken on device (a pair call is always at an
// even width, nBlocks 120, 233 or 240): the kernel's `loop` needs a trip
// count of nBlocks-1 and does not itself check for zero, and it has no odd
// tail, so both cases fall back to the C++ path instead, the same shape
// AnimCaustics.cpp's own precondition check takes.
// Non-Xtensa builds (host tests, sanitizers) and any build with
// GM_BGANIM_NO_ASM route straight to bandRef instead, which contains the
// identical loop.
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const int nBlocks = blockCount(w);
    if (g_fineDetail) {
        for (int ry = 0; ry < rows; ry++) {
            computeRowFull(dst + static_cast<size_t>(ry) * w, y0 + ry, w);
        }
        return;
    }
    int ry = 0;
    while (ry < rows) {
        const int y = y0 + ry;
        uint16_t *row = dst + static_cast<size_t>(ry) * w;
        if ((y & 1) == 0 && ry + 1 < rows) {
            uint8_t top[MAX_BLOCKS];
            memcpy(top, fetchRow(y, w), static_cast<size_t>(nBlocks));
            const uint8_t *bottom = fetchRow(y + 2, w);
            if (nBlocks < 2 || (w & 1)) {
                writeInterpPair(row, row + w, top, bottom, w);
            } else {
                interpPairKernel(row, row + w, top, bottom, paletteLUT, nBlocks);
            }
            patchCentre(row, y, w);
            patchCentre(row + w, y + 1, w);
            ry += 2;
            continue;
        }
        if (y & 1) {
            uint8_t top[MAX_BLOCKS];
            memcpy(top, fetchRow(y - 1, w), static_cast<size_t>(nBlocks));
            const uint8_t *bottom = fetchRow(y + 1, w);
            writeInterpBottom(row, top, bottom, w);
        } else {
            writeInterpTop(row, fetchRow(y, w), w);
        }
        patchCentre(row, y, w);
        ry++;
    }
}
#else

void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) { bandRef(dst, y0, rows, w, tMs, p); }

#endif // __XTENSA__ && !GM_BGANIM_NO_ASM

void release() {
    releaseTable(sinA, ANG_N * sizeof(int16_t));
    releaseTable(sinB, ANG_N * sizeof(int16_t));
    releaseTable(sinC, ANG_N * sizeof(int16_t));
    releaseTable(recipLUT, 241 * sizeof(uint32_t));
    releaseTable(vigByR, static_cast<size_t>(R_ENTRIES));
    releaseTable(rescaleAlloc, RESCALE_N);
    rescaleLUT = nullptr;
    rescaleAt = nullptr;
    releaseTable(paletteLUT, 256 * sizeof(uint16_t));
    if (polarMap != nullptr) {
        heap_caps_free(polarMap);
        polarMap = nullptr;
    }
    if (polarMapQuarter != nullptr) {
        heap_caps_free(polarMapQuarter);
        polarMapQuarter = nullptr;
    }
    releaseTable(rParams, R_ENTRIES * sizeof(uint32_t));
    releaseTable(rParamsC, R_ENTRIES * sizeof(uint16_t));
    releaseTable(g_sampleCache, static_cast<size_t>(g_cx) + 1);
    // The sentinels. tablesBuilt gates the one-time fills, so leaving it
    // set would hand back reallocated tables that nothing ever writes;
    // lastThemeGen and lastContrastP force the palette and the rescale
    // table to be rebuilt into the new allocations; g_sampleCacheY must not
    // survive into the next init() either, since a stale row number could
    // false-hit fetchRow's cache against a freshly reallocated (and
    // unfilled) g_sampleCache.
    tablesBuilt = false;
    lastThemeGen = 0xFFFFFFFF;
    lastContrastP = -1;
    lastThreeLayers = false;
    g_sampleCacheY = NO_CACHED_ROW;
}

} // namespace

extern const BgAnimation bg_anim_mandala;
const BgAnimation bg_anim_mandala = {
    "mandala",
    "Mandala",
    {{"speed", "Speed", 50},
     {"symmetry", "Symmetry", 50},
     {"complexity", "Complexity", 45},
     {"drift", "Ring drift", 50},
     {"vignette", "Vignette", 50},
     {"breathe", "Breathe", 50},
     {"contrast", "Contrast", 50},
     {"rings", "Ring pitch", 50}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
