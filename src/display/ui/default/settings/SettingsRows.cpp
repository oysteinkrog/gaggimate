#include "SettingsRows.h"

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
    if (obj->class_p == &lv_label_class || obj->class_p == &lv_img_class) {
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
    // buildIconButton so every target here re-hit-tests on each poll.
    lv_obj_clear_flag(row, LV_OBJ_FLAG_PRESS_LOCK);
    // A button flush against this row's own edge needs its ext click pad to
    // extend past the row's bounds: several rows fill kRowW exactly (label
    // column + gaps + two 40 px buttons), leaving no margin inside the row
    // for the pad to grow into. Without this flag that pad is clipped to
    // nothing on the outward side (eez/actions.cpp's applyClickArea works
    // around the same clipping the same way, on every ancestor short of the
    // screen).
    lv_obj_add_flag(row, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    return row;
}

// Label (Montserrat 18) above value (Montserrat 20), left-aligned, both
// truncating with dots as a last resort. width is kTextColW when a control
// cluster shares the row, or nearly the full row when nothing else does.
constexpr lv_coord_t kTextColW = 200;

struct TextCol {
    lv_obj_t *label;
    lv_obj_t *value;
};

TextCol buildTextCol(lv_obj_t *parent, const char *labelText, lv_coord_t width) {
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, width, SettingsUI::kRowH);
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

// A 40x40 icon grown to a 56x56 effective hit area (ext click pad 8, the
// same arithmetic the page header's up/down arrows use), clickable and
// bubbling; gets its pressed-dim styling for free from
// DefaultUI::applyPressedFeedbackTo, which the shell runs over the whole
// page after buildRow returns (a clickable lv_img is one of the two classes
// that walk recolors).
lv_obj_t *buildIconButton(lv_obj_t *parent, const lv_img_dsc_t *icon, lv_color_t fg) {
    lv_obj_t *img = lv_img_create(parent);
    lv_img_set_src(img, icon);
    lv_obj_set_style_img_recolor(img, fg, LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(img, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_PRESS_LOCK); // see createRowContainer
    lv_obj_set_ext_click_area(img, 8);
    return img;
}

// Whole-row targets (toggle, action, confirm, the locked row's unlock
// gesture) are plain lv_obj, not lv_btn or a clickable lv_img, so
// applyPressedFeedbackTo's walk does nothing for them (CLAUDE.md: "transparent
// text rows get nothing from it and must style their own pressed text/image
// colour"); this is that styling, applied directly rather than through the
// LV_STATE_PRESSED selector because LVGL only adds that state to the object
// actually hit (the row), never to its child labels.
struct PressDim {
    lv_obj_t *label = nullptr;
    lv_obj_t *value = nullptr;
    lv_color_t fg{};
    lv_color_t dim{};
};

void applyPressDim(const PressDim &pd, bool pressed) {
    const lv_color_t c = pressed ? pd.dim : pd.fg;
    if (pd.label != nullptr) {
        lv_obj_set_style_text_color(pd.label, c, LV_PART_MAIN);
    }
    if (pd.value != nullptr) {
        lv_obj_set_style_text_color(pd.value, c, LV_PART_MAIN);
    }
}

PressDim makePressDim(const TextCol &col, lv_color_t fg, lv_color_t dim) {
    return {col.label, col.value, fg, settingsPressedColor(fg, dim)};
}

} // namespace

lv_color_t settingsPressedColor(lv_color_t rest, lv_color_t dim) { return lv_color_mix(dim, rest, LV_OPA_40); }

namespace {

lv_color_t touchDimColor(SettingsUI &ui) {
    return lv_color_hex(static_cast<uint32_t>(ui.controller().getSettings().getTouchDimColor()));
}

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
    // default is on; createRowContainer and buildIconButton clear it), so
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
    const lv_color_t fg = themeFg();

    lv_obj_t *row = createRowContainer(parent);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 20, LV_PART_MAIN); // 56x56 hit boxes 4px apart, see buildIconButton

    auto *ctx = new StepperCtx();

    TextCol col = buildTextCol(row, label, kTextColW);
    ui.tag(col.value, rowName, "value", ctx->value);

    lv_obj_t *minusBtn = buildIconButton(row, &img_minus_small_40x40, fg);
    ctx->minus = {onStep, user, -1, 0};
    wireRepeatBtn(minusBtn, &ctx->minus);
    ui.tag(minusBtn, rowName, "minus");

    lv_obj_t *plusBtn = buildIconButton(row, &img_plus_small_40x40, fg);
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
    const lv_color_t fg = themeFg();

    lv_obj_t *row = createRowContainer(parent);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 20, LV_PART_MAIN);

    auto *ctx = new ChoiceCtx();
    ctx->onCycle = onCycle;
    ctx->user = user;

    TextCol col = buildTextCol(row, label, kTextColW);
    ui.tag(col.value, rowName, "value", ctx->value);

    lv_obj_t *prevBtn = buildIconButton(row, &img_angle_left_40x40, fg);
    ctx->prev = {choiceStepAdapter, ctx, -1, 0};
    wireRepeatBtn(prevBtn, &ctx->prev);
    ui.tag(prevBtn, rowName, "prev");

    lv_obj_t *nextBtn = buildIconButton(row, &img_angle_right_40x40, fg);
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
    PressDim pd;
    SettingsRowToggleFn onToggle = nullptr;
    void *user = nullptr;
    bool on = false;
};

// The toggle widget owns its own "On"/"Off" text (unlike every other kind's
// value, which the caller formats): the mapping is fixed, not model-specific.
void toggleSetText(ToggleCtx *ctx) {
    snprintf(ctx->value, sizeof(ctx->value), "%s", ctx->on ? "On" : "Off");
    lv_label_set_text(ctx->valueLabel, ctx->value);
}

void toggleEvent(lv_event_t *e) {
    auto *ctx = static_cast<ToggleCtx *>(lv_event_get_user_data(e));
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        applyPressDim(ctx->pd, true);
        break;
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        applyPressDim(ctx->pd, false);
        break;
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
    const lv_color_t dim = touchDimColor(ui);

    lv_obj_t *row = createRowContainer(parent);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto *ctx = new ToggleCtx();
    ctx->onToggle = onToggle;
    ctx->user = user;
    ctx->on = initial;

    TextCol col = buildTextCol(row, label, SettingsUI::kRowW - 8);
    ctx->valueLabel = col.value;
    ctx->pd = makePressDim(col, fg, dim);
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
    PressDim pd;
    SettingsRowActivateFn onActivate = nullptr;
    void *user = nullptr;
};

void actionEvent(lv_event_t *e) {
    auto *ctx = static_cast<ActionCtx *>(lv_event_get_user_data(e));
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        applyPressDim(ctx->pd, true);
        break;
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        applyPressDim(ctx->pd, false);
        break;
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
    const lv_color_t fg = themeFg();
    const lv_color_t dim = touchDimColor(ui);

    lv_obj_t *row = createRowContainer(parent);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto *ctx = new ActionCtx();
    ctx->onActivate = onActivate;
    ctx->user = user;

    TextCol col = buildTextCol(row, label, SettingsUI::kRowW - 8);
    ctx->pd = makePressDim(col, fg, dim);
    ui.tag(col.value, rowName, "value", ctx->value);

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
    PressDim pd;
    lv_obj_t *progressBar = nullptr;
    SettingsRowConfirmFn onConfirm = nullptr;
    void *user = nullptr;
    uint32_t pressedAtMs = 0;
    bool fired = false;
};

void confirmSetProgress(ConfirmCtx *ctx, uint32_t elapsed) {
    const uint32_t clamped = elapsed > kSettingsRowConfirmHoldMs ? kSettingsRowConfirmHoldMs : elapsed;
    const lv_coord_t w =
        static_cast<lv_coord_t>(static_cast<int64_t>(SettingsUI::kRowW) * clamped / kSettingsRowConfirmHoldMs);
    lv_obj_set_width(ctx->progressBar, w);
}

void confirmEvent(lv_event_t *e) {
    auto *ctx = static_cast<ConfirmCtx *>(lv_event_get_user_data(e));
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        ctx->pressedAtMs = lv_tick_get();
        ctx->fired = false;
        applyPressDim(ctx->pd, true);
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
        applyPressDim(ctx->pd, false);
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
    const lv_color_t dim = touchDimColor(ui);

    lv_obj_t *row = createRowContainer(parent);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto *ctx = new ConfirmCtx();
    ctx->onConfirm = onConfirm;
    ctx->user = user;

    TextCol col = buildTextCol(row, label, SettingsUI::kRowW - 8);
    ctx->pd = makePressDim(col, fg, dim);
    ui.tag(col.value, rowName, "value", ctx->value);

    // Growing underline: a non-clickable bar excluded from the row's flex
    // flow (LV_OBJ_FLAG_IGNORE_LAYOUT) so it can sit pinned to the bottom
    // edge while textCol occupies the flow normally.
    lv_obj_t *bar = lv_obj_create(row);
    lv_obj_remove_style_all(bar);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE); // see createRowContainer: lv_obj_create defaults this on
    lv_obj_add_flag(bar, LV_OBJ_FLAG_IGNORE_LAYOUT);
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
    PressDim pd;
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

void lockedSetProgress(LockedCtx *ctx, uint32_t elapsed) {
    const uint32_t clamped = elapsed > kSettingsRowUnlockHoldMs ? kSettingsRowUnlockHoldMs : elapsed;
    const lv_coord_t w =
        static_cast<lv_coord_t>(static_cast<int64_t>(SettingsUI::kRowW) * clamped / kSettingsRowUnlockHoldMs);
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

// The hold-to-unlock gesture: registered on lockBtn, not the row. The
// row's own hit rect is the full 320x56 slot, and that overlaps the exit
// chevron's clipped hit box whenever this row lands in a page's last slot
// (measured: row4's own bounds reach 9-10 px into the chevron's ext-padded
// reach, independent of what is drawn inside row4; the shell's per-page
// row positions leave no spare margin against either the header above or
// the chevron below, only enough for content that does not span the row's
// full height). lockBtn sits inset from the row's right edge, clear of
// both the chevron's zone and the panel's edge circle with room to spare
// (see settingsRowLockedCreate).
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
        applyPressDim(ctx->pd, true);
        lockedSetProgress(ctx, 0);
        break;
    case LV_EVENT_LONG_PRESSED_REPEAT:
        lockedSetProgress(ctx, lv_tick_elaps(ctx->pressedAtMs));
        break;
    case LV_EVENT_RELEASED: {
        const uint32_t elapsed = lv_tick_elaps(ctx->pressedAtMs);
        applyPressDim(ctx->pd, false);
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
        applyPressDim(ctx->pd, false);
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
    const lv_color_t dim = touchDimColor(ui);

    lv_obj_t *row = createRowContainer(parent);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 20, LV_PART_MAIN);

    auto *ctx = new LockedCtx();
    ctx->onUnlocked = onUnlocked;
    ctx->user = user;
    ctx->row = row;

    TextCol col = buildTextCol(row, label, kTextColW);
    ctx->valueLabel = col.value;
    ctx->pd = makePressDim(col, fg, dim);
    ui.tag(col.value, rowName, "value", ctx->value);

    lv_obj_t *minusBtn = buildIconButton(row, &img_minus_small_40x40, fg);
    ctx->minus = {onStep, user, -1, 0};
    wireRepeatBtn(minusBtn, &ctx->minus);
    ui.tag(minusBtn, rowName, "minus");
    ctx->minusBtn = minusBtn;

    lv_obj_t *plusBtn = buildIconButton(row, &img_plus_small_40x40, fg);
    ctx->plus = {onStep, user, +1, 0};
    wireRepeatBtn(plusBtn, &ctx->plus);
    ui.tag(plusBtn, rowName, "plus");
    ctx->plusBtn = plusBtn;

    // Outside the row's flex flow (minus/plus hide while locked and locked
    // and unlocked never show at once, so there is no layout to share) and
    // inset from the row's right edge: at x_ofs=0 its ext-padded hit rect
    // would reach x=407, only 0.8 px inside the panel's 228 px safety
    // radius; -20 clears both the radius and the exit chevron's clipped
    // hit box, with roughly 15 px and 28 px of margin respectively (see
    // lockedBtnEvent's comment for why this needs its own hit rect at all
    // rather than the whole row). No lock glyph exists among the shared
    // icon set, so this reuses img_check_40x40; the primary "hold to
    // unlock" affordance is the value text, this is a secondary, discoverable
    // tap target sized and placed to pass the geometry audit.
    lv_obj_t *lockBtn = buildIconButton(row, &img_check_40x40, fg);
    lv_obj_add_flag(lockBtn, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(lockBtn, LV_ALIGN_RIGHT_MID, -20, 0);
    ui.tag(lockBtn, rowName, "unlock");
    ctx->lockBtn = lockBtn;

    lv_obj_t *bar = lv_obj_create(row);
    lv_obj_remove_style_all(bar);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE); // see createRowContainer: lv_obj_create defaults this on
    lv_obj_add_flag(bar, LV_OBJ_FLAG_IGNORE_LAYOUT);
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
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    TextCol col = buildTextCol(row, label, SettingsUI::kRowW - 8);
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
