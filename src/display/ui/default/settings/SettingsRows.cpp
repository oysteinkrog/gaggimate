#include "SettingsRows.h"
#include <display/drivers/common/LV_Helper.h>

#include <display/core/Controller.h>
#include <display/core/Settings.h>
#include <display/ui/default/eez/eez-flow.h>
#include <display/ui/default/eez/images.h>
#include <display/ui/default/eez/screens.h>

#include <cstdio>
#include <cstring>

namespace {

lv_color_t themeFg() { return lv_color_hex(theme_colors[eez_flow_get_selected_theme_index()][0]); }

// ---- generic helpers --------------------------------------------------------

// Depth-first search for the first descendant of obj whose SettingsDebugTag
// carries this role. user_data on every object under the settings cover is
// either null or a valid SettingsDebugTag* (DefaultUI.cpp's touchMapNode
// reads it unconditionally, for every descendant of the cover, whether or
// not the caller meant to tag it), so nothing in this file may point a row
// object's own user_data at anything else; the per-row runtime state below
// travels through each event's own user_data (lv_event_get_user_data), a
// separate mechanism from lv_obj_set_user_data/lv_obj_get_user_data.
lv_obj_t *findByRole(lv_obj_t *obj, const char *role) {
    const uint32_t n = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *child = lv_obj_get_child(obj, i);
        if (const auto *tag = static_cast<const SettingsDebugTag *>(lv_obj_get_user_data(child))) {
            if (strcmp(tag->role, role) == 0) {
                return child;
            }
        }
        if (lv_obj_t *found = findByRole(child, role)) {
            return found;
        }
    }
    return nullptr;
}

void applyEnabledRecurse(lv_obj_t *obj, bool enabled) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE)) {
        if (enabled) {
            lv_obj_clear_state(obj, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(obj, LV_STATE_DISABLED);
        }
    }
    // A clickable frame loses its border through the LV_STATE_DISABLED
    // selector styleFrame sets. A bordered object that is not a target (the
    // toggle's switch outline) is a cue, not a frame, and dims with the text
    // instead; opa multiplies down the tree, so its knob dims with it.
    const bool cue = !lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE) && lv_obj_get_style_border_width(obj, LV_PART_MAIN) > 0;
    if (obj->class_p == &lv_label_class || obj->class_p == &lv_img_class || cue) {
        lv_obj_set_style_opa(obj, enabled ? LV_OPA_COVER : LV_OPA_50, LV_PART_MAIN);
    }
    const uint32_t n = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < n; i++) {
        applyEnabledRecurse(lv_obj_get_child(obj, i), enabled);
    }
}

// Fills the 320x56 slot, transparent, bubbling (every clickable descendant
// needs the same flag, and so does every container between it and this one,
// so a press anywhere in the row still reaches the cover's PRESSED handler,
// the only thing that calls controller->updateLastAction() while settings
// is open).
lv_obj_t *createRowContainer(lv_obj_t *parent) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, SettingsUI::kRowW, SettingsUI::kRowH);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    // lv_obj_create defaults to clickable (obj->flags = LV_OBJ_FLAG_CLICKABLE
    // at construction, lv_obj.c's object constructor). Every row here gets
    // an LV_EVENT_DELETE callback to free its ctx, which is enough for the
    // touchmap audit's targets() to count a clickable object with a
    // callback as a tap target; left on, a stepper, choice or info row
    // would audit as a target the size of its own child buttons, always
    // overlapping them. Toggle, action, confirm and locked re-add this
    // explicitly, since for those the whole row is meant to be one target.
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    // lv_obj's constructor also sets LV_OBJ_FLAG_PRESS_LOCK on every child
    // object, which makes the indev keep the pressed object even when the
    // finger leaves it; a hold could then never be cancelled by PRESS_LOST
    // (sliding off, or the row being disabled mid-hold). Cleared here and in
    // makeFramedTarget so every target here re-hit-tests on each poll.
    lv_obj_clear_flag(row, LV_OBJ_FLAG_PRESS_LOCK);
    // A control's frame sits 2 px inside its 56 px hit box and takes the
    // difference back as ext click pad (styleFrame), and the outermost
    // button's box ends exactly on this row's edge; a framed row is itself
    // 4 px smaller than its slot. Without this flag those pads would be
    // clipped on the outward side (eez/actions.cpp's applyClickArea works
    // around the same clipping the same way, on every ancestor short of the
    // screen).
    lv_obj_add_flag(row, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    return row;
}

// ---- row geometry (gm-3vj.50) ------------------------------------------
//
// Every target draws a 2 px frame with no fill, kFrameInset inside its hit
// box, and gets the inset back as ext click pad: a 56x56 button is a 52x52
// object, a framed row a 316x52 object in its 320x56 slot. The slots are
// stacked with no gap, so the inset is what leaves 4 px between two
// neighbouring frames. Plain rows (stepper, choice, locked, info) keep no
// frame of their own; their text column is plain and each control is its
// own framed button. Cost in overlay pixels, computed from the outlines and
// not yet measured on the bench: about 400 px per 52x52 frame and 1,450 per
// framed row.
constexpr lv_coord_t kFrameInset = 2;
constexpr lv_coord_t kFrameBorder = 2;
constexpr lv_coord_t kFrameRadius = 8;
constexpr lv_coord_t kBtnHit = 56;
constexpr lv_coord_t kBtnGap = 4;           // between two neighbouring hit boxes
constexpr lv_coord_t kTextX = 12;           // text column's left edge in the 320 px slot
// Two buttons at the right of a plain row: plus's box ends on the row edge.
constexpr lv_coord_t kPlusHitX = SettingsUI::kRowW - kBtnHit;          // 264
constexpr lv_coord_t kMinusHitX = kPlusHitX - kBtnGap - kBtnHit;       // 204
constexpr lv_coord_t kTextColW = kMinusHitX - kBtnGap - kTextX;        // 188
// A framed row's content area starts kFrameInset + kFrameBorder in.
constexpr lv_coord_t kFramedRowW = SettingsUI::kRowW - 2 * kFrameInset; // 316
constexpr lv_coord_t kFramedRowH = SettingsUI::kRowH - 2 * kFrameInset; // 52
constexpr lv_coord_t kFramedContentW = kFramedRowW - 2 * kFrameBorder;  // 312
constexpr lv_coord_t kFramedContentH = kFramedRowH - 2 * kFrameBorder;  // 48
constexpr lv_coord_t kFramedTextX = kTextX - kFrameInset - kFrameBorder; // 8, same 12 px on screen
constexpr lv_coord_t kFramedTextColW = 220;
constexpr lv_coord_t kCueRightPad = 10;

// Label (Montserrat 18) above value (Montserrat 20), left-aligned, both
// truncating with dots as a last resort, placed at x in the parent's
// content area and centred vertically.
struct TextCol {
    lv_obj_t *label;
    lv_obj_t *value;
};

// Shared body for the two buildTextCol overloads below: builds the label
// and value column without positioning it.
TextCol buildTextColUnpositioned(lv_obj_t *parent, const char *labelText, lv_coord_t width, lv_coord_t height) {
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, width, height);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE); // see createRowContainer: lv_obj_create defaults this on
    lv_obj_add_flag(col, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    const lv_color_t fg = themeFg();

    lv_obj_t *label = lv_label_create(col);
    lv_label_set_text(label, labelText);
    lv_obj_set_width(label, width);
    // LV_LABEL_LONG_DOT keeps the label's own size and dots the overflow;
    // a label defaults to LV_SIZE_CONTENT height, which has no fixed size
    // to keep, so it wraps to a second line instead of dotting (found by
    // rendering "America/Argentina/ComodRivadavia": the value label grew to
    // two lines rather than truncating). Pinning height to one line of the
    // font is what makes LONG_DOT truncate instead of wrap.
    lv_obj_set_height(label, lv_font_get_line_height(&lv_font_montserrat_18));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, fg, LV_PART_MAIN);

    lv_obj_t *value = lv_label_create(col);
    lv_label_set_text(value, "");
    lv_obj_set_width(value, width);
    lv_obj_set_height(value, lv_font_get_line_height(&lv_font_montserrat_20)); // see label above
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(value, fg, LV_PART_MAIN);

    return {label, value};
}

// The height the two lines need with nothing around them, for a caller that
// puts something else in the same column (the stepping swatch row's ramp bar).
lv_coord_t textColContentH() {
    return lv_font_get_line_height(&lv_font_montserrat_18) + lv_font_get_line_height(&lv_font_montserrat_20);
}

// Left-aligned at x inside a plain (non-flex) row: every stepper, choice,
// framed and info row.
TextCol buildTextCol(lv_obj_t *parent, const char *labelText, lv_coord_t x, lv_coord_t width, lv_coord_t height) {
    TextCol col = buildTextColUnpositioned(parent, labelText, width, height);
    lv_obj_align(lv_obj_get_parent(col.label), LV_ALIGN_LEFT_MID, x, 0);
    return col;
}

// Positioned by the caller's own flex layout instead (the swatch and
// step-band rows place the column themselves).
TextCol buildTextCol(lv_obj_t *parent, const char *labelText, lv_coord_t width, lv_coord_t height = SettingsUI::kRowH) {
    return buildTextColUnpositioned(parent, labelText, width, height);
}

// Recolours a target and everything drawn inside it: text, icons and
// borders. The knob of a switch and a progress bar keep their colour.
void recolorTree(lv_obj_t *obj, lv_color_t c) {
    if (obj->class_p == &lv_label_class) {
        lv_obj_set_style_text_color(obj, c, LV_PART_MAIN);
    } else if (obj->class_p == &lv_img_class) {
        lv_obj_set_style_img_recolor(obj, c, LV_PART_MAIN);
    }
    if (lv_obj_get_style_border_width(obj, LV_PART_MAIN) > 0) {
        lv_obj_set_style_border_color(obj, c, LV_PART_MAIN);
    }
    const uint32_t n = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < n; i++) {
        recolorTree(lv_obj_get_child(obj, i), c);
    }
}

// Press feedback for every settings target. They are plain lv_obj, not
// lv_btn or a clickable lv_img, so DefaultUI::applyPressedFeedbackTo's walk
// does nothing for them, and LVGL puts PRESSED on the target only, never on
// its children: the frame and what is inside it are recoloured by hand. The
// pressed colour (settingsPressedColor, the 40% rule) travels packed in the
// event's user data, so no allocation outlives the object; the rest colour
// is the theme colour every frame is built with (a theme change rebuilds
// the page). A press dims only while the compositor's plate is not doing
// the job (g_pressPlateActive); a release always restores.
void framePressEvent(lv_event_t *e) {
    const lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) {
        return;
    }
    lv_obj_t *self = lv_event_get_current_target(e);
    if (lv_event_get_target(e) != self) {
        return; // bubbled up from a child target
    }
    const auto packed = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    const lv_color_t c = (code == LV_EVENT_PRESSED && !g_pressPlateActive) ? lv_color_hex(packed) : themeFg();
    recolorTree(self, c);
}

lv_color_t touchDimColor(SettingsUI &ui) {
    return lv_color_hex(static_cast<uint32_t>(ui.controller().getSettings().getTouchDimColor()));
}

// Makes obj a framed target: border, ext click pad, clickable, bubbling,
// pressed recolour. The caller sizes it kFrameInset smaller than its hit
// box on every side.
void makeFramedTarget(SettingsUI &ui, lv_obj_t *obj) {
    const lv_color_t fg = themeFg();
    settingsStyleFrame(obj, fg);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_PRESS_LOCK); // see createRowContainer
    const lv_color_t pressed = settingsPressedColor(fg, touchDimColor(ui));
    lv_obj_add_event_cb(obj, framePressEvent, LV_EVENT_ALL,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(lv_color_to32(pressed) & 0xFFFFFFu)));
}

// A right-hand cue on a framed row: Montserrat 16 text, right-aligned.
lv_obj_t *buildCueText(lv_obj_t *row, const char *text) {
    lv_obj_t *cue = lv_label_create(row);
    lv_label_set_text(cue, text);
    lv_obj_set_style_text_font(cue, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(cue, themeFg(), LV_PART_MAIN);
    lv_obj_align(cue, LV_ALIGN_RIGHT_MID, -kCueRightPad, 0);
    return cue;
}

// The framed row kinds (toggle, action, confirm): the row object is the
// target and the frame, kFrameInset inside its slot.
lv_obj_t *createFramedRow(SettingsUI &ui, lv_obj_t *parent) {
    lv_obj_t *row = createRowContainer(parent);
    lv_obj_set_size(row, kFramedRowW, kFramedRowH);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, 0);
    makeFramedTarget(ui, row);
    return row;
}

// A framed control in a plain row, its 56 px tall hit box starting at hitX
// and filling the row's height.
lv_obj_t *buildRowButton(SettingsUI &ui, lv_obj_t *row, lv_coord_t hitX, const lv_img_dsc_t *icon,
                         lv_coord_t hitW = kBtnHit, const char *text = nullptr) {
    lv_obj_t *btn = settingsFrameButtonCreate(ui, row, hitW, icon, text);
    lv_obj_set_pos(btn, hitX + kFrameInset, kFrameInset);
    return btn;
}

} // namespace

lv_color_t settingsPressedColor(lv_color_t rest, lv_color_t dim) { return lv_color_mix(dim, rest, LV_OPA_40); }

void settingsStyleFrame(lv_obj_t *obj, lv_color_t fg) {
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, kFrameBorder, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, fg, LV_PART_MAIN);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_radius(obj, kFrameRadius, LV_PART_MAIN);
    lv_obj_set_ext_click_area(obj, kFrameInset);
}

lv_obj_t *settingsFrameButtonCreate(SettingsUI &ui, lv_obj_t *parent, lv_coord_t hitW, const lv_img_dsc_t *icon,
                                    const char *text) {
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, hitW - 2 * kFrameInset, kBtnHit - 2 * kFrameInset);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    makeFramedTarget(ui, btn);
    const lv_color_t fg = themeFg();
    if (icon != nullptr) {
        lv_obj_t *img = lv_img_create(btn);
        lv_img_set_src(img, icon);
        lv_obj_set_style_img_recolor(img, fg, LV_PART_MAIN);
        lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(img, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_center(img);
    }
    if (text != nullptr) {
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(label, fg, LV_PART_MAIN);
        lv_obj_add_flag(label, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_center(label);
    }
    return btn;
}

namespace {

// ---- press-and-repeat button (stepper minus/plus, choice prev/next) --------

// onFire(user, dir, fast) once on PRESSED (fast always false) and once per
// LONG_PRESSED_REPEAT while held (fast true once the hold has run
// kSettingsRowFastHoldMs). Choice buttons reuse this through an adapter that
// drops fast; only a stepper's own callback ever sees fast=true. All state
// (pressedAtMs) lives in the slot LVGL hands back on every event for this
// button, never in a timer: RELEASED/PRESS_LOST need no handling here
// because nothing is derived except from PRESSED and REPEAT timestamps.
struct RepeatBtn {
    SettingsRowStepFn onFire = nullptr;
    void *user = nullptr;
    int dir = 0;
    uint32_t pressedAtMs = 0;
};

void repeatBtnEvent(lv_event_t *e) {
    // Same exposure as lockedBtnEvent: registered for LV_EVENT_ALL, and the
    // button's own DELETE arrives after the row's DELETE freed the ctx that
    // owns this slot. Check the code before touching the slot.
    const lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_LONG_PRESSED_REPEAT) {
        return;
    }
    auto *btn = static_cast<RepeatBtn *>(lv_event_get_user_data(e));
    switch (code) {
    case LV_EVENT_PRESSED:
        btn->pressedAtMs = lv_tick_get();
        if (btn->onFire != nullptr) {
            btn->onFire(btn->user, btn->dir, false);
        }
        break;
    case LV_EVENT_LONG_PRESSED_REPEAT: {
        const bool fast = lv_tick_elaps(btn->pressedAtMs) >= kSettingsRowFastHoldMs;
        if (btn->onFire != nullptr) {
            btn->onFire(btn->user, btn->dir, fast);
        }
        break;
    }
    default:
        break;
    }
}

void wireRepeatBtn(lv_obj_t *btn, RepeatBtn *slot) { lv_obj_add_event_cb(btn, repeatBtnEvent, LV_EVENT_ALL, slot); }

} // namespace

// ---- shared row API ---------------------------------------------------------

void settingsRowSetValue(lv_obj_t *row, const char *value) {
    if (row == nullptr || value == nullptr) {
        return;
    }
    lv_obj_t *valueObj = findByRole(row, "value");
    if (valueObj == nullptr) {
        return;
    }
    const auto *tag = static_cast<const SettingsDebugTag *>(lv_obj_get_user_data(valueObj));
    if (tag == nullptr || tag->text == nullptr) {
        return;
    }
    // The tag's text pointer is const for readers (the touchmap dump); this
    // row owns the buffer it points at (settingsRowStepperCreate and its
    // siblings hand ui.tag() the same address) and is the one place allowed
    // to write through it.
    char *buf = const_cast<char *>(tag->text);
    snprintf(buf, kSettingsRowValueCap, "%s", value);
    lv_label_set_text(valueObj, buf);
}

void settingsRowSetEnabled(lv_obj_t *row, bool enabled) {
    if (row == nullptr) {
        return;
    }
    applyEnabledRecurse(row, enabled);
    // No separate hold-cancellation step: lv_obj_hit_test refuses a
    // LV_STATE_DISABLED object, and indev_proc_press re-searches for a hit
    // on every poll for objects without LV_OBJ_FLAG_PRESS_LOCK (the LVGL
    // default is on; createRowContainer and makeFramedTarget clear it), so
    // disabling mid-hold sends the held button a PRESS_LOST on the very
    // next poll, same as if the finger had slid off it. Every hold
    // handler in this file resets its own progress on PRESS_LOST, so
    // re-enabling afterwards starts clean: nothing here can wake up
    // mid-progress.
}

// ---- stepper -----------------------------------------------------------------

namespace {

struct StepperCtx {
    char value[kSettingsRowValueCap] = {0};
    RepeatBtn minus;
    RepeatBtn plus;
};

} // namespace

lv_obj_t *settingsRowStepperCreate(SettingsUI &ui, lv_obj_t *parent, const char *rowName, const char *label,
                                    SettingsRowStepFn onStep, void *user) {
    lv_obj_t *row = createRowContainer(parent);

    auto *ctx = new StepperCtx();

    TextCol col = buildTextCol(row, label, kTextX, kTextColW, SettingsUI::kRowH);
    ui.tag(col.value, rowName, "value", ctx->value);

    lv_obj_t *minusBtn = buildRowButton(ui, row, kMinusHitX, &img_minus_small_40x40);
    ctx->minus = {onStep, user, -1, 0};
    wireRepeatBtn(minusBtn, &ctx->minus);
    ui.tag(minusBtn, rowName, "minus");

    lv_obj_t *plusBtn = buildRowButton(ui, row, kPlusHitX, &img_plus_small_40x40);
    ctx->plus = {onStep, user, +1, 0};
    wireRepeatBtn(plusBtn, &ctx->plus);
    ui.tag(plusBtn, rowName, "plus");

    lv_obj_add_event_cb(
        row, [](lv_event_t *e) { delete static_cast<StepperCtx *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, ctx);
    ui.tag(row, rowName, "row");
    return row;
}

// ---- choice ------------------------------------------------------------------

namespace {

struct ChoiceCtx {
    char value[kSettingsRowValueCap] = {0};
    SettingsRowCycleFn onCycle = nullptr;
    void *user = nullptr;
    RepeatBtn prev;
    RepeatBtn next;
};

// Adapts a RepeatBtn (SettingsRowStepFn shape) onto onCycle, dropping fast:
// a choice arrow repeats while held but has no fast tier.
void choiceStepAdapter(void *userCtx, int dir, bool /*fast*/) {
    auto *cc = static_cast<ChoiceCtx *>(userCtx);
    if (cc->onCycle != nullptr) {
        cc->onCycle(cc->user, dir);
    }
}

} // namespace

lv_obj_t *settingsRowChoiceCreate(SettingsUI &ui, lv_obj_t *parent, const char *rowName, const char *label,
                                   SettingsRowCycleFn onCycle, void *user) {
    lv_obj_t *row = createRowContainer(parent);

    auto *ctx = new ChoiceCtx();
    ctx->onCycle = onCycle;
    ctx->user = user;

    TextCol col = buildTextCol(row, label, kTextX, kTextColW, SettingsUI::kRowH);
    ui.tag(col.value, rowName, "value", ctx->value);

    lv_obj_t *prevBtn = buildRowButton(ui, row, kMinusHitX, &img_angle_left_40x40);
    ctx->prev = {choiceStepAdapter, ctx, -1, 0};
    wireRepeatBtn(prevBtn, &ctx->prev);
    ui.tag(prevBtn, rowName, "prev");

    lv_obj_t *nextBtn = buildRowButton(ui, row, kPlusHitX, &img_angle_right_40x40);
    ctx->next = {choiceStepAdapter, ctx, +1, 0};
    wireRepeatBtn(nextBtn, &ctx->next);
    ui.tag(nextBtn, rowName, "next");

    lv_obj_add_event_cb(
        row, [](lv_event_t *e) { delete static_cast<ChoiceCtx *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, ctx);
    ui.tag(row, rowName, "row");
    return row;
}

// ---- toggle --------------------------------------------------------------

namespace {

struct ToggleCtx {
    char value[kSettingsRowValueCap] = {0};
    lv_obj_t *valueLabel = nullptr;
    lv_obj_t *knob = nullptr;
    SettingsRowToggleFn onToggle = nullptr;
    void *user = nullptr;
    bool on = false;
};

// The switch outline at the right of a toggle row: a 44x24 track with no
// fill and a 14 px knob that sits left for Off and right for On.
constexpr lv_coord_t kSwitchW = 44;
constexpr lv_coord_t kSwitchH = 24;
constexpr lv_coord_t kKnob = 14;
constexpr lv_coord_t kKnobPad = 3;

// The toggle widget owns its own "On"/"Off" text (unlike every other kind's
// value, which the caller formats): the mapping is fixed, not model-specific.
// The switch knob follows the same state.
void toggleSetText(ToggleCtx *ctx) {
    snprintf(ctx->value, sizeof(ctx->value), "%s", ctx->on ? "On" : "Off");
    lv_label_set_text(ctx->valueLabel, ctx->value);
    lv_obj_align(ctx->knob, ctx->on ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, ctx->on ? -kKnobPad : kKnobPad, 0);
}

void toggleEvent(lv_event_t *e) {
    auto *ctx = static_cast<ToggleCtx *>(lv_event_get_user_data(e));
    switch (lv_event_get_code(e)) {
    case LV_EVENT_CLICKED:
        ctx->on = !ctx->on;
        toggleSetText(ctx);
        if (ctx->onToggle != nullptr) {
            ctx->onToggle(ctx->user, ctx->on);
        }
        break;
    default:
        break;
    }
}

} // namespace

lv_obj_t *settingsRowToggleCreate(SettingsUI &ui, lv_obj_t *parent, const char *rowName, const char *label,
                                   bool initial, SettingsRowToggleFn onToggle, void *user) {
    const lv_color_t fg = themeFg();

    lv_obj_t *row = createFramedRow(ui, parent);

    auto *ctx = new ToggleCtx();
    ctx->onToggle = onToggle;
    ctx->user = user;
    ctx->on = initial;

    TextCol col = buildTextCol(row, label, kFramedTextX, kFramedTextColW, kFramedContentH);
    ctx->valueLabel = col.value;

    lv_obj_t *track = lv_obj_create(row);
    lv_obj_remove_style_all(track);
    lv_obj_clear_flag(track, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE); // a cue, not a target
    lv_obj_add_flag(track, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(track, kSwitchW, kSwitchH);
    lv_obj_align(track, LV_ALIGN_RIGHT_MID, -kCueRightPad, 0);
    lv_obj_set_style_border_width(track, kFrameBorder, LV_PART_MAIN);
    lv_obj_set_style_border_color(track, fg, LV_PART_MAIN);
    lv_obj_set_style_border_opa(track, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(track, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_t *knob = lv_obj_create(track);
    lv_obj_remove_style_all(knob);
    lv_obj_clear_flag(knob, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(knob, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(knob, kKnob, kKnob);
    lv_obj_set_style_radius(knob, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(knob, fg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(knob, LV_OPA_COVER, LV_PART_MAIN);
    ctx->knob = knob;
    toggleSetText(ctx);
    ui.tag(col.value, rowName, "value", ctx->value);

    lv_obj_add_event_cb(row, toggleEvent, LV_EVENT_ALL, ctx);
    lv_obj_add_event_cb(
        row, [](lv_event_t *e) { delete static_cast<ToggleCtx *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, ctx);
    ui.tag(row, rowName, "toggle");
    return row;
}

// ---- action ------------------------------------------------------------------

namespace {

struct ActionCtx {
    char value[kSettingsRowValueCap] = {0};
    SettingsRowActivateFn onActivate = nullptr;
    void *user = nullptr;
};

void actionEvent(lv_event_t *e) {
    auto *ctx = static_cast<ActionCtx *>(lv_event_get_user_data(e));
    switch (lv_event_get_code(e)) {
    case LV_EVENT_CLICKED:
        if (ctx->onActivate != nullptr) {
            ctx->onActivate(ctx->user);
        }
        break;
    default:
        break;
    }
}

} // namespace

lv_obj_t *settingsRowActionCreate(SettingsUI &ui, lv_obj_t *parent, const char *rowName, const char *label,
                                   SettingsRowActivateFn onActivate, void *user) {
    lv_obj_t *row = createFramedRow(ui, parent);

    auto *ctx = new ActionCtx();
    ctx->onActivate = onActivate;
    ctx->user = user;

    TextCol col = buildTextCol(row, label, kFramedTextX, kFramedTextColW, kFramedContentH);
    ui.tag(col.value, rowName, "value", ctx->value);
    buildCueText(row, "Tap");

    lv_obj_add_event_cb(row, actionEvent, LV_EVENT_ALL, ctx);
    lv_obj_add_event_cb(
        row, [](lv_event_t *e) { delete static_cast<ActionCtx *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, ctx);
    ui.tag(row, rowName, "action");
    return row;
}

// ---- confirm -----------------------------------------------------------------

namespace {

struct ConfirmCtx {
    char value[kSettingsRowValueCap] = {0};
    lv_obj_t *progressBar = nullptr;
    SettingsRowConfirmFn onConfirm = nullptr;
    void *user = nullptr;
    uint32_t pressedAtMs = 0;
    bool fired = false;
};

void confirmSetProgress(ConfirmCtx *ctx, uint32_t elapsed) {
    const uint32_t clamped = elapsed > kSettingsRowConfirmHoldMs ? kSettingsRowConfirmHoldMs : elapsed;
    const lv_coord_t w =
        static_cast<lv_coord_t>(static_cast<int64_t>(kFramedContentW) * clamped / kSettingsRowConfirmHoldMs);
    lv_obj_set_width(ctx->progressBar, w);
}

void confirmEvent(lv_event_t *e) {
    auto *ctx = static_cast<ConfirmCtx *>(lv_event_get_user_data(e));
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        ctx->pressedAtMs = lv_tick_get();
        ctx->fired = false;
        confirmSetProgress(ctx, 0);
        break;
    case LV_EVENT_LONG_PRESSED_REPEAT: {
        const uint32_t elapsed = lv_tick_elaps(ctx->pressedAtMs);
        confirmSetProgress(ctx, elapsed);
        // Fires the moment the threshold is crossed, mid-hold, not deferred
        // to release: nothing else on this row is revealed for a continued
        // hold to land on by surprise (unlike the locked row's unlock),
        // so there is no reason to wait for the finger to lift.
        if (!ctx->fired && elapsed >= kSettingsRowConfirmHoldMs) {
            ctx->fired = true;
            if (ctx->onConfirm != nullptr) {
                ctx->onConfirm(ctx->user);
            }
        }
        break;
    }
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        confirmSetProgress(ctx, 0);
        ctx->fired = false;
        break;
    default:
        break;
    }
}

} // namespace

lv_obj_t *settingsRowConfirmCreate(SettingsUI &ui, lv_obj_t *parent, const char *rowName, const char *label,
                                    SettingsRowConfirmFn onConfirm, void *user) {
    const lv_color_t fg = themeFg();

    lv_obj_t *row = createFramedRow(ui, parent);

    auto *ctx = new ConfirmCtx();
    ctx->onConfirm = onConfirm;
    ctx->user = user;

    TextCol col = buildTextCol(row, label, kFramedTextX, kFramedTextColW, kFramedContentH);
    ui.tag(col.value, rowName, "value", ctx->value);

    // The hold time is shown at rest, so the row says how to operate it
    // before the finger is down.
    char cue[16];
    snprintf(cue, sizeof(cue), "Hold %u s", static_cast<unsigned>(kSettingsRowConfirmHoldMs / 1000));
    buildCueText(row, cue);

    // Growing underline along the inside of the frame's bottom edge.
    lv_obj_t *bar = lv_obj_create(row);
    lv_obj_remove_style_all(bar);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE); // see createRowContainer: lv_obj_create defaults this on
    lv_obj_set_size(bar, 0, 3);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, fg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    ctx->progressBar = bar;

    lv_obj_add_event_cb(row, confirmEvent, LV_EVENT_ALL, ctx);
    lv_obj_add_event_cb(
        row, [](lv_event_t *e) { delete static_cast<ConfirmCtx *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, ctx);
    ui.tag(row, rowName, "confirm");
    return row;
}

// ---- locked (wraps a stepper) --------------------------------------------

namespace {

// The custom event settingsRowSetLocked sends: lv_event_send reaches this
// row's LV_EVENT_ALL handler synchronously, the same path a real LVGL event
// would take, without needing anywhere else to stash the ctx pointer that
// getting it from outside an event callback would otherwise require (this
// row's own user_data is reserved for its SettingsDebugTag, see findByRole's
// comment above).
lv_event_code_t lockedSetEventCode() {
    static const lv_event_code_t code = static_cast<lv_event_code_t>(lv_event_register_id());
    return code;
}

struct LockedCtx {
    char value[kSettingsRowValueCap] = {0};
    lv_obj_t *valueLabel = nullptr;
    lv_obj_t *row = nullptr;
    lv_obj_t *minusBtn = nullptr;
    lv_obj_t *plusBtn = nullptr;
    lv_obj_t *lockBtn = nullptr;
    lv_obj_t *progressBar = nullptr;
    RepeatBtn minus;
    RepeatBtn plus;
    SettingsRowUnlockedFn onUnlocked = nullptr;
    void *user = nullptr;
    bool locked = true;
    uint32_t pressedAtMs = 0;
};

// The Hold button covers the two buttons' boxes and the gap between them,
// and its progress underline runs along the inside of its bottom edge.
constexpr lv_coord_t kHoldHitW = SettingsUI::kRowW - kMinusHitX;                 // 116
constexpr lv_coord_t kHoldBarW = kHoldHitW - 2 * kFrameInset - 2 * kFrameBorder; // 108

void lockedSetProgress(LockedCtx *ctx, uint32_t elapsed) {
    const uint32_t clamped = elapsed > kSettingsRowUnlockHoldMs ? kSettingsRowUnlockHoldMs : elapsed;
    const lv_coord_t w =
        static_cast<lv_coord_t>(static_cast<int64_t>(kHoldBarW) * clamped / kSettingsRowUnlockHoldMs);
    lv_obj_set_width(ctx->progressBar, w);
}

// Applies ctx->locked to the widgets: hides the stepper controls and shows
// "hold to unlock" (locked), or the reverse (unlocked). Called at creation
// and on every locked/unlocked transition, never mid-press (see
// settingsRowLockedCreate's header comment for why the transition itself is
// deferred to release).
void lockedApplyVisual(LockedCtx *ctx) {
    if (ctx->locked) {
        lv_obj_add_flag(ctx->minusBtn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->plusBtn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ctx->lockBtn, LV_OBJ_FLAG_HIDDEN);
        snprintf(ctx->value, sizeof(ctx->value), "%s", LV_SYMBOL_EYE_CLOSE " Hold to unlock");
        lv_label_set_text(ctx->valueLabel, ctx->value);
    } else {
        lv_obj_clear_flag(ctx->minusBtn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ctx->plusBtn, LV_OBJ_FLAG_HIDDEN);
        // Hidden, not just left alone: a hidden object is skipped by
        // hit-testing entirely, which is what stops the very press that
        // just unlocked this row from also landing on lockBtn on the next
        // poll (see the RELEASED case below), and once the buttons are
        // revealed they sit where lockBtn does, so a touchmap audit would
        // otherwise see two overlapping clickable targets stacked up.
        lv_obj_add_flag(ctx->lockBtn, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_width(ctx->progressBar, 0);
}

// The custom set-locked event and the delete cleanup live on the row
// itself: settingsRowSetLocked(row, ...) only ever has the row pointer, and
// ctx's lifetime matches the row's, not lockBtn's (a child, torn down
// before the row during a delete cascade).
void lockedRowEvent(lv_event_t *e) {
    auto *ctx = static_cast<LockedCtx *>(lv_event_get_user_data(e));
    const bool want = *static_cast<const bool *>(lv_event_get_param(e));
    if (want != ctx->locked) {
        ctx->locked = want;
        lockedApplyVisual(ctx);
    }
}

// The hold-to-unlock gesture: registered on lockBtn, the framed Hold
// button where the minus and plus buttons appear once unlocked, not the
// row. The button is the target, so the row says where to press and for
// how long ("Hold 1 s") before the finger is down (gm-3vj.50).
void lockedBtnEvent(lv_event_t *e) {
    // Registered for LV_EVENT_ALL, so this also runs for lockBtn's own
    // LV_EVENT_DELETE, which LVGL delivers after the row's DELETE handler
    // has already freed ctx (a parent's DELETE precedes its children's):
    // the event code is checked before ctx is touched.
    const lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_LONG_PRESSED_REPEAT && code != LV_EVENT_RELEASED &&
        code != LV_EVENT_PRESS_LOST) {
        return;
    }
    auto *ctx = static_cast<LockedCtx *>(lv_event_get_user_data(e));
    if (!ctx->locked) {
        return; // lockBtn is hidden while unlocked; stay defensive anyway
    }
    switch (code) {
    case LV_EVENT_PRESSED:
        ctx->pressedAtMs = lv_tick_get();
        lockedSetProgress(ctx, 0);
        break;
    case LV_EVENT_LONG_PRESSED_REPEAT:
        lockedSetProgress(ctx, lv_tick_elaps(ctx->pressedAtMs));
        break;
    case LV_EVENT_RELEASED: {
        const uint32_t elapsed = lv_tick_elaps(ctx->pressedAtMs);
        if (elapsed >= kSettingsRowUnlockHoldMs) {
            // Revealing the buttons only now, at release, is what stops the
            // very press that crossed the threshold from landing on one of
            // them: while locked, lockBtn (not the buttons, which stay
            // hidden the whole press) is the only clickable object here,
            // so LVGL's indev keeps re-targeting it every poll for the
            // rest of this press regardless of when the threshold crossed.
            ctx->locked = false;
            lockedApplyVisual(ctx);
            if (ctx->onUnlocked != nullptr) {
                ctx->onUnlocked(ctx->user);
            }
        } else {
            lockedSetProgress(ctx, 0);
        }
        break;
    }
    case LV_EVENT_PRESS_LOST:
        lockedSetProgress(ctx, 0);
        break;
    default:
        break;
    }
}

} // namespace

lv_obj_t *settingsRowLockedCreate(SettingsUI &ui, lv_obj_t *parent, const char *rowName, const char *label,
                                   SettingsRowStepFn onStep, SettingsRowUnlockedFn onUnlocked, void *user) {
    const lv_color_t fg = themeFg();

    lv_obj_t *row = createRowContainer(parent);

    auto *ctx = new LockedCtx();
    ctx->onUnlocked = onUnlocked;
    ctx->user = user;
    ctx->row = row;

    TextCol col = buildTextCol(row, label, kTextX, kTextColW, SettingsUI::kRowH);
    ctx->valueLabel = col.value;
    ui.tag(col.value, rowName, "value", ctx->value);

    lv_obj_t *minusBtn = buildRowButton(ui, row, kMinusHitX, &img_minus_small_40x40);
    ctx->minus = {onStep, user, -1, 0};
    wireRepeatBtn(minusBtn, &ctx->minus);
    ui.tag(minusBtn, rowName, "minus");
    ctx->minusBtn = minusBtn;

    lv_obj_t *plusBtn = buildRowButton(ui, row, kPlusHitX, &img_plus_small_40x40);
    ctx->plus = {onStep, user, +1, 0};
    wireRepeatBtn(plusBtn, &ctx->plus);
    ui.tag(plusBtn, rowName, "plus");
    ctx->plusBtn = plusBtn;

    // Locked and unlocked never show at once, so the Hold button takes the
    // minus and plus buttons' place: a 116x56 hit box from x 204 to the
    // row's edge, the same edge clearance the plus button has.
    char holdText[16];
    snprintf(holdText, sizeof(holdText), "Hold %u s", static_cast<unsigned>(kSettingsRowUnlockHoldMs / 1000));
    lv_obj_t *lockBtn = buildRowButton(ui, row, kMinusHitX, nullptr, kHoldHitW, holdText);
    ui.tag(lockBtn, rowName, "unlock");
    ctx->lockBtn = lockBtn;

    lv_obj_t *bar = lv_obj_create(lockBtn);
    lv_obj_remove_style_all(bar);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE); // see createRowContainer: lv_obj_create defaults this on
    lv_obj_add_flag(bar, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(bar, 0, 3);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, fg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    ctx->progressBar = bar;

    lv_obj_add_event_cb(row, lockedRowEvent, lockedSetEventCode(), ctx);
    lv_obj_add_event_cb(
        row, [](lv_event_t *e) { delete static_cast<LockedCtx *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, ctx);
    lv_obj_add_event_cb(lockBtn, lockedBtnEvent, LV_EVENT_ALL, ctx);
    ui.tag(row, rowName, "row");

    lockedApplyVisual(ctx); // starts locked: hides the buttons, shows lockBtn and "hold to unlock"
    return row;
}

void settingsRowSetLocked(lv_obj_t *row, bool locked) {
    if (row == nullptr) {
        return;
    }
    lv_event_send(row, lockedSetEventCode(), &locked);
}

// ---- info ------------------------------------------------------------------

lv_obj_t *settingsRowInfoCreate(SettingsUI &ui, lv_obj_t *parent, const char *rowName, const char *label) {
    lv_obj_t *row = createRowContainer(parent);

    TextCol col = buildTextCol(row, label, kTextX, SettingsUI::kRowW - kTextX - 4, SettingsUI::kRowH);
    // No per-kind ctx (nothing else about an info row is stateful), so the
    // canonical value buffer is its own small heap allocation, freed on this
    // row's own delete same as every other kind's ctx.
    auto *buf = new char[kSettingsRowValueCap]();
    ui.tag(col.value, rowName, "value", buf);
    ui.tag(row, rowName, "row");

    lv_obj_add_event_cb(
        row, [](lv_event_t *e) { delete[] static_cast<char *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, buf);
    return row;
}

// ---- swatch ------------------------------------------------------------------

namespace {

// The picker row's geometry (gm-nov3.3, framed in gm-3vj.50). The row is a
// framed row: a 316x52 object in its 320x56 slot whose 312x48 content holds,
// left to right, the text column, a marker dot that is hidden unless the row
// is the entry currently in force, and the ramp, one pixel column per sample.
// The text starts kFramedTextX in, where every framed row's text starts, and
// the ramp stops kSwatchPadRight short of the frame's inside edge.
constexpr lv_coord_t kSwatchW = kSettingsRowSwatchSamples;
constexpr lv_coord_t kSwatchH = 30;
constexpr lv_coord_t kMarkerSize = 14;
constexpr lv_coord_t kSwatchGap = 6;
constexpr lv_coord_t kSwatchPadRight = 6;
constexpr lv_coord_t kSwatchTextColW =
    kFramedContentW - kFramedTextX - kSwatchGap - kMarkerSize - kSwatchGap - kSwatchW - kSwatchPadRight; // 176

// The stepping row's geometry (gm-nov3.32, framed in gm-3vj.50). It is a plain
// row laid out like a choice row: prev and next are the choice row's two
// framed 56x56 buttons at x 204 and 264, and the band that opens the picker
// takes the hit box left of them, x 0 to 200 of the slot, 4 px short of prev
// like every other pair of neighbouring targets. So the audit sees the three
// rectangles a choice row already passes, plus a band whose left edge is the
// slot's own (the bottom row's far corner is 222.8 px from the panel centre).
//
// The band is a framed target, 196x52 with 192x48 inside its border, and the
// two text lines need 43 of the 48 (Montserrat 18 and 20, line heights 21 and
// 22). The ramp bar under them therefore gets 4 px, not the 10 it had before
// the frame. The bar keeps the picker row's kSettingsRowSwatchSamples columns,
// so the same ramp drawn on a category row and on a picker row is the same
// pixels in a row and a check can compare the two widgets directly
// (test_animation.py's high_id_row_draws_the_chosen_ramp does).
constexpr lv_coord_t kStepBandHitW = kMinusHitX - kBtnGap;                   // 200
constexpr lv_coord_t kStepBandW = kStepBandHitW - 2 * kFrameInset;           // 196
constexpr lv_coord_t kStepBandContentW = kStepBandW - 2 * kFrameBorder;      // 192
constexpr lv_coord_t kStepBandPadRight = 4;
constexpr lv_coord_t kStepBandTextW = kStepBandContentW - kFramedTextX - kStepBandPadRight; // 180
constexpr lv_coord_t kStepBarW = kSwatchW;
constexpr lv_coord_t kStepBarH = 4;

struct SwatchCtx {
    char value[kSettingsRowValueCap] = {0};
    SettingsRowActivateFn onActivate = nullptr;
    void *user = nullptr;
    lv_obj_t *canvas = nullptr;
    lv_obj_t *marker = nullptr;
    lv_color_t *buf = nullptr;
    // The canvas this row owns. Both kinds are kSettingsRowSwatchSamples
    // columns wide; only the height differs, 30 for the picker row's block
    // and 4 for the stepping row's bar.
    lv_coord_t canvasW = kSwatchW;
    lv_coord_t canvasH = kSwatchH;
    // Only the stepping variant fills these in.
    SettingsRowCycleFn onCycle = nullptr;
    RepeatBtn prev;
    RepeatBtn next;
};

// Adapts a RepeatBtn onto onCycle, dropping fast, the way choiceStepAdapter
// does for the choice row: a gradient arrow repeats while held and has no
// fast tier.
void swatchStepAdapter(void *userCtx, int dir, bool /*fast*/) {
    auto *sc = static_cast<SwatchCtx *>(userCtx);
    if (sc->onCycle != nullptr) {
        sc->onCycle(sc->user, dir);
    }
}

// The tap. Press feedback is the frame's own (makeFramedTarget's
// framePressEvent recolours the border and the text; a canvas is neither a
// label nor an image, so the ramp keeps its colours while pressed).
void swatchEvent(lv_event_t *e) {
    auto *ctx = static_cast<SwatchCtx *>(lv_event_get_user_data(e));
    switch (lv_event_get_code(e)) {
    case LV_EVENT_CLICKED:
        if (ctx->onActivate != nullptr) {
            ctx->onActivate(ctx->user);
        }
        break;
    default:
        break;
    }
}

// The event codes the two setters below travel on, allocated once. Same
// mechanism settingsRowSetLocked uses: a setter that reaches a row through
// its own event queue needs no pointer into this file's per-kind context.
lv_event_code_t swatchSetEventCode() {
    static const lv_event_code_t code = static_cast<lv_event_code_t>(lv_event_register_id());
    return code;
}

lv_event_code_t swatchSelectEventCode() {
    static const lv_event_code_t code = static_cast<lv_event_code_t>(lv_event_register_id());
    return code;
}

void swatchApply(SwatchCtx *ctx, const uint16_t *ramp) {
    if (ctx->canvas == nullptr || ctx->buf == nullptr) {
        return;
    }
    if (ramp == nullptr) {
        lv_obj_add_flag(ctx->canvas, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    // One sample per column, copied down the rows. lv_color_t is a union over
    // a 16-bit `full` at LV_COLOR_DEPTH 16, which is the format the samples
    // already are, so nothing is converted here. Both kinds are as wide as
    // the ramp today; a canvas of some other width samples across it rather
    // than indexing it, so a width change cannot read past the ramp's end.
    for (int x = 0; x < ctx->canvasW; x++) {
        const int s = ctx->canvasW == kSettingsRowSwatchSamples
                          ? x
                          : x * kSettingsRowSwatchSamples / ctx->canvasW;
        lv_color_t c;
        c.full = ramp[s];
        ctx->buf[x] = c;
    }
    for (int y = 1; y < ctx->canvasH; y++) {
        memcpy(&ctx->buf[y * ctx->canvasW], ctx->buf, ctx->canvasW * sizeof(lv_color_t));
    }
    lv_obj_clear_flag(ctx->canvas, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(ctx->canvas);
}

// The canvas both swatch kinds draw into, from LVGL's heap (PSRAM on the
// device): 5.6 kB for a picker row's 96x30 block, 0.8 kB for a stepping
// row's 96x4 bar, and at most five rows are built at a time. A failed
// allocation leaves the row without a swatch rather than without a row.
void swatchBuildCanvas(SettingsUI &ui, lv_obj_t *parent, SwatchCtx *ctx, const char *rowName, lv_color_t fg) {
    ctx->buf = static_cast<lv_color_t *>(lv_mem_alloc(ctx->canvasW * ctx->canvasH * sizeof(lv_color_t)));
    if (ctx->buf == nullptr) {
        return;
    }
    ctx->canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(ctx->canvas, ctx->buf, ctx->canvasW, ctx->canvasH, LV_IMG_CF_TRUE_COLOR);
    lv_obj_clear_flag(ctx->canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(ctx->canvas, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(ctx->canvas, LV_OBJ_FLAG_HIDDEN);
    lv_canvas_fill_bg(ctx->canvas, fg, LV_OPA_COVER);
    ui.tag(ctx->canvas, rowName, "swatch");
}

// settingsRowSetSwatch, settingsRowSetSelected and the row's own teardown,
// all three addressed to the row container so both kinds answer the same
// two setters whatever their internal shape is.
void swatchWireSetters(lv_obj_t *row, SwatchCtx *ctx) {
    lv_obj_add_event_cb(
        row,
        [](lv_event_t *e) {
            auto *c = static_cast<SwatchCtx *>(lv_event_get_user_data(e));
            swatchApply(c, static_cast<const uint16_t *>(lv_event_get_param(e)));
        },
        swatchSetEventCode(), ctx);
    lv_obj_add_event_cb(
        row,
        [](lv_event_t *e) {
            auto *c = static_cast<SwatchCtx *>(lv_event_get_user_data(e));
            if (c->marker == nullptr) {
                return;
            }
            if (*static_cast<const bool *>(lv_event_get_param(e))) {
                lv_obj_clear_flag(c->marker, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(c->marker, LV_OBJ_FLAG_HIDDEN);
            }
        },
        swatchSelectEventCode(), ctx);
    lv_obj_add_event_cb(
        row,
        [](lv_event_t *e) {
            auto *c = static_cast<SwatchCtx *>(lv_event_get_user_data(e));
            // obj_del_core sends this before it deletes the row's children
            // (lv_obj_tree.c), so the canvas still points at this buffer for
            // the rest of the deletion. Nothing draws in between: the whole
            // deletion runs inside one lv_obj_del call on the UI task.
            c->canvas = nullptr;
            c->marker = nullptr;
            lv_mem_free(c->buf);
            delete c;
        },
        LV_EVENT_DELETE, ctx);
}

} // namespace

lv_obj_t *settingsRowSwatchCreate(SettingsUI &ui, lv_obj_t *parent, const char *rowName, const char *label,
                                   SettingsRowActivateFn onActivate, void *user) {
    const lv_color_t fg = themeFg();

    // A framed row, like the action row it stands in for: the row object is
    // the target and the frame. Its children are laid out by flex inside the
    // frame's content area.
    lv_obj_t *row = createFramedRow(ui, parent);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(row, kFramedTextX, LV_PART_MAIN);
    lv_obj_set_style_pad_right(row, kSwatchPadRight, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, kSwatchGap, LV_PART_MAIN);

    auto *ctx = new SwatchCtx();
    ctx->onActivate = onActivate;
    ctx->user = user;

    TextCol col = buildTextCol(row, label, kSwatchTextColW, kFramedContentH);
    ui.tag(col.value, rowName, "value", ctx->value);

    // An indicator, not a target, so it is filled rather than outlined. It
    // has no border and is not a label or an image, so neither the press
    // recolour nor a disabled row's dimming touches it.
    ctx->marker = lv_obj_create(row);
    lv_obj_remove_style_all(ctx->marker);
    lv_obj_set_size(ctx->marker, kMarkerSize, kMarkerSize);
    lv_obj_clear_flag(ctx->marker, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(ctx->marker, LV_OBJ_FLAG_CLICKABLE); // see createRowContainer
    lv_obj_add_flag(ctx->marker, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(ctx->marker, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_radius(ctx->marker, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ctx->marker, fg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ctx->marker, LV_OPA_COVER, LV_PART_MAIN);
    ui.tag(ctx->marker, rowName, "selected");

    swatchBuildCanvas(ui, row, ctx, rowName, fg);

    lv_obj_add_event_cb(row, swatchEvent, LV_EVENT_ALL, ctx);
    swatchWireSetters(row, ctx);
    // Role "action", like every other whole-row target, so rows_on_page() and
    // the geometry audit see one framed row here and not a second kind.
    ui.tag(row, rowName, "action");
    return row;
}

lv_obj_t *settingsRowSwatchStepCreate(SettingsUI &ui, lv_obj_t *parent, const char *rowName, const char *label,
                                       SettingsRowActivateFn onActivate, SettingsRowCycleFn onCycle, void *user) {
    const lv_color_t fg = themeFg();

    // A plain row, like the choice row: the container is no target, and its
    // three targets are each framed on their own.
    lv_obj_t *row = createRowContainer(parent);

    auto *ctx = new SwatchCtx();
    ctx->onActivate = onActivate;
    ctx->onCycle = onCycle;
    ctx->user = user;
    ctx->canvasW = kStepBarW;
    ctx->canvasH = kStepBarH;

    // The band opens the picker. A row-wide target laid over the arrows would
    // be two targets in one place, which Rig.audit() rejects (gm-nov3.3's
    // reason for taking the arrows off, which this shape answers rather than
    // reverses).
    lv_obj_t *band = lv_obj_create(row);
    lv_obj_remove_style_all(band);
    lv_obj_set_size(band, kStepBandW, kFramedRowH);
    lv_obj_set_pos(band, kFrameInset, kFrameInset);
    lv_obj_clear_flag(band, LV_OBJ_FLAG_SCROLLABLE);
    makeFramedTarget(ui, band);
    lv_obj_set_flex_flow(band, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(band, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_left(band, kFramedTextX, LV_PART_MAIN);
    lv_obj_set_style_pad_right(band, kStepBandPadRight, LV_PART_MAIN);

    TextCol col = buildTextCol(band, label, kStepBandTextW, textColContentH());
    ui.tag(col.value, rowName, "value", ctx->value);

    swatchBuildCanvas(ui, band, ctx, rowName, fg);

    lv_obj_t *prevBtn = buildRowButton(ui, row, kMinusHitX, &img_angle_left_40x40);
    ctx->prev = {swatchStepAdapter, ctx, -1, 0};
    wireRepeatBtn(prevBtn, &ctx->prev);
    ui.tag(prevBtn, rowName, "prev");

    lv_obj_t *nextBtn = buildRowButton(ui, row, kPlusHitX, &img_angle_right_40x40);
    ctx->next = {swatchStepAdapter, ctx, +1, 0};
    wireRepeatBtn(nextBtn, &ctx->next);
    ui.tag(nextBtn, rowName, "next");

    lv_obj_add_event_cb(band, swatchEvent, LV_EVENT_ALL, ctx);
    // The setters and the teardown stay on the row container: a caller holds
    // the row, and settingsRowSetSwatch/settingsRowSetValue address it.
    swatchWireSetters(row, ctx);
    // Role "action" on the row container, as on the plain swatch row, so
    // rows_on_page() counts one row here and a caller that taps the row's
    // centre still lands on the band (the band's hit box is x 0 to 200 of the
    // slot, and the centre is x 160). The container is not clickable, so the
    // audit sees three targets on this row: the band and the two arrows.
    ui.tag(row, rowName, "action");
    ui.tag(band, rowName, "open");
    return row;
}

void settingsRowSetSwatch(lv_obj_t *row, const uint16_t *ramp) {
    if (row == nullptr) {
        return;
    }
    lv_event_send(row, swatchSetEventCode(), const_cast<uint16_t *>(ramp));
}

void settingsRowSetSelected(lv_obj_t *row, bool selected) {
    if (row == nullptr) {
        return;
    }
    lv_event_send(row, swatchSelectEventCode(), &selected);
}
