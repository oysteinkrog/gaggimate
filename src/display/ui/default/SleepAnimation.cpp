#ifndef GAGGIMATE_SIM

#include "SleepAnimation.h"
#include <Arduino.h>
#include <display/drivers/common/Display.h>
#include <display/ui/default/bganim/BgAnim.h>
#include <esp_cache.h> // esp_cache_msync, around the direct push
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <math.h>
#include <string.h>  // memmove, for the round-panel band compaction

// Stage timers for the bench build. These compile to nothing in a normal
// build, so the shipping render path carries no measurement overhead.
#ifdef GM_ANIM_BENCH
#define BENCH_T0(v) const int64_t v = esp_timer_get_time()
#define BENCH_ACC(acc, t0) (acc) += static_cast<uint64_t>(esp_timer_get_time() - (t0))
// How long to sit on each animation before recording it. Long enough that the
// mean is not dominated by the first frames, where the lazy LUT init runs.
constexpr unsigned long BENCH_DWELL_MS = 6000;
#define GM_BENCH_LOCK_ONE_BAND 1
#else
#define BENCH_T0(v) ((void)0)
#define BENCH_ACC(acc, t0) ((void)0)
// Constant-false, so the suspend branch in renderFrame folds away entirely in
// shipping builds rather than being compiled and never taken.
#define GM_BENCH_LOCK_ONE_BAND 0
#endif

namespace {
// Rows rendered/pushed per chunk, and how many of those chunks are in flight.
// Push cost is per byte rather than per call, so band height does not change
// what a frame costs to push -- but it does set the handoff count, and 8 rows
// (60 cross-core semaphore round trips per frame instead of 30) cost ~1.3 ms
// and dropped plasma 43.0 -> 40.6 fps.
//
// 8 rows across two slots is chosen against internal SRAM, which is the
// binding constraint: internal SRAM is what WiFi/BLE/TLS draw from at runtime,
// and the web server needs a contiguous 2,872 B of it per send round. 8 x 2 is
// 15,360 B; the handoff cost above is the price of the row count, and the slot
// count is argued separately at NUM_SLOTS in the header.
//
// Note on the fps figure quoted above: it comes from a 16-vs-8 bench, which is
// where the 30-vs-60 handoff counts come from. The 12-vs-8 step this constant
// actually took has never been benched directly; scaling the measured number
// linearly puts it nearer 0.9 ms than 1.3 ms, so 40.6 fps is a conservative
// floor rather than a measurement of the current configuration.
constexpr int BAND_H = 8;
// Headroom for the snapshot's ext draw size (shadows etc. extend the render
// area past the object on every side).
constexpr int OVERLAY_EXT_MARGIN = 16;
// How many runs a single row's list can hold before emitRun starts merging.
// 24 is well past what the standby widgets produce; a row through the clock and
// both icons emits about eight.
constexpr int RUNS_PER_ROW = 24;
// Runs closer together than this are emitted as one. A run record costs a load,
// two extracts and a loop setup, which is more than the handful of transparent
// pixels the blend will skip, and antialiased text puts one- and two-pixel gaps
// everywhere.
constexpr int RUN_GAP_MERGE = 4;
// The same idea for the halo runs, but in cells, so a merged gap is four times
// as wide in pixels. One cell, because a merged gap here is not a few skipped
// pixels: every cell in it gets read, scaled and written back.
constexpr int HALO_GAP_MERGE_CELLS = 1;
// The scrim factor is stored as 32nds of full brightness, so 32 means "leave
// this cell alone". Two smoothing passes leave a wide skirt of cells whose dim
// rounds away to nothing, and those are dropped from the runs entirely.
constexpr int SCRIM_INV_NONE = 32;

// Scrim grid resolution: 1 << 2 = one cell per 4x4 panel pixels.
constexpr int SCRIM_SHIFT = 2;
// How far the halo reaches past the outermost widget pixel, in cells: two
// 3-wide max passes carry coverage two cells out, and the 3-tap smoothing pass
// carries a fraction of it one further. 3 cells is 12 pixels.
constexpr int SCRIM_REACH_CELLS = 3;
// One separable 3-tap pass over a byte grid, either a max (dilate) or a
// 1-2-1 average (smooth), with the edges clamped rather than wrapped.
//
// `lineStep`/`step` are what let one function do both directions: horizontally
// a line is a row (lineStep = grid width, step = 1), vertically a line is a
// column (lineStep = 1, step = grid width). Six calls build the whole field, so
// this is the only place the halo shape is defined.
void scrimTap3(const uint8_t *src, uint8_t *dst, int lines, int n, int lineStep, int step, bool useMax) {
    for (int l = 0; l < lines; l++) {
        const uint8_t *sp = src + static_cast<size_t>(l) * lineStep;
        uint8_t *dp = dst + static_cast<size_t>(l) * lineStep;
        for (int i = 0; i < n; i++) {
            const int a = sp[static_cast<size_t>(i > 0 ? i - 1 : 0) * step];
            const int b = sp[static_cast<size_t>(i) * step];
            const int c = sp[static_cast<size_t>(i < n - 1 ? i + 1 : n - 1) * step];
            int v;
            if (useMax) {
                v = a > b ? a : b;
                if (c > v) {
                    v = c;
                }
            } else {
                v = (a + 2 * b + c) >> 2;
            }
            dp[static_cast<size_t>(i) * step] = static_cast<uint8_t>(v);
        }
    }
}

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Per-channel RGB565 alpha blend (a: 0..255 foreground opacity).
// always_inline rather than plain inline: at -O2 GCC has repeatedly declined to
// inline same-file helpers in this codebase, and a real call in a per-pixel loop
// costs a windowed-ABI register rotation on top of the call itself. The /255
// divisions are not divisions -- GCC strength-reduces each to a multiply-high
// and a shift, confirmed in the disassembly.
__attribute__((always_inline)) inline uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t a) {
    // One lane per channel, because the packed red+blue lane this replaces was
    // wrong. That form multiplied (c & 0xF81F) by an 8-bit alpha and relied on
    // the two fields staying clear of each other. They do not: blue's product
    // reaches 31 * 256 = 7936, thirteen bits wide, while red sits only eleven
    // bits above it, so blue's top two bits land inside red's field and red's
    // bottom two land inside blue's. Red survives -- the intrusion is worth at
    // most 3 out of the 256 it is about to be divided by -- but blue comes out
    // as blue + ((red_product & 3) << 3) mod 32, wrong for 75% of alpha values
    // and wrapped low nearly every time. Exhaustively: 50% of all
    // (fg, bg, alpha) triples came out wrong, worst case blue off by 24 of 31.
    //
    // On white text over a dark background that is red and green at full
    // coverage and blue at nothing, i.e. a yellow rim on every antialiased
    // pixel. The trick is sound with a 5-bit alpha (31 * 32 = 992, ten bits,
    // clear of red); it did not survive alpha being promoted to eight bits.
    //
    // Only edge pixels arrive here at all -- the caller stores fully opaque
    // pixels directly and skips fully transparent ones -- so this function IS
    // the antialiasing, and getting it wrong shows up nowhere else.
    //
    // Scaling by 256 rather than 255 keeps the divide a shift; each lane now
    // owns its whole 32-bit word, so the bound that matters is only that a
    // single field cannot overflow it. Red is the widest at 0xF800 * 256 =
    // 16,252,928, well inside 32 bits.
    const uint32_t inv = 256u - a;
    const uint32_t r = (((fg & 0xF800u) * a) + ((bg & 0xF800u) * inv)) >> 8;
    const uint32_t g = (((fg & 0x07E0u) * a) + ((bg & 0x07E0u) * inv)) >> 8;
    const uint32_t b = (((fg & 0x001Fu) * a) + ((bg & 0x001Fu) * inv)) >> 8;
    return static_cast<uint16_t>((r & 0xF800u) | (g & 0x07E0u) | (b & 0x001Fu));
}

// Scale an RGB565 toward black. inv is 0..32, i.e. 32 keeps the pixel and 0
// blacks it out.
//
// Two multiplies rather than blend565's six, because red and blue share one
// lane. That packing is what the comment above blend565 warns is broken -- and
// it is, for an 8-bit alpha. With five bits it is exact: blue's product tops
// out at 31 * 32 = 992, ten bits, while red's starts at bit 11, so the two
// never touch. The scrim is a blurred halo, so the step from 256 levels to 32
// is not visible in it; the antialiasing on the glyph edges, where it would be,
// still goes through blend565.
__attribute__((always_inline)) inline uint16_t scale565(uint16_t c, uint32_t inv) {
    // The two products are left where the multiply puts them and the masks do
    // the shifting, so one shift serves both lanes instead of one each.
    // (c & 0xF81F) * inv leaves blue in bits 0..9 and red in bits 11..20 -- inv
    // is five bits, so they cannot reach each other -- and 0x1F03E0 picks the
    // top five of each. Green, five bits further up, comes out under 0xFC00.
    const uint32_t rb = (c & 0xF81Fu) * inv;
    const uint32_t g = (c & 0x07E0u) * inv;
    return static_cast<uint16_t>(((rb & 0x1F03E0u) | (g & 0xFC00u)) >> 5);
}

// Two neighbouring pixels at once. The band is 4-byte aligned and a scrim cell
// starts on an even pixel, so the pair is one aligned load and one aligned
// store where four half-word accesses stood before.
__attribute__((always_inline)) inline uint32_t scale565x2(uint32_t w, uint32_t inv) {
    return scale565(static_cast<uint16_t>(w), inv) | (static_cast<uint32_t>(scale565(w >> 16, inv)) << 16);
}

// Append [x0, x1) to a row's run list, merging rather than growing it where
// merging is the cheaper answer.
//
// Two merges, for two different reasons. A gap narrower than gapMerge is
// swallowed because a run record costs a load, two extracts and a loop setup,
// which is more than the few skipped pixels; antialiased text is full of one-
// and two-pixel gaps. A full list is merged
// into its last entry because the alternative is dropping coverage, and
// swallowing the gap is only slower, never wrong -- the kernels test each
// pixel's own coverage byte.
static inline int emitRun(uint32_t *runs, int n, int x0, int x1, int gapMerge) {
    if (n > 0) {
        const uint32_t last = runs[n - 1];
        const int lastEnd = static_cast<int>(last >> 16);
        if (x0 - lastEnd <= gapMerge || n >= RUNS_PER_ROW) {
            runs[n - 1] = (last & 0xFFFFu) | (static_cast<uint32_t>(x1) << 16);
            return n;
        }
    }
    runs[n] = static_cast<uint32_t>(x0) | (static_cast<uint32_t>(x1) << 16);
    return n + 1;
}

// Composite one row of overlay pixels over the band.
//
// Standalone rather than inlined into renderFrame, and that is load-bearing.
// Inlined, the loop ran out of registers: the disassembly reloaded the span
// end, the alpha pointer and the scrim row from the stack on every single
// pixel and closed with a taken branch instead of Xtensa's zero-overhead loop,
// which is how a body of five instructions came to cost sixty cycles.
//
// A row at a time rather than a run, for the opposite reason. Entered once per
// run it was called ~3,100 times a frame, and on the windowed ABI each of those
// entry/retw pairs can trip a register-window overflow, which is a real
// exception. Taking the whole row's run list instead costs one call per row
// with content, about 150 a frame.
//
// Colour and coverage both come from the snapshot, four bytes apart in the
// same cache line, because the runs are exact: every pixel this walks is one
// the caller already knows has coverage, so there is nothing to be gained by
// reading the coverage from somewhere denser first.
__attribute__((noinline)) static void blendRow(uint16_t *__restrict dst, const uint8_t *__restrict colour,
                                               const uint32_t *__restrict runs, int nRuns) {
    for (int i = 0; i < nRuns; i++) {
        const uint32_t r = runs[i];
        int x = static_cast<int>(r & 0xFFFFu);
        const int xEnd = static_cast<int>(r >> 16);
        const uint8_t *px = colour + static_cast<size_t>(x) * 3;
        for (; x < xEnd; x++, px += 3) {
            // LV_IMG_CF_TRUE_COLOR_ALPHA @16bpp, LV_COLOR_16_SWAP=0:
            // little-endian RGB565 followed by a coverage byte.
            const uint32_t a = px[2];
            if (a == 0) {
                continue; // only the few pixels a gap merge swallowed
            }
            const uint16_t c = static_cast<uint16_t>(px[0] | (px[1] << 8));
            // Opaque is most of a glyph's interior and needs no arithmetic at
            // all; only the antialiased rim reaches blend565.
            dst[x] = a == 255 ? c : blend565(c, dst[x], static_cast<uint8_t>(a));
        }
    }
}

// Dim the band toward black wherever the text scrim reaches, so the glyphs
// about to go down on top of it stay legible over a bright animation.
//
// A pass of its own, ahead of the blend, and that is what makes it cheap. Done
// inside the blend it had to load the coverage byte for every pixel in the
// halo just to decide whether to also composite, and the halo is four times
// the area of the glyphs: 19,000 of the 24,600 pixels the composite walked
// paid a load whose answer was always zero. Split out, this pass reads nothing
// but the 1/4-resolution grid -- 120 bytes a row -- and writes the band, which
// is internal SRAM.
//
// Dimming a pixel the blend is about to overwrite is wasted, but only for the
// ~3,000 fully opaque pixels a frame, and detecting them is what cost the load
// in the first place.
//
// The dim is keyed on the cell rather than on a pixel's own coverage on
// purpose: the pixels that decide legibility are the transparent ones between
// strokes and inside glyph counters, which the blend never touches at all.
__attribute__((noinline)) static void scrimRow(uint16_t *__restrict dst, const uint8_t *__restrict invRow,
                                               const uint32_t *__restrict runs, int nRuns, int w) {
    for (int i = 0; i < nRuns; i++) {
        const uint32_t r = runs[i];
        const int c0 = static_cast<int>(r & 0xFFFFu);
        int c1 = static_cast<int>(r >> 16);
        if ((c1 << SCRIM_SHIFT) > w) {
            c1 = w >> SCRIM_SHIFT;
        }
        for (int c = c0; c < c1; c++) {
            // Already the 1/32 factor, not the coverage it came from: the
            // strength multiply, the clamp and the rounding are the same for
            // every frame the overlay lives through, so buildScrim does them
            // once per publish instead of 24,000 times per frame.
            const uint32_t inv = invRow[c];
            if (inv == SCRIM_INV_NONE) {
                continue; // a cell a gap merge swallowed
            }
            // Spelled out rather than looped: at a four-iteration trip count
            // the compiler kept the counter and the branch, which is two of
            // every thirteen instructions the loop issued.
            static_assert(SCRIM_SHIFT == 2, "the cell body below is four pixels wide");
            uint32_t *const q = reinterpret_cast<uint32_t *>(dst + (c << SCRIM_SHIFT));
            q[0] = scale565x2(q[0], inv);
            q[1] = scale565x2(q[1], inv);
        }
    }
}

#ifdef GM_ANIM_BENCH
// Bench only: the blend walk with its work removed, so the pixel loop can be
// split into what it computes and what it waits on.
//   2 -- read the coverage byte and nothing else (the PSRAM load on its own)
//   3 -- coverage plus the band read-modify-write (adds the SRAM traffic)
// Returns the accumulator so the loads cannot be optimised away.
__attribute__((noinline)) static uint32_t blendRowProbe(uint16_t *__restrict dst, const uint8_t *__restrict colour,
                                                        const uint32_t *__restrict runs, int nRuns, int level) {
    uint32_t acc = 0;
    for (int i = 0; i < nRuns; i++) {
        const uint32_t r = runs[i];
        int x = static_cast<int>(r & 0xFFFFu);
        const int xEnd = static_cast<int>(r >> 16);
        const uint8_t *px = colour + static_cast<size_t>(x) * 3;
        for (; x < xEnd; x++, px += 3) {
            acc += px[2];
            if (level >= 3) {
                dst[x] = scale565(dst[x], 24);
            }
        }
    }
    return acc;
}
#endif

#ifdef GM_ANIM_BENCH
// Bench only: a deterministic value for every panel pixel, so a host can
// compute the entire framebuffer independently and compare it byte for byte.
//
// The point is to take visual judgement out of the loop. A photograph of this
// panel cannot settle whether the pipeline corrupts anything -- a webcam in a
// dark room auto-exposes to tens of milliseconds and integrates ten panel
// frames, which fabricates shear and duplication that are not on the screen.
// This is the same path the animations use, up to and including the GDMA
// transfer into the framebuffer, with the content replaced by something the
// far end already knows the answer to. Any misplaced band, dropped descriptor,
// wrong destination offset or flipped bit shows up as an exact mismatch count
// and coordinate rather than an opinion about a JPEG.
//
// Multiplied by primes and folded so neighbouring pixels and neighbouring rows
// differ in the high bits: an offset error has to change the value, which a
// smooth ramp would let slide for small displacements.
__attribute__((always_inline)) inline uint16_t benchPatternPx(int x, int y) {
    const uint32_t v = static_cast<uint32_t>(x) * 2654435761u + static_cast<uint32_t>(y) * 40503u;
    return static_cast<uint16_t>((v >> 11) ^ (v >> 27));
}
#endif

// Internal SRAM is deliberately scarce in this firmware (WiFi/BLE/TLS all
// compete for it) — always fall back to PSRAM rather than failing.
void *allocPreferInternal(size_t size) {
    void *p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (p == nullptr) {
        p = ps_malloc(size);
        if (p != nullptr) {
            log_w("SleepAnimation: %u B in PSRAM (internal SRAM full)", static_cast<unsigned>(size));
        }
    }
    return p;
}
} // namespace

SleepAnimation::~SleepAnimation() { stop(); }

void SleepAnimation::configure(uint8_t id, const uint8_t p[4]) {
#ifdef GM_ANIM_BENCH
    // The bench owns the selection: DefaultUI re-applies the stored animation
    // on every UI pass, which would otherwise yank the sweep back to whatever
    // is saved in settings after each frame.
    (void)id;
    (void)p;
    return;
#else
    animId.store(id);
    animParams.store(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
                     (static_cast<uint32_t>(p[3]) << 24));
#endif
}

#ifdef GM_ANIM_BENCH
namespace {
SleepAnimation *g_benchInstance = nullptr;
} // namespace

SleepAnimation *sleep_animation_bench_instance() { return g_benchInstance; }
#endif

// Both of these are reached from the GDMA completion interrupt, which is
// IRAM_ATTR and can run with the flash cache disabled, so neither may hide
// behind anything that lives in flash.
//
// The counter is a plain volatile rather than the std::atomic the other
// counters use: a fetch_add on this target can land in a libatomic helper that
// is not in IRAM. There is one animation instance, and the only readers are the
// bench endpoint and the drain loop in stop().
static volatile uint32_t g_sleepAnimDmaDone = 0;
// The panel's framebuffer gate, cached as a raw handle at start(). The ISR
// releases it on the last chunk of a band; calling a virtual accessor from
// there would be a jump into flash.
static SemaphoreHandle_t g_sleepAnimFbGate = nullptr;

static bool IRAM_ATTR sleepAnimBandRetire(void *arg);

void SleepAnimation::start(Display *d) {
    // !stopped: a previous task timed out its stop() and hasn't exited yet —
    // refuse to start rather than run two renderers against the same buffers.
    if (running || !stopped || d == nullptr) {
        return;
    }
#ifdef GM_ANIM_BENCH
    g_benchInstance = this;
#endif
    display = d;
    // Until the animation has covered the screen once, the rows an interlaced
    // frame skips still hold the previous screen's pixels, so the first frames
    // go out whole.
    warmupFrames.store(3);
    const int w = display->width();
    const int h = display->height();
    for (int i = 0; i < NUM_SLOTS; i++) {
        if (bandBuf[i] == nullptr) {
            // 64-byte aligned and DMA-capable, because on the direct path these
            // are the transfer source. The fallback keeps the ordinary push
            // path working if the aligned allocator cannot find a contiguous
            // block.
#ifdef GM_ANIM_BENCH
            // Only the bench build can reach the GDMA push path (benchSetDma is
            // compiled out otherwise), and only that path needs these to be a
            // legal transfer source.
            bandBuf[i] = static_cast<uint16_t *>(
                heap_caps_aligned_alloc(64, w * BAND_H * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
            if (bandBuf[i] == nullptr)
#endif
            {
                bandBuf[i] = static_cast<uint16_t *>(allocPreferInternal(w * BAND_H * sizeof(uint16_t)));
            }
        }
        if (bandReady[i] == nullptr) {
            bandReady[i] = xSemaphoreCreateBinary();
        }
        if (bandFree[i] == nullptr) {
            bandFree[i] = xSemaphoreCreateBinary();
        }
    }
    if (halfBuf == nullptr) {
        halfBuf = static_cast<uint16_t *>(allocPreferInternal((w / 2) * (BAND_H / 2) * sizeof(uint16_t)));
    }
    computeChords(w, h);
    if (overlayCap == 0) {
        overlayCap = static_cast<uint32_t>(w + 2 * OVERLAY_EXT_MARGIN) * (h + 2 * OVERLAY_EXT_MARGIN) * 3;
        for (auto &ov : overlays) {
            // Snapshot pixels only fit in PSRAM (~700 KB each); the tiny span
            // tables prefer SRAM. Read a couple times per frame — well within
            // the PSRAM budget that the LVGL composite path blew.
            ov.buf = static_cast<uint8_t *>(ps_malloc(overlayCap));
            // PSRAM, not SRAM: the run lists are read once per row by the
            // composite, roughly 480 loads across a whole frame, so they are
            // nowhere near a per-pixel path and 46 KB of internal DRAM matters
            // far more to the network stack than their latency does here.
            ov.runs = static_cast<uint32_t *>(ps_malloc(static_cast<size_t>(h) * RUNS_PER_ROW * 4));
            ov.runN = static_cast<uint8_t *>(ps_malloc(h));
            if (ov.runN != nullptr) {
                // A publish only rewrites the rows LVGL redrew, so every other
                // row has to start out meaning "nothing here".
                memset(ov.runN, 0, h);
            }
            ov.scrimW = (w + (1 << SCRIM_SHIFT) - 1) >> SCRIM_SHIFT;
            ov.scrimH = (h + (1 << SCRIM_SHIFT) - 1) >> SCRIM_SHIFT;
            const size_t cells = static_cast<size_t>(ov.scrimW) * ov.scrimH;
            ov.haloRuns =
                static_cast<uint32_t *>(ps_malloc(static_cast<size_t>(ov.scrimH) * RUNS_PER_ROW * 4));
            ov.haloN = static_cast<uint8_t *>(ps_malloc(ov.scrimH));
            if (ov.haloN != nullptr) {
                memset(ov.haloN, 0, ov.scrimH);
            }
            ov.scrimSrc = static_cast<uint8_t *>(ps_malloc(cells));
            ov.scrim = static_cast<uint8_t *>(ps_malloc(cells));
            // Zeroed because a publish only rewrites the cell rows LVGL
            // redrew; every other cell has to start out meaning "no widget
            // here" rather than whatever the allocator handed back.
            if (ov.scrimSrc != nullptr) {
                memset(ov.scrimSrc, 0, cells);
            }
            if (ov.scrim != nullptr) {
                memset(ov.scrim, 0, cells);
            }
            // ov.scrim is the single sentinel the composite and the publish
            // path test, so it must not be non-null unless the whole scrim
            // apparatus is present. The scrim is a refinement, not a
            // requirement: losing it costs legibility on bright themes and
            // nothing else, so it degrades on its own rather than joining
            // overlayOk and taking the animation down with it.
            if (ov.scrimSrc == nullptr || ov.haloRuns == nullptr || ov.haloN == nullptr) {
                free(ov.scrim);
                ov.scrim = nullptr;
            }
        }
        if (scrimTmp == nullptr) {
            scrimTmp = static_cast<uint8_t *>(
                ps_malloc(static_cast<size_t>(overlays[0].scrimW) * overlays[0].scrimH));
        }
        if (scrimTmp == nullptr) {
            for (auto &ov : overlays) {
                free(ov.scrim);
                ov.scrim = nullptr;
            }
        }
    }
    bool overlayOk = true;
    for (auto &ov : overlays) {
        overlayOk = overlayOk && ov.buf != nullptr && ov.runs != nullptr && ov.runN != nullptr;
    }
    bool pipelineOk = true;
    for (int i = 0; i < NUM_SLOTS; i++) {
        pipelineOk = pipelineOk && bandBuf[i] != nullptr && bandReady[i] != nullptr && bandFree[i] != nullptr;
    }
    if (halfBuf == nullptr) {
        pipelineOk = false;
    }
    if (!pipelineOk || !overlayOk) {
        log_e("SleepAnimation: buffer allocation failed (bandBuf=%p/%p halfBuf=%p overlayOk=%d)", bandBuf[0], bandBuf[1],
              halfBuf, overlayOk);
        return;
    }
    // Reset the pipeline: both cursors to slot 0, any signal left over from a
    // previous run drained, both slots marked free. DefaultUI stops and
    // restarts the animation on every standby transition, so a stale bandReady
    // or a cursor left on slot 1 would desynchronise the two tasks and push a
    // band that was never rendered.
    renderSlot = 0;
    for (int i = 0; i < NUM_SLOTS; i++) {
        while (xSemaphoreTake(static_cast<SemaphoreHandle_t>(bandReady[i]), 0) == pdTRUE) {
        }
        while (xSemaphoreTake(static_cast<SemaphoreHandle_t>(bandFree[i]), 0) == pdTRUE) {
        }
        xSemaphoreGive(static_cast<SemaphoreHandle_t>(bandFree[i]));
    }
    // The direct path is brought up and torn down by the render task, in
    // beginDirectPath / endDirectPath: esp_intr_alloc binds the completion
    // handler to whichever core calls it, and that is the task that waits on
    // it. All start() does is clear the latches so a new run re-resolves.
    dmaActive = false;
    dmaInstallTried = false;
    fbDirect[0] = fbDirect[1] = nullptr;
    fbCount = 0;
    fbBack = 0;
    gateWarned = false;
    frameGateHeld = false;
    g_sleepAnimFbGate = nullptr;
    initializedAnimId = -1; // force the animation's init on the render task
    running = true;
    stopped = false;
    TaskHandle_t handle = nullptr;
    // Core 1, SAME priority as the UI task: FreeRTOS round-robins equal
    // priorities every tick, so LVGL's touch poll stays responsive even when
    // a frame overruns its budget. At priority 2 the animation preempted the
    // UI task and left it ~1 ms per 33 ms frame — standby taps took seconds
    // to register. In standby the UI task is nearly idle, so the animation
    // still gets almost the whole core; on active screens (all-screens mode)
    // it gracefully drops frames instead of starving input.
    // 8 KB stack: renderFrame itself is lean, but log_i's float formatting and
    // the esp_lcd draw path both burn stack; 4 KB was within canary distance.
    if (xTaskCreatePinnedToCore(taskEntry, "SleepAnim", 8192, this, 1, &handle, 1) != pdPASS) {
        log_e("SleepAnimation: task creation failed");
        running = false;
        stopped = true;
        return;
    }
    taskHandle = handle;

    // Push task on core 0 at priority 2. It must sit BELOW
    // Controller::loopLogicTask (core 0, priority 3) so animation work can
    // never preempt the control path, and above the default-priority-1 tasks
    // that share core 0 (Arduino loop, WiFi events, AsyncTCP), none of which
    // are time-critical. Core 1 is left exactly as it was: the render task
    // stays at priority 1 alongside the UI task, which is deliberate (see the
    // note above -- priority 2 there starved touch input).
    TaskHandle_t push = nullptr;
    pushStopped = false;
    if (xTaskCreatePinnedToCore(pushTaskEntry, "SleepPush", 4096, this, 2, &push, 0) != pdPASS) {
        log_e("SleepAnimation: push task creation failed");
        pushStopped = true;
        running = false;
        stopped = true;
        return;
    }
    pushHandle = push;
    log_i("SleepAnimation: started (%dx%d), push task on core 0", w, h);
}

void SleepAnimation::stop() {
    if (!running) {
        return;
    }
    running = false;
    const unsigned long deadline = millis() + 500;
    while (!stopped && millis() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    // The push task blocks on bandReady, so it needs a wakeup to observe
    // !running. Give both slots: whichever it is waiting on releases it.
    for (int i = 0; i < NUM_SLOTS; i++) {
        if (bandReady[i] != nullptr) {
            xSemaphoreGive(static_cast<SemaphoreHandle_t>(bandReady[i]));
        }
    }
    const unsigned long pushDeadline = millis() + 500;
    while (!pushStopped && millis() < pushDeadline) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    endDirectPath();
}

// Bring the direct push up. Called only from the render task: the completion
// interrupt is bound to whichever core installs the engine, and this is the
// task that waits on it. Every expensive step latches, so a call that has
// already failed costs a handful of loads and logs nothing.
bool SleepAnimation::beginDirectPath() {
    if (dmaActive) {
        engineReadyForMode(); // the mode may have changed under a running path
        return true;
    }
    if (display == nullptr) {
        return false;
    }
    if (fbCount == 0) {
        // The panel caches its own answer, so this resolve happens once.
        const int have = display->frameBufferCount();
        for (int i = 0; i < have && i < FB_MAX; i++) {
            fbDirect[i] = display->directFrameBuffer(i);
            if (fbDirect[i] == nullptr) {
                break;
            }
            fbCount = i + 1;
        }
        if (fbCount == 0) {
            return false;
        }
        // Compose into the buffer that is not on screen. With only one there is
        // nothing to flip to and the path degrades to writing the live buffer,
        // which is what it did before -- fast, and it tears.
        fbBack = fbCount > 1 ? 1 : 0;
    }
    // Without the gate the direct path cannot be made coherent against
    // pushColors, so refuse it rather than run a known race.
    SemaphoreHandle_t gate = static_cast<SemaphoreHandle_t>(display->frameBufferGate());
    if (gate == nullptr) {
        if (!gateWarned) {
            gateWarned = true;
            log_w("SleepAnimation: panel has no framebuffer gate, direct push disabled");
        }
        return false;
    }
    if (!engineReadyForMode()) {
        return false;
    }
    // Whatever LVGL last drew is still sitting in dirty cache lines over this
    // region. Those must reach PSRAM before DMA starts writing there, or a
    // later eviction drops a stale line on top of a rendered band.
    const size_t fbBytes = static_cast<size_t>(display->width()) * display->height() * 2;
    for (int i = 0; i < fbCount; i++) {
        esp_cache_msync(fbDirect[i], fbBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    }
    g_sleepAnimFbGate = gate;
    frameGateHeld = false;
    // From here the panel's own pushColors must stop trusting its cached view
    // of the framebuffer, because this task is about to write it behind the
    // cache.
    display->setDirectWriter(true);
    dmaActive = true;
    log_i("SleepAnimation: direct framebuffer push active");
    return true;
}

// Take the direct push back down, leaving the pipeline on the ordinary push
// task. Safe to call when it was never up.
void SleepAnimation::endDirectPath() {
    if (!dmaActive) {
        return;
    }
    // A transfer may still be reading a band buffer, and those buffers are
    // handed straight back to the render task.
    const unsigned long drain = millis() + 200;
    while (dmaIssued.load() != g_sleepAnimDmaDone && millis() < drain) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    dmaActive = false;
    if (display != nullptr) {
        for (int i = 0; i < fbCount; i++) {
            // DMA wrote the framebuffer behind the cache, so the CPU's view of
            // it is stale but not dirty. When LVGL resumes, a partial redraw
            // writes only its own rectangle, and any 32-byte line it touches is
            // written back whole -- resurrecting old animation pixels in the
            // bytes it did not write. Dropping the lines forces a refetch.
            // Discarding rather than writing back is correct precisely because
            // nothing has CPU-dirtied this region since beginDirectPath
            // flushed it.
            esp_cache_msync(fbDirect[i], static_cast<size_t>(display->width()) * display->height() * 2,
                            ESP_CACHE_MSYNC_FLAG_DIR_M2C);
        }
        display->setDirectWriter(false);
    }
    if (g_sleepAnimFbGate != nullptr) {
        // If the drain above timed out, the last band's completion never
        // released the gate and pushColors would block on it forever. A give on
        // an already-free binary semaphore is a no-op, so this is
        // unconditional rather than conditional on the drain succeeding.
        xSemaphoreGive(g_sleepAnimFbGate);
        g_sleepAnimFbGate = nullptr;
    }
    frameGateHeld = false;
}

// Horizontal extent of the inscribed circle for each band. The panel is round:
// pixels outside the circle are physically not there, so pushing them spends
// PSRAM bandwidth -- the scarcest thing in this pipeline -- on invisible
// output. One rectangle per band, taken from the widest row it contains,
// because pushColors takes a rectangle and esp_lcd_panel_draw_bitmap has no
// stride parameter (verified in esp_lcd_panel_ops.h:59) so a sub-window of a
// full-width buffer would walk off each row's end into the next row's data.
void SleepAnimation::computeChords(int w, int h) {
    const float cx = (w - 1) * 0.5f;
    const float cy = (h - 1) * 0.5f;
    const float r = (w < h ? w : h) * 0.5f;
    const int bands = (h + BAND_H - 1) / BAND_H;
    for (int b = 0; b < bands && b < MAX_BANDS; b++) {
        const int y0 = b * BAND_H;
        const int y1 = (y0 + BAND_H < h ? y0 + BAND_H : h) - 1;
        // Widest row in the band is the one nearest the vertical centre.
        float dy = 0.0f;
        if (cy < y0) {
            dy = y0 - cy;
        } else if (cy > y1) {
            dy = cy - y1;
        }
        int x0 = 0, x1 = w;
        const float inside = r * r - dy * dy;
        if (inside > 0.0f) {
            const float half = sqrtf(inside);
            x0 = static_cast<int>(cx - half);
            x1 = static_cast<int>(cx + half) + 1;
            if (x0 < 0) {
                x0 = 0;
            }
            if (x1 > w) {
                x1 = w;
            }
        }
        // Keep the start even and the width even: the animations' paired
        // 32-bit stores assume 4-byte alignment, and the packing memmove below
        // is cheaper on aligned words.
        x0 &= ~1;
        if ((x1 - x0) & 1) {
            x1++;
        }
        if (x1 > w) {
            x1 = w;
        }
        bandX0[b] = static_cast<int16_t>(x0);
        bandX1[b] = static_cast<int16_t>(x1);
    }
}

void SleepAnimation::pushTaskEntry(void *arg) {
    auto *self = static_cast<SleepAnimation *>(arg);
    self->pushLoop();
    self->pushStopped = true;
    vTaskDelete(nullptr);
}


// The same retirement, for the native engine, which gives every transfer an arg
// because it submits one per band rather than one per chunk.
static bool IRAM_ATTR sleepAnimNativeDone(void *arg) {
    g_sleepAnimDmaDone = g_sleepAnimDmaDone + 1;
    return arg != nullptr && sleepAnimBandRetire(arg);
}

static bool sleepAnimBandRetire(void *arg) {
    const auto *done = static_cast<const SleepAnimation::BandDone *>(arg);
    BaseType_t woken = pdFALSE;
    // Releases the slot the transfer has finished reading. The render task
    // waits on this same semaphore before refilling that slot, so the transfer
    // overlaps the next band's render.
    xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(done->slot), &woken);
    // On the last band of a frame, also the framebuffer gate. It is released
    // from here rather than at submission time because
    // esp_lcd_panel_draw_bitmap ends with a cache writeback over whole
    // scanlines, and running that against an in-flight transfer smears stale
    // lines over DMA-written pixels. A binary semaphore rather than a mutex
    // exactly so this give is legal from an interrupt.
    if (done->gate != nullptr) {
        BaseType_t gateWoken = pdFALSE;
        xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(done->gate), &gateWoken);
        woken = (woken == pdTRUE || gateWoken == pdTRUE) ? pdTRUE : pdFALSE;
    }
    return woken == pdTRUE;
}

#ifdef GM_ANIM_BENCH
uint32_t SleepAnimation::benchDmaCompleted() const { return g_sleepAnimDmaDone; }

size_t SleepAnimation::benchCopyFrameBuffer(uint8_t *out, size_t cap, int *outW, int *outH) {
    if (out == nullptr || display == nullptr) {
        return 0;
    }
    // The buffer currently on screen, which with double buffering is the one
    // the render task is NOT composing into. Dumping the back buffer would show
    // a half-written frame and invite exactly the wrong conclusion.
    uint16_t *const fb = (dmaActive && fbCount > 1) ? fbDirect[fbBack ^ 1] : display->directFrameBuffer(0);
    if (fb == nullptr) {
        return 0;
    }
    const int w = display->width();
    const int h = display->height();
    const size_t bytes = static_cast<size_t>(w) * h * 2;
    if (bytes > cap) {
        return 0;
    }
    display->lockFrameBuffer();
    // The DMA path writes this buffer without going through the cache, so a
    // plain read can return whatever the CPU happens to still hold.
    esp_cache_msync(fb, bytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    memcpy(out, fb, bytes);
    display->unlockFrameBuffer();
    if (outW != nullptr) {
        *outW = w;
    }
    if (outH != nullptr) {
        *outH = h;
    }
    return bytes;
}

size_t SleepAnimation::benchCopyOverlay(uint8_t *out, size_t cap, int *outW, int *outH) {
    const int front = overlayFront.load();
    if (out == nullptr || front < 0) {
        return 0;
    }
    const Overlay &ov = overlays[front & 1];
    if (ov.buf == nullptr || ov.w <= 0 || ov.h <= 0) {
        return 0;
    }
    const size_t bytes = static_cast<size_t>(ov.w) * ov.h * 3;
    if (bytes > cap) {
        return 0;
    }
    memcpy(out, ov.buf, bytes);
    if (outW != nullptr) {
        *outW = ov.w;
    }
    if (outH != nullptr) {
        *outH = ov.h;
    }
    return bytes;
}

bool SleepAnimation::benchBandsInternal() const {
    for (int i = 0; i < NUM_SLOTS; i++) {
        const uintptr_t a = reinterpret_cast<uintptr_t>(bandBuf[i]);
        if (a == 0 || (a >= 0x3C000000u && a < 0x3E000000u)) {
            return false;
        }
    }
    return true;
}
#endif // GM_ANIM_BENCH

namespace {
// esp_intr_alloc binds the handler to whichever core calls it, and there is no
// argument to say otherwise -- so the only way to choose is to call from a task
// already pinned where the interrupt should land. This one-shot task exists for
// that and nothing else.
struct NativeInstallReq {
    BandDma *dma;
    size_t maxBytes;
    size_t burst;
    esp_err_t err;
    SemaphoreHandle_t done;
};

void nativeInstallTaskEntry(void *arg) {
    auto *req = static_cast<NativeInstallReq *>(arg);
    req->err = req->dma->install(req->maxBytes, req->burst, sleepAnimNativeDone);
    xSemaphoreGive(req->done);
    vTaskDelete(nullptr);
}
} // namespace

bool SleepAnimation::installNativeOnIsrCore() {
    if (bandDma.ready()) {
        return true;
    }
    if (nativeInstallTried) {
        return false;
    }
    nativeInstallTried = true;
    // 32-byte burst: it matches both the data cache line and the octal PSRAM
    // burst, and the esp32s3 register field documents 16 and 32 as the valid
    // encodings for this channel.
    NativeInstallReq req{&bandDma, static_cast<size_t>(BAND_H) * 480 * 2, 32, ESP_FAIL, xSemaphoreCreateBinary()};
    if (req.done != nullptr) {
        TaskHandle_t installer = nullptr;
        // Same reason as the async engine: esp_intr_alloc binds the handler to
        // the calling core, and the render task shares core 1 with the panel's
        // own scan-out interrupt.
        if (xTaskCreatePinnedToCore(nativeInstallTaskEntry, "BandDmaIns", 4096, &req, 3, &installer, DMA_ISR_CORE) ==
            pdPASS) {
            xSemaphoreTake(req.done, pdMS_TO_TICKS(2000));
        }
        vSemaphoreDelete(req.done);
    }
    if (!bandDma.ready()) {
        log_w("SleepAnimation: native GDMA install failed (%s), falling back", esp_err_to_name(req.err));
        return false;
    }
    return true;
}

// Installed on first use, and latched, so the steady-state cost of asking is a
// load and a branch.
bool SleepAnimation::engineReadyForMode() { return installNativeOnIsrCore(); }


void SleepAnimation::pushLoop() {
    int slot = 0;
    while (running) {
        if (xSemaphoreTake(static_cast<SemaphoreHandle_t>(bandReady[slot]), pdMS_TO_TICKS(200)) != pdTRUE) {
            continue; // render task idle or stopping; re-check running
        }
        if (!running) {
            break;
        }
        const PushJob job = pushJob[slot];
#ifdef GM_ANIM_BENCH
        const int64_t t0 = esp_timer_get_time();
#endif
        if (job.mode != 0) {
            // esp_lcd takes a rectangle and no stride, so the rows that go out
            // cannot be one call. Mode 2 sends them two at a time -- at half
            // resolution a pair is one source row, contiguous in the
            // framebuffer -- which is half the calls of mode 1 for the same
            // bytes. The rows left alone keep the previous frame.
            const int step = job.mode == 2 ? 2 : 1;
            const int stride = job.x1 - job.x0;
            for (int y = job.y0; y + step <= job.y1; y += step) {
                if ((((job.mode == 2 ? (y >> 1) : y) ^ job.parity) & 1) != 0) {
                    continue;
                }
                display->pushColors(job.x0, static_cast<int16_t>(y), job.x1, static_cast<int16_t>(y + step),
                                    bandBuf[slot] + static_cast<size_t>(y - job.y0) * stride);
            }
        } else {
            display->pushColors(job.x0, job.y0, job.x1, job.y1, bandBuf[slot]);
        }
#ifdef GM_ANIM_BENCH
        accPushUs += static_cast<uint64_t>(esp_timer_get_time() - t0);
#endif
        xSemaphoreGive(static_cast<SemaphoreHandle_t>(bandFree[slot]));
        slot = (slot + 1) % NUM_SLOTS;
    }
}

uint8_t *SleepAnimation::overlayBackBuffer() {
    if (overlayCap == 0) {
        return nullptr;
    }
    const int back = (overlayFront.load() + 1) & 1;
    // With two buffers, the back buffer is the front of two publishes ago; a
    // frame started just before the last publish may still be blending from
    // it. Writing into it now would tear the composited widgets — skip and
    // let the caller retry on the next UI pass (the frame is ~20 ms).
    if (overlayInUse.load() == back) {
        return nullptr;
    }
    return overlays[back].buf;
}

void SleepAnimation::publishOverlay(int w, int h, int rowY0, int rowY1) {
    if (overlayCap == 0 || display == nullptr) {
        return;
    }
    const int back = (overlayFront.load() + 1) & 1;
    Overlay &ov = overlays[back];
    if (ov.buf == nullptr || ov.runs == nullptr || ov.runN == nullptr) {
        return;
    }
    ov.w = w;
    ov.h = h;
    const int panelW = display->width();
    const int panelH = display->height();
    // The snapshot extends past the panel by ext draw size on every side.
    const int xoff = (w - panelW) / 2;
    const int yoff = (h - panelH) / 2;
    // Span scan: one pass over the alpha channel (~230 KB) per refresh, on the
    // UI task. The render task then touches only rows/pixels that matter.
    if (rowY0 < 0) {
        rowY0 = 0;
    }
    if (rowY1 > panelH) {
        rowY1 = panelH;
    }
    const bool doScrim = ov.scrim != nullptr && scrimTmp != nullptr;
    const int sw = ov.scrimW;
    if (doScrim) {
        // Widen the range to whole scrim cells. A cell's value is the peak alpha
        // of the 4 rows it covers, so it can only be rebuilt from all 4 of them
        // -- clearing a cell row and then refilling it from a partial range
        // would drop the coverage the other rows contributed. The extra rows
        // cost one more pass over alpha the buffer already holds, and their span
        // tables come out identical to what was there.
        rowY0 &= ~((1 << SCRIM_SHIFT) - 1);
        rowY1 = (rowY1 + (1 << SCRIM_SHIFT) - 1) & ~((1 << SCRIM_SHIFT) - 1);
        if (rowY1 > panelH) {
            rowY1 = panelH;
        }
        const int cellY0 = rowY0 >> SCRIM_SHIFT;
        const int cellY1 = ((rowY1 + (1 << SCRIM_SHIFT) - 1) >> SCRIM_SHIFT);
        memset(ov.scrimSrc + static_cast<size_t>(cellY0) * sw, 0, static_cast<size_t>(cellY1 - cellY0) * sw);
    }
    for (int y = rowY0; y < rowY1; y++) {
        uint32_t *const rowRuns = ov.runs + static_cast<size_t>(y) * RUNS_PER_ROW;
        int nRuns = 0;
        const int sy = y + yoff;
        if (sy >= 0 && sy < h) {
            const uint8_t *a = ov.buf + (static_cast<size_t>(sy) * w + xoff) * 3 + 2;
            uint8_t *cell = doScrim ? ov.scrimSrc + static_cast<size_t>(y >> SCRIM_SHIFT) * sw : nullptr;
            int runStart = -1;
            for (int x = 0; x < panelW; x++, a += 3) {
                if (*a != 0) {
                    if (runStart < 0) {
                        runStart = x;
                    }
                    if (cell != nullptr) {
                        // Peak, not average: the halo is meant to cover the gaps
                        // between strokes and inside glyph counters, and those
                        // are exactly where an average would fade it out.
                        uint8_t &c = cell[x >> SCRIM_SHIFT];
                        if (*a > c) {
                            c = *a;
                        }
                    }
                    continue;
                }
                if (runStart < 0) {
                    continue;
                }
                nRuns = emitRun(rowRuns, nRuns, runStart, x, RUN_GAP_MERGE);
                runStart = -1;
            }
            if (runStart >= 0) {
                nRuns = emitRun(rowRuns, nRuns, runStart, panelW, RUN_GAP_MERGE);
            }
        }
        ov.runN[y] = static_cast<uint8_t>(nRuns);
    }
    if (doScrim) {
        buildScrim(ov, panelW, panelH);
    }
    overlayFront.store(back);
}

// Turns the per-cell coverage in ov.scrimSrc into the halo the composite reads,
// then widens the span tables to cover it.
//
// Whole-grid rather than incremental. The dilate spreads coverage 3 cells in
// every direction, so a partial rebuild would have to run over the refreshed
// rows plus a margin and would still get the seam wrong wherever the margin
// itself was stale. The grid is 120x120, and six passes over it cost far less
// than the single pass over the 230 KB alpha plane that just ran.
void SleepAnimation::buildScrim(Overlay &ov, int panelW, int panelH) {
    const int sw = ov.scrimW;
    const int sh = ov.scrimH;
    // Dilate: two 3-wide max passes per axis, so coverage reaches 2 cells out
    // in every direction and diagonals get the same reach as the axes.
    scrimTap3(ov.scrimSrc, scrimTmp, sh, sw, sw, 1, true);
    scrimTap3(scrimTmp, ov.scrim, sh, sw, sw, 1, true);
    scrimTap3(ov.scrim, scrimTmp, sw, sh, 1, sw, true);
    scrimTap3(scrimTmp, ov.scrim, sw, sh, 1, sw, true);
    // Smooth, so the scrim's own edge is a gradient rather than a visible
    // rectangle sitting on the animation.
    scrimTap3(ov.scrim, scrimTmp, sh, sw, sw, 1, false);
    scrimTap3(scrimTmp, ov.scrim, sw, sh, 1, sw, false);

    // The halo runs the composite walks, read straight off the grid that was
    // just built. Whole-grid, like the passes above: the dilate spreads
    // coverage three cells in every direction, so which cells are non-zero in
    // one row depends on glyphs several rows away, and a partial rebuild would
    // get the seam wrong wherever its margin was stale. It is 120x120.
    const int q8 = scrimQ8.load();
    for (int cy = 0; cy < ov.scrimH; cy++) {
        uint8_t *const row = ov.scrim + static_cast<size_t>(cy) * sw;
        for (int cx = 0; cx < sw; cx++) {
            int dim = (row[cx] * q8) >> 8;
            if (dim > 255) {
                dim = 255;
            }
            // Round to the nearest 1/32 so a full-strength scrim still reaches 0.
            row[cx] = static_cast<uint8_t>(SCRIM_INV_NONE - ((dim + 4) >> 3));
        }
        uint32_t *const runs = ov.haloRuns + static_cast<size_t>(cy) * RUNS_PER_ROW;
        int n = 0;
        int start = -1;
        for (int cx = 0; cx < sw; cx++) {
            if (row[cx] != SCRIM_INV_NONE) {
                if (start < 0) {
                    start = cx;
                }
                continue;
            }
            if (start < 0) {
                continue;
            }
            n = emitRun(runs, n, start, cx, HALO_GAP_MERGE_CELLS);
            start = -1;
        }
        if (start >= 0) {
            n = emitRun(runs, n, start, sw, HALO_GAP_MERGE_CELLS);
        }
        ov.haloN[cy] = static_cast<uint8_t>(n);
    }
    (void)panelW;
    (void)panelH;
}

void SleepAnimation::taskEntry(void *arg) {
    auto *self = static_cast<SleepAnimation *>(arg);
    self->renderLoop();
    self->stopped = true;
    vTaskDelete(nullptr);
}

// Show the frame that renderFrame just composed, and start composing into the
// other buffer.
//
// This is what makes the pipeline tear-free rather than merely fast. The panel
// scans its framebuffer continuously at ~61 Hz and the render task writes at
// ~40, so with a single buffer the write front crosses the scan line several
// times a frame and the picture on screen is a seam of two generations. Writing
// a buffer nobody is reading removes the race instead of narrowing it.
//
// The gate is taken and immediately released rather than held: taking it is how
// this task learns the frame's last transfer has landed, because the completion
// interrupt is what gives it back. Presenting before that would flip to a
// buffer whose bottom bands are still in flight.
void SleepAnimation::presentFrame() {
    // directPushOn as well as dmaActive: with the direct path switched off the
    // bands go through pushColors, which writes whichever buffer esp_lcd counts
    // as current, so flipping underneath it would show a buffer nothing wrote.
    if (!dmaActive || !directPushOn.load() || fbCount < 2 || display == nullptr) {
        return;
    }
    display->lockFrameBuffer();
    // Empty dirty range: everything in this buffer arrived over DMA, straight
    // into PSRAM, so there is nothing in the cache to write back.
    display->presentFrameBuffer(fbBack, 0, 0);
    display->unlockFrameBuffer();
    fbBack ^= 1;
}

void SleepAnimation::renderLoop() {
    uint32_t fpsFrames = 0;
    unsigned long fpsWindowStart = millis();
    while (running) {
        // The direct path follows dmaWanted, so the bench knob takes effect on
        // the next frame instead of only at the next start(). Both calls are
        // no-ops once the state matches.
        if (dmaWanted.load()) {
            beginDirectPath();
        } else {
            endDirectPath();
        }
        const int64_t frameStart = esp_timer_get_time();
        renderFrame();
        presentFrame();
        // Once per frame, not once per band: every band of a frame must push
        // the same parity or the two halves of the picture drift apart.
        frameParity++;
        const uint32_t warm = warmupFrames.load();
        if (warm > 0) {
            warmupFrames.store(warm - 1);
        }
        fpsFrames++;
#ifdef GM_ANIM_BENCH
        const uint32_t frameUs = static_cast<uint32_t>(esp_timer_get_time() - frameStart);
        accTotalUs += frameUs;
        accFrames++;
        if (frameUs > accMaxTotalUs) {
            accMaxTotalUs = frameUs;
        }
        benchTick();
#endif

        const unsigned long now = millis();
        if (now - fpsWindowStart >= 10000) {
            log_i("SleepAnimation: %.1f fps", fpsFrames * 1000.0f / (now - fpsWindowStart));
            fpsFrames = 0;
            fpsWindowStart = now;
        }

        int fps = maxFps.load();
        fps = fps < 5 ? 5 : (fps > 60 ? 60 : fps);
        const int64_t targetFrameUs = 1000000 / fps;
        const int64_t elapsed = esp_timer_get_time() - frameStart;
        const int64_t remaining = targetFrameUs - elapsed;
        // Always yield at least one full tick so the UI task keeps polling
        // touch even when a frame overruns its budget.
        TickType_t ticks = pdMS_TO_TICKS(remaining > 1000 ? remaining / 1000 : 1);
        vTaskDelay(ticks > 0 ? ticks : 1);
    }
}

#ifdef GM_ANIM_BENCH
void SleepAnimation::benchTick() {
    const unsigned long now = millis();
    if (benchResetPending.exchange(false)) {
        // Applied here, on the render task, so no dwell is half-recorded and
        // the reader never sees a torn benchDone[].
        for (int i = 0; i < BENCH_MAX_ANIMS; i++) {
            benchDone[i] = BenchResult{};
        }
        accBandUs = accBlendUs = accPushUs = accTotalUs = accWaitUs = accPackUs = 0;
        accSpanPx = accScrimPx = 0;
    accSpanPx = accScrimPx = 0;
        accFrames = 0;
        accMaxTotalUs = 0;
        accBandLockedUs = 0;
        accBandLockedRows = accBandRows = 0;
        benchPasses = 0;
        benchDwellStart = now;
        uint8_t p[4];
        bg_parse_params(nullptr, 0, p);
        animParams.store(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24));
        animId.store(0);
        log_i("animbench: results cleared, sweep restarted");
        return;
    }
    if (benchDwellStart == 0) {
        benchDwellStart = now;
        return;
    }
    if (now - benchDwellStart >= BENCH_DWELL_MS) {
        benchFinishDwell();
    }
}

void SleepAnimation::benchFinishDwell() {
    const int id = animId.load();
    const unsigned long elapsedMs = millis() - benchDwellStart;
    if (id >= 0 && id < BENCH_MAX_ANIMS && accFrames > 0) {
        BenchResult &r = benchDone[id];
        r.frames = accFrames;
        r.bandUs = static_cast<uint32_t>(accBandUs / accFrames);
        r.blendUs = static_cast<uint32_t>(accBlendUs / accFrames);
        r.pushUs = static_cast<uint32_t>(accPushUs / accFrames);
        r.totalUs = static_cast<uint32_t>(accTotalUs / accFrames);
        r.maxTotalUs = accMaxTotalUs;
        r.waitUs = static_cast<uint32_t>(accWaitUs / accFrames);
        r.packUs = static_cast<uint32_t>(accPackUs / accFrames);
        r.spanPx = static_cast<uint32_t>(accSpanPx / accFrames);
        r.scrimPx = static_cast<uint32_t>(accScrimPx / accFrames);
        r.achievedFps = elapsedMs > 0 ? static_cast<uint32_t>(accFrames * 100000ULL / elapsedMs) : 0;
        // Both normalised per row so the locked sample (one band per frame)
        // is directly comparable to the unlocked one (all 30 bands).
        const uint64_t unlockedUs = accBandUs > accBandLockedUs ? accBandUs - accBandLockedUs : 0;
        const uint32_t unlockedRows = accBandRows > accBandLockedRows ? accBandRows - accBandLockedRows : 0;
        r.bandNsPerRow = unlockedRows > 0 ? static_cast<uint32_t>(unlockedUs * 1000ULL / unlockedRows) : 0;
        r.bandLockedNsPerRow =
            accBandLockedRows > 0 ? static_cast<uint32_t>(accBandLockedUs * 1000ULL / accBandLockedRows) : 0;
        r.valid = true; // publish last: readers on other tasks gate on this
        log_i("animbench: %-10s band=%u us blend=%u us push=%u us total=%u us max=%u us fps=%u.%02u",
              bg_animation(id).id, r.bandUs, r.blendUs, r.pushUs, r.totalUs, r.maxTotalUs, r.achievedFps / 100,
              r.achievedFps % 100);
    }

    accBandUs = accBlendUs = accPushUs = accTotalUs = accWaitUs = accPackUs = 0;
    accSpanPx = accScrimPx = 0;
    accFrames = 0;
    accMaxTotalUs = 0;
    accBandLockedUs = 0;
    accBandLockedRows = accBandRows = 0;
    benchDwellStart = millis();

    const int count = bg_animation_count();
    // benchOnly pins the sweep to a single animation. A full pass is 13 dwells
    // of 6 s, so iterating on one animation's inner loop otherwise costs about
    // 90 s of waiting per measurement, nearly all of it spent measuring the
    // twelve animations that did not change.
    const int pin = benchOnly.load();
    const int next = (pin >= 0 && pin < count) ? pin : (id + 1) % count;
    if (next == 0 || pin >= 0) {
        benchPasses++;
        log_i("animbench: completed sweep %u of all %d animations", benchPasses, count);
    }
    // Each animation is measured at its own documented defaults, so a run is
    // reproducible and comparable against the host harness numbers.
    uint8_t p[4];
    bg_parse_params(nullptr, next, p);
    animParams.store(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
                     (static_cast<uint32_t>(p[3]) << 24));
    animId.store(static_cast<uint8_t>(next));
}
#endif

void SleepAnimation::renderFrame() {
    // Cropping to the round panel's visible chord trades render-side work
    // (packing each band to a tight stride) for push-side work (fewer bytes to
    // PSRAM). Since the push runs on the other core now, that trade only pays
    // when push is the stage setting the pace -- otherwise it adds ~4.6 ms to
    // the critical path to save time on a stage that already has slack.
    //
    // The render task blocking on bandFree IS the signal that push is the
    // bottleneck, so last frame's wait decides this frame's crop. Thresholds
    // are split to damp oscillation around the crossover, where both states
    // are near-optimal anyway.
    if (frameWaitUs > 1500) {
        cropEnabled = true;
    } else if (frameWaitUs < 400) {
        cropEnabled = false;
    }
    frameWaitUs = 0;
    const int w = display->width();
    const int h = display->height();
    const uint32_t tMs = millis();

    const int id = animId.load();
    const uint32_t packed = animParams.load();
    const uint8_t p[4] = {static_cast<uint8_t>(packed & 0xFF), static_cast<uint8_t>((packed >> 8) & 0xFF),
                          static_cast<uint8_t>((packed >> 16) & 0xFF), static_cast<uint8_t>((packed >> 24) & 0xFF)};
    // Half-resolution mode: the animation renders a 240x240 image and each
    // pixel is doubled on the way into the band buffer. Every animation here
    // is a smooth procedural field -- gradients, glows, warped curtains -- with
    // no text and no hard one-pixel detail, so the resolution it is computed at
    // is a quality dial rather than a correctness property. It costs a quarter
    // of the per-pixel work, which is the only thing on the critical path large
    // enough to matter for the heavy animations.
    const bool half = halfRes;
    const int rw = half ? w / 2 : w;
    const int rh = half ? h / 2 : h;
    const BgAnimation &anim = bg_animation(id);
    if (id != initializedAnimId || half != initializedHalf) {
        // Hand back the outgoing animation's tables before the incoming one
        // asks for its own. Two reasons, and the second is a correctness one.
        //
        // Memory: animations allocate lazily and used to hold their tables for
        // the rest of the boot, so the fleet's internal-SRAM cost was
        // sum-over-animations against a fixed ceiling. Releasing here caps it
        // at max-over-animations and stops registration order from deciding
        // which animations get SRAM.
        //
        // Correctness: per-row and per-column tables are sized from the w/h of
        // the init() that allocated them, behind an `if (ptr == nullptr)`
        // guard. This branch already fires on a resolution change, but that
        // guard made the re-init a no-op, leaving band() to walk a 240-entry
        // table across 480 columns. Freeing first makes the reallocation real.
        //
        // Safe to free here specifically: this runs on the render task, which
        // is the only task that calls init(), frame() or band().
        // residentAnimId, not initializedAnimId: start() clears the latter to
        // force this branch, and gating on it would skip the release on the
        // first frame after every restart -- leaving init() to no-op against
        // non-null pointers and, after a resolution change, leaving band() to
        // walk tables sized for the previous resolution.
        if (residentAnimId >= 0) {
            const BgAnimation &prev = bg_animation(residentAnimId);
            if (prev.release != nullptr) {
                prev.release();
                residentAnimId = -1;
            }
        }
        // Before init(), not after it succeeds. init() is what allocates, and
        // it can allocate several tables and then fail on a later one, leaving
        // the earlier pointers live. Recording residency only on success would
        // orphan those: the next pass would skip release(), init() would skip
        // reallocating the surviving buffers because they are non-null, and at
        // a larger resolution frame() would write past the end of one sized for
        // the smaller. Marking it resident up front costs nothing when init()
        // succeeds and makes the failure recoverable.
        if (anim.release != nullptr) {
            residentAnimId = id;
        }
        if (!anim.init(rw, rh)) {
            log_e("SleepAnimation: init failed for animation %d (%s)", id, anim.id);
            running = false;
            return;
        }
        initializedAnimId = id;
        initializedHalf = half;
    }
    BENCH_T0(tSetup);
    anim.frame(tMs, rw, rh, p);
    BENCH_ACC(accBandUs, tSetup);

    // One overlay for the whole frame; a publish mid-frame lands next frame.
    // The load/store/load dance closes the race with publishOverlay: after it,
    // overlayInUse is guaranteed to name the overlay we actually read.
    int ofi;
    do {
        ofi = overlayFront.load();
        overlayInUse.store(ofi);
    } while (ofi != overlayFront.load());
    const Overlay *ov = ofi >= 0 ? &overlays[ofi] : nullptr;
    const int ovXoff = ov != nullptr ? (ov->w - w) / 2 : 0;
    const int ovYoff = ov != nullptr ? (ov->h - h) / 2 : 0;

#ifdef GM_ANIM_BENCH
    // Advance which band gets the suspended render (see the note by
    // lockThisBand). h/BAND_H rounded up, so the last short band is included.
    benchLockBand = (benchLockBand + 1) % static_cast<uint32_t>((h + BAND_H - 1) / BAND_H);
#endif
    for (int y0 = 0; y0 < h && running; y0 += BAND_H) {
        const int rows = (y0 + BAND_H <= h) ? BAND_H : (h - y0);
        // Wait for the push task to finish with this slot. On the first band
        // of a frame this is normally already free; mid-frame it is where the
        // render task blocks if band+blend is faster than push, which is
        // exactly the intended behaviour -- the pipeline runs at the slower
        // stage's rate rather than the sum of both.
        const int64_t tWait = esp_timer_get_time();
        const bool gotSlot =
            xSemaphoreTake(static_cast<SemaphoreHandle_t>(bandFree[renderSlot]), pdMS_TO_TICKS(1000)) == pdTRUE;
        const uint32_t waitUs = static_cast<uint32_t>(esp_timer_get_time() - tWait);
        frameWaitUs += waitUs;
#ifdef GM_ANIM_BENCH
        accWaitUs += waitUs;
#endif
        if (!gotSlot) {
            log_w("SleepAnimation: push task stalled, dropping frame");
            return;
        }
        uint16_t *const band = bandBuf[renderSlot];
        // Decided once per band and used twice: the blend skips rows this frame
        // will not push, and the push job carries the parity. Interlacing only
        // applies to the two-task push path -- the direct path writes the
        // framebuffer itself and has no per-row call to skip.
        // One decision per band, read by the render, the blend and the push, so
        // the three cannot disagree about which rows this frame owns. Two things
        // switch it off beyond the feature flag:
        //
        // warmupFrames -- until the animation has covered the screen once, the
        // rows an interlaced frame skips still hold whatever the previous screen
        // left in the framebuffer. Gating only the push would send those rows
        // while the render and blend were still skipping them, which pushes
        // stale pixels: the opposite of what the warm-up is for.
        //
        // An odd row count -- a pair cannot be half a row. The expand loop
        // truncates at rows >> 1 and the push loop stops at y + 2 <= y1, so the
        // odd row would be neither written nor sent. 480/8 leaves no partial
        // band today, so this is a guard rather than a live case.
        const bool oddBand = (rows & 1) != 0;
        const bool bandInterlaced = interlace.load() && !(dmaActive && directPushOn.load()) &&
                                    warmupFrames.load() == 0 && !(half && oddBand);
        const int parityNow = static_cast<int>(frameParity & 1u);
        // At half resolution the unit is a row pair, one source row expanded;
        // anywhere else it is a single row.
        const bool pairMode = bandInterlaced && half;
        const bool renderSkip = pairMode && renderHalf.load();
        // Bench builds render ONE band per frame with the scheduler suspended
        // on this core. Aurora measures ~82 CPU cycles/pixel for a loop body
        // that looks like it should run in far less, and its max frame is 1.8x
        // its mean -- both consistent with the render task being preempted
        // mid-band rather than the loop being slow. The band timer is wall
        // clock, so preemption lands inside it and is indistinguishable from
        // compute. Task switches cannot happen inside a suspended section, so
        // the gap between per-row locked and per-row unlocked cost IS the
        // preemption share. Interrupts still run, so this bounds the effect
        // rather than eliminating it. One band only: suspending for a whole
        // frame would starve LVGL.
        //
        // WHICH band rotates every frame. Locking band 0 always confounds
        // preemption with content: animations whose cost varies down the
        // screen (steam rises from the bottom, fireflies are sparse, lava
        // blobs drift) make the top band cheap for reasons that have nothing
        // to do with the scheduler. The first cut of this measurement locked
        // band 0 and reported steam at "+191% preemption" purely from that.
        // Rotating means every band is locked equally often across a dwell, so
        // both populations cover the same pixels.
#if GM_BENCH_LOCK_ONE_BAND
        const bool lockThisBand = (y0 / BAND_H) == static_cast<int>(benchLockBand);
#else
        constexpr bool lockThisBand = false;
#endif
        BENCH_T0(tBand);
        if (half) {
            // Render rows/2 half-width rows, then expand 2x in both axes.
            const int hrows = rows >> 1;
            const int srcBase = y0 >> 1;
            // One band() call per source row instead of one for the whole band,
            // so the rows this frame will not push are never computed. The
            // contract takes a row count (BgAnim.h) and the last band of the
            // screen already passes a short one, so this is within it; what it
            // relies on is that no animation carries state from one call to the
            // next, which is true of all thirteen -- each derives its row terms
            // from the absolute y it is handed.
            const bool splitRender = renderSkip && hrows > 0;
            if (lockThisBand) {
                vTaskSuspendAll();
            }
            if (splitRender) {
                for (int sr = 0; sr < hrows; sr++) {
                    if (((srcBase + sr) & 1) != parityNow) {
                        continue;
                    }
                    anim.band(halfBuf + static_cast<size_t>(sr) * rw, srcBase + sr, 1, rw, tMs, p);
                }
            } else {
                anim.band(halfBuf, srcBase, hrows, rw, tMs, p);
            }
            if (lockThisBand) {
                xTaskResumeAll();
            }
            for (int sr = 0; sr < hrows; sr++) {
                if (splitRender && ((srcBase + sr) & 1) != parityNow) {
                    continue; // its pair is not going out, so do not expand it
                }
                const uint16_t *__restrict src = halfBuf + static_cast<size_t>(sr) * rw;
                uint32_t *__restrict d0 = reinterpret_cast<uint32_t *>(band + static_cast<size_t>(sr * 2) * w);
                uint32_t *__restrict d1 = reinterpret_cast<uint32_t *>(band + static_cast<size_t>(sr * 2 + 1) * w);
                // Both output rows in one pass. Each source pixel becomes a
                // pair, so one 32-bit value covers both copies, and writing it
                // to each row costs a second store rather than a second pass --
                // the earlier shape read d0 back to fill d1, which spent a load
                // per output word purely to re-fetch something already in a
                // register. This is the whole frame at half resolution: 240
                // rows x 240 words of avoidable loads.
                for (int i = 0; i < rw; i++) {
                    const uint32_t v = src[i];
                    const uint32_t pair = v | (v << 16);
                    d0[i] = pair;
                    d1[i] = pair;
                }
            }
        } else if (lockThisBand) {
            vTaskSuspendAll();
            anim.band(band, y0, rows, w, tMs, p);
            xTaskResumeAll();
        } else {
            anim.band(band, y0, rows, w, tMs, p);
        }
        BENCH_ACC(accBandUs, tBand);
#ifdef GM_ANIM_BENCH
        accBandRows += static_cast<uint32_t>(rows);
        if (lockThisBand) {
            accBandLockedUs += static_cast<uint64_t>(esp_timer_get_time() - tBand);
            accBandLockedRows += static_cast<uint32_t>(rows);
        }
#endif
#ifdef GM_ANIM_BENCH
        // Overwrite whatever the animation produced, after any half-resolution
        // expansion, so what lands in the framebuffer is exactly the pattern
        // regardless of how the band was generated. The composite is skipped
        // too: the widgets are the one thing the host cannot predict.
        const bool patternMode = patternOn.load();
        if (patternMode) {
            for (int r = 0; r < rows; r++) {
                uint16_t *const prow = band + static_cast<size_t>(r) * w;
                const int py = y0 + r;
                for (int x = 0; x < w; x++) {
                    prow[x] = benchPatternPx(x, py);
                }
            }
        }
#endif
        BENCH_T0(tBlend);
#ifdef GM_ANIM_BENCH
        // Locals, not the uint64_t members: a member increment in the innermost
        // per-pixel loop is two loads, an add-with-carry and two stores, and it
        // pins `this` for the whole loop. Charging that to the blend made the
        // stage look ~2x its real cost and sent an earlier round of work at a
        // memory-layout problem the blend did not have.
        uint32_t spanPxLocal = 0;
        uint32_t probeAcc = 0;
        uint32_t scrimPxLocal = 0;
#endif
        // Text scrim strength, Q8. Zero whenever the user has it off or the
        // grids could not be allocated, and that zero is what makes it free
        // when unused: pass one is skipped outright rather than run with a
        // no-op factor.
        const int scrim = (ov != nullptr && ov->scrim != nullptr) ? scrimQ8.load() : 0;
#ifdef GM_ANIM_BENCH
        const int probe = blendProbe.load();
#endif
        // Rows this frame will not push are thrown away, so compositing
        // widgets into them is wasted. Both rows of a pushed pair still need it.
        for (int y = y0; y < y0 + rows && ov != nullptr && !patternMode; y++) {
            if (bandInterlaced && ((((pairMode ? (y >> 1) : y) ^ parityNow) & 1) != 0)) {
                continue;
            }
            uint16_t *const drow = band + static_cast<size_t>(y - y0) * w;
            // Pass one: dim the halo. Its own runs, at cell resolution, because
            // the halo reaches 12 pixels past the glyphs and into rows that
            // hold no glyph at all.
            if (scrim != 0) {
                const int cy = y >> SCRIM_SHIFT;
                const int nHalo = ov->haloN[cy];
                if (nHalo != 0) {
                    scrimRow(drow, ov->scrim + static_cast<size_t>(cy) * ov->scrimW,
                             ov->haloRuns + static_cast<size_t>(cy) * RUNS_PER_ROW, nHalo, w);
#ifdef GM_ANIM_BENCH
                    for (int i = 0; i < nHalo; i++) {
                        const uint32_t r = ov->haloRuns[static_cast<size_t>(cy) * RUNS_PER_ROW + i];
                        scrimPxLocal += ((r >> 16) - (r & 0xFFFFu)) << SCRIM_SHIFT;
                    }
#endif
                }
            }
            // Pass two: composite the widgets, over the glyph runs only.
            const int nRuns = ov->runN[y];
            if (nRuns == 0) {
                continue;
            }
            const uint32_t *const runs = ov->runs + static_cast<size_t>(y) * RUNS_PER_ROW;
#ifdef GM_ANIM_BENCH
            for (int i = 0; i < nRuns; i++) {
                spanPxLocal += (runs[i] >> 16) - (runs[i] & 0xFFFFu);
            }
            if (probe == 1) {
                continue; // row and run walk only, no pixels touched
            }
#endif
            const uint8_t *const crow = ov->buf + (static_cast<size_t>(y + ovYoff) * ov->w + ovXoff) * 3;
#ifdef GM_ANIM_BENCH
            if (probe >= 2) {
                probeAcc += blendRowProbe(drow, crow, runs, nRuns, probe);
                continue;
            }
#endif
            blendRow(drow, crow, runs, nRuns);
        }
        BENCH_ACC(accBlendUs, tBlend);
#ifdef GM_ANIM_BENCH
        accSpanPx += spanPxLocal;
        accScrimPx += scrimPxLocal;
        benchProbeSink += probeAcc;
#endif

        // Compact the band to just the columns the round panel actually shows.
        // Rows are written full-width by the animations; here each row's
        // visible span is moved down to a tight [0, cw) stride so pushColors
        // can take it as a rectangle. The move is always backwards within the
        // same buffer (dst offset r*cw <= src offset r*w + x0 for every r), so
        // it is safe in place and needs no second buffer.
        // The direct path leaves the band full-width and contiguous so it can
        // go out as a single transfer. Cropping would buy back the ~21% of
        // pixels outside the round panel's circle, but each cropped row is a
        // separate run in the framebuffer and would need its own descriptor to
        // step the stride. At 48 MB/s those corners cost less than the
        // descriptors and the extra pack pass would.
        const bool directPush = dmaActive && directPushOn.load();
        const bool crop = cropEnabled && !directPush;
        const int bi = y0 / BAND_H;
        const int cx0 = crop ? bandX0[bi] : 0;
        const int cx1 = crop ? bandX1[bi] : w;
        const int cw = cx1 - cx0;
        BENCH_T0(tPack);
        if (cw < w) {
            // Explicit forward word copy, NOT memmove. The regions overlap
            // (same buffer) so memmove is the only correct libc call, and
            // newlib's memmove takes a byte-at-a-time path for overlap --
            // measured at 23 MB/s, which cost 18 ms/frame and cancelled the
            // entire saving the crop was meant to produce. Forward is provably
            // safe here because the destination is always below the source
            // (r*cw <= r*w + cx0 for every r, since cw <= w), and computeChords
            // forces cx0 and cw even so both pointers stay 4-byte aligned:
            // two pixels move per store.
            for (int r = 0; r < rows; r++) {
                uint32_t *__restrict dst = reinterpret_cast<uint32_t *>(band + static_cast<size_t>(r) * cw);
                const uint32_t *__restrict src =
                    reinterpret_cast<const uint32_t *>(band + static_cast<size_t>(r) * w + cx0);
                const int n = cw >> 1;
                int i = 0;
                // 8 pixels per iteration: the copy is load-use bound, so
                // batching the loads hides their latency behind each other
                // instead of stalling once per word.
                for (; i + 4 <= n; i += 4) {
                    const uint32_t a = src[i], b = src[i + 1], c = src[i + 2], d = src[i + 3];
                    dst[i] = a;
                    dst[i + 1] = b;
                    dst[i + 2] = c;
                    dst[i + 3] = d;
                }
                for (; i < n; i++) {
                    dst[i] = src[i];
                }
            }
        }
        BENCH_ACC(accPackUs, tPack);

        // pushColors' width/height params are actually END coordinates — they
        // pass through unchanged to esp_lcd_panel_draw_bitmap (exclusive end).
        // Passing dimensions here asserted in rgb_panel_draw_bitmap on the
        // second band (y_start==y_end) and boot-looped sleep3/sleep4.
        if (directPush) {
            // One contiguous run: full-width rows are adjacent in both the band
            // buffer and the framebuffer, and the panel only hands over a
            // framebuffer whose base and row stride both carry the data cache
            // line's alignment.
            //
            // Nothing is awaited. The transfer reads the slot on its own time
            // and its completion interrupt gives bandFree[renderSlot], which is
            // exactly what this loop takes before refilling that slot, so the
            // render of band N+1 overlaps the transfer of band N.
            BENCH_T0(tPush);
            const size_t bytes = static_cast<size_t>(w) * rows * 2;
            uint16_t *const dstRow = fbDirect[fbBack] + static_cast<size_t>(y0) * w;
            // Shut pushColors out for as long as this FRAME has transfers in
            // flight. esp_lcd_panel_draw_bitmap ends with a cache writeback
            // over whole SCANLINES, so letting another writer in mid-transfer
            // drops stale full-width lines on top of DMA-written pixels.
            //
            // Per frame, not per band. Gating each band individually made the
            // render task block on the previous band's completion before it
            // could even submit the next, which serialised render against DMA
            // and cost a whole transfer of stall per band: measured 25.5 fps
            // against 58 for the CPU path at half the rows. Per frame there is
            // one handoff, the two stages overlap, and any other writer still
            // waits at most one frame.
            const bool lastBandOfFrame = (y0 + BAND_H >= h);
            if (!frameGateHeld) {
                display->lockFrameBuffer();
                frameGateHeld = true;
            }
            BandDone &done = bandDone[renderSlot];
            done.slot = bandFree[renderSlot];
            done.gate = lastBandOfFrame ? static_cast<void *>(g_sleepAnimFbGate) : nullptr;
            if (lastBandOfFrame) {
                frameGateHeld = false; // the completion interrupt owns the release now
            }

            // One submission for the whole band, into descriptors that were
            // built at install and are only re-pointed here.
            //
            // A DMA descriptor tops out at 4092 bytes, so a whole 12-row band
            // (11,520) has to be split. BandDma splits it at install time, at
            // four rows: 3,840 bytes fits one descriptor and is a whole number
            // of rows, so every chunk's destination (fb + row * 960) keeps the
            // framebuffer base's alignment, which the panel checked against the
            // data cache line before handing the pointer over.
            esp_err_t err = ESP_OK;
            if (bandDma.ready()) {
                dmaIssued++;
                err = bandDma.submit(renderSlot, dstRow, band, bytes, &done);
                if (err != ESP_OK) {
                    dmaIssued--;
                }
            } else {
                err = ESP_ERR_INVALID_STATE;
            }
            (void)bytes;
            if (err != ESP_OK) {
                // A dropped band is a visible tear, so fall back to the CPU
                // copy. The gate has to be released by hand first: the failed
                // submission is the one that would have carried the release
                // arg, so no interrupt is coming for it, and pushColors takes
                // the same gate.
                dmaErrors++;
                dmaIssued--;
                // Earlier bands may still be in flight, reading their slot
                // and writing the framebuffer. Let them retire before the CPU
                // fallback touches either, or the fallback races a transfer.
                const unsigned long chunkDrain = millis() + 50;
                while (dmaIssued.load() != g_sleepAnimDmaDone && millis() < chunkDrain) {
                    taskYIELD();
                }
                // The failed submission is the one that would have carried the
                // release, so no interrupt is coming for this band's gate. Drop
                // it unconditionally: the next band re-takes it, and pushColors
                // below wants it.
                display->unlockFrameBuffer();
                frameGateHeld = false;
                display->pushColors(0, y0, w, y0 + rows, band);
                xSemaphoreGive(static_cast<SemaphoreHandle_t>(bandFree[renderSlot]));
            }
            BENCH_ACC(accPushUs, tPush);
        } else {
            const uint8_t pushMode = !bandInterlaced ? 0 : (pairMode ? 2 : 1);
            pushJob[renderSlot] = {static_cast<int16_t>(cx0), static_cast<int16_t>(y0), static_cast<int16_t>(cx1),
                                   static_cast<int16_t>(y0 + rows), pushMode,
                                   static_cast<uint8_t>(parityNow)};
            xSemaphoreGive(static_cast<SemaphoreHandle_t>(bandReady[renderSlot]));
        }
        renderSlot = (renderSlot + 1) % NUM_SLOTS;
    }
}

#endif // GAGGIMATE_SIM
