#ifndef SLEEPANIMATION_H
#define SLEEPANIMATION_H

#include <atomic>
#include <stdint.h>
#ifndef GAGGIMATE_SIM
#include <display/drivers/common/BandDma.h>
#endif

class Display;

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
    int overlayBackIndex() const { return 0; }
    void requestWholeFrames() {}
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
#else
    void setHalfRes(bool on) { halfRes.store(on); }
    void setInterlace(bool on) {
        interlace.store(on);
        renderHalf.store(on);
    }
#endif

    // Text scrim: how far to dim the animation behind and immediately around
    // overlaid widget pixels, 0-100 percent, where 0 is off and 100 is black.
    // The one legibility control that does not change the animation anywhere
    // text is not, which is why it is the one that defaults on. Applies on the
    // next frame; the halo shape itself is built at overlay-publish time.
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
        uint32_t waitUs = 0; // render task blocked waiting for the push task to free a slot
        uint32_t packUs = 0; // compacting each band to the round panel's visible chord
        uint32_t spanPx = 0;  // overlay pixels read per frame (span-limited)
        uint32_t blendPx = 0; // of those, how many were not fully transparent
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
    void benchSetDmaMode(int m) { dmaMode.store(m); }
    void benchSetInterlace(bool on) { interlace.store(on); }
    bool benchInterlace() const { return interlace.load(); }
    void benchSetRenderHalf(bool on) { renderHalf.store(on); }
    bool benchRenderHalf() const { return renderHalf.load(); }
    int benchDmaMode() const { return dmaMode.load(); }
    bool benchDmaActive() const { return dmaActive; }
    uint32_t benchDmaIssued() const { return dmaIssued.load(); }
    uint32_t benchDmaCompleted() const;
    uint32_t benchDmaErrors() const { return dmaErrors.load(); }
    // Where the band buffers actually landed. They are the DMA source, so one
    // of them falling back to PSRAM would mean GDMA reads memory the CPU has
    // only written through the cache.
    uint32_t benchBandAddr(int i) const {
        return (i >= 0 && i < NUM_SLOTS) ? reinterpret_cast<uint32_t>(bandBuf[i]) : 0;
    }
    bool benchBandsInternal() const;
#endif

  private:
    struct Overlay {
        uint8_t *buf = nullptr;
        int w = 0;
        int h = 0;
        // Per PANEL row: first/last column with alpha > 0, or min = -1 for an
        // empty row. Lets the blend skip the ~85% of rows/pixels that are
        // plain plasma.
        int16_t *spanMin = nullptr;
        int16_t *spanMax = nullptr;
        // Bit b set => pixels [b*32, b*32+32) in this row contain some alpha.
        // 32 blocks covers a 1024-wide row, well past this panel.
        uint32_t *rowBlocks = nullptr;

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
        // Span/block tables widened to cover the halo, which reaches past the
        // glyph bounding span and can put alpha in rows that hold no glyph at
        // all. Kept separate from spanMin/spanMax/rowBlocks rather than folded
        // into them: those are recomputed only for the rows LVGL redrew, so
        // widening them in place would ratchet -- a row's span would keep every
        // halo it ever had until that row happened to be redrawn. These are
        // rebuilt whole-panel from the glyph tables each publish, which is
        // cheap because it never touches the alpha plane.
        int16_t *blendMin = nullptr;
        int16_t *blendMax = nullptr;
        uint32_t *blendBlocks = nullptr;
    };

    static void taskEntry(void *arg);
    void renderLoop();
    void renderFrame();
    void buildScrim(Overlay &ov, int panelW, int panelH);

    Display *display = nullptr;
    void *taskHandle = nullptr;
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
    int renderSlot = 0; // slot the render task fills next; push task tracks its own
    bool cropEnabled = false;   // crop to the panel's circle only while push is the pacing stage
    // Push every other row pair, alternating parity each frame. Halves the
    // bytes and the driver's writeback range, at the cost of each row pair
    // refreshing at half the frame rate. Looked at on the panel at 45-59 fps:
    // the one-frame stagger between adjacent pairs is not visible.
    std::atomic<bool> interlace{true};
    // Render only the source rows this frame will push. Only legal alongside
    // interlacing at half resolution, where one source row feeds one pushed
    // pair, so skipping it costs nothing that is displayed.
    std::atomic<bool> renderHalf{true};
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
    uint32_t frameWaitUs = 0;   // this frame's total block on the push task, drives cropEnabled
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
    // 0 = ordinary two-task CPU push, 1 = direct CPU memcpy under the gate
    // (diagnostic), 2 = one esp_async_memcpy per band, 3 = the same split into
    // 4-row chunks, 4 = the native preallocated GDMA engine (see BandDma.h).
    //
    // 4 is the default. Measured on the panel, per frame:
    //
    //   mode 3  push 18.8 ms  25.6 fps    esp_async_memcpy, 3 chunks per band
    //   mode 2  push  9.7 ms  33.2 fps    esp_async_memcpy, 1 call per band
    //   mode 4  push  0.6 ms  41.5 fps    native engine, 1 submit per band
    //
    // The spread between 2 and 3 is what gives the API away: IDF 5.5's
    // esp_async_memcpy deletes and rebuilds both of its GDMA link lists from
    // the heap on every single call, so its cost tracks submissions rather than
    // bytes. Mode 4 keeps the descriptors and only re-points them.
    std::atomic<int> dmaMode{4};
    bool dmaActive = false; // fbDirect resolved AND the engine installed
    uint16_t *fbDirect = nullptr;
    void *dmaHandle = nullptr; // async_memcpy_t, installed once from the render task
    bool dmaInstallTried = false;
#ifndef GAGGIMATE_SIM
    // The native engine used by mode 4. Kept beside the esp_async_memcpy handle
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
    void endDirectPath();
    std::atomic<uint32_t> dmaIssued{0};
    std::atomic<uint32_t> dmaErrors{0};
    // Core the async-memcpy completion interrupt is bound to. Deliberately not
    // the render core: the RGB panel driver's ISR is on core 1 and must not
    // queue behind ours.
    static constexpr int DMA_ISR_CORE = 0;
    bool installDmaOnRenderCore();

    // Per-band horizontal extent of the panel's inscribed circle. The panel is
    // round, so the corners of the 480x480 rectangle are never visible and
    // pushing them is wasted PSRAM bandwidth. One rectangle per band (the
    // widest row in it), since pushColors takes a rectangle.
    static constexpr int MAX_BANDS = 64; // 480 rows / BAND_H, with headroom
    int16_t bandX0[MAX_BANDS] = {};
    int16_t bandX1[MAX_BANDS] = {};
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
#else
    std::atomic<uint8_t> maxFps{30};
#endif
    int initializedAnimId = -1; // last id whose init() ran on the render task
    // Which animation currently holds allocated tables, or -1 for none. Kept
    // apart from initializedAnimId because start() clears that one to force an
    // init(), and stopping the render task frees nothing -- so the tables
    // outlive it and something has to remember whose they are.
    int residentAnimId = -1;
    bool initializedHalf = false; // resolution that init() ran at; a change re-inits
    uint16_t *halfBuf = nullptr;  // (w/2)x(BAND_H/2) scratch for half-res rendering

    // Scrim strength in Q8 (0 = off, 256 = black). Read once per band by the
    // composite, so a plain relaxed load is all it needs.
    std::atomic<int> scrimQ8{55 * 256 / 100};
    // Scratch grid for the separable dilate/blur passes, one shared copy: the
    // passes run to completion inside publishOverlay on the UI task, so the two
    // overlays never need it at the same time.
    uint8_t *scrimTmp = nullptr;

    Overlay overlays[2];
    uint32_t overlayCap = 0;
    std::atomic<int> overlayFront{-1};  // -1 = nothing published yet
    std::atomic<int> overlayInUse{-1};  // overlay the render task reads this frame

#ifdef GM_ANIM_BENCH
    // Accumulators for the dwell in progress; render task only, no locking.
    uint64_t accBandUs = 0;
    uint64_t accBlendUs = 0;
    uint64_t accPushUs = 0;
    uint64_t accTotalUs = 0;
    uint64_t accWaitUs = 0;
    uint64_t accPackUs = 0;
    uint64_t accSpanPx = 0;
    uint64_t accBlendPx = 0;
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

    void benchTick();      // called once per frame from renderLoop
    void benchFinishDwell(); // records the current animation and advances
#endif
};

#ifdef GM_ANIM_BENCH
// The running instance, so the web plugin can publish results without the
// whole UI object graph being reachable from it. Null until start() runs.
SleepAnimation *sleep_animation_bench_instance();

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
