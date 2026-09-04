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
// *half*, no per-pixel branch at all. bandRef() below renders each row as
// two straight runs (mirrored left half, direct right half) instead of one
// branchy loop, and walks the polar-map pointer with ++/-- instead of
// recomputing `ax` and its address each pixel (ax's stride equals the map's
// own element stride).
//
// The "outside the disc" case (map radius sentinel 0xFF) used to be a
// separate per-pixel branch that wrote g_outside and skipped the rest of
// the pipeline. It is now handled by padding rParams/rescaleLUT to 256
// entries and forcing rParams[0xFF]'s vig field to 0 once in init() below:
// whatever garbage idxA/idxB/v the normal pipeline computes for an outside
// pixel, multiplying by a zero vignette collapses it to v=0, and
// paletteLUT[0] == themeRGB(0) at full brightness == g_outside exactly
// (buildThemeRamp(...,256) at i=0 computes precisely that). So the outside
// case falls out of the same branch-free arithmetic for free.
//
// Assembly pass (2026-09-04): idxOffA/idxOffB were already packed into one
// idxOffAB array (comment below, since superseded) to free a base-pointer
// register for the device kernel; this pass takes that one step further and
// merges idxOffAB with vigBreathe too, into a single 256-entry uint32_t
// `rParams` table (bits 0..7 = A term, 8..15 = B term, 16..23 = vig). That
// turns two per-pixel gathers (idxOffAB[r], vigBreathe[r]) into one, and
// two base-pointer registers into one, the same register-pressure trade
// the earlier merge made, applied one level further. bandRef() (this
// file's former band(), the portable spec) and the on-device band() both
// read rParams, so they cannot disagree about it.
//
// Assembly pass, round 2 (2026-09-04, same day, after device numbers came
// back): round 1's band() PIE-decoded polarMap 8 entries at a time into a
// scratch buffer, then drained the scratch with a separate hand-scheduled
// scalar pass, on the theory that batching the oct*gN_eff multiply into PIE
// (once per 8 pixels instead of once per pixel) would be a net win. On the
// device it measured SLOWER than bandRef -- 48.1ms vs 41.9ms per full frame
// with every table pinned to internal SRAM, i.e. with the PSRAM-placement
// question held fixed so the comparison is kernel vs kernel, not table vs
// table. Diffing xtensa-asm14's output for mandalaRun<Step> (bandRef's
// compiler-generated loop) against round 1's mandalaGatherFwd/Back found
// why: GCC's schedule pays exactly ONE load-use stall per pixel (the final
// palette-load-into-store, genuinely unavoidable -- nothing independent is
// left to do at the end of an iteration), where round 1's hand kernel paid
// four. GCC gets there two ways round 1 missed: it never separately masks
// A or B out of `params` -- it adds the WHOLE params register to base (for
// idxA) or to base*2 via one addx2 (for idxB), and masks only the SUM to 8
// bits at the very end, which is correct because whatever B/vig (or vig
// alone) leaves in the bits above position 7 is an exact multiple of 256
// and cancels under that final mod-256 truncation; and it interposes the
// independent oct*gN_eff multiply directly after the rParams load, and the
// next LUT's address computation directly after every other load, so every
// load but the last has real work already queued behind it. Round 1's
// kernel extracted A and B via separate EXTUIs this trick shows are
// redundant, and its `slli` for base*2 sat in the wrong slot, leaving the
// rParams load's result consumed by the very next instruction.
//
// Given that, PIE's amortized multiply was buying back less than the
// two-pass materialize-then-drain design cost elsewhere (two extra
// function calls per row, a full round trip of every radius/base pair
// through a scratch buffer instead of using it once while it's already in
// a register, a software-branch decode loop, and the padded/aligned
// polarMap stride that existed only to keep EE.VLD.128.IP's 16-byte
// alignment requirement satisfied) -- exactly the "reorganised tables cost
// more in cache than they gain in the loop" pattern the round-2 brief
// flags across the fleet. This pass removes all of it: mandalaDecodePie,
// the scratch buffers, the octant/radius mask constants, and polarMap's
// column padding and hand-aligned allocation are gone; polarMap is back to
// a plain (cx+1)x(cx+1) table (see init()), and bandRef()'s left half is
// back to starting at mapRow+g_cx and decrementing (no more mapRow+0
// workaround, since nothing here needs 16-byte alignment).
//
// What replaces it is a single hand-scheduled scalar pass, one pixel per
// LOOPNEZ iteration, that reads polarMap directly -- structurally the same
// shape as mandalaRun<Step>, applying GCC's own proven schedule (verified
// instruction for instruction against xtensa-asm14/AnimMandala.S's
// mandalaRun<Step> dump) by hand. The honest case for hand-asm here is not
// a smarter algorithm than GCC found -- it isn't, GCC's schedule already
// hits the one truly unavoidable stall and nothing worse -- it is the same
// schedule, called from band() the same way bandRef() calls
// mandalaRun<Step>, measuring five static instructions per call lighter
// (xtensa-asm14: 40/41 vs 45/46 -- see the round-2 report). See
// mandalaBandFwd's comment for the register budget and the per-instruction
// reasoning.
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
// The base of rescaleLUT's allocation. rescaleLUT itself points RESCALE_PAD
// entries into it, so it is not a valid pointer to free; keeping the base is
// what lets release() hand the block back instead of corrupting the heap.
uint8_t *rescaleAlloc = nullptr;
uint16_t *paletteLUT = nullptr;
uint16_t *polarMap = nullptr; // quadrant map: (angleOct<<8 | radiusOr0xFF), (cx+1)x(cx+1) entries, row-major
// rParams[r], r in 0..cx (plus the outside-disc sentinel 0xFF): bits 0..7 =
// the A-harmonic's radial phase offset, bits 8..15 = the B-harmonic's,
// bits 16..23 = the vignette-times-breathe factor Q8. Rebuilt every frame
// (frame() below) except index 0xFF, which init() sets once and band()
// never touches again, see the top-of-file comment for why that makes the
// outside-the-disc case fall out of the branch-free per-pixel arithmetic
// for free.
//
// Formerly two arrays (idxOffAB, itself already a merge of idxOffA and
// idxOffB for the same reason, see history, and vigBreathe). Merging a
// third field in trades 256 * 4 - (256*2 + 256) = 256 bytes of SRAM (still
// two orders of magnitude under this file's share of the fleet's 28 KB
// budget) for one fewer per-pixel gather and one fewer base-pointer
// register in the device kernel, which is what actually gated this file's
// last register-starved rewrite. See the top-of-file assembly-pass comment.
uint32_t *rParams = nullptr;
uint32_t lastThemeGen = 0xFFFFFFFF;
// Whether the one-time table fills have run. Namespace scope, not a static
// local in init(): vigByR and polarMap are sized from cx, so a resolution
// change has to rebuild them, and release() can only reset this flag if it can
// see it. While it was function-local, init() would reallocate the tables on a
// resolution change and then skip filling them.
bool tablesBuilt = false;
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
    // Every table carries its own guard and the combined check below runs
    // unconditionally, so a partial allocation failure retries cleanly on the
    // next activation. This used to be one `if (sin256 == nullptr)` block
    // wrapping the allocations, the check and the fills together: if sin256
    // succeeded but any later table failed, init() returned false yet left
    // sin256 non-null, so the next call skipped the whole block -- retry and
    // check alike -- and fell through to `return true` with null tables that
    // frame() and band() dereference without checking.
    g_cx = w / 2;
    const int mapDim = g_cx + 1;
    // rParams, sin256, sinHalf256, rescaleLUT and paletteLUT are this file's
    // per-pixel set: every one of them is read once per pixel by band()'s
    // scalar gather (see mandalaBandFwd/Back), 230,400 reads/frame each at
    // full resolution, which is the "reads per frame, not size" criterion
    // BgAnimCommon.h's allocHot() comment asks for. Combined they are 256*4
    // (rParams) + 256*2 (sin256) + 256*2 (sinHalf256) + RESCALE_N (389,
    // rescaleLUT) + 256*2 (paletteLUT) = 2,949 B, comfortably inside the
    // 9,216 B per-animation slab. sqrtLUT, recipLUT and vigByR below stay on
    // alloc() (PSRAM): all three are read only at init()/frame() time (once
    // per (cx+1)^2 map-fill pass, or once per cx+1 per frame), never from
    // band()'s per-pixel path, so they do not compete for the slab.
    // allocHot() returns nullptr when the slab is full; alloc() (PSRAM) is
    // the explicit fallback, same pattern as AnimSilk.cpp's g_lut, so a
    // full slab degrades this animation instead of failing its init().
    if (sin256 == nullptr) {
        sin256 = static_cast<int16_t *>(allocHot(256 * sizeof(int16_t)));
        if (sin256 == nullptr) {
            sin256 = static_cast<int16_t *>(alloc(256 * sizeof(int16_t)));
        }
    }
    if (sinHalf256 == nullptr) {
        sinHalf256 = static_cast<int16_t *>(allocHot(256 * sizeof(int16_t)));
        if (sinHalf256 == nullptr) {
            sinHalf256 = static_cast<int16_t *>(alloc(256 * sizeof(int16_t)));
        }
    }
    if (sqrtLUT == nullptr) {
        sqrtLUT = static_cast<int16_t *>(alloc(602 * sizeof(int16_t)));
    }
    if (recipLUT == nullptr) {
        recipLUT = static_cast<uint32_t *>(alloc(241 * sizeof(uint32_t)));
    }
    if (vigByR == nullptr) {
        vigByR = static_cast<uint8_t *>(alloc(mapDim));
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
    // sequential sweeps, not a small randomly-indexed LUT, so it goes
    // straight to PSRAM (8 MB, plentiful) rather than through alloc()'s
    // SRAM-first path. Internal SRAM is the scarce resource WiFi/BLE/TLS
    // draw from at runtime; a map this size has no business contending
    // for it. Same convention as SleepAnimation.cpp's overlay snapshot
    // buffers ("Snapshot pixels only fit in PSRAM (~700 KB each); the
    // tiny span tables prefer SRAM") — polarMap is the snapshot-sized
    // table here, and every other LUT on this page is the span-sized one.
    // heap_caps_malloc(MALLOC_CAP_SPIRAM), not ps_malloc: the latter is an
    // Arduino-layer helper this translation unit does not pull in, and it
    // builds on the host shim while failing the real firmware build. No
    // alignment requirement on the allocation itself (see the round-2
    // top-of-file comment: the device kernel reads this with plain scalar
    // loads, not EE.VLD.128.IP, so the 16-byte alignment round 1 needed
    // here no longer applies).
    if (polarMap == nullptr) {
        polarMap = static_cast<uint16_t *>(
            heap_caps_malloc(static_cast<size_t>(mapDim) * mapDim * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
    }
    // 256, not mapDim: index 0xFF is the map's outside-disc sentinel and
    // is read unconditionally by both bandRef() and the device kernel
    // (no per-pixel branch). Per-pixel set, see the allocHot comment above.
    if (rParams == nullptr) {
        rParams = static_cast<uint32_t *>(allocHot(256 * sizeof(uint32_t)));
        if (rParams == nullptr) {
            rParams = static_cast<uint32_t *>(alloc(256 * sizeof(uint32_t)));
        }
    }
    if (sin256 == nullptr || sinHalf256 == nullptr || sqrtLUT == nullptr || recipLUT == nullptr || vigByR == nullptr ||
        rescaleLUT == nullptr || paletteLUT == nullptr || polarMap == nullptr || rParams == nullptr) {
        return false;
    }
    if (!tablesBuilt) {
        tablesBuilt = true;
        for (int i = 0; i < 256; i++) {
            sin256[i] = static_cast<int16_t>(lroundf(127.0f * sinf(i * 6.2831853f / 256.0f)));
        }
        for (int i = 0; i < 256; i++) {
            sinHalf256[i] = static_cast<int16_t>(sin256[i] >> 1);
        }
        // Outside-disc sentinel slot: the A/B fields at rParams[0xFF] are
        // never load-bearing (see the top-of-file comment), but the vig
        // field being 0 is the crux of the branchless outside path, it
        // zeroes v regardless of whatever idxA/idxB garbage the normal
        // pipeline computes, landing on paletteLUT[0] == g_outside. frame()
        // only ever rewrites indices 0..g_cx, so this is set once, here,
        // for the process lifetime.
        rParams[OUTSIDE_R] = 0;
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
    // into a (cx+1)-entry table, bandRef()/the device kernel then just
    // read and add. Cheap: g_cx+1 (<=241) iterations, once per frame, plain
    // integer ops. Index 0xFF (outside-disc) is deliberately NOT touched
    // here, it is set once in init() and must stay put (see rParams'
    // declaration comment).
    const int mapDim = g_cx + 1;
    for (int r = 0; r < mapDim; r++) {
        const int rOffset = (r * g_rOffsetScale) & 0xFF;
        const auto a = static_cast<uint8_t>(rOffset + g_tOffA);
        const auto b = static_cast<uint8_t>(g_tOffB - (rOffset * 3) / 5);
        const auto vig = static_cast<uint8_t>((vigByR[r] * g_breatheQ8) >> 8);
        rParams[r] = static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 8) | (static_cast<uint32_t>(vig) << 16);
    }
}

// bandRef() renders each row as two branch-free straight runs instead of one
// branchy 0..w loop. Both runs walk the polar map with a pointer ++/--
// instead of recomputing an address from ax each pixel, and both fold the
// angle-reconstruction sign flip into the sign of g_N (gN_eff) plus a
// per-half additive constant (halfOffset) — see the big comment at the top
// for the mod-256 algebra. Neither run branches on rOrOut for the
// outside-disc case either: rParams[0xFF]'s vig field is 0 (set once in
// init()), which makes v collapse to 0 for those pixels, landing on
// paletteLUT[0] == g_outside.
// Deliberately NOT inlined: Xtensa has a single hardware zero-overhead-loop
// register set (LBEG/LEND/LCOUNT), and gcc declines to emit the `loop`
// instruction when a function contains two sibling loop candidates (as this
// would if it were inlined at both call sites, confirmed via xtensa-asm.sh,
// see the report at the bottom of this file's history). Kept as its own
// function, this compiles once per Step with a single loop and gets the
// `loop` instruction; bandRef() pays two ordinary CALL8s per row (960
// total, negligible next to 230K pixel-iterations).
//
// Step is a template parameter, not a runtime int, deliberately: this
// function is already register-starved (dstPtr, mapPtr, count, gN_eff,
// halfOffset, plus rParams/sin256/sinHalf256/rescaleLUT/paletteLUT, five
// global LUT pointers, one fewer than before the rParams merge, is more
// live state than Xtensa's 8 free a-registers after the windowed-call ABI
// takes a2..a6+). A runtime mapStep parameter was measured (xtensa-asm.sh)
// to spill to the stack and get reloaded every pixel; making it a
// compile-time constant removes both the parameter and that reload.
template <int Step>
__attribute__((noinline)) void mandalaRun(uint16_t *dstPtr, const uint16_t *mapPtr, int count, int gN_eff,
                                           int halfOffset) {
    for (int i = 0; i < count; i++) {
        const uint16_t entry = *mapPtr;
        mapPtr += Step;
        const int r = entry & 0xFF;
        const int oct = entry >> 8;
        const int base = halfOffset + oct * gN_eff;
        const uint32_t params = rParams[r]; // bits 0..7 A, 8..15 B, 16..23 vig
        const auto A = static_cast<uint8_t>(params);
        const auto B = static_cast<uint8_t>(params >> 8);
        const auto vig = static_cast<uint8_t>(params >> 16);
        const uint8_t idxA = static_cast<uint8_t>(base + A);
        const uint8_t idxB = static_cast<uint8_t>(2 * base + B);
        int v = sin256[idxA] + sinHalf256[idxB]; // -191..190 (see RESCALE_PAD)
        v = rescaleLUT[v + 190];
        v = (v * vig) >> 8;
        dstPtr[i] = paletteLUT[v];
    }
}

// The portable spec: same algorithm as the device kernel below, kept as
// plain compiler-generated code. Host bench goldens run against this, and
// the device equivalence test (SleepAnimation::runAnimTest, in
// /api/debug/animtest) checks band()'s asm kernel against it pixel for
// pixel. See BgAnim.h's bandRef field comment.
void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const int cx = w / 2, cy = w / 2;
    const int mapDim = g_cx + 1; // polarMap's row stride, see its declaration comment
    for (int ry = 0; ry < rows; ry++) {
        const int y = y0 + ry;
        const int dy = y - cy;
        const int ay = dy < 0 ? -dy : dy; // ay in [0, g_cx] given w == 2*g_cx (band's contract)
        const bool sy = dy < 0;
        uint16_t *row = dst + static_cast<size_t>(ry) * w;
        // mapDim = g_cx+1: polarMap's row stride, see its declaration
        // comment. bandRef() and band() (below) share this buffer and this
        // stride; they cannot disagree about it.
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

#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)

// mandalaBandFwd is the right-half (Step=+1) kernel: it reads polarMap
// forward and runs the same per-pixel algebra as mandalaRun<+1> above, hand
// scheduled to match GCC's own proven schedule for this exact computation
// (see the round-2 top-of-file comment). halfOffset is always 0 at this
// kernel's one call site (band(), and bandRef's own mandalaRun<+1> call
// mirrors it) -- a real compile-time fact this file's angle algebra relies
// on (the pass-2 comment at the top), so it is dropped as a
// parameter/instruction here rather than carried as a runtime add of a
// known-zero value; mandalaBandBack below is the version that needs it.
//
// Per-pixel algebra, matching mandalaRun's C++ exactly:
//   entry = *mapPtr; mapPtr++            r = entry & 0xFF; oct = entry >> 8
//   params = rParams[r]                  base = oct * gN_eff
//   idxA = (base + params) & 0xFF        idxB = (base*2 + (params >> 8)) & 0xFF
//   v = sin256[idxA] + sinHalf256[idxB]  v = rescaleLUT[v + 190]
//   v = (v * (params >> 16)) >> 8        dst[i] = paletteLUT[v]
// idxA and idxB skip the separate "mask A/B out of params" step mandalaRun's
// C++ spells out: adding the WHOLE params register (idxA) or the whole
// params register shifted down 8 (idxB, fused into base*2 via one addx2) is
// correct, because whatever B/vig (or vig alone) sits in the bits above
// position 7 of that sum is an exact multiple of 256 and cancels under the
// final mod-256 mask -- this is GCC's own trick, confirmed by reading
// xtensa-asm14/AnimMandala.S's mandalaRun<Step> output, not independently
// re-derived here.
//
// Instructions are ordered so a load's result is not consumed by the very
// next instruction where independent work can be interposed instead
// (load-use interlock: one stall cycle otherwise, OPTIMIZE.md/ASM_BRIEF.md):
// the ptr-decrement fills the entry load's slot, the oct*gN_eff multiply
// (fully independent of the rParams gather) fills the params load's slot,
// and each LUT's address computation fills the load right before it. Only
// one stall is unavoidable: the palette load into the store (S16I has no
// negative-immediate form, so unlike every other step this one cannot be
// reordered around an early pointer bump to buy a free instruction of
// distance, and nothing else is left to do in this iteration). This matches
// GCC's own mandalaRun<Step> exactly -- one stall, in the same place. This
// is still a separate noinline function, called from band() the same way
// bandRef() calls mandalaRun<Step> (an earlier draft of this comment
// claimed inlining the asm directly into band()'s row loop to shave the
// CALL8/RETW pair; that was not built, and correctly so on reflection --
// band()'s own loop state (mapRow, row, ay, sy, both gN_eff values) would
// then have to stay live across this kernel's own 12-13-register budget in
// ONE function, which is real spill risk for a per-row-half saving of a
// handful of cycles against a 240-plus-pixel loop, the kind of trade
// OPTIMIZE.md's silk lesson warns against taking without a measurement
// behind it). The honest case for a hand-asm kernel here, given GCC's own
// schedule already hits the one unavoidable stall, is simply matching that
// schedule with five fewer static instructions per call (see the round-2
// report's xtensa-asm14 counts) -- not a structural win over calling
// bandRef()'s own compiled code, but not a loss either, which is what
// round 1 was.
//
// Register budget: mapPtr, dstPtr (2, stepped every iteration) + gN_eff,
// rParamsBase, sin256Base, sinHalf256Base, rescaleLUTBase, paletteLUTBase
// (6, live for the whole loop) + 4 reused scratch temps (u1..u4, mirroring
// GCC's own 4-register reuse for this computation) = 12, one under the
// ~13-usable-AR ceiling ASM_BRIEF.md documents (mandalaBandBack below, one
// register heavier for the real halfOffset, hits that ceiling exactly, and
// GCC's own compiled mandalaRun<-1> -- which carries the same runtime
// halfOffset -- independently uses a2..a15 minus the one hardware
// loop-count register, 13, confirming the ceiling empirically for this
// exact computation). u1 starts as the loop trip count (consumed once by
// LOOPNEZ into hardware LCOUNT/LBEG/LEND, not a general register after
// that) and is reused as the first per-pixel temp, so the trip count does
// not cost an extra register. Checked in xtensa-asm14/AnimMandala.S for a
// spill (see the round-2 report).
__attribute__((noinline)) static void mandalaBandFwd(uint16_t *dstPtr, const uint16_t *mapPtr, int count,
                                                       int gN_eff) {
    const uint32_t *rpb = rParams;
    const int16_t *s256 = sin256;
    const int16_t *sh256 = sinHalf256;
    const uint8_t *resc = rescaleLUT;
    const uint16_t *pal = paletteLUT;
    const uint16_t *mp = mapPtr;
    uint16_t *dp = dstPtr;
    int u1 = count;
    int u2, u3, u4;
    asm volatile(
        "loopnez %[u1], 2f\n"
        "l16ui   %[u2], %[mp], 0\n"       // u2 = entry
        "addi    %[mp], %[mp], 2\n"       // mp++ (Step = +1; fills entry's load-use slot)
        "extui   %[u1], %[u2], 0, 8\n"    // u1 = r (count is dead, already consumed by loopnez)
        "srli    %[u2], %[u2], 8\n"       // u2 = oct (entry is dead)
        "addx4   %[u1], %[u1], %[rpb]\n"  // u1 = &rParams[r]
        "mull    %[u2], %[u2], %[ge]\n"   // u2 = base = oct*gN_eff (halfOffset is always 0 here)
        "l32i    %[u3], %[u1], 0\n"       // u3 = params
        "extui   %[u2], %[u2], 0, 8\n"    // u2 = base truncated to 8 bits (fills params' load-use slot)
        "srli    %[u4], %[u3], 8\n"       // u4 = params>>8 (B in bits0..7; vig's bits cancel mod 256 below)
        "addx2   %[u4], %[u2], %[u4]\n"   // u4 = idxB_raw = base*2 + (params>>8)
        "add     %[u1], %[u2], %[u3]\n"   // u1 = idxA_raw = base + params
        "extui   %[u4], %[u4], 0, 8\n"    // u4 = idxB
        "addx2   %[u2], %[u4], %[sh256]\n" // u2 = &sinHalf256[idxB]
        "extui   %[u1], %[u1], 0, 8\n"    // u1 = idxA
        "l16si   %[u2], %[u2], 0\n"       // u2 = sinValB
        "addx2   %[u1], %[u1], %[s256]\n" // u1 = &sin256[idxA] (fills sinValB's load-use slot)
        "l16si   %[u1], %[u1], 0\n"       // u1 = sinValA
        "add     %[u2], %[resc], %[u2]\n" // u2 = rescaleLUT base + sinValB (fills sinValA's load-use slot)
        "add     %[u2], %[u2], %[u1]\n"   // u2 = &rescaleLUT[v] (v = sinValA+sinValB; +190 folded into the load)
        "l8ui    %[u1], %[u2], 190\n"     // u1 = rescaled v
        "extui   %[u2], %[u3], 16, 8\n"   // u2 = vig (params>>16, exact: params never sets bits above 23;
                                           // fills the rescale load's slot)
        "mull    %[u2], %[u2], %[u1]\n"   // u2 = rescaled v * vig
        "srli    %[u2], %[u2], 8\n"       // u2 = palette index
        "addx2   %[u2], %[u2], %[pal]\n"  // u2 = &paletteLUT[idx]
        "l16ui   %[u2], %[u2], 0\n"       // u2 = pixel
        "s16i    %[u2], %[dp], 0\n"       // store: the one unavoidable stall, see the comment above
        "addi    %[dp], %[dp], 2\n"
        "2:\n"
        : [mp] "+r"(mp), [dp] "+r"(dp), [u1] "+r"(u1), [u2] "=&r"(u2), [u3] "=&r"(u3), [u4] "=&r"(u4)
        : [rpb] "r"(rpb), [s256] "r"(s256), [sh256] "r"(sh256), [resc] "r"(resc), [pal] "r"(pal), [ge] "r"(gN_eff)
        : "memory");
}

// Same as mandalaBandFwd but for the left half (Step=-1, mapPtr walked
// backward) and carrying a real runtime halfOffset (g_sxOffset, 0 or 128 --
// see the pass-2 comment at the top); otherwise identical instruction for
// instruction, see mandalaBandFwd's comment for the reasoning.
__attribute__((noinline)) static void mandalaBandBack(uint16_t *dstPtr, const uint16_t *mapPtr, int count,
                                                        int gN_eff, int halfOffset) {
    const uint32_t *rpb = rParams;
    const int16_t *s256 = sin256;
    const int16_t *sh256 = sinHalf256;
    const uint8_t *resc = rescaleLUT;
    const uint16_t *pal = paletteLUT;
    const uint16_t *mp = mapPtr;
    uint16_t *dp = dstPtr;
    int u1 = count;
    int u2, u3, u4;
    asm volatile(
        "loopnez %[u1], 2f\n"
        "l16ui   %[u2], %[mp], 0\n"       // u2 = entry
        "addi    %[mp], %[mp], -2\n"      // mp-- (Step = -1)
        "extui   %[u1], %[u2], 0, 8\n"    // u1 = r
        "srli    %[u2], %[u2], 8\n"       // u2 = oct
        "addx4   %[u1], %[u1], %[rpb]\n"  // u1 = &rParams[r]
        "mull    %[u2], %[u2], %[ge]\n"   // u2 = oct*gN_eff
        "l32i    %[u3], %[u1], 0\n"       // u3 = params
        "add     %[u2], %[u2], %[ho]\n"   // u2 = base = oct*gN_eff + halfOffset (fills params' load-use slot)
        "extui   %[u2], %[u2], 0, 8\n"    // u2 = base truncated to 8 bits
        "srli    %[u4], %[u3], 8\n"       // u4 = params>>8
        "addx2   %[u4], %[u2], %[u4]\n"   // u4 = idxB_raw = base*2 + (params>>8)
        "add     %[u1], %[u2], %[u3]\n"   // u1 = idxA_raw = base + params
        "extui   %[u4], %[u4], 0, 8\n"    // u4 = idxB
        "addx2   %[u2], %[u4], %[sh256]\n" // u2 = &sinHalf256[idxB]
        "extui   %[u1], %[u1], 0, 8\n"    // u1 = idxA
        "l16si   %[u2], %[u2], 0\n"       // u2 = sinValB
        "addx2   %[u1], %[u1], %[s256]\n" // u1 = &sin256[idxA]
        "l16si   %[u1], %[u1], 0\n"       // u1 = sinValA
        "add     %[u2], %[resc], %[u2]\n" // u2 = rescaleLUT base + sinValB
        "add     %[u2], %[u2], %[u1]\n"   // u2 = &rescaleLUT[v]
        "l8ui    %[u1], %[u2], 190\n"     // u1 = rescaled v
        "extui   %[u2], %[u3], 16, 8\n"   // u2 = vig
        "mull    %[u2], %[u2], %[u1]\n"   // u2 = rescaled v * vig
        "srli    %[u2], %[u2], 8\n"       // u2 = palette index
        "addx2   %[u2], %[u2], %[pal]\n"  // u2 = &paletteLUT[idx]
        "l16ui   %[u2], %[u2], 0\n"       // u2 = pixel
        "s16i    %[u2], %[dp], 0\n"       // store: unavoidable stall, see mandalaBandFwd
        "addi    %[dp], %[dp], 2\n"
        "2:\n"
        : [mp] "+r"(mp), [dp] "+r"(dp), [u1] "+r"(u1), [u2] "=&r"(u2), [u3] "=&r"(u3), [u4] "=&r"(u4)
        : [rpb] "r"(rpb), [s256] "r"(s256), [sh256] "r"(sh256), [resc] "r"(resc), [pal] "r"(pal), [ge] "r"(gN_eff),
          [ho] "r"(halfOffset)
        : "memory");
}

// Device path: same shape as bandRef() (same two row-halves, same
// gN_eff/halfOffset algebra), reading polarMap directly instead of through
// a materialized scratch buffer -- see the round-2 top-of-file comment for
// why. Kept structurally parallel to bandRef() on purpose, down to the
// pointer arithmetic, so the two are easy to diff against each other.
void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const int cx = w / 2, cy = w / 2;
    const int mapDim = g_cx + 1;
    for (int ry = 0; ry < rows; ry++) {
        const int y = y0 + ry;
        const int dy = y - cy;
        const int ay = dy < 0 ? -dy : dy;
        const bool sy = dy < 0;
        uint16_t *row = dst + static_cast<size_t>(ry) * w;
        const uint16_t *mapRow = polarMap + static_cast<size_t>(ay) * mapDim;
        const int gN_eff_left = sy ? g_N : -g_N;
        const int gN_eff_right = sy ? -g_N : g_N;
        // Left half: see bandRef()'s identical comment.
        mandalaBandBack(row, mapRow + g_cx, cx, gN_eff_left, g_sxOffset);
        // Right half: see bandRef()'s identical comment.
        mandalaBandFwd(row + cx, mapRow, w - cx, gN_eff_right);
    }
}

#else

// Host / non-Xtensa builds: band() IS bandRef(), not merely equivalent to
// it, there is no second implementation to keep in sync.
void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) { bandRef(dst, y0, rows, w, tMs, p); }

#endif // __XTENSA__ && !GM_BGANIM_NO_ASM

void release() {
    const int mapDim = g_cx + 1;
    releaseTable(sin256, 256 * sizeof(int16_t));
    releaseTable(sinHalf256, 256 * sizeof(int16_t));
    releaseTable(sqrtLUT, 602 * sizeof(int16_t));
    releaseTable(recipLUT, 241 * sizeof(uint32_t));
    releaseTable(vigByR, static_cast<size_t>(mapDim));
    // The base pointer, not rescaleLUT, which is offset RESCALE_PAD into it.
    releaseTable(rescaleAlloc, RESCALE_N);
    rescaleLUT = nullptr;
    releaseTable(paletteLUT, 256 * sizeof(uint16_t));
    // Not releaseTable: polarMap comes from heap_caps_malloc(MALLOC_CAP_SPIRAM)
    // directly rather than through alloc(), so it is not on alloc()'s books
    // and crediting it back would corrupt the accounting the bench reads.
    if (polarMap != nullptr) {
        heap_caps_free(polarMap);
        polarMap = nullptr;
    }
    releaseTable(rParams, 256 * sizeof(uint32_t));
    // Both sentinels. tablesBuilt gates the one-time fills, so leaving it set
    // would hand back reallocated tables that nothing ever writes.
    tablesBuilt = false;
    lastThemeGen = 0xFFFFFFFF;
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
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
