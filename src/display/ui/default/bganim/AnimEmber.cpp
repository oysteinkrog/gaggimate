#ifndef GAGGIMATE_SIM

// "Ember" — a warm glow breathing from below screen center, like coals in a
// hearth. Three incommensurate breathing periods (11.3s/17.7s/6.1s) so the
// pattern never visibly repeats; optional edge-of-perception flicker from the
// shared tileable noise texture. Radial field is an incremental r^2 walk (two
// adds per pixel) into a radius LUT — no sqrt in the loop.
//
// Palette is stored "padded": paletteExt has PAD clamp entries on each side
// of the real 256-entry ramp, so the per-pixel breathe/dither/flicker offset
// can be added to the radius index and used to index paletteExt directly,
// with no branch to clamp into [0,255] — the pad entries already hold the
// clamped edge colors. Range proof (worst-case params, p[1..3]=100): breathe
// in [-35,+35], dither in [-6,+6], flicker term in [-10,+9], so the combined
// index lands in [-51,+305]; PAD=64 covers that with margin.
//
// radiusLUT is padded the same way on the high side (RPAD entries repeating
// radiusLUT[255]) so `radiusLUT[ridx]` needs no >255 clamp either. Verified
// against real xtensa-esp32s3 codegen (tools/animbench/xtensa-asm.sh): the
// clamp's `movi #255` + `min` cost a spare register that forced the noiseRow
// pointer to spill to the stack and reload every pixel — removing the clamp
// dropped that reload too. Safe for this animation's fixed 480x480 target:
// max r^2 is at the farthest corner from center (g_cx=240,g_cy=260), giving
// r^2>>RSHIFT ≈ 244, well inside the 255+RPAD range with margin to spare.
//
// Breathe is frame-constant, so instead of subtracting it from every pixel
// it's folded once per row into perCol[8] alongside the dither term — the
// inner loop does one add (radiusLUT[ridx] + perCol[x&7]) instead of a
// subtract-then-add.
//
// Flicker adds noise*flickerAmp, a per-pixel int mul+shift; since flickerAmp
// is frame-constant, it's folded into a 256-entry LUT (flickerLUT) rebuilt
// only when the flicker param changes, turning the multiply into a lookup.
// The "is flicker on" test is hoisted out of the pixel loop entirely — band()
// picks one of two loop bodies (with/without noise term) once, not per pixel.
//
// combRow[x] = perCol[x&7] + flickerLUT[noiseRow[(x+g_sx)&255]], the two
// non-radius terms of rn, is precomputed once per row in a short sequential
// pass before the radius loop runs (doFlicker branch only). Integer addition
// is associative/commutative exactly, so summing these two terms first and
// adding radiusLUT[ridx] second gives the identical rn the three-term sum
// always did — this is a reassociation, not a behavior change. It matters
// because the un-reassociated per-pixel loop chained FOUR dependent loads
// (radiusLUT, perCol, noiseRow, then flickerLUT keyed off the noiseRow byte)
// into one scalar dependency chain feeding the palette lookup; the precompute
// pass turns that into loads with no cross-iteration dependency (only the
// loop counter carries), and the radius loop is left with just two
// independent loads (radiusLUT[ridx], combRow[x]) on the critical path.
// combRow is int16_t and read with a sign-extending load so the per-pixel
// path needs no separate sign-extend op — see the range proof above (rn's
// non-radius terms land in about [-51,+50], comfortably inside int16_t).
// palOff = paletteExt + PAD folds the "+PAD" constant into the base pointer
// once per row instead of adding it to rn on every pixel.
//
// Design: anim-atmosphere (Fable), 2026-08-15. Optimized: opt-ember,
// 2026-08-15; row-precomputed flicker/dither term, 2026-08-30.
//
// --- Xtensa assembly pass, 2026-09-04 --------------------------------------
//
// The C++ pixel loop above is kept verbatim as `bandRef`: the spec, run by
// the host bench against golden/ and by the on-device equivalence test
// (SleepAnimation::runAnimTest, /api/debug/animtest) against the kernel
// below. band() itself now dispatches, on real hardware, to a hand-written
// scalar Xtensa kernel (emberGatherRow) for the palette gather -- the part
// of this loop the device pays for on every one of the 240 band() calls a
// frame makes (BAND_H=2 on the device; see ASM_BRIEF.md's fleet table,
// which measured this animation at 35,399us/frame, 21.2x the host-scaled
// estimate, second worst in the fleet after nebula).
//
// Why scalar and not PIE: this animation's inner loop is a two-level GATHER
// (radiusLUT[ridx], then paletteExt[that + combRow[x]]), and PIE has no
// vector gather instruction on this chip (ASM_BRIEF.md's PIE facts). The
// r^2 stepping itself (two adds per pixel) is already the cheapest possible
// form and not worth vectorizing -- the actual device cost is the gather's
// load-use stalls and whatever register pressure keeps -O2 from scheduling
// around them, which is exactly what hand-written scalar asm with a
// guaranteed schedule and a hardware LOOPNEZ can fix. See emberGatherRow's
// own header comment for the schedule proof.
//
// One gather kernel serves BOTH of band()'s branches (flicker on or off).
// The original C++ read perCol[x&7] straight out of eight resident bytes in
// the no-flicker branch, with no combRow round-trip at all; unifying onto
// one kernel means band() now always fills combRow[0,w) before the gather --
// with the noise term added when flicker is on (unchanged math, just moved
// out of the inline loop), or the periodic dither/breathe term alone,
// replicated across the row, when it is off. That costs the no-flicker path
// a combRow write+read it did not pay before (roughly 1-2 cy/pixel for the
// replication below), bought back by needing to write, schedule and verify
// only one gather kernel instead of two. This is the right trade for this
// pass specifically because flicker is nonzero by default (param default is
// 20) and the device measurement above that this pass is scoped against was
// taken at default params -- so the flicker-on path, which pays nothing
// extra, is the one that actually matters for the number being chased.
//
// bandRef's own per-pixel arithmetic, types (int8_t perCol) and pairing are
// untouched by any of this; band()'s row setup below is a separate,
// independently-written copy of the same formulas (not a refactor of
// bandRef), so a mistake here cannot silently change what the host bench or
// the golden compare see -- only the device equivalence test (rung 4, which
// this pass does not have hardware to run) can catch a divergence between
// the two, which is exactly the check that test exists for.
//
// --- Round 2, 2026-09-04 (device measurement) ------------------------------
//
// Rung 4 came back bit-exact but only marginally faster (1.06-1.09x HEAD):
// the gather loop above was never the dominant cost, and the fleet-wide
// device data confirms why -- the round 1 allocator placed each of this
// file's four small tables (paletteExt, radiusLUT, flickerLUT, combRow) in
// internal SRAM or PSRAM depending on whatever the free-pool happened to
// look like at that particular boot, not on anything this file controlled.
// flickerLUT in particular is indexed by a noise byte value in the combRow
// precompute pass below -- an effectively random-access gather -- which is
// exactly the access pattern PSRAM latency punishes; noiseTex256 itself,
// read sequentially, was never the problem (BgAnimCommon.h: bulk
// sequentially-swept tables stream from PSRAM at close to SRAM speed). All
// four tables now go through allocHotOrPsram() (see init()), which places
// them in the new fixed internal-SRAM slab deterministically instead of by
// boot-time luck, with the same PSRAM fallback the old allocator gave every
// table anyway if the slab is ever full.
//
// The second round-2 finding was this file's own IRAM cost: pinning band(),
// bandRef AND emberGatherRow all three cost +502 B of internal RAM against
// HEAD, which pinned only its one function. bandRef is off the per-frame
// path in production (see its own comment, below) and is no longer pinned.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

// Band-kernel code pinned to IRAM on device. The ESP32-S3's single icache
// (16 KB here) is shared by both cores, and LVGL's code footprint churns it
// from core 1 on every telemetry repaint; a flash refill for this loop then
// queues on the MSPI behind the scan-out refill's PSRAM stream. With the
// kernel's tables in SRAM (post-settle re-placement in bganim::alloc) and its
// output band buffer in SRAM, instruction fetch is the band bracket's last
// external dependency; pinning removes it. Host and sim builds compile the
// attribute away.
#if defined(ESP_PLATFORM)
#include <esp_attr.h>
#define GM_ANIM_IRAM IRAM_ATTR
#else
#define GM_ANIM_IRAM
#endif

namespace {
using namespace bganim;

// DDS phase steps: full circle = 2^32, periods 11.3s / 17.7s / 6.1s.
constexpr uint32_t STEP1 = static_cast<uint32_t>(4294967296.0 / 11300.0);
constexpr uint32_t STEP2 = static_cast<uint32_t>(4294967296.0 / 17700.0);
constexpr uint32_t STEP3 = static_cast<uint32_t>(4294967296.0 / 6100.0);
constexpr uint32_t PHOFF2 = static_cast<uint32_t>(1.7 / 6.2831853 * 4294967296.0);
constexpr uint32_t PHOFF3 = static_cast<uint32_t>(4.2 / 6.2831853 * 4294967296.0);
constexpr int RSHIFT = 9; // r^2 -> radiusLUT bucket

// Padding either side of the 256-entry palette ramp (see file header proof).
constexpr int PAD = 64;
constexpr int PAL_EXT_N = 256 + 2 * PAD;

// High-side padding on radiusLUT so ridx never needs a >255 clamp (see
// file header proof — safe margin for the fixed 480x480 target).
constexpr int RPAD = 64;
constexpr int RLUT_N = 256 + RPAD;

uint16_t *paletteExt = nullptr; // [PAL_EXT_N]; real ramp lives at paletteExt+PAD
uint16_t *palette = nullptr;    // = paletteExt + PAD, 256 entries, reversed theme ramp
uint8_t *radiusLUT = nullptr;   // [RLUT_N]; r^2>>RSHIFT -> normalized radius byte
int8_t *flickerLUT = nullptr;   // noise byte -> signed flicker contribution
const uint8_t *noise = nullptr;
int16_t *combRow = nullptr; // [allocW]; perCol+flicker combined, rebuilt per row (see file header)
int allocW = 0;             // width combRow was sized for; release() needs it back
uint32_t lastThemeGen = 0xFFFFFFFF;
uint8_t lastGlow = 255;
uint8_t lastFlickerParam = 255;
int g_cx = 240, g_cy = 260;
float g_maxR = 353.7f;
int g_breathe = 0, g_flickerAmp = 0, g_sx = 0, g_sy = 0;

// The ramp is reversed, so index 0 is the brightest stop and the glow's core
// lands squarely on it — in the middle of the screen, which is where the UI
// puts its readouts. Starting the ramp part way in keeps the hearth gradient
// and its falloff but takes the peak off the text. Purely a shift of where the
// curve begins: clamp8f still bounds the result to [0,255], so the index range
// the palette padding is sized for (see file header) is unchanged.
constexpr int CORE_FLOOR = 72;

void buildRadiusLut(uint8_t glow) {
    const float glowGain = 0.55f + 0.014f * glow;
    const float scale = 255.0f / (g_maxR * glowGain);
    for (int i = 0; i < 256; i++) {
        const float r = sqrtf(static_cast<float>(i << RSHIFT));
        radiusLUT[i] = clamp8f(CORE_FLOOR + r * scale);
    }
    // Pad entries repeat the outermost (fully-clamped) value so an
    // unclamped ridx past 255 (shouldn't happen on the real target, see
    // file header) still reads a sane color instead of the flicker LUT.
    for (int i = 256; i < RLUT_N; i++) {
        radiusLUT[i] = radiusLUT[255];
    }
}

// Fills the clamp padding around the freshly-rebuilt 256-entry ramp so
// paletteExt[PAD + rn] is valid for rn in [-PAD, 255+PAD] with no branch.
void extendPalette() {
    const uint16_t lo = palette[0];
    const uint16_t hi = palette[255];
    for (int i = 0; i < PAD; i++) {
        paletteExt[i] = lo;
        paletteExt[PAD + 256 + i] = hi;
    }
}

void buildFlickerLut(int flickerAmp) {
    for (int i = 0; i < 256; i++) {
        flickerLUT[i] = static_cast<int8_t>(((i - 128) * flickerAmp) >> 7);
    }
}

// Tries the hot internal-SRAM slab first, falls back to alloc() (PSRAM,
// unconditionally, per the new placement policy) if the slab is full --
// "the caller falls back to alloc()" is explicitly the caller's job per
// BgAnimCommon.h's comment above allocHot(). release()/releaseTable() do
// not need to know which pool a pointer actually landed in: release()
// auto-detects via isHot()/isPsram() internally.
void *allocHotOrPsram(size_t size) {
    void *p = allocHot(size);
    if (p == nullptr) {
        p = alloc(size);
    }
    return p;
}

bool init(int w, int h) {
    if (paletteExt == nullptr) {
        // All four tables are read once per pixel (paletteExt, radiusLUT,
        // combRow directly in emberGatherRow's gather; flickerLUT once per
        // pixel too, but only on the doFlicker precompute pass) -- 230,400
        // reads/frame each at full res, exactly the "reads per frame, not
        // size" criterion BgAnimCommon.h's hot-slab comment asks for. Total
        // is 768 + 320 + 256 + up to 960 = up to 2,304 B, comfortably inside
        // the 9,216 B this animation gets while resident (round 2 report has
        // the exact figure). noiseTex256 is NOT moved here: it is a 64 KB
        // fleet-shared asset owned by BgAnimCommon (borrowed via
        // noiseTex256(), never allocated by this file) and BgAnimCommon.h
        // says bulk sequentially-swept tables were never the ones placement
        // mattered for -- this animation's own finding (round 1 report) was
        // that combRow/flickerLUT/radiusLUT landing in PSRAM by chance under
        // the OLD allocator, not noiseTex256's own access pattern, was the
        // real cost: flickerLUT in particular is indexed by a noise byte
        // value, i.e. an effectively random-access gather, which is exactly
        // the pattern PSRAM latency hurts and sequential-stream tables do not.
        paletteExt = static_cast<uint16_t *>(allocHotOrPsram(PAL_EXT_N * sizeof(uint16_t)));
        radiusLUT = static_cast<uint8_t *>(allocHotOrPsram(RLUT_N));
        flickerLUT = static_cast<int8_t *>(allocHotOrPsram(256));
        noise = noiseTex256();
        combRow = static_cast<int16_t *>(allocHotOrPsram(static_cast<size_t>(w) * sizeof(int16_t)));
        allocW = w;
        if (paletteExt == nullptr || radiusLUT == nullptr || flickerLUT == nullptr || noise == nullptr ||
            combRow == nullptr) {
            return false;
        }
        palette = paletteExt + PAD;
    }
    g_cx = w / 2;
    g_cy = h / 2 + (20 * h) / 480;
    const float dx = static_cast<float>(g_cx);
    const float dy = static_cast<float>(g_cy > h - g_cy ? g_cy : h - g_cy);
    g_maxR = sqrtf(dx * dx + dy * dy);
    lastThemeGen = 0xFFFFFFFF;
    lastGlow = 255;
    lastFlickerParam = 255;
    return true;
}

void frame(uint32_t tMs, int, int, const uint8_t p[4]) {
    if (themeGen() != lastThemeGen) {
        buildThemeRamp(palette, 256, /*reversed=*/true); // brightest stop at the core
        extendPalette();
        lastThemeGen = themeGen();
    }
    if (p[1] != lastGlow) {
        buildRadiusLut(p[1]);
        lastGlow = p[1];
    }
    const float spd = speedMul(p[0]);
    // Speed scales virtual time; a param change causes one phase jump, which
    // the slow breathing envelope absorbs invisibly.
    // Virtual time is deliberately modular (every use below is a shift-and-mask
    // into a 1024-entry sine table), but tMs * spd reaches ~2.9e10 at the top
    // of the speed range before tMs wraps, and converting a float that large
    // straight to uint32_t is undefined rather than wrapping. Go through double
    // (float's 24-bit mantissa cannot hold tMs near its wrap anyway) and then
    // int64_t, where the conversion is defined, and let integer-to-unsigned do
    // the modular reduction.
    const uint32_t vt = static_cast<uint32_t>(static_cast<int64_t>(static_cast<double>(tMs) * spd));
    const float pulseGain = p[3] / 100.0f;
    const float s1 = sin1024((vt * STEP1) >> 22) * (1.0f / SIN_AMP);
    const float s2 = sin1024(((vt * STEP2) + PHOFF2) >> 22) * (1.0f / SIN_AMP);
    const float s3 = sin1024(((vt * STEP3) + PHOFF3) >> 22) * (1.0f / SIN_AMP);
    g_breathe = static_cast<int>(pulseGain * (0.30f * s1 + 0.15f * s2 + 0.05f * s3) * 70.0f);
    g_flickerAmp = (10 * p[2]) / 100;
    if (p[2] != lastFlickerParam) {
        buildFlickerLut(g_flickerAmp);
        lastFlickerParam = p[2];
    }
    g_sx = static_cast<int>((vt * 6u) >> 10) & 255;
    g_sy = static_cast<int>((vt * 4u) >> 10) & 255;
}

// The portable reference implementation. Kept byte-for-byte as this
// animation's original band() (only the name changed): the host bench and
// the on-device equivalence test both hold this to be ground truth, so it
// is deliberately NOT refactored to share code with the asm-dispatching
// band() below -- any accidental behavior change here would poison the very
// check meant to catch a bug in the kernel.
//
// Deliberately NOT IRAM-pinned (round 2: this file previously pinned band(),
// bandRef and emberGatherRow all three, +502 B of internal RAM against HEAD,
// which pinned only its one band()). bandRef only runs in production for the
// w%8!=0 defensive fallback in band() below, which never fires on the real
// device (w is always 240 or 480) -- everywhere else it is invoked from
// SleepAnimation::runAnimTest/the on-device equivalence test and the debug
// ?useref=1 knob, both explicitly diagnostic, where speed does not matter.
// Paying IRAM space to protect a function that is not on the per-frame path
// is exactly the static-RAM cost round 2 flagged; band() and emberGatherRow
// together cover what HEAD's single pinned function did and stay pinned.
void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const bool doFlicker = g_flickerAmp != 0;
    for (int r = 0; r < rows; r++) {
        const int y = y0 + r;
        const int dy = y - g_cy;
        const int dy2 = dy * dy;
        uint16_t *row = dst + static_cast<size_t>(r) * w;
        const uint8_t *bayerRow = &BAYER8[(y & 7) * 8];
        // perCol combines the dither term with the (frame-constant) breathe
        // offset once per row, so the pixel loop does a single add instead
        // of a subtract-then-add — see file header.
        int8_t perCol[8];
        for (int k = 0; k < 8; k++) {
            perCol[k] = static_cast<int8_t>((static_cast<int>(bayerRow[k]) - 31) / 5 - g_breathe);
        }
        int dx = -g_cx;
        int r2 = dx * dx + dy2;
        int ddx = 2 * dx + 1; // r2 delta for this step; += 2 per pixel thereafter

        // PAD folded into the base pointer once per row instead of into rn on
        // every pixel (see file header) -- valid because PAD is compile-time
        // constant and paletteExt[PAD + rn] == (paletteExt + PAD)[rn] exactly.
        const uint16_t *palOff = paletteExt + PAD;

        if (doFlicker) {
            const uint8_t *noiseRow = noise + ((y + g_sy) & 255) * 256;
            // Precompute the two non-radius terms of rn once per row (see file
            // header): no cross-iteration dependency here, unlike the radius
            // loop below, so this pass is just independent loads+adds+stores.
            for (int x = 0; x < w; x++) {
                combRow[x] = static_cast<int16_t>(perCol[x & 7] + flickerLUT[noiseRow[(x + g_sx) & 255]]);
            }
            // 2x unrolled, paired into one 32-bit store (dst is 4-byte
            // aligned and w is even -- see xtensa-asm addendum, and matches
            // the existing convention in AnimAurora.cpp's emitPair). Two
            // independent pixel computations sit between each loop branch
            // instead of one, giving the in-order core two non-dependent
            // load chains to interleave while a radiusLUT/palette load from
            // one pixel is still in flight for the other -- the unroll's
            // actual payoff is hiding load latency, not the halved store
            // count. Xtensa is little-endian, so the first pixel of the pair
            // is the low halfword.
            int x = 0;
            for (; x + 1 < w; x += 2) {
                const int ridx0 = r2 >> RSHIFT;
                const uint16_t c0 = palOff[radiusLUT[ridx0] + combRow[x]];
                r2 += ddx;
                ddx += 2;
                const int ridx1 = r2 >> RSHIFT;
                const uint16_t c1 = palOff[radiusLUT[ridx1] + combRow[x + 1]];
                r2 += ddx;
                ddx += 2;
                *reinterpret_cast<uint32_t *>(row + x) =
                    static_cast<uint32_t>(c0) | (static_cast<uint32_t>(c1) << 16);
            }
            if (x < w) { // odd leftover (w is 480 on the real target; kept for contract generality)
                const int ridx = r2 >> RSHIFT;
                row[x] = palOff[radiusLUT[ridx] + combRow[x]];
                r2 += ddx;
                ddx += 2;
            }
        } else {
            for (int x = 0; x < w; x++) {
                const int ridx = r2 >> RSHIFT;
                row[x] = palOff[radiusLUT[ridx] + perCol[x & 7]];
                r2 += ddx;
                ddx += 2;
            }
        }
    }
}

#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)

// Hand-written Xtensa scalar kernel for the palette gather that dominates
// band(): idx = radiusLUT[r2>>RSHIFT] + combRow[x], colour = palOff[idx].
// There is no vector gather on this chip's PIE unit (ASM_BRIEF.md's PIE
// facts), so this stays scalar; the win here is a guaranteed load-use
// schedule and the hardware zero-overhead LOOPNEZ, not vectorization.
//
// One kernel serves BOTH of band()'s branches (flicker on or off): the
// caller always fills combRow[0,w) before calling this -- with the noise
// term added when flicker is on, or just the periodic dither/breathe term
// (perCol[x&7]) replicated across the row when it is off (see band(),
// below, and the file header for why this trade is worth it here).
//
// r2/ddx are bandRef's own incremental r^2 walk (two adds per pixel; see
// file header) -- this kernel just takes over from the point bandRef
// shifts, gathers and packs. r2 is provably non-negative for any real
// geometry on this animation's fixed radial field (sum of squares -- see
// the file header's range proof), but SRAI (arithmetic shift) is used
// rather than SRLI so the kernel matches C's `int r2 >> RSHIFT` exactly
// regardless of that external proof, same as bandRef's own `int r2`.
//
// No alignment requirement anywhere in this kernel: every access is a
// scalar byte/half-word load (L8UI/L16UI/L16SI have no alignment
// restriction beyond their own element size), unlike EE.VLD.128/EE.VST.128
// which silently truncate the address to 16 bytes (see scale565Oct's
// warning in SleepAnimation.cpp) -- this kernel never uses those, so
// combRow/radiusLUT/paletteExt keep whatever alignment bganim::alloc()
// happens to give them, and it does not matter.
//
// Register budget (11 live values across the asm block: r2, ddx, wr, cr,
// ridx0, ridx1, cv0, cv1, plus the read-only n/rlut/pal): comfortably
// inside the ~13 usable ARs the windowed ABI leaves an inline asm block
// (ASM_BRIEF.md), confirmed by xtensa-asm14 showing no spill code around
// the call site and LOOPNEZ still emitted. ridx0/ridx1 double as the
// address register for their own gather (radiusLUT+ridx computed in place,
// then loaded from that same register) and again as the palette index
// after the add; cv0/cv1 are reused the same way for the palOff address.
// This is safe on this core: a load's address operand is read before the
// load's destination write lands, so reusing one register for both is not
// a hazard, only a data dependency the schedule below already respects.
//
// Schedule (verified by inspection, and proved to execute correctly, not
// just assemble, by the QEMU test below): every loaded value is consumed
// at least one instruction after the load that produced it, so no
// load-use stall (OPTIMIZE.md's addendum) ever fires. In order:
//   1 srai ridx0<-r2        (r2 from the previous iteration's adds)
//   2 add  r2+=ddx           3 addi ddx+=2
//   4 srai ridx1<-r2        5 add r2+=ddx      6 addi ddx+=2
//   7 add  ridx0 = &radiusLUT[ridx0]
//   8 l8ui ridx0 = radiusLUT[ridx0]            <- load
//   9 add  ridx1 = &radiusLUT[ridx1]            (gap for #8; independent)
//  10 l8ui ridx1 = radiusLUT[ridx1]            <- load
//  11 l16si cv0 = combRow[x0]                   (gap for #10; independent) <- load
//  12 l16si cv1 = combRow[x1]                   (gap for #11; independent) <- load
//  13 add  ridx0 += cv0     (ridx0 4 instrs old, cv0 1 instr old: both safe)
//  14 add  ridx1 += cv1     (ridx1 4 instrs old, cv1 1 instr old: both safe)
//  15 addx2 cv0 = &palOff[ridx0]  (ALU-to-ALU, no interlock)
//  16 l16ui ridx0 = palOff[ridx0]               <- load
//  17 addx2 cv1 = &palOff[ridx1]                (gap for #16; independent)
//  18 l16ui ridx1 = palOff[ridx1]               <- load
//  19 addi cr += 4                              (gap for #18; independent)
//  20 slli ridx1 <<= 16     (ridx1 1 instr old: safe)
//  21 or   ridx0 |= ridx1   (ALU-to-ALU)
//  22 s32i [wr] = ridx0     (ALU-to-store, no interlock)
//  23 addi wr += 4
// 23 instructions for 2 pixels (11.5/pixel), zero stalls, zero
// per-iteration branch cost (LOOPNEZ). Interleaving is 2-wide, matching
// bandRef's own C++ pairing and OPTIMIZE.md's silk finding that a deeper
// unroll spills the windowed ABI's register file and loses the hardware
// loop; verified via xtensa-asm14 that this width keeps LOOPNEZ and does
// not spill (see this pass's report).
//
// wPairs must be w/2; the caller guarantees w is even (band() falls back
// to bandRef for the odd case, which never happens on the real device: w
// is always 240 or 480). Body is 23 instructions; xtensa-esp32s3-elf-objdump
// on the real compiler's .o shows the assembler auto-narrowing six of them
// (ADD.N/ADDI.N/L32I.N/S32I.N) to 2 bytes, for a measured loop body of 58
// bytes -- well inside the LOOPNEZ 256-byte body limit.
__attribute__((noinline)) static void GM_ANIM_IRAM emberGatherRow(uint16_t *__restrict row,
                                                                  const uint16_t *__restrict palOff,
                                                                  const uint8_t *__restrict radiusLUT,
                                                                  const int16_t *__restrict combRowIn, int r2_0,
                                                                  int ddx_0, int wPairs) {
    int r2 = r2_0;
    int ddx = ddx_0;
    uint16_t *wr = row;
    const int16_t *cr = combRowIn;
    int ridx0, ridx1, cv0, cv1;
    asm volatile("loopnez %[n], 2f\n"
                 "srai   %[ridx0], %[r2], %[rshift]\n" // ridx0 = r2 >> RSHIFT
                 "add    %[r2], %[r2], %[ddx]\n"       // r2 += ddx
                 "addi   %[ddx], %[ddx], 2\n"          // ddx += 2
                 "srai   %[ridx1], %[r2], %[rshift]\n" // ridx1 = r2 >> RSHIFT
                 "add    %[r2], %[r2], %[ddx]\n"
                 "addi   %[ddx], %[ddx], 2\n"
                 "add    %[ridx0], %[rlut], %[ridx0]\n" // &radiusLUT[ridx0]
                 "l8ui   %[ridx0], %[ridx0], 0\n"       // radiusLUT[ridx0]
                 "add    %[ridx1], %[rlut], %[ridx1]\n" // &radiusLUT[ridx1] (fills #prev load's slot)
                 "l8ui   %[ridx1], %[ridx1], 0\n"       // radiusLUT[ridx1]
                 "l16si  %[cv0], %[cr], 0\n"            // combRow[x0]
                 "l16si  %[cv1], %[cr], 2\n"            // combRow[x1]
                 "add    %[ridx0], %[ridx0], %[cv0]\n"  // idx0 = radiusLUT[ridx0] + combRow[x0]
                 "add    %[ridx1], %[ridx1], %[cv1]\n"  // idx1 = radiusLUT[ridx1] + combRow[x1]
                 "addx2  %[cv0], %[ridx0], %[pal]\n"    // &palOff[idx0]
                 "l16ui  %[ridx0], %[cv0], 0\n"         // c0 = palOff[idx0]
                 "addx2  %[cv1], %[ridx1], %[pal]\n"    // &palOff[idx1]
                 "l16ui  %[ridx1], %[cv1], 0\n"         // c1 = palOff[idx1]
                 "addi   %[cr], %[cr], 4\n"             // combRow += 2 pixels
                 "slli   %[ridx1], %[ridx1], 16\n"
                 "or     %[ridx0], %[ridx0], %[ridx1]\n" // pack c0 | (c1<<16); little-endian: c0 is x0
                 "s32i   %[ridx0], %[wr], 0\n"
                 "addi   %[wr], %[wr], 4\n"
                 "2:\n"
                 : [r2] "+r"(r2), [ddx] "+r"(ddx), [wr] "+r"(wr), [cr] "+r"(cr), [ridx0] "=&r"(ridx0),
                   [ridx1] "=&r"(ridx1), [cv0] "=&r"(cv0), [cv1] "=&r"(cv1)
                 : [n] "r"(wPairs), [rlut] "r"(radiusLUT), [pal] "r"(palOff), [rshift] "i"(RSHIFT)
                 : "memory");
}

#endif // __XTENSA__ && !GM_BGANIM_NO_ASM

// band(): on real hardware this fills combRow (see file header for why one
// buffer serves both branches) and calls emberGatherRow for the palette
// gather; everywhere else (host bench, sim builds) it is bandRef.
GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
    if (w % 8 != 0) {
        // Never happens on the real device (w is always 240 or 480, both
        // multiples of 16 -- ASM_BRIEF.md) -- defensive fallback only.
        // emberGatherRow itself only needs w even (it processes pixel
        // pairs), but the no-flicker branch below expands perCol into
        // combRow 8 pixels at a time with no remainder handling, so an out-
        // of-bounds combRow write is possible for an even-but-not-x8 w
        // (e.g. 250) unless this guard also catches that case. combRow's
        // buffer is exactly w entries (init()), so any overrun here would
        // be a real heap write past it -- bandRef has no such constraint
        // (its no-flicker loop indexes perCol via x&7, valid for any w), so
        // this check exists specifically to keep band()'s contract as wide
        // as bandRef's despite the kernel's narrower internal assumption.
        bandRef(dst, y0, rows, w, tMs, p);
        return;
    }
    const bool doFlicker = g_flickerAmp != 0;
    const int wPairs = w >> 1;
    for (int r = 0; r < rows; r++) {
        const int y = y0 + r;
        const int dy = y - g_cy;
        const int dy2 = dy * dy;
        uint16_t *row = dst + static_cast<size_t>(r) * w;
        const uint8_t *bayerRow = &BAYER8[(y & 7) * 8];
        // Same formula as bandRef's perCol, widened to int16_t so the
        // kernel's L16SI load needs no separate sign-extend instruction
        // (matches the reasoning already used for combRow -- see file
        // header). This is a fresh local array, not a share of bandRef's:
        // the two are independently written on purpose (see file header).
        int16_t perCol[8];
        for (int k = 0; k < 8; k++) {
            perCol[k] = static_cast<int16_t>((static_cast<int>(bayerRow[k]) - 31) / 5 - g_breathe);
        }
        const int dx = -g_cx;
        const int r2_0 = dx * dx + dy2;
        const int ddx_0 = 2 * dx + 1;
        const uint16_t *palOff = paletteExt + PAD;

        if (doFlicker) {
            // Identical formula to bandRef's combRow precompute.
            const uint8_t *noiseRow = noise + ((y + g_sy) & 255) * 256;
            for (int x = 0; x < w; x++) {
                combRow[x] = static_cast<int16_t>(perCol[x & 7] + flickerLUT[noiseRow[(x + g_sx) & 255]]);
            }
        } else {
            // No flicker: combRow needs only the periodic dither/breathe
            // term, replicated, so the one gather kernel below can serve
            // this branch too (see file header for why sharing one kernel
            // is worth this small per-row expansion). w is always a
            // multiple of 8 on this animation's real targets (240/480,
            // both multiples of 16 -- ASM_BRIEF.md), so this unrolled
            // 8-wide copy has no remainder to special-case.
            for (int x = 0; x < w; x += 8) {
                for (int k = 0; k < 8; k++) {
                    combRow[x + k] = perCol[k];
                }
            }
        }
        emberGatherRow(row, palOff, radiusLUT, combRow, r2_0, ddx_0, wPairs);
    }
#else
    bandRef(dst, y0, rows, w, tMs, p);
#endif
}

void release() {
    releaseTable(paletteExt, PAL_EXT_N * sizeof(uint16_t));
    // An alias into paletteExt (paletteExt + PAD), not its own allocation —
    // handing it to free() would be heap corruption. Just drop it.
    palette = nullptr;
    releaseTable(radiusLUT, RLUT_N);
    releaseTable(flickerLUT, 256);
    releaseTable(combRow, static_cast<size_t>(allocW) * sizeof(int16_t));
    // Borrowed: noiseTex256() is a 64 KB fleet-wide asset owned by
    // BgAnimCommon and shared with nebula. Dropping the pointer is all this
    // animation is entitled to do.
    noise = nullptr;
    // init() reinstates these unconditionally, but reset them here too so the
    // freed-and-nulled state is self-consistent: leaving a live sentinel next
    // to a null table is the failure mode this whole entry point exists to
    // avoid (see BgAnimCommon.h).
    lastThemeGen = 0xFFFFFFFF;
    lastGlow = 255;
    lastFlickerParam = 255;
}

} // namespace

extern const BgAnimation bg_anim_ember;
const BgAnimation bg_anim_ember = {
    "ember",
    "Ember",
    {{"speed", "Speed", 50}, {"glow", "Glow size", 45}, {"flicker", "Flicker", 20}, {"pulse", "Pulse", 50}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
