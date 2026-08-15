#ifndef GAGGIMATE_SIM

// "Mandala" — N-fold rotational symmetry built from angular harmonics
// (sin(N*theta)), which are smooth and periodic by construction — no fold
// seams. Design: anim-geometric (Fable), 2026-08-15.
//
// Polar map optimization (2026-08-15): angle and radius are geometry-only —
// they never depend on time or params — so instead of running the fast
// atan2 poly + sqrt-bucket lookup per pixel per frame, we compute them once
// in init() into a quadrant-symmetric map and turn band() into table reads.
// The disc is mirrored across both axes (dx,dy -> |dx|,|dy|), so the map
// only needs one quadrant: (cx+1)^2 entries for a 480-wide panel (cx=240)
// is ~58k uint16 entries, ~115 KB. Each entry packs:
//   bits 15..8: angleOct  — the octant-folded angle (0..64) for the point
//               (|dx|,|dy|), i.e. what fastAngleQ8 would return for a point
//               in the first quadrant (dx>=0, dy>=0).
//   bits 7..0:  radius (0..cx) if inside the inscribed circle, else the
//               sentinel 0xFF ("outside").
// band() looks up the quadrant entry via (|dx|,|dy|), then reconstructs the
// full 0..255 angle from angleOct + the two sign bits of (dx,dy) — the same
// case split fastAngleQ8 used to do per pixel. See the pass-2 comment below
// for how that reconstruction was later made branch-free.
//
// Everything that is a function of radius alone but still depends on
// per-frame params (the radial phase offset `rOffset = (r*g_rOffsetScale)
// & 0xFF` folded into each harmonic's index, and the vignette x breathe
// scale) is baked into small (cx+1)-entry tables rebuilt once per frame in
// frame() — so band() never multiplies/divides by a per-frame param, it
// just adds two table reads together. The final (v+190)*255/380 rescale
// (v is bounded -191..190 by construction) is likewise a fixed one-time
// 381-entry LUT built in init(). Net per pixel: a handful of table reads,
// a couple of adds/shifts, one angle multiply — no float, no divide, no
// atan2 poly, no per-frame-param multiply.
//
// Branchless angle reconstruction (2026-08-15 pass 2): the map only stores
// the octant-folded angle `oct` (0..64) for |dx|,|dy|; band() used to
// rebuild the true angleQ8 (0..255) from oct + the two sign bits via a
// 4-way branch cascade (foldAngle), then multiply by g_N. That's ~10
// instructions and 2 data-dependent branches PER PIXEL on real hardware
// (see xtensa-asm/AnimMandala.S before this pass). The fix: fold the sign
// bits directly into g_N's sign and a single additive offset, algebraically
// (mod 256, matching the uint8_t truncation the original code relied on):
//   angleQ8 = (sx ? 128 : 0) + (sx == sy ? +1 : -1) * oct        (mod 256)
//   base    = angleQ8 * g_N
//           = (sx ? SX_OFFSET : 0) + oct * (sx == sy ? g_N : -g_N)   (mod 256)
// where SX_OFFSET = (g_N & 1) ? 128 : 0 (because 128*g_N mod 256 is 128 when
// g_N is odd, 0 when even). sx flips exactly once per row (at x==cx) and sy
// is constant for the whole row, so `base` reduces to ONE multiply-add
// (`oct * gN_eff + halfOffset`) with both operands loop-invariant per row
// *half* — no per-pixel branch at all. band() below renders each row as two
// straight runs (mirrored left half, direct right half) instead of one
// branchy loop, and walks the polar-map pointer with ++/-- instead of
// recomputing `ax` and its address each pixel (ax's stride equals the map's
// own element stride).
//
// The "outside the disc" case (map radius sentinel 0xFF) used to be a
// separate per-pixel branch that wrote g_outside and skipped the rest of
// the pipeline. It is now handled by padding idxOffAB/vigBreathe to
// 256 entries and forcing vigBreathe[0xFF] = 0 once in init(): whatever
// garbage idxA/idxB/v the normal pipeline computes for an outside pixel,
// multiplying by a zero vignette collapses it to v=0, and
// paletteLUT[0] == themeRGB(0) at full brightness == g_outside exactly
// (buildThemeRamp(...,256) at i=0 computes precisely that). So the outside
// case falls out of the same branch-free arithmetic for free.
#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <esp_heap_caps.h>
#include <math.h>

namespace {
using namespace bganim;

int16_t *sin256 = nullptr;      // Q7 sine, 256 entries
int16_t *sinHalf256 = nullptr;  // sin256[i]>>1 precomputed, 256 entries — saves a per-pixel shift
int16_t *sqrtLUT = nullptr;     // r2 bucket -> radius (bucket 192); init-time only
uint32_t *recipLUT = nullptr;   // Q16 65536/(i+1), 0..240; init-time only
uint8_t *vigByR = nullptr;      // radius 0..cx -> vignette falloff Q8, indexed directly by radius
// v = sin256[idxA] + sinHalf256[idxB]. sin256 is Q7 with |sin256| <= 127, and
// sinHalf256[i] = sin256[i] >> 1 — an ARITHMETIC shift, which rounds toward
// negative infinity, so -127 >> 1 is -64, not -63. The true range of v is
// therefore [-191, 190], not the symmetric [-190, 190] the original comment
// claimed, and the unpadded 381-entry table was read one byte off its front
// (caught by tools/animbench/fuzz under ASan). Rather than re-derive the
// offset, pad both ends and clamp the padding entries, which keeps every
// in-range value bit-identical and costs 8 bytes.
constexpr int RESCALE_SPAN = 381; // v+190 for v in [-190, 190]
constexpr int RESCALE_PAD = 4;    // covers v = -191 with room on both sides
constexpr int RESCALE_N = RESCALE_SPAN + 2 * RESCALE_PAD;
// (v+190) -> 0..255, replaces the old */380 divide. Padded: see RESCALE_PAD.
// Points RESCALE_PAD entries into its allocation, so a slightly out-of-range v
// lands on a clamped entry instead of off the front of the block.
uint8_t *rescaleLUT = nullptr;
uint16_t *paletteLUT = nullptr;
uint16_t *polarMap = nullptr;   // quadrant map: (angleOct<<8 | radiusOr0xFF), (cx+1)x(cx+1)
// idxOffAB/vigBreathe are sized 256, not cx+1: index 0..cx are the
// real per-frame per-radius offsets rebuilt in frame(); index 0xFF (the
// polar map's "outside the disc" sentinel) is set ONCE in init() below and
// never touched again, so band() can use the map's radius/sentinel byte to
// index these arrays directly with no per-pixel branch — see the pass-2
// comment above.
//
// idxOffA and idxOffB used to be separate uint8_t arrays. They are packed
// into one uint16_t array (low byte = A term, high byte = B term) so the
// hot loop needs only one base-pointer register and one load instead of
// two of each — mandalaRun<> is register-starved (see its comment), and
// this is a straight register-pressure win with no algorithm change.
uint16_t *idxOffAB = nullptr;   // radius -> idxOffA(lo) | idxOffB(hi)<<8, [0xFF] unused (any value is safe)
uint8_t *vigBreathe = nullptr;  // radius -> (vigByR[r] * g_breatheQ8) >> 8, [0xFF] forced to 0
uint32_t lastThemeGen = 0xFFFFFFFF;
int g_cx = 240; // half panel width; also the map's per-axis extent (assumes cx < 255)

constexpr uint8_t OUTSIDE_R = 0xFF;

void buildThemePalette() { buildThemeRamp(paletteLUT, 256); }

// Octant-folded angle (0..64) for a point in the first quadrant (ax,ay >= 0).
// Same reciprocal-LUT + minimax-poly approximation the old per-pixel path
// used, but now only ever called (cx+1)^2 times, once, in init().
inline uint8_t octantAngle(int ax, int ay) {
    const bool swap = ax < ay;
    const int hi = swap ? ay : ax;
    const int lo = swap ? ax : ay;
    if (hi == 0) {
        return 0;
    }
    // hi <= cx (<=240 for the real panel), recipLUT covers that range.
    const uint32_t ratioQ16 = (static_cast<uint32_t>(lo) * recipLUT[hi - 1]) >> 16;
    const float ratio = ratioQ16 * (1.0f / 65536.0f);
    const float ang = ratio * (0.9817f - 0.1963f * ratio * ratio); // radians, 0..pi/4
    int oct = static_cast<int>(ang * (128.0f / 3.14159265f));      // 0..32 within octant
    if (swap) {
        oct = 64 - oct;
    }
    return static_cast<uint8_t>(oct);
}

bool init(int w, int) {
    if (sin256 == nullptr) {
        g_cx = w / 2;
        const int mapDim = g_cx + 1;
        sin256 = static_cast<int16_t *>(alloc(256 * sizeof(int16_t)));
        sinHalf256 = static_cast<int16_t *>(alloc(256 * sizeof(int16_t)));
        sqrtLUT = static_cast<int16_t *>(alloc(602 * sizeof(int16_t)));
        recipLUT = static_cast<uint32_t *>(alloc(241 * sizeof(uint32_t)));
        vigByR = static_cast<uint8_t *>(alloc(mapDim));
        uint8_t *rescaleAlloc = static_cast<uint8_t *>(alloc(RESCALE_N));
        rescaleLUT = rescaleAlloc != nullptr ? rescaleAlloc + RESCALE_PAD : nullptr;
        paletteLUT = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
        // polarMap is ~113 KiB ((cx+1)^2 uint16 entries) — a bulk table read
        // in sequential sweeps, not a small randomly-indexed LUT, so it goes
        // straight to PSRAM (8 MB, plentiful) rather than through alloc()'s
        // SRAM-first path. Internal SRAM is the scarce resource WiFi/BLE/TLS
        // draw from at runtime; a map this size has no business contending
        // for it. Same convention as SleepAnimation.cpp's overlay snapshot
        // buffers ("Snapshot pixels only fit in PSRAM (~700 KB each); the
        // tiny span tables prefer SRAM") — polarMap is the snapshot-sized
        // table here, and every other LUT on this page is the span-sized one.
        // heap_caps_malloc(MALLOC_CAP_SPIRAM), not ps_malloc: the latter is an
        // Arduino-layer helper this translation unit does not pull in, and it
        // builds on the host shim while failing the real firmware build.
        polarMap = static_cast<uint16_t *>(
            heap_caps_malloc(static_cast<size_t>(mapDim) * mapDim * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
        // 256, not mapDim: index 0xFF is the map's outside-disc sentinel and
        // is read unconditionally by band() now (no per-pixel branch).
        idxOffAB = static_cast<uint16_t *>(alloc(256 * sizeof(uint16_t)));
        vigBreathe = static_cast<uint8_t *>(alloc(256));
        if (sin256 == nullptr || sinHalf256 == nullptr || sqrtLUT == nullptr || recipLUT == nullptr ||
            vigByR == nullptr || rescaleLUT == nullptr || paletteLUT == nullptr || polarMap == nullptr ||
            idxOffAB == nullptr || vigBreathe == nullptr) {
            return false;
        }
        for (int i = 0; i < 256; i++) {
            sin256[i] = static_cast<int16_t>(lroundf(127.0f * sinf(i * 6.2831853f / 256.0f)));
        }
        for (int i = 0; i < 256; i++) {
            sinHalf256[i] = static_cast<int16_t>(sin256[i] >> 1);
        }
        // Outside-disc sentinel slot: idxOffAB[0xFF] is never load-bearing
        // (see comment at the top), but vigBreathe[0xFF] = 0 is the crux of
        // the branchless outside path — it zeroes v regardless of whatever
        // idxA/idxB garbage the pipeline computes, landing on
        // paletteLUT[0] == g_outside. frame() only ever rewrites indices
        // 0..g_cx, so this is set once, here, for the process lifetime.
        idxOffAB[OUTSIDE_R] = 0;
        vigBreathe[OUTSIDE_R] = 0;
        for (int i = 0; i < 602; i++) {
            sqrtLUT[i] = static_cast<int16_t>(lroundf(sqrtf(i * 192.0f)));
        }
        for (int i = 0; i < 241; i++) {
            recipLUT[i] = static_cast<uint32_t>(lroundf(65536.0f / (i + 1)));
        }
        for (int r = 0; r < mapDim; r++) {
            vigByR[r] = static_cast<uint8_t>(lroundf(255.0f * powf(1.0f - static_cast<float>(r) / g_cx, 0.55f)));
        }
        // Build through the padded base so the leading and trailing pad
        // entries repeat the clamped end values (see RESCALE_PAD above).
        for (int j = 0; j < RESCALE_N; j++) {
            int i = j - RESCALE_PAD;
            if (i < 0) {
                i = 0;
            } else if (i > RESCALE_SPAN - 1) {
                i = RESCALE_SPAN - 1;
            }
            rescaleLUT[j - RESCALE_PAD] = static_cast<uint8_t>((i * 255) / 380);
        }
        const int maxR2 = g_cx * g_cx;
        for (int ay = 0; ay < mapDim; ay++) {
            for (int ax = 0; ax < mapDim; ax++) {
                const int r2 = ax * ax + ay * ay;
                uint8_t rOrOut;
                if (r2 > maxR2) {
                    rOrOut = OUTSIDE_R;
                } else {
                    rOrOut = static_cast<uint8_t>(sqrtLUT[r2 / 192]);
                }
                const uint8_t oct = octantAngle(ax, ay);
                polarMap[ay * mapDim + ax] = static_cast<uint16_t>((static_cast<uint16_t>(oct) << 8) | rOrOut);
            }
        }
        buildThemePalette();
        lastThemeGen = themeGen();
    }
    return true;
}

int g_N = 8, g_tOffA = 0, g_tOffB = 0, g_rOffsetScale = 0;
int g_breatheQ8 = 256;
int g_sxOffset = 0; // (g_N & 1) ? 128 : 0 — see the pass-2 comment at the top

void frame(uint32_t tMs, int, int, const uint8_t p[4]) {
    if (themeGen() != lastThemeGen) {
        buildThemePalette();
        lastThemeGen = themeGen();
    }
    g_N = 4 + (p[1] * 8) / 100;
    g_sxOffset = (g_N & 1) ? 128 : 0;
    const float t = tMs * 0.001f * 0.35f * speedMul(p[0]);
    const float turb = 0.25f + (p[2] / 100.0f) * 1.1f;
    g_rOffsetScale = static_cast<int>(turb * 18.0f);
    // 40.74 = 256 ticks per 2*pi radians
    g_tOffA = static_cast<int>(t * 1.4f * 40.74f) & 0xFF;
    g_tOffB = static_cast<int>(t * 0.8f * 40.74f) & 0xFF;
    g_breatheQ8 = static_cast<int>((0.82f + 0.18f * fastSinRad(t * 0.45f)) * 256.0f);

    // Fold everything that is "per-frame param x radius" but not per-pixel
    // into (cx+1)-entry tables — band() then just reads and adds. Cheap:
    // g_cx+1 (<=241) iterations, once per frame, plain integer ops. Index
    // 0xFF (outside-disc) is deliberately NOT touched here — it is set once
    // in init() and must stay put (see the arrays' declaration comment).
    const int mapDim = g_cx + 1;
    for (int r = 0; r < mapDim; r++) {
        const int rOffset = (r * g_rOffsetScale) & 0xFF;
        const auto a = static_cast<uint8_t>(rOffset + g_tOffA);
        const auto b = static_cast<uint8_t>(g_tOffB - (rOffset * 3) / 5);
        idxOffAB[r] = static_cast<uint16_t>(a | (b << 8));
        vigBreathe[r] = static_cast<uint8_t>((vigByR[r] * g_breatheQ8) >> 8);
    }
}

// band() renders each row as two branch-free straight runs instead of one
// branchy 0..w loop. Both runs walk the polar map with a pointer ++/--
// instead of recomputing an address from ax each pixel, and both fold the
// angle-reconstruction sign flip into the sign of g_N (gN_eff) plus a
// per-half additive constant (halfOffset) — see the big comment at the top
// for the mod-256 algebra. Neither run branches on rOrOut for the
// outside-disc case either: vigBreathe[0xFF]==0 (set once in init()) makes
// v collapse to 0 for those pixels, landing on paletteLUT[0] == g_outside.
// Deliberately NOT inlined: Xtensa has a single hardware zero-overhead-loop
// register set (LBEG/LEND/LCOUNT), and gcc declines to emit the `loop`
// instruction when a function contains two sibling loop candidates (as
// band() would if this were inlined at both call sites — confirmed via
// xtensa-asm.sh, see the report at the bottom of this file's history). Kept
// as its own function, this compiles once per Step with a single loop and
// gets the `loop` instruction; band() pays two ordinary CALL8s per row (960
// total, negligible next to 230K pixel-iterations).
//
// Step is a template parameter, not a runtime int, deliberately: this
// function is already register-starved (dstPtr, mapPtr, count, gN_eff,
// halfOffset, plus 6 global LUT pointers — idxOffAB/sin256/sinHalf256/
// rescaleLUT/vigBreathe/paletteLUT — is more live state than Xtensa's 8
// free a-registers after the windowed-call ABI takes a2..a6+). A runtime
// mapStep parameter was measured (xtensa-asm.sh) to spill to the stack and
// get reloaded every pixel; making it a compile-time constant removes both
// the parameter and that reload. idxOffA/idxOffB were likewise merged into
// one packed idxOffAB array below to remove a second pointer register —
// even after both fixes, rescaleLUT/vigBreathe/paletteLUT still spill (see
// xtensa-asm.sh output); further reduction would need merging those three
// or the sin256/sinHalf256 pair.
template <int Step>
__attribute__((noinline)) void mandalaRun(uint16_t *dstPtr, const uint16_t *mapPtr, int count, int gN_eff,
                                           int halfOffset) {
    for (int i = 0; i < count; i++) {
        const uint16_t entry = *mapPtr;
        mapPtr += Step;
        const int r = entry & 0xFF;
        const int oct = entry >> 8;
        const int base = halfOffset + oct * gN_eff;
        const uint16_t offAB = idxOffAB[r]; // low byte = A term, high byte = B term
        const uint8_t idxA = static_cast<uint8_t>(base + (offAB & 0xFF));
        const uint8_t idxB = static_cast<uint8_t>(2 * base + (offAB >> 8));
        int v = sin256[idxA] + sinHalf256[idxB]; // -191..190 (see RESCALE_PAD)
        v = rescaleLUT[v + 190];
        v = (v * vigBreathe[r]) >> 8;
        dstPtr[i] = paletteLUT[v];
    }
}

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const int cx = w / 2, cy = w / 2;
    const int mapDim = g_cx + 1;
    for (int ry = 0; ry < rows; ry++) {
        const int y = y0 + ry;
        const int dy = y - cy;
        const int ay = dy < 0 ? -dy : dy; // ay in [0, g_cx] given w == 2*g_cx (band's contract)
        const bool sy = dy < 0;
        uint16_t *row = dst + static_cast<size_t>(ry) * w;
        const uint16_t *mapRow = polarMap + static_cast<size_t>(ay) * mapDim;
        // gN_eff carries the (sx == sy) sign flip; halfOffset carries the
        // (sx ? SX_OFFSET : 0) term. Both derived once per row (not per
        // pixel) — see the pass-2 comment at the top for the algebra.
        const int gN_eff_left = sy ? g_N : -g_N;
        const int gN_eff_right = sy ? -g_N : g_N;
        // Left half: x in [0,cx), dx<0 (mirrored). ax = cx-x walks DOWN
        // from g_cx to 1 as x increases, so start the map pointer at
        // mapRow+g_cx and decrement it once per pixel.
        mandalaRun<-1>(row, mapRow + g_cx, cx, gN_eff_left, g_sxOffset);
        // Right half: x in [cx,w), dx>=0 (direct). ax = x-cx walks UP from
        // 0 to g_cx-1, so start the map pointer at mapRow+0 and increment.
        mandalaRun<+1>(row + cx, mapRow, w - cx, gN_eff_right, 0);
    }
}

} // namespace

extern const BgAnimation bg_anim_mandala;
const BgAnimation bg_anim_mandala = {
    "mandala",
    "Mandala",
    {{"speed", "Speed", 50}, {"symmetry", "Symmetry", 50}, {"complexity", "Complexity", 45}, {nullptr, nullptr, 0}},
    init,
    frame,
    band,
};

#endif // GAGGIMATE_SIM
