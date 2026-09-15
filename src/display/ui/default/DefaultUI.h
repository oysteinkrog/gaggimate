#ifndef DEFAULTUI_H
#define DEFAULTUI_H

#include <display/ui/default/TouchTask.h>
#include <atomic>
#include <display/core/PluginManager.h>
#include <display/core/ProfileManager.h>
#include <display/core/constants.h>
#include <display/drivers/Driver.h>
#include <display/models/profile.h>
#include <display/ui/default/SleepAnimation.h>
#include <display/ui/default/eez/MeterTickCache.h>
#include <display/ui/default/eez/screens.h>
#include <display/ui/default/eez/structs.h>
#include <display/ui/default/settings/SettingsUI.h>
#include <mutex>

class Controller;

constexpr int RERENDER_INTERVAL_IDLE = 2500;
// Freshness floor: force a pass at least this often during an active
// process even if no change event fired. Was 100, which against the old
// ~650 ms pipeline merely throttled; raising it alone measured marginal
// (logs 36 vs 37) because the real saturator is the event side, bounded by
// RERENDER_MIN_INTERVAL below.
constexpr int RERENDER_INTERVAL_ACTIVE = 300;
// Rate ceiling for telemetry passes: a rerender pass may not START within
// this many ms of the previous pass start unless a touch edge arrived
// within GM_TOUCH_GRACE_US. Every telemetry change event sets rerender,
// the loadtest feed changes continuously, and a full pass costs ~90-110
// ms, so unspaced passes ran back to back (~7 Hz, 60-100% UI-task duty)
// and a touch edge waited a median 122 ms just to be READ (GM_EDGEWAIT,
// log 37): the indev poll runs on the same task the passes monopolize.
// Measured alone this spacer was null on tap latency (log 38 == log 36
// medians) because the snapshot refresh keeps running off invalidations
// that never ride the rerender flag — OVERLAY_MIN_REFRESH_US below is the
// gate on the expensive unit; this one only spaces the updateState/eez
// side. 4 Hz numeric readouts are indistinguishable from 7 Hz by eye.
constexpr int RERENDER_MIN_INTERVAL = 250;
// Rate ceiling for overlay refresh STARTS (the snapshot+publish unit in
// refreshSleepOverlay), in esp_timer time. Spacing rerender passes alone
// measured null on tap latency (log 38 == log 36 medians): invalidations
// keep arriving outside the gated pass, and each ~100 ms refresh re-armed
// the next one back to back, so the UI task stayed ~96% busy and a touch
// edge still waited a median ~110 ms to be read. This gates the expensive
// unit itself; the debt lists make a held refresh lossless (coalesced, not
// dropped). A touch edge since the last refresh start bypasses the gate,
// which is what separates this from the unconditional 1000 ms throttle
// that was removed for holding tap feedback a second (see maintain call
// site). Full-buffer fills (screen change, geometry move) also bypass.
constexpr int64_t OVERLAY_MIN_REFRESH_US = 250000;
// How long a multi-clip overlay pass runs before it polls the touch
// controller between clips (gm-qo3.2). Shorter than one clip's snapshot on
// a telemetry screen, so any pass long enough to hide a tap gets polled.
constexpr int64_t OVERLAY_INPUT_SLICE_US = 20000;
// The touch grace window that holds both gates above open after an edge
// (GM_TOUCH_GRACE_US) lives in LV_Helper.h beside the edge stamp it reads:
// the overlay publish in SleepAnimation uses the same window to wake the
// render task early.

constexpr int TEMP_HISTORY_INTERVAL = 250;
constexpr int TEMP_HISTORY_LENGTH = 20 * 1000 / TEMP_HISTORY_INTERVAL;

int16_t calculate_angle(int set_temp, int range, int offset);

enum class BrewScreenState { Brew, Settings };

class DefaultUI {
  public:
    DefaultUI(Controller *controller, Driver *driver, PluginManager *pluginManager);

    // Default work methods
    void init();
    void loop();
    void loopProfiles();

    // Interface methods
    void changeScreen(ScreensEnum screen);

    void changeBrewScreenMode(BrewScreenState state);
    void onProfileSwitch();
    void onNextProfile();
    void onPreviousProfile();
    void onProfileSelect();
    void setBrightness(int brightness);

    void onVolumetricDelete();

    // Scale mode: opened from the menu's Grind slot when the scaleMenuButton
    // setting is on. Hosted as an overlay on the grind screen (EEZ flow can't
    // grow new screens at runtime): live weight readout + tare + back.
    void openScaleScreen();

    void markDirty() { rerender = true; }
    // For the settings shell: call before a navigation rebuilds the cover.
    void beginOverlayTransition(const char *why) { beginOverlayTransition(why, false); }
    void markProfileDirty() { profileDirty = true; }
    void markProfileClean() { profileDirty = false; }

    void applyTheme();

    // Settings shell lifecycle: SettingsUI owns the cover and its pages,
    // DefaultUI owns the instance and the entry points a caller (the menu
    // tile, the debug route) reaches it through.
    void openSettings();
    void closeSettings();
    SettingsUI &getSettingsUI() { return settingsUI; }
    // Read-only view of the private apActive flag (updateSystemStatus/the
    // config-AP fallback), so the Status category can show "Access point"
    // and the AP's fixed 4.4.4.1 the same way the standby screen already
    // does, without a setter this bead has no reason to add.
    bool isApActive() const { return apActive != 0; }

#if defined(GM_TOUCH_PROBE) || defined(GAGGIMATE_SIM)
    // /api/debug/settingsui (WebUIPlugin.cpp, bench and sim builds only):
    // opens, closes and navigates the settings shell from a script and
    // reports where it is. One command in flight at a time:
    // queueSettingsUiCommand (web/async task) accepts it if none is
    // pending; serviceSettingsUi (UI task, called every DefaultUI::loop())
    // executes it, staging Open across passes when the active screen is
    // not the menu screen, and publishes the resulting state. Every
    // executed command logs "SettingsDbg: <cmd> -> depth=.. category=..
    // page=..".
    enum class SettingsUiCmd : uint8_t { Open, Close, Cat, Page, Pop };
    struct SettingsUiState {
        uint32_t seq = 0;
        bool open = false;
        int depth = 0;
        int category = -1;
        int page = 0;
        int pages = 0;
        char title[24] = "Settings";
        int fixtureEnter = 0;
        int fixtureCommit = 0;
        int fixtureDraft = 0;
        // Added for the row-widget bead (gm-flw.3), which is also when the
        // Fixture category grew stepper/choice/toggle/action/locked/confirm
        // rows to expose these from: see SettingsUI::FixtureCounters.
        int fixtureAction = 0;
        int fixtureConfirm = 0;
        bool fixtureLocked = true;
        int fixtureRepeats = 0;
        int fixtureFastRepeats = 0;
    };
    // Valid range for a Cat command's arg. SettingsUI.h derives the count
    // from the same condition that builds kCategories and static_asserts it
    // against the array, so a script that asks for category 5 on a build
    // without the Fixture tile is refused rather than indexing past the end.
    static constexpr int kSettingsUiCategoryCount = kSettingsCategoryCount;
    // Queues cmd/arg if no command is already in flight and returns true
    // with seqOut set to the seq a caller should poll settingsUiState()
    // for; returns false, seqOut untouched, when a command is already
    // pending (caller reports 409). Called from the web/async task.
    bool queueSettingsUiCommand(SettingsUiCmd cmd, int arg, uint32_t &seqOut);
    // Republishes the shell state for the debug route; see the definition.
    SettingsUI::State publishSettingsUiState(uint32_t seq, bool force);
    // Copies the last-published state out, as one struct under a critical
    // section so a reader never observes a stale title paired with new
    // counters. Called from the web/async task.
    void settingsUiState(SettingsUiState &out) const;
#endif

    // Styles just the subtree at root (the existing applyPressedRecurse
    // walk), skipping applyPressedFeedback's lv_scr_act()-vs-pressedStyledRoot
    // check: for a freshly built settings tile page or category page, so a
    // page turn styles only what it just created instead of re-walking the
    // whole menu screen underneath the cover.
    void applyPressedFeedbackTo(lv_obj_t *root);
    // Forces the next applyPressedFeedback() pass to re-walk the active
    // screen once. SettingsUI::open() calls this because the cover changes
    // what is on screen without lv_scr_act() itself changing.
    void resetPressedFeedbackRoot() { pressedStyledRoot = nullptr; }

    bool isTaskHealthy() const {
        return is_task_healthy(eTaskGetState(taskHandle)) && is_task_healthy(eTaskGetState(profileTaskHandle));
    }

  private:
    void setupPanel();
    void setupState();

    void handleScreenChange();

    void startSleepAnimation();
    void stopSleepAnimation();
    // The half of stopSleepAnimation that hands the framebuffers back to
    // LVGL; runs at once when stop() confirmed, otherwise from the loop once
    // stopConfirmed() says so or the 3 s cap expires (gm-bzu.16).
    void finishStopSleepAnimation(bool forced);
    void serviceAnimStopPending();
    // Blocks (up to maxMs) until a pending stop is confirmed; for the panel
    // stop path, which is about to delete the panel under any transfer.
    void waitAnimStopPending(unsigned long maxMs);
    // Fade transitions (gm-2cl.2). A whole-page overlay rebuild (screen
    // change, settings navigation) is hidden behind the composite's gain:
    // the old page fades out from the moment the change is requested, the
    // rebuilt page is published only once the gain has reached zero, and
    // fades in from the first frame that composites it. waitSwap marks a
    // screen change whose EEZ swap is still pending: partial publishes are
    // held until handleScreenChange has swapped, so the old page never
    // gets a refresh mid-fade.
    void beginOverlayTransition(const char *why, bool waitSwap);
    // Press highlight through the compositor (gm-2cl.3): the touch read
    // callback hands every edge here; a press writes element 0 over the
    // target's box, a release clears it. LVGL's pressed styles stand down
    // while this is the feedback (see g_pressPlateActive).
    static void touchHitHook(lv_obj_t *hit, bool pressed, int16_t x, int16_t y);
    void onTouchHit(lv_obj_t *hit, bool pressed);
    // The active screen's clickable rectangles for the touch task
    // (TouchTask.h), rebuilt once per UI pass and after a screen change.
    void publishTouchHitMap();
    static void collectHitRects(lv_obj_t *obj, const lv_area_t &clip, lv_obj_t *scr, touchtask::HitRect *out, int &n);
    void updatePressPlateMode();
    bool pressPlateMode = false;
    static DefaultUI *s_instance;
    static constexpr int PRESS_PLATE_ELEMENT = 0;
    static constexpr int PRESS_PLATE_OUTSET = 4;
    void serviceOverlayTransition();
    void finishOverlayTransition();
    bool overlayFadedOut() const;
    enum class OverlayTrans : uint8_t { Idle, FadeOut, FadeIn };
    OverlayTrans overlayTrans = OverlayTrans::Idle;
    bool overlayTransWaitSwap = false;
    bool overlayTransHeld = false; // a rendered page waits in the back buffer for gain 0
    int64_t overlayTransT0Us = 0;
    // The fade durations come from the settings (bgFadeOutMs, bgFadeInMs,
    // web "Screen fade" and the Animation category's Fade rows). The
    // fade-out length in force when the transition started is kept here so
    // overlayFadedOut() judges the ramp that actually ran, not a value
    // changed mid-transition.
    uint32_t overlayTransOutMs = 0;
    uint32_t overlayFadeOutMs() const;
    uint32_t overlayFadeInMs() const;
    // A fade-out whose page never arrives (a cancelled change) is undone
    // after this long, so the screen cannot stay bare.
    static constexpr int64_t OVERLAY_TRANS_ABANDON_US = 1500000;
    // Move the animation's host screen without interrupting it. The only
    // per-screen state the animation holds is the host's transparent
    // background: the plate table and the status icons it also rewrites are
    // fixed global objects that span every screen, so a screen change costs
    // one style property, not a restart.
    void adoptAnimHost(lv_obj_t *host);
    void releaseAnimHost();
    void maintainSleepAnimation();
    void refreshSleepOverlay();
    // The 5 ms handler pass in loopTask calls this so widget redraws reach the
    // overlay snapshot on the input cadence instead of waiting for the next
    // 25 ms ui->loop() pass. Costs a bool check when the animation is off and
    // one comparison when it is on but nothing was drawn.
    void pumpSleepOverlay();
    // Applies g_uiAnimTestReq (LV_Helper.h) on the UI task: creates, moves or
    // removes the foreground motion test widget.
    void serviceUiAnimTest();
    // Serves /api/debug/touchmap (LV_Helper.h, g_touchMapReq/g_touchMapPending):
    // dumps a screen's (or, for screen=0, the active screen's) object tree
    // with the hit rectangles LVGL uses, label text, settings debug tags and
    // a seq/uptime_ms pair. Runs on the simulator too.
    void serviceTouchMap();
#if defined(GM_TOUCH_PROBE) || defined(GAGGIMATE_SIM)
    // Executes the pending /api/debug/settingsui command, if any (see the
    // public settingsui block above). Called every DefaultUI::loop() pass
    // on both venues, same as serviceTouchMap(): an Open command needs the
    // active screen to become the menu screen first (changeScreen(), whose
    // actual eez_flow_set_screen() only runs inside the rerender-gated
    // half of loop()), so this polls the live current screen across passes
    // rather than assuming one call is enough.
    void serviceSettingsUi();
#endif
    // Renders obj and its children into buf as LV_IMG_CF_TRUE_COLOR_ALPHA
    // (RGB565 + A8), sized to the object's coords grown by its ext draw size;
    // *outArea receives that area in screen coordinates. False when the
    // buffer is too small or the object has no size yet. Same rendering
    // machinery as snapshotAreaToOverlay, but for a subtree into its own
    // buffer: the layer path (SleepAnimation::layer*) starts here.
    bool snapshotObjectToBuffer(lv_obj_t *obj, uint8_t *buf, uint32_t bufSize, lv_area_t *outArea);
    // Moves an LVGL object smoothly while the animation owns the panel. The
    // object is rendered once into a layer (snapshotObjectToBuffer), hidden
    // from LVGL, and the render task slides the layer by (dx, dy) over durMs
    // at the animation loop's rate (about 30 fps, against 5 through LVGL).
    // When it lands, serviceLayerMoves moves the object to the end position
    // and shows it again, and releases the layer once an overlay refresh has
    // published it there, so the hand-back never shows a gap or a double.
    // Both ends of the move bypass the overlay refresh gate (overlayUrgent
    // UntilUs), so neither the copy left behind at the start nor the layer
    // at the end outlives the next UI pass by more than it has to. The
    // object's content is frozen for the duration: a label that changes
    // mid-move shows the change when it lands. False, with the object
    // untouched, when it is already moving, no layer is free, the animation
    // is not running or the object has no size yet; the caller falls back to
    // lv_anim. dx/dy add to the object's style x/y, so an aligned object
    // moves relative to its alignment origin, same as lv_obj_set_x would.
    bool moveObjectViaLayer(lv_obj_t *obj, lv_coord_t dx, lv_coord_t dy, uint32_t durMs, SleepAnimation::LayerEase ease);
    // True from moveObjectViaLayer until the layer has been released again
    // (landed, shown and published).
    bool layerMoveInFlight(const lv_obj_t *obj) const;
    // Once per UI pass: lands finished moves, releases published ones.
    void serviceLayerMoves();
    // Ends a move now: the object goes to its end position and is shown, the
    // layer is released. Every move before a screen change (the flow engine
    // may delete the objects) and one before deleting a moving object.
    void cancelLayerMove(const lv_obj_t *obj);
    void cancelLayerMoves();
    // Hide, restore or repaint the opaque background plates the generated
    // screens put behind their content. mode is Settings::getBgAnimClearPlates
    // (0 keep, 1 hide, 2 custom); color is 0xRRGGBB and opaPct 0-100, both used
    // only in mode 2. See the definition for which objects and why.
    void applyAnimPlates(int mode, uint32_t color = 0, int opaPct = 0);
    // lv_snapshot_take_to_buf with a clip area. Renders only `clip` into `buf`,
    // leaving the rest of the buffer alone, so an overlay refresh costs what
    // actually changed rather than a whole screen.
    bool snapshotAreaToOverlay(lv_obj_t *obj, uint8_t *buf, uint32_t bufSize, const lv_area_t &clip, int *outW,
                               int *outH, bool clearByRuns = false);
    void maintainScaleScreen();
    void buildScaleScreen();
    void displaceGrindWidgets(bool displaced);
    lv_obj_t *scaleScreen = nullptr;      // overlay covering the grind screen
    lv_obj_t *scaleWeightLabel = nullptr;
    lv_obj_t *scaleTareBtn = nullptr;     // opaque pill, driven by applyAnimPlates
    bool scaleScreenRequested = false;
    bool scaleMenuSwap = false; // settings.isScaleMenuButton(), cached per render
    float lastShownScaleWeight = -1000.0f;
    SleepAnimation sleepAnimation;
    unsigned long lastSleepAnimAttempt = 0;
    // A stop() that could not confirm its workers and transfers had retired.
    // While set, LVGL stays on its scratch buffer and neither start nor stop
    // runs; the panel keeps showing the last frame.
    bool animStopPending = false;
    unsigned long animStopPendingSince = 0;
    unsigned long lastSleepOverlayRefresh = 0;
    // Dirty regions still owed to each of the two overlay buffers, in screen
    // coordinates, and whether that buffer has ever held a full render. They
    // are written alternately, so each carries its own debt: a partial update
    // is only valid against what that specific buffer already holds.
    //
    // A list per buffer, not one rectangle: a single bounding box unioned the
    // temperature readout and the status bar into the whole screen, and every
    // refresh then re-rendered and re-scanned all 480x480 pixels. That was
    // most of the 650 ms UI pass the touch probe measured.
    static constexpr int OVERLAY_DIRTY_RECTS = 4;
    lv_area_t overlayDirty[2][OVERLAY_DIRTY_RECTS];
    int overlayDirtyN[2] = {0, 0};
    // Debt the buffer can settle by copying from the other buffer instead of
    // rendering (gm-qo3.4). A fresh rect is rendered once, into the buffer
    // that is back when it arrives, and owed to the other one as a copy:
    // by the time that buffer is back again, the first holds the rect as of
    // its render, and everything newer is in its own render debt, which is
    // drawn after the copy and so wins. A copy of a 3-byte pixel row is a
    // memcpy in PSRAM; the render it replaces was a clear plus a full LVGL
    // draw of the same area (10 plus 42 ms per refresh on the board).
    lv_area_t overlayCopy[2][OVERLAY_DIRTY_RECTS];
    int overlayCopyN[2] = {0, 0};
    bool overlayValid[2] = {false, false};
    // Set when a snapshot draws into the buffer, cleared when it is
    // published: while set, the buffer's run table does not describe its
    // alpha and a whole-page clear must memset the plane.
    bool overlayDrawnUnpublished[2] = {true, true};
    // The snapshot geometry each overlay buffer was built with.
    //
    // The snapshot is sized width+ext*2 by height+ext*2, where ext is the
    // screen's extended draw size, and the animation composites it centred, at
    // an offset of exactly ext. LVGL recomputes ext as widgets with shadows or
    // outlines come and go, so it is not a constant. The buffer is indexed
    // relative to coords.y1-ext, which means a change to ext moves the whole
    // buffer's coordinate system -- every pixel already in it is now read a
    // few rows off.
    //
    // The dirty rectangles track which pixels changed, not that the frame they
    // are expressed in changed, so a partial publish after an ext change
    // leaves the untouched remainder displaced. On the panel that is the
    // profile name drawn a second time about 25 rows below itself, faint and
    // clipped, while every widget that happened to be re-published looks
    // perfect. It survives with the scan-out slip counter reading zero,
    // because nothing about the scan-out is wrong.
    int overlayW[2] = {-1, -1};
    int overlayH[2] = {-1, -1};
    bool bgAnimAllScreens = false;         // settings.isBgAnimAllScreens(), cached per render
    // Gradient editor live preview, handed over from the web socket task and
    // applied by updateState on the UI task. While previewUntil is ahead of
    // millis() the panel shows previewAnim drawn with previewStops instead of
    // the saved selection; the editor re-sends while it is open, and a closed
    // tab lapses back on its own.
    std::mutex previewMutex;
    String previewStops;
    int previewAnim = 0;
    bool previewDirty = false;
    unsigned long previewUntil = 0;
    // millis() when setupPanel() finished building the UI, or 0 before that.
    // The animation needs a live screen to host its overlay snapshot, so it
    // cannot start earlier. `initialized` cannot serve this purpose: it is set
    // only when a controller connects, which is a different thing entirely.
    unsigned long uiBuiltAt = 0;
    lv_obj_t *animHostScreen = nullptr;    // screen whose bg was made transparent for the animation
    lv_obj_t *uiAnimTestObj = nullptr;     // the foreground motion test widget, when one is up
    int uiAnimTestMode = 0;                // g_uiAnimTestReq value the widget was built for
    unsigned long touchMapDumpAt = 0;      // touchmap: earliest millis() to dump after a load
    int uiAnimTestTravel = 0;              // mode 3: how far each leg slides the plate
    bool uiAnimTestFwd = true;
    struct LayerMove {
        lv_obj_t *obj = nullptr;
        int layer = -1;
        lv_coord_t dx = 0, dy = 0;
        bool landed = false;    // object moved and shown, layer still up
        uint32_t hideGen = 0;   // overlay publish generation that shows the object again
        int64_t landedAtUs = 0; // fallback clock for a publish that never comes
    };
    LayerMove layerMoves[SleepAnimation::MAX_LAYERS];
    // refreshSleepOverlay's spacing gate is bypassed until this time: set by
    // the layer moves, which need the next refresh out promptly.
    int64_t overlayUrgentUntilUs = 0;
    // Number of entries in the plate table in applyAnimPlates.
    static constexpr int ANIM_PLATE_COUNT = 9;
    // Last applied (mode, color, opacity), so a no-op settings poll costs one
    // comparison. -1 means nothing has been applied yet, which forces the first
    // pass through even when the stored mode is 0.
    //
    // The cache records what this code last wrote, not what is on screen, so
    // anything else that writes bg_color on a plate silently invalidates it.
    // change_color_theme() does exactly that, which is why applyTheme() resets
    // animPlateMode after a live theme switch.
    int animPlateMode = -1;
    uint32_t animPlateColor = 0;
    int animPlateOpaPct = -1;
    // Style each plate had before mode 1 or 2 touched it, used to put it back
    // for mode 0. Captured rather than assumed: these objects do not all start
    // out fully opaque. Captured per plate, not in one pass, because the scale
    // screen's Tare pill is built long after the generated screens and would
    // otherwise be "restored" to a zero-initialised entry.
    bool animPlateHas[ANIM_PLATE_COUNT] = {};
    lv_opa_t animPlateOpa[ANIM_PLATE_COUNT] = {};
    lv_color_t animPlateBg[ANIM_PLATE_COUNT] = {};
    // Mode 2 does not tint the four dial panels themselves (360 and 400 px
    // discs, about 102k blended pixels a frame at 60 percent). It paints a
    // smaller disc behind their children instead (gm-2cl.10, owner's choice
    // 2026-09-09: radius 140, 61k pixels): one child object per panel,
    // created on first use, shown in mode 2 and hidden otherwise.
    static constexpr int ANIM_PLATE_DISC_COUNT = 4;
    static constexpr int ANIM_PLATE_DISC_RADIUS = 140;
    lv_obj_t *animPlateDisc[ANIM_PLATE_DISC_COUNT] = {};
    std::atomic<bool> panelStopRequested{false};
    std::atomic<bool> panelStopped{false};
    std::atomic<bool> otaEnded{false};

    // Phone-style pressed feedback for the generated tappable widgets. The
    // EEZ screens register only LV_IMGBTN_STATE_RELEASED images, lv_imgbtn is
    // not lv_btn_class, and the buttons' local default-state backgrounds
    // outrank the theme's pressed styles — so a press changed no pixels at
    // all, even though lv_imgbtn's refr_img already invalidates on every
    // PRESSED/RELEASED (the overlay pipeline was paying the redraw on every
    // tap for zero visual return). The walk sets per-object LOCAL
    // LV_STATE_PRESSED color props derived from each widget's own resolved
    // rest colors (hue-preserving darken), covering imgbtns, clickable
    // lv_imgs, and buttons with a visible background. Color-only props on
    // purpose: they alter RGB, never alpha, so the snapshot's alpha plane
    // stays byte-identical and publishOverlayRanges' scrim memcmp skips the
    // ~23 ms whole-grid scrim rebuild on the press/release refreshes.
    // Applied by walk over the active screen, not by editing generated
    // screens.c (EEZ regen would drop it). Re-applied when the active screen
    // object changes; handleScreenChange and a theme-mode change clear the
    // root so recreated screens and re-themed rest colors get rewalked.
    void applyPressedFeedback();
    lv_obj_t *pressedStyledRoot = nullptr;
    // tuneGeneratedScreen's walk runs once per screen root and again when
    // its knobs change (gm-2cl.19).
    lv_obj_t *tunedRoot = nullptr;
    int tunedKnobs = -1;
    void tuneGeneratedScreen();
    // The start/pause control on brew, water, grind and status: 60x60 from
    // UiImages.h and re-centred in its band. Idempotent, run from
    // tuneGeneratedScreen on every pass.
    void growActionButtons();
    // Last-applied web-configurable colors, sentinel-initialized so the first
    // pass applies. appliedTintKey packs enabled+color (see applyTheme).
    int appliedDimColor = -1;
    int appliedTintKey = -1;

    // Animate the dial meters' tick length on screen change (short on profile/new-menu, long elsewhere).
    void animateGaugeTicks(ScreensEnum from, ScreensEnum to);
    void collectMeters(lv_obj_t *obj);
    void setGaugeTickLength(int32_t len);
    static void gaugeTickAnimCb(void *var, int32_t v);
    lv_obj_t *gaugeMeters[4] = {nullptr};
    uint8_t gaugeCount = 0;

    // The dial meters' tick rings as compositor elements (gm-2cl.6). While
    // the animation composites the screen, each visible dial on the active
    // screen hands its ring to a TickRing element: the meter gets
    // LV_OBJ_FLAG_USER_1 (its draw handler leaves the ring transparent and
    // its value setters stop invalidating), its tick cache slot is pinned,
    // and serviceDialElements writes the lit range the flow has set after
    // every ui_tick. Released on screen change, when the animation stops,
    // when the meter hides, and while the tick-length morph is running
    // (the morph changes the cache key every frame). A released slot stays
    // pinned until the render task has latched two more frames, because
    // the frame in flight may still read its sprites.
    static constexpr int DIAL_ELEMENT_BASE = 5;
    static constexpr int DIAL_ELEMENTS = 3;
    struct DialElement {
        lv_obj_t *meter = nullptr;
        meterticks::Key key{};
        tickring::Sprites ring{};
        bool owned = false;
        int16_t lo = -1, hi = -1;
        uint16_t lit = 0, unlit = 0;
        uint32_t releasedFrame = 0;
        bool releasedRecently = false;
    };
    DialElement dialElems[DIAL_ELEMENTS];
    struct DialRetire {
        meterticks::Key key{};
        uint32_t frame = 0;
        bool live = false;
    };
    DialRetire dialRetire[DIAL_ELEMENTS * 2];
    void serviceDialElements();
    void releaseDialElements();
    void releaseDialElement(DialElement &d);
    void retireDialRing(const meterticks::Key &key);

    // The brew progress bar's fill as a RoundRect element (gm-2cl.6). LVGL
    // keeps drawing the track; the fill's right edge eases toward the bar's
    // value at the UI loop's rate (kBarEaseTauUs) instead of stepping once
    // per flow update. Owned and released with the dials, and the bar's own
    // indicator is made transparent through a local style while owned.
    static constexpr int BAR_ELEMENT = 4;
    static constexpr int64_t kBarEaseTauUs = 120000;
    struct BarElement {
        lv_obj_t *bar = nullptr;
        bool owned = false;
        float x2 = 0.0f; // eased right edge, panel x
        int16_t lastX2 = -1;
        int64_t lastUs = 0;
        uint8_t opa = 255; // the indicator's design opacity, read before the override
    };
    BarElement barElem;
    void serviceBarElement(bool canOwn);
    void releaseBarElement();

    // Live labels as Text elements (gm-2cl.5). A label on the active screen
    // whose text has changed once since the screen was entered is "live";
    // while the animation composites, up to kTextElements live labels are
    // owned: the label gets LV_OBJ_FLAG_USER_2, which the patched lv_label
    // (scripts/patch_lvgl_label_elem.py) reads as "draw nothing and do not
    // invalidate on set_text", and the UI task rebuilds its glyph list from
    // the glyph atlas after every ui_tick, placing each glyph the way
    // lv_draw_label would have. The hidden flag is left to the flow, which
    // reads and writes it every tick on the dial value labels. A numeric
    // value eases toward the flow's value at the UI loop's rate
    // (kTextEaseTauUs) so a reading counts up instead of stepping. Released
    // on screen change, when the animation stops and when the label or an
    // ancestor hides.
    static constexpr int TEXT_ELEMENT_BASE = 8;
    static constexpr int TEXT_ELEMENTS = SleepAnimation::kTextElements;
    static constexpr int64_t kTextEaseTauUs = 150000;
    static constexpr int kTextMaxLen = 40;
    struct TextElement {
        lv_obj_t *label = nullptr;
        bool owned = false;
        uint16_t ver = 0;
        uint32_t hash = 0; // of the glyph list's inputs; unchanged means no rewrite
        // Easing state: the number in the label's text, if it has one.
        bool numeric = false;
        float shown = 0.0f;
        float target = 0.0f;
        int decimals = 0;
        int intDigits = 0;
        bool padded = false;
        int64_t lastUs = 0;
        char prefix[kTextMaxLen] = {};
        char suffix[kTextMaxLen] = {};
        char shownText[kTextMaxLen] = {};
    };
    TextElement textElems[TEXT_ELEMENTS];
    static constexpr int kLiveLabelCap = 24;
    struct LiveLabel {
        lv_obj_t *obj = nullptr;
        uint32_t hash = 0;
        uint32_t seen = 0;
        bool live = false;
        bool refused = false;
    };
    LiveLabel liveLabels[kLiveLabelCap];
    int liveLabelN = 0;
    uint32_t liveLabelPass = 0;
    lv_obj_t *liveScreen = nullptr;
    void serviceTextElements(bool canOwn);
    void releaseTextElements();
    void releaseTextElement(TextElement &t);
    void scanLiveLabels(lv_obj_t *obj);
    LiveLabel *liveLabelFor(lv_obj_t *obj);
    bool textLabelEligible(lv_obj_t *label, bool owned) const;
    bool buildTextGlyphs(lv_obj_t *label, const char *txt, SleepAnimation::TextDesc &t, int &bx0, int &by0, int &bx1,
                         int &by1) const;
    void easeTextValue(TextElement &t, const char *target, int64_t now);
    static void textLabelDeleted(lv_event_t *e);

    // Blinking icons as layers (gm-2cl.17). The dial screens' 40x40 icons
    // blink because the flow toggles their CHECKED state about twice a
    // second and the theme recolors them on it; every toggle was an LVGL
    // invalidation, a snapshot and a publish, and the pass it cost stalled
    // the eased text elements. An lv_img on the active screen whose state
    // or hidden flag has changed twice since the screen was entered gets
    // LV_OBJ_FLAG_USER_3: the patched lv_img draws nothing for it and the
    // patched lv_obj_invalidate_area drops its invalidations. Each state
    // the image shows is rendered once into a layer sprite (two per icon,
    // keyed by lv_obj_get_state), and every pass shows the sprite for the
    // current state and hides the other, or hides both while the image or
    // an ancestor is hidden. Released on screen change, animation stop, a
    // third state, a src or box change, delete, or icons=0.
    static constexpr int kIconLayers = 2;
    static constexpr int kIconSprites = 2;
    static constexpr int kIconCandCap = 24;
    static constexpr int kIconMaxPx = 96 * 96;
    struct IconCand {
        lv_obj_t *obj = nullptr;
        uint32_t seen = 0;
        uint16_t state = 0;
        uint8_t toggles = 0;
        bool hidden = false;
        bool refused = false;
    };
    struct IconLayer {
        lv_obj_t *obj = nullptr;
        int layer[kIconSprites] = {-1, -1};
        uint16_t state[kIconSprites] = {0, 0};
        int sprites = 0;
        int shown = -1; // sprite index drawn, or -1 for none
        const void *src = nullptr;
        lv_area_t coords = {};
    };
    IconCand iconCands[kIconCandCap];
    int iconCandN = 0;
    uint32_t iconPass = 0;
    lv_obj_t *iconScreen = nullptr;
    IconLayer iconLayers[kIconLayers];
    void serviceIconLayers(bool canOwn);
    void releaseIconLayers();
    void releaseIconLayer(IconLayer &l);
    void scanIcons(lv_obj_t *obj);
    bool takeIconLayer(IconLayer &l, lv_obj_t *img);
    int snapshotIconSprite(IconLayer &l);
    static void iconDeleted(lv_event_t *e);

    // Scrolling labels as looping layers (gm-2cl.18). A
    // LV_LABEL_LONG_SCROLL_CIRCULAR label whose text overflows its box runs
    // an lv_anim that invalidates the box on every LVGL tick: the info
    // screen refreshed 3.75 times a second for its one marquee, and the text
    // stepped at that rate. The label's text is rendered twice (text, gap,
    // text) into a layer sprite, the label gets USER_2 (draws no text) and
    // USER_3 (no invalidations), and the render task loops the sprite across
    // the box at LVGL's speed with an x clip on the layer, starting at the
    // label's current offset. Released on text or box change, screen
    // change, animation stop, delete, or marquees=0.
    static constexpr int kMarquees = 2;
    static constexpr int kMarqueeCandCap = 8;
    static constexpr uint32_t kMarqueeMaxBytes = 256 * 1024;
    struct Marquee {
        lv_obj_t *label = nullptr;
        int layer = -1;
        uint32_t hash = 0;
        lv_area_t coords = {};
        int period = 0;
    };
    Marquee marquees[kMarquees];
    lv_obj_t *marqueeCands[kMarqueeCandCap];
    int marqueeCandN = 0;
    lv_obj_t *marqueeScreen = nullptr;
    void serviceMarquees(bool canOwn);
    void releaseMarquees();
    void releaseMarquee(Marquee &m);
    void scanMarquees(lv_obj_t *obj);
    // 0: taken, 1: not a marquee right now (text fits), -1: refused.
    int takeMarquee(Marquee &m, lv_obj_t *label);
    static void marqueeDeleted(lv_event_t *e);
    void positionMenuIcon(lv_obj_t *obj, int angle, int radius);

    void updateState();
    void migrateBgAnimGradients();
    void updateSystemStatus();
    void updateProfileInfo();
    void updateBoiler();
    void updateBrewProcess();
    void updateMenuScreen();
    String getErrorMessage();

    void adjustDials(lv_obj_t *dials);
    void adjustTarget(lv_obj_t *obj, double percentage, double start, double range) const;

    int tempHistory[TEMP_HISTORY_LENGTH] = {0};
    int tempHistoryIndex = 0;
    int prevTargetTemp = 0;
    bool isTempHistoryInitialized = false;
    int isTemperatureStable = false;
    unsigned long lastTempLog = 0;

    void updateTempHistory();
    void updateTempStableFlag();
    void reloadProfiles();

    Driver *panelDriver = nullptr;
    Controller *controller;
    PluginManager *pluginManager;
    ProfileManager *profileManager;
    SettingsUI settingsUI;

    // Screen state
    int updateAvailable = false;
    int apActive = false;
    int wifiConnected = false;
    int waitingForController = false;
    int initialized = false;
    int grindAvailable = false;

    // Seasonal flags
    int christmasMode = false;

    bool rerender = false;
    unsigned long lastRender = 0;
    // Same stamp in esp_timer time, compared against g_touchEdgeAtUs for the
    // telemetry-pass spacer's touch bypass (millis and esp_timer drift, so
    // the comparison stays within one clock).
    int64_t lastRenderUs = 0;
    // Last overlay refresh START in esp_timer time, for OVERLAY_MIN_REFRESH_US
    // and its g_touchEdgeAtUs comparison (same clock as the edge stamp).
    int64_t lastOverlayRefreshUs = 0;

    int mode = MODE_STANDBY;
    bool pressureAvailable = false;
    int heatingFlash = 0;
    float pressure = 0.0f;
    float currentTemp = 0.0f;
    float targetTemp = 0.0f;
    double activeWeight = 0.0;
    // Scale screen reads the hardware cells directly, not the mode-gated active
    // stream: getActiveScaleSource() goes BLUETOOTH-only in grind mode and would
    // freeze this readout. hardware:change fires on every valid HW measurement.
    double scaleHardwareWeight = 0.0;
    BrewScreenState brewScreenState = BrewScreenState::Brew;

    // EEZ Structs
    SystemStatusValue systemStatus;
    ProfileInfoValue selectedProfileInfo;
    ProfileInfoValue previewProfileInfo;
    BoilerValue boiler;
    UIFlagsValue uiFlags;
    BrewProcessValue brewProcess;
    Value currentWeight = FloatValue(0.0);
    Value steamReady = BooleanValue(false);
    Value grindWeightTarget = FloatValue(18.0);
    Value grindTimeTarget = StringValue("0:15");

    int profileDirty = 0;
    int currentProfileIdx = 0;
    std::atomic<int> profileLoaded{0}; // cleared from event callbacks on arbitrary tasks
    // The profile task (core 0) rebuilds these while the UI task reads them (GM-147).
    std::mutex profilesMutex;
    std::vector<String> favoritedProfileIds;
    std::vector<Profile> favoritedProfiles;
    int currentThemeMode = -1; // Force applyTheme on first loop

    // Screen change
    ScreensEnum targetScreen = ScreensEnum::SCREEN_ID_STANDBY_SCREEN;
    ScreensEnum currentScreen = ScreensEnum::SCREEN_ID_STANDBY_SCREEN;

    // Standby brightness control
    unsigned long standbyEnterTime = 0;

    TaskHandle_t taskHandle;
    static void loopTask(void *arg);
    TaskHandle_t profileTaskHandle;
    static void profileLoopTask(void *arg);
};

#endif // DEFAULTUI_H
