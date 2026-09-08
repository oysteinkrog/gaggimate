#include "DefaultUI.h"
#include <display/ui/default/GlyphAtlas.h>
#include <ctype.h>
#include <math.h>

#include <WiFi.h>
// Unconditional (unlike the GM_TOUCH_PROBE-guarded include below): setBrightness
// logs on both the device and the simulator, and the simulator's esp_log.h shim
// is only pulled in here, not through any device driver header.
#include "esp_log.h"
#include <cstring>       // memcpy, for the touchmap dump's per-object line copy
#include <esp_heap_caps.h> // heap_caps_malloc/realloc, for the touchmap dump's growable PSRAM buffer (both venues)
#include <freertos/FreeRTOS.h> // portMUX_TYPE / portENTER_CRITICAL / portEXIT_CRITICAL, for the settingsui request queue
#ifdef GM_ANIM_BENCH
#include <display/ui/default/SleepAnimation.h>
const BenchGateState &bench_gate_state() {
    static BenchGateState state;
    return state;
}
#endif
#include <display/core/Controller.h>
#include <display/core/process/BrewProcess.h>
#include <display/core/process/Process.h>
#include <display/core/zones.h>
#ifndef GAGGIMATE_SIM // hardware panel drivers are device-only
#include <display/drivers/AmoledDisplayDriver.h>
#include <display/drivers/LilyGoDriver.h>
#include <display/drivers/WaveshareDriver.h>
#include <display/drivers/common/PanelClock.h>
#include <display/gm_lv_mem.h>

#include <climits>
#ifdef GM_TOUCH_PROBE
#include "esp_log.h"
#include "esp_timer.h"
// Where does a UI pass spend its time? The touch probe showed the LVGL task
// pass effectively running every ~650 ms instead of every 5, which starves
// input and widget updates alike. All on the UI task, so no locking.
namespace {
uint32_t g_uiPassN = 0;
int64_t g_uiPassSum = 0, g_uiPassMax = 0;
uint32_t g_ovlN = 0;
int64_t g_ovlSnapSum = 0, g_ovlSnapMax = 0;
int64_t g_ovlPubSum = 0, g_ovlPubMax = 0;
int64_t g_ovlAreaSum = 0, g_ovlAreaMax = 0;
int64_t g_ovlCopySum = 0, g_ovlCopyArea = 0; // back-buffer repair by copy, per window
// Snapshot sub-stages: alpha-clear memset vs the lv_obj_redraw itself.
int64_t g_snapClearSum = 0, g_snapDrawSum = 0;
int64_t g_uiStatLastLog = 0;
} // namespace
// Defined in eez/actions.cpp: the meter tick-draw handler's share of the
// draw= bucket, plus its clip-precheck hit rate. Outside the anonymous
// namespace, or the extern picks up internal linkage and never resolves.
extern int64_t g_meterDrawUs;
extern uint32_t g_meterDrawCalls, g_meterTicksDrawn, g_meterTicksClipped;
// Defined in the patched LVGL libdep (scripts/patch_lvgl_walkstat.py): the
// walk's event-dispatch / style-lookup / image-blit shares. gm_ws_active is
// raised around the snapshot lv_obj_redraw below so these partition exactly
// the draw= bucket. C symbols, hence the linkage block.
extern "C" {
extern bool gm_ws_active;
extern uint32_t gm_ws_ev_calls;
extern int64_t gm_ws_ev_us;
extern uint32_t gm_ws_style_calls;
extern int64_t gm_ws_style_us;
extern uint32_t gm_ws_img_calls;
extern int64_t gm_ws_img_us;
extern uint32_t gm_ws_rect_calls;
extern int64_t gm_ws_rect_us;
extern uint32_t gm_ws_rectr_calls;
extern int64_t gm_ws_rectr_us;
extern int64_t gm_ws_rect_max_us;
extern uint32_t gm_ws_label_calls;
extern int64_t gm_ws_label_us;
extern uint32_t gm_ws_line_calls;
extern int64_t gm_ws_line_us;
extern uint32_t gm_ws_arc_calls;
extern int64_t gm_ws_arc_us;
}
#endif
#endif
// LV_Helper.h itself is portable (no hardware types), unlike the driver
// headers above it used to sit beside: DefaultUI reads g_touchEdgeAtUs,
// GM_TOUCH_GRACE_US, g_overlayMinRefreshUs and g_uiAnimTestReq on every path,
// sim included. LV_Helper.cpp (the definitions) stays excluded with the rest
// of drivers/; the sim's lv_helper_stub.cpp supplies them instead.
#include "esp_timer.h"
#include <display/drivers/common/LV_Helper.h>
#include <display/main.h>
#include <display/ui/utils/effects.h>
#include <utility>

#include "esp_sntp.h"

#include <display/ui/default/bganim/BgAnim.h>
#include <display/ui/default/bganim/BgAnimCommon.h>
#include <display/ui/default/eez/actions.h>
#include <display/ui/default/eez/images.h>
#include <display/ui/default/eez/ui.h>

// Kitchen-scale glyph for the menu's Scale button (img_scale_80x80.c).
extern const lv_img_dsc_t img_scale_80x80;

static EffectManager effect_mgr;

static constexpr uint32_t STARTUP_FADE_MS = 1000; // standby fade-in duration on power-up
// How long one gradient-preview message holds the panel. The editor re-sends
// on every edit and every few seconds while open, so this only needs to
// outlast the gap between two of those.
static constexpr uint32_t BGANIM_PREVIEW_HOLD_MS = 15000;

namespace {
inline bool areaEmpty(const lv_area_t &a) { return a.x1 > a.x2 || a.y1 > a.y2; }
} // namespace

static constexpr int32_t GAUGE_TICK_LONG = 25;      // meter tick length on most screens
static constexpr int32_t GAUGE_TICK_SHORT = 10;     // shortened tick length on profile / new-menu screens
static constexpr uint32_t GAUGE_TICK_ANIM_MS = 300; // tick length transition duration

// Profile and the new menu screen show shortened meter ticks.
static bool isShortTickScreen(ScreensEnum s) {
    return s == SCREEN_ID_PROFILE_SCREEN || s == SCREEN_ID_MENU_SCREEN_NEW || s == SCREEN_ID_INFO_SCREEN;
}

// Format a millisecond duration as "m:ss" for the brew/profile time labels.
static void formatDuration(unsigned long ms, char *buf, size_t len) {
    const double seconds = ms / 1000.0;
    const int minutes = static_cast<int>(seconds / 60.0);
    const int secs = static_cast<int>(seconds) % 60;
    snprintf(buf, len, "%d:%02d", minutes, secs);
}

static float clampPercentage(float pct) { return pct < 0.0f ? 0.0f : (pct > 100.0f ? 100.0f : pct); }

// EEZ string setters allocate a fresh StringRef on the LVGL heap each call; skip unchanged text to cut churn.
static bool stringChanged(const char *current, const char *next) {
    return current == nullptr || next == nullptr || strcmp(current, next) != 0;
}

int16_t calculate_angle(int set_temp, int range, int offset) {
    const double percentage = static_cast<double>(set_temp) / static_cast<double>(MAX_TEMP);
    return (percentage * ((double)range)) - range / 2 - offset;
}

void DefaultUI::updateTempHistory() {
    if (currentTemp > 0) {
        if (tempHistoryIndex >= TEMP_HISTORY_LENGTH) {
            tempHistoryIndex = 0;
            isTempHistoryInitialized = true;
        }
        tempHistory[tempHistoryIndex] = currentTemp;
        tempHistoryIndex += 1;
    }

    if (tempHistoryIndex % 4 == 0) {
        heatingFlash = !heatingFlash;
        rerender = true;
    }
}

void DefaultUI::updateTempStableFlag() {
    if (isTempHistoryInitialized) {
        float totalError = 0.0f;
        float maxError = 0.0f;
        for (uint16_t i = 0; i < TEMP_HISTORY_LENGTH; i++) {
            float error = abs(tempHistory[i] - targetTemp);
            totalError += error;
            maxError = error > maxError ? error : maxError;
        }

        const float avgError = totalError / TEMP_HISTORY_LENGTH;
        const float errorMargin = max(2.0f, static_cast<float>(targetTemp) * 0.02f);

        isTemperatureStable = avgError < errorMargin && maxError <= errorMargin;
    }

    // instantly reset stability if setpoint has changed
    if (prevTargetTemp != targetTemp) {
        isTemperatureStable = false;
    }

    prevTargetTemp = targetTemp;
}

void DefaultUI::reloadProfiles() { profileLoaded = 0; }

void DefaultUI::setBrightness(int brightness) {
    // The Display category's tests read this line on both the device and the
    // simulator (neither has a way to sample backlight PWM directly).
    ESP_LOGI("DefaultUI", "Display: brightness %d", brightness);
    if (panelDriver) {
        panelDriver->setBrightness(brightness);
    }
}

void DefaultUI::openSettings() { settingsUI.open(); }

void DefaultUI::closeSettings() { settingsUI.close(); }

#ifndef GAGGIMATE_SIM
// One-time carry-over from the single custom gradient (bgAnimCustomTheme,
// selected by bgAnimTheme == bg_theme_count()) to the library: the string
// becomes library entry 1 "Custom", and if it was the active theme every
// animation is pointed at it so nothing changes on screen. Runs only while
// the library is empty, so a user who has since built their own is left
// alone.
void DefaultUI::migrateBgAnimGradients() {
    ::Settings &settings = controller->getSettings();
    if (!settings.getBgAnimGradients().isEmpty()) {
        return;
    }
    uint8_t stops[BG_THEME_MAX_STOPS][3];
    uint8_t pos[BG_THEME_MAX_STOPS];
    bool uniform = true;
    const int n = bg_parse_gradient(settings.getBgAnimCustomTheme().c_str(), stops, pos, uniform);
    if (n == 0) {
        return;
    }
    char gradient[BG_GRADIENT_STR_MAX];
    bg_format_gradient(stops, pos, n, uniform, gradient, sizeof(gradient));
    settings.setBgAnimGradients(String("1|Custom|") + gradient);
    if (settings.getBgAnimTheme() == bg_theme_count() && settings.getBgAnimThemeMap().isEmpty()) {
        String map;
        for (int i = 0; i < bg_animation_count(); i++) {
            if (i > 0) {
                map += ';';
            }
            map += "c1";
        }
        settings.setBgAnimThemeMap(map);
        settings.setBgAnimTheme(0);
    }
    ESP_LOGI("DefaultUI", "custom gradient moved to the library (%d stops)", n);
}
#endif

DefaultUI::DefaultUI(Controller *controller, Driver *driver, PluginManager *pluginManager)
    : controller(controller), panelDriver(driver), pluginManager(pluginManager),
      settingsUI(*controller, *this, *pluginManager) {
    setupPanel();
#ifndef GAGGIMATE_SIM
    // The touch controller read moves off the UI task (gm-2cl.4). The first
    // hit map is published by the first loop() pass.
    if (panelDriver != nullptr && panelDriver->getDisplay() != nullptr) {
        if (!touchtask::start(panelDriver->getDisplay(), &sleepAnimation, PRESS_PLATE_ELEMENT)) {
            log_w("touch task not started; touch stays on the UI task");
        }
    }
#endif
    xTaskCreatePinnedToCore(loopTask, "DefaultUI::loop", configMINIMAL_STACK_SIZE * 6, this, 1, &taskHandle, 1);
}

DefaultUI *DefaultUI::s_instance = nullptr;

void DefaultUI::touchHitHook(lv_obj_t *hit, bool pressed, int16_t x, int16_t y) {
    (void)x;
    (void)y;
    if (s_instance != nullptr) {
        s_instance->onTouchHit(hit, pressed);
    }
}

void DefaultUI::onTouchHit(lv_obj_t *hit, bool pressed) {
#ifndef GAGGIMATE_SIM
    if (touchtask::running()) {
        // The touch task owns the plate: it wrote it at the press edge from
        // the hit map, before LVGL saw the sample.
        return;
    }
    if (!pressed || !pressPlateMode) {
        sleepAnimation.clearElement(PRESS_PLATE_ELEMENT);
        return;
    }
    if (hit == nullptr || hit == lv_scr_act() || lv_obj_has_state(hit, LV_STATE_DISABLED) ||
        !lv_obj_has_flag(hit, LV_OBJ_FLAG_CLICKABLE)) {
        return;
    }
    lv_area_t a;
    lv_obj_get_coords(hit, &a);
    const int w = lv_area_get_width(&a);
    const int h = lv_area_get_height(&a);
    // A container the size of the screen is not a target, whatever LVGL
    // says: no plate over a background press.
    if (w * h > (480 * 480) / 3) {
        return;
    }
    SleepAnimation::ElementDesc e;
    e.type = SleepAnimation::ElementType::RoundRect;
    e.alpha = LV_OPA_40;
    const lv_color_t dim = lv_color_hex(static_cast<uint32_t>(controller->getSettings().getTouchDimColor()));
    e.color = lv_color_to16(dim);
    e.x = static_cast<int16_t>(a.x1 - PRESS_PLATE_OUTSET);
    e.y = static_cast<int16_t>(a.y1 - PRESS_PLATE_OUTSET);
    e.w = static_cast<int16_t>(w + 2 * PRESS_PLATE_OUTSET);
    e.h = static_cast<int16_t>(h + 2 * PRESS_PLATE_OUTSET);
    const int side = e.w < e.h ? e.w : e.h;
    int r = side / 4;
    if (r > 16) {
        r = 16;
    }
    e.radius = static_cast<uint8_t>(r);
    e.tUs = esp_timer_get_time();
    sleepAnimation.setElement(PRESS_PLATE_ELEMENT, e);
#else
    (void)hit;
    (void)pressed;
#endif
}

// The plate is the press feedback whenever the animation composites the
// screen; LVGL's own pressed styles are cleared from the active screen then,
// and put back by the next applyPressedFeedback walk when it stops.
void DefaultUI::updatePressPlateMode() {
#ifndef GAGGIMATE_SIM
    const bool plate = sleepAnimation.isActive();
    if (plate == pressPlateMode) {
        return;
    }
    pressPlateMode = plate;
    g_pressPlateActive = plate;
    pressedStyledRoot = nullptr; // force a walk either way
    if (!plate) {
        sleepAnimation.clearElement(PRESS_PLATE_ELEMENT);
    }
#endif
}

namespace {
#ifndef GAGGIMATE_SIM
// Every lv_meter under obj, in tree order, up to n slots.
void collectDialMeters(lv_obj_t *obj, lv_obj_t **out, int &count, int n) {
    const uint32_t children = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < children && count < n; i++) {
        lv_obj_t *child = lv_obj_get_child(obj, i);
        if (lv_obj_check_type(child, &lv_meter_class)) {
            out[count++] = child;
        }
        collectDialMeters(child, out, count, n);
    }
}

// The meter's scale-lines indicator with a single colour, or nullptr when
// the meter has none or its colour is a gradient (the element paints one
// colour per tick; LVGL keeps those dials).
lv_meter_indicator_t *uniformScaleLines(lv_obj_t *obj, lv_meter_scale_t *&scaleOut) {
    auto *meter = reinterpret_cast<lv_meter_t *>(obj);
    scaleOut = static_cast<lv_meter_scale_t *>(_lv_ll_get_head(&meter->scale_ll));
    if (scaleOut == nullptr) {
        return nullptr;
    }
    lv_meter_indicator_t *found = nullptr;
    for (auto *indic = static_cast<lv_meter_indicator_t *>(_lv_ll_get_head(&meter->indicator_ll)); indic != nullptr;
         indic = static_cast<lv_meter_indicator_t *>(_lv_ll_get_next(&meter->indicator_ll, indic))) {
        if (indic->type != LV_METER_INDICATOR_TYPE_SCALE_LINES) {
            continue;
        }
        if (found != nullptr || indic->scale != scaleOut ||
            indic->type_data.scale_lines.color_start.full != indic->type_data.scale_lines.color_end.full) {
            return nullptr; // two indicators or a gradient: not this element's shape
        }
        found = indic;
    }
    return found;
}
#endif
} // namespace

void DefaultUI::serviceDialElements() {
#ifndef GAGGIMATE_SIM
    const uint32_t frame = sleepAnimation.animFrameCount();
    for (DialRetire &r : dialRetire) {
        if (r.live && frame - r.frame >= 2) {
            meterticks::pin(r.key, false);
            r.live = false;
        }
    }
    // The tick-length morph (animateGaugeTicks) rewrites the cache key every
    // frame; LVGL draws the ring until it has settled and the cache holds
    // the final geometry.
    const bool canOwn = g_dialElementsReq != 0 && sleepAnimation.isActive() && currentScreen == targetScreen &&
                        !panelStopRequested && lv_anim_get(this, gaugeTickAnimCb) == nullptr;
    serviceBarElement(canOwn);
    serviceTextElements(canOwn);
    serviceIconLayers(canOwn);
    if (!canOwn) {
        releaseDialElements();
        return;
    }
    lv_obj_t *meters[DIAL_ELEMENTS] = {nullptr};
    int n = 0;
    collectDialMeters(lv_scr_act(), meters, n, DIAL_ELEMENTS);
    for (int i = 0; i < DIAL_ELEMENTS; i++) {
        DialElement &d = dialElems[i];
        lv_obj_t *m = i < n ? meters[i] : nullptr;
        if (d.meter != m) {
            releaseDialElement(d);
            d.meter = m;
        }
        if (m == nullptr) {
            continue;
        }
        lv_meter_scale_t *scale = nullptr;
        lv_meter_indicator_t *indic = uniformScaleLines(m, scale);
        meterticks::Key key;
        if (!lv_obj_is_visible(m) || indic == nullptr || !meterticks::keyFor(m, key)) {
            releaseDialElement(d);
            continue;
        }
        if (d.owned && !(d.key == key)) {
            releaseDialElement(d);
        }
        if (!d.owned) {
            // Re-owning right after a release would rewrite d.ring while
            // the frame in flight may still read it.
            if (d.releasedRecently && frame - d.releasedFrame < 2) {
                continue;
            }
            if (!meterticks::ring(key, d.ring)) {
                continue; // LVGL draws the ring until the cache holds all of it
            }
            meterticks::pin(key, true);
            d.key = key;
            d.owned = true;
            d.lo = d.hi = -1;
            lv_obj_add_flag(m, LV_OBJ_FLAG_USER_1);
            // LVGL repaints the meter without its ticks; the element covers
            // the same pixels from this frame on, so the handover is one
            // unthrottled refresh with no bare ring in between.
            lv_obj_invalidate(m);
            overlayUrgentUntilUs = esp_timer_get_time() + GM_TOUCH_GRACE_US;
        }
        // The lit range, by LVGL's rule: tick i is lit when its mapped value
        // lies within [start, end]. Values are monotonic in i (min may be
        // above max, as on the pressure dial), so the lit ticks are one run.
        const int cnt = key.cnt;
        int lo = cnt, hi = 0;
        for (int t = 0; t < cnt; t++) {
            const int32_t value = lv_map(t, 0, cnt - 1, scale->min, scale->max);
            if (value >= indic->start_value && value <= indic->end_value) {
                if (t < lo) {
                    lo = t;
                }
                hi = t + 1;
            }
        }
        if (hi <= lo) {
            lo = hi = 0;
        }
        const uint16_t lit = lv_color_to16(indic->type_data.scale_lines.color_start);
        const uint16_t unlit = lv_color_to16(scale->tick_color);
        if (lo == d.lo && hi == d.hi && lit == d.lit && unlit == d.unlit) {
            continue;
        }
        d.lo = static_cast<int16_t>(lo);
        d.hi = static_cast<int16_t>(hi);
        d.lit = lit;
        d.unlit = unlit;
        tickring::Box bb;
        if (!tickring::bounds(d.ring, bb)) {
            releaseDialElement(d);
            continue;
        }
        SleepAnimation::ElementDesc e;
        e.type = SleepAnimation::ElementType::TickRing;
        e.alpha = 255;
        e.x = bb.x1;
        e.y = bb.y1;
        e.w = static_cast<int16_t>(bb.x2 - bb.x1 + 1);
        e.h = static_cast<int16_t>(bb.y2 - bb.y1 + 1);
        e.ring.ring = &d.ring;
        e.ring.litColor = lit;
        e.ring.unlitColor = unlit;
        e.ring.lo = d.lo;
        e.ring.hi = d.hi;
        e.tUs = esp_timer_get_time();
        sleepAnimation.setElement(DIAL_ELEMENT_BASE + i, e);
    }
#endif
}

void DefaultUI::releaseDialElement(DialElement &d) {
#ifndef GAGGIMATE_SIM
    if (!d.owned) {
        return;
    }
    const int slot = static_cast<int>(&d - dialElems);
    sleepAnimation.clearElement(DIAL_ELEMENT_BASE + slot);
    if (d.meter != nullptr) {
        lv_obj_clear_flag(d.meter, LV_OBJ_FLAG_USER_1);
        lv_obj_invalidate(d.meter);
        overlayUrgentUntilUs = esp_timer_get_time() + GM_TOUCH_GRACE_US;
    }
    retireDialRing(d.key);
    d.owned = false;
    d.lo = d.hi = -1;
    d.releasedFrame = sleepAnimation.animFrameCount();
    d.releasedRecently = true;
#else
    (void)d;
#endif
}

void DefaultUI::retireDialRing(const meterticks::Key &key) {
#ifndef GAGGIMATE_SIM
    const uint32_t frame = sleepAnimation.animFrameCount();
    for (DialRetire &r : dialRetire) {
        if (!r.live) {
            r.key = key;
            r.frame = frame;
            r.live = true;
            return;
        }
    }
    // No free entry: the oldest one has certainly been latched past by now.
    DialRetire *oldest = &dialRetire[0];
    for (DialRetire &r : dialRetire) {
        if (r.frame < oldest->frame) {
            oldest = &r;
        }
    }
    meterticks::pin(oldest->key, false);
    oldest->key = key;
    oldest->frame = frame;
#else
    (void)key;
#endif
}

void DefaultUI::releaseDialElements() {
    for (DialElement &d : dialElems) {
        releaseDialElement(d);
    }
    releaseBarElement();
}

void DefaultUI::serviceBarElement(bool canOwn) {
#ifndef GAGGIMATE_SIM
    lv_obj_t *bar = objects.brew_bar;
    if (!canOwn || bar == nullptr || lv_obj_get_screen(bar) != lv_scr_act() || !lv_obj_is_visible(bar)) {
        releaseBarElement();
        return;
    }
    if (barElem.bar != bar) {
        releaseBarElement();
        barElem.bar = bar;
    }
    // The fill's geometry, the way lv_bar's draw_indic lays it out for a
    // horizontal left-to-right bar at rest: the track is the bar's box less
    // its main-part padding, the fill runs from the track's left edge for
    // the value's share of the track width.
    lv_area_t coords;
    lv_obj_get_coords(bar, &coords);
    const int x1 = coords.x1 + lv_obj_get_style_pad_left(bar, LV_PART_MAIN);
    const int x2 = coords.x2 - lv_obj_get_style_pad_right(bar, LV_PART_MAIN);
    const int y1 = coords.y1 + lv_obj_get_style_pad_top(bar, LV_PART_MAIN);
    const int y2 = coords.y2 - lv_obj_get_style_pad_bottom(bar, LV_PART_MAIN);
    const int trackW = x2 - x1 + 1;
    const int32_t range = lv_bar_get_max_value(bar) - lv_bar_get_min_value(bar);
    if (trackW <= 0 || y2 < y1 || range <= 0) {
        releaseBarElement();
        return;
    }
    const int32_t value = lv_bar_get_value(bar) - lv_bar_get_min_value(bar);
    const float target = static_cast<float>(x1) + static_cast<float>(trackW) * static_cast<float>(value) / range;
    const int64_t now = esp_timer_get_time();
    if (!barElem.owned) {
        barElem.owned = true;
        barElem.x2 = target; // no slide in from zero on takeover
        barElem.lastX2 = -1;
        barElem.opa = lv_obj_get_style_bg_opa(bar, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, LV_PART_INDICATOR);
        overlayUrgentUntilUs = now + GM_TOUCH_GRACE_US;
    } else {
        int64_t dt = now - barElem.lastUs;
        if (dt < 0) {
            dt = 0;
        }
        if (dt > 100000) {
            dt = 100000;
        }
        const float k = static_cast<float>(dt) / static_cast<float>(dt + kBarEaseTauUs);
        barElem.x2 += (target - barElem.x2) * k;
        if ((target - barElem.x2) * (target - barElem.x2) < 0.01f) {
            barElem.x2 = target;
        }
    }
    barElem.lastUs = now;
    const int16_t edge = static_cast<int16_t>(barElem.x2 + 0.5f);
    if (edge == barElem.lastX2) {
        return;
    }
    barElem.lastX2 = edge;
    const int w = edge - x1;
    if (w <= 0) {
        sleepAnimation.clearElement(BAR_ELEMENT);
        return;
    }
    SleepAnimation::ElementDesc e;
    e.type = SleepAnimation::ElementType::RoundRect;
    e.alpha = barElem.opa;
    e.color = lv_color_to16(lv_obj_get_style_bg_color(bar, LV_PART_INDICATOR));
    int radius = lv_obj_get_style_radius(bar, LV_PART_INDICATOR);
    const int h = y2 - y1 + 1;
    if (radius > h / 2) {
        radius = h / 2; // LV_RADIUS_CIRCLE and anything larger: a pill
    }
    if (radius > SleepAnimation::kElementMaxRadius) {
        radius = SleepAnimation::kElementMaxRadius;
    }
    e.radius = static_cast<uint8_t>(radius < 0 ? 0 : radius);
    e.x = static_cast<int16_t>(x1);
    e.y = static_cast<int16_t>(y1);
    e.w = static_cast<int16_t>(w);
    e.h = static_cast<int16_t>(h);
    e.tUs = now;
    sleepAnimation.setElement(BAR_ELEMENT, e);
#else
    (void)canOwn;
#endif
}

void DefaultUI::releaseBarElement() {
#ifndef GAGGIMATE_SIM
    if (!barElem.owned) {
        return;
    }
    sleepAnimation.clearElement(BAR_ELEMENT);
    if (barElem.bar != nullptr) {
        lv_obj_remove_local_style_prop(barElem.bar, LV_STYLE_BG_OPA, LV_PART_INDICATOR);
        overlayUrgentUntilUs = esp_timer_get_time() + GM_TOUCH_GRACE_US;
    }
    barElem.owned = false;
    barElem.lastX2 = -1;
#endif
}

// ---- Text elements (gm-2cl.5) ----

namespace {
uint32_t fnv1a(uint32_t h, const void *data, size_t n) {
    const uint8_t *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}
uint32_t textHash(const char *s) { return fnv1a(2166136261u, s, strlen(s)); }
void copyStr(char *dst, size_t cap, const char *src) { snprintf(dst, cap, "%s", src); }
} // namespace

void DefaultUI::textLabelDeleted(lv_event_t *e) {
    DefaultUI *ui = static_cast<DefaultUI *>(lv_event_get_user_data(e));
    lv_obj_t *obj = lv_event_get_target(e);
    if (ui == nullptr) {
        return;
    }
    for (TextElement &t : ui->textElems) {
        if (t.label == obj) {
            // The object is going away under us: drop the element without
            // touching the label.
            t.label = nullptr;
            if (t.owned) {
                ui->sleepAnimation.clearElement(TEXT_ELEMENT_BASE + static_cast<int>(&t - ui->textElems));
                t.owned = false;
            }
        }
    }
    for (int i = 0; i < ui->liveLabelN; i++) {
        if (ui->liveLabels[i].obj == obj) {
            ui->liveLabels[i] = ui->liveLabels[ui->liveLabelN - 1];
            ui->liveLabelN--;
            break;
        }
    }
}

DefaultUI::LiveLabel *DefaultUI::liveLabelFor(lv_obj_t *obj) {
    for (int i = 0; i < liveLabelN; i++) {
        if (liveLabels[i].obj == obj) {
            return &liveLabels[i];
        }
    }
    return nullptr;
}

void DefaultUI::scanLiveLabels(lv_obj_t *obj) {
    if (lv_obj_check_type(obj, &lv_label_class)) {
        const char *txt = lv_label_get_text(obj);
        const uint32_t h = txt != nullptr ? textHash(txt) : 0;
        LiveLabel *l = liveLabelFor(obj);
        if (l == nullptr) {
            if (liveLabelN < kLiveLabelCap) {
                l = &liveLabels[liveLabelN++];
                l->obj = obj;
                l->hash = h;
                l->live = false;
                l->refused = false;
                lv_obj_add_event_cb(obj, textLabelDeleted, LV_EVENT_DELETE, this);
            }
        } else if (l->hash != h) {
            l->hash = h;
            l->live = true;
        }
        if (l != nullptr) {
            l->seen = liveLabelPass;
        }
        return; // labels have no children of interest
    }
    const uint32_t n = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < n; i++) {
        scanLiveLabels(lv_obj_get_child(obj, i));
    }
}

bool DefaultUI::textLabelEligible(lv_obj_t *label, bool owned) const {
    // The label and its ancestors visible: the flow drives the hidden flag
    // and an owned label keeps it, so a label the flow hides is released.
    (void)owned;
    for (lv_obj_t *p = label; p != nullptr; p = lv_obj_get_parent(p)) {
        if (lv_obj_has_flag(p, LV_OBJ_FLAG_HIDDEN)) {
            return false;
        }
    }
    if (lv_label_get_recolor(label)) {
        return false;
    }
    const lv_label_long_mode_t lm = lv_label_get_long_mode(label);
    if (lm == LV_LABEL_LONG_SCROLL || lm == LV_LABEL_LONG_SCROLL_CIRCULAR) {
        return false;
    }
    if (lv_obj_get_style_text_opa(label, LV_PART_MAIN) == LV_OPA_TRANSP) {
        return false;
    }
    if (lv_obj_get_style_text_decor(label, LV_PART_MAIN) != LV_TEXT_DECOR_NONE) {
        return false;
    }
    const lv_label_t *lbl = reinterpret_cast<const lv_label_t *>(label);
    if (lbl->offset.x != 0 || lbl->offset.y != 0) {
        return false;
    }
    const char *txt = lv_label_get_text(label);
    return txt != nullptr && txt[0] != '\0' && strlen(txt) < static_cast<size_t>(kTextMaxLen) &&
           strchr(txt, '\n') == nullptr;
}

// Places txt's glyphs the way lv_draw_label does for a single line inside
// the label's content box (lv_label.c draw_main, lv_draw_label.c,
// lv_draw_sw_letter.c): pen from the content box's top left, moved for
// CENTER and RIGHT by the line width, each glyph at pen + ofs_x and
// pen.y + (line_height - base_line) - box_h - ofs_y, the pen advancing by
// the kerned advance plus letter_space. False when the text would wrap,
// a glyph is not in the atlas or the list is full.
bool DefaultUI::buildTextGlyphs(lv_obj_t *label, const char *txt, SleepAnimation::TextDesc &t, int &bx0, int &by0,
                                int &bx1, int &by1) const {
    t.n = 0;
    lv_obj_update_layout(label);
    lv_area_t coords;
    lv_obj_get_content_coords(label, &coords);
    const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    if (font == nullptr) {
        g_textDbg[3] = 5;
        return false;
    }
    const lv_coord_t letterSpace = lv_obj_get_style_text_letter_space(label, LV_PART_MAIN);
    const lv_coord_t lineSpace = lv_obj_get_style_text_line_space(label, LV_PART_MAIN);
    const lv_label_t *lbl = reinterpret_cast<const lv_label_t *>(label);
    lv_text_flag_t flag = LV_TEXT_FLAG_NONE;
    if (lbl->expand != 0) {
        flag = static_cast<lv_text_flag_t>(flag | LV_TEXT_FLAG_EXPAND);
    }
    if (lv_obj_get_style_width(label, LV_PART_MAIN) == LV_SIZE_CONTENT && !label->w_layout) {
        flag = static_cast<lv_text_flag_t>(flag | LV_TEXT_FLAG_FIT);
    }
    const lv_coord_t boxW = lv_area_get_width(&coords);
    const lv_coord_t wrapW = (flag & LV_TEXT_FLAG_EXPAND) ? LV_COORD_MAX : boxW;
    const int32_t lineHeight = lv_font_get_line_height(font);
    lv_point_t size;
    lv_txt_get_size(&size, txt, font, letterSpace, lineSpace, wrapW, flag);
    if (size.y > lineHeight) {
        g_textDbg[3] = 1;
        return false; // more than one line
    }
    lv_text_align_t align = lv_obj_calculate_style_text_align(label, LV_PART_MAIN, txt);
    const lv_coord_t lineWidth = lv_txt_get_width(txt, static_cast<uint32_t>(strlen(txt)), font, letterSpace, flag);
    int32_t penX = coords.x1;
    const int32_t penY = coords.y1;
    if (align == LV_TEXT_ALIGN_CENTER) {
        penX += (boxW - lineWidth) / 2;
    } else if (align == LV_TEXT_ALIGN_RIGHT) {
        penX += boxW - lineWidth;
    }
    const int32_t baseY = penY + (font->line_height - font->base_line);
    bx0 = by0 = 0x7fff;
    bx1 = by1 = -0x7fff;
    uint32_t i = 0;
    while (txt[i] != '\0') {
        uint32_t letter = 0, letterNext = 0;
        _lv_txt_encoded_letter_next_2(txt, &letter, &letterNext, &i);
        if (letter == 0) {
            break;
        }
        const lv_coord_t letterW = lv_font_get_glyph_width(font, letter, letterNext);
        glyphatlas::Glyph g;
        if (!glyphatlas::get(font, letter, g)) {
            g_textDbg[3] = 2;
            return false;
        }
        if (g.w > 0 && g.h > 0 && g.a8 != nullptr) {
            if (t.n >= SleepAnimation::kTextMaxGlyphs) {
                g_textDbg[3] = 3;
                return false;
            }
            SleepAnimation::TextGlyph &tg = t.g[t.n++];
            tg.x = static_cast<int16_t>(penX + g.ofsX);
            tg.y = static_cast<int16_t>(baseY - g.h - g.ofsY);
            tg.w = g.w;
            tg.h = g.h;
            tg.a8 = g.a8;
            if (tg.x < bx0) {
                bx0 = tg.x;
            }
            if (tg.y < by0) {
                by0 = tg.y;
            }
            if (tg.x + tg.w > bx1) {
                bx1 = tg.x + tg.w;
            }
            if (tg.y + tg.h > by1) {
                by1 = tg.y + tg.h;
            }
        }
        if (letterW > 0) {
            penX += letterW + letterSpace;
        }
    }
    if (t.n == 0) {
        g_textDbg[3] = 4;
    }
    return t.n > 0;
}

// The first number in target (optional sign, digits, optional fraction) is
// eased toward from the value shown so far when the text around it and its
// format have not changed; anything else snaps. shownText is what the
// element renders.
void DefaultUI::easeTextValue(TextElement &t, const char *target, int64_t now) {
    const char *p = target;
    while (*p != '\0' && !(isdigit(static_cast<unsigned char>(*p)) ||
                           (*p == '-' && isdigit(static_cast<unsigned char>(p[1]))))) {
        p++;
    }
    if (*p == '\0') {
        t.numeric = false;
        copyStr(t.shownText, sizeof(t.shownText), target);
        return;
    }
    const char *numStart = p;
    if (*p == '-') {
        p++;
    }
    const char *intStart = p;
    while (isdigit(static_cast<unsigned char>(*p))) {
        p++;
    }
    const int intDigits = static_cast<int>(p - intStart);
    int decimals = 0;
    if (*p == '.' && isdigit(static_cast<unsigned char>(p[1]))) {
        p++;
        while (isdigit(static_cast<unsigned char>(*p))) {
            p++;
            decimals++;
        }
    }
    const char *numEnd = p;
    char prefix[kTextMaxLen];
    const size_t preLen = static_cast<size_t>(numStart - target);
    memcpy(prefix, target, preLen);
    prefix[preLen] = '\0';
    const bool padded = intDigits > 1 && intStart[0] == '0';
    const float value = strtof(numStart, nullptr);
    const bool sameShape = t.numeric && strcmp(prefix, t.prefix) == 0 && strcmp(numEnd, t.suffix) == 0 &&
                           decimals == t.decimals && padded == t.padded && g_textEaseReq != 0;
    if (!sameShape) {
        t.numeric = true;
        t.shown = value;
        t.target = value;
        t.decimals = decimals;
        t.intDigits = intDigits;
        t.padded = padded;
        copyStr(t.prefix, sizeof(t.prefix), prefix);
        copyStr(t.suffix, sizeof(t.suffix), numEnd);
        t.lastUs = now;
        copyStr(t.shownText, sizeof(t.shownText), target);
        return;
    }
    t.target = value;
    t.intDigits = intDigits;
    int64_t dt = now - t.lastUs;
    t.lastUs = now;
    if (dt < 0) {
        dt = 0;
    }
    if (dt > 100000) {
        dt = 100000;
    }
    const float k = static_cast<float>(dt) / static_cast<float>(dt + kTextEaseTauUs);
    t.shown += (t.target - t.shown) * k;
    float unit = 1.0f;
    for (int d = 0; d < decimals; d++) {
        unit *= 0.1f;
    }
    if (fabsf(t.target - t.shown) < unit * 0.5f) {
        t.shown = t.target;
    }
    if (t.shown == t.target) {
        copyStr(t.shownText, sizeof(t.shownText), target);
        return;
    }
    char num[24];
    if (padded) {
        const int width = intDigits + (decimals > 0 ? decimals + 1 : 0) + (t.shown < 0 ? 1 : 0);
        snprintf(num, sizeof(num), "%0*.*f", width, decimals, static_cast<double>(t.shown));
    } else {
        snprintf(num, sizeof(num), "%.*f", decimals, static_cast<double>(t.shown));
    }
    snprintf(t.shownText, sizeof(t.shownText), "%s%s%s", t.prefix, num, t.suffix);
}

void DefaultUI::serviceTextElements(bool canOwn) {
#ifndef GAGGIMATE_SIM
    if (!canOwn || g_textElementsReq == 0) {
        releaseTextElements();
        return;
    }
    lv_obj_t *scr = lv_scr_act();
    if (scr != liveScreen) {
        releaseTextElements();
        for (int i = 0; i < liveLabelN; i++) {
            if (liveLabels[i].obj != nullptr) {
                lv_obj_remove_event_cb_with_user_data(liveLabels[i].obj, textLabelDeleted, this);
            }
        }
        liveLabelN = 0;
        liveScreen = scr;
    }
    liveLabelPass++;
    scanLiveLabels(scr);
    // Labels that left the tree without a DELETE event (reparented) drop out.
    for (int i = 0; i < liveLabelN;) {
        if (liveLabels[i].seen != liveLabelPass) {
            for (TextElement &t : textElems) {
                if (t.label == liveLabels[i].obj) {
                    releaseTextElement(t);
                    t.label = nullptr;
                }
            }
            liveLabels[i] = liveLabels[liveLabelN - 1];
            liveLabelN--;
        } else {
            i++;
        }
    }
    const int64_t now = esp_timer_get_time();
    // Owned labels: still eligible, and still hidden by us.
    for (TextElement &t : textElems) {
        if (!t.owned) {
            continue;
        }
        if (t.label == nullptr) {
            t.owned = false;
            continue;
        }
        if (!textLabelEligible(t.label, true)) {
            g_textDbg[4]++;
            releaseTextElement(t);
        }
    }
    // Take live, eligible labels into free slots.
    for (int i = 0; i < liveLabelN; i++) {
        LiveLabel &l = liveLabels[i];
        if (!l.live || l.refused || l.obj == nullptr) {
            continue;
        }
        bool have = false;
        for (TextElement &t : textElems) {
            if (t.owned && t.label == l.obj) {
                have = true;
                break;
            }
        }
        if (have) {
            continue;
        }
        TextElement *slot = nullptr;
        for (TextElement &t : textElems) {
            if (!t.owned) {
                slot = &t;
                break;
            }
        }
        if (slot == nullptr) {
            break;
        }
        if (!textLabelEligible(l.obj, false)) {
            continue;
        }
        SleepAnimation::TextDesc td;
        int bx0, by0, bx1, by1;
        if (!buildTextGlyphs(l.obj, lv_label_get_text(l.obj), td, bx0, by0, bx1, by1)) {
            l.refused = true; // a font or shape the atlas cannot carry
            g_textDbg[2]++;
            continue;
        }
        g_textDbg[0]++;
        slot->label = l.obj;
        slot->owned = true;
        slot->hash = 0;
        slot->numeric = false;
        slot->shownText[0] = '\0';
        // With USER_2 set the patched lv_label draws nothing and stops
        // invalidating on set_text (scripts/patch_lvgl_label_elem.py). LVGL
        // repaints the label's area without it; the element covers the same
        // pixels from this frame on, one unthrottled refresh, as the dial
        // rings do.
        lv_obj_add_flag(l.obj, LV_OBJ_FLAG_USER_2);
        lv_obj_invalidate(l.obj);
        overlayUrgentUntilUs = now + GM_TOUCH_GRACE_US;
    }
    // Rebuild what changed.
    for (TextElement &t : textElems) {
        if (!t.owned || t.label == nullptr) {
            continue;
        }
        easeTextValue(t, lv_label_get_text(t.label), now);
        lv_obj_update_layout(t.label);
        lv_area_t coords;
        lv_obj_get_content_coords(t.label, &coords);
        const lv_color_t color = lv_obj_get_style_text_color_filtered(t.label, LV_PART_MAIN);
        const lv_opa_t opa = lv_obj_get_style_text_opa(t.label, LV_PART_MAIN);
        const lv_font_t *font = lv_obj_get_style_text_font(t.label, LV_PART_MAIN);
        uint32_t h = textHash(t.shownText);
        h = fnv1a(h, &coords, sizeof(coords));
        h = fnv1a(h, &color, sizeof(color));
        h = fnv1a(h, &opa, sizeof(opa));
        h = fnv1a(h, &font, sizeof(font));
        if (h == t.hash) {
            continue;
        }
        SleepAnimation::TextDesc td;
        int bx0, by0, bx1, by1;
        if (!buildTextGlyphs(t.label, t.shownText, td, bx0, by0, bx1, by1)) {
            LiveLabel *l = liveLabelFor(t.label);
            if (l != nullptr) {
                l->refused = true;
            }
            releaseTextElement(t);
            continue;
        }
        t.hash = h;
        t.ver++;
        g_textDbg[5]++;
        {
            TextElemDbg &dbg = g_textElemDbg[&t - textElems];
            dbg.owned = true;
            dbg.x = static_cast<int16_t>(bx0);
            dbg.y = static_cast<int16_t>(by0);
            dbg.w = static_cast<int16_t>(bx1 - bx0);
            dbg.h = static_cast<int16_t>(by1 - by0);
            dbg.ver = t.ver;
            copyStr(dbg.text, sizeof(dbg.text), t.shownText);
        }
        SleepAnimation::ElementDesc e;
        e.type = SleepAnimation::ElementType::Text;
        e.alpha = opa;
        e.color = lv_color_to16(color);
        e.textSlot = static_cast<uint8_t>(&t - textElems);
        e.ver = t.ver;
        e.x = static_cast<int16_t>(bx0);
        e.y = static_cast<int16_t>(by0);
        e.w = static_cast<int16_t>(bx1 - bx0);
        e.h = static_cast<int16_t>(by1 - by0);
        e.tUs = now;
        sleepAnimation.setTextElement(TEXT_ELEMENT_BASE + e.textSlot, e, td);
    }
#else
    (void)canOwn;
#endif
}

void DefaultUI::releaseTextElement(TextElement &t) {
#ifndef GAGGIMATE_SIM
    if (!t.owned) {
        return;
    }
    sleepAnimation.clearElement(TEXT_ELEMENT_BASE + static_cast<int>(&t - textElems));
    g_textElemDbg[&t - textElems].owned = false;
    if (t.label != nullptr) {
        lv_obj_clear_flag(t.label, LV_OBJ_FLAG_USER_2);
        lv_obj_invalidate(t.label);
        overlayUrgentUntilUs = esp_timer_get_time() + GM_TOUCH_GRACE_US;
    }
    t.owned = false;
    t.hash = 0;
    t.numeric = false;
#else
    (void)t;
#endif
}

void DefaultUI::releaseTextElements() {
    for (TextElement &t : textElems) {
        releaseTextElement(t);
    }
}

void DefaultUI::iconDeleted(lv_event_t *e) {
    DefaultUI *ui = static_cast<DefaultUI *>(lv_event_get_user_data(e));
    lv_obj_t *obj = lv_event_get_target(e);
    if (ui == nullptr) {
        return;
    }
    for (IconLayer &l : ui->iconLayers) {
        if (l.obj == obj) {
            // The object is going away under us: free the layers without
            // touching the image.
            l.obj = nullptr;
            ui->releaseIconLayer(l);
        }
    }
    for (int i = 0; i < ui->iconCandN; i++) {
        if (ui->iconCands[i].obj == obj) {
            ui->iconCands[i] = ui->iconCands[ui->iconCandN - 1];
            ui->iconCandN--;
            break;
        }
    }
}

void DefaultUI::scanIcons(lv_obj_t *obj) {
    if (lv_obj_check_type(obj, &lv_img_class)) {
        const bool hidden = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
        const uint16_t state = lv_obj_get_state(obj);
        IconCand *c = nullptr;
        for (int i = 0; i < iconCandN; i++) {
            if (iconCands[i].obj == obj) {
                c = &iconCands[i];
                break;
            }
        }
        if (c == nullptr) {
            if (iconCandN < kIconCandCap && lv_obj_get_width(obj) * lv_obj_get_height(obj) <= kIconMaxPx) {
                c = &iconCands[iconCandN++];
                c->obj = obj;
                c->hidden = hidden;
                c->state = state;
                c->toggles = 0;
                c->refused = false;
                lv_obj_add_event_cb(obj, iconDeleted, LV_EVENT_DELETE, this);
            }
        } else if (c->hidden != hidden || c->state != state) {
            c->hidden = hidden;
            c->state = state;
            if (c->toggles < 255) {
                c->toggles++;
            }
        }
        if (c != nullptr) {
            c->seen = iconPass;
        }
        return;
    }
    const uint32_t n = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < n; i++) {
        scanIcons(lv_obj_get_child(obj, i));
    }
}

void DefaultUI::releaseIconLayer(IconLayer &l) {
#ifndef GAGGIMATE_SIM
    for (int i = 0; i < kIconSprites; i++) {
        if (l.layer[i] >= 0) {
            sleepAnimation.layerRelease(l.layer[i]);
            l.layer[i] = -1;
        }
    }
    g_iconLayerDbg[&l - iconLayers].owned = false;
    if (l.obj != nullptr) {
        // Clear the flag first: the invalidation has to reach the display.
        // A no-op while the image is hidden, which is right.
        lv_obj_clear_flag(l.obj, LV_OBJ_FLAG_USER_3);
        lv_obj_invalidate(l.obj);
        overlayUrgentUntilUs = esp_timer_get_time() + GM_TOUCH_GRACE_US;
    }
    l.obj = nullptr;
    l.src = nullptr;
    l.sprites = 0;
    l.shown = -1;
#else
    (void)l;
#endif
}

void DefaultUI::releaseIconLayers() {
    for (IconLayer &l : iconLayers) {
        releaseIconLayer(l);
    }
}

// Renders the image in its current state into a new sprite and returns its
// index, or -1 when there is no room or the render failed. The owner flag
// is lifted for the render because the patched lv_img draws nothing under
// it; lifting and restoring it invalidates nothing.
int DefaultUI::snapshotIconSprite(IconLayer &l) {
#ifndef GAGGIMATE_SIM
    if (l.sprites >= kIconSprites || l.obj == nullptr) {
        return -1;
    }
    lv_obj_t *img = l.obj;
    const lv_coord_t ext = _lv_obj_get_ext_draw_size(img);
    const int lw = lv_obj_get_width(img) + ext * 2;
    const int lh = lv_obj_get_height(img) + ext * 2;
    if (lw <= 0 || lh <= 0 || lw * lh > kIconMaxPx * 2) {
        return -1;
    }
    const int id = sleepAnimation.layerAcquire(lw, lh);
    if (id < 0) {
        return -1;
    }
    const bool owned = lv_obj_has_flag(img, LV_OBJ_FLAG_USER_3);
    if (owned) {
        lv_obj_clear_flag(img, LV_OBJ_FLAG_USER_3);
    }
    lv_area_t area;
    const bool ok = snapshotObjectToBuffer(img, sleepAnimation.layerBuffer(id), static_cast<uint32_t>(lw) * lh * 3, &area);
    if (owned) {
        lv_obj_add_flag(img, LV_OBJ_FLAG_USER_3);
    }
    if (!ok) {
        sleepAnimation.layerRelease(id);
        return -1;
    }
    sleepAnimation.layerPublish(id, area.x1, area.y1);
    sleepAnimation.layerHide(id);
    const int idx = l.sprites++;
    l.layer[idx] = id;
    l.state[idx] = lv_obj_get_state(img);
    IconLayerDbg &d = g_iconLayerDbg[&l - iconLayers];
    d.x = static_cast<int16_t>(area.x1);
    d.y = static_cast<int16_t>(area.y1);
    d.w = static_cast<int16_t>(lw);
    d.h = static_cast<int16_t>(lh);
    return idx;
#else
    (void)l;
    return -1;
#endif
}

bool DefaultUI::takeIconLayer(IconLayer &l, lv_obj_t *img) {
#ifndef GAGGIMATE_SIM
    lv_obj_update_layout(img);
    l.obj = img;
    l.sprites = 0;
    l.shown = -1;
    const int idx = snapshotIconSprite(l);
    if (idx < 0) {
        l.obj = nullptr;
        return false;
    }
    // Same handover as moveObjectViaLayer: the sprite is drawn from the
    // first overlay publish that no longer holds the image. That publish
    // comes from this invalidation, recorded before the flag blocks them.
    lv_obj_invalidate(img);
    lv_obj_add_flag(img, LV_OBJ_FLAG_USER_3);
    sleepAnimation.layerShowAtGen(l.layer[idx], sleepAnimation.overlayPublishGen() + 1);
    overlayUrgentUntilUs = esp_timer_get_time() + GM_TOUCH_GRACE_US;
    l.shown = idx;
    l.src = lv_img_get_src(img);
    lv_obj_get_coords(img, &l.coords);
    IconLayerDbg &d = g_iconLayerDbg[&l - iconLayers];
    d.toggles = 0;
    d.shown = true;
    d.owned = true;
    return true;
#else
    (void)l;
    (void)img;
    return false;
#endif
}

void DefaultUI::serviceIconLayers(bool canOwn) {
#ifndef GAGGIMATE_SIM
    if (!canOwn || g_iconLayersReq == 0) {
        releaseIconLayers();
        return;
    }
    lv_obj_t *scr = lv_scr_act();
    if (scr != iconScreen) {
        releaseIconLayers();
        for (int i = 0; i < iconCandN; i++) {
            if (iconCands[i].obj != nullptr) {
                lv_obj_remove_event_cb_with_user_data(iconCands[i].obj, iconDeleted, this);
            }
        }
        iconCandN = 0;
        iconScreen = scr;
    }
    iconPass++;
    scanIcons(scr);
    // Images that left the tree without a DELETE event drop out.
    for (int i = 0; i < iconCandN;) {
        if (iconCands[i].seen != iconPass) {
            for (IconLayer &l : iconLayers) {
                if (l.obj == iconCands[i].obj) {
                    l.obj = nullptr; // never touch it again
                    releaseIconLayer(l);
                }
            }
            iconCands[i] = iconCands[iconCandN - 1];
            iconCandN--;
        } else {
            i++;
        }
    }
    // Owned images: same picture in the same place, and the sprite shown
    // follows the state and the hidden flags (an ancestor's counts too).
    for (IconLayer &l : iconLayers) {
        if (l.obj == nullptr) {
            continue;
        }
        lv_area_t coords;
        lv_obj_get_coords(l.obj, &coords);
        if (lv_img_get_src(l.obj) != l.src || !_lv_area_is_equal(&coords, &l.coords)) {
            releaseIconLayer(l);
            continue;
        }
        bool hidden = false;
        for (lv_obj_t *p = l.obj; p != nullptr; p = lv_obj_get_parent(p)) {
            if (lv_obj_has_flag(p, LV_OBJ_FLAG_HIDDEN)) {
                hidden = true;
                break;
            }
        }
        int want = -1;
        if (!hidden) {
            const uint16_t st = lv_obj_get_state(l.obj);
            for (int i = 0; i < l.sprites; i++) {
                if (l.state[i] == st) {
                    want = i;
                    break;
                }
            }
            if (want < 0) {
                want = snapshotIconSprite(l);
                if (want < 0) {
                    // A third state, or no room: LVGL takes it back.
                    releaseIconLayer(l);
                    continue;
                }
            }
        }
        if (want != l.shown) {
            if (l.shown >= 0) {
                sleepAnimation.layerHide(l.layer[l.shown]);
            }
            if (want >= 0) {
                sleepAnimation.layerShow(l.layer[want]);
            }
            l.shown = want;
            IconLayerDbg &d = g_iconLayerDbg[&l - iconLayers];
            d.shown = want >= 0;
            d.toggles = d.toggles + 1;
        }
    }
    // Take blinking, visible images into free slots.
    for (int i = 0; i < iconCandN; i++) {
        IconCand &c = iconCands[i];
        if (c.toggles < 2 || c.refused || c.hidden || c.obj == nullptr) {
            continue;
        }
        bool have = false;
        for (IconLayer &l : iconLayers) {
            if (l.obj == c.obj) {
                have = true;
                break;
            }
        }
        if (have) {
            continue;
        }
        IconLayer *slot = nullptr;
        for (IconLayer &l : iconLayers) {
            if (l.obj == nullptr) {
                slot = &l;
                break;
            }
        }
        if (slot == nullptr) {
            break;
        }
        if (!takeIconLayer(*slot, c.obj)) {
            c.refused = true;
        }
    }
#else
    (void)canOwn;
#endif
}

void DefaultUI::init() {
    s_instance = this;
    g_touchHitHook = &DefaultUI::touchHitHook;
    profileManager = controller->getProfileManager();
    g_overlayMinRefreshUs = OVERLAY_MIN_REFRESH_US;
    auto triggerRender = [this](Event const &) { rerender = true; };
    pluginManager->on("boiler:currentTemperature:change", [this](Event const &event) {
        int newTemp = static_cast<int>(event.getFloat("value"));
        if (newTemp != currentTemp) {
            currentTemp = newTemp;
            rerender = true;
        }
    });
    pluginManager->on("boiler:pressure:change", [this](Event const &event) {
        float newPressure = event.getFloat("value");
        if (round(newPressure * 10.0f) != round(pressure * 10.0f)) {
            pressure = newPressure;
            rerender = true;
        }
    });
    pluginManager->on("boiler:targetTemperature:change", [this](Event const &event) {
        int newTemp = static_cast<int>(event.getFloat("value"));
        if (newTemp != targetTemp) {
            targetTemp = newTemp;
            rerender = true;
        }
    });
    pluginManager->on("controller:targetVolume:change", [this](Event const &event) { rerender = true; });
    pluginManager->on("controller:targetDuration:change", [this](Event const &event) { rerender = true; });
    pluginManager->on("controller:grindDuration:change", [this](Event const &event) { rerender = true; });
    pluginManager->on("controller:grindVolume:change", [this](Event const &event) { rerender = true; });
    pluginManager->on("controller:process:end", triggerRender);
    pluginManager->on("controller:process:start", triggerRender);
    pluginManager->on("controller:mode:change", [this](Event const &event) {
        mode = event.getInt("value");
        switch (mode) {
        case MODE_STANDBY:
            changeScreen(SCREEN_ID_STANDBY_SCREEN);
            break;
        case MODE_BREW:
            changeScreen(SCREEN_ID_BREW_SCREEN);
            break;
        case MODE_GRIND:
            changeScreen(SCREEN_ID_GRIND_SCREEN);
            break;
        case MODE_STEAM:
            changeScreen(SCREEN_ID_STEAM_SCREEN);
            break;
        case MODE_WATER:
            changeScreen(SCREEN_ID_WATER_SCREEN);
            break;
        default:
            break;
        };
    });
    pluginManager->on("controller:brew:start", [this](Event const &event) { changeScreen(SCREEN_ID_STATUS_SCREEN); });
    pluginManager->on("controller:brew:clear", [this](Event const &event) {
        if (eez_flow_get_current_screen() == SCREEN_ID_STATUS_SCREEN) {
            changeScreen(SCREEN_ID_BREW_SCREEN);
        }
    });
    pluginManager->on("controller:bluetooth:waiting", [this](Event const &) {
        waitingForController = true;
        rerender = true;
    });
    pluginManager->on("controller:bluetooth:connect", [this](Event const &) {
        waitingForController = false;
        rerender = true;
        initialized = true;
        // Stay on the standby screen when the controller is incompatible so the
        // mismatch message remains visible instead of jumping into brew.
        if (eez_flow_get_current_screen() == SCREEN_ID_STANDBY_SCREEN && !controller->getSystemInfo().protocolMismatch) {
            ::Settings &settings = controller->getSettings();
            if (settings.getStartupMode() == MODE_BREW) {
                changeScreen(SCREEN_ID_BREW_SCREEN);
            } else {
                standbyEnterTime = ::millis();
            }
        }
        pressureAvailable = controller->getSystemInfo().capabilities.pressure;
    });
    pluginManager->on("controller:bluetooth:disconnect", [this](Event const &) {
        waitingForController = true;
        rerender = true;
    });
    pluginManager->on("controller:wifi:connect", [this](Event const &event) {
        rerender = true;
        apActive = event.getInt("AP");
    });
    pluginManager->on("ota:update:start", [this](Event const &event) {
        rerender = true;
        changeScreen(SCREEN_ID_STANDBY_SCREEN);
        // A display update flashes ~4 MB while the RGB peripheral streams the
        // framebuffer from PSRAM; both contend on the S3's shared memory bus
        // and the download starves and aborts. The device reboots right after
        // a display OTA anyway, so stop scan-out for the duration. Handled on
        // the UI task (via flag) so it can't race an in-flight flush.
        if (event.getString("component") != "controller") {
            panelStopRequested = true;
        }
    });
    pluginManager->on("ota:update:end", [this](Event const &) {
        rerender = true;
        changeScreen(SCREEN_ID_STANDBY_SCREEN);
        otaEnded = true;
    });
    pluginManager->on("ota:update:status", [this](Event const &event) {
        rerender = true;
        updateAvailable = event.getInt("value");
    });
    pluginManager->on("bganim:preview", [this](Event const &event) {
        std::lock_guard<std::mutex> guard(previewMutex);
        previewAnim = event.getInt("anim");
        previewStops = event.getString("stops");
        previewDirty = true;
        previewUntil = ::millis() + BGANIM_PREVIEW_HOLD_MS;
    });
    pluginManager->on("bganim:preview-end", [this](Event const &) {
        std::lock_guard<std::mutex> guard(previewMutex);
        previewUntil = 0;
    });
    // Fired from WebUIPlugin's async task after a web save; consumed on the
    // UI task by settingsUI.service() so an open category page reconciles
    // its untouched fields instead of showing a stale draft.
    pluginManager->on("settings:changed", [this](Event const &) { settingsUI.notifyWebSaved(); });
#ifndef GAGGIMATE_SIM
    migrateBgAnimGradients();
#endif
    pluginManager->on("controller:error", [this](Event const &) {
        rerender = true;
        changeScreen(SCREEN_ID_STANDBY_SCREEN);
    });
    pluginManager->on("controller:protocol:mismatch", [this](Event const &) {
        // Incompatible firmware on the other end: control is inhibited (OTA only),
        // so surface it on the standby screen like a runaway error.
        rerender = true;
        changeScreen(SCREEN_ID_STANDBY_SCREEN);
    });
    pluginManager->on("controller:autotune:start", [this](Event const &) { changeScreen(SCREEN_ID_STANDBY_SCREEN); });
    pluginManager->on("controller:autotune:result", [this](Event const &) { changeScreen(SCREEN_ID_STANDBY_SCREEN); });

    pluginManager->on("profiles:profile:select", [this](Event const &event) {
        reloadProfiles();
        rerender = true;
    });
    pluginManager->on("profiles:profile:favorite", [this](Event const &event) { reloadProfiles(); });
    pluginManager->on("profiles:profile:unfavorite", [this](Event const &event) { reloadProfiles(); });
    pluginManager->on("profiles:profile:save", [this](Event const &event) { reloadProfiles(); });
    pluginManager->on("controller:volumetric-measurement:active:change", [this](Event const &event) {
        double newWeight = event.getFloat("value");
        if (round(newWeight * 10.0) != round(activeWeight * 10.0)) {
            activeWeight = newWeight;
            rerender = true;
        }
    });
    // Scale screen feed. The hardware cells report through this event on every
    // valid measurement regardless of controller mode, so the readout stays live
    // even while the overlay forces MODE_GRIND (which routes active:change to the
    // Bluetooth-only grind source and would otherwise freeze it).
    pluginManager->on("controller:volumetric-measurement:hardware:change",
                      [this](Event const &event) { scaleHardwareWeight = event.getFloat("value"); });
    xTaskCreatePinnedToCore(profileLoopTask, "DefaultUI::loopProfiles", configMINIMAL_STACK_SIZE * 4, this, 1, &profileTaskHandle,
                            0);
}

void DefaultUI::loop() {
#ifndef GAGGIMATE_SIM
    // Here as well as in pumpSleepOverlay: a UI pass on a busy screen runs
    // longer than UI_PERIOD_MS, so the task loop takes this branch every
    // time and the pump branch never.
    serviceLayerMoves();
    serviceUiAnimTest();
    if (panelStopRequested && !panelStopped) {
        panelStopped = true;
        stopSleepAnimation();
        if (panelDriver != nullptr) {
            panelDriver->stopPanel();
        }
    }
    if (panelStopped && otaEnded) {
        // Success never reaches here (GitHubOTA restarts the device); a failed
        // display OTA must reboot to bring the panel back.
        delay(250);
        ESP.restart();
    }
#endif
    // Unconditional (not inside the ifndef block above): the simulator has no
    // serviceLayerMoves/serviceUiAnimTest (both drive the panel scan-out
    // path), but /api/debug/touchmap needs serviceTouchMap serviced there
    // too, and the settings shell needs its web-save reconciliation, refresh
    // tick and theme restyle checks there regardless of venue.
    serviceTouchMap();
    settingsUI.service();
#if defined(GM_TOUCH_PROBE) || defined(GAGGIMATE_SIM)
    serviceSettingsUi();
#endif

    const unsigned long now = ::millis();
    const unsigned long diff = now - lastRender;

    if (now - lastTempLog > TEMP_HISTORY_INTERVAL) {
        updateTempHistory();
        lastTempLog = now;
    }

    if ((controller->isActive() && diff > RERENDER_INTERVAL_ACTIVE) || diff > RERENDER_INTERVAL_IDLE) {
        rerender = true;
    }

    // Telemetry-pass spacing (see RERENDER_MIN_INTERVAL): rerender stays
    // pending while held, so no change is ever dropped, only coalesced into
    // the next spaced pass. Within GM_TOUCH_GRACE_US of a touch edge the
    // spacer is bypassed so interaction effects — including CLICK flags that
    // only get applied on the pass AFTER the edge — run immediately.
    // ui_tick() and the maintain calls below still run on a held pass.
    const bool spacerHold =
        rerender && diff < RERENDER_MIN_INTERVAL && esp_timer_get_time() - g_touchEdgeAtUs >= GM_TOUCH_GRACE_US;

    if (rerender && !spacerHold) {
        rerender = false;
        lastRender = now;
        lastRenderUs = esp_timer_get_time();
        applyTheme();
        if (controller->isErrorState()) {
            changeScreen(SCREEN_ID_STANDBY_SCREEN);
        }
        updateTempStableFlag();

        updateState();
        // Fill the EEZ data models before handleScreenChange() creates/ticks a screen (undefined fields abort the flow).
        updateSystemStatus();
        updateProfileInfo();
        updateBoiler();
        updateBrewProcess();
        currentWeight = FloatValue(activeWeight);
        eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SCALE_WEIGHT_CURRENT, currentWeight);

        char timeBuf[12];
        formatDuration(controller->getSettings().getTargetGrindDuration(), timeBuf, sizeof(timeBuf));
        if (stringChanged(grindTimeTarget.getString(), timeBuf)) {
            grindTimeTarget = StringValue(timeBuf);
            eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_GRIND_TIME_TARGET, grindTimeTarget);
        }
        grindWeightTarget = FloatValue(controller->getSettings().getTargetGrindVolume());
        eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_GRIND_WEIGHT_TARGET, grindWeightTarget);

        handleScreenChange();
        currentScreen = static_cast<ScreensEnum>(eez_flow_get_current_screen());
        effect_mgr.evaluate_all();

        if (currentScreen == SCREEN_ID_STANDBY_SCREEN) {
            if (standbyEnterTime > 0) {
                const Settings &settings = controller->getSettings();
                const unsigned long now = millis();
                if (now - standbyEnterTime >= settings.getStandbyBrightnessTimeout()) {
                    setBrightness(settings.getStandbyBrightness());
                }
            }
        }
    }

    // ui_tick() first: it runs the generated tick_screen_*, which is what derives
    // widget visibility from flow state. Running it after the maintain calls
    // meant that on the pass where a screen had just been created, the overlay
    // snapshot captured HIDDEN flags still at their creation defaults, and the
    // real values only reached the panel on the following refresh — a widget
    // visibly settling a frame late, but only the first time a screen was
    // entered, since after that the flags were already correct.
    ui_tick();
    // After ui_tick, which is where the flow writes the dials' values, and
    // before the snapshot below, so an ownership change's invalidation is
    // published in this pass.
    serviceDialElements();
    // After ui_tick so a screen eez_flow_set_screen created this pass exists
    // before the walk runs.
    applyPressedFeedback();
    // Scale overlay before the animation maintenance: maintainSleepAnimation
    // snapshots the LVGL tree into the panel overlay, so the overlay's
    // show/hide state must be final by then, or the pass that enters the
    // grind screen publishes it bare - one refresh of naked grind widgets -
    // before the scale cover is up.
    maintainScaleScreen();
    // Input before the snapshot (gm-qo3.2): lv_task_handler() is where the
    // touch controller is polled, and it used to run after
    // maintainSleepAnimation() took the pass's snapshot, so a tap read here
    // showed up one whole pass later. Polled first, the tap's pressed style
    // is among the dirty rects this pass publishes. The refresh timer is
    // parked while the animation owns the panel, so this renders nothing
    // by itself; when the animation is off it is the same flush pass as
    // before, a few lines earlier.
    lv_task_handler();
    maintainSleepAnimation();
    serviceOverlayTransition();
    publishTouchHitMap();
}

// LVGL's own search (lv_indev_search_obj): a hidden object hides its
// subtree; children are searched when the point is on the object's
// coordinates or the object has overflow visible; an object is hit when it
// is clickable, enabled and the point is inside its click area. Flattened
// here in tree order, so the last containing rectangle is the one LVGL
// would return; a disabled object is left out and its parent answers, as
// in LVGL. The screen itself is not a target.
void DefaultUI::collectHitRects(lv_obj_t *obj, const lv_area_t &clip, lv_obj_t *scr, touchtask::HitRect *out, int &n) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    if (obj != scr && lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE) && !lv_obj_has_state(obj, LV_STATE_DISABLED) &&
        n < touchtask::kMaxHitRects) {
        lv_area_t click;
        lv_obj_get_click_area(obj, &click);
        lv_area_t hit;
        if (_lv_area_intersect(&hit, &click, &clip)) {
            touchtask::HitRect &r = out[n++];
            r.x1 = hit.x1;
            r.y1 = hit.y1;
            r.x2 = hit.x2;
            r.y2 = hit.y2;
            r.px1 = coords.x1;
            r.py1 = coords.y1;
            r.px2 = coords.x2;
            r.py2 = coords.y2;
            // A container the size of the screen is not a target, whatever
            // LVGL says: no plate over a background press (onTouchHit's rule).
            const int w = lv_area_get_width(&coords);
            const int h = lv_area_get_height(&coords);
            r.plate = (w * h > (480 * 480) / 3) ? 0 : 1;
        }
    }
    lv_area_t childClip = clip;
    if (!lv_obj_has_flag(obj, LV_OBJ_FLAG_OVERFLOW_VISIBLE)) {
        if (!_lv_area_intersect(&childClip, &coords, &clip)) {
            return;
        }
    }
    const uint32_t children = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < children; i++) {
        collectHitRects(lv_obj_get_child(obj, i), childClip, scr, out, n);
    }
}

void DefaultUI::publishTouchHitMap() {
#ifndef GAGGIMATE_SIM
    if (!touchtask::running()) {
        return;
    }
    lv_obj_t *scr = lv_scr_act();
    if (scr == nullptr) {
        return;
    }
    // PSRAM, not BSS: 96 rectangles are 1.6 KB, and internal RAM is the
    // WiFi budget.
    static touchtask::HitRect *rects = nullptr;
    if (rects == nullptr) {
        rects = static_cast<touchtask::HitRect *>(ps_malloc(sizeof(touchtask::HitRect) * touchtask::kMaxHitRects));
        if (rects == nullptr) {
            return;
        }
    }
    int n = 0;
    const lv_area_t whole = {-32767, -32767, 32767, 32767};
    collectHitRects(scr, whole, scr, rects, n);
    const lv_color_t dim = lv_color_hex(static_cast<uint32_t>(controller->getSettings().getTouchDimColor()));
    touchtask::publishHitMap(rects, n, pressPlateMode, lv_color_to16(dim), PRESS_PLATE_OUTSET);
#endif
}

void DefaultUI::beginOverlayTransition(const char *why, bool waitSwap) {
#ifndef GAGGIMATE_SIM
    if (!sleepAnimation.isActive() || animHostScreen == nullptr) {
        return; // LVGL flushes the panel itself; there is no composite to fade
    }
    if (overlayTrans == OverlayTrans::FadeOut) {
        overlayTransWaitSwap = overlayTransWaitSwap || waitSwap;
        return;
    }
    overlayTrans = OverlayTrans::FadeOut;
    overlayTransWaitSwap = waitSwap;
    overlayTransHeld = false;
    overlayTransT0Us = esp_timer_get_time();
    overlayTransOutMs = overlayFadeOutMs();
    // The page must come as soon as it is built, not on the spaced pass.
    overlayUrgentUntilUs = overlayTransT0Us + OVERLAY_TRANS_ABANDON_US;
    sleepAnimation.rampOverlayGain(0, overlayTransOutMs);
#ifdef GM_TOUCH_PROBE
    ESP_LOGI("TouchProbe", "GM_TRANS: fade_out_start %s edge+%lld us", why,
             (long long)(overlayTransT0Us - g_touchEdgeAtUs));
#else
    (void)why;
#endif
#else
    (void)why;
    (void)waitSwap;
#endif
}

void DefaultUI::serviceOverlayTransition() {
#ifndef GAGGIMATE_SIM
    if (overlayTrans == OverlayTrans::Idle) {
        return;
    }
    const int64_t nowUs = esp_timer_get_time();
    if (overlayTrans == OverlayTrans::FadeIn) {
        if (sleepAnimation.overlayGainSettled()) {
#ifdef GM_TOUCH_PROBE
            ESP_LOGI("TouchProbe", "GM_TRANS: fade_in_end t0+%lld us", (long long)(nowUs - overlayTransT0Us));
#endif
            overlayTrans = OverlayTrans::Idle;
        }
        return;
    }
    // FadeOut with no page to show: the animation stopped, or the change was
    // cancelled before its page was built. Bring the page back.
    const bool lost = !sleepAnimation.isActive() || animHostScreen == nullptr;
    if (lost || (!overlayTransHeld && nowUs - overlayTransT0Us > OVERLAY_TRANS_ABANDON_US)) {
#ifdef GM_TOUCH_PROBE
        ESP_LOGI("TouchProbe", "GM_TRANS: abandoned %s t0+%lld us", lost ? "lost" : "timeout",
                 (long long)(nowUs - overlayTransT0Us));
#endif
        overlayTransWaitSwap = false;
        overlayTransHeld = false;
        if (lost) {
            sleepAnimation.setOverlayGain(256);
            overlayTrans = OverlayTrans::Idle;
        } else {
            sleepAnimation.rampOverlayGain(256, overlayFadeInMs());
            overlayTrans = OverlayTrans::FadeIn;
        }
    }
#endif
}

// Runs every UI-task pass. Starts/stops the background animation and keeps
// the composited widget snapshot fresh. Two operating modes:
//  - default: animation only during genuine sleep — standby screen, standby
//    mode, controller BLE-connected, and no status overlay (update/error/
//    autotune/protocol mismatch, which render on the plain standby screen).
//  - all-screens (settings.bgAnimAllScreens): animation behind every screen
//    whenever the UI is up and nothing critical is running. OTA/error states
//    still stop it (the OTA download needs every byte of PSRAM bandwidth).
void DefaultUI::maintainSleepAnimation() {
#ifndef GAGGIMATE_SIM
    const bool blocked = controller->isUpdating() || controller->isErrorState() || controller->isAutotuning() ||
                         controller->getSystemInfo().protocolMismatch;
    const bool connected = controller->isLinkUp();
    // getMode() is controller-sourced, so while the link is down its value
    // carries no information -- only consult it once there is a link that could
    // have supplied it. Requiring MODE_STANDBY unconditionally is what left the
    // standby screen blank behind "Waiting for controller...".
    const bool standbyMode = !connected || controller->getMode() == MODE_STANDBY;
    // Not `initialized`, which despite the name only becomes true once a
    // controller has connected -- testing it here is what kept the standby
    // screen blank behind "Waiting for controller...". What the animation
    // actually needs is a built UI and a finished power-up fade, since it
    // suppresses the LVGL flushes the fade is made of.
    const bool uiReady = uiBuiltAt != 0 && ::millis() - uiBuiltAt >= STARTUP_FADE_MS;
    const bool sleepWant = uiReady && currentScreen == SCREEN_ID_STANDBY_SCREEN && standbyMode && !blocked;
    const bool wantAnimation = bgAnimAllScreens ? (uiReady && !blocked) : sleepWant;

#ifdef GM_ANIM_BENCH
    {
        BenchGateState &g = const_cast<BenchGateState &>(bench_gate_state());
        g.uiInitialized = initialized;
        g.blocked = blocked;
        g.wantAnimation = wantAnimation;
        g.animActive = sleepAnimation.isActive();
        g.mode = controller->getMode();
        g.screen = static_cast<int>(currentScreen);
    }
#endif

    if (wantAnimation) {
        if (!sleepAnimation.isActive()) {
            const unsigned long now = ::millis();
            // lastSleepAnimAttempt is only armed after a FAILED start, so a
            // stop/start across a screen change restarts on the next pass.
            if (now - lastSleepAnimAttempt > 2000) {
                startSleepAnimation();
                if (!sleepAnimation.isActive()) {
                    lastSleepAnimAttempt = now;
#ifdef GM_ANIM_BENCH
                    BenchGateState &g = const_cast<BenchGateState &>(bench_gate_state());
                    g.startFailed = true;
                    g.lastStartAttempt = now;
#endif
                }
            }
        } else {
            // A screen change released the old host without stopping the
            // animation; give it the new one. Idempotent on every other pass.
            adoptAnimHost(lv_scr_act());
            // Standby content changes once a minute (clock); active screens
            // update continuously — refresh the snapshot faster there so
            // gauges and numbers stay reasonably live behind the animation.
            const Settings &plateSettings = controller->getSettings();
            applyAnimPlates(plateSettings.getBgAnimClearPlates(), static_cast<uint32_t>(plateSettings.getBgAnimPlateColor()),
                            plateSettings.getBgAnimPlateOpacity());
            // No refresh throttle. refreshSleepOverlay's own early-out makes a
            // pass with no fresh LVGL output cost one comparison, so the only
            // thing an interval here rate-limited was the response to actual
            // changes -- and the standby screen's old 1000 ms interval held a
            // tap's visual feedback for up to a second, which is most of what
            // "the UI feels slow under the animation" was. The snapshot rate
            // stays bounded by the rate at which LVGL actually redraws.
            refreshSleepOverlay();
        }
    } else if (sleepAnimation.isActive()) {
        stopSleepAnimation();
    }
#endif
}

void DefaultUI::pumpSleepOverlay() {
#ifndef GAGGIMATE_SIM
    serviceLayerMoves();
    serviceUiAnimTest();
    if (sleepAnimation.isActive()) {
        refreshSleepOverlay();
    }
#endif
}

static void uiAnimTestSlideCb(void *var, int32_t v) { lv_obj_set_x(static_cast<lv_obj_t *>(var), static_cast<lv_coord_t>(v)); }

void DefaultUI::serviceUiAnimTest() {
    const int req = g_uiAnimTestReq;
    if (req == uiAnimTestMode) {
        // Mode 3 ping-pongs through the layer move: when one leg has landed
        // and been handed back to LVGL, start the other.
        if (req == 3 && uiAnimTestObj != nullptr && !layerMoveInFlight(uiAnimTestObj)) {
            uiAnimTestFwd = !uiAnimTestFwd;
            moveObjectViaLayer(uiAnimTestObj, static_cast<lv_coord_t>(uiAnimTestFwd ? uiAnimTestTravel : -uiAnimTestTravel), 0,
                               1200, SleepAnimation::LayerEase::EaseInOut);
        }
        return;
    }
    if (uiAnimTestObj != nullptr) {
        cancelLayerMove(uiAnimTestObj);
        lv_anim_del(uiAnimTestObj, nullptr);
        lv_obj_del(uiAnimTestObj);
        uiAnimTestObj = nullptr;
    }
    uiAnimTestMode = req;
    if (req == 0) {
        return;
    }
    lv_obj_t *scr = lv_scr_act();
    if (scr == nullptr) {
        return;
    }
    const lv_coord_t size = req == 2 ? 60 : 120;
    lv_obj_t *o = lv_obj_create(scr);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, size, size);
    lv_obj_set_pos(o, 40, (lv_obj_get_height(scr) - size) / 2);
    lv_obj_set_style_radius(o, size / 6, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(0xF4A261), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *l = lv_label_create(o);
    lv_label_set_text(l, req == 2 ? "1" : "9.2");
    lv_obj_set_style_text_color(l, lv_color_hex(0x1B1B1B), 0);
    lv_obj_center(l);
    uiAnimTestObj = o;
    if (req == 3) {
        // The layer path, through moveObjectViaLayer: same travel and
        // duration as mode 1, so the two are directly comparable on the
        // probe (ui_fpsprobe.py: mode 1 shows up as overlay refreshes, mode 3
        // as animation frames with layer_us). Each leg re-snapshots the plate
        // where it landed, which is what a real caller gets too.
        uiAnimTestTravel = lv_obj_get_width(scr) - size - 80;
        uiAnimTestFwd = true;
        if (!moveObjectViaLayer(o, static_cast<lv_coord_t>(uiAnimTestTravel), 0, 1200, SleepAnimation::LayerEase::EaseInOut)) {
            log_w("uianim: layer move refused");
        }
        return;
    }
    if (req == 4) {
        // Parked in a layer at the far end of the travel, for a framebuffer
        // grab that a moving sprite would tear: the plate is placed there
        // through LVGL first, then snapshotted and moved by nothing.
        lv_obj_set_x(o, static_cast<lv_coord_t>(lv_obj_get_width(scr) - size - 40));
        if (!moveObjectViaLayer(o, 0, 0, 600000, SleepAnimation::LayerEase::Linear)) {
            log_w("uianim: layer park refused");
        }
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, uiAnimTestSlideCb);
    lv_anim_set_values(&a, 40, lv_obj_get_width(scr) - size - 40);
    lv_anim_set_time(&a, 1200);
    lv_anim_set_playback_time(&a, 1200);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static const char *touchMapClass(const lv_obj_t *obj) {
    struct Entry {
        const lv_obj_class_t *cls;
        const char *name;
    };
    static const Entry table[] = {
        {&lv_btn_class, "btn"},       {&lv_imgbtn_class, "imgbtn"}, {&lv_img_class, "img"},
        {&lv_label_class, "label"},   {&lv_meter_class, "meter"},   {&lv_switch_class, "switch"},
        {&lv_bar_class, "bar"},       {&lv_arc_class, "arc"},       {&lv_line_class, "line"},
        {&lv_slider_class, "slider"}, {&lv_roller_class, "roller"}, {&lv_dropdown_class, "dropdown"},
        {&lv_checkbox_class, "checkbox"}, {&lv_textarea_class, "textarea"}, {&lv_btnmatrix_class, "btnmatrix"},
        {&lv_obj_class, "obj"},
    };
    for (const Entry &e : table) {
        if (obj->class_p == e.cls) {
            return e.name;
        }
    }
    return "other";
}

// The screen id (1-based index into the EEZ `objects` table) that lv_scr_act()
// currently resolves to, or 0 if it is not one of the top-level screens (should
// not happen: the flow engine only ever activates one of them, and the
// settings shell mounts as a child object rather than a new screen). Used for
// screen_id when the dump was requested as screen=0 (the active screen,
// whichever it is), since the request itself carries no screen id then.
static int touchMapFindScreenId(const lv_obj_t *scr) {
    lv_obj_t **arr = reinterpret_cast<lv_obj_t **>(&objects);
    const size_t nObj = sizeof(objects) / sizeof(lv_obj_t *);
    for (size_t i = 0; i < nObj; i++) {
        if (arr[i] == scr) {
            return static_cast<int>(i) + 1;
        }
    }
    return 0;
}

// Appends `,"key":"<json-escaped src, up to maxBytes source bytes, UTF-8
// safe>"` to buf, advancing len. maxBytes == 0 means no limit (the full
// string). Sets truncated and writes nothing when the escaped field would not
// fit in the remaining capacity, the same contract touchMapNode's own fields
// use, so a caller can grow the buffer and retry the whole dump rather than
// reason about a half-written object line.
static void touchMapAppendString(char *buf, size_t cap, size_t &len, bool &truncated, const char *key, const char *src,
                                  size_t maxBytes) {
    size_t srcLen = strlen(src);
    if (maxBytes != 0 && srcLen > maxBytes) {
        srcLen = maxBytes;
        // Back off if the cut lands inside a multi-byte UTF-8 sequence: a
        // continuation byte is 10xxxxxx (0x80..0xBF).
        while (srcLen > 0 && (static_cast<unsigned char>(src[srcLen]) & 0xC0) == 0x80) {
            srcLen--;
        }
    }
    // Worst case every source byte becomes a two-character escape, plus the
    // key, quotes, colon, comma and the fixed tail margin the caller reserves.
    const size_t need = strlen(key) + srcLen * 2 + 8;
    if (truncated || len + need >= cap) {
        truncated = true;
        return;
    }
    len += snprintf(buf + len, cap - len, ",\"%s\":\"", key);
    for (size_t i = 0; i < srcLen; i++) {
        const unsigned char c = static_cast<unsigned char>(src[i]);
        if (c == '"' || c == '\\') {
            buf[len++] = '\\';
            buf[len++] = static_cast<char>(c);
        } else if (c == '\n') {
            buf[len++] = '\\';
            buf[len++] = 'n';
        } else if (c < 0x20) {
            continue; // rare in UI text; drop rather than \u-escape it
        } else {
            buf[len++] = static_cast<char>(c);
        }
    }
    buf[len++] = '"';
    buf[len] = '\0';
}

// One object per line of the JSON array: its objects[] index (-1 when it is
// not a generated object), class, coords, hidden/clickable/overflow-visible
// flags, ext click pad, event callback count, translate_y and depth, plus (for
// lv_label objects) its text and (for descendants of the open settings shell's
// cover, SettingsUI::cover()) its debug tag. underCover is carried down from
// the parent rather than walked up per object: cheaper for a screen with many
// objects, and the only ancestor that can ever match is the cover itself,
// created once per settings-open as a child of the menu screen.
//
// Writes straight into the shared buffer (no per-object local copy: this runs
// on DefaultUI's UI task, whose stack is sized for LVGL work, not a few KB of
// scratch per recursion level). A field that does not fit sets truncated and
// leaves this object's line unclosed; serviceTouchMap's caller either grows
// the buffer and re-walks the whole tree from scratch (the unclosed line is
// simply overwritten, never published), or, at its size ceiling, closes the
// array after the last object that DID finish -- len only ever advances past
// a fully-written, newline-terminated object, since every write here is
// itself capacity-checked before it happens.
static void touchMapNode(lv_obj_t *obj, int parent, int depth, const lv_obj_t *cover, bool underCover, char *buf, size_t cap,
                         size_t &len, int &nextId, bool &truncated) {
    if (truncated) {
        return; // the dump is being retried (or closed out) at this size; do no more work for it
    }
    const int id = nextId++;
    underCover = underCover || obj == cover;
    int oi = -1;
    lv_obj_t **arr = reinterpret_cast<lv_obj_t **>(&objects);
    const size_t nObj = sizeof(objects) / sizeof(lv_obj_t *);
    for (size_t i = 0; i < nObj; i++) {
        if (arr[i] == obj) {
            oi = static_cast<int>(i);
            break;
        }
    }
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const int ext = obj->spec_attr != nullptr ? obj->spec_attr->ext_click_pad : 0;
    const int ev = obj->spec_attr != nullptr ? obj->spec_attr->event_dsc_cnt : 0;
    const size_t lineStart = len;
    // Reserve enough for the fixed fields below; "t"/"tag"/"val" carry their
    // own reserve in touchMapAppendString, and the tail margin serviceTouchMap
    // holds back from `cap` is never spent here, so "]}\n" always has room.
    constexpr size_t kBaseReserve = 200;
    if (len + kBaseReserve >= cap) {
        truncated = true;
        return;
    }
    len += snprintf(buf + len, cap - len,
                    "%s{\"i\":%d,\"p\":%d,\"o\":%d,\"c\":\"%s\",\"x1\":%d,\"y1\":%d,\"x2\":%d,\"y2\":%d,\"h\":%d,\"k\":%d,"
                    "\"e\":%d,\"n\":%d,\"v\":%d,\"ty\":%d,\"d\":%d",
                    id == 0 ? "" : ",", id, parent, oi, touchMapClass(obj), a.x1, a.y1, a.x2, a.y2,
                    lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) ? 1 : 0, lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE) ? 1 : 0, ext, ev,
                    lv_obj_has_flag(obj, LV_OBJ_FLAG_OVERFLOW_VISIBLE) ? 1 : 0,
                    static_cast<int>(lv_obj_get_style_translate_y(obj, LV_PART_MAIN)), depth);
    if (obj->class_p == &lv_label_class) {
        const char *text = lv_label_get_text(obj);
        if (text != nullptr) {
            touchMapAppendString(buf, cap, len, truncated, "t", text, 48);
        }
    }
    if (underCover) {
        if (const auto *tag = static_cast<const SettingsDebugTag *>(lv_obj_get_user_data(obj))) {
            char rowRole[80];
            snprintf(rowRole, sizeof(rowRole), "%s/%s", tag->row, tag->role);
            touchMapAppendString(buf, cap, len, truncated, "tag", rowRole, 0);
            if (tag->text != nullptr) {
                touchMapAppendString(buf, cap, len, truncated, "val", tag->text, 0);
            }
        }
    }
    if (truncated) {
        len = lineStart; // undo this object's partial write; nothing after it is valid either
        return;
    }
    buf[len++] = '}';
    buf[len++] = '\n';
    buf[len] = '\0';

    const uint32_t n = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < n; i++) {
        touchMapNode(lv_obj_get_child(obj, i), id, depth + 1, cover, underCover, buf, cap, len, nextId, truncated);
    }
}

void DefaultUI::serviceTouchMap() {
    if (!g_touchMapPending) {
        return;
    }
    const int req = g_touchMapReq;
    if (req < 0 || req > _SCREEN_ID_LAST) {
        g_touchMapPending = false;
        return;
    }
    lv_obj_t *scr;
    if (req == 0) {
        // The active screen, dumped immediately: no changeScreen/g_touchMapLoad
        // path (nothing was just switched to) and no settle delay.
        scr = lv_scr_act();
    } else {
        if (g_touchMapLoad) {
            g_touchMapLoad = false;
            changeScreen(static_cast<ScreensEnum>(req));
            // The flow engine swaps the screen later this pass and its tick sets
            // the flow-driven HIDDEN flags on the next; give it a few passes.
            touchMapDumpAt = ::millis() + 400;
            return;
        }
        if (::millis() < touchMapDumpAt) {
            return;
        }
        scr = reinterpret_cast<lv_obj_t **>(&objects)[req - 1];
    }

    static size_t s_cap = 0;
    constexpr size_t kInitialCap = 64 * 1024;
    constexpr size_t kMaxCap = 4 * 1024 * 1024; // PSRAM, not the animation hot slab; a debug endpoint's ceiling, not a budget
    constexpr size_t kTailMargin = 8;           // headroom the retry loop never spends, so "]}\n" always fits
    if (g_touchMapBuf == nullptr) {
        s_cap = kInitialCap;
        g_touchMapBuf = static_cast<char *>(heap_caps_malloc(s_cap, MALLOC_CAP_SPIRAM));
        if (g_touchMapBuf == nullptr) {
            s_cap = 0;
            g_touchMapPending = false;
            return;
        }
    }
    if (scr == nullptr) {
        snprintf(g_touchMapBuf, s_cap, "{\"screen\":%d,\"error\":\"screen not created\"}", req);
        g_touchMapLen = static_cast<uint32_t>(strlen(g_touchMapBuf));
        g_touchMapPending = false;
        return;
    }

    lv_obj_update_layout(scr);
    const lv_obj_t *cover = settingsUI.isOpen() ? settingsUI.cover() : nullptr;
    const int screenId = req != 0 ? req : touchMapFindScreenId(scr);
    static uint32_t s_seq = 0;
    const uint32_t seq = s_seq + 1;

    // Grows g_touchMapBuf and re-walks the whole tree when it does not fit
    // (touchMapNode/touchMapAppendString's truncated flag): a screen has, at
    // most, a few hundred objects, so re-walking on a miss costs far less
    // than sizing up front for a worst case that almost never happens.
    size_t len = 0;
    for (;;) {
        len = 0;
        bool truncated = false;
        len += snprintf(g_touchMapBuf, s_cap - kTailMargin,
                        "{\"screen\":%d,\"screen_id\":%d,\"active\":%d,\"uptime_ms\":%lu,\"seq\":%u,\"objects\":[\n", req,
                        screenId, lv_scr_act() == scr ? 1 : 0, static_cast<unsigned long>(::millis()), seq);
        int nextId = 0;
        touchMapNode(scr, -1, 0, cover, false, g_touchMapBuf, s_cap - kTailMargin, len, nextId, truncated);
        if (!truncated || s_cap >= kMaxCap) {
            len += snprintf(g_touchMapBuf + len, s_cap - len, "]}\n");
            break;
        }
        const size_t nextCap = s_cap * 2;
        char *grown = static_cast<char *>(heap_caps_realloc(g_touchMapBuf, nextCap, MALLOC_CAP_SPIRAM));
        if (grown == nullptr) {
            // Out of PSRAM for this: close out whatever fit at the current
            // size rather than serve nothing.
            len += snprintf(g_touchMapBuf + len, s_cap - len, "]}\n");
            break;
        }
        g_touchMapBuf = grown;
        s_cap = nextCap;
    }
    s_seq = seq;
    g_touchMapLen = static_cast<uint32_t>(len);
    g_touchMapPending = false;
}

#if defined(GM_TOUCH_PROBE) || defined(GAGGIMATE_SIM)
namespace {

portMUX_TYPE g_settingsUiMux = portMUX_INITIALIZER_UNLOCKED;

// One pending command, written by the web/async task under g_settingsUiMux
// and consumed by DefaultUI::serviceSettingsUi on the UI task. pending
// stays true for an Open command's whole multi-pass stage, not just until
// it is first looked at, so a second request arriving mid-stage still sees
// a command in flight and is refused with 409.
struct SettingsUiSlot {
    bool pending = false;
    DefaultUI::SettingsUiCmd cmd = DefaultUI::SettingsUiCmd::Open;
    int arg = 0;
    uint32_t seq = 0;
};
SettingsUiSlot g_settingsUiSlot;
uint32_t g_settingsUiSeqCounter = 0;
DefaultUI::SettingsUiState g_settingsUiPublished;

// Open staging: changeScreen() only queues a screen change (targetScreen +
// rerender=true); the actual eez_flow_set_screen() runs inside
// handleScreenChange(), which only executes on a rerender pass, so this
// polls the live current screen (eez_flow_get_current_screen(), not the
// currentScreen member, which is stale between rerender passes) across
// as many DefaultUI::loop() calls as it takes. WaitMenu: the request is
// issued, or re-issued every check while the screen has not arrived yet
// (one or more MODE_STANDBY broadcasts race a boot-time request and can
// send it back to standby more than once). Settling: the menu screen was
// seen current on a previous check; wait kOpenSettleMs before touching it,
// so it has been through at least one ui_tick()/lv_task_handler() pass
// (acting on a screen in the same pass it became current crashed the
// simulator intermittently: see tools/settings_ui_tests/rig.py's Sim class
// for the same hazard on a screen-changing touchmap request), then
// re-verify it is still current (a late bounce drops this back to
// WaitMenu instead of opening on a screen that is no longer active).
enum class OpenStage : uint8_t { Idle, WaitMenu, Settling };
OpenStage g_openStage = OpenStage::Idle;
unsigned long g_openSettleAt = 0;
constexpr unsigned long kOpenSettleMs = 60;

const char *settingsUiCmdName(DefaultUI::SettingsUiCmd cmd) {
    switch (cmd) {
    case DefaultUI::SettingsUiCmd::Open:
        return "open";
    case DefaultUI::SettingsUiCmd::Close:
        return "close";
    case DefaultUI::SettingsUiCmd::Cat:
        return "cat";
    case DefaultUI::SettingsUiCmd::Page:
        return "page";
    case DefaultUI::SettingsUiCmd::Pop:
        return "pop";
    }
    return "?";
}

} // namespace

bool DefaultUI::queueSettingsUiCommand(SettingsUiCmd cmd, int arg, uint32_t &seqOut) {
    bool queued = false;
    portENTER_CRITICAL(&g_settingsUiMux);
    if (!g_settingsUiSlot.pending) {
        g_settingsUiSlot.pending = true;
        g_settingsUiSlot.cmd = cmd;
        g_settingsUiSlot.arg = arg;
        g_settingsUiSlot.seq = ++g_settingsUiSeqCounter;
        seqOut = g_settingsUiSlot.seq;
        queued = true;
    }
    portEXIT_CRITICAL(&g_settingsUiMux);
    if (queued) {
        markDirty(); // wake a pass promptly instead of waiting out RERENDER_INTERVAL_IDLE
    }
    return queued;
}

void DefaultUI::settingsUiState(SettingsUiState &out) const {
    portENTER_CRITICAL(&g_settingsUiMux);
    out = g_settingsUiPublished;
    portEXIT_CRITICAL(&g_settingsUiMux);
}

void DefaultUI::serviceSettingsUi() {
    bool pending;
    SettingsUiCmd cmd;
    int arg;
    uint32_t seq;
    portENTER_CRITICAL(&g_settingsUiMux);
    pending = g_settingsUiSlot.pending;
    cmd = g_settingsUiSlot.cmd;
    arg = g_settingsUiSlot.arg;
    seq = g_settingsUiSlot.seq;
    portEXIT_CRITICAL(&g_settingsUiMux);
    if (!pending) {
        // Taps, the exit chevron and external leaves move the shell without
        // a command, so the state is republished whenever it differs from
        // the last publication; seq stays that of the last completed
        // command.
        publishSettingsUiState(g_settingsUiPublished.seq, /*force=*/false);
        return;
    }

    bool done = true;
    // Navigation commands need the cover: openCategory on a closed shell
    // would build the page under a null parent (a stray LVGL screen) and
    // push a ctx that nothing can pop.
    const bool navigation = cmd == SettingsUiCmd::Cat || cmd == SettingsUiCmd::Page || cmd == SettingsUiCmd::Pop;
    if (navigation && !settingsUI.isOpen()) {
        ESP_LOGW("SettingsUI", "SettingsDbg: %s ignored, settings closed", settingsUiCmdName(cmd));
        publishSettingsUiState(seq, /*force=*/true);
        portENTER_CRITICAL(&g_settingsUiMux);
        g_settingsUiSlot.pending = false;
        portEXIT_CRITICAL(&g_settingsUiMux);
        return;
    }
    switch (cmd) {
    case SettingsUiCmd::Open:
        if (settingsUI.isOpen()) {
            // Already open: a second ?open=1 in a row is a no-op, not an
            // error (the shared contract's one-cover guarantee holds either
            // way since open() itself no-ops while already open).
            g_openStage = OpenStage::Idle;
            break;
        }
        if (eez_flow_get_current_screen() != SCREEN_ID_MENU_SCREEN_NEW) {
            changeScreen(SCREEN_ID_MENU_SCREEN_NEW);
            g_openStage = OpenStage::WaitMenu;
            done = false;
            break;
        }
        if (g_openStage != OpenStage::Settling) {
            g_openStage = OpenStage::Settling;
            g_openSettleAt = ::millis() + kOpenSettleMs;
            done = false;
            break;
        }
        if (::millis() < g_openSettleAt) {
            done = false;
            break;
        }
        if (eez_flow_get_current_screen() != SCREEN_ID_MENU_SCREEN_NEW) {
            g_openStage = OpenStage::WaitMenu; // bounced back; re-request
            done = false;
            break;
        }
        openSettings();
        g_openStage = OpenStage::Idle;
        break;
    case SettingsUiCmd::Close:
        closeSettings();
        break;
    case SettingsUiCmd::Cat:
        // From inside a category, pop back to the tiles first (committing
        // each level), then push the requested one; from the tile page
        // this loop does nothing and openCategory pushes directly.
        while (settingsUI.state().depth > 0) {
            settingsUI.popPage();
        }
        settingsUI.openCategory(arg);
        break;
    case SettingsUiCmd::Page:
        settingsUI.gotoPage(arg);
        break;
    case SettingsUiCmd::Pop:
        // One chevron: pops a category page, closes from the tile page.
        if (settingsUI.state().depth > 0) {
            settingsUI.popPage();
        } else {
            closeSettings();
        }
        break;
    }

    if (!done) {
        markDirty(); // keep a staged Open moving without waiting on the idle interval
        return;
    }

    const SettingsUI::State st = publishSettingsUiState(seq, /*force=*/true);
    portENTER_CRITICAL(&g_settingsUiMux);
    g_settingsUiSlot.pending = false;
    portEXIT_CRITICAL(&g_settingsUiMux);

    ESP_LOGI("SettingsUI", "SettingsDbg: %s -> depth=%d category=%d page=%d", settingsUiCmdName(cmd), st.depth,
             st.category, st.page);
}

// Copies the shell's state and the Fixture counters into the published
// struct the web task reads. Without force, only writes when something other
// than seq changed, so the per-pass call costs a compare, not a critical
// section, while the shell is idle.
SettingsUI::State DefaultUI::publishSettingsUiState(uint32_t seq, bool force) {
    const SettingsUI::State st = settingsUI.state();
    const SettingsUI::FixtureCounters fc = settingsUI.fixtureCounters();
    SettingsUiState pub;
    memset(&pub, 0, sizeof(pub)); // padding included, so the memcmp below is meaningful
    pub.seq = seq;
    pub.open = st.open;
    pub.depth = st.depth;
    pub.category = st.category;
    pub.page = st.page;
    pub.pages = st.pages;
    snprintf(pub.title, sizeof(pub.title), "%s", st.title);
    pub.fixtureEnter = fc.enter;
    pub.fixtureCommit = fc.commit;
    pub.fixtureDraft = fc.draft;
    pub.fixtureAction = fc.action;
    pub.fixtureConfirm = fc.confirm;
    pub.fixtureLocked = fc.locked;
    pub.fixtureRepeats = fc.repeats;
    pub.fixtureFastRepeats = fc.fastRepeats;

    // Only the UI task writes g_settingsUiPublished, so reading it here
    // without the lock is safe; the lock orders the write against the web
    // task's copy.
    if (!force && memcmp(&pub, &g_settingsUiPublished, sizeof(pub)) == 0) {
        return st;
    }
    portENTER_CRITICAL(&g_settingsUiMux);
    g_settingsUiPublished = pub;
    portEXIT_CRITICAL(&g_settingsUiMux);
    return st;
}
#endif // GM_TOUCH_PROBE || GAGGIMATE_SIM

bool DefaultUI::snapshotObjectToBuffer(lv_obj_t *obj, uint8_t *buf, uint32_t bufSize, lv_area_t *outArea) {
#ifndef GAGGIMATE_SIM
    if (obj == nullptr || buf == nullptr) {
        return false;
    }
    const lv_coord_t ext = _lv_obj_get_ext_draw_size(obj);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    lv_area_increase(&area, ext, ext);
    const int w = lv_area_get_width(&area);
    const int h = lv_area_get_height(&area);
    if (w <= 0 || h <= 0 || static_cast<uint32_t>(w) * h * 3 > bufSize) {
        return false;
    }
    memset(buf, 0, static_cast<size_t>(w) * h * 3);

    lv_disp_t *objDisp = lv_obj_get_disp(obj);
    lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.hor_res = lv_disp_get_hor_res(objDisp);
    driver.ver_res = lv_disp_get_ver_res(objDisp);
    lv_disp_drv_use_generic_set_px_cb(&driver, LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_disp_t fakeDisp;
    lv_memset_00(&fakeDisp, sizeof(lv_disp_t));
    fakeDisp.driver = &driver;
    lv_draw_ctx_t *drawCtx = static_cast<lv_draw_ctx_t *>(lv_mem_alloc(objDisp->driver->draw_ctx_size));
    if (drawCtx == nullptr) {
        return false;
    }
    objDisp->driver->draw_ctx_init(fakeDisp.driver, drawCtx);
    fakeDisp.driver->draw_ctx = drawCtx;
    drawCtx->clip_area = &area;
    drawCtx->buf_area = &area;
    drawCtx->buf = static_cast<void *>(buf);
    driver.draw_ctx = drawCtx;
    lv_disp_t *refrOri = _lv_refr_get_disp_refreshing();
    _lv_refr_set_disp_refreshing(&fakeDisp);
    lv_obj_redraw(drawCtx, obj);
    _lv_refr_set_disp_refreshing(refrOri);
    objDisp->driver->draw_ctx_deinit(fakeDisp.driver, drawCtx);
    lv_mem_free(drawCtx);
    if (outArea != nullptr) {
        *outArea = area;
    }
    return true;
#else
    return false;
#endif
}

bool DefaultUI::moveObjectViaLayer(lv_obj_t *obj, lv_coord_t dx, lv_coord_t dy, uint32_t durMs,
                                   SleepAnimation::LayerEase ease) {
#ifndef GAGGIMATE_SIM
    if (obj == nullptr || !sleepAnimation.isActive() || layerMoveInFlight(obj)) {
        return false;
    }
    LayerMove *slot = nullptr;
    for (LayerMove &m : layerMoves) {
        if (m.obj == nullptr) {
            slot = &m;
            break;
        }
    }
    if (slot == nullptr) {
        return false;
    }
    lv_obj_update_layout(obj);
    const lv_coord_t ext = _lv_obj_get_ext_draw_size(obj);
    const int lw = lv_obj_get_width(obj) + ext * 2;
    const int lh = lv_obj_get_height(obj) + ext * 2;
    const int id = sleepAnimation.layerAcquire(lw, lh);
    if (id < 0) {
        return false;
    }
    lv_area_t area;
    if (!snapshotObjectToBuffer(obj, sleepAnimation.layerBuffer(id), static_cast<uint32_t>(lw) * lh * 3, &area)) {
        sleepAnimation.layerRelease(id);
        return false;
    }
    sleepAnimation.layerPublish(id, area.x1, area.y1);
    // Atomic handover (gm-2cl.8): the layer is gated on the next overlay
    // publish, which is the one that no longer holds the object (the hide
    // below invalidates it and this task is the only publisher), so the
    // first frame that draws the layer is the first frame without the
    // object. Its motion clock starts in that frame. The refresh goes out
    // unthrottled.
    sleepAnimation.layerShowAtGen(id, sleepAnimation.overlayPublishGen() + 1);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    overlayUrgentUntilUs = esp_timer_get_time() + GM_TOUCH_GRACE_US;
    sleepAnimation.layerAnimate(id, area.x1 + dx, area.y1 + dy, durMs, ease);
    slot->obj = obj;
    slot->layer = id;
    slot->dx = dx;
    slot->dy = dy;
    slot->landed = false;
    slot->hideGen = 0;
    slot->landedAtUs = 0;
    return true;
#else
    return false;
#endif
}

bool DefaultUI::layerMoveInFlight(const lv_obj_t *obj) const {
    for (const LayerMove &m : layerMoves) {
        if (m.obj != nullptr && m.obj == obj) {
            return true;
        }
    }
    return false;
}

void DefaultUI::serviceLayerMoves() {
#ifndef GAGGIMATE_SIM
    for (LayerMove &m : layerMoves) {
        if (m.obj == nullptr) {
            continue;
        }
        if (!m.landed) {
            if (sleepAnimation.layerAnimating(m.layer)) {
                continue;
            }
            lv_obj_set_pos(m.obj, static_cast<lv_coord_t>(lv_obj_get_style_x(m.obj, LV_PART_MAIN) + m.dx),
                           static_cast<lv_coord_t>(lv_obj_get_style_y(m.obj, LV_PART_MAIN) + m.dy));
            lv_obj_clear_flag(m.obj, LV_OBJ_FLAG_HIDDEN);
            overlayUrgentUntilUs = esp_timer_get_time() + GM_TOUCH_GRACE_US;
            // The layer leaves in the frame that first composites the
            // publish carrying the object again: the same latch as the
            // start, the other way round, so no frame shows both or neither.
            m.hideGen = sleepAnimation.overlayPublishGen() + 1;
            sleepAnimation.layerHideAtGen(m.layer, m.hideGen);
            m.landed = true;
            m.landedAtUs = esp_timer_get_time();
            continue;
        }
        // Once that publish is out, every frame from here on hides the layer
        // by the latch, so the slot can be freed. The one-second fallback
        // covers an overlay that stops publishing (animation stopped, screen
        // gone): the object is visible through LVGL either way.
        if (sleepAnimation.overlayPublishGen() >= m.hideGen || esp_timer_get_time() - m.landedAtUs > 1000000) {
            sleepAnimation.layerRelease(m.layer);
            m = LayerMove{};
        }
    }
#endif
}

void DefaultUI::cancelLayerMove(const lv_obj_t *obj) {
#ifndef GAGGIMATE_SIM
    for (LayerMove &m : layerMoves) {
        if (m.obj == nullptr || m.obj != obj) {
            continue;
        }
        if (!m.landed) {
            lv_obj_set_pos(m.obj, static_cast<lv_coord_t>(lv_obj_get_style_x(m.obj, LV_PART_MAIN) + m.dx),
                           static_cast<lv_coord_t>(lv_obj_get_style_y(m.obj, LV_PART_MAIN) + m.dy));
            lv_obj_clear_flag(m.obj, LV_OBJ_FLAG_HIDDEN);
        }
        sleepAnimation.layerRelease(m.layer);
        m = LayerMove{};
    }
#endif
}

void DefaultUI::cancelLayerMoves() {
    for (LayerMove &m : layerMoves) {
        if (m.obj != nullptr) {
            cancelLayerMove(m.obj);
        }
    }
}

void DefaultUI::loopProfiles() {
    if (!profileLoaded) {
        // Build into locals and swap under the lock — the UI task reads these concurrently (GM-147).
        const auto favoritedIds = profileManager->getFavoritedProfiles();
        std::vector<String> ids;
        ids.reserve(favoritedIds.size() + 1);
        ids.emplace_back(controller->getSettings().getSelectedProfile());
        for (const auto &id : favoritedIds) {
            if (std::find(ids.begin(), ids.end(), id) == ids.end())
                ids.emplace_back(id);
        }
        std::vector<Profile> profiles;
        profiles.reserve(ids.size());
        for (const auto &profileId : ids) {
            Profile profile{};
            profileManager->loadProfile(profileId, profile);
            profiles.emplace_back(std::move(profile));
        }
        {
            std::lock_guard<std::mutex> guard(profilesMutex);
            favoritedProfileIds = std::move(ids);
            favoritedProfiles = std::move(profiles);
        }
        profileLoaded = 1;
    }
}

void DefaultUI::changeScreen(ScreensEnum screen) {
    if (screen != targetScreen && screen != currentScreen) {
        // At the request, not at the swap: the swap waits for the next
        // rerender pass, and the fade is the response the finger sees.
        beginOverlayTransition("screen", true);
    }
    targetScreen = screen;
    brewScreenState = BrewScreenState::Brew;
    rerender = true;
    // Reset some submenus
}

void DefaultUI::changeBrewScreenMode(BrewScreenState state) {
    brewScreenState = state;
    rerender = true;
}

void DefaultUI::onProfileSwitch() {
    currentProfileIdx = 0;
    changeScreen(SCREEN_ID_PROFILE_SCREEN);
}

void DefaultUI::onNextProfile() {
    std::lock_guard<std::mutex> guard(profilesMutex);
    if (currentProfileIdx + 1 < static_cast<int>(favoritedProfileIds.size())) {
        currentProfileIdx++;
    }
    rerender = true;
}

void DefaultUI::onPreviousProfile() {
    if (currentProfileIdx > 0) {
        currentProfileIdx--;
    }
    rerender = true;
}

void DefaultUI::onProfileSelect() {
    String id;
    {
        std::lock_guard<std::mutex> guard(profilesMutex);
        if (currentProfileIdx >= 0 && currentProfileIdx < static_cast<int>(favoritedProfileIds.size())) {
            id = favoritedProfileIds[currentProfileIdx];
        }
    }
    if (!id.isEmpty()) {
        profileManager->selectProfile(id);
    }
    profileDirty = false;
    changeScreen(SCREEN_ID_BREW_SCREEN);
}

void DefaultUI::onVolumetricDelete() {
    controller->onVolumetricDelete();
    profileDirty = true;
}

void DefaultUI::setupPanel() {
    ui_init();
    setupState();
    applyTheme();
    ui_tick();

    // Polished power-up: ui_init() makes standby active instantly, so stage a black screen and
    // fade standby in over it (lv_scr_load_anim no-ops when the target is already the active screen).
    lv_obj_t *standby = lv_scr_act();
    lv_obj_t *black = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(black, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(black, LV_OPA_COVER, LV_PART_MAIN);
    lv_scr_load(black);
    lv_scr_load_anim(standby, LV_SCR_LOAD_ANIM_FADE_ON, STARTUP_FADE_MS, 0, true);

    lv_task_handler();

    delay(100);
    // Set initial brightness based on settings
    const ::Settings &settings = controller->getSettings();
    setBrightness(settings.getMainBrightness());
    uiBuiltAt = ::millis();
}

void DefaultUI::setupState() {
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SCALE_WEIGHT_CURRENT, currentWeight);
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_GRIND_WEIGHT_TARGET, grindWeightTarget);
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_GRIND_TIME_TARGET, grindTimeTarget);

    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SYSTEM, systemStatus);
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PREVIEW_PROFILE, previewProfileInfo);
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SELECTED_PROFILE, selectedProfileInfo);
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_BOILER, boiler);
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_UI_FLAGS, uiFlags);
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_BREW_PROCESS_INFO, brewProcess);

    updateState();
    updateSystemStatus();
    updateProfileInfo();
    updateBoiler();
    updateBrewProcess();

    effect_mgr.use_effect([this]() { return currentScreen == SCREEN_ID_INFO_SCREEN; },
                          [this]() {
                              String content = "";
                              if (apActive) {
                                  // WIFI: QR syntax — escape \ ; , : " in the password per the spec.
                                  const String pw = controller->getSettings().getWifiApPassword();
                                  String escaped;
                                  escaped.reserve(pw.length() + 4);
                                  for (size_t i = 0; i < pw.length(); i++) {
                                      const char c = pw.charAt(i);
                                      if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') {
                                          escaped += '\\';
                                      }
                                      escaped += c;
                                  }
                                  if (escaped.isEmpty()) {
                                      content = "WIFI:S:GaggiMate;;;;";
                                  } else {
                                      content = "WIFI:S:GaggiMate;T:WPA;P:" + escaped + ";;";
                                  }
                              } else if (wifiConnected) {
                                  content = "http://" + WiFi.localIP().toString() + "/";
                              }
                              if (content == "") {
                                  return;
                              }
                              const char *data = content.c_str();
                              lv_qrcode_update(objects.qrcode, data, strlen(data));
                          },
                          &wifiConnected, &apActive);
    effect_mgr.use_effect([this]() { return currentScreen == SCREEN_ID_MENU_SCREEN_NEW; },
                          [this]() {
                              const bool fourTiles = grindAvailable || scaleMenuSwap;
                              // The ring is the one the menu had before the settings work
                              // (8f77807e): the mode tiles evenly spread, four at 45 degree
                              // offsets or three at 120 degree steps, brew first, clockwise
                              // from the top. The settings tile sits at the centre (gm-z7x)
                              // where the info button and the mode word used to be; the
                              // info screen is reached from the Status category. The gear
                              // gives up its click pad and the ring tiles keep a smaller
                              // one, so with 100 px ring tiles at radius 140 the hit boxes
                              // stay clear of each other (rig.py audit, 2026-09-06): the
                              // 80 px icons look the same at any tile size.
                              constexpr int kRingTile = 100;
                              constexpr int kRingPad = 8;
                              constexpr int kCentreTile = 80;
                              constexpr int kRadius = 140;
                              lv_obj_t *ring[] = {objects.btn_brew_1, objects.btn_steam_1, objects.btn_water_1,
                                                  objects.btn_grind_1};
                              for (lv_obj_t *tile : ring) {
                                  if (tile != nullptr) {
                                      lv_obj_set_size(tile, kRingTile, kRingTile);
                                      lv_obj_set_ext_click_area(tile, kRingPad);
                                  }
                              }
                              if (objects.info_btn != nullptr) {
                                  lv_obj_add_flag(objects.info_btn, LV_OBJ_FLAG_HIDDEN);
                              }
                              if (objects.obj13 != nullptr) { // the BREW/STEAM/WATER word
                                  lv_obj_add_flag(objects.obj13, LV_OBJ_FLAG_HIDDEN);
                              }
                              if (objects.btn_settings_1 != nullptr) {
                                  lv_obj_set_size(objects.btn_settings_1, kCentreTile, kCentreTile);
                                  lv_obj_set_pos(objects.btn_settings_1, 0, 0);
                                  lv_obj_set_ext_click_area(objects.btn_settings_1, 0);
                              }
                              const int count = fourTiles ? 4 : 3;
                              const int step = 360 / count;
                              const int rotation = fourTiles ? 45 : 0;
                              positionMenuIcon(objects.btn_brew_1, step * 0 - rotation, kRadius);
                              positionMenuIcon(objects.btn_steam_1, step * 1 - rotation, kRadius);
                              positionMenuIcon(objects.btn_water_1, step * 2 - rotation, kRadius);
                              if (fourTiles) {
                                  positionMenuIcon(objects.btn_grind_1, step * 3 - rotation, kRadius);
                              }
                              // Grind slot doubles as the Scale button.
                              if (objects.btn_grind_1 != nullptr) {
                                  lv_obj_set_style_bg_img_src(objects.btn_grind_1,
                                                              scaleMenuSwap ? &img_scale_80x80 : &img_coffee_bean_80x80,
                                                              LV_PART_MAIN | LV_STATE_DEFAULT);
                              }
                          },
                          &grindAvailable, &scaleMenuSwap);
}

void DefaultUI::handleScreenChange() {
    if (currentScreen != targetScreen) {
        // While an animated screen load is in progress (only the power-up
        // fade in setupPanel), lv_scr_load_anim for another screen deletes
        // the screen it is fading in: LVGL 8.4 honours auto_del by deleting
        // lv_scr_act(), which during the animation is already the incoming
        // screen, and then dereferences it. Keep targetScreen pending until
        // the fade has finished; scr_anim_ready clears the field.
        if (lv_disp_get_default()->scr_to_load != nullptr) {
            return;
        }
        // The standby timeout, a mode change, a brew start or any other route
        // that lands here must never lose a pending settings edit or leave
        // the cover behind on a screen the flow engine is about to tear down.
        if (settingsUI.isOpen()) {
            settingsUI.onExternalLeave();
        }
        if (targetScreen == SCREEN_ID_STANDBY_SCREEN) {
            standbyEnterTime = ::millis();
        } else if (currentScreen == SCREEN_ID_STANDBY_SCREEN) {
            const ::Settings &settings = controller->getSettings();
            setBrightness(settings.getMainBrightness());
        }
        // Any screen change while animating has to release the old host screen,
        // or its transparent-bg override leaks onto a screen that is no longer
        // being drawn over plasma.
        //
        // Releasing is all it takes though. This used to stop the animation
        // outright and let the next maintain pass start it again, which meant
        // every navigation tore the background down and built it back up: the
        // render task exited, the framebuffers changed hands twice, and the
        // plasma visibly restarted. In all-screens mode that is the common
        // case, not the rare one, and it read as jank. Nothing about the
        // animation is per-screen except this one style property; the plate
        // table and the status icons it also rewrites are fixed global objects
        // that span every screen. So when the animation is going to keep
        // running anyway, hand it the new host instead of restarting it.
        // maintainSleepAnimation does the adopting, which runs later in this
        // same pass, after ui_tick has actually swapped the screen.
        // Routes that set targetScreen without changeScreen still fade;
        // one that came through it only clears the swap hold here. Before
        // the host is released: with no host there is no composite to fade.
        beginOverlayTransition("swap", false);
        overlayTransWaitSwap = false;
        if (bgAnimAllScreens && sleepAnimation.isActive()) {
            releaseAnimHost();
        } else {
            stopSleepAnimation();
        }
        cancelLayerMoves();
        releaseDialElements();
        eez_flow_set_screen(targetScreen, LV_SCR_LOAD_ANIM_NONE, 0, 0);
        animateGaugeTicks(currentScreen, targetScreen);
        // The flow engine may delete and later recreate the screen this
        // leaves; a recreated screen can land on the old lv_obj address, so
        // an address compare alone would skip restyling it.
        pressedStyledRoot = nullptr;
        rerender = true;
    }
}

// See the declaration for why this exists and why it darkens colors instead
// of changing background opacity. Local per-object props (not one shared
// style) because the pressed look derives from each widget's OWN rest
// colors — a flat black-60 recolor read as "almost too much" dimming on
// theme-tinted icons and lost their hue entirely. Setting a local prop
// twice just overwrites it, so the walk is idempotent, and the rest colors
// are re-read on every walk, which is what makes the theme-change rewalk
// (applyTheme clears the root) pick up new hues.
static void applyPressedRecurse(lv_obj_t *obj, lv_color_t dim, bool clear = false) {
    const bool isImgBtn = lv_obj_check_type(obj, &lv_imgbtn_class);
    if (clear) {
        // The compositor's plate is the feedback: take the local PRESSED
        // props back so a press does not dim the target twice.
        lv_obj_remove_local_style_prop(obj, LV_STYLE_IMG_RECOLOR, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_remove_local_style_prop(obj, LV_STYLE_IMG_RECOLOR_OPA, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_remove_local_style_prop(obj, LV_STYLE_BG_COLOR, LV_PART_MAIN | LV_STATE_PRESSED);
    } else if (isImgBtn || (lv_obj_check_type(obj, &lv_img_class) && lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE))) {
        // Resolved for the unpressed state this walk runs in. Icons the theme
        // fully recolors (opa 255, most of them) shift 40% toward the
        // configured dim color from their own rest color; raw bitmaps get
        // the dim color at 40% directly.
        const lv_color_t rest = lv_obj_get_style_img_recolor(obj, LV_PART_MAIN);
        const lv_opa_t restOpa = lv_obj_get_style_img_recolor_opa(obj, LV_PART_MAIN);
        lv_color_t pressed = dim;
        lv_opa_t pressedOpa = LV_OPA_40;
        if (restOpa > LV_OPA_50) {
            pressed = lv_color_mix(dim, rest, LV_OPA_40);
            pressedOpa = restOpa;
        }
        lv_obj_set_style_img_recolor(obj, pressed, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_img_recolor_opa(obj, pressedOpa, LV_PART_MAIN | LV_STATE_PRESSED);
    } else if (lv_obj_check_type(obj, &lv_btn_class)) {
        // The generated buttons set their bg locally in the DEFAULT state,
        // which outranks the theme's pressed styles — so give them a local
        // pressed bg too. Buttons without a visible bg are left alone rather
        // than given one: adding opacity on press would change the snapshot's
        // alpha coverage and re-trigger the scrim rebuild.
        if (lv_obj_get_style_bg_opa(obj, LV_PART_MAIN) >= LV_OPA_20) {
            const lv_color_t bg = lv_obj_get_style_bg_color(obj, LV_PART_MAIN);
            lv_obj_set_style_bg_color(obj, lv_color_mix(dim, bg, LV_OPA_40), LV_PART_MAIN | LV_STATE_PRESSED);
        }
    }
    const uint32_t n = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < n; i++) {
        applyPressedRecurse(lv_obj_get_child(obj, i), dim, clear);
    }
}

void DefaultUI::applyPressedFeedback() {
    updatePressPlateMode();
    lv_obj_t *scr = lv_scr_act();
    const int dim = controller->getSettings().getTouchDimColor();
    if (scr == nullptr || (scr == pressedStyledRoot && dim == appliedDimColor)) {
        return;
    }
    appliedDimColor = dim;
    applyPressedRecurse(scr, lv_color_hex(static_cast<uint32_t>(dim)), pressPlateMode);
    pressedStyledRoot = scr;
}

void DefaultUI::applyPressedFeedbackTo(lv_obj_t *root) {
    if (root == nullptr || pressPlateMode) {
        return;
    }
    const int dim = controller->getSettings().getTouchDimColor();
    applyPressedRecurse(root, lv_color_hex(static_cast<uint32_t>(dim)));
}

void DefaultUI::startSleepAnimation() {
#ifndef GAGGIMATE_SIM
    Display *display = panelDriver != nullptr ? panelDriver->getDisplay() : nullptr;
    lv_obj_t *host = lv_scr_act();
    if (display == nullptr || host == nullptr) {
        return;
    }
    // Before start(), not after: LVGL renders into the panel's framebuffers, and
    // start() spawns a task that begins writing them. Suppressing first is what
    // moves LVGL onto its scratch buffer, so the two never hold the same memory
    // at once. Put back if the animation declines to run.
    lvgl_helper_suppress_flush(true);
    sleepAnimation.start(display);
    if (!sleepAnimation.isActive()) {
        lvgl_helper_suppress_flush(false);
        return;
    }
    // The status icons carry a 10 px border in the theme background color (an
    // EEZ spacing trick, invisible on black) — over the plasma it snapshots as
    // an opaque plate around each icon. Hide the borders while animating.
    for (lv_obj_t *icon : {objects.wifi_icon, objects.bluetooth_icon, objects.update_icon}) {
        if (icon != nullptr) {
            lv_obj_set_style_border_opa(icon, LV_OPA_TRANSP, LV_PART_MAIN);
        }
    }
    const Settings &plateSettings = controller->getSettings();
    applyAnimPlates(plateSettings.getBgAnimClearPlates(), static_cast<uint32_t>(plateSettings.getBgAnimPlateColor()),
                    plateSettings.getBgAnimPlateOpacity());
    // Last, because it ends by snapshotting the widgets, and the two rewrites
    // above are part of what that snapshot has to capture.
    adoptAnimHost(host);
#endif
}

// The generated screens put a full-bleed opaque object behind their content:
// brew, status and profile each a 360x360 circle (radius 180), info a 400x400
// rounded square. Menu, steam, water and grind have none. startSleepAnimation
// makes the SCREEN transparent, but not these children, so the animation shows
// whole on the screens without a plate and with a black disc punched through it
// on the ones with. Hiding them is what makes every screen look alike.
//
// The original opacity is saved and put back rather than dropping the local
// style property, because the generated code sets that property explicitly and
// removing it would fall through to the theme default instead of the value the
// screen was designed with. Colours are restored differently — see the note on
// change_color_theme at the end of the function.
void DefaultUI::applyAnimPlates(int mode, uint32_t color, int opaPct) {
#ifndef GAGGIMATE_SIM
    if (opaPct < 0) {
        opaPct = 0;
    } else if (opaPct > 100) {
        opaPct = 100;
    }
    // Colour and opacity only mean anything in mode 2; ignoring them otherwise
    // keeps mode 0/1 from repainting every time the (unused) colour changes.
    const uint32_t wantColor = mode == 2 ? color : 0;
    const int wantOpa = mode == 2 ? opaPct : 0;
    if (mode == animPlateMode && wantColor == animPlateColor && wantOpa == animPlateOpaPct) {
        return;
    }

    // obj2/obj9/obj15/obj26 are the full-bleed panels behind the dials.
    // profile_name/profile_name_1 are the profile-name labels on the brew and
    // profile screens: not panels, but opaque, and because the text scrolls
    // (LONG_SCROLL_CIRCULAR) the bar behind it is in constant motion against
    // the animation, which makes it the most obvious of the lot.
    // mode_switch/mode_switch1 are the pills carrying the scale weight readout
    // on the brew and grind screens. They are buttons as well as backdrops, so
    // mode 2 is the useful setting for them: it keeps a visible affordance
    // instead of dissolving the control into the animation.
    // The last entry is the scale overlay's Tare pill, built at runtime by
    // buildScaleScreen rather than generated, so it is null whenever that screen
    // is down. It is the one plate change_color_theme() knows nothing about.
    lv_obj_t *const plates[ANIM_PLATE_COUNT] = {objects.obj2,        objects.obj9,         objects.obj15,
                                                objects.obj26,       objects.profile_name, objects.profile_name_1,
                                                objects.mode_switch, objects.mode_switch1, scaleTareBtn};

    for (int i = 0; i < ANIM_PLATE_COUNT; i++) {
        if (plates[i] == nullptr) {
            continue;
        }
        if (!animPlateHas[i]) {
            animPlateOpa[i] = lv_obj_get_style_bg_opa(plates[i], LV_PART_MAIN);
            animPlateBg[i] = lv_obj_get_style_bg_color(plates[i], LV_PART_MAIN);
            animPlateHas[i] = true;
        }
        switch (mode) {
        case 0:
            lv_obj_set_style_bg_opa(plates[i], animPlateOpa[i], LV_PART_MAIN);
            lv_obj_set_style_bg_color(plates[i], animPlateBg[i], LV_PART_MAIN);
            // Mode 2 adds a checked-state colour where most of these objects
            // never had one; drop it so the state falls back to the default
            // again. The ones that legitimately do have one (mode_switch1)
            // get it reinstated by change_color_theme below.
            lv_obj_remove_local_style_prop(plates[i], LV_STYLE_BG_COLOR, LV_PART_MAIN | LV_STATE_CHECKED);
            break;
        case 2:
            lv_obj_set_style_bg_color(plates[i], lv_color_hex(color), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(plates[i], static_cast<lv_opa_t>((opaPct * 255 + 50) / 100), LV_PART_MAIN);
            // mode_switch1 carries a checked-state bg_color of its own, applied
            // whenever grind_volumetric is set, and a default-state colour does
            // not win against it. Without this the grind screen's weight pill
            // ignores the chosen colour (at the chosen opacity) exactly half the
            // time. Writing both states is also what makes the custom colour
            // hold on any plate that gains a checked style later.
            lv_obj_set_style_bg_color(plates[i], lv_color_hex(color), LV_PART_MAIN | LV_STATE_CHECKED);
            break;
        default:
            lv_obj_set_style_bg_opa(plates[i], LV_OPA_TRANSP, LV_PART_MAIN);
            break;
        }
    }

    // Restoring from a capture is not enough for the generated plates. The
    // capture reads whichever state the object was in at the time (LVGL resolves
    // a style query against the live state, it takes no state argument), and
    // change_color_theme() may have overwritten the colour since. Re-running the
    // theme is the authoritative restore: it reassigns every generated object's
    // colours in both states from theme_colors. The per-plate capture above
    // still carries the Tare pill, which the theme function does not reach.
    //
    // That leaves the Tare pill relying on its capture having been taken in the
    // default state, and it is: buildScaleScreen creates it with lv_btn_create
    // and never sets LV_OBJ_FLAG_CHECKABLE, so it has no checked state to be in,
    // and LVGL's default theme renders PRESSED through a colour filter rather
    // than a bg_color override, so a capture taken mid-press still reads the
    // default colour. If the pill ever becomes checkable, or gains a state-
    // specific bg_color, capture it explicitly instead of querying the live
    // state -- the query resolves against whatever state the object is in.
    if (mode == 0 && currentThemeMode >= 0) {
        change_color_theme(static_cast<uint32_t>(currentThemeMode));
    }

    animPlateMode = mode;
    animPlateColor = wantColor;
    animPlateOpaPct = wantOpa;
#endif
}

void DefaultUI::releaseAnimHost() {
#ifndef GAGGIMATE_SIM
    if (animHostScreen != nullptr) {
        // Drop the transparent-background override, back to the EEZ style.
        lv_obj_remove_local_style_prop(animHostScreen, LV_STYLE_BG_OPA, LV_PART_MAIN);
        animHostScreen = nullptr;
    }
#endif
}

void DefaultUI::adoptAnimHost(lv_obj_t *host) {
#ifndef GAGGIMATE_SIM
    if (host == nullptr || host == animHostScreen) {
        return;
    }
    releaseAnimHost();
    animHostScreen = host;
    // The render task owns the panel while the animation runs; LVGL keeps the
    // host screen active only for input and for the offscreen widget
    // snapshots. Making the screen background transparent keeps those
    // snapshots per-pixel alpha (widgets only, no opaque color plate).
    lv_obj_set_style_bg_opa(animHostScreen, LV_OPA_TRANSP, LV_PART_MAIN);
    // Both overlay buffers describe the screen that just went away.
    overlayValid[0] = overlayValid[1] = false;
    overlayDirtyN[0] = overlayDirtyN[1] = 0;
    refreshSleepOverlay();
#endif
}

void DefaultUI::stopSleepAnimation() {
#ifndef GAGGIMATE_SIM
    releaseDialElements();
    sleepAnimation.stop();
    sleepAnimation.setOverlayGain(256);
    overlayTrans = OverlayTrans::Idle;
    overlayTransWaitSwap = false;
    overlayTransHeld = false;
    lvgl_helper_suppress_flush(false);
    applyAnimPlates(0);
    for (lv_obj_t *icon : {objects.wifi_icon, objects.bluetooth_icon, objects.update_icon}) {
        if (icon != nullptr) {
            lv_obj_remove_local_style_prop(icon, LV_STYLE_BORDER_OPA, LV_PART_MAIN);
        }
    }
    lv_obj_t *const host = animHostScreen;
    releaseAnimHost();
    if (host != nullptr) {
        // Repaint the whole screen over the last animation frame.
        lv_obj_invalidate(host);
    }
#endif
}

// Renders the host screen's widgets into the animation's back overlay buffer
// via an offscreen LVGL snapshot (RGB565+A8), then publishes it for the
// render task to alpha-blend into every animation frame.
// Modelled on lv_snapshot_take_to_buf (lvgl/src/extra/others/snapshot), with
// one difference that is the entire point: buf_area stays the full snapshot
// rectangle, so the buffer keeps its geometry and stride, while clip_area is
// narrowed to the region that changed. LVGL then draws only that region, into
// its correct place in the existing buffer.
//
// The upstream function also memsets the whole buffer first. Here only the clip
// rectangle is cleared, because everything outside it is still valid from an
// earlier pass. Clearing it at all matters: alpha has to go back to zero where
// a widget shrank or moved away, or it would leave a trail.
// The overlay's x margin: the screen's ext draw size rounded up to a
// multiple of 8, so the snapshot's stride and the panel's x offset into it
// are multiples of 8 and every 8-aligned group of a row is one 16-byte
// aligned vector load for the planar blend (SleepAnimation::overlayVecOk).
// The y margin stays the ext draw size.
static lv_coord_t overlayExtX(lv_coord_t ext) { return static_cast<lv_coord_t>((ext + 7) & ~7); }

bool DefaultUI::snapshotAreaToOverlay(lv_obj_t *obj, uint8_t *buf, uint32_t bufSize, const lv_area_t &clip, int *outW,
                                      int *outH, bool clearByRuns) {
#ifndef GAGGIMATE_SIM
    const lv_coord_t ext = _lv_obj_get_ext_draw_size(obj);
    const lv_coord_t extX = overlayExtX(ext);
    lv_area_t snapshotArea;
    lv_obj_get_coords(obj, &snapshotArea);
    lv_area_increase(&snapshotArea, extX, ext);

    const int w = lv_obj_get_width(obj) + extX * 2;
    const int h = lv_obj_get_height(obj) + ext * 2;
    // Two planes of w * h uint16_t, the alpha plane planePx pixels in
    // (SleepAnimation::overlayPlanePixels; bufSize is both planes).
    const uint32_t planePx = bufSize / 4;
    if (w <= 0 || h <= 0 || static_cast<uint32_t>(w) * h > planePx) {
        return false;
    }
    uint16_t *const colPlane = reinterpret_cast<uint16_t *>(buf);
    uint16_t *const a16Plane = colPlane + planePx;

    lv_area_t clipped = clip;
    if (clipped.x1 < snapshotArea.x1)
        clipped.x1 = snapshotArea.x1;
    if (clipped.y1 < snapshotArea.y1)
        clipped.y1 = snapshotArea.y1;
    if (clipped.x2 > snapshotArea.x2)
        clipped.x2 = snapshotArea.x2;
    if (clipped.y2 > snapshotArea.y2)
        clipped.y2 = snapshotArea.y2;
    if (areaEmpty(clipped)) {
        return false;
    }

    // Reset alpha (and colour) across the region about to be redrawn.
#ifdef GM_TOUCH_PROBE
    const int64_t clear0 = esp_timer_get_time();
#endif
    // Alpha only. Colour under alpha 0 is never read: the planar writer
    // copies over a transparent pixel without reading it, the blend kernels
    // give (fg * 0 + bg * 256) >> 8 = bg exactly, and the span scan reads
    // the alpha plane alone. Clearing both planes was half of a whole-page
    // snapshot's clear (35 to 53 ms of the two planes' 921 KB, gm-2cl.7).
    // Full-width clips are one contiguous memset instead of one per row.
    const size_t rowBytes = static_cast<size_t>(clipped.x2 - clipped.x1 + 1) * 2;
    if (clipped.x1 == snapshotArea.x1 && clipped.x2 == snapshotArea.x2) {
        // A whole-buffer clip whose buffer was published and not drawn into
        // since: zero only where its run table says there is coverage. The
        // margin rows and columns outside the panel are never composited or
        // scanned, so they can keep whatever they hold.
        const bool wholeBuf = clipped.y1 == snapshotArea.y1 && clipped.y2 == snapshotArea.y2;
        bool done = false;
        if (clearByRuns && wholeBuf) {
            done = sleepAnimation.clearBackAlphaByRuns(w, h);
        }
        if (!done) {
            const size_t off = static_cast<size_t>(clipped.y1 - snapshotArea.y1) * w;
            memset(a16Plane + off, 0, static_cast<size_t>(clipped.y2 - clipped.y1 + 1) * w * 2);
        }
        if (wholeBuf) {
            g_overlayStats.lastWholeClearByRuns = done ? 1 : 0;
        }
        gm_snap_rows_mark_cleared(clipped.y1 - snapshotArea.y1, clipped.y2 - snapshotArea.y1);
    } else {
        for (int y = clipped.y1; y <= clipped.y2; y++) {
            const size_t off = static_cast<size_t>(y - snapshotArea.y1) * w + (clipped.x1 - snapshotArea.x1);
            memset(a16Plane + off, 0, rowBytes);
        }
    }
    (void)colPlane;
#ifdef GM_TOUCH_PROBE
    g_snapClearSum += esp_timer_get_time() - clear0;
#endif

    lv_disp_t *objDisp = lv_obj_get_disp(obj);
    lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.hor_res = lv_disp_get_hor_res(objDisp);
    driver.ver_res = lv_disp_get_hor_res(objDisp);
    // The planar writer (LV_Helper.h); the patched lv_draw_sw_blend.c
    // recognises it by pointer identity and inlines it.
    driver.set_px_cb = gm_set_px_planar;

    lv_disp_t fakeDisp;
    lv_memset_00(&fakeDisp, sizeof(lv_disp_t));
    fakeDisp.driver = &driver;

    lv_draw_ctx_t *drawCtx = static_cast<lv_draw_ctx_t *>(lv_mem_alloc(objDisp->driver->draw_ctx_size));
    if (drawCtx == nullptr) {
        return false;
    }
    objDisp->driver->draw_ctx_init(fakeDisp.driver, drawCtx);
    fakeDisp.driver->draw_ctx = drawCtx;
    drawCtx->clip_area = &clipped;     // only this is redrawn
    drawCtx->buf_area = &snapshotArea; // buffer keeps full geometry and stride
    drawCtx->buf = static_cast<void *>(buf);
    driver.draw_ctx = drawCtx;

    lv_disp_t *refrOri = _lv_refr_get_disp_refreshing();
    _lv_refr_set_disp_refreshing(&fakeDisp);
#ifdef GM_TOUCH_PROBE
    const int64_t draw0 = esp_timer_get_time();
    gm_ws_active = true;
#endif
    lv_obj_redraw(drawCtx, obj);
#ifdef GM_TOUCH_PROBE
    gm_ws_active = false;
    g_snapDrawSum += esp_timer_get_time() - draw0;
#endif
    _lv_refr_set_disp_refreshing(refrOri);

    objDisp->driver->draw_ctx_deinit(fakeDisp.driver, drawCtx);
    lv_mem_free(drawCtx);

    if (outW != nullptr)
        *outW = w;
    if (outH != nullptr)
        *outH = h;
    return true;
#else
    return false;
#endif
}

uint32_t DefaultUI::overlayFadeOutMs() const {
    const int ms = controller->getSettings().getBgFadeOutMs();
    return ms < 0 ? 0 : static_cast<uint32_t>(ms);
}

uint32_t DefaultUI::overlayFadeInMs() const {
    const int ms = controller->getSettings().getBgFadeInMs();
    return ms < 0 ? 0 : static_cast<uint32_t>(ms);
}

// True once no frame can still show the old page: the render task has
// latched gain zero, or the fade-out ramp has run its course, so any frame
// it renders from now on evaluates to zero. The second test matters when
// frames are stalled (the web task serialising a large dump on the render
// core, for one): waiting on the latch alone held a page 400 ms.
bool DefaultUI::overlayFadedOut() const {
#ifndef GAGGIMATE_SIM
    if (sleepAnimation.overlayGain() == 0) {
        return true;
    }
    return esp_timer_get_time() >= overlayTransT0Us + static_cast<int64_t>(overlayTransOutMs) * 1000 + 2000;
#else
    return true;
#endif
}

// The transition's page has just been published: fade it in from the first
// frame that composites it (the gate), and log the hand-over.
void DefaultUI::finishOverlayTransition() {
#ifndef GAGGIMATE_SIM
    sleepAnimation.rampOverlayGain(256, overlayFadeInMs(), sleepAnimation.overlayFrontIndex());
    overlayTrans = OverlayTrans::FadeIn;
#ifdef GM_TOUCH_PROBE
    ESP_LOGI("TouchProbe", "GM_TRANS: fade_in_start t0+%lld us", (long long)(esp_timer_get_time() - overlayTransT0Us));
#endif
#endif
}

void DefaultUI::refreshSleepOverlay() {
#ifndef GAGGIMATE_SIM
    // The debt lists, LVGL's accumulator and publishOverlayRanges' local range
    // buffer all size to the same cap; a mismatch silently drops debt.
    static_assert(OVERLAY_DIRTY_RECTS == GM_DIRTY_RECT_CAP, "rect caps must match");
    lv_obj_t *scr = animHostScreen;
    if (scr == nullptr) {
        return;
    }
    // lv_obj_redraw() below draws objects where the layout says they are, and
    // it does not run the layout itself — lv_task_handler() does, and that runs
    // after this. On a freshly created screen the flex containers have not been
    // laid out yet, so their children still sit at the container origin and the
    // snapshot catches them stacked. This is a no-op once the layout is valid.
    lv_obj_update_layout(scr);
    // Collect what LVGL redrew since the last pass FIRST, and owe it to both
    // buffers: as a render to the one that is back now, as a copy to the
    // other (see overlayCopy in the header). Doing this before any early
    // return is what makes the retry paths below safe: a refresh that
    // cannot proceed loses nothing, and the back index only moves on a
    // publish, so the split stays right across a retry.
    const int back = sleepAnimation.overlayBackIndex();
    const int front = back ^ 1;
    lv_area_t fresh[OVERLAY_DIRTY_RECTS];
    const int freshN = lvgl_helper_take_dirty_rects(fresh, OVERLAY_DIRTY_RECTS);
    for (int i = 0; i < freshN; i++) {
        lvgl_helper_rect_add(overlayDirty[back], &overlayDirtyN[back], OVERLAY_DIRTY_RECTS, fresh[i]);
        lvgl_helper_rect_add(overlayCopy[front], &overlayCopyN[front], OVERLAY_DIRTY_RECTS, fresh[i]);
        DirtyLogEntry &e = g_dirtyLog[g_dirtyLogCount % DIRTYLOG_N];
        e.x1 = static_cast<int16_t>(fresh[i].x1);
        e.y1 = static_cast<int16_t>(fresh[i].y1);
        e.x2 = static_cast<int16_t>(fresh[i].x2);
        e.y2 = static_cast<int16_t>(fresh[i].y2);
        e.tMs = static_cast<uint32_t>(esp_timer_get_time() / 1000);
        g_dirtyLogCount = g_dirtyLogCount + 1;
    }

    // nullptr means the render task is still reading that buffer; retry next
    // pass without stamping the refresh time.
    uint8_t *buf = sleepAnimation.overlayBackBuffer();
    if (buf == nullptr) {
        return;
    }

    // A screen change is fading the old page out and its swap has not run
    // yet: nothing published now could be anything but the old page, so
    // hold. The debt harvested above is kept, and adoptAnimHost discards it
    // with the rest of the old screen.
    if (overlayTransWaitSwap) {
        return;
    }
    // The transition's page is rendered and waiting for the fade-out to
    // reach zero. Publish it whole the moment it has; debt harvested since
    // stays listed against this buffer and is rendered on its next turn.
    if (overlayTransHeld) {
        if (!overlayFadedOut()) {
            return;
        }
        overlayTransHeld = false;
        const int whole[1][2] = {{0, overlayH[back]}};
        const int64_t pub0 = esp_timer_get_time();
        sleepAnimation.requestBandWarmup(whole, 1);
        sleepAnimation.publishOverlayRanges(overlayW[back], overlayH[back], whole, 1);
        overlayDrawnUnpublished[back] = false;
        g_overlayStats.lastWholePubUs = static_cast<uint32_t>(esp_timer_get_time() - pub0);
        g_overlayStats.lastWholeScanUs = g_overlayStats.lastPubScanUs;
        g_overlayStats.lastWholeScrimUs = g_overlayStats.lastPubScrimUs;
        finishOverlayTransition();
        return;
    }

    // Nothing moved and this buffer is already complete: the whole refresh
    // costs one comparison. This is the case that gives touch its time back on
    // a screen that is merely sitting there.
    if (overlayValid[back] && overlayDirtyN[back] == 0 && overlayCopyN[back] == 0) {
        return;
    }

    // Telemetry refresh spacing (see OVERLAY_MIN_REFRESH_US). Only partial
    // refreshes of an already-filled buffer are gated: a buffer that needs a
    // whole fill (screen change, geometry move) paints immediately, and for
    // GM_TOUCH_GRACE_US after any touch edge everything goes straight
    // through so a tap's visual effects never wait behind the gate. The debt
    // merged above survives the return, so a held refresh coalesces instead
    // of dropping.
    if (overlayValid[back]) {
        const int64_t nowUs = esp_timer_get_time();
        if (nowUs - g_touchEdgeAtUs >= GM_TOUCH_GRACE_US && nowUs >= overlayUrgentUntilUs &&
            nowUs - lastOverlayRefreshUs < g_overlayMinRefreshUs) {
            return;
        }
    }

    // If the snapshot geometry has moved since this buffer was last written,
    // its contents are no longer where they claim to be: the buffer is indexed
    // from coords.y1-ext and composited at an offset of ext, so a change to
    // the screen's extended draw size displaces everything already in it. A
    // dirty rectangle cannot express that, so redraw the buffer whole.
    {
        lv_area_t geom;
        lv_obj_get_coords(scr, &geom);
        const lv_coord_t extNow = _lv_obj_get_ext_draw_size(scr);
        const int wNow = lv_area_get_width(&geom) + overlayExtX(extNow) * 2;
        const int hNow = lv_area_get_height(&geom) + extNow * 2;
        if (overlayValid[back] && (overlayW[back] != wNow || overlayH[back] != hNow)) {
            log_i("sleep overlay: snapshot geometry %dx%d -> %dx%d, redrawing buffer %d whole", overlayW[back],
                  overlayH[back], wNow, hNow, back);
            overlayValid[back] = false;
        }
    }

    // One snapshot per owed rectangle rather than one of their bounding box:
    // the snapshot render is the expensive stage, and its cost has to scale
    // with what actually changed, not with how far apart the changes sit.
    // Twice the cap: the second half takes what the mid-pass input read
    // below turns up, kept apart from the first so a merge can never fold a
    // fresh rect into a clip this pass has already snapshotted.
    lv_area_t clips[2 * OVERLAY_DIRTY_RECTS];
    int clipN = 0;
    lv_area_t copies[OVERLAY_DIRTY_RECTS];
    int copyN = 0;
    const bool wholeSnap = !overlayValid[back];
    // Every row scans whole unless a snapshot below clears its full width.
    gm_snap_rows_reset_all();
#ifdef GM_TOUCH_PROBE
    const int64_t wholeClear0 = g_snapClearSum;
    const int64_t wholeDraw0 = g_snapDrawSum;
#endif
    if (overlayValid[back]) {
        clipN = overlayDirtyN[back];
        for (int i = 0; i < clipN; i++) {
            clips[i] = overlayDirty[back][i];
        }
        // Copy debt is settled from the front buffer only while the front
        // is complete and the same size; otherwise it is rendered like any
        // other debt. Rendered rects go into the first half of clips[] so
        // they merge with the render debt, never with the mid-pass extras.
        const bool canCopy = sleepAnimation.overlayFrontBuffer() != nullptr && overlayValid[front] &&
                             overlayW[front] == overlayW[back] && overlayH[front] == overlayH[back];
        for (int i = 0; i < overlayCopyN[back]; i++) {
            if (canCopy) {
                copies[copyN++] = overlayCopy[back][i];
            } else {
                lvgl_helper_rect_add(clips, &clipN, OVERLAY_DIRTY_RECTS, overlayCopy[back][i]);
            }
        }
    } else {
        // First use of this buffer: it holds nothing, so a partial draw would
        // composite against garbage. Take the whole screen once.
        lv_obj_get_coords(scr, &clips[0]);
        const lv_coord_t ext = _lv_obj_get_ext_draw_size(scr);
        lv_area_increase(&clips[0], overlayExtX(ext), ext);
        clipN = 1;
    }

    lastSleepOverlayRefresh = ::millis();
    lastOverlayRefreshUs = esp_timer_get_time();
    int w = 0, h = 0;
    const int64_t probeSnap0 = esp_timer_get_time();
    int64_t probeArea = 0;
    int64_t copyArea = 0;
    // Copies first, renders after: a rect in both lists holds newer content
    // in the render debt, and the render overwrites the copy.
    if (copyN > 0) {
        const uint16_t *src = reinterpret_cast<const uint16_t *>(sleepAnimation.overlayFrontBuffer());
        uint16_t *dst = reinterpret_cast<uint16_t *>(buf);
        const uint32_t planePx = sleepAnimation.overlayPlanePixels();
        lv_area_t origin;
        lv_obj_get_coords(scr, &origin);
        const lv_coord_t ext = _lv_obj_get_ext_draw_size(scr);
        lv_area_increase(&origin, overlayExtX(ext), ext);
        const int bufW = lv_area_get_width(&origin);
        for (int i = 0; i < copyN; i++) {
            lv_area_t r;
            if (!_lv_area_intersect(&r, &copies[i], &origin)) {
                continue;
            }
            const size_t rowBytes = static_cast<size_t>(lv_area_get_width(&r)) * 2;
            for (lv_coord_t y = r.y1; y <= r.y2; y++) {
                const size_t off = static_cast<size_t>(y - origin.y1) * bufW + (r.x1 - origin.x1);
                memcpy(dst + off, src + off, rowBytes);                     // colour plane
                memcpy(dst + planePx + off, src + planePx + off, rowBytes); // alpha plane
            }
            copyArea += static_cast<int64_t>(lv_area_get_width(&r)) * lv_area_get_height(&r);
        }
        w = bufW;
        h = lv_area_get_height(&origin);
    }
#ifdef GM_TOUCH_PROBE
    const int64_t probeCopy1 = esp_timer_get_time();
#endif
    const int baseN = clipN;
    int extraN = 0;
    for (int i = 0; i < baseN + extraN; i++) {
        const bool byRuns = wholeSnap && !overlayDrawnUnpublished[back] && g_clearByRunsReq != 0;
        overlayDrawnUnpublished[back] = true;
        if (!snapshotAreaToOverlay(scr, buf, sleepAnimation.overlayCapacity(), clips[i], &w, &h, byRuns)) {
            // Leave the debt list intact; the next pass retries every rect.
            // Rects already snapshotted this pass just render identically then.
            log_w("Sleep overlay snapshot failed");
            return;
        }
        probeArea += static_cast<int64_t>(lv_area_get_width(&clips[i])) * lv_area_get_height(&clips[i]);
        // gm-qo3.2: a pass with several clips can run past 100 ms, and the
        // touch controller is only polled from lv_task_handler(), so a tap
        // that lands during such a pass used to wait for all of it, then for
        // the next pass to snapshot its effect. Between clips, once the pass
        // has run a while, poll input and pull whatever it invalidated into
        // THIS pass, so the tap rides the publish already under way. Only
        // during the original clips: the extras live in the second half of
        // clips[] and merging into them once they are being snapshotted
        // would break the same invariant the split exists for. The other
        // buffer is owed the same rects as always.
        if (i + 1 < baseN && esp_timer_get_time() - probeSnap0 >= OVERLAY_INPUT_SLICE_US) {
            lv_task_handler();
            if (animHostScreen != scr) {
                // A click changed screens. Debt is intact; the next pass
                // starts over on the new host.
                return;
            }
            lv_area_t more[OVERLAY_DIRTY_RECTS];
            const int moreN = lvgl_helper_take_dirty_rects(more, OVERLAY_DIRTY_RECTS);
            for (int k = 0; k < moreN; k++) {
                lvgl_helper_rect_add(overlayCopy[front], &overlayCopyN[front], OVERLAY_DIRTY_RECTS, more[k]);
                lvgl_helper_rect_add(clips + baseN, &extraN, OVERLAY_DIRTY_RECTS, more[k]);
            }
        }
    }
    clipN = baseN + extraN;
    const int64_t probeSnap1 = esp_timer_get_time();
    // Marked here rather than at the call site, and after the snapshot rather
    // than before it, so the slip log measures the thing that actually costs
    // something. Every early return above is a pass that touched no memory --
    // most of them, since on standby the widgets only change when the clock
    // does -- and marking those would spread the timestamp over passes that
    // cannot have caused anything.
    panelclock::scanoutMark(panelclock::SCANOUT_ACT_OVERLAY);

    // Only the rows that changed need their alpha spans recomputed. The clips
    // are in screen coordinates and the host object is the screen, so screen
    // row and panel row are the same number.
    int ranges[3 * OVERLAY_DIRTY_RECTS][2];
    int rangeN = 0;
    for (int i = 0; i < clipN; i++) {
        ranges[rangeN][0] = clips[i].y1;
        ranges[rangeN][1] = clips[i].y2 + 1;
        rangeN++;
    }
    for (int i = 0; i < copyN; i++) {
        ranges[rangeN][0] = copies[i].y1;
        ranges[rangeN][1] = copies[i].y2 + 1;
        rangeN++;
    }
    // Warm-up before the publish (an ordering the GPT-6 Astra oracle flagged,
    // 2026-09-06): the render task runs on the other core, and a frame that
    // started between the publish and this request would have interlaced
    // the changed rows once. Requested first, the worst case is one band
    // rendered whole against the previous overlay, which is harmless. The
    // list is the same one the publish gets, in the same panel-row space.
    // A screen change requested from the snapshot's own input slice: what
    // was just rendered is the old screen. Drop it; adoptAnimHost rebuilds
    // both buffers for the new one.
    if (overlayTransWaitSwap) {
        return;
    }
    // The transition's page must not appear until the old one has faded
    // out entirely: hold it in this buffer and publish on a later pass.
    if (overlayTrans == OverlayTrans::FadeOut && !overlayFadedOut()) {
#ifdef GM_TOUCH_PROBE
        ESP_LOGI("TouchProbe", "GM_TRANS: page_ready held, snap %lld us, t0+%lld us",
                 (long long)(probeSnap1 - probeSnap0), (long long)(probeSnap1 - overlayTransT0Us));
#endif
        overlayTransHeld = true;
        overlayValid[back] = true;
        overlayW[back] = w;
        overlayH[back] = h;
        overlayDirtyN[back] = 0;
        overlayCopyN[back] = 0;
        g_overlayStats.lastWholeSnapUs = static_cast<uint32_t>(probeSnap1 - probeSnap0);
        g_overlayStats.lastWholeAtMs = static_cast<uint32_t>(::millis());
#ifdef GM_TOUCH_PROBE
        g_overlayStats.lastWholeClearUs = static_cast<uint32_t>(g_snapClearSum - wholeClear0);
        g_overlayStats.lastWholeDrawUs = static_cast<uint32_t>(g_snapDrawSum - wholeDraw0);
#endif
        return;
    }
    sleepAnimation.requestBandWarmup(ranges, rangeN);
    sleepAnimation.publishOverlayRanges(w, h, ranges, rangeN);
    overlayDrawnUnpublished[back] = false;
    if (overlayTrans == OverlayTrans::FadeOut) {
#ifdef GM_TOUCH_PROBE
        ESP_LOGI("TouchProbe", "GM_TRANS: page_ready published, snap %lld us, t0+%lld us",
                 (long long)(probeSnap1 - probeSnap0), (long long)(probeSnap1 - overlayTransT0Us));
#endif
        finishOverlayTransition();
    }
    g_overlayStats.lastSnapUs = static_cast<uint32_t>(probeSnap1 - probeSnap0);
    g_overlayStats.lastPubUs = static_cast<uint32_t>(esp_timer_get_time() - probeSnap1);
    if (wholeSnap) {
        g_overlayStats.lastWholeSnapUs = g_overlayStats.lastSnapUs;
        g_overlayStats.lastWholePubUs = g_overlayStats.lastPubUs;
        g_overlayStats.lastWholeScanUs = g_overlayStats.lastPubScanUs;
        g_overlayStats.lastWholeScrimUs = g_overlayStats.lastPubScrimUs;
        g_overlayStats.lastWholeAtMs = static_cast<uint32_t>(::millis());
#ifdef GM_TOUCH_PROBE
        g_overlayStats.lastWholeClearUs = static_cast<uint32_t>(g_snapClearSum - wholeClear0);
        g_overlayStats.lastWholeDrawUs = static_cast<uint32_t>(g_snapDrawSum - wholeDraw0);
#endif
    }
    g_overlayStats.lastAreaPx = static_cast<uint32_t>(probeArea);
    g_overlayStats.lastClips = static_cast<uint32_t>(clipN + copyN);
    g_overlayStats.refreshes = g_overlayStats.refreshes + 1;
#ifdef GM_TOUCH_PROBE
    {
        const int64_t snapUs = probeSnap1 - probeSnap0;
        const int64_t pubUs = esp_timer_get_time() - probeSnap1;
        g_ovlN++;
        g_ovlSnapSum += snapUs;
        g_ovlPubSum += pubUs;
        if (snapUs > g_ovlSnapMax)
            g_ovlSnapMax = snapUs;
        if (pubUs > g_ovlPubMax)
            g_ovlPubMax = pubUs;
        g_ovlAreaSum += probeArea;
        if (probeArea > g_ovlAreaMax)
            g_ovlAreaMax = probeArea;
        g_ovlCopySum += probeCopy1 - probeSnap0;
        g_ovlCopyArea += copyArea;
    }
    if (g_probeEdgeUs != 0) {
        const int64_t edge = g_probeEdgeUs;
        g_probeEdgeUs = 0;
        ESP_LOGI("TouchProbe", "GM_TOUCHLAT: %s->overlay_publish %lld us", g_probeEdgeIsPress ? "press" : "release",
                 (long long)(esp_timer_get_time() - edge));
        // Hand the interval to the render task: the pixels reach the panel at
        // the present of the first frame whose composite samples this publish.
        g_probePublishIsPress = g_probeEdgeIsPress;
        g_probePublishUs = edge;
    }
#endif
    overlayValid[back] = true;
    overlayW[back] = w;
    overlayH[back] = h;
    overlayDirtyN[back] = 0;
    overlayCopyN[back] = 0;
#endif
}

// Entry point for the menu's Scale button (via action_on_grind_screen when the
// scaleMenuButton setting is on). Mirrors the other menu actions: switch to the
// host screen, set a non-heating mode, ensure nothing is active.
void DefaultUI::openScaleScreen() {
    scaleScreenRequested = true;
    changeScreen(SCREEN_ID_GRIND_SCREEN);
    controller->setMode(MODE_GRIND);
    controller->deactivate();
}

// Runs every UI pass. The overlay can only be built once the EEZ grind screen
// object exists (it is created lazily on first load), so creation is deferred
// here; the same pass also keeps the weight readout current and tears the
// overlay down when the user leaves the screen by any path.
void DefaultUI::maintainScaleScreen() {
    if (scaleScreenRequested && currentScreen == SCREEN_ID_GRIND_SCREEN && objects.grind_screen != nullptr) {
        if (scaleScreen == nullptr) {
            buildScaleScreen();
        }
        if (scaleScreen != nullptr) {
            // Re-asserted every pass: the EEZ tick fights HIDDEN flags on these
            // widgets, but never touches translate, so displacement sticks.
            displaceGrindWidgets(true);
            lv_obj_clear_flag(scaleScreen, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(scaleScreen);
            const float w = static_cast<float>(scaleHardwareWeight);
            if (scaleWeightLabel != nullptr && fabsf(w - lastShownScaleWeight) >= 0.05f) {
                lastShownScaleWeight = w;
                lv_label_set_text_fmt(scaleWeightLabel, "%.1f", static_cast<double>(w));
            }
        }
    } else if (scaleScreen != nullptr && !lv_obj_has_flag(scaleScreen, LV_OBJ_FLAG_HIDDEN) &&
               (!scaleScreenRequested || currentScreen != SCREEN_ID_GRIND_SCREEN)) {
        displaceGrindWidgets(false);
        lv_obj_add_flag(scaleScreen, LV_OBJ_FLAG_HIDDEN);
        if (currentScreen != SCREEN_ID_GRIND_SCREEN) {
            scaleScreenRequested = false;
        }
    }
}

// The grind screen's own widgets can't be hidden while the scale overlay is up:
// tick_screen_grind_screen re-derives their HIDDEN flags from flow state every
// tick and would undo it. Translating them off-panel instead is tick-proof and
// fully reversible (the local style prop is simply removed on exit).
void DefaultUI::displaceGrindWidgets(bool displaced) {
    // grind_dials__menu_icon goes with them: the scale overlay draws its own exit
    // arrow in that slot, and the dials' icon is flow-driven so it cannot simply
    // be hidden.
    lv_obj_t *const widgets[] = {objects.main_label4,   objects.grind_start_button, objects.mode_switch1,
                                 objects.target_weight, objects.target_time,        objects.grind_dials__menu_icon};
    for (lv_obj_t *obj : widgets) {
        if (obj == nullptr) {
            continue;
        }
        if (displaced) {
            if (lv_obj_get_style_translate_y(obj, LV_PART_MAIN) != 600) {
                lv_obj_set_style_translate_y(obj, 600, LV_PART_MAIN);
            }
        } else {
            lv_obj_remove_local_style_prop(obj, LV_STYLE_TRANSLATE_Y, LV_PART_MAIN);
        }
    }
}

void DefaultUI::buildScaleScreen() {
    lv_obj_t *scr = objects.grind_screen;
    if (scr == nullptr) {
        return;
    }
    const uint32_t themeIdx = eez_flow_get_selected_theme_index();
    const lv_color_t fg = lv_color_hex(theme_colors[themeIdx][0]);   // text/accent
    const lv_color_t fill = lv_color_hex(theme_colors[themeIdx][1]); // bg/pill fill

    // Transparent, click-through cover: the dial gauges (and their standby/menu
    // icons) stay visible and operable underneath, so the screen shares the
    // visual signature of every other process screen. The grind-only widgets
    // are translated off-panel (displaceGrindWidgets) and the scale widgets
    // occupy the same layout slots the grind widgets vacated.
    lv_obj_t *cover = lv_obj_create(scr);
    scaleScreen = cover;
    lv_obj_set_size(cover, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(cover, 0, 0);
    lv_obj_set_style_radius(cover, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(cover, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cover, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cover, 0, LV_PART_MAIN);
    lv_obj_clear_flag(cover, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    // If a flow action ever deletes the grind screen, the overlay dies with it —
    // null the cached pointers so the next maintain pass rebuilds cleanly.
    lv_obj_add_event_cb(
        cover,
        [](lv_event_t *e) {
            auto *ui = static_cast<DefaultUI *>(lv_event_get_user_data(e));
            ui->scaleScreen = nullptr;
            ui->scaleWeightLabel = nullptr;
            // Child of the cover, so it dies with it. applyAnimPlates walks this
            // pointer every pass and would otherwise reach a freed object.
            ui->scaleTareBtn = nullptr;
        },
        LV_EVENT_DELETE, this);

    // Title in the standard slot (grind's "Grind" label position/font).
    lv_obj_t *title = lv_label_create(cover);
    lv_label_set_text(title, "Scale");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, fg, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -140);

    // Readout as a flex row (number, unit) sized to its content and centred as
    // a group. A one-shot lv_obj_align_to() of the unit against the number only
    // holds for the width the number had at build time: "302.2" in a 48 pt face
    // is ~60 px wider than "0.0", and a centre-aligned number grows both ways,
    // so its last digits landed on top of the "g". Flex re-lays the pair on
    // every width change, so the unit follows the number.
    lv_obj_t *readout = lv_obj_create(cover);
    lv_obj_remove_style_all(readout);
    lv_obj_set_size(readout, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(readout, LV_FLEX_FLOW_ROW);
    // Cross axis END puts both baselines on the row's bottom edge; the unit's
    // bottom padding then lifts its glyph the 6 px the old alignment offset did.
    lv_obj_set_flex_align(readout, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(readout, 8, LV_PART_MAIN);
    lv_obj_clear_flag(readout, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(readout, LV_ALIGN_CENTER, 0, -15);

    scaleWeightLabel = lv_label_create(readout);
    lv_label_set_text(scaleWeightLabel, "0.0");
    lv_obj_set_style_text_font(scaleWeightLabel, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(scaleWeightLabel, fg, LV_PART_MAIN);
    // Fixed width, right-aligned: the number is painted by a Text element
    // once it is live (serviceTextElements), which eases the digits at the
    // animation's rate, while the flex row above is laid out only when LVGL
    // refreshes. A content-sized number would move the unit only on those
    // refreshes, so the "g" trailed the digits (2026-09-08). "-999.9" in the
    // 48 pt face is under 170 px.
    lv_obj_set_width(scaleWeightLabel, 170);
    lv_obj_set_style_text_align(scaleWeightLabel, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lastShownScaleWeight = -1000.0f;

    lv_obj_t *unit = lv_label_create(readout);
    lv_label_set_text(unit, "g");
    lv_obj_set_style_text_font(unit, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(unit, fg, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(unit, 6, LV_PART_MAIN);

    // Tare as a standard pill (mode_switch1 geometry: 160x50, r10, 2px border).
    // Opaque, and on screen over the animation, so applyAnimPlates drives its
    // background like the generated plates. It is registered below, after its
    // styles are set, so the first capture sees the designed values.
    lv_obj_t *tareBtn = lv_btn_create(cover);
    lv_obj_set_size(tareBtn, 160, 50);
    lv_obj_align(tareBtn, LV_ALIGN_CENTER, 0, 70);
    lv_obj_set_style_radius(tareBtn, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tareBtn, fill, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tareBtn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(tareBtn, fg, LV_PART_MAIN);
    lv_obj_set_style_border_opa(tareBtn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(tareBtn, 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(tareBtn, 0, LV_PART_MAIN);
    // A rebuilt pill is a fresh object with the designed background, so drop the
    // capture and force the next pass to re-apply the current mode to it.
    scaleTareBtn = tareBtn;
    animPlateHas[ANIM_PLATE_COUNT - 1] = false;
    animPlateMode = -1;
    lv_obj_add_event_cb(
        tareBtn, [](lv_event_t *e) { action_on_volumetric_hold(e); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *tareLabel = lv_label_create(tareBtn);
    lv_label_set_text(tareLabel, "Tare");
    lv_obj_set_style_text_font(tareLabel, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(tareLabel, fg, LV_PART_MAIN);
    lv_obj_center(tareLabel);

    // Exit to the menu, in the dials widget's menu-icon slot (CENTER + 210) so
    // it lands where it does on every other screen. The dials' own menu icon is
    // translated off-panel by displaceGrindWidgets while this screen is up:
    // both are img_angle_up_40x40 and both leave to the menu, so leaving both
    // visible drew the arrow twice. This one is kept rather than the dials' one
    // because it also clears scaleScreenRequested and deactivates the
    // controller, which the generated handler does not.
    lv_obj_t *exitBtn = lv_imgbtn_create(cover);
    lv_obj_set_size(exitBtn, 40, 40);
    lv_obj_align(exitBtn, LV_ALIGN_CENTER, 0, 210);
    lv_obj_set_ext_click_area(exitBtn, 45); // same box as the dials' chevron (actions.cpp)
    lv_imgbtn_set_src(exitBtn, LV_IMGBTN_STATE_RELEASED, nullptr, &img_angle_up_40x40, nullptr);
    lv_obj_set_style_img_recolor(exitBtn, fg, LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(exitBtn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_event_cb(
        exitBtn,
        [](lv_event_t *e) {
            auto *ui = static_cast<DefaultUI *>(lv_event_get_user_data(e));
            ui->scaleScreenRequested = false;
            ui->controller->deactivate();
            ui->changeScreen(SCREEN_ID_MENU_SCREEN_NEW);
        },
        LV_EVENT_CLICKED, this);

    // The pill above was just built with its designed opaque fill and its plate
    // capture dropped, so the configured mode has to be re-applied to it. The
    // maintenance loop does that, but only on its next tick: entering this screen
    // while the animation is already running would show one frame of the opaque
    // fill first. Apply it here so the pill is never briefly wrong. Same call the
    // loop makes, and a no-op when the animation is not running (mode -1 is
    // re-applied by startSleepAnimation in that case).
    if (sleepAnimation.isActive()) {
        const Settings &plateSettings = controller->getSettings();
        applyAnimPlates(plateSettings.getBgAnimClearPlates(), static_cast<uint32_t>(plateSettings.getBgAnimPlateColor()),
                        plateSettings.getBgAnimPlateOpacity());
    }
}

// Collect every lv_meter under obj (the dial gauges) so their tick length can be animated together.
void DefaultUI::collectMeters(lv_obj_t *obj) {
    const uint32_t n = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *child = lv_obj_get_child(obj, i);
        if (gaugeCount < 4 && lv_obj_check_type(child, &lv_meter_class)) {
            gaugeMeters[gaugeCount++] = child;
        }
        collectMeters(child);
    }
}

void DefaultUI::setGaugeTickLength(int32_t len) {
    for (uint8_t i = 0; i < gaugeCount; i++) {
        auto *meter = reinterpret_cast<lv_meter_t *>(gaugeMeters[i]);
        auto *scale = static_cast<lv_meter_scale_t *>(_lv_ll_get_head(&meter->scale_ll));
        if (scale != nullptr) {
            scale->tick_length = static_cast<uint16_t>(len);
        }
        lv_obj_invalidate(gaugeMeters[i]);
    }
}

void DefaultUI::gaugeTickAnimCb(void *var, int32_t v) { static_cast<DefaultUI *>(var)->setGaugeTickLength(v); }

void DefaultUI::animateGaugeTicks(ScreensEnum from, ScreensEnum to) {
    const int32_t fromLen = isShortTickScreen(from) ? GAUGE_TICK_SHORT : GAUGE_TICK_LONG;
    const int32_t toLen = isShortTickScreen(to) ? GAUGE_TICK_SHORT : GAUGE_TICK_LONG;

    lv_anim_del(this, gaugeTickAnimCb); // cancel any in-flight tick animation
    gaugeCount = 0;
    collectMeters(lv_scr_act());
    if (gaugeCount == 0) {
        return;
    }
    // Start at the previous screen's length so the ticks morph continuously in both directions.
    setGaugeTickLength(fromLen);
    if (fromLen == toLen) {
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, this);
    lv_anim_set_exec_cb(&a, gaugeTickAnimCb);
    lv_anim_set_values(&a, fromLen, toLen);
    lv_anim_set_time(&a, GAUGE_TICK_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void DefaultUI::positionMenuIcon(lv_obj_t *obj, int angle, int radius) {
    int x = sin(angle * M_PI / 180) * radius;
    int y = -1 * cos(angle * M_PI / 180) * radius;
    lv_obj_set_pos(obj, x, y);
}

void DefaultUI::updateState() {
    const auto &settings = controller->getSettings();
    mode = controller->getMode();
    currentTemp = static_cast<int>(controller->getCurrentTemp());
    targetTemp = static_cast<int>(controller->getTargetTemp());
    pressureAvailable = controller->getSystemInfo().capabilities.pressure ? 1 : 0;
    wifiConnected = WiFi.status() == WL_CONNECTED;
    grindAvailable = settings.isSmartGrindActive() || settings.getAltRelayFunction() == ALT_RELAY_GRIND;
    scaleMenuSwap = settings.isScaleMenuButton();

#ifndef GAGGIMATE_SIM
    // Keep the background animation's selection and params current — cheap
    // (two atomic stores) and makes web-UI tweaks apply live on the next frame.
#ifdef GM_ANIM_BENCH
    // The bench exists to render animations, so do not make that conditional
    // on being parked on the standby screen in standby mode.
    bgAnimAllScreens = true;
#else
    bgAnimAllScreens = settings.isBgAnimAllScreens();
#endif
    // A live gradient preview overrides both the animation shown and its
    // gradient until it lapses; the saved selection is re-resolved after.
    int animId = settings.getBgAnimId();
    bool previewActive = false;
    bool previewApply = false;
    String previewGradient;
    {
        std::lock_guard<std::mutex> guard(previewMutex);
        previewActive = previewUntil != 0 && static_cast<long>(::millis() - previewUntil) < 0;
        if (previewActive) {
            animId = previewAnim;
            previewApply = previewDirty;
            previewDirty = false;
            if (previewApply) {
                previewGradient = previewStops;
            }
        }
    }
    uint8_t animP[4];
    bg_parse_params(settings.getBgAnimParams().c_str(), animId, animP);
    sleepAnimation.configure(static_cast<uint8_t>(animId), animP);
    // fps= on /api/debug/anim overrides the stored cap for a measurement
    // (0 = stored). Applied here because this line re-applies the cap every
    // pass, so a value poked into the animation alone would not stick.
    sleepAnimation.setMaxFps(static_cast<uint8_t>(g_animFpsOverride != 0 ? g_animFpsOverride : settings.getBgAnimFps()));
    sleepAnimation.setHalfRes(settings.getBgAnimHalfRes() != 0);
    sleepAnimation.setInterlace(settings.getBgAnimInterlace() != 0);
    sleepAnimation.setOverlayGainSmooth(settings.getBgFadeCurve() != 0);
    // Panel refresh rate: live pclk divider (0 = build default). One register
    // poke, but only touch the peripheral on an actual change. Floored at
    // MIN_USER_DIV: see PanelClock.h for the measurement behind it.
    static int lastPclkDiv = INT_MIN;
    const int pclkDiv = panelclock::clampUserDiv(settings.getPanelClockDiv());
    if (pclkDiv != lastPclkDiv) {
        lastPclkDiv = pclkDiv;
        panelclock::setDiv(pclkDiv);
    }
    // Panel VCOM, same shape: a register write over the panel's SPI control
    // interface, which is separate from the RGB data path, so it is safe to do
    // while scan-out is running. Panels that have no such register ignore it.
    static int lastVcom = INT_MIN;
    const int vcom = settings.getPanelVcom();
    if (vcom != lastVcom) {
        lastVcom = vcom;
        if (panelDriver != nullptr) {
            panelDriver->setPanelVcom(vcom);
        }
    }
    // Publish the color theme only on change — setThemeStops bumps a
    // generation counter that makes every animation rebuild its palettes.
    // The key covers everything the resolution depends on; the map and
    // library strings are a few KB at most, read by reference, and equal on
    // every ordinary tick, so the comparison is a length check plus memcmp.
    static int lastThemeAnim = -1;
    static int lastThemeId = -1;
    static String lastCustom;
    static String lastMap;
    static String lastLibrary;
    if (previewActive) {
        lastThemeAnim = -1; // force a re-resolve once the preview lapses
        if (previewApply) {
            uint8_t stops[BG_THEME_MAX_STOPS][3];
            uint8_t pos[BG_THEME_MAX_STOPS];
            bool uniform = true;
            const int nStops = bg_parse_gradient(previewGradient.c_str(), stops, pos, uniform);
            if (nStops > 0) {
                if (uniform) {
                    bganim::setThemeStops(stops, nStops);
                } else {
                    bganim::setThemeStopsPos(stops, pos, nStops);
                }
            }
        }
    } else {
        const int themeId = settings.getBgAnimTheme();
        const String custom = settings.getBgAnimCustomTheme();
        const String &map = settings.getBgAnimThemeMap();
        const String &library = settings.getBgAnimGradients();
        if (animId != lastThemeAnim || themeId != lastThemeId || custom != lastCustom || map != lastMap ||
            library != lastLibrary) {
            lastThemeAnim = animId;
            lastThemeId = themeId;
            lastCustom = custom;
            lastMap = map;
            lastLibrary = library;
            uint8_t stops[BG_THEME_MAX_STOPS][3];
            uint8_t pos[BG_THEME_MAX_STOPS];
            int nStops = 0;
            bool uniform = true;
            bg_resolve_anim_theme(animId, map.c_str(), library.c_str(), themeId, custom.c_str(), stops, pos, nStops,
                                  uniform);
            if (uniform) {
                bganim::setThemeStops(stops, nStops);
            } else {
                bganim::setThemeStopsPos(stops, pos, nStops);
            }
        }
    }
    // Tone is published separately from the stops, and after them: it survives
    // a theme change (setThemeStops re-applies the stored tone), so a user who
    // has dimmed the animation does not get full brightness back the moment
    // they try a different theme. setThemeTone is a no-op when neither value
    // moved, which keeps this off the generation counter on ordinary ticks.
    bganim::setThemeTone(settings.getBgAnimBrightness() * 256 / 100, settings.getBgAnimHighlightKnee() * 255 / 100);
    sleepAnimation.setScrim(settings.getBgAnimScrim());
#endif

    uiFlags.brew_adjustments(brewScreenState == BrewScreenState::Settings);
    uiFlags.active(controller->isActive());
    uiFlags.grind_active(controller->isGrindActive());
    uiFlags.grind_volumetric(controller->isVolumetricAvailable() && settings.isVolumetricTarget());
    uiFlags.heating_flash(heatingFlash);
    uiFlags.temperature_stable(isTemperatureStable);
    uiFlags.has_prev_profile(currentProfileIdx > 0);
    {
        std::lock_guard<std::mutex> guard(profilesMutex);
        uiFlags.has_next_profile(currentProfileIdx + 1 < static_cast<int>(favoritedProfileIds.size()));
    }
}

void DefaultUI::updateSystemStatus() {
    const auto &settings = controller->getSettings();
    systemStatus.bluetooth(controller->getClientController()->isConnected());
    systemStatus.wifi(!apActive && WiFi.status() == WL_CONNECTED);
    bool error = !initialized || waitingForController || controller->isErrorState() || controller->isUpdating() ||
                 controller->isAutotuning() || controller->getSystemInfo().protocolMismatch || !controller->isReady();
    systemStatus.error(error);
    const String errorLabel = error ? getErrorMessage() : "";
    if (stringChanged(systemStatus.error_label(), errorLabel.c_str()))
        systemStatus.error_label(errorLabel.c_str());
    systemStatus.volumetric_available(controller->isVolumetricAvailable());
    systemStatus.bluetooth_scales(controller->isScaleSourceHealthy(controller->getEffectiveScaleSource()));
    systemStatus.controller_version(controller->getSystemInfo().version.c_str());
    systemStatus.display_version(BUILD_GIT_VERSION);
    systemStatus.update_available(updateAvailable);
    // targetScreen, not currentScreen: this runs before handleScreenChange()
    // in the same pass, so on the pass that switches screens currentScreen
    // still names the screen being left. The dials widget's standby and menu
    // icons share one slot with hidden flags that are exact complements of
    // in_menu, so a stale value showed the power icon on the grind screen for
    // one refresh, under the exit arrow buildScaleScreen draws in that slot.
    // targetScreen is set synchronously by changeScreen() and already names
    // the screen this pass is switching to.
    systemStatus.in_menu(targetScreen == SCREEN_ID_MENU_SCREEN_NEW);
    systemStatus.pressure_available(pressureAvailable);
    // The Scale menu button reuses the grind slot, so the flow variable that
    // shows/hides that button must account for both.
    systemStatus.grind_available(grindAvailable || scaleMenuSwap);
    systemStatus.mode(mode);
    const String ip = apActive ? String("4.4.4.1") : WiFi.localIP().toString();
    if (stringChanged(systemStatus.ip(), ip.c_str()))
        systemStatus.ip(ip.c_str());
    const String network = apActive ? String("GaggiMate") : systemStatus.wifi() ? settings.getWifiSsid() : String("Disconnected");
    if (stringChanged(systemStatus.network(), network.c_str()))
        systemStatus.network(network.c_str());
    systemStatus.ap_active(apActive);

    char timeBuf[12] = "";
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 5)) {
        strftime(timeBuf, sizeof(timeBuf), settings.isClock24hFormat() ? "%H:%M" : "%I:%M %p", &timeinfo);
        if (!settings.isClock24hFormat() && timeBuf[0] == '0')
            timeBuf[0] = ' ';
    }
    if (stringChanged(systemStatus.time(), timeBuf))
        systemStatus.time(timeBuf);
}

static void populateProfileInfo(ProfileInfoValue &info, const Profile &profile, bool isCurrent) {
    char timeBuf[12];
    formatDuration(static_cast<unsigned long>(profile.getTotalDuration() * 1000.0f), timeBuf, sizeof(timeBuf));
    if (stringChanged(info.name(), profile.label.c_str()))
        info.name(profile.label.c_str());
    info.temperature(profile.temperature);
    if (stringChanged(info.time(), timeBuf))
        info.time(timeBuf);
    info.phases(static_cast<int>(profile.getPhaseCount()));
    info.steps(static_cast<int>(profile.phases.size()));
    info.is_volumetric(profile.isVolumetric());
    info.is_current(isCurrent);
    info.target_weight(profile.getTotalVolume());
}

void DefaultUI::updateProfileInfo() {
    if (!initialized) {
        return;
    }
    populateProfileInfo(selectedProfileInfo, profileManager->getSelectedProfile(), true);
    selectedProfileInfo.dirty(profileDirty);

    // Preview backs the ProfileScreen carousel (index 0 = selected); hold the lock while
    // reading the vector — the profile task rebuilds it concurrently (GM-147).
    bool populated = false;
    {
        std::lock_guard<std::mutex> guard(profilesMutex);
        if (!favoritedProfiles.empty() && currentProfileIdx >= 0 &&
            currentProfileIdx < static_cast<int>(favoritedProfiles.size())) {
            populateProfileInfo(previewProfileInfo, favoritedProfiles[currentProfileIdx], currentProfileIdx == 0);
            populated = true;
        }
    }
    if (!populated) {
        populateProfileInfo(previewProfileInfo, profileManager->getSelectedProfile(), true);
    }
}

void DefaultUI::updateBoiler() {
    const ::Settings &settings = controller->getSettings();
    boiler.current_temperature(controller->getCurrentTemp());
    boiler.target_temperature(controller->getTargetTemp());
    boiler.current_pressure(pressure);
    boiler.target_pressure(controller->getTargetPressure());
    boiler.max_temperature(160.0f);
    boiler.max_pressure(settings.getPressureScaling());
}

// Mirror the live BrewProcess into brew_process_info; every field must stay valid/typed or the StatusScreen flow aborts.
void DefaultUI::updateBrewProcess() {
    if (!initialized) {
        return;
    }

    const Profile &selected = profileManager->getSelectedProfile();
    char buf[12];

    // Profile-derived defaults so the struct is valid even before a process runs.
    formatDuration(static_cast<unsigned long>(selected.getTotalDuration() * 1000.0f), buf, sizeof(buf));
    brewProcess.profile_temperature(selected.temperature);
    if (stringChanged(brewProcess.profile_time(), buf))
        brewProcess.profile_time(buf);
    brewProcess.profile_phases(static_cast<int>(selected.getPhaseCount()));
    brewProcess.profile_steps(static_cast<int>(selected.phases.size()));
    brewProcess.profile_is_volumetric(selected.isVolumetric());
    brewProcess.profile_is_current(true);
    brewProcess.profile_target_weight(selected.getTotalVolume());
    brewProcess.boiler_target_temperature(controller->getTargetTemp());

    // Hold the process lock across every deref below — the logic/AsyncTCP/BLE tasks delete
    // the process at any time (GM-147).
    std::lock_guard<std::recursive_mutex> guard(controller->getProcessLock());
    Process *process = controller->getProcess();
    if (process == nullptr) {
        process = controller->getLastProcess();
    }
    const bool validBrew = process != nullptr && process->getType() == MODE_BREW;
    if (!validBrew) {
        if (stringChanged(brewProcess.phase_type(), ""))
            brewProcess.phase_type("");
        if (stringChanged(brewProcess.phase_name(), ""))
            brewProcess.phase_name("");
        brewProcess.phase_value_current(0.0f);
        brewProcess.phase_value_target(0.0f);
        brewProcess.phase_value_is_weight(false);
        if (stringChanged(brewProcess.elapsed_time(), "0:00"))
            brewProcess.elapsed_time("0:00");
        brewProcess.elapsed_percentage(0.0f);
        brewProcess.is_complete(false);
        return;
    }

    auto *bp = static_cast<BrewProcess *>(process);
    if (bp->profile.phases.empty() || bp->phaseIndex >= bp->profile.phases.size()) {
        // Object is mid-mutation/invalid: keep the last valid values.
        return;
    }

    const Phase phase = bp->currentPhase;
    const bool active = process->isActive();

    // Live profile fields from the running process.
    formatDuration(bp->getTotalDuration(), buf, sizeof(buf));
    brewProcess.profile_temperature(bp->profile.temperature);
    if (stringChanged(brewProcess.profile_time(), buf))
        brewProcess.profile_time(buf);
    brewProcess.profile_phases(static_cast<int>(bp->profile.getPhaseCount()));
    brewProcess.profile_steps(static_cast<int>(bp->profile.phases.size()));
    brewProcess.profile_is_volumetric(bp->target == ProcessTarget::VOLUMETRIC);
    brewProcess.profile_target_weight(bp->getBrewVolume());
    brewProcess.boiler_target_temperature(bp->getTemperature());
    brewProcess.current_volume(bp->currentVolume);

    const char *phaseType = phase.phase == PhaseType::PHASE_TYPE_BREW ? "BREW" : "INFUSION";
    if (stringChanged(brewProcess.phase_type(), phaseType))
        brewProcess.phase_type(phaseType);

    String phaseName = "Finished";
    if (active) {
        phaseName = phase.name;
    } else if (controller->getSettings().isDelayAdjust() && !process->isComplete()) {
        phaseName = "Calibrating...";
    }
    if (stringChanged(brewProcess.phase_name(), phaseName.c_str()))
        brewProcess.phase_name(phaseName.c_str());

    unsigned long now = ::millis();
    if (!active && bp->finished > 0) {
        now = bp->finished;
    }
    const unsigned long elapsedMs = (bp->processStarted > 0 && now >= bp->processStarted) ? now - bp->processStarted : 0;
    formatDuration(elapsedMs, buf, sizeof(buf));
    if (stringChanged(brewProcess.elapsed_time(), buf))
        brewProcess.elapsed_time(buf);

    const bool weightTarget = bp->target == ProcessTarget::VOLUMETRIC && phase.hasVolumetricTarget();
    brewProcess.phase_value_is_weight(weightTarget);
    if (weightTarget) {
        const float target = phase.getVolumetricTarget().value;
        const float current = static_cast<float>(bp->currentVolume);
        brewProcess.phase_value_current(current);
        brewProcess.phase_value_target(target);
        brewProcess.elapsed_percentage(target > 0.0f ? clampPercentage(current / target * 100.0f) : 0.0f);
    } else {
        const unsigned long phaseElapsed =
            (bp->currentPhaseStarted > 0 && now >= bp->currentPhaseStarted) ? now - bp->currentPhaseStarted : 0;
        const float current = phaseElapsed / 1000.0f;
        const float target = bp->getPhaseDuration() / 1000.0f;
        brewProcess.phase_value_current(current);
        brewProcess.phase_value_target(target);
        brewProcess.elapsed_percentage(target > 0.0f ? clampPercentage(current / target * 100.0f) : 0.0f);
    }

    brewProcess.is_complete(process->isComplete());
}

void DefaultUI::updateMenuScreen() {}

String DefaultUI::getErrorMessage() {
    if (controller->isUpdating()) {
        return "Updating...";
    }
    if (controller->isAutotuning()) {
        return "Autotuning...";
    }
    if (controller->getSystemInfo().protocolMismatch) {
        return controller->getSystemInfo().protocolVersion > gm_proto::PROTOCOL_VERSION ? "Version mismatch, update display"
                                                                                        : "Version mismatch, update controller";
    }
    if (controller->isErrorState()) {
        switch (controller->getError()) {
        case ERROR_CODE_RUNAWAY:
            return "Temperature error, restart...";
        default:
            return "Unknown error";
        }
    }
    if (waitingForController) {
        return "Waiting for controller...";
    }
    return initialized ? "" : "Starting...";
}

void DefaultUI::applyTheme() {
    const ::Settings &settings = controller->getSettings();
    int newThemeMode = settings.getThemeMode();
#ifndef GAGGIMATE_SIM // Amoled-specific black theme override is device-only
    if (newThemeMode == 0 && panelDriver == AmoledDisplayDriver::getInstance()) {
        newThemeMode = THEME_ID_AMOLED_DARK;
    }
#endif

    // The element-tint override participates in the change key: enabling,
    // disabling, or recoloring it must re-run change_color_theme just like a
    // mode change. 0x1000000 is out of the 24-bit color range, so "disabled"
    // can never collide with a chosen color.
    const int tintKey = settings.getElementTintEnabled() ? settings.getElementTintColor() : 0x1000000;
    if (newThemeMode != currentThemeMode || tintKey != appliedTintKey) {
        currentThemeMode = newThemeMode;
        appliedTintKey = tintKey;
        // The generated screens read their accent (icons, accent text) from
        // theme_colors slot 0, both at create time and inside
        // change_color_theme's live re-apply — so patching that one slot IS
        // the custom-tint mechanism, and it survives EEZ regen because only
        // runtime memory is written. Pristine values are captured before the
        // first override so disabling the tint restores the theme's own
        // accent.
        static bool accentCaptured = false;
        static uint32_t themeAccent[sizeof(theme_colors) / sizeof(theme_colors[0])];
        constexpr int themeCount = sizeof(theme_colors) / sizeof(theme_colors[0]);
        if (!accentCaptured) {
            accentCaptured = true;
            for (int i = 0; i < themeCount; i++) {
                themeAccent[i] = theme_colors[i][0];
            }
        }
        if (currentThemeMode >= 0 && currentThemeMode < themeCount) {
            theme_colors[currentThemeMode][0] = settings.getElementTintEnabled()
                                                    ? static_cast<uint32_t>(settings.getElementTintColor())
                                                    : themeAccent[currentThemeMode];
        }
        change_color_theme(currentThemeMode);
        // Rest colors just changed under the pressed-feedback props; rewalk.
        pressedStyledRoot = nullptr;
        // change_color_theme just reassigned bg_color on every plate, so a
        // custom-coloured plate (mode 2) has silently reverted to the theme
        // colour while applyAnimPlates still believes it wrote the custom one.
        // Clearing the cache makes the next pass re-apply. The captures are
        // deliberately kept: mode 0 restores the generated plates by re-running
        // the theme, so a stale captured colour cannot outlive a restore.
        animPlateMode = -1;
    }
}

void DefaultUI::loopTask(void *arg) {
    auto *ui = static_cast<DefaultUI *>(arg);
    // The UI work and LVGL do not want the same cadence, and running them at
    // one rate charged the responsive half the price of the expensive half.
    // lv_task_handler() is where the touch controller is polled, so how often
    // it is called IS the input sampling rate -- LVGL cannot read the panel
    // more often than it is asked to run. At one call per 25 ms pass a tap
    // could sit unnoticed for most of a frame, on top of the indev timer's own
    // period, and the result was a screen that answered late. ui_tick() and the
    // widget updates, meanwhile, are worth doing only a few times a second.
    //
    // A refresh still only happens when something was invalidated, so the
    // faster handler costs nothing on a screen that is merely sitting there.
    constexpr unsigned long HANDLER_PERIOD_MS = 5;
    constexpr unsigned long UI_PERIOD_MS = 25;
    unsigned long lastUi = 0;
    while (true) {
        const unsigned long now = ::millis();
#ifdef GM_TOUCH_PROBE
        const int64_t probePass0 = esp_timer_get_time();
        g_statPassStartUs = probePass0;
#endif
        if (now - lastUi >= UI_PERIOD_MS) {
            lastUi = now;
            ui->loop();
        } else {
            lv_task_handler();
            // While the animation owns the panel, LVGL output only reaches the
            // screen through the overlay snapshot, and waiting for the next
            // ui->loop() pass added up to UI_PERIOD_MS to every touch response.
            // Publishing from here puts the snapshot on the same 5 ms cadence
            // as input; the refresh early-outs to one comparison when the
            // handler above drew nothing.
            ui->pumpSleepOverlay();
        }
#ifdef GM_TOUCH_PROBE
        {
            const int64_t passEnd = esp_timer_get_time();
            g_statPassEndUs = passEnd;
            g_statPassStartUs = 0;
            const int64_t passUs = passEnd - probePass0;
            g_uiPassN++;
            g_uiPassSum += passUs;
            if (passUs > g_uiPassMax)
                g_uiPassMax = passUs;
            if (esp_timer_get_time() - g_uiStatLastLog >= 5000000 && g_uiPassN > 0) {
                g_uiStatLastLog = esp_timer_get_time();
                ESP_LOGI("TouchProbe",
                         "GM_UISTAT: passes=%lu avg=%lld max=%lld us | refreshes=%lu snap avg=%lld max=%lld pub "
                         "avg=%lld max=%lld area avg=%lld max=%lld px | clear=%lld draw=%lld scan=%lld scrim=%lld"
                         " | meter=%lld mcalls=%lu ticks=%lu clip=%lu | ev=%lld/%lu sty=%lld/%lu img=%lld/%lu"
                         " rect=%lld/%lu rectr=%lld/%lu rmax=%lld lbl=%lld/%lu ln=%lld/%lu arc=%lld/%lu"
                         " | copy=%lld carea=%lld",
                         (unsigned long)g_uiPassN, (long long)(g_uiPassSum / g_uiPassN), (long long)g_uiPassMax,
                         (unsigned long)g_ovlN, (long long)(g_ovlN ? g_ovlSnapSum / g_ovlN : 0), (long long)g_ovlSnapMax,
                         (long long)(g_ovlN ? g_ovlPubSum / g_ovlN : 0), (long long)g_ovlPubMax,
                         (long long)(g_ovlN ? g_ovlAreaSum / g_ovlN : 0), (long long)g_ovlAreaMax,
                         (long long)(g_ovlN ? g_snapClearSum / g_ovlN : 0),
                         (long long)(g_ovlN ? g_snapDrawSum / g_ovlN : 0),
                         (long long)(g_ovlN ? g_statPubScanUs / g_ovlN : 0),
                         (long long)(g_ovlN ? g_statPubScrimUs / g_ovlN : 0),
                         (long long)(g_ovlN ? g_meterDrawUs / g_ovlN : 0), (unsigned long)g_meterDrawCalls,
                         (unsigned long)g_meterTicksDrawn, (unsigned long)g_meterTicksClipped,
                         (long long)(g_ovlN ? gm_ws_ev_us / g_ovlN : 0), (unsigned long)gm_ws_ev_calls,
                         (long long)(g_ovlN ? gm_ws_style_us / g_ovlN : 0), (unsigned long)gm_ws_style_calls,
                         (long long)(g_ovlN ? gm_ws_img_us / g_ovlN : 0), (unsigned long)gm_ws_img_calls,
                         (long long)(g_ovlN ? gm_ws_rect_us / g_ovlN : 0), (unsigned long)gm_ws_rect_calls,
                         (long long)(g_ovlN ? gm_ws_rectr_us / g_ovlN : 0), (unsigned long)gm_ws_rectr_calls,
                         (long long)gm_ws_rect_max_us,
                         (long long)(g_ovlN ? gm_ws_label_us / g_ovlN : 0), (unsigned long)gm_ws_label_calls,
                         (long long)(g_ovlN ? gm_ws_line_us / g_ovlN : 0), (unsigned long)gm_ws_line_calls,
                         (long long)(g_ovlN ? gm_ws_arc_us / g_ovlN : 0), (unsigned long)gm_ws_arc_calls,
                         (long long)(g_ovlN ? g_ovlCopySum / g_ovlN : 0), (long long)(g_ovlN ? g_ovlCopyArea / g_ovlN : 0));
                g_uiPassN = 0;
                g_uiPassSum = g_uiPassMax = 0;
                g_ovlN = 0;
                g_ovlSnapSum = g_ovlSnapMax = g_ovlPubSum = g_ovlPubMax = 0;
                g_ovlAreaSum = g_ovlAreaMax = 0;
                g_ovlCopySum = g_ovlCopyArea = 0;
                g_snapClearSum = g_snapDrawSum = 0;
                g_statPubScanUs = g_statPubScrimUs = 0;
                g_meterDrawUs = 0;
                g_meterDrawCalls = g_meterTicksDrawn = g_meterTicksClipped = 0;
                gm_ws_ev_us = gm_ws_style_us = gm_ws_img_us = 0;
                gm_ws_ev_calls = gm_ws_style_calls = gm_ws_img_calls = 0;
                gm_ws_rect_us = gm_ws_label_us = gm_ws_line_us = gm_ws_arc_us = 0;
                gm_ws_rect_calls = gm_ws_label_calls = gm_ws_line_calls = gm_ws_arc_calls = 0;
                gm_ws_rectr_us = gm_ws_rect_max_us = 0;
                gm_ws_rectr_calls = 0;
                {
                    // Sizing data for a possible internal-RAM LVGL arena:
                    // the live set the tree walk chases vs. the internal
                    // heap headroom that would have to absorb it.
                    GmLvMemStats m = gm_lv_mem_stats();
                    ESP_LOGI("TouchProbe",
                             "GM_LVMEM: live=%lu hwm=%lu n=%lu allocs=%lu | int_free=%u int_lgst=%u",
                             (unsigned long)m.liveBytes, (unsigned long)m.hwmBytes, (unsigned long)m.liveCount,
                             (unsigned long)m.allocCalls,
                             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
                }
            }
        }
#endif
        vTaskDelay(HANDLER_PERIOD_MS / portTICK_PERIOD_MS);
    }
}

void DefaultUI::profileLoopTask(void *arg) {
    auto *ui = static_cast<DefaultUI *>(arg);
    while (true) {
        ui->loopProfiles();
        vTaskDelay(25 / portTICK_PERIOD_MS);
    }
}
