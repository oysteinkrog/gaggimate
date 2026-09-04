#ifndef SLEEPANIMATION_H
#define SLEEPANIMATION_H

#include <atomic>
#include <stdint.h>
#ifndef GAGGIMATE_SIM
#include <display/drivers/common/BandDma.h>
#endif

class Display;
struct BgAnimation;

#ifdef GAGGIMATE_SIM
// The simulator has no panel/FreeRTOS; stub the whole feature out.
class SleepAnimation {
  public:
    void start(Display *) {}
    void stop() {}
    bool isActive() const { return false; }
    void configure(uint8_t, const uint8_t *) {}
    void setMaxFps(uint8_t) {}
    void setHalfRes(bool) {}
    void setInterlace(bool) {}
    void setScrim(int) {}
    uint8_t *overlayBackBuffer() { return nullptr; }
    uint32_t overlayCapacity() const { return 0; }
    void publishOverlay(int, int, int, int) {}
    void publishOverlayRanges(int, int, const int (*)[2], int) {}
    int overlayBackIndex() const { return 0; }
    void requestWholeFrames() {}
    void requestBandWarmup(const int (*)[2], int) {}
};
#else

// Procedural background animation engine: renders the selected registry
// animation (see bganim/BgAnim.h) with a dedicated task directly to the panel
// in horizontal bands, bypassing the LVGL draw pipeline (an LVGL full-screen
// composite moves ~4x the PSRAM traffic and starves the RGB scan-out —
// visible as horizontal shaking). The host screen's widgets are alpha-blended
// into each band from an offscreen LVGL snapshot the UI task refreshes
// periodically, so the animation appears BEHIND the normal content while the
// host screen stays the active LVGL screen (touch keeps working). No stored
// assets — everything is generated at runtime, so OTA updates carry the
// whole feature.
class SleepAnimation {
  public:
    // What the GDMA completion interrupt is handed on the last chunk of a band.
    // Two handles rather than one because the last band of a frame also
    // releases the framebuffer gate, and the interrupt cannot look either of
    // them up: it may run with the flash cache disabled.
    struct BandDone {
        void *slot = nullptr; // SemaphoreHandle_t, the band buffer this transfer read
        void *gate = nullptr; // SemaphoreHandle_t or null, the framebuffer gate
    };

    SleepAnimation() = default;
    ~SleepAnimation();

    // Starts the render task. No-op if already running or display is null.
    void start(Display *display);
    // Signals the task to exit and blocks briefly until it has stopped.
    void stop();
    bool isActive() const { return running; }

    // Selects which registry animation renders and its 4 params (0-100 each).
    // Safe to call while running — params apply on the next frame, an id
    // change triggers the new animation's lazy init on the render task.
    void configure(uint8_t animId, const uint8_t p[4]);
    // Frame-rate cap (clamped 5-60). Lower caps cut the animation's PSRAM
    // write bandwidth — the tuning lever against scan-out underruns when the
    // panel refresh (pclk) is raised. Applies on the next frame.
#ifdef GM_ANIM_BENCH
    // Inert on the bench, like configure(): DefaultUI re-applies the stored
    // frame cap on every UI pass, and a throttled frame reports the cap
    // instead of what the pipeline actually costs.
    void setMaxFps(uint8_t) {}
#else
    void setMaxFps(uint8_t fps) { maxFps.store(fps); }
#endif

    // Render at 240x240 and double on the way out. The panel cannot do full
    // resolution and 40 fps at once: full res clears 40 on 5 of the 13
    // animations, half res on all 13. Read once per frame, so a live change
    // never splits a frame between the two.
    //
    // Interlacing pushes every other row pair and alternates each frame,
    // halving both the bytes and the driver's whole-scanline writeback. Render
    // halving follows it rather than being separately settable: the rows it
    // skips are exactly the rows interlacing was already not going to push, so
    // it changes what the frame COSTS and not what it SHOWS.
#ifdef GM_ANIM_BENCH
    void setHalfRes(bool) {}
    void setInterlace(bool) {}
    void setHalfForce(int8_t) {}
    int8_t halfForced() const { return -1; }
    void setInterlaceForce(int8_t) {}
    int8_t interlaceForced() const { return -1; }
    void setDebugPattern(int) {}
    int debugPatternOn() const { return 0; }
#else
    // A ceiling, not an instruction. Half resolution has a real quality cost
    // (see autoResolution), so which animations actually pay it is measured
    // rather than assumed; this only says whether they are allowed to.
    // Idempotent on purpose. DefaultUI::updateState() re-applies the stored
    // settings wholesale, so this is called with the SAME value repeatedly.
    // Resetting the probe unconditionally meant autoResReset was raised again
    // before autoResolution could ever settle, which pinned every animation at
    // full resolution permanently: measured 8.5 fps against a 15 fps target
    // with a 105,000 us frame against a 66,667 us budget, and no drop to half
    // ever taken because the decision was restarted every pass. Only an actual
    // change to the ceiling should cost a re-probe.
    void setHalfRes(bool on) {
        if (halfResAllowed.exchange(on) == on) {
            return;
        }
        if (!on) {
            halfRes.store(false);
        }
        autoResReset.store(true);
    }
    // Same idempotent-caller shape as setHalfRes(): DefaultUI::updateState()
    // re-applies the persisted setting on every pass regardless of whether it
    // changed, which is exactly what a debug-endpoint override needs to
    // survive. Pinned by setInterlaceForce() below, this becomes a no-op --
    // otherwise the very next UI pass after a ilace=0/interlace=0 debug
    // request silently puts the persisted value straight back, which reads
    // as a dead knob.
    void setInterlace(bool on) {
        if (interlaceForce.load() >= 0) {
            return;
        }
        interlace.store(on);
        renderHalf.store(on);
    }
    void setHalfForce(int8_t v) {
        halfForce.store(v);
        autoResReset.store(true);
    }
    int8_t halfForced() const { return halfForce.load(); }
    // Debug override for setInterlace(), mirroring halfForce/setHalfForce
    // above: -1 leaves the settings-driven value in control, 0/1 pins
    // interlace (and renderHalf, kept in lockstep the same way setInterlace()
    // does) and holds it there against DefaultUI's periodic re-apply. Applies
    // immediately on pin so the caller does not have to wait for the next UI
    // pass to see the change take effect; releasing the pin (v < 0) leaves
    // the last-applied value in place until the next settings-driven
    // setInterlace() call restores control, same as halfForce does for
    // resolution.
    void setInterlaceForce(int8_t v) {
        interlaceForce.store(v);
        if (v >= 0) {
            interlace.store(v != 0);
            renderHalf.store(v != 0);
        }
    }
    int8_t interlaceForced() const { return interlaceForce.load(); }
    void setDebugPattern(int v) { debugPattern.store(v); }
    int debugPatternOn() const { return debugPattern.load(); }
#endif
    // Read-only instrument accessors, outside the bench split on purpose: the
    // counters they expose exist in every build, and putting them on one side
    // of it once already broke display-bench while display and the load rig
    // both compiled.
    //
    // Where the DMA source slots actually landed. Reported rather than assumed:
    // the allocation falls back to PSRAM when internal DRAM is short, and which
    // one it picked decides whether a cache writeback is needed at all.
    const void *bandBufAddr(int i) const { return bandBuf[i]; }
    uint32_t msyncFailCount() const { return msyncFails.load(); }
    uint32_t msyncOkCount() const { return msyncOks.load(); }
    uint32_t fbCheckedCount() const { return fbChecked.load(); }
    uint32_t fbMismatchCount() const { return fbMismatch.load(); }
    int32_t fbLastBandIndex() const { return fbLastBand.load(); }
    int32_t fbLastSourceIndex() const { return fbLastSource.load(); }
    int32_t fbLastDeltaBands() const { return fbLastDelta.load(); }
    uint32_t lastInvalidateUs() const { return lastInvalUs.load(); }
    // Caps the animation's frame rate independently of the stored setting, to
    // measure how much of the scan-out's lost refill headroom the animation's
    // own PSRAM traffic accounts for. 0 restores the normal cap.
    void setFpsOverride(uint8_t fps) { fpsOverride.store(fps); }
    // Live re-prioritisation of the render task, to measure how much of its
    // wall time is preemption by higher-priority core-0 work: the band bracket
    // is pure compute into SRAM, so any wall time a priority bump removes was
    // never the animation's. Debug-only, not persisted, clamped to [1,4]:
    // above 4 sits inside the radio stack's priority range and a starved
    // WiFi/BLE task wedges the link rather than just skewing the measurement.
    // 3 and 4 do delay Controller::loopLogic - acceptable for a bench
    // measurement, never for a shipped default. Defined in the .cpp because
    // the handle is opaque here (this header compiles in the simulator,
    // without FreeRTOS).
    void setRenderPrio(int prio);
    int renderPrioValue() const;
    // Replaces the rendered image with a scan-out test pattern a camera can
    // decode from a single photograph, which is the only way to judge the panel
    // with nobody in front of it. See the comment at the write site.
    void setTestPattern(bool on) { testPattern.store(on); }
    bool testPatternOn() const { return testPattern.load(); }
    uint8_t fpsOverrideValue() const { return fpsOverride.load(); }
    // Tearing: live writes over frames actually checked. The denominator is
    // exposed so a zero cannot be read as clean when the check never ran.
    // Scoped to non-interlaced frames only -- see interlacedLiveWriteCount()
    // below for the frames this deliberately excludes and why a nonzero
    // liveWriteCount() stays a bug report even while interlacing runs.
    uint32_t liveWriteCount() const { return liveWrites.load(); }
    uint32_t liveWriteCheckedCount() const { return liveWriteClean.load() + liveWrites.load(); }
    // Frames where renderFrame() deliberately wrote the buffer the panel was
    // scanning, because interlacing converged fbBack onto it on purpose (see
    // directInterlacedFrame in the private section). This climbs continuously
    // while interlacing is active -- that is the mechanism working as
    // designed, not a fault -- which is exactly why it is a separate counter
    // from liveWriteCount() rather than folded into it: an interlaced run's
    // volume here would swamp the one real accidental live-write this file
    // has ever shipped with, so mixing them would have hidden it.
    uint32_t interlacedLiveWriteCount() const { return interlacedLiveWrites.load(); }
    uint32_t flipTimeoutCount() const { return flipTimeouts.load(); }
    uint32_t lastFrameUsValue() const { return lastFrameUs.load(); }
    uint32_t lastWorkUsValue() const { return lastWorkUs.load(); }
    uint32_t lastWaitUsValue() const { return lastWaitUs.load(); }
    uint32_t lastBandUsValue() const { return lastBandUs.load(); }
    uint32_t lastExpandUsValue() const { return lastExpandUs.load(); }
    uint32_t lastFillUsValue() const { return lastFillUs.load(); }
    uint32_t lastCopyUsValue() const { return lastCopyUs.load(); }
    const void *halfBufAddr() const { return halfBuf; }
    uint32_t lastBlendUsValue() const { return lastBlendUs.load(); }
    uint32_t lastMsyncUsValue() const { return lastMsyncUs.load(); }
    uint32_t lastPushUsValue() const { return lastPushUs.load(); }

    // Text scrim: how far to dim the animation behind and immediately around
    // overlaid widget pixels, 0-100 percent, where 0 is off and 100 is black.
    // The one legibility control that does not change the animation anywhere
    // text is not, but it ships off: see bgAnimScrim in Settings.h for why the
    // halo costs more than it buys on most of the fleet. Applies on the next
    // frame; the halo shape itself is built at overlay-publish time.
    void setScrim(int pct) {
        if (pct < 0) {
            pct = 0;
        } else if (pct > 100) {
            pct = 100;
        }
        // Stored in Q8 so the per-pixel path multiplies and shifts rather than
        // dividing by 100. 100 percent maps to 256, which takes a fully covered
        // cell to black.
        scrimQ8.store(pct * 256 / 100);
    }

    // Overlay: an LV_IMG_CF_TRUE_COLOR_ALPHA (RGB565 + A8, 3 B/px) snapshot of
    // the standby widgets. Double-buffered: the UI task renders a snapshot
    // into overlayBackBuffer(), then publishOverlay() computes per-row alpha
    // spans and atomically flips which overlay the render task blends from.
    // Returns nullptr when the render task is still reading the back overlay
    // mid-frame (rare, ~20 ms window) — the caller just retries next UI pass.
    uint8_t *overlayBackBuffer();
    uint32_t overlayCapacity() const { return overlayCap; }
    // w/h: the snapshot's actual pixel size (may exceed the panel by the
    // object's ext draw size on each side; the blend centers it).
    // rowY0/rowY1 bound the PANEL rows whose alpha changed; only those get
    // their spans rescanned. Pass the full height after a full snapshot.
    void publishOverlay(int w, int h, int rowY0, int rowY1);
    // Same, for several disjoint row ranges in one publish (one flip). Each
    // ranges[i] is {rowY0, rowY1}. Rescanning rows between two changed widgets
    // is what made every publish cost a near-full-screen span scan when the
    // dirty regions sat at opposite ends of the screen.
    void publishOverlayRanges(int w, int h, const int (*ranges)[2], int n);
    // Which of the two overlay buffers overlayBackBuffer() hands out. The
    // caller needs it to know how much of that particular buffer is stale,
    // since the two are written alternately and a partial update is only valid
    // against what that buffer already holds.
    int overlayBackIndex() const { return (overlayFront.load() + 1) & 1; }
    // Push whole bands for the next couple of frames rather than interlacing.
    // Interlacing splits a change across two frames, which is invisible on the
    // animation (it moves smoothly and has no hard edges) but very visible on
    // UI widgets, which change in discrete steps and are full of them. So the
    // rule is: interlace the animation, never interlace a widget update.
    void requestWholeFrames() { warmupFrames.store(2); }
    // Same rule as requestWholeFrames() above, but scoped to just the bands
    // the published ranges cover, for the same 2-frame window: a widget
    // change must not straddle interlace phases across ITS OWN pixels, but
    // pixels the change never touched carry no such constraint, and forcing
    // every OTHER band full too (240 of them for a telemetry number update
    // touching 2-12) was interlacing's whole saving given straight back.
    // ranges[i] is {rowY0, rowY1}, same shape and same units as
    // publishOverlayRanges() above -- call this with the exact list just
    // handed to that publish, not a separate read of stored dirty rects, so
    // it can never see a rect the render task has not been told about yet
    // either. A range spanning the whole screen (first buffer fill,
    // geometry change, host screen swap) covers every band, so this
    // subsumes the whole-frame cases without any special-casing of its own.
    void requestBandWarmup(const int (*ranges)[2], int n);

    // Framebuffer-ownership controls, deliberately not behind GM_ANIM_BENCH.
    // Each setting produces a different visible defect and neither is visible
    // to the scan-out slip counter, so the only way to tell them apart is a
    // person watching the panel while the setting changes. Behind a build flag
    // that comparison costs a reflash each way.
    void setDirectPush(bool on) { directPushWanted.store(on); }
    bool directPush() const { return directPushWanted.load(); }
    void setDmaWanted(bool on) { dmaWanted.store(on); }
    // Reports the resolution actually in effect. setHalfRes() already exists
    // above and owns the allow/reset logic; this is only so the debug endpoint
    // can show which way the auto-resolution logic landed.
    bool halfResOn() const { return halfRes.load(); }
    // Reports whether interlacing is currently on. setInterlace() above owns
    // the write side (and keeps renderHalf in lockstep); this is only so the
    // debug endpoint can report back which way the knob is actually set,
    // rather than which way a request just asked for it to be -- the two can
    // differ if the request never arrived, or arrived before boot finished
    // applying its own default.
    bool interlaceEnabled() const { return interlace.load(); }
    // Band-DMA submit failures. Not a curiosity: on failure the band falls back
    // to pushColors, which writes fbs[cur_fb_index] -- the buffer the panel is
    // scanning -- while its neighbours went to fbDirect[fbBack]. A frame split
    // across both buffers shows as content composited twice.
    uint32_t dmaErrorCount() const { return dmaErrors.load(); }
    // submitRows() failures specifically -- a subset of the above. Falls back
    // to pushColors on only the rows this frame owned rather than the whole
    // band, so it does not carry the same double-composite risk the plain
    // dmaErrorCount() comment describes; watched separately because a rising
    // count here says the row-group mount is failing (capacity, alignment)
    // even while whole-band submits keep succeeding, which dmaErrorCount()
    // alone cannot distinguish.
    uint32_t dmaRowFallbackCount() const { return dmaRowFallbacks.load(); }
    // How many bands ran with interlace requested but half resolution
    // vetoing it -- see bandInterlaced's declaration. A soak that expects
    // half+interlace to actually interlace and instead sees fps recover to
    // the non-interlaced number should check this counter before suspecting
    // the knob is dead again: it may simply be opting out, as designed.
    uint32_t halfInterlaceVetoCount() const { return halfInterlaceVeto.load(); }
    // See interlaceBandSkips's own comment: legitimate "other phase's turn"
    // no-ops, not failures. Should track roughly half of h/BAND_H per frame
    // whenever half+interlace is forced back on for testing, and stay at
    // zero at full resolution.
    uint32_t interlaceBandSkipCount() const { return interlaceBandSkips.load(); }
    // Bands the REGIONAL warmup (requestBandWarmup(), bandWarmup[] below)
    // forced full this run, cumulative since boot -- distinct from the
    // bands warmupFrames forces full on init/geometry/host-screen changes,
    // which this does not count (see requestBandWarmup's own comment for
    // why those already arrive as a full-screen range and so are already
    // counted band-by-band the same way, just because every band matches).
    // Expect near zero on a static screen and a small number (2-12 for one
    // changed telemetry field, not all 240) right after a widget update.
    uint32_t bandsForcedFullCount() const { return bandsForcedFull.load(); }
    bool dmaPathWanted() const { return dmaWanted.load(); }
    // Band-DMA transfer durations, hardware start to EOF. The discriminating
    // measurement for the scan-out residual: BandDma.h explains what the
    // over-512 rate decides.
    void dmaXferStats(uint32_t *count, uint32_t *sumUs, uint32_t *maxUs, uint32_t *over256, uint32_t *over512) const {
        bandDma.xferStats(count, sumUs, maxUs, over256, over512);
    }

#ifdef GM_ANIM_BENCH
    // Bench build only. The render task walks the whole registry, dwelling on
    // each animation for BENCH_DWELL_MS at its default params, and records
    // where the frame time actually went. Timings come from the render task
    // itself, so they are real on-device costs including PSRAM latency and
    // whatever else shares the core -- not a host estimate.
    static constexpr int BENCH_MAX_ANIMS = 32;
    struct BenchResult {
        bool valid = false;
        uint32_t frames = 0;
        // Mean microseconds per frame, split by pipeline stage.
        uint32_t bandUs = 0;  // anim.frame() + anim.band() -- the animation's own cost
        uint32_t blendUs = 0; // overlay composite over the rendered bands
        uint32_t pushUs = 0;  // display->pushColors -- panel/PSRAM write
        uint32_t totalUs = 0; // sum of the above, measured end to end
        uint32_t maxTotalUs = 0;
        uint32_t waitUs = 0;      // render task blocked waiting for the push task to free a slot
        uint32_t packUs = 0;      // compacting each band to the round panel's visible chord
        uint32_t spanPx = 0;      // overlay pixels the composite walked, per frame
        uint32_t scrimPx = 0;     // panel pixels the scrim pass dimmed, per frame
        uint32_t achievedFps = 0; // x100, so 2997 == 29.97 fps
        // Nanoseconds per band row, measured with and without the scheduler
        // suspended on this core. Equal means the band cost is real compute;
        // unlocked >> locked means the wall-clock band timer is mostly
        // charging this task for time it spent preempted.
        uint32_t bandNsPerRow = 0;
        uint32_t bandLockedNsPerRow = 0;
    };
    // Snapshot of every completed dwell. Safe to read from another task: each
    // entry is only written once, before valid flips true.
    const BenchResult *benchResults() const { return benchDone; }
    int benchCurrentAnim() const { return animId.load(); }
    uint32_t benchPassCount() const { return benchPasses; }
    // Discards every recorded dwell and restarts the sweep from the first
    // animation. Set from the web task after changing something the timings
    // depend on (the pixel clock), so the next sweep measures the new state
    // instead of averaging across the change.
    void benchRequestReset() { benchResetPending.store(true); }
    void benchSetHalfRes(bool on) { halfRes = on; }
    bool benchHalfRes() const { return halfRes; }
    // Pin the sweep to one animation (-1 sweeps the whole registry), so a
    // change to one inner loop can be measured in one dwell instead of a full
    // 13-animation pass.
    void benchSetOnly(int id) { benchOnly.store(id); }
    // Blend-stage decomposition, by removing work from the composite:
    //   1 -- scrim pass and the run walk only, no glyph pixels touched
    //   2 -- plus the coverage load, so the PSRAM read shows on its own
    //   3 -- plus the band read-modify-write, so the SRAM traffic does too
    // Which separates the cost of deciding what to touch from the cost of
    // touching it, and that from what waits on memory.
    void benchSetBlendProbe(int level) { blendProbe.store(level); }
    // Replace the animation and the composite with a deterministic pattern the
    // host can recompute, so the render-to-framebuffer path can be checked by
    // byte comparison instead of by looking at it.
    void benchSetFlash(int on) { flashOn.store(on); }
    int benchFlash() const { return flashOn.load(); }
    void benchSetPattern(int on) { patternOn.store(on); }
    int benchPattern() const { return patternOn.load(); }
    // Dim the scrim on the PIE vector unit rather than a pixel at a time, and
    // check that kernel against the scalar one over its whole input space.
    void benchSetPie(bool on) { pieOn.store(on); }
    bool benchPie() const { return pieOn.load(); }
    void benchSetBpie(bool on) { bpieOn.store(on); }
    bool benchBpie() const { return bpieOn.load(); }
    uint32_t benchPieSelfTest(uint32_t *firstBad);
    int benchGetOnly() const { return benchOnly.load(); }
    // The sweep normally runs uncapped, because a throttled frame reports the
    // cap instead of the cost. This puts the cap back deliberately, for
    // measuring what a bounded duty cycle does to the rest of the system.
    void benchSetMaxFps(uint8_t fps) { maxFps.store(fps); }
    uint8_t benchMaxFps() const { return maxFps.load(); }
    // Direct-to-framebuffer push over GDMA, switchable per frame so both paths
    // can be measured on one flash. Requesting it is not the same as getting
    // it: the panel must hand over its framebuffer and the engine must install.
    void benchSetDma(bool on) { dmaWanted.store(on); }
    bool benchDmaWanted() const { return dmaWanted.load(); }
    // 0 = push task (esp_lcd draw_bitmap), 1 = CPU memcpy straight into the
    // panel's framebuffer, 2 = GDMA straight into it. Mode 1 exists purely to
    // bisect: it bypasses draw_bitmap exactly as mode 2 does but keeps the copy
    // an ordinary CPU one, so a fault that appears in 1 is about bypassing the
    // driver and a fault that appears only in 2 is about the transfer.
    void benchSetDirectPush(bool on) { directPushWanted.store(on); }
    void benchSetInterlace(bool on) { interlace.store(on); }
    bool benchInterlace() const { return interlace.load(); }
    void benchSetRenderHalf(bool on) { renderHalf.store(on); }
    bool benchRenderHalf() const { return renderHalf.load(); }
    bool benchDirectPush() const { return directPushOn.load(); }
    bool benchDmaActive() const { return dmaActive; }
    uint32_t benchDmaIssued() const { return dmaIssued.load(); }
    uint32_t benchDmaCompleted() const;
    uint32_t benchDmaErrors() const { return dmaErrors.load(); }
    int benchFrameBufferCount() const { return fbCount; }

    // Pixel-exact captures, so a claim about corruption can be checked against
    // bytes instead of against a photograph of a lit panel. A webcam in a dark
    // room integrates several panel frames and cannot separate a moving widget
    // from a stale one; these can.
    //
    // benchCopyFrameBuffer takes the frame gate, so what comes out is one
    // coherent frame rather than a tear, and invalidates the range first
    // because the DMA path writes it behind the cache.
    // Returns the byte count written, or 0 if the panel has no direct buffer
    // or the caller's buffer is too small.
    size_t benchCopyFrameBuffer(uint8_t *out, size_t cap, int *outW, int *outH);
    // The overlay the render task is currently blending from, alpha plane and
    // all (LV_IMG_CF_TRUE_COLOR_ALPHA, 3 B/px). Unsynchronised: the UI task
    // only ever writes the OTHER buffer, so the worst case is catching a flip
    // mid-copy, which shows up as a seam and is itself informative.
    size_t benchCopyOverlay(uint8_t *out, size_t cap, int *outW, int *outH);
    // Where the band buffers actually landed. They are the DMA source, so one
    // of them falling back to PSRAM would mean GDMA reads memory the CPU has
    // only written through the cache.
    uint32_t benchBandAddr(int i) const { return (i >= 0 && i < NUM_SLOTS) ? reinterpret_cast<uint32_t>(bandBuf[i]) : 0; }
    bool benchBandsInternal() const;
#endif

    // On-device equivalence test for an animation's assembly kernel. The
    // render task, at its next frame boundary, releases the resident animation,
    // inits `anim` at the panel size and renders every band of `frames`
    // frames through both band() and bandRef() (BgAnim.h) for three parameter
    // sets (defaults, all 0, all 100), comparing pixel for pixel and timing
    // both. Results are read back with animTestResult(); `seq` increments
    // when a run completes, so a poller can tell a fresh result from the
    // previous one. The resident animation re-inits on the following frame.
    struct AnimTestResult {
        uint32_t seq = 0;
        int anim = -1;
        int frames = 0;
        bool hasRef = false;
        bool initFailed = false;
        uint32_t bands = 0;      // band pairs compared
        uint32_t mismatchPx = 0; // pixels that differed, over all frames and param sets
        int firstFrame = -1;     // first differing pixel: frame, param set, x, y
        int firstPset = -1;
        int firstX = -1;
        int firstY = -1;
        uint16_t firstGot = 0;   // band()'s pixel there
        uint16_t firstWant = 0;  // bandRef()'s pixel there
        uint32_t bandUs = 0;     // total band() time, all frames and param sets
        uint32_t refUs = 0;      // same for bandRef()
    };
    void requestAnimTest(int anim, int frames) {
        animTestFrames.store(frames < 1 ? 1 : (frames > 64 ? 64 : frames));
        animTestReq.store(anim);
    }
    bool animTestPending() const { return animTestReq.load() >= 0; }
    // Routes the render loop through bandRef() instead of band() for every
    // animation that carries one: the within-boot A/B of a hand-written kernel
    // against the compiler's code under production conditions (push task
    // running, LVGL on the other core, real preemption), which the
    // equivalence test's back-to-back timing cannot reproduce.
    void setUseBandRef(bool on) { useBandRef.store(on); }
    bool useBandRefOn() const { return useBandRef.load(); }
    // Seqlock read: the render task takes animTestSeq odd while it writes the
    // fields and even when it is done, so a copy taken between two equal even
    // reads is a consistent snapshot.
    AnimTestResult animTestResult() const {
        AnimTestResult r;
        uint32_t s0;
        do {
            s0 = animTestSeq.load(std::memory_order_acquire);
            r = animTest;
        } while ((s0 & 1u) != 0 || s0 != animTestSeq.load(std::memory_order_acquire));
        return r;
    }

#ifdef GM_KBLOB
    // ---- hot-loaded kernel blob (bganim/KBlob.h, tools/kblob) -------------
    // The blob is addressed as one more animation slot: renderFrame() and the
    // benches resolve slots through animBySlot(), and the resident/initialised
    // bookkeeping treats KBLOB_SLOT like any registry id, so switching to or
    // away from the blob releases and re-inits tables the same way switching
    // animations does.
    static constexpr int KBLOB_SLOT = 1000;
    // Routes the live render loop into the blob's descriptor (when one is
    // installed) instead of the stored animation: the production-conditions
    // A/B and the visual check for a kernel that has not been flashed.
    // Ignored while an install is in progress: the loader is overwriting the
    // blob's text, and this is the one path that could send the render task
    // back into it before that finishes.
    void setUseBlob(bool on) {
        if (!on || !blobInstalling.load()) {
            useBlob.store(on);
        }
    }
    bool useBlobOn() const { return useBlob.load(); }
    // Brackets kblob::install(). Begin stops dispatching into the blob, asks
    // the render task to release its tables and waits up to timeoutMs for
    // the acknowledgement; true means the blob is not resident and nothing
    // will call into it until end() and a later setUseBlob(true) or a kbench
    // naming it. With the render task not running the answer is immediate:
    // whatever is resident stays resident, so begin fails if that is the
    // blob. A failed begin leaves nothing to end.
    bool kblobBeginInstall(uint32_t timeoutMs);
    void kblobEndInstall() { blobInstalling.store(false); }
    bool kblobResident() const { return blobResident.load(); }

    // Cycle-count microbench, /api/debug/kbench. On the render task, at a
    // frame boundary, for each requested variant in turn (firmware band(),
    // firmware bandRef(), blob band(), blob bandRef()): release whatever is
    // resident, init() the variant's descriptor at the panel size, then for
    // `frames` frames call frame() and time every band `n` times with the CPU
    // cycle counter. Sequential rather than interleaved so each variant gets
    // the whole hot slab, which is what it gets in production. Per band the
    // minimum of the n runs is what survives interrupts; the first run is the
    // cold number; the sum gives the mean. The blob variants' output is
    // hashed per band and compared with the firmware band()'s, so a blob that
    // is meant to be the same algorithm is checked for equality in the same
    // pass (mismatchBands, first differing frame and y0). Parameters are the
    // animation's defaults; time base as runAnimTest().
    //
    // The two kernels of one object (firmware or blob) share one init() and
    // one frame() per frame and are timed band by band on that state, in
    // alternating order: an animation whose frame() carries state (nebula's
    // scroll, starfield's RNG) would otherwise hand bandRef() a different
    // frame than band() saw, and the equality column would report the drift
    // as a kernel mismatch. Across objects the globals still differ (the
    // firmware's carry the panel's history, the blob's start zeroed), so
    // for those animations "blob vs band()" can legitimately differ while
    // "blobref vs blob" is the check that holds.
    //
    // hotLeak* are the hot-slab bytes still allocated after the object's
    // release() (leakBefore: before the bench, from whatever ran earlier);
    // the bench resets the slab after recording them, so one leaking
    // candidate does not push every later bench's tables into PSRAM.
    struct KBenchVariant {
        bool ran = false;
        bool initFailed = false;
        uint32_t bands = 0;
        uint64_t minCyc = 0;   // sum over bands of min-of-n
        uint64_t firstCyc = 0; // sum over bands of run 0
        uint64_t sumCyc = 0;   // sum over bands and runs
        uint32_t mismatchBands = 0; // vs the firmware band() (variant 0)
        int firstMismatchFrame = -1;
        int firstMismatchY = -1;
        uint32_t mismatchVsBlob = 0; // blobref only: vs the blob's band() (variant 2)
    };
    enum KBenchWhich : uint32_t { KB_BAND = 1, KB_REF = 2, KB_BLOB = 4, KB_BLOBREF = 8 };
    struct KBenchResult {
        uint32_t seq = 0;
        int anim = -1;
        int frames = 0;
        int n = 0;
        uint32_t which = 0;
        uint32_t hotLeakBefore = 0;
        uint32_t hotLeakFw = 0;
        uint32_t hotLeakBlob = 0;
        KBenchVariant v[4]; // indexed by the bit position in KBenchWhich
    };
    void requestKBench(int anim, int n, int frames, uint32_t which) {
        kbenchN.store(n < 1 ? 1 : (n > 32 ? 32 : n));
        kbenchFrames.store(frames < 1 ? 1 : (frames > 16 ? 16 : frames));
        kbenchWhich.store(which & 15u);
        kbenchReq.store(anim);
    }
    bool kbenchPending() const { return kbenchReq.load() >= 0; }
    KBenchResult kbenchResult() const {
        KBenchResult r;
        uint32_t s0;
        do {
            s0 = kbenchSeq.load(std::memory_order_acquire);
            r = kbench;
        } while ((s0 & 1u) != 0 || s0 != kbenchSeq.load(std::memory_order_acquire));
        return r;
    }
#endif

  private:
  public:
    struct Overlay {
        uint8_t *buf = nullptr;
        int w = 0;
        int h = 0;
        // What each pass has to touch, as exact per-row runs of consecutive
        // pixels rather than a bounding span or a block mask.
        //
        // The block mask this replaces marked 8-pixel groups, and the widgets
        // are scattered: of the 14,200 pixels a frame it sent the composite
        // through, only 5,300 had any coverage at all. The rest paid a load and
        // a test to learn they were transparent. Runs come out of the same scan
        // for nothing -- the scan already walks every pixel and already knows
        // where coverage starts and stops -- and they made the separate alpha
        // plane pointless too, since with no wasted pixels the coverage byte is
        // cheapest read from the snapshot alongside the colour it belongs to.
        //
        // Packed [x0 | x1 << 16), half-open, ascending, non-overlapping, so the
        // walk is one aligned load per run. RUNS_PER_ROW bounds each list; see
        // emitRun in the .cpp for what happens at the bound.
        // Glyph runs, per PANEL row. runN[y] == 0 means the row is plain
        // animation and the composite skips it entirely.
        uint32_t *runs = nullptr;
        uint8_t *runN = nullptr;

        // Text scrim, at 1/4 resolution (SCRIM_SHIFT): scrimSrc holds each
        // cell's peak widget alpha, scrim the dilated and smoothed halo the
        // blend actually reads. Two planes rather than one because the halo has
        // to be rebuilt from the original coverage every publish -- dilating in
        // place would grow the halo a little further on every refresh.
        //
        // Quarter resolution because the halo carries no fine detail: it is a
        // soft field whose whole job is to be wider than the glyphs. Full
        // resolution would cost 460 KB across both overlays for a field that is
        // blurred anyway.
        uint8_t *scrimSrc = nullptr;
        uint8_t *scrim = nullptr;
        int scrimW = 0;
        int scrimH = 0;
        // Halo runs, in scrim CELLS and per CELL row -- a quarter as many rows
        // and a quarter as many columns as the glyph runs, because that is the
        // resolution the field itself has. Read straight off the blurred grid
        // after it is built, so they cover exactly the cells that dim anything;
        // deriving them from the glyph runs instead would mean guessing how far
        // the dilate reached.
        uint32_t *haloRuns = nullptr;
        // scrimQ8 value the current scrim/haloRuns were built with; -1 means
        // never built. Lets publishes skip the rebuild when the coverage
        // grid did not change (see publishOverlayRanges).
        int scrimBuiltQ8 = -1;
        uint8_t *haloN = nullptr;
    };

    static void taskEntry(void *arg);
    void reapTasks(); // frees finished render/push tasks; owner side only
    void renderLoop();
    void renderFrame();
    // Chooses the render resolution for the running animation by measuring it.
    // See the definition for why this is a probe rather than a setting.
    void autoResolution(int id, int fps, int64_t frameUs, int64_t budgetUs);
    void buildScrim(Overlay &ov, int panelW, int panelH);
    // Rebuild only the scrim rows a source change in cell rows
    // [cellY0, cellY1] (inclusive) can reach. Requires ov to hold a completed
    // build of the otherwise-identical previous source at the same strength.
    // Proven bit-exact against the full build in
    // tools/overlaybench/scrim_regional_prove.cpp.
    void buildScrimRegional(Overlay &ov, int cellY0, int cellY1);

    Display *display = nullptr;
    void *taskHandle = nullptr;
    // SemaphoreHandle_t, created once and never deleted. The render task's
    // pacing sleep takes it with the frame-period timeout; a touch-driven
    // overlay publish gives it so the frame that samples the new snapshot
    // starts immediately instead of waiting out the rest of the period. A
    // semaphore rather than a task notify because taskHandle is cleared by
    // reapTasks after the render task finishes; giving a persistent
    // semaphore is safe whatever the task lifecycle is doing.
    void *overlayWakeSem = nullptr;
    std::atomic<bool> running{false};
    std::atomic<bool> stopped{true};

    // Band buffers, so the render task can fill one while the push task drains
    // another. band+blend is CPU work touching only internal SRAM; push is a
    // PSRAM write. They contend for almost nothing, so running them on separate
    // cores turns frame time from band+blend+push into max(band+blend, push)
    // plus one band of pipeline latency.
    //
    // Two, not three, and this is a memory decision that costs frame rate on
    // some animations. Throughput is max(render, push) either way -- extra
    // slots buy nothing in steady state -- but band render time is not uniform
    // across a frame (steam's blobs sit at the bottom, fireflies are sparse at
    // the top) while push time per band is flat. With only two slots the render
    // task blocks the moment it runs ahead, so a frame costs the sum of the
    // per-band maxima rather than the max of the two totals. A third slot
    // absorbed that variance; animations with flat per-band cost (plasma) lose
    // nothing, the lumpy ones (steam, fireflies) lose the most.
    //
    // What bought the change: each slot is w*BAND_H*2 = 7,680 B of internal
    // DRAM, and internal DRAM is what ESPAsyncWebServer needs a contiguous
    // 2,872 B of for every send round. Measured on device, the largest free
    // internal block fell to 2,804 B under four concurrent HTTP connections --
    // below that threshold -- and every response stalled silently. Four
    // connections is an ordinary browser, not a stress test. A third slot is
    // worth some fps; it is not worth a web UI that stops answering.
    static constexpr int NUM_SLOTS = 2;
    uint16_t *bandBuf[NUM_SLOTS] = {};
    BandDone bandDone[NUM_SLOTS] = {};
    void *bandReady[NUM_SLOTS] = {}; // render -> push, slot has data
    void *bandFree[NUM_SLOTS] = {};  // push -> render, slot is reusable
    // The panel rectangle each queued slot covers. pushColors takes end
    // coordinates, not extents.
    struct PushJob {
        int16_t x0, y0, x1, y1;
        // 0 = whole band in one call, 1 = every other absolute row, 2 = every
        // other row PAIR (half resolution, where a pair is one source row and
        // must never be split). parity picks which half goes out this frame.
        uint8_t mode;
        uint8_t parity;
    };
    PushJob pushJob[NUM_SLOTS] = {};
    int renderSlot = 0;       // slot the render task fills next; push task tracks its own
    bool cropEnabled = false; // crop to the panel's circle only while push is the pacing stage
    // Push every other row (or, at half resolution, row pair), alternating
    // parity each frame. Halves the bytes and, on the CPU push path, the
    // driver's writeback range; on the direct-DMA path it halves both the
    // transfer and the render, at the cost of each row (or pair) refreshing
    // at half the frame rate. Looked at on the panel at 45-59 fps: the
    // one-frame stagger between adjacent rows is not visible. One flag feeds
    // both push paths -- see bandInterlaced in renderFrame() and
    // directInterlacedFrame below for what the direct path does with it and
    // what that costs, without a second copy of this setting.
    //
    // Defaults false. Before the direct-DMA path grew its own interlaced
    // branch, a default of true here was harmless in the shipping
    // configuration: bandInterlaced's old veto (!(dmaActive &&
    // directPushOn.load())) forced it off whenever the direct path was
    // active, which is the normal case, so this flag only ever did anything
    // on the CPU-push fallback. That veto is gone now, so a true default
    // would make every boot skip flips and write the live framebuffer from
    // first frame on the shipping path -- exactly the untested-by-default
    // behaviour the interlace lane's own knob (ilace=1 / interlace=1 on
    // /api/debug/anim, see WebUIPlugin.cpp) exists to opt into deliberately.
    std::atomic<bool> interlace{false};
    // Render only the rows this frame will push. At half resolution one
    // source row feeds one pushed pair; at full resolution (direct-DMA path
    // only -- see bandInterlaced) it is one row for one row. Either way,
    // skipping it costs nothing that is displayed.
    //
    // Defaults false alongside interlace above, for the same reason: this
    // flag is meaningless while interlace is off (bandInterlaced already
    // requires interlace.load()), so it only needs to track interlace's
    // safe-default change, not add a second one of its own.
    std::atomic<bool> renderHalf{false};
    uint32_t frameParity = 0;
    // Frames after a start that push whole bands regardless of parity. Until
    // the animation has covered the screen once, the rows an interlaced frame
    // skips still hold the previous screen's pixels.
    std::atomic<uint32_t> warmupFrames{0};
    // Render at 240x240 and double on the way out. On this panel 40+ fps and
    // full resolution are mutually exclusive: full res reaches 40 on only 5 of
    // the 13 animations (nebula 15.1, mandala 16.6, silk 18.0), half res on all
    // 13. Note that the bench setter below is compiled out of env:display, so
    // this initialiser is the shipped configuration, not a starting value.
    std::atomic<bool> halfRes{true};
    // Whether halfRes is allowed to be true at all: the user setting. The
    // effective value above is chosen per animation by autoResolution().
    std::atomic<bool> halfResAllowed{true};
    // Debug override for autoResolution: -1 auto, 0 pin full, 1 pin half.
    // setHalfRes() is a ceiling, not an instruction, so it cannot be used to
    // hold a resolution for a measurement: autoResolution weighs frame time
    // against the budget and on a light screen keeps full res however loudly
    // half was allowed. A before/after that cannot pin the variable it is
    // comparing is not a comparison, and one was already read the wrong way
    // round because of this.
    std::atomic<int8_t> halfForce{-1};
    // Debug override for setInterlace(): -1 auto (settings control it), 0/1
    // pin interlace off/on. See setInterlaceForce() for why this exists --
    // without it, DefaultUI::updateState()'s unconditional periodic
    // setInterlace(settings...) call stomps a debug-endpoint request within
    // one UI pass.
    std::atomic<int8_t> interlaceForce{-1};
    // Equivalence test request and result; see requestAnimTest().
    std::atomic<int> animTestReq{-1};
    std::atomic<bool> useBandRef{false};
    std::atomic<int> animTestFrames{8};
    std::atomic<uint32_t> animTestSeq{0};
    AnimTestResult animTest;
    void runAnimTest();
#ifdef GM_KBLOB
    std::atomic<bool> useBlob{false};
    std::atomic<bool> blobInstalling{false};
    std::atomic<bool> blobResident{false};
    std::atomic<bool> blobDetachReq{false};
    std::atomic<int> kbenchReq{-1};
    std::atomic<int> kbenchN{8};
    std::atomic<int> kbenchFrames{2};
    std::atomic<uint32_t> kbenchWhich{0};
    std::atomic<uint32_t> kbenchSeq{0};
    KBenchResult kbench;
    void runKBench();
    void serviceBlobDetach();
#endif
    // Registry id, or KBLOB_SLOT for the hot-loaded blob when there is one.
    int activeSlot() const;
    const BgAnimation &animBySlot(int slot) const;
    // Releases the resident animation's tables, if any, and clears residency.
    void releaseResident();
    // Row-encoded test pattern; see the renderFrame() site for what it settles.
    std::atomic<int> debugPattern{0};
    // Set when something the decision depended on changed under it.
    std::atomic<bool> autoResReset{true};
    int autoResAnim = -1;      // animation the current decision belongs to
    uint8_t autoResFps = 0;    // and the fps target it was taken against
    uint8_t autoResSeen = 0;   // frames discarded before the window opened
    uint8_t autoResFrames = 0; // frames accumulated into autoResUs
    uint64_t autoResUs = 0;
    // Long-run watch kept AFTER the decision has settled, so a verdict reached
    // on an unrepresentative six-frame window does not stand for the rest of
    // the animation. See the re-probe branch in autoResolution().
    uint64_t autoResPostUs = 0;
    uint32_t autoResPostFrames = 0;
    bool autoResSettled = false;
    uint32_t frameWaitUs = 0; // this frame's total block on the push task, drives cropEnabled
    void *pushHandle = nullptr;
    std::atomic<bool> pushStopped{true};

    // ---- direct-to-framebuffer push ------------------------------------
    // pushColors copies the band into the PSRAM framebuffer with the CPU, and a
    // CPU write to PSRAM on this part costs about twice a read: the 32-byte
    // write-allocate line is fetched before it is overwritten, so a 460 KB
    // frame moves 920 KB of bus traffic. The direct path hands the same bytes
    // to a DMA engine instead, which never passes through the cache.
    //
    // fbDirect is the panel's own framebuffer, or null when the panel will not
    // hand it over -- in which case dmaActive stays false and the pipeline runs
    // the ordinary push task, unchanged. Only the LilyGo RGB panel offers one.
    //
    // On by default since the coherency work: the panel hands out a gate that
    // the direct writer holds for as long as a frame has transfers in flight,
    // so esp_lcd's whole-scanline cache writeback can no longer land on top of
    // one, and the cache is flushed into PSRAM on the way in and dropped on the
    // way out. Measured on the panel against the CPU push: 41.5 fps at full row
    // count against 58 fps interlaced at half, with the render task's share of
    // the push falling from 9.7 ms per frame to 0.6, and no transfer errors in
    // 72k transfers. /api/animbench?dma=0 turns it off at runtime.
    std::atomic<bool> dmaWanted{true};
    // Whether bands go straight into the panel's framebuffer over GDMA, or
    // through the ordinary two-task CPU push. Live, unlike dmaWanted, which
    // only takes effect at the next start() -- and start() never happens while
    // a bench sweep is running, so this is the switch a measurement can use.
    //
    // Three other routes lived here and are gone: a CPU memcpy under the same
    // gate, and esp_async_memcpy at one call per band and at four-row chunks.
    // They existed to find out whether a sheared picture came from the
    // transfer engine or from the panel scanning a region while it was
    // written. It was neither -- the shear was the camera's exposure
    // integrating ten panel frames -- and the framebuffer now flips at a frame
    // boundary, so there is nothing left for them to bisect. Their numbers,
    // measured on the panel, per frame:
    //
    //   4-row chunks   push 18.8 ms  25.6 fps
    //   one per band   push  9.7 ms  33.2 fps
    //   native engine  push  0.6 ms  41.5 fps
    //
    // The spread between the first two is what gives that API away: IDF 5.5's
    // esp_async_memcpy deletes and rebuilds both of its GDMA link lists from
    // the heap on every call, so its cost tracks submissions rather than bytes.
    // The native engine keeps its descriptors and only re-points them.
    // Off, because on this board the animation is not the only writer of the
    // framebuffer pair and the direct path assumes it is.
    //
    // LV_Helper hands LVGL both panel framebuffers and runs it in direct_mode,
    // so LVGL alternates between them itself and draws only invalidated areas
    // into whichever one it believes is current. The direct path renders bands
    // into fbDirect[fbBack] and then flips the panel in presentFrame(). Neither
    // owner knows about the other's flip, so the two disagree about which
    // buffer is back, and LVGL's widget pixels land in a buffer the animation
    // is about to overwrite or has already flipped away from.
    //
    // What that looks like on the panel: every glyph drawn twice about 24 rows
    // apart, the second copy partial, with the animation behind it broken into
    // displaced horizontal bands. Confirmed by dumping both framebuffers over
    // /api/debug/fb -- the duplicate is in the pixels, not the scan-out -- and
    // by this switch alone making it clean, on the same build, in the buffer
    // dump and on the panel.
    //
    // presentFrame() already states half of this hazard: flipping underneath
    // pushColors would show a buffer nothing wrote. The same argument applies
    // to LVGL and was missed.
    //
    // The dual-ownership reading above was wrong, and the real cause is now
    // known. lvgl_helper_suppress_flush() already moves LVGL off the pair
    // before start(), correctly ordered, and the doubling survived it. What
    // actually produced it: bandBuf[] is the GDMA transfer SOURCE and it lands
    // in cached PSRAM, so the CPU rendered each band into the data cache while
    // the engine read PSRAM underneath and got the slot's previous occupant.
    // With NUM_SLOTS 2 that is the band from two bands earlier, which a
    // row-encoded framebuffer dump measured as a clean -16 row offset across
    // the panel. The writeback meant to prevent it was refused every call for
    // being 36 and 40 bytes off a 64 byte cache line, and only said so on a
    // serial port nobody was reading. Aligning the allocation fixed it.
    //
    // The remaining reason this stayed off was that no instrument could see a
    // tear: framebuffer content is identical whether or not it was written
    // while being scanned, so the dump calls a torn frame perfect, and the
    // slip counter read 0.032% -- a healthy display -- through the whole
    // fault. presentFrame() now grounds that: on_frame_buf_complete confirms
    // which buffer the panel is reading, and any frame rendered into it is
    // counted (tear_live over tear_checked on /api/debug/anim).
    //
    // Measured after both fixes, under WiFi load and repeated brew cycling:
    // 0 tearing frames in 8,570 checked with 0 unconfirmed flips, and 0
    // misplaced rows in 26,400. The path this switch enables is also the only
    // one that CAN reach zero -- the ordinary push hands esp_lcd a pointer
    // outside the framebuffers, and vendor rgb_panel_draw_bitmap then copies
    // it straight into the buffer being scanned, every band of every frame.
    std::atomic<bool> directPushOn{false};
    // What the next frame should switch to. Never read inside a frame.
    //
    // directPushOn is read per band in renderFrame() and again in
    // presentFrame(), and the two paths balance the framebuffer gate
    // differently: the direct path queues DMA whose completion interrupt gives
    // the gate back, the ordinary path does not. Change it between those two
    // reads and the gate is left unbalanced -- the render task then blocks on
    // it forever, which also blocks LVGL's pushColors, and the panel freezes
    // holding whatever half-composed frame was on it. Observed exactly that,
    // live, from the debug endpoint: scan-out kept running at 45 fps and the
    // controller task kept updating temperature while the picture stood still.
    //
    // So the setting is latched once per frame at the top of renderLoop(), the
    // same place and for the same reason as frameParity.
    // Default on: see directPushOn above for why this is now the path that
    // reaches zero tearing rather than the one that caused the doubling.
    std::atomic<bool> directPushWanted{true};
    bool dmaActive = false; // fbDirect resolved AND the engine installed
    // Whether THIS frame is an interlaced pass on the direct-DMA path.
    // Latched once per frame in renderLoop(), same place and for the same
    // reason as directPushOn just above: renderFrame() decides per band
    // whether to skip a row from these same terms (bandInterlaced), and
    // presentFrame() decides from this member whether to flip at all, and a
    // change landing between the two calls would let them disagree about
    // whether this frame wrote half of fbBack or all of it, or about which
    // buffer fbBack even was.
    //
    // The double-buffered flip this path normally uses is what interlacing
    // has to give up, not what it rides on top of. The first design tried
    // here kept flipping: accumulate one phase into the off-screen buffer,
    // then the other, then flip once every two passes, so fbBack is always
    // complete and never touched live. It preserves zero tearing perfectly
    // and buys NOTHING -- a flip's row count and a flip's byte count are
    // fixed by the panel regardless of how many render passes fed it, so the
    // total bytes moved and the total render-task CPU per second of VISIBLE
    // update are identical to just running the ordinary non-interlaced path
    // at half the frame rate, with none of this feature's extra bookkeeping.
    // Interlacing's actual value -- letting a fixed bandwidth budget buy
    // twice the update FREQUENCY, each update touching half the rows, which
    // is what the CPU-push path's own long-shipped interlacing already does
    // and calls invisible at 45-59 fps -- only exists if each pass is
    // visible the moment it lands, not deferred behind a flip.
    //
    // So: no flip, ever, while this is true. renderLoop() converges fbBack
    // onto scanFb (the buffer the panel is CONFIRMED to be scanning) for as
    // long as directInterlacedFrame holds, and renderFrame()'s row-group GDMA
    // writes land directly in it -- visible as soon as the transfer completes
    // and this frame's own cache invalidate runs (see the interlaced branch
    // in renderFrame()'s directPush block). presentFrame() then has nothing
    // to do and returns immediately.
    //
    // What this costs: the rows an interlaced pass touches are written while
    // the panel may be scanning them, exactly the exposure
    // LilyGo_RGBPanel::pushColors has always had for EVERY row of EVERY
    // frame via a CPU copy through esp_lcd_panel_draw_bitmap. This narrows
    // that same exposure to only the rows a pass actually writes, over a
    // GDMA transfer measured at 70-150 us a band (BandDma's own xferStats),
    // rather than widening it. interlacedLiveWrites below counts it
    // separately from liveWrites, which stays reserved for catching this
    // happening somewhere it was NOT supposed to.
    //
    // When this goes false (warmupFrames just got set, or ilace was turned
    // off), renderLoop's wasDirectInterlacedFrame check un-converges fbBack
    // back to the off-screen buffer, because the ordinary flip machinery
    // below is about to render a full frame into it and flip as it always
    // has -- fbBack has to mean "not on screen" again for that to be correct.
    bool directInterlacedFrame = false;
    // Previous frame's directInterlacedFrame, so renderLoop can tell when
    // interlacing just STOPPED (this false, that true) and un-converge fbBack
    // back off-screen for the ordinary flip path -- see directInterlacedFrame
    // above for why that reconciliation exists at all.
    bool wasDirectInterlacedFrame = false;
    // The panel's framebuffers. With two, the frame is composed in the one the
    // scan-out is not reading and shown by flipping at the end of the frame, so
    // no pixel is ever written while it is on screen -- the difference between
    // tearing being unlikely and tearing being impossible. With one, fbBack
    // stays 0 and the writes race the beam exactly as before.
    static constexpr int FB_MAX = 2;
    uint16_t *fbDirect[FB_MAX] = {};
    int fbCount = 0;
    int fbBack = 0; // the buffer this frame is being composed into
    // Render one extra frame into the other buffer before the first flip.
    //
    // beginDirectPath() has to pick a starting fbBack and there is no way to
    // ask esp_lcd which buffer it is scanning -- cur_fb_index is private to
    // esp_lcd_panel_rgb.c and no getter is exported. So it guesses 1, assuming
    // LVGL left buffer 0 on screen, and LVGL's direct-mode parity is whatever
    // it happened to land on since boot. Guess wrong and the animation's first
    // full band sweep goes straight into the live buffer.
    //
    // Priming sidesteps the guess instead of trying to win it: fill BOTH
    // buffers before presenting, and whichever one is really on screen already
    // holds a correct frame. Costs one extra render at animation start.
    bool primePending = false;
    size_t fbBytes = 0;   // one framebuffer, for the per-frame cache invalidate
    bool dmaInstallTried = false;
#ifndef GAGGIMATE_SIM
    // The native engine. Kept beside the panel's framebuffer pointers
    // rather than replacing it so the two can be compared on the same run; only
    // the engine a mode actually asks for is ever installed.
    BandDma bandDma;
#endif
    bool nativeInstallTried = false;
    bool installNativeOnIsrCore();
    bool engineReadyForMode();
    bool gateWarned = false;    // one warning per run, not one per frame
    bool frameGateHeld = false; // the framebuffer gate is taken for this frame's transfers
    bool beginDirectPath();
    void verifyBandPlacement();
    void presentFrame();
    void endDirectPath();
    std::atomic<uint32_t> dmaIssued{0};
    std::atomic<uint32_t> dmaErrors{0};
    // submitRows() failures, counted separately from dmaErrors above (which
    // still counts them too) because a rise here specifically implicates the
    // row-group mount -- capacity or alignment -- rather than the plain
    // whole-band submit(), and the two failing at different rates is itself
    // the diagnostic.
    std::atomic<uint32_t> dmaRowFallbacks{0};
    // Bands where interlace was on but half resolution vetoed it outright --
    // see the comment at bandInterlaced's declaration for why. Counts bands,
    // not frames, so it also reads back the shape of the veto: at BAND_H's
    // current tuning a frame is h/BAND_H bands, so a soak run entirely at
    // half+interlace should see this climb by exactly that many per frame,
    // not some smaller number that would mean the veto was only catching
    // part of the screen.
    std::atomic<uint32_t> halfInterlaceVeto{0};
    // Bands where an interlaced pass legitimately had nothing to push --
    // this band's row-pair belonged to the OTHER phase this frame, not a
    // failure. See the nothingOwnedThisBand comment at the submit call site
    // for why this used to be misrouted through dmaErrors/the CPU-fallback
    // path instead: fixed now, but this counter is what proves it, and
    // what dma_row_fallbacks could not (it only ever counted a genuine
    // submitRows() failure, which this never was). At half resolution
    // (pairMode) expect this to climb by roughly half of h/BAND_H per
    // frame; at full resolution it should never move at all, since the
    // two-candidate-per-band loop there always finds one of each parity.
    std::atomic<uint32_t> interlaceBandSkips{0};
    // See bandsForcedFullCount()'s own comment. Incremented at the same
    // per-band decision point as halfInterlaceVeto/interlaceBandSkips above,
    // once per band per frame, never inside a per-pixel loop.
    std::atomic<uint32_t> bandsForcedFull{0};
    // Cache writeback outcomes for the GDMA source band. Both directions are
    // counted so "it is working" is a positive reading rather than the absence
    // of a complaint.
    std::atomic<uint32_t> msyncFails{0};
    std::atomic<uint32_t> msyncOks{0};

    // Framebuffer placement verifier.
    //
    // Every instrument this file carried was blind to the fault the panel
    // actually shows: a block of lines displaying content that belongs to a
    // different Y. tear_live compares the scan-out buffer against the render
    // target and cannot see it; the scan-out slip counters live in the panel
    // driver and cannot see it either, because the framebuffer is already
    // wrong by the time it is scanned. Both read clean while the picture was
    // visibly displacing, and every "clean" conclusion drawn from them was
    // worthless.
    //
    // So check the thing itself. Each band's rendered content gets a signature
    // taken from the middle of its first row -- the middle, because column 0 is
    // outside the round panel's circle and is the same black on every band,
    // which would make the signature collide everywhere. presentFrame() then
    // recomputes the signature from the framebuffer it is about to present, at
    // the row that band was rendered for, and compares. A mismatch is a band
    // that did not land where it belonged.
    //
    // On a mismatch the other bands' signatures are searched for the content
    // that DID land there, which turns "something is wrong" into the exact
    // displacement in bands -- the number that separates a stale slot (a fixed
    // offset of NUM_SLOTS) from anything else.
    uint16_t fbCheckCursor = 0;
    std::atomic<uint32_t> fbChecked{0};
    std::atomic<uint32_t> fbMismatch{0};
    std::atomic<int32_t> fbLastBand{-1};    // band index that held wrong content
    std::atomic<int32_t> fbLastSource{-1};  // band whose content was there instead
    std::atomic<int32_t> fbLastDelta{0};    // source - band, in bands
    std::atomic<uint32_t> lastInvalUs{0};   // cost of the pre-flip cache invalidate
    // Tearing instrument. scanFb is which framebuffer the panel was last
    // CONFIRMED to be scanning, set only after on_frame_buf_complete has fired
    // for a flip, and -1 when that confirmation timed out. liveWrites counts
    // frames rendered into that buffer, which is the only way this path can
    // tear; liveWriteClean counts the frames that were checked and were fine,
    // so a zero is distinguishable from a check that never ran.
    std::atomic<int> scanFb{-1};
    std::atomic<uint32_t> liveWrites{0};
    std::atomic<uint32_t> liveWriteClean{0};
    // Same trigger as liveWrites (fbBack == scanFb) but for frames where
    // directInterlacedFrame made that true on purpose. Excluded from
    // liveWrites/liveWriteClean entirely -- not merely tallied alongside them
    // -- so that pair keeps meaning exactly what it always has: a nonzero
    // liveWriteCount() is a bug, full stop, whether or not interlacing is
    // running at the same time.
    std::atomic<uint32_t> interlacedLiveWrites{0};
    std::atomic<uint32_t> flipTimeouts{0};
    // How long the last presentFrame() spent waiting for the scan-out to leave
    // the buffer the next frame overwrites. Render-task only, so a plain
    // member. Subtracted from the frame time before autoResolution judges it.
    uint32_t lastFlipWaitUs = 0;
    // Per-frame stage costs, render-task only so plain members. Published at
    // the end of each frame into the atomics below.
    uint32_t profBandUs = 0;   // the animation's own per-pixel field work, plus the half-res expansion
    uint32_t profExpandUs = 0; // just the 2x2 expansion inside profBandUs, zero at full resolution
    uint32_t profFillUs = 0;   // the x2 fill of the even output row, inside profExpandUs
    uint32_t profCopyUs = 0;   // duplicating that row into the odd one, inside profExpandUs
    uint32_t profBlendUs = 0; // compositing the widget overlay into the band
    uint32_t profMsyncUs = 0; // cache writeback of the GDMA source band
    uint32_t profPushUs = 0;  // handing the band to the DMA engine
    // Frame time split, for working out what actually caps the frame rate:
    // total wall clock, the part that was work, and the part that was waiting
    // for the panel. Reported rather than reasoned about, because the first
    // guess at this (autoResolution being the cap) was wrong.
    std::atomic<uint32_t> lastFrameUs{0};
    std::atomic<uint32_t> lastWorkUs{0};
    std::atomic<uint32_t> lastWaitUs{0};
    std::atomic<uint32_t> lastBandUs{0};
    std::atomic<uint32_t> lastExpandUs{0};
    std::atomic<uint32_t> lastFillUs{0};
    std::atomic<uint32_t> lastCopyUs{0};
    std::atomic<uint32_t> lastBlendUs{0};
    std::atomic<uint32_t> lastMsyncUs{0};
    std::atomic<uint32_t> lastPushUs{0};
    // Core the async-memcpy completion interrupt is bound to. Deliberately not
    // the render core: the RGB panel driver's ISR is on core 1 and must not
    // queue behind ours.
    static constexpr int DMA_ISR_CORE = 0;

    // Per-band horizontal extent of the panel's inscribed circle. The panel is
    // round, so the corners of the 480x480 rectangle are never visible and
    // pushing them is wasted PSRAM bandwidth. One rectangle per band (the
    // widest row in it), since pushColors takes a rectangle.
    static constexpr int MAX_BANDS = 256; // 480 rows / BAND_H, with headroom
    int16_t bandX0[MAX_BANDS] = {};
    int16_t bandX1[MAX_BANDS] = {};
    uint32_t bandSig[MAX_BANDS] = {}; // see the framebuffer placement verifier above
    // Per-band regional-warmup countdown: frames remaining to force this one
    // band non-interlaced. Written by requestBandWarmup() from the UI task
    // right after an overlay publish; read and decremented once per frame by
    // the render task at the same per-band decision point as warmupFrames
    // (see requestWholeFrames()'s comment for why interlacing must not run
    // across a band a widget update just touched). Atomic for the same
    // cross-core reason warmupFrames itself is: the UI task runs on core 1,
    // the render task on core 0. Array-of-atomics zero-init via `= {}` is
    // confirmed to actually zero every element (checked against this
    // toolchain's libstdc++ across every -std flag this project builds
    // with), unlike relying on each element's bare default constructor.
    std::atomic<uint8_t> bandWarmup[MAX_BANDS] = {};
    void computeChords(int w, int h);
    static void pushTaskEntry(void *arg);
    void pushLoop();

    // Animation selection; id and params may tear against each other for one
    // frame, which is harmless. Packed params: p[i] = (word >> 8*i) & 0xFF.
    std::atomic<uint8_t> animId{0};
    std::atomic<uint32_t> animParams{0};
#ifdef GM_ANIM_BENCH
    // The bench measures what the pipeline can do, so it must not sit against
    // the shipping frame cap -- a throttled frame reports the cap, not the cost.
    std::atomic<uint8_t> maxFps{60};
    // Diagnostic frame cap, 0 for off. Separate from maxFps because DefaultUI
    // re-applies the stored cap on every UI pass and would stomp a value
    // written from the debug endpoint within a frame or two.
    std::atomic<uint8_t> fpsOverride{0};
#else
    std::atomic<uint8_t> maxFps{30};
    std::atomic<uint8_t> fpsOverride{0};
#endif
    std::atomic<bool> testPattern{false};
    int initializedAnimId = -1; // last id whose init() ran on the render task
    // Which animation currently holds allocated tables, or -1 for none. Kept
    // apart from initializedAnimId because start() clears that one to force an
    // init(), and stopping the render task frees nothing -- so the tables
    // outlive it and something has to remember whose they are.
    int residentAnimId = -1;
    bool initializedHalf = false; // resolution that init() ran at; a change re-inits
    uint16_t *halfBuf = nullptr;  // (w/2)x(BAND_H/2) scratch for half-res rendering
    // One panel row of per-pixel scrim factors, internal SRAM, 16-byte aligned.
    uint16_t *scrimInvPx = nullptr;

    // Scrim strength in Q8 (0 = off, 256 = black). Read once per band by the
    // composite, so a plain relaxed load is all it needs.
    std::atomic<int> scrimQ8{0}; // off until setScrim says otherwise; see bgAnimScrim
    std::atomic<int> blendProbe{0};
    // Dim the scrim on the PIE vector unit rather than a pixel at a time.
    // Default on; the scalar path stays as the reference /api/pietest checks
    // against, and as the kernel for a run's unaligned edge cells.
    std::atomic<bool> pieOn{true};
    // Same shape for the composite pass: vector blendRowPie vs the scalar
    // blendRow it replaced. Default on; ?bpie=0 is the within-boot A/B.
    std::atomic<bool> bpieOn{true};
    std::atomic<int> patternOn{0};
    // Alternate the whole screen between two colours per frame, so tearing
    // shows up as a spatial edge a long-exposure photo cannot fabricate.
    std::atomic<int> flashOn{0};
    // Scratch grid for the separable dilate/blur passes, one shared copy: the
    // passes run to completion inside publishOverlay on the UI task, so the two
    // overlays never need it at the same time.
    uint8_t *scrimTmp = nullptr;
    // Second whole-grid scratch, used only by buildScrimRegional (see there).
    uint8_t *scrimTmp2 = nullptr;
    // Pre-scan copy of the scanned scrimSrc rows, compared after the scan so
    // an unchanged coverage grid skips the whole buildScrim. nullptr degrades
    // to always rebuilding.
    uint8_t *scrimCmp = nullptr;

    Overlay overlays[2];
    uint32_t overlayCap = 0;
    std::atomic<int> overlayFront{-1}; // -1 = nothing published yet
    std::atomic<int> overlayInUse{-1}; // overlay the render task reads this frame

#ifdef GM_ANIM_BENCH
    // Accumulators for the dwell in progress; render task only, no locking.
    uint64_t accBandUs = 0;
    uint64_t accBlendUs = 0;
    uint64_t accPushUs = 0;
    uint64_t accTotalUs = 0;
    uint64_t accWaitUs = 0;
    uint64_t accPackUs = 0;
    uint64_t accSpanPx = 0;
    uint64_t accScrimPx = 0;
    // Keeps the probe kernels' loads from being optimised away. Never read.
    volatile uint64_t benchProbeSink = 0;
    uint32_t accFrames = 0;
    uint32_t accMaxTotalUs = 0;
    uint32_t benchLockBand = 0; // which band gets the suspended render, rotates per frame
    uint64_t accBandLockedUs = 0;
    uint32_t accBandLockedRows = 0;
    uint32_t accBandRows = 0;
    unsigned long benchDwellStart = 0;
    uint32_t benchPasses = 0; // completed sweeps of the whole registry
    BenchResult benchDone[BENCH_MAX_ANIMS];
    std::atomic<bool> benchResetPending{false};
    std::atomic<int> benchOnly{-1}; // -1 sweeps the registry; otherwise pin to this id

    void benchTick();        // called once per frame from renderLoop
    void benchFinishDwell(); // records the current animation and advances
#endif
};

// The running instance, so the web plugin can publish results without the
// whole UI object graph being reachable from it. Null until start() runs.
SleepAnimation *sleep_animation_bench_instance();

#ifdef GM_ANIM_BENCH

// Why the animation is or is not running. maintainSleepAnimation() has several
// gates and none of them are visible from outside the UI, which makes a
// silently idle bench impossible to diagnose over the network.
struct BenchGateState {
    bool uiInitialized = false;
    bool blocked = false;
    bool wantAnimation = false;
    bool animActive = false;
    int mode = -1;
    int screen = -1;
    unsigned long lastStartAttempt = 0;
    bool startFailed = false;
};
const BenchGateState &bench_gate_state();
#endif

#endif // GAGGIMATE_SIM

#endif // SLEEPANIMATION_H
