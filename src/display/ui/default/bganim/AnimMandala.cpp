#ifndef GAGGIMATE_SIM

// "Mandala": N-fold rotational symmetry built from angular harmonics
// (sin(N*theta)), which are smooth and periodic by construction, no fold
// seams. Design: anim-geometric (Fable), 2026-08-15.
//
// Polar map optimization (2026-08-15): angle and radius are geometry-only,
// they never depend on time or params, so instead of running the fast
// atan2 poly + sqrt-bucket lookup per pixel per frame, we compute them once
// in init() into a quadrant-symmetric map and turn band() into table reads.
// The disc is mirrored across both axes (dx,dy -> |dx|,|dy|), so the map
// only needs one quadrant: (cx+1)^2 entries for a 480-wide panel (cx=240)
// is ~58k uint16 entries, ~115 KB. Each entry packs:
//   bits 15..8: angleOct, the octant-folded angle (0..64) for the point
//               (|dx|,|dy|), i.e. what fastAngleQ8 would return for a point
//               in the first quadrant (dx>=0, dy>=0).
//   bits 7..0:  radius (0..cx) if inside the inscribed circle, else the
//               sentinel 0xFF ("outside").
// band() looks up the quadrant entry via (|dx|,|dy|), then reconstructs the
// full 0..255 angle from angleOct + the two sign bits of (dx,dy), the same
// case split fastAngleQ8 used to do per pixel. See the pass-2 comment below
// for how that reconstruction was later made branch-free.
//
// Everything that is a function of radius alone but still depends on
// per-frame params (the radial phase offset `rOffset = (r*g_rOffsetScale)
// & 0xFF` folded into each harmonic's index, and the vignette x breathe
// scale) is baked into small (cx+1)-entry tables rebuilt once per frame in
// frame() so band() never multiplies/divides by a per-frame param, it
// just adds two table reads together. The final (v+190)*255/380 rescale
// (v is bounded -191..190 by construction) is likewise a fixed one-time
// 381-entry LUT built in init(). Net per pixel: a handful of table reads,
// a couple of adds/shifts, one angle multiply, no float, no divide, no
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
// *half*, no per-pixel branch at all. The exact per-pixel path this file
// used before the 2026-09-05 redesign below rendered each row as two
// straight runs (mirrored left half, direct right half) instead of one
// branchy loop, and walked the polar-map pointer with ++/-- instead of
// recomputing `ax` and its address each pixel (ax's stride equals the map's
// own element stride); mandalaIndex/mandalaRunSingle below still do, for
// the rows that still render that way.
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
// the earlier merge made, applied one level further. The portable spec and
// the on-device path both read rParams, so they cannot disagree about it.
//
// Assembly pass, round 2 (2026-09-04, same day, after device numbers came
// back): round 1's band() PIE-decoded polarMap 8 entries at a time into a
// scratch buffer, then drained the scratch with a separate hand-scheduled
// scalar pass, on the theory that batching the oct*gN_eff multiply into PIE
// (once per 8 pixels instead of once per pixel) would be a net win. On the
// device it measured SLOWER than the portable spec: 48.1ms vs 41.9ms per
// full frame with every table pinned to internal SRAM, i.e. with the
// PSRAM-placement question held fixed so the comparison is kernel vs
// kernel, not table vs table. Diffing xtensa-asm14's output for the
// compiler-generated loop against round 1's mandalaGatherFwd/Back found
// why: GCC's schedule pays exactly ONE load-use stall per pixel (the final
// palette-load-into-store, genuinely unavoidable, nothing independent is
// left to do at the end of an iteration), where round 1's hand kernel paid
// four. GCC gets there two ways round 1 missed: it never separately masks
// A or B out of `params`, it adds the WHOLE params register to base (for
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
// alignment requirement satisfied), exactly the "reorganised tables cost
// more in cache than they gain in the loop" pattern the round-2 brief
// flags across the fleet. This pass removes all of it: mandalaDecodePie,
// the scratch buffers, the octant/radius mask constants, and polarMap's
// column padding and hand-aligned allocation are gone; polarMap is back to
// a plain (cx+1)x(cx+1) table (see init()), and the left half of each row
// is back to starting at mapRow+g_cx and decrementing (no more mapRow+0
// workaround, since nothing here needs 16-byte alignment).
//
// What replaces it is a single hand-scheduled scalar pass, one pixel per
// LOOPNEZ iteration, that reads polarMap directly, structurally the same
// shape as the compiler-generated loop, applying GCC's own proven schedule
// (verified instruction for instruction against xtensa-asm14/AnimMandala.S's
// dump) by hand. The honest case for hand-asm here is not a smarter
// algorithm than GCC found, it isn't, GCC's schedule already hits the one
// truly unavoidable stall and nothing worse, it is the same schedule,
// called the same way the portable spec calls its own loop, measuring five
// static instructions per call lighter (xtensa-asm14: 40/41 vs 45/46, see
// the round-2 report). See mandalaBandFwd's comment (superseded by
// interpPairKernel below) for the register budget and the per-instruction
// reasoning it established.
//
// Round 5 (2026-09-04, device-in-the-loop via tools/kblob): kb.py confirmed
// band() and the portable spec still have the same min_ms on the bench
// board (both ride GCC's own schedule, see round 2 above), so there was no
// arithmetic edge left to take, and the round's brief pointed at
// first_ms/min_ms instead: polarMap is the only table this file reads from
// PSRAM per pixel, everything else (rParams, sin256, sinHalf256,
// rescaleLUT, paletteLUT) is already in the hot SRAM slab.
//
// Tried: at the top of each band() call, memcpy the one or two polarMap
// rows that call needs (mapDim entries, 482 B each) into a small hot-slab
// scratch buffer once, then have the per-pixel kernels read that copy
// instead of polarMap directly, same total bytes moved, but as one tight
// sequential copy loop (nothing but the load, the store and two pointer
// steps between iterations) instead of one PSRAM load sitting in the
// middle of ~20 instructions of per-pixel compute, on the theory that
// back-to-back requests pipeline through the PSRAM controller better than
// requests spaced out by unrelated arithmetic.
//
// Measured on the bench board, 7 kb.py runs before and after: blob's min_ms
// went from matching band() exactly (27.6x ms, both variants, every run) to
// a reproducible 27.65 -> 28.30 ms, +2.3%, with essentially no run-to-run
// spread on either side of that comparison, the extra copy is real,
// measurable overhead even when every table is already warm. first_ms did
// not move in compensation: band()'s own first_ms across those runs ranged
// 35.4-47.6 ms and blob's 35.4-54.0 ms, and the mean difference (blob minus
// band, paired within each run so both sides see the same board contention)
// was -1.7 ms with a swing from +17.9 to -14.6 ms run to run, indistin-
// guishable from the noise floor, not the clear win the theory predicted.
// Reverted; the diff is not in this file.
//
// Why the theory did not pay off, best guess: BgAnimCommon.h's own placement
// note says a PSRAM table swept sequentially already streams close to SRAM
// speed because the cache prefetches the run, and polarMap's access here
// was already sequential (mapPtr walked ++/-- one row at a time) before
// this pass touched anything. Decoupling the read from the per-pixel
// compute did not change the address stream the prefetcher sees, so there
// was no latency left to hide this way; the copy only added a second pass
// over the same bytes. This also means the round's opening framing
// (first_ms/min_ms 1.9x, "second worst in the fleet") did not reproduce
// here: the same board, same file, unmodified, measured 1.3x to 1.5x
// across repeats with the same wide run-to-run spread noted above, which
// reads more like a single high sample than a stable ratio.
//
// Net for round 5: band()'s per-pixel arithmetic was 27 instructions (5
// loads, 1 store, one unavoidable load-use stall on the last), matching
// min_ms's ~28.8 measured cycles/pixel almost exactly and already at GCC's
// own proven schedule, within ASM_BRIEF.md's ~25 cycles/pixel target by
// about 15%, with no slack in that schedule left to spend on anything
// else. Combined with the memory experiment above coming back negative,
// the file did not move that round.
//
// Redesign, 2026-09-05 (design-mandala): round 5 left no arithmetic edge on
// the exact per-pixel path and no win in moving tables around, so the only
// lever left was fewer samples, not a cheaper sample. Below, two of a 2x2
// output block's four pixels are still exact evaluations of the polar
// chain (its top-left corner and every other one to its right); the other
// two are linear blends of the pre-palette magnitude, never of the palette
// colour or the wrapping polar angle, so the interpolation cannot tear
// across a phase wrap. The 64x64 center square (INNER_BAND rows by
// INNER_HALF_W columns either side of center, where curvature and the
// resampling grain were both worst) is redrawn at full resolution on top
// of the halved render, and a per-frame guard (WRAP_GUARD, below)
// falls back to full resolution for the whole frame once complexity pushes
// the ring wavelength close enough to the block pitch to alias into a
// moire crosshatch; that guard's threshold was checked again for this
// design and holds safely at 24, the highest value g_rOffsetScale actually
// reaches. A one-row cache (g_sampleCache/fetchRow, below) carries a
// sampled row's magnitudes forward so a contiguous run of block-pairs
// samples each row once, not twice; every call shape interlace_check
// exercises (solitary rows, out-of-parity sequences, every band height)
// has to produce the same pixels whether or not that cache happens to be
// warm, and --shapes confirms it does. A second lever, tried after the
// first landed: the per-pixel chain reads one PSRAM table (polarMap) per
// pixel, and a quarter-resolution copy of it (polarMapQuarter, below),
// holding only the even/even entries every fast path samples, cuts that
// read to a quarter of its bytes and cache lines. It helped, but by less
// than hoped, most of the remaining band time is the per-sample arithmetic
// itself, not the PSRAM sweep. Production band time (the live render loop,
// not kbench, which cannot honestly time this design, see fetchRow's
// comment) went from 42.9 ms for the exact per-pixel design to 22.9 ms for
// the interpolated design shipped below. Two cheaper variants were
// measured and not shipped: a probe that interpolates only a block's
// horizontal midpoint and duplicates the bottom row from the top (21.3 ms),
// and a reconstruction of an earlier duplication design that samples once
// per block and duplicates all four pixels (17.1 ms). Both were faster,
// and both looked worse: the duplication reconstruction's rings came out
// rough and stair-stepped where the original is smooth, and the
// horizontal-only probe kept that same vertical stepping on one axis. The
// user picked the interpolated design's picture over the faster options'
// streaking.
#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

namespace {
using namespace bganim;

int16_t *sin256 = nullptr;      // Q7 sine, 256 entries
int16_t *sinHalf256 = nullptr;  // sin256[i]>>1 precomputed, 256 entries, saves a per-pixel shift
int16_t *sqrtLUT = nullptr;     // r2 bucket -> radius (bucket 192); init-time only
uint32_t *recipLUT = nullptr;   // Q16 65536/(i+1), 0..240; init-time only
uint8_t *vigByR = nullptr;      // radius 0..cx -> vignette falloff Q8, indexed directly by radius
// v = sin256[idxA] + sinHalf256[idxB]. sin256 is Q7 with |sin256| <= 127, and
// sinHalf256[i] = sin256[i] >> 1, an ARITHMETIC shift, which rounds toward
// negative infinity, so -127 >> 1 is -64, not -63. The true range of v is
// therefore [-191, 190], not the symmetric [-190, 190] an earlier comment
// here claimed, and the unpadded 381-entry table was read one byte off its
// front (caught by tools/animbench/fuzz under ASan). Rather than re-derive
// the offset, pad both ends and clamp the padding entries, which keeps
// every in-range value bit-identical and costs 8 bytes.
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
// Every fast-path sample (every halved-scheme sample) reads polarMap
// only at even ax and even ay: the row is always an even absolute output
// row, and the column walk starts aligned and steps by two. polarMapQuarter
// holds exactly those entries, (cx/2+1)^2 of them (about 29 KB against
// polarMap's 116 KB at the panel's fixed width), so a fast-path row sweeps
// a quarter of the bytes, cache lines and PSRAM-read misses that same row
// costs out of the full map. Filled at init() as a copy of polarMap's own
// values at the matching (ax,ay) pairs, never recomputed independently, so
// the two maps cannot disagree. patchCentre (the center square) and
// computeRowFull (every row once g_fineDetail engages) still read polarMap
// directly: they sample odd coordinates too, which polarMapQuarter does
// not have.
uint16_t *polarMapQuarter = nullptr;
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
// local in init(): vigByR, polarMap and polarMapQuarter are sized from cx,
// so a resolution change has to rebuild them, and release() can only reset
// this flag if it can see it. While it was function-local, init() would
// reallocate the tables on a resolution change and then skip filling them.
bool tablesBuilt = false;
int g_cx = 240; // half panel width; also the map's per-axis extent (assumes cx < 255)

// One sampled row of pre-palette magnitudes, carried forward so a row-pair
// does not resample the row the pair below it will also need: keyed by
// which absolute row it holds and the width it was sampled at (a 240-wide
// call splits and orders the blocks differently from a 480-wide one; see
// AnimAurora.cpp's g_rowLUT/g_lastBgIdx for the same idiom), checked
// before every use and recomputed on any mismatch by fetchRow below,
// never assumed valid. frame() resets
// g_sampleCacheY to the sentinel on every call, since that is the only
// thing that changes what sampleRowIndices(y) returns for a fixed y
// (rParams and g_N advance there; the tables sampleRowIndices reads
// otherwise are set once at init()). Sized off g_cx at init() (g_cx bytes
// is always >= w/2 for a call whose w matches init's).
constexpr int NO_CACHED_ROW = INT32_MIN;
uint8_t *g_sampleCache = nullptr;
int g_sampleCacheY = NO_CACHED_ROW;
int g_sampleCacheW = 0;

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
    // sin256 non-null, so the next call skipped the whole block, retry and
    // check alike, and fell through to `return true` with null tables that
    // frame() and band() dereference without checking.
    g_cx = w / 2;
    const int mapDim = g_cx + 1;
    // rParams, sin256, sinHalf256 and rescaleLUT are read once per real
    // polar-chain evaluation (mandalaIndex below, called by every write*
    // function plus patchCentre for the center square); paletteLUT is read
    // once per OUTPUT pixel regardless, since every pixel keeps its own
    // gather even when its magnitude is interpolated (see the header's
    // 2026-09-05 paragraph). Both categories run well into the tens of
    // thousands of reads per frame even outside the center square, the "reads per
    // frame, not size" criterion BgAnimCommon.h's allocHot() comment asks
    // for. Combined with g_sampleCache (up to g_cx bytes, read by fetchRow's
    // cache and by interpPairKernel's block loop) they are 256*4 (rParams) +
    // 256*2 (sin256) + 256*2 (sinHalf256) + RESCALE_N (389, rescaleLUT) +
    // 256*2 (paletteLUT) + up to 240 (g_sampleCache) = 3,189 B, comfortably
    // inside the 9,216 B per-animation slab. sqrtLUT, recipLUT and vigByR
    // below stay on alloc() (PSRAM): all three are read only at
    // init()/frame() time (once per (cx+1)^2 map-fill pass, or once per
    // cx+1 per frame), never from the per-pixel path, so they do not
    // compete for the slab. allocHot() returns nullptr when the slab is
    // full; alloc() (PSRAM) is the explicit fallback, same pattern as
    // AnimSilk.cpp's g_lut, so a full slab degrades this animation instead
    // of failing its init().
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
    // tiny span tables prefer SRAM"), polarMap is the snapshot-sized
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
    // PSRAM, same reasoning as polarMap above; see polarMapQuarter's
    // declaration comment for why a quarter-size copy is enough.
    const int qDim = g_cx / 2 + 1;
    if (polarMapQuarter == nullptr) {
        polarMapQuarter = static_cast<uint16_t *>(
            heap_caps_malloc(static_cast<size_t>(qDim) * qDim * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
    }
    // 256, not mapDim: index 0xFF is the map's outside-disc sentinel and is
    // read unconditionally by mandalaIndex on every real polar-chain
    // evaluation, from both the portable spec and band()'s own row
    // classification (interpPairKernel itself only reads paletteLUT, not
    // rParams). Per-pixel set, see the allocHot comment above.
    if (rParams == nullptr) {
        rParams = static_cast<uint32_t *>(allocHot(256 * sizeof(uint32_t)));
        if (rParams == nullptr) {
            rParams = static_cast<uint32_t *>(alloc(256 * sizeof(uint32_t)));
        }
    }
    // This file's own per-pixel-adjacent set: read by fetchRow's cache and,
    // via its aliasing pointers, by interpPairKernel's block loop. Sized
    // off g_cx (<=240), not a fixed 256, since it only ever needs one row's
    // worth of blocks.
    if (g_sampleCache == nullptr) {
        g_sampleCache = static_cast<uint8_t *>(allocHot(static_cast<size_t>(g_cx)));
        if (g_sampleCache == nullptr) {
            g_sampleCache = static_cast<uint8_t *>(alloc(static_cast<size_t>(g_cx)));
        }
    }
    g_sampleCacheY = NO_CACHED_ROW;
    if (sin256 == nullptr || sinHalf256 == nullptr || sqrtLUT == nullptr || recipLUT == nullptr || vigByR == nullptr ||
        rescaleLUT == nullptr || paletteLUT == nullptr || polarMap == nullptr || polarMapQuarter == nullptr ||
        rParams == nullptr || g_sampleCache == nullptr) {
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
                const uint16_t entry = static_cast<uint16_t>((static_cast<uint16_t>(oct) << 8) | rOrOut);
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

int g_N = 8, g_tOffA = 0, g_tOffB = 0, g_rOffsetScale = 0;
int g_breatheQ8 = 256;
int g_sxOffset = 0; // (g_N & 1) ? 128 : 0, see the pass-2 comment at the top

// g_rOffsetScale is the per-unit-radius phase step (256ths of a turn) the
// A/B harmonics advance by, so 256/g_rOffsetScale is roughly the radial
// wavelength in pixels of the ring pattern. Above WRAP_GUARD that
// wavelength gets close enough to the halved scheme's 2-pixel column pitch
// that review caught a moire cross-hatch at complexity=100 on an earlier
// duplication design that also sub-sampled the map. g_fineDetail, set once
// per frame below, is the guard: bandRef() renders the whole frame at full
// resolution instead of aliasing when it is set. g_rOffsetScale tops out at
// 24 (complexity=100).
//
// That duplication design's own threshold was 16 (first engaging at
// complexity 59), from a diff-vs-complexity sweep with the guard disabled.
// The same sweep against the interpolated design shipped below, run with
// the guard held off across the whole range, climbs smoothly and stays
// well under the design brief's 2.5 ceiling: mean diff 1.57 at complexity
// 60 (where duplication's sweep needed the guard), 2.06 at complexity 100,
// no discontinuity anywhere in between. Interpolation does not have
// duplication's failure mode: a zero-order hold aliases a ring spacing
// below its sampling pitch into a spurious crosshatch frequency, where a
// linear blend instead just softens the ring edge, so there is no sudden
// threshold to find. Full-frame renders and zoomed crops at complexity 100
// across five different time offsets, including the region just outside
// the center square where the ring wavelength is shortest, show rougher ring
// edges but no crosshatch. That was checked only in static frames, not
// live motion, so 24 (the highest g_rOffsetScale actually reaches, engaging
// only at complexity 99-100) keeps a one-step margin at the extreme this
// sweep is least certain about, rather than removing the guard outright.
// Checked once per frame, so it costs nothing until complexity is turned up
// far enough to need it, and nothing at the animation's default
// (complexity=45, g_rOffsetScale=9).
constexpr int WRAP_GUARD = 24;
bool g_fineDetail = false;

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
    g_fineDetail = g_rOffsetScale >= WRAP_GUARD;
    // 40.74 = 256 ticks per 2*pi radians
    g_tOffA = static_cast<int>(t * 1.4f * 40.74f) & 0xFF;
    g_tOffB = static_cast<int>(t * 0.8f * 40.74f) & 0xFF;
    g_breatheQ8 = static_cast<int>((0.82f + 0.18f * fastSinRad(t * 0.45f)) * 256.0f);
    // rParams (below) is about to change, which is everything
    // sampleRowIndices reads that varies frame to frame; drop any cached
    // row so the next fetchRow call for it recomputes rather than reusing
    // last frame's magnitude.
    g_sampleCacheY = NO_CACHED_ROW;

    // Fold everything that is "per-frame param x radius" but not per-pixel
    // into a (cx+1)-entry table, the per-pixel chain below then just reads
    // and adds. Cheap: g_cx+1 (<=241) iterations, once per frame, plain
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

// This file's per-pixel chain up to but not including the palette gather,
// factored out so it can be cached and interpolated on its own (see
// sampleRowIndices below) as well as fed straight through to a pixel.
// Bit-identical to the exact per-pixel path this file used before the
// 2026-09-05 interpolation redesign (see the header) up to the gather:
// base, idxA and idxB truncate the same way through uint8_t, and the
// outside-disc case falls out for free the same way (rParams[0xFF]'s vig
// field is 0, set once in init(), so v collapses to 0 and paletteLUT[0] is
// g_outside). v is a magnitude in [0, 254]: rescaleLUT and vig are both
// uint8_t, so the product shifted down by 8 cannot reach 255. It never
// wraps as a function of position, unlike oct or the raw sin table
// indices, which is what makes it safe to linearly interpolate.
inline int mandalaIndex(int oct, int r, int gN_eff, int halfOffset) {
    const int base = halfOffset + oct * gN_eff;
    const uint32_t params = rParams[r];
    const auto A = static_cast<uint8_t>(params);
    const auto B = static_cast<uint8_t>(params >> 8);
    const auto vig = static_cast<uint8_t>(params >> 16);
    const uint8_t idxA = static_cast<uint8_t>(base + A);
    const uint8_t idxB = static_cast<uint8_t>(2 * base + B);
    int v = sin256[idxA] + sinHalf256[idxB]; // -191..190
    v = rescaleLUT[v + 190];
    v = (v * vig) >> 8;
    return v;
}

inline uint16_t mandalaPixel(int oct, int r, int gN_eff, int halfOffset) {
    return paletteLUT[mandalaIndex(oct, r, gN_eff, halfOffset)];
}

// Same walk, one column at a time, for the leftover column when a half's
// width is odd. Never exercised at the panel's fixed 480x480 (cx=240 is
// even both halves), kept only so band() stays correct if w ever changes.
template <int Step>
void mandalaRunSingle(uint16_t *dstPtr, const uint16_t *mapPtr, int count, int gN_eff, int halfOffset) {
    for (int i = 0; i < count; i++) {
        const uint16_t entry = *mapPtr;
        mapPtr += Step;
        const int r = entry & 0xFF;
        const int oct = entry >> 8;
        dstPtr[i] = mandalaPixel(oct, r, gN_eff, halfOffset);
    }
}

// Same walk again, but storing the pre-palette magnitude into a plain
// array at one entry per iteration (no pixel store, no duplication): this
// is the sampling half of the interpolation scheme below.
template <int Step>
void mandalaIndexRun(uint8_t *dstPtr, const uint16_t *mapPtr, int count, int gN_eff, int halfOffset) {
    for (int i = 0; i < count; i++) {
        const uint16_t entry = *mapPtr;
        mapPtr += Step;
        const int r = entry & 0xFF;
        const int oct = entry >> 8;
        dstPtr[i] = static_cast<uint8_t>(mandalaIndex(oct, r, gN_eff, halfOffset));
    }
}

// Widest row this file ever samples at half resolution: w/2 for the
// panel's fixed 480 width is 240; 256 leaves headroom without needing a
// heap size computed from a width not yet known at compile time.
constexpr int MAX_BLOCKS = 256;

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
// that formula exactly. Assumes cx and w-cx are both even, true for the
// panel's fixed 480 width; an odd remainder is not handled here, the same
// "never exercised at 480x480" assumption mandalaRunSingle's own remainder
// path documents.
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
    mandalaIndexRun<-1>(idxOut, mapRow + g_cx / 2, nBlocksL, gN_eff_left, g_sxOffset);
    const int nBlocksR = (w - cx) / 2;
    mandalaIndexRun<+1>(idxOut + nBlocksL, mapRow, nBlocksR, gN_eff_right, 0);
}

// Returns nBlocks valid magnitudes for absolute row y, from the cache when
// its key matches, freshly sampled otherwise. The returned pointer aliases
// g_sampleCache: a caller that still needs this row's data after a later
// fetchRow call (which may sample a different row into the same buffer)
// must copy it out first, as the paired-row path below does.
//
// This cache is also why kbench cannot honestly time this design. kbench
// calls one band repeatedly to take the minimum of n runs, so every repeat
// after the first asks fetchRow for the same y a prior repeat of the SAME
// call already cached, a cache hit that never happens in production, where
// consecutive bands advance y and each pair's own fetchRow(y) is a fresh
// row the previous pair never sampled. That understates the real cost by a
// whole sampling pass. This design's real cost is the production-order
// useblob A/B (camshots/anim_devbench.py, KNOB=useblob against a resident
// kb.py blob), not kbench's min_ms; see the header's 2026-09-05 paragraph
// for those numbers.
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
void writeInterpTop(uint16_t *row, const uint8_t *a, int nBlocks) {
    for (int b = 0; b < nBlocks; b++) {
        const int va = a[b];
        const int vb = (b + 1 < nBlocks) ? a[b + 1] : va;
        row[2 * b] = paletteLUT[va];
        row[2 * b + 1] = paletteLUT[(va + vb) >> 1];
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
void writeInterpBottom(uint16_t *row, const uint8_t *a, const uint8_t *c, int nBlocks) {
    for (int b = 0; b < nBlocks; b++) {
        const int va = a[b];
        const int vb = (b + 1 < nBlocks) ? a[b + 1] : va;
        const int vc = c[b];
        const int vd = (b + 1 < nBlocks) ? c[b + 1] : vc;
        const int hMidTop = (va + vb) >> 1;
        const int hMidBot = (vc + vd) >> 1;
        row[2 * b] = paletteLUT[(va + vc) >> 1];
        row[2 * b + 1] = paletteLUT[(hMidTop + hMidBot) >> 1];
    }
}

// Both rows of one block-pair in a single pass: hMidTop (the top row's own
// horizontal midpoint) is computed once here and reused for the bottom
// row's centre pixel, rather than once in writeInterpTop and again in
// writeInterpBottom. Each store is the pair's own two columns combined
// into one 32-bit write, the same trick the exact per-pixel paths use.
void writeInterpPair(uint16_t *rowTop, uint16_t *rowBot, const uint8_t *a, const uint8_t *c, int nBlocks) {
    for (int b = 0; b < nBlocks; b++) {
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
// band() below only calls this kernel when nBlocks >= 2, which is every
// real call (nBlocks is w/2, 120 or 240 for the panel's fixed widths).
__attribute__((noinline)) static void interpPairKernel(uint16_t *rowTop, uint16_t *rowBot, const uint8_t *a,
                                                         const uint8_t *c, const uint16_t *palette, int nBlocks) {
    int aCur = a[0];
    int cCur = c[0];
    const uint8_t *aPtr = a + 1;
    const uint8_t *cPtr = c + 1;
    int aNext, cNext, hMidTop, hMidBot, p0, t;
    int p1 = nBlocks - 1;
    asm volatile("loop %[p1], 2f\n"
                 // Load next block's two real samples; independent of each
                 // other, so the second load fills the first's stall.
                 "l8ui  %[aNext], %[aPtr], 0\n"
                 "l8ui  %[cNext], %[cPtr], 0\n"
                 "addi  %[aPtr], %[aPtr], 1\n"
                 "addi  %[cPtr], %[cPtr], 1\n"
                 "add   %[t], %[aCur], %[aNext]\n"
                 "srli  %[hMidTop], %[t], 1\n"
                 "add   %[t], %[cCur], %[cNext]\n"
                 "srli  %[hMidBot], %[t], 1\n"
                 // pTL gather.
                 "addx2 %[t], %[aCur], %[pal]\n"
                 "addx2 %[p1], %[hMidTop], %[pal]\n"
                 "l16ui %[p0], %[t], 0\n"
                 // pBL address, independent of p0: fills pTL's load-use gap.
                 "add   %[t], %[aCur], %[cCur]\n"
                 "srli  %[t], %[t], 1\n"
                 // pTR gather (base was set two instructions above).
                 "l16ui %[p1], %[p1], 0\n"
                 "addx2 %[t], %[t], %[pal]\n"
                 "slli  %[p1], %[p1], 16\n"
                 "or    %[p1], %[p1], %[p0]\n"
                 "s32i  %[p1], %[rowTop], 0\n"
                 // pBL gather.
                 "l16ui %[p0], %[t], 0\n"
                 // pBR address, independent of p0: fills pBL's load-use gap.
                 "add   %[t], %[hMidTop], %[hMidBot]\n"
                 "srli  %[t], %[t], 1\n"
                 "addx2 %[t], %[t], %[pal]\n"
                 // pBR gather.
                 "l16ui %[p1], %[t], 0\n"
                 "addi  %[rowTop], %[rowTop], 4\n"
                 "slli  %[p1], %[p1], 16\n"
                 "or    %[p1], %[p1], %[p0]\n"
                 "s32i  %[p1], %[rowBot], 0\n"
                 "addi  %[rowBot], %[rowBot], 4\n"
                 // Carry this iteration's next samples forward as the next
                 // iteration's current ones (move-via-or, no spare mov).
                 "or    %[aCur], %[aNext], %[aNext]\n"
                 "or    %[cCur], %[cNext], %[cNext]\n"
                 "2:\n"
                 // Last block, outside the loop so its body stays
                 // branch-free: aCur/cCur already hold a[nBlocks-1] and
                 // c[nBlocks-1] (the final carry-forward above, or the
                 // initial load when nBlocks==1). Clamped, hMidTop==aCur
                 // and hMidBot==cCur, so pTR==pTL and pBR==pBL: two
                 // gathers, each written twice.
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

// Full-resolution row: same per-pixel evaluation as mandalaRunSingle above,
// both row-halves, with no block interpolation. Bit-identical to the exact
// per-pixel path this file used before the 2026-09-05 interpolation
// redesign for this one row. Used only while g_fineDetail is set (see
// WRAP_GUARD); patchCentre below is the same walk over the center square.
void computeRowFull(uint16_t *row, int y, int w) {
    const int cx = w / 2, cy = w / 2;
    const int mapDim = g_cx + 1;
    const int dy = y - cy;
    const int ay = dy < 0 ? -dy : dy;
    const bool sy = dy < 0;
    const uint16_t *mapRow = polarMap + static_cast<size_t>(ay) * mapDim;
    const int gN_eff_left = sy ? g_N : -g_N;
    const int gN_eff_right = sy ? -g_N : g_N;
    mandalaRunSingle<-1>(row, mapRow + g_cx, cx, gN_eff_left, g_sxOffset);
    mandalaRunSingle<+1>(row + cx, mapRow, w - cx, gN_eff_right, 0);
}

// Pixels within INNER_BAND rows and INNER_HALF_W columns of the center
// render at full resolution instead of the halved scheme. This is where
// curvature is highest (the wavelength of the N-fold pattern shrinks toward
// the center, see the top-of-file comment on aliasing), and it is where the
// 2x2 resampling grain was most visible against the goldens (the innermost
// rings showing octagonal faceting the exact per-pixel design renders as
// smooth curves).
//
// The exact region is a square, not a full-width band. The first cut of
// this redesign rendered every row within INNER_BAND of the center at full
// resolution across the whole width, and that was the production frame's
// single biggest cost: 64 rows of 480 at up to 4x the halved scheme's
// per-pixel price, with all but the middle 64 pixels of each row lying as
// far from the center as rows the halved scheme already handles. On the
// board that design measured 23.6 ms per frame and landed on 58 ms frames
// (three panel refreshes), the same pace as the exact design it replaced;
// the interpolation had bought band time and no frame rate. So every row
// is now rendered by the halved scheme first and the center square is
// redrawn exactly on top (patchCentre, below). Inside the square nothing
// changed; the outer part of those 64 rows now carries the same grain as
// the rest of the frame.
constexpr int INNER_BAND = 32;
constexpr int INNER_HALF_W = 32;

// Redraw the center square's share of one row at full resolution, when the
// row has one. Same walk as computeRowFull, restricted to the INNER_HALF_W
// columns either side of the vertical center line: the left run walks the
// map inward toward index (g_cx - cx), the right run walks outward from
// index 0, so the pixels written are exactly the ones computeRowFull would
// have written at those columns. Rows outside INNER_BAND return at once.
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
    mandalaRunSingle<-1>(row + cx - k, mapRow + (g_cx - cx + k), k, gN_eff_left, g_sxOffset);
    mandalaRunSingle<+1>(row + cx, mapRow, k, gN_eff_right, 0);
}

// bandRef is the portable spec for this design and the one place
// interlace_check's call-shape correctness is proven, since host builds
// never see interpPairKernel's asm. band() below dispatches to that kernel
// on an Xtensa device build; every other build (host tests, sanitizers, or
// GM_BGANIM_NO_ASM) routes straight here.
//
// g_fineDetail (set once per frame in frame()) bypasses every saving in
// this file and renders exactly like the exact per-pixel design, for the
// reason WRAP_GUARD's comment gives; this only fires at high complexity
// settings, so it costs nothing at the animation's defaults.
//
// Otherwise every row goes through the halved scheme, and rows inside
// INNER_BAND then get their center square redrawn exactly (patchCentre).
// When a call actually holds both rows of a block-pair (the ordinary case:
// an even row followed by its odd partner), writeInterpPair renders both in
// one pass so the top row's own horizontal midpoint is computed once, not
// twice. When it does not (a call that starts or ends on an unpaired row),
// each row falls back to computing itself from fetchRow directly. Either
// path relies on fetchRow's cache the same way: the row a pair samples as
// "two below" is exactly the row the next pair needs as its own top, so
// across a contiguous run each absolute row is sampled once, not twice.
// The patch comes after the interpolated write of the same row, never
// before, since the pair write covers the whole row.
void bandRef(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const int nBlocks = w / 2;
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
            // fetchRow(y+2, ...) may reuse the same backing buffer
            // fetchRow(y, ...) just returned, so the top row's samples
            // are copied out before that second fetch runs (the same
            // rule the solo odd-row path below follows).
            uint8_t top[MAX_BLOCKS];
            memcpy(top, fetchRow(y, w), static_cast<size_t>(nBlocks));
            const uint8_t *bottom = fetchRow(y + 2, w);
            writeInterpPair(row, row + w, top, bottom, nBlocks);
            patchCentre(row, y, w);
            patchCentre(row + w, y + 1, w);
            ry += 2;
            continue;
        }
        if (y & 1) {
            uint8_t top[MAX_BLOCKS];
            memcpy(top, fetchRow(y - 1, w), static_cast<size_t>(nBlocks));
            const uint8_t *bottom = fetchRow(y + 1, w);
            writeInterpBottom(row, top, bottom, nBlocks);
        } else {
            writeInterpTop(row, fetchRow(y, w), nBlocks);
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
// the hand-asm kernel described on its own comment. nBlocks < 2 is defensive only,
// never taken on device (nBlocks is w/2, 120 or 240 for the panel's fixed
// widths): the kernel's `loop` needs a trip count of nBlocks-1 and does
// not itself check for zero, so that case falls back to the C++ path
// instead, the same shape AnimCaustics.cpp's own precondition check takes.
// Non-Xtensa builds (host tests, sanitizers) and any build with
// GM_BGANIM_NO_ASM route straight to bandRef instead, which contains the
// identical loop.
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    const int nBlocks = w / 2;
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
            if (nBlocks < 2) {
                writeInterpPair(row, row + w, top, bottom, nBlocks);
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
            writeInterpBottom(row, top, bottom, nBlocks);
        } else {
            writeInterpTop(row, fetchRow(y, w), nBlocks);
        }
        patchCentre(row, y, w);
        ry++;
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
    // Not releaseTable: polarMap and polarMapQuarter come from
    // heap_caps_malloc(MALLOC_CAP_SPIRAM) directly rather than through
    // alloc(), so neither is on alloc()'s books and crediting them back
    // would corrupt the accounting the bench reads.
    if (polarMap != nullptr) {
        heap_caps_free(polarMap);
        polarMap = nullptr;
    }
    if (polarMapQuarter != nullptr) {
        heap_caps_free(polarMapQuarter);
        polarMapQuarter = nullptr;
    }
    releaseTable(rParams, 256 * sizeof(uint32_t));
    releaseTable(g_sampleCache, static_cast<size_t>(g_cx));
    // Three sentinels. tablesBuilt gates the one-time fills, so leaving it
    // set would hand back reallocated tables that nothing ever writes;
    // g_sampleCacheY must not survive into the next init() either, since a
    // stale row number could false-hit fetchRow's cache against a freshly
    // reallocated (and unfilled) g_sampleCache.
    tablesBuilt = false;
    lastThemeGen = 0xFFFFFFFF;
    g_sampleCacheY = NO_CACHED_ROW;
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
