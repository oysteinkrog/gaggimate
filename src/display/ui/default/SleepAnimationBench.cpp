// The bench build's half of SleepAnimation (GM_ANIM_BENCH, display-bench):
// the registry sweep with its stage accumulators, the flash and pattern
// fills, the pixel-exact captures and the vector-kernel self-test. The
// render path reaches it through the hooks in SleepAnimationProbe.h; nothing
// here is compiled into any other build.
#ifndef GAGGIMATE_SIM
#ifdef GM_ANIM_BENCH

#include "SleepAnimation.h"
#include "SleepAnimationInternal.h"
#include "SleepAnimationProbe.h"
#include <Arduino.h>
#include <display/drivers/common/Display.h>
#include <display/ui/default/bganim/BgAnim.h>
#include <esp_cache.h> // esp_cache_msync, before the framebuffer copy
#include <esp_timer.h>
#include <string.h>

// How long to sit on each animation before recording it. Long enough that the
// mean is not dominated by the first frames, where the lazy LUT init runs.
constexpr unsigned long BENCH_DWELL_MS = 6000;

namespace {
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

// Bench only: every input the vector kernel can ever see, checked against the
// scalar one on the silicon that will run it.
//
// The host can only confirm the algebra. What it cannot confirm is that this
// core's EE.VMUL.U16 really keeps a 32-bit product before the shift, that the
// assembler encoded what was meant, or that the 128-bit accesses land where
// they were pointed -- and all three fail silently, as wrong colours rather
// than as a fault. 65,536 colours by 33 factors is the whole input space, so a
// pass here is exhaustive rather than a sample.
//
// ~135 ms of solid compute, so it blocks whichever task calls it. Diagnostic
// only, never on a frame path.
static uint32_t pieSelfTest(uint32_t *firstBad) {
    alignas(16) uint16_t px[8];
    alignas(16) uint16_t iv[8];
    uint32_t bad = 0;
    for (uint32_t inv = 0; inv <= SCRIM_INV_NONE; inv++) {
        for (int i = 0; i < 8; i++) {
            iv[i] = static_cast<uint16_t>(inv);
        }
        for (uint32_t c = 0; c < 65536; c += 8) {
            for (int i = 0; i < 8; i++) {
                px[i] = static_cast<uint16_t>(c + i);
            }
            probeKernelScale565Oct(px, iv, 1);
            for (int i = 0; i < 8; i++) {
                const uint16_t want = scale565(static_cast<uint16_t>(c + i), inv);
                if (px[i] != want) {
                    if (bad == 0 && firstBad != nullptr) {
                        // colour, factor, what came back, what was wanted
                        *firstBad = (c + i) | (inv << 16);
                    }
                    bad++;
                }
            }
        }
    }
    return bad;
}

} // namespace

uint32_t SleepAnimation::benchPieSelfTest(uint32_t *firstBad) { return pieSelfTest(firstBad); }

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
    // Interleaved RGB565 plus a coverage byte for the dump's readers.
    const uint16_t *col = ov.col(overlayPlanePx);
    const uint16_t *a16 = ov.a16(overlayPlanePx);
    const size_t n = static_cast<size_t>(ov.w) * ov.h;
    for (size_t i = 0; i < n; i++) {
        out[i * 3] = static_cast<uint8_t>(col[i]);
        out[i * 3 + 1] = static_cast<uint8_t>(col[i] >> 8);
        out[i * 3 + 2] = a16[i] > 255 ? 255 : static_cast<uint8_t>(a16[i]);
    }
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

bool SleepAnimation::benchFillBandDiag(uint16_t *band, int y0, int rows, int w, int h) {
    // Overwrite whatever the animation produced, after any half-resolution
    // expansion, so what lands in the framebuffer is exactly the pattern
    // regardless of how the band was generated. The composite is skipped
    // too: the widgets are the one thing the host cannot predict.
    // A tearing test a bad camera can still answer.
    //
    // The reason the webcam kept reporting corruption that was not in
    // memory is that a dark room drives its exposure to tens of
    // milliseconds, so one photo integrates ten panel frames. Integration
    // blends, though -- it cannot invent an edge. So drive the whole
    // screen to one of two maximally distinct colours on alternating
    // frames and the signature becomes spatial instead of temporal: a
    // correct flip can only ever photograph as uniform red, uniform blue,
    // or a uniform mix of the two, at any exposure. A horizontal boundary
    // between the two colours means part of the panel was scanning one
    // frame while part scanned the next, which is exactly tearing, and no
    // exposure time can fake it.
    //
    // frameParity advances once per frame, so every band of a frame picks
    // the same colour. The fill covers all rows of the band whatever the
    // interlace is doing, or the untouched rows would themselves read as
    // a tear.
    //   1 -- alternate per frame, the actual test
    //   3 -- alternate every 32 frames instead of every frame. Mode 1 came
    //        back uniformly red in every photo, which has two possible
    //        causes: the flip never reaches the panel and one buffer is
    //        stuck on screen, or the render rate and the panel's 61 Hz
    //        scan are close enough to a harmonic that the shutter keeps
    //        landing on the same parity. Half a second per colour is far
    //        longer than any exposure and beats any harmonic, so if the
    //        panel still never goes blue, the flip is the problem. Both
    //        the pattern test and mode 1 render identical content every
    //        frame, so neither can see a stuck flip on its own.
    //   2 -- a fixed red-over-blue split at mid-screen, which is what a
    //        tear looks like, as the negative control. Without it a clean
    //        result proves nothing: at these exposures the two colours
    //        partly blend, and a test whose contrast has been washed out
    //        reports no tear because it can no longer see one. Mode 2
    //        holds the edge still so the same measurement has to find it.
    //   4 -- a static fiducial for horizontal scanout displacement, which
    //        modes 1-3 cannot see at all: they fill whole rows with one
    //        colour, so every pixel in a row is identical and shifting the
    //        row sideways changes nothing a camera could record. The RGB
    //        peripheral clocks HSYNC and VSYNC off its own counters
    //        regardless of whether the DMA kept up, so a PSRAM underrun
    //        desynchronises the pixel stream against the sync signals and
    //        the picture shifts horizontally without the framebuffer ever
    //        being wrong. /api/fbdump is therefore blind to it by
    //        construction and only the panel's own output can show it.
    //
    //        Three vertical bars at deliberately unequal spacing, plus one
    //        horizontal bar. Unequal spacing is the point: a periodic
    //        grating shifted by a whole period is indistinguishable from
    //        one not shifted at all, so a regular pattern can report clean
    //        while displaced. The scene is static, which removes the other
    //        confound -- with nothing moving, any displacement a photograph
    //        records belongs to the panel and not to the animation.
    //
    //        Reading the result: a bar that is ragged or stepped means the
    //        displacement varies line to line, while several clean copies
    //        of the same bar mean the scanout phase was stable within a
    //        frame but moved between frames during the exposure. The
    //        horizontal bar catches the vertical component, since a shift
    //        large enough to wrap carries pixels onto the next line.
    const int flashMode = flashOn.load();
    if (flashMode == 4) {
        // Dark grey rather than black: the camera's auto-exposure hunts on
        // a near-black field and returns unusable frames (measured at
        // roughly one in seven), and a bar blooming out of pure black is
        // harder to locate than one on a ground the sensor can meter.
        const int bw = w >= 400 ? 6 : 4;
        const int bx0 = w / 8, bx1 = (w * 2) / 5, bx2 = (w * 5) / 6;
        const int by = h / 4, bh = bw;
        for (int r = 0; r < rows; r++) {
            const int py = y0 + r;
            uint16_t *const prow = band + static_cast<size_t>(r) * w;
            const bool hbar = py >= by && py < by + bh;
            for (int x = 0; x < w; x++) {
                const bool vbar = (x >= bx0 && x < bx0 + bw) || (x >= bx1 && x < bx1 + bw) || (x >= bx2 && x < bx2 + bw);
                prow[x] = (hbar || vbar) ? 0xFFFF : 0x2124;
            }
        }
    } else if (flashMode != 0) {
        const uint32_t phase = flashMode == 3 ? (frameParity >> 5) : frameParity;
        const uint16_t alt = (phase & 1u) != 0 ? 0xF800 : 0x001F;
        for (int r = 0; r < rows; r++) {
            const uint16_t c = flashMode == 2 ? ((y0 + r) < (h / 2) ? 0xF800 : 0x001F) : alt;
            const uint32_t pair = static_cast<uint32_t>(c) | (static_cast<uint32_t>(c) << 16);
            uint32_t *const prow = reinterpret_cast<uint32_t *>(band + static_cast<size_t>(r) * w);
            for (int x = 0; x < w / 2; x++) {
                prow[x] = pair;
            }
        }
    }
    // pattern 1 replaces the animation and skips the composite, so the
    // host can predict every pixel. pattern 2 keeps the composite, which
    // the host cannot predict -- but it makes the background static, and a
    // static scene is what lets the vector scrim be compared against the
    // scalar one end to end: same scene, two kernels, the dumps must match
    // byte for byte. The exhaustive kernel test covers the arithmetic;
    // this covers the plumbing around it.
    const int patternLevel = patternOn.load();
    const bool patternMode = patternLevel == 1 || flashMode != 0;
    if (patternLevel != 0) {
        for (int r = 0; r < rows; r++) {
            uint16_t *const prow = band + static_cast<size_t>(r) * w;
            const int py = y0 + r;
            for (int x = 0; x < w; x++) {
                prow[x] = benchPatternPx(x, py);
            }
        }
    }
    return patternMode;
}

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
        animParams.store(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
                         (static_cast<uint32_t>(p[3]) << 24));
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
        r.bandLockedNsPerRow = accBandLockedRows > 0 ? static_cast<uint32_t>(accBandLockedUs * 1000ULL / accBandLockedRows) : 0;
        r.valid = true; // publish last: readers on other tasks gate on this
        log_i("animbench: %-10s band=%u us blend=%u us push=%u us total=%u us max=%u us fps=%u.%02u", bg_animation(id).id,
              r.bandUs, r.blendUs, r.pushUs, r.totalUs, r.maxTotalUs, r.achievedFps / 100, r.achievedFps % 100);
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

#endif // GM_ANIM_BENCH
#endif // GAGGIMATE_SIM
