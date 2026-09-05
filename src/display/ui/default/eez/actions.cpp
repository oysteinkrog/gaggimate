#include "actions.h"
#include "screens.h"
#include "ui.h"
#include <Arduino.h>
#include <display/main.h>
#include <display/plugins/BLEScalePlugin.h>

void action_on_wakeup(lv_event_t *e) {
    if (controller.isUpdating() || controller.isErrorState() || controller.isAutotuning() || !controller.isLinkUp()) {
        return;
    }
    controller.getUI()->changeScreen(SCREEN_ID_BREW_SCREEN);
    controller.deactivate();
    controller.setMode(MODE_BREW);
};

void action_on_load_started(lv_event_t *e) {

};

void action_on_menu_click(lv_event_t *e) {
    controller.deactivate();
    controller.getUI()->changeScreen(SCREEN_ID_MENU_SCREEN_NEW);
};

void action_on_brew_screen(lv_event_t *e) {
    controller.getUI()->changeScreen(SCREEN_ID_BREW_SCREEN);
    controller.deactivate();
    controller.setMode(MODE_BREW);
};

void action_on_steam_screen(lv_event_t *e) {
    controller.getUI()->changeScreen(SCREEN_ID_STEAM_SCREEN);
    controller.setMode(MODE_STEAM);
    controller.deactivate();
};

void action_on_water_screen(lv_event_t *e) {
    controller.getUI()->changeScreen(SCREEN_ID_WATER_SCREEN);
    controller.setMode(MODE_WATER);
    controller.deactivate();
};

void action_on_grind_screen(lv_event_t *e) {
    // The menu's grind slot doubles as the Scale screen when configured.
    if (controller.getSettings().isScaleMenuButton()) {
        controller.getUI()->openScaleScreen();
        return;
    }
    controller.getUI()->changeScreen(SCREEN_ID_GRIND_SCREEN);
    controller.setMode(MODE_GRIND);
    controller.deactivate();
};

void action_on_brew_start(lv_event_t *e) { controller.activate(); };

void action_on_flush(lv_event_t *e) { controller.onFlush(); };

void action_on_volumetric_hold(lv_event_t *e) {
    controller.getClientController()->tare();
    BLEScales.tare();
};

void action_on_profile_select(lv_event_t *e) { controller.getUI()->onProfileSwitch(); };

void action_on_profile_settings(lv_event_t *e) { controller.getUI()->changeBrewScreenMode(BrewScreenState::Settings); };

void action_on_brew_temp_lower(lv_event_t *e) {
    controller.getUI()->markProfileDirty();
    controller.lowerTemp();
};

void action_on_brew_temp_raise(lv_event_t *e) {
    controller.getUI()->markProfileDirty();
    controller.raiseTemp();
};

void action_on_brew_time_raise(lv_event_t *e) {
    controller.getUI()->markProfileDirty();
    controller.raiseBrewTarget();
};

void action_on_brew_time_lower(lv_event_t *e) {
    controller.getUI()->markProfileDirty();
    controller.lowerBrewTarget();
};

void action_on_volumetric_delete(lv_event_t *e) { controller.getUI()->onVolumetricDelete(); };

void action_on_profile_accept(lv_event_t *e) { controller.getUI()->changeBrewScreenMode(BrewScreenState::Brew); };

void action_on_profile_save(lv_event_t *e) {
    controller.onProfileSave();
    controller.getUI()->markProfileClean();
    controller.getUI()->changeBrewScreenMode(BrewScreenState::Brew);
};

void action_on_profile_save_as_new(lv_event_t *e) {

    controller.onProfileSaveAsNew();
    controller.getUI()->markProfileClean();
    controller.getUI()->changeBrewScreenMode(BrewScreenState::Brew);
};

// Repaint all of a dial meter's ticks as rounded pills/dots
#ifdef GM_TOUCH_PROBE
#include <esp_timer.h>
// Attribution for GM_UISTAT's draw= bucket (the whole lv_obj_redraw walk of a
// snapshot pass): how much of it is this handler, and how well the per-tick
// clip precheck is holding. Printed and reset by the UISTAT logger in
// DefaultUI.cpp on its 5 s cadence.
int64_t g_meterDrawUs = 0;
uint32_t g_meterDrawCalls = 0, g_meterTicksDrawn = 0, g_meterTicksClipped = 0;
#endif

static void gm_meter_draw_inner(lv_event_t *e);

void action_on_meter_draw(lv_event_t *e) {
#ifdef GM_TOUCH_PROBE
    const int64_t t0 = esp_timer_get_time();
    gm_meter_draw_inner(e);
    g_meterDrawUs += esp_timer_get_time() - t0;
    g_meterDrawCalls++;
#else
    gm_meter_draw_inner(e);
#endif
};

static void gm_meter_draw_inner(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    if (!lv_obj_check_type(obj, &lv_meter_class)) {
        return;
    }
    auto *meter = reinterpret_cast<lv_meter_t *>(obj);
    auto *scale = static_cast<lv_meter_scale_t *>(_lv_ll_get_head(&meter->scale_ll));
    if (scale == nullptr) {
        return;
    }
    const uint16_t cnt = scale->tick_major_nth; // original tick count stashed by suppressMeterTicks()
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    if (cnt < 2 || scale->tick_length == 0 || draw_ctx == nullptr) {
        return;
    }

    lv_area_t content;
    lv_obj_get_content_coords(obj, &content);
    const lv_coord_t r_edge = LV_MIN(lv_area_get_width(&content), lv_area_get_height(&content)) / 2;
    const lv_coord_t cx = content.x1 + r_edge;
    const lv_coord_t cy = content.y1 + r_edge;
    // Pull the ring in 2px so round cap/dot tips clear the meter's outer radius (else they look shaved).
    const lv_coord_t r_out = r_edge - 2;
    const lv_coord_t r_in = r_out - scale->tick_length;
    const lv_coord_t cap = scale->tick_width / 2;
    const bool pill = scale->tick_length > scale->tick_width; // collapses to a dot once shorter than wide

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    lv_obj_init_draw_line_dsc(obj, LV_PART_TICKS, &line_dsc);
    line_dsc.width = scale->tick_width;
    line_dsc.round_start = 1;
    line_dsc.round_end = 1;
    line_dsc.raw_end = 0;
    line_dsc.opa = LV_OPA_COVER;

    lv_draw_rect_dsc_t dot_dsc;
    lv_draw_rect_dsc_init(&dot_dsc);
    dot_dsc.radius = LV_RADIUS_CIRCLE;
    dot_dsc.bg_opa = LV_OPA_COVER;
    const float rad = scale->tick_length / 2.0f;
    const float cr = r_out - rad; // dot band centre
    const lv_coord_t ri = (lv_coord_t)lroundf(rad);
    constexpr float DEG2RAD = 3.14159265358979323846f / 180.0f;

    for (uint16_t i = 0; i < cnt; i++) {
        const float angle = ((float)i * scale->angle_range / (cnt - 1) + scale->rotation) * DEG2RAD;
        const float ux = cosf(angle);
        const float uy = sinf(angle);

        // Geometry before colour, so ticks outside the clip can be rejected
        // early. Partial invalidation keeps a value update down to a thin
        // sector of the ring; lv_draw_line/_rect do clip, but only after each
        // call has built its rounded-cap masks, and paying that for the ~95%
        // of ticks a sector excludes is what kept redraws meter-sized. The
        // 2px margin covers anti-aliasing bleed past the rounded coords.
        lv_point_t inner{}, outer{};
        lv_coord_t dx = 0, dy = 0;
        lv_area_t tickBox;
        if (pill) {
            // Round (not truncate) the coords so every tick lands evenly on the pixel grid.
            inner = {(lv_coord_t)lroundf(cx + ux * (r_in + cap)), (lv_coord_t)lroundf(cy + uy * (r_in + cap))};
            outer = {(lv_coord_t)lroundf(cx + ux * (r_out - cap)), (lv_coord_t)lroundf(cy + uy * (r_out - cap))};
            tickBox.x1 = (lv_coord_t)(LV_MIN(inner.x, outer.x) - cap - 2);
            tickBox.y1 = (lv_coord_t)(LV_MIN(inner.y, outer.y) - cap - 2);
            tickBox.x2 = (lv_coord_t)(LV_MAX(inner.x, outer.x) + cap + 2);
            tickBox.y2 = (lv_coord_t)(LV_MAX(inner.y, outer.y) + cap + 2);
        } else {
            dx = (lv_coord_t)lroundf(cx + ux * cr);
            dy = (lv_coord_t)lroundf(cy + uy * cr);
            tickBox = {(lv_coord_t)(dx - ri - 2), (lv_coord_t)(dy - ri - 2), (lv_coord_t)(dx + ri + 2),
                       (lv_coord_t)(dy + ri + 2)};
        }
        if (!_lv_area_is_on(&tickBox, draw_ctx->clip_area)) {
#ifdef GM_TOUCH_PROBE
            g_meterTicksClipped++;
#endif
            continue;
        }
#ifdef GM_TOUCH_PROBE
        g_meterTicksDrawn++;
#endif

        const int32_t value = lv_map(i, 0, cnt - 1, scale->min, scale->max);

        // SCALE_LINES indicators light up the ticks within their [start,end] range (the current level).
        lv_color_t color = scale->tick_color;
        for (auto *indic = static_cast<lv_meter_indicator_t *>(_lv_ll_get_tail(&meter->indicator_ll)); indic != nullptr;
             indic = static_cast<lv_meter_indicator_t *>(_lv_ll_get_prev(&meter->indicator_ll, indic))) {
            if (indic->type != LV_METER_INDICATOR_TYPE_SCALE_LINES)
                continue;
            if (value < indic->start_value || value > indic->end_value)
                continue;
            if (indic->type_data.scale_lines.color_start.full == indic->type_data.scale_lines.color_end.full) {
                color = indic->type_data.scale_lines.color_start;
            } else {
                const lv_opa_t ratio = indic->type_data.scale_lines.local_grad
                                           ? lv_map(value, indic->start_value, indic->end_value, LV_OPA_TRANSP, LV_OPA_COVER)
                                           : lv_map(value, scale->min, scale->max, LV_OPA_TRANSP, LV_OPA_COVER);
                color = lv_color_mix(indic->type_data.scale_lines.color_end, indic->type_data.scale_lines.color_start, ratio);
            }
        }

        if (pill) {
            line_dsc.color = color;
            lv_draw_line(draw_ctx, &line_dsc, &inner, &outer);
        } else {
            dot_dsc.bg_color = color;
            lv_area_t area = {(lv_coord_t)(dx - ri), (lv_coord_t)(dy - ri), (lv_coord_t)(dx + ri), (lv_coord_t)(dy + ri)};
            lv_draw_rect(draw_ctx, &dot_dsc, &area);
        }
    }
};

void action_on_steam_temp_lower(lv_event_t *e) { controller.lowerTemp(); };

void action_on_steam_temp_raise(lv_event_t *e) { controller.raiseTemp(); };

void action_on_grind_time_lower(lv_event_t *e) { controller.lowerGrindTarget(); };

void action_on_grind_time_raise(lv_event_t *e) { controller.raiseGrindTarget(); };

void action_on_timed_click(lv_event_t *e) {

};

void action_on_volumetric_click(lv_event_t *e) {
    controller.onTargetToggle();
    controller.getUI()->markDirty();
};

void action_on_grind_toggle(lv_event_t *e) {
    controller.isGrindActive() ? controller.deactivateGrind() : controller.activateGrind();
};

void action_on_simple_process_toggle(lv_event_t *e) {
    if (controller.getMode() != MODE_STEAM) {
        controller.isActive() ? controller.deactivate() : controller.activate();
    }
};

void action_on_profile_load(lv_event_t *e) { controller.getUI()->onProfileSelect(); };

void action_on_previous_profile(lv_event_t *e) { controller.getUI()->onPreviousProfile(); };

void action_on_next_profile(lv_event_t *e) { controller.getUI()->onNextProfile(); };

void action_on_brew_cancel(lv_event_t *e) {
    controller.deactivate();
    controller.clear();
}

void action_on_standby(lv_event_t *e) { controller.activateStandby(); }

// The pad only counts where LVGL will look: lv_indev_search_obj descends
// into a container's children only for points inside the container's own
// coords, unless the container has LV_OBJ_FLAG_OVERFLOW_VISIBLE. The
// generated screens wrap most buttons in rows sized to the icons (a 50 px
// value row, a 45 px save row), so a 15 px pad on a 40 px button used to be
// a 50 px band: every ancestor short of the screen gets the flag. Measured
// with tools/touchmap.py, which reports the effective rectangles.
void applyClickArea(lv_obj_t *obj, lv_coord_t size) {
    if (obj == nullptr) {
        return;
    }
    lv_obj_set_ext_click_area(obj, size);
    for (lv_obj_t *p = lv_obj_get_parent(obj); p != nullptr && lv_obj_get_parent(p) != nullptr; p = lv_obj_get_parent(p)) {
        lv_obj_add_flag(p, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    }
}

// Stop lv_meter drawing its own ticks (action_on_meter_draw takes over): stash the design count in the
// unused tick_major_nth, then zero the live count. Once per meter; self-cleans if EEZ recreates the screen.
static void suppressMeterTicks(lv_obj_t *obj) {
    const uint32_t n = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *child = lv_obj_get_child(obj, i);
        if (lv_obj_check_type(child, &lv_meter_class)) {
            auto *meter = reinterpret_cast<lv_meter_t *>(child);
            auto *scale = static_cast<lv_meter_scale_t *>(_lv_ll_get_head(&meter->scale_ll));
            if (scale != nullptr && scale->tick_cnt > 0) {
                scale->tick_major_nth = scale->tick_cnt;
                scale->tick_cnt = 0;
            }
            // The default theme dresses every meter in the card style: a grey
            // border ring under a full-circle radius mask. The screens zero
            // bg_opa but not the border, so each meter redraw still paid the
            // circle-mask border pass (measured 2-21 ms per lv_draw_rect call,
            // ~22 ms of a ~68 ms walk per refresh) for a ring that lies in the
            // round panel's invisible corner region: the 500 px dial overhangs
            // the 480 px display everywhere the ring would show.
            lv_obj_set_style_border_width(child, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_outline_width(child, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        suppressMeterTicks(child);
    }
}

// Hit areas, in panel pixels (0.111 mm/px on the 2.1" panel). Where two
// pads overlap, the object created later wins (lv_indev_search_obj walks
// children last to first), so a pad may only grow into a neighbour whose
// action it is acceptable to lose that sliver to.
//
// The exit chevron sits at the bottom edge of the round glass (rows 430 to
// 469), where the finger lands high and partly over the bezel: its box is
// 175..304 x 385..479, of which the start/pause/select button above keeps
// 195..284 x 385..414 (its own pad, created later), exactly what it kept
// before. The menu's power button gets less (30): with 45 its box would
// take the bottom-inner corners of the grind and water tiles' pads, and a
// standby by mis-tap is worse than a missed one.
void action_on_screen_load(lv_event_t *e) {
    suppressMeterTicks(lv_event_get_target(e));
    applyClickArea(objects.select_profile, 30);
    applyClickArea(objects.previous_profile, 30);
    applyClickArea(objects.next_profile, 30);
    applyClickArea(objects.btn_brew_1, 15);
    applyClickArea(objects.btn_steam_1, 15);
    applyClickArea(objects.btn_water_1, 15);
    applyClickArea(objects.btn_grind_1, 15);
    applyClickArea(objects.btn_settings_1, 15);
    applyClickArea(objects.info_btn, 15);
    applyClickArea(objects.menu_dials__standby_icon, 30);
    applyClickArea(objects.standby_btn, 30);
    applyClickArea(objects.brew_dials__menu_icon, 45);
    applyClickArea(objects.status_dials__menu_icon, 45);
    applyClickArea(objects.steam_dials__menu_icon, 45);
    applyClickArea(objects.water_dials__menu_icon, 45);
    applyClickArea(objects.grind_dials__menu_icon, 45);
    applyClickArea(objects.profile_dials__menu_icon, 45);
    applyClickArea(objects.info_menu_icon, 45);
    applyClickArea(objects.start_button, 25);
    applyClickArea(objects.water_start_button, 25);
    applyClickArea(objects.grind_start_button, 25);
    applyClickArea(objects.profile_select_button, 25);
    applyClickArea(objects.settings_button, 25);
    applyClickArea(objects.up_duration_button, 15);
    applyClickArea(objects.down_duration_button, 15);
    applyClickArea(objects.up_weight_button, 15);
    applyClickArea(objects.down_weight_button, 15);
    // 20 px right of the weight "+": 10 keeps the boundary between them.
    applyClickArea(objects.remove_volumetric_button, 10);
    applyClickArea(objects.up_temp_button, 15);
    applyClickArea(objects.down_temp_button, 15);
    applyClickArea(objects.water_up_temp_button, 25);
    applyClickArea(objects.water_down_temp_button, 25);
    applyClickArea(objects.steam_up_temp_button, 25);
    applyClickArea(objects.steam_down_temp_button, 25);
    applyClickArea(objects.grind_up_duration_button, 15);
    applyClickArea(objects.grind_down_duration_button, 15);
    applyClickArea(objects.grind_up_weight_button, 15);
    applyClickArea(objects.grind_down_weight_button, 15);
    applyClickArea(objects.pause_button, 25);
    applyClickArea(objects.check_button, 25);
    applyClickArea(objects.accept_button, 25);
    applyClickArea(objects.save_as_new_button, 25);
    applyClickArea(objects.save_button, 25);
    // The profile name beside the profile button reads as the thing to tap
    // to change profiles; make it one. Labels are not clickable by default,
    // so the flag doubles as the once-per-object guard against stacking the
    // callback on every screen load (the flow engine recreates screens).
    if (objects.profile_name != nullptr && !lv_obj_has_flag(objects.profile_name, LV_OBJ_FLAG_CLICKABLE)) {
        lv_obj_add_flag(objects.profile_name, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(objects.profile_name, action_on_profile_select, LV_EVENT_CLICKED, nullptr);
        applyClickArea(objects.profile_name, 15);
    }
    // The grind screen's weight/time pill is one target, like the brew
    // screen's, but its icon is an lv_imgbtn (clickable by default) rather
    // than an lv_img: a tap on the icon landed on the icon, whose handler
    // does nothing, instead of on the pill.
    if (objects.obj24 != nullptr) {
        lv_obj_clear_flag(objects.obj24, LV_OBJ_FLAG_CLICKABLE);
    }
}

void action_on_screen_swipe(lv_event_t *e) {
    lv_event_code_t event_code = lv_event_get_code(e);

    if (event_code == LV_EVENT_GESTURE) {
        if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP) {
            lv_indev_wait_release(lv_indev_get_act());
            action_on_menu_click(e);
        } else if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT &&
                   eez_flow_get_current_screen() == SCREEN_ID_PROFILE_SCREEN) {
            lv_indev_wait_release(lv_indev_get_act());
            action_on_previous_profile(e);
        } else if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_LEFT &&
                   eez_flow_get_current_screen() == SCREEN_ID_PROFILE_SCREEN) {
            lv_indev_wait_release(lv_indev_get_act());
            action_on_next_profile(e);
        }
    }
}

void action_on_info_screen(lv_event_t *e) { controller.getUI()->changeScreen(SCREEN_ID_INFO_SCREEN); }
