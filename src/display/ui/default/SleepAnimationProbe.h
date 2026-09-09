#ifndef SLEEPANIMATION_PROBE_H
#define SLEEPANIMATION_PROBE_H
// What the render path leaves for the bench and probe builds, and what each
// hook expands to in each build. Included by SleepAnimation.cpp and the probe
// translation units next to it, never by a client of the class.
//
// The flags (platformio.ini) and the unit that carries each one's code:
//   GM_ANIM_BENCH   display-bench. The registry sweep with its stage
//                   accumulators, the flash and pattern fills, the pixel-exact
//                   captures and the vector-kernel self-test
//                   (SleepAnimationBench.cpp).
//   GM_TOUCH_PROBE  display-loadtest, display-blestress, display-qemu. The
//                   touch latency stamps (GM_TOUCHLAT, GM_ELEM), the UI-stat
//                   counters and the scrim slice of the blend stage
//                   (SleepAnimationTouchProbe.cpp).
//   GM_BLEND_PROBE  either of the above (SleepAnimation.h defines it). The
//                   blend decomposition levels 1 to 9 (SleepAnimationBlendProbe.cpp).
//   GM_KBLOB        display-kdev. The hot-loaded kernel blob and the
//                   cycle-count bench (SleepAnimationKBlob.cpp).
//
// The rules the hooks keep, because the render loop is IRAM-pinned and its
// production codegen is compared against a baseline ELF (tools/elf_func_diff.py):
//   - A hook that runs per band or per row is a macro or an inline that
//     expands to nothing outside its flag. The per-frame hooks may be an
//     out-of-line call inside the flag; outside it the inline stub folds.
//   - A value the render path tests (probeLevel, benchLockThisBand,
//     probeBlobActive) is a constant outside the flag, so every branch on it
//     folds away.
//   - Nothing here adds a member to the class outside its flag: the class
//     layout is part of the production codegen.
#include "SleepAnimation.h"
#include "SleepAnimationInternal.h"
#include <esp_cpu.h>
#include <esp_timer.h>
#include <stdint.h>

// ---- GM_ANIM_BENCH ----------------------------------------------------------
// Stage timers for the bench build. Outside it the marks compile to nothing,
// so the shipping render path carries no measurement overhead from them.
// The per-band stage profile on /api/debug/anim used to ride on these (a
// systimer read per mark, four marks a band); it reads the cycle counter
// through PROF_T0 now (SleepAnimation.cpp).
#ifdef GM_ANIM_BENCH
constexpr bool kAnimBench = true;
#define BENCH_T0(v) const int64_t v = esp_timer_get_time()
#define BENCH_ACC(acc, t0) (acc) += static_cast<uint64_t>(esp_timer_get_time() - (t0))
#define BENCH_ADD(acc, v) (acc) += (v)
#define BENCH_BAND_DONE(rows, locked, t0) benchBandDone((rows), (locked), (t0))
#define BENCH_SPAN_PX(acc, runs, n) (acc) += benchSpanPx((runs), (n))
#define BENCH_SCRIM_PX(acc, ov, cy, n) (acc) += benchScrimPx((ov), (cy), (n))

// Overlay pixels the composite walks on this row, and panel pixels the scrim
// pass dims on this cell row.
inline uint32_t benchSpanPx(const uint32_t *runs, int nRuns) {
    uint32_t px = 0;
    for (int i = 0; i < nRuns; i++) {
        px += (runs[i] >> 16) - (runs[i] & 0xFFFFu);
    }
    return px;
}
inline uint32_t benchScrimPx(const SleepAnimation::Overlay *ov, int cy, int nHalo) {
    uint32_t px = 0;
    for (int i = 0; i < nHalo; i++) {
        const uint32_t r = ov->haloRuns[static_cast<size_t>(cy) * RUNS_PER_ROW + i];
        px += ((r >> 16) - (r & 0xFFFFu)) << SCRIM_SHIFT;
    }
    return px;
}
// Per band: the two mode loads stay inline in renderFrame, as they were, and
// the fills themselves (benchFillBandDiag, SleepAnimationBench.cpp) are only
// called with a diagnostic mode on.
inline bool SleepAnimation::benchFillBand(uint16_t *band, int y0, int rows, int w, int h) {
    if (flashOn.load() == 0 && patternOn.load() == 0) {
        return false;
    }
    return benchFillBandDiag(band, y0, rows, w, h);
}
// Advance which band gets the suspended render (see the note by lockThisBand
// in renderFrame). h/BAND_H rounded up, so the last short band is included.
inline void SleepAnimation::benchFrameBegin(int h) {
    benchLockBand = (benchLockBand + 1) % static_cast<uint32_t>((h + BAND_H - 1) / BAND_H);
}
inline bool SleepAnimation::benchLockThisBand(int y0) const { return (y0 / BAND_H) == static_cast<int>(benchLockBand); }
inline void SleepAnimation::benchBandDone(int rows, bool locked, int64_t tBand) {
    accBandRows += static_cast<uint32_t>(rows);
    if (locked) {
        accBandLockedUs += static_cast<uint64_t>(esp_timer_get_time() - tBand);
        accBandLockedRows += static_cast<uint32_t>(rows);
    }
}
inline void SleepAnimation::benchFrameDone(int64_t frameStart) {
    const uint32_t frameUs = static_cast<uint32_t>(esp_timer_get_time() - frameStart);
    accTotalUs += frameUs;
    accFrames++;
    if (frameUs > accMaxTotalUs) {
        accMaxTotalUs = frameUs;
    }
    benchTick();
}
#else
constexpr bool kAnimBench = false;
#define BENCH_T0(v) ((void)0)
#define BENCH_ACC(acc, t0) ((void)0)
#define BENCH_ADD(acc, v) ((void)0)
#define BENCH_BAND_DONE(rows, locked, t0) ((void)0)
#define BENCH_SPAN_PX(acc, runs, n) ((void)0)
#define BENCH_SCRIM_PX(acc, ov, cy, n) ((void)0)
inline bool SleepAnimation::benchFillBand(uint16_t *, int, int, int, int) { return false; }
inline void SleepAnimation::benchFrameBegin(int) {}
inline void SleepAnimation::benchFrameDone(int64_t) {}
inline bool SleepAnimation::benchLockThisBand(int) const { return false; }
#endif

// ---- GM_TOUCH_PROBE ---------------------------------------------------------
// The scrim slice of the blend stage (blend_scrim_us) and the UI-stat
// counters (GM_UISTAT, LV_Helper.h); the frame hooks are out of line.
#ifdef GM_TOUCH_PROBE
#define PROBE_SCRIM_T0(v) const uint32_t v = esp_cpu_get_cycle_count()
#define PROBE_SCRIM_END(t0) profBlendScrimCyc += (esp_cpu_get_cycle_count() - (t0))
#define PROBE_STAT_ADD(var, v) (var) += (v)
#else
#define PROBE_SCRIM_T0(v) ((void)0)
#define PROBE_SCRIM_END(t0) ((void)0)
#define PROBE_STAT_ADD(var, v) ((void)0)
inline void SleepAnimation::probeFrameSampled(int) {}
inline void SleepAnimation::probeFramePresented() {}
inline void SleepAnimation::probeElemEdgeClose(bool) {}
inline void SleepAnimation::probeElemShown(int, int64_t) {}
#endif

// ---- GM_BLEND_PROBE ---------------------------------------------------------
#ifdef GM_BLEND_PROBE
#define PROBE_SINK(v) probeSink += (v)
// A level change back to 0 frees the captured row, so a new capture starts
// clean (probeReset).
inline int SleepAnimation::probeLevel() {
    const int level = blendProbe.load();
    if (level == 0 && probeCaptureBands != 0) {
        probeReset();
    }
    return level;
}
// The kernels are static in SleepAnimation.cpp; the probe units reach them
// through these (defined there, IRAM).
void probeKernelScale565Oct(uint16_t *dst, const uint16_t *inv, int nOct);
void probeKernelBlendRow(uint16_t *dst, const uint8_t *colour, const uint32_t *runs, int nRuns);
void probeKernelBlendRowPie(uint16_t *dst, const uint8_t *colour, const uint32_t *runs, int nRuns);
void probeKernelBlendRowPlanar(uint16_t *dst, const uint16_t *col, const uint16_t *a16, const uint32_t *runs, int nRuns,
                               bool vector);
#else
#define PROBE_SINK(v) ((void)0)
inline int SleepAnimation::probeLevel() { return 0; }
inline uint32_t SleepAnimation::probeBlendRow(uint16_t *, const uint16_t *, const uint16_t *, const uint32_t *, int, int, int,
                                              bool) {
    return 0;
}
inline void SleepAnimation::probeReset() {}
#endif

// ---- GM_KBLOB ---------------------------------------------------------------
#ifndef GM_KBLOB
inline void SleepAnimation::probeFrameBoundary() {}
inline void SleepAnimation::probeResidentSet(int) {}
inline bool SleepAnimation::probeBlobActive() const { return false; }
inline const BgAnimation *SleepAnimation::probeBlobAnim(int) const { return nullptr; }
inline bool SleepAnimation::probeReleaseBlobResident() { return false; }
#endif

#endif // SLEEPANIMATION_PROBE_H
