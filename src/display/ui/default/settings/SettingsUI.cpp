#include "SettingsUI.h"
#include "SettingsRows.h"

#include <display/core/Controller.h>
#include <display/core/PluginManager.h>
#include <display/core/Settings.h>
#include <display/drivers/common/LV_Helper.h>
#include <display/ui/default/DefaultUI.h>
#include <display/ui/default/eez/images.h>
#include <display/ui/default/eez/screens.h>
#include <display/ui/default/eez/ui.h>

#include <Arduino.h>
#include <cmath>
#include <cstdio>

#include "esp_log.h"

namespace {

// Category registry, in the epic's order. The tile page and openCategory()
// index into this; production builds carry five tiles, GM_TOUCH_PROBE and
// GAGGIMATE_SIM builds carry the bench-only Fixture tile (SettingsFixture.cpp)
// as a sixth.
const SettingsCategoryDef *const kCategories[] = {
    &kCatTemps, &kCatDisplay, &kCatAnimation, &kCatMachine, &kCatStatus,
#if defined(GM_TOUCH_PROBE) || defined(GAGGIMATE_SIM)
    &kCatFixture,
#endif
};
constexpr int kCategoryCount = sizeof(kCategories) / sizeof(kCategories[0]);

} // namespace

SettingsUI::SettingsUI(Controller &controller, DefaultUI &ui, PluginManager &plugins)
    : controller_(controller), ui_(ui), plugins_(plugins) {}

// ---------------------------------------------------------------------------
// Lifecycle

void SettingsUI::open() {
    if (coverObj != nullptr) {
        return; // already open
    }
    lv_obj_t *menu = objects.menu_screen_new;
    if (menu == nullptr) {
        return; // menu screen not built yet
    }
    ui_.beginOverlayTransition("settings_open");

    capturedVisible.clear();
    const uint32_t n = lv_obj_get_child_cnt(menu);
    capturedVisible.reserve(n);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *child = lv_obj_get_child(menu, i);
        const bool wasHidden = lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN);
        capturedVisible.emplace_back(child, wasHidden);
        // The tile-holder container and every other direct child except the
        // status icons is hidden as a whole; the flow tick keeps driving
        // per-tile HIDDEN flags on ITS children (btn_grind_1 and friends),
        // which this never touches.
        if (child != objects.status_icons) {
            lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
        }
    }

    coverObj = lv_obj_create(menu);
    lv_obj_remove_style_all(coverObj);
    lv_obj_set_size(coverObj, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(coverObj, 0, 0);
    lv_obj_clear_flag(coverObj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(coverObj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(coverObj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_event_cb(
        coverObj,
        [](lv_event_t *e) {
            auto *self = static_cast<SettingsUI *>(lv_event_get_user_data(e));
            // teardownAll() empties the stack before deleting the cover;
            // anything else deleting it (a menu screen deleted under us)
            // would drop every open page's draft uncommitted and leak its
            // ctx, so say so rather than fail silently.
            if (!self->pageStack.empty()) {
                ESP_LOGW("SettingsUI", "SettingsUI: cover deleted with %d page(s) open, edits dropped",
                         static_cast<int>(self->pageStack.size()));
            }
            self->coverObj = nullptr;
            self->tilePageObj = nullptr;
            self->pageStack.clear();
        },
        LV_EVENT_DELETE, this);
    lv_obj_add_event_cb(
        coverObj,
        [](lv_event_t *e) {
            auto *self = static_cast<SettingsUI *>(lv_event_get_user_data(e));
            self->controller_.updateLastAction();
        },
        LV_EVENT_PRESSED, this);
    coverTag = {"cover", "cover", nullptr};
    lv_obj_set_user_data(coverObj, &coverTag);

    // The cover changes what is on screen without lv_scr_act() changing, so
    // applyPressedFeedback's own re-walk gate would never fire for it.
    ui_.resetPressedFeedbackRoot();
    buildTilePage();
}

void SettingsUI::close() {
    if (coverObj == nullptr) {
        return;
    }
    ui_.beginOverlayTransition("settings_close");
    teardownAll();
}

void SettingsUI::onExternalLeave() {
    if (coverObj == nullptr) {
        return;
    }
    teardownAll();
}

void SettingsUI::teardownAll() {
    while (!pageStack.empty()) {
        const SettingsCategoryDef *def = pageStack.back().def;
        void *ctx = pageStack.back().ctx;
        lv_obj_t *root = pageStack.back().root;
        pageStack.pop_back();
        {
            Settings::Guard guard(controller_.getSettings());
            if (def->commit) {
                def->commit(ctx);
            }
        }
        if (root) {
            lv_obj_del(root); // before the ctx, as in popPage
        }
        if (def->destroyCtx && ctx) {
            def->destroyCtx(ctx);
        }
    }
    restoreMenuChildren();
    if (coverObj) {
        // Cascades tilePageObj; the DELETE callback above nulls coverObj and
        // tilePageObj and clears pageStack (already empty here, harmless).
        lv_obj_del(coverObj);
    }
}

void SettingsUI::restoreMenuChildren() {
    for (auto &entry : capturedVisible) {
        lv_obj_t *child = entry.first;
        if (entry.second) {
            lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(child, LV_OBJ_FLAG_HIDDEN);
        }
    }
    capturedVisible.clear();
}

void SettingsUI::service() {
    // Always consumed, whether or not settings is open: a flag left set
    // while closed would otherwise cause a spurious immediate reconcile the
    // next time a category page is opened.
    const bool webSaved = webSaved_.exchange(false, std::memory_order_relaxed);
    if (webSaved) {
        ESP_LOGI("SettingsUI", "SettingsUI: reconcile");
    }
    if (coverObj == nullptr) {
        return;
    }

    const unsigned long now = millis();
    // refresh and reconcile read Settings into the draft the same way enter
    // does, so they take the same guard: reconcile in particular runs right
    // after a web save's batchUpdate, the one moment another task is
    // replacing the container-typed properties (schedules, theme map).
    if (!pageStack.empty() && now - lastRefreshMs >= 1000) {
        lastRefreshMs = now;
        PageEntry &top = pageStack.back();
        if (top.def->refresh) {
            Settings::Guard guard(controller_.getSettings());
            top.def->refresh(top.ctx);
        }
    }

    if (webSaved && !pageStack.empty()) {
        // The def and ctx are read before the call: a reconcile may pop its
        // own page (the schedule editor does when its entry vanished), and
        // rebuildPage() below then rebuilds whatever is on top afterwards.
        const SettingsCategoryDef *def = pageStack.back().def;
        void *ctx = pageStack.back().ctx;
        if (def->reconcile) {
            Settings::Guard guard(controller_.getSettings());
            def->reconcile(ctx);
        }
        rebuildPage();
        ui_.markDirty();
    }

    const uint32_t themeIdx = eez_flow_get_selected_theme_index();
    const uint32_t accent = theme_colors[themeIdx][0];
    if (static_cast<int>(themeIdx) != builtThemeIdx || accent != builtAccent) {
        if (pageStack.empty()) {
            buildTilePage();
        } else {
            rebuildPage();
        }
    }
}

// ---------------------------------------------------------------------------
// Navigation

void SettingsUI::openCategory(int index) {
    if (index < 0 || index >= kCategoryCount) {
        return;
    }
    const SettingsCategoryDef *def = kCategories[index];
    void *ctx = def->createCtx ? def->createCtx() : nullptr;
    pushPage(def, ctx);
}

void SettingsUI::pushPage(const SettingsCategoryDef *def, void *ctx) {
    ui_.beginOverlayTransition("settings_push");
    if (pageStack.empty()) {
        if (tilePageObj) {
            lv_obj_del(tilePageObj);
            tilePageObj = nullptr;
        }
    } else if (pageStack.back().root) {
        lv_obj_del(pageStack.back().root);
        pageStack.back().root = nullptr;
    }
    {
        Settings::Guard guard(controller_.getSettings());
        if (def->enter) {
            def->enter(ctx);
        }
    }
    PageEntry entry;
    entry.def = def;
    entry.ctx = ctx;
    entry.page = 0;
    pageStack.push_back(entry);
    rebuildPage();
}

void SettingsUI::popPages(int count) {
    if (pageStack.empty() || count <= 0) {
        return;
    }
    ui_.beginOverlayTransition("settings_pop");
    for (int i = 0; i < count && !pageStack.empty(); i++) {
        const SettingsCategoryDef *def = pageStack.back().def;
        void *ctx = pageStack.back().ctx;
        lv_obj_t *root = pageStack.back().root;
        pageStack.pop_back();
        {
            Settings::Guard guard(controller_.getSettings());
            if (def->commit) {
                def->commit(ctx);
            }
        }
        // The page's objects go before the ctx: row DELETE callbacks a
        // category registered with its ctx as user data run during lv_obj_del.
        if (root) {
            lv_obj_del(root);
        }
        if (def->destroyCtx && ctx) {
            def->destroyCtx(ctx);
        }
    }
    if (pageStack.empty()) {
        buildTilePage();
    } else {
        // The child may have edited the parent's draft through the ctx it
        // was handed; rebuild so the parent's rows show it, without
        // re-entering (which would overwrite the draft from Settings).
        rebuildPage();
    }
}

void SettingsUI::gotoPage(int page) {
    if (pageStack.empty()) {
        return;
    }
    ui_.beginOverlayTransition("settings_page");
    pageStack.back().page = page; // clamped inside buildCategoryPage
    rebuildPage();
}

void SettingsUI::rebuildPage() {
    if (pageStack.empty()) {
        return;
    }
    buildCategoryPage(pageStack.back());
}

// ---------------------------------------------------------------------------
// Building

void SettingsUI::buildTilePage() {
    if (tilePageObj != nullptr) {
        lv_obj_del(tilePageObj);
        tilePageObj = nullptr;
    }
    tilePageTagsUsed = 0;

    const uint32_t themeIdx = eez_flow_get_selected_theme_index();
    const lv_color_t fg = lv_color_hex(theme_colors[themeIdx][0]);

    lv_obj_t *page = lv_obj_create(coverObj);
    tilePageObj = page;
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(page, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, LV_PART_MAIN);

    for (int i = 0; i < kCategoryCount; i++) {
        buildTile(page, i, kCategories[i], fg);
    }
    buildExitChevron(page, fg, /*topLevel=*/true);

    ui_.applyPressedFeedbackTo(page);
    builtThemeIdx = static_cast<int>(themeIdx);
    builtAccent = theme_colors[themeIdx][0];
}

void SettingsUI::buildTile(lv_obj_t *parent, int index, const SettingsCategoryDef *def, lv_color_t fg) {
    // Six slots around the panel, avoiding the status icons at the top and
    // the exit chevron's clipped hit box at the bottom: hand-verified against
    // the 96x96/12px-edge/56x56-arrow rules in the epic's shared contract
    // (kTileRadius=145, kSize=96 keeps every corner inside radius 228 with
    // 15-30 px to spare, and every pair of adjacent tiles at least a few px
    // apart). Production uses the first five; Fixture (bench/sim only) takes
    // the sixth.
    static constexpr int16_t kAngles[6] = {45, 90, 135, 225, 270, 315};
    static constexpr int kRadius = 145;
    static constexpr int kSize = 96;

    lv_obj_t *tileObj = lv_obj_create(parent);
    lv_obj_remove_style_all(tileObj);
    lv_obj_set_size(tileObj, kSize, kSize);
    const double angleRad = kAngles[index % 6] * M_PI / 180.0;
    const int x = static_cast<int>(lround(sin(angleRad) * kRadius));
    const int y = static_cast<int>(lround(-cos(angleRad) * kRadius));
    lv_obj_align(tileObj, LV_ALIGN_CENTER, x, y);
    lv_obj_clear_flag(tileObj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tileObj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_bg_opa(tileObj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_flex_flow(tileObj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tileObj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *icon = lv_img_create(tileObj);
    lv_img_set_src(icon, def->icon);
    lv_obj_set_style_img_recolor(icon, fg, LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(icon, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *caption = lv_label_create(tileObj);
    lv_label_set_text(caption, def->title);
    lv_obj_set_width(caption, kSize - 4);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(caption, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(caption, fg, LV_PART_MAIN);

    tileClickCtx[index] = {this, index};
    lv_obj_add_event_cb(
        tileObj,
        [](lv_event_t *e) {
            auto *ctx = static_cast<TileClickCtx *>(lv_event_get_user_data(e));
            ctx->self->openCategory(ctx->index);
        },
        LV_EVENT_CLICKED, &tileClickCtx[index]);
    // Press feedback. The tile is a plain lv_obj whose icon is not itself
    // clickable, so DefaultUI::applyPressedFeedbackTo's walk (lv_btn and
    // clickable lv_img only) does nothing for it, and LVGL puts the PRESSED
    // state on the tile, never on its children: the icon and caption are
    // recoloured by hand, the same 40% shift toward the touch dim colour
    // every other target gets (settingsPressedColor). Measured before this
    // existed: 0 change per pixel while held, against 19 to 29 for the
    // generated menu buttons.
    lv_obj_add_event_cb(
        tileObj,
        [](lv_event_t *e) {
            const lv_event_code_t code = lv_event_get_code(e);
            if (code != LV_EVENT_PRESSED && code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) {
                return;
            }
            auto *ctx = static_cast<TileClickCtx *>(lv_event_get_user_data(e));
            lv_obj_t *tile = lv_event_get_target(e);
            lv_obj_t *icon = lv_obj_get_child(tile, 0);
            lv_obj_t *caption = lv_obj_get_child(tile, 1);
            const lv_color_t rest = lv_color_hex(theme_colors[eez_flow_get_selected_theme_index()][0]);
            const lv_color_t dim =
                lv_color_hex(static_cast<uint32_t>(ctx->self->controller().getSettings().getTouchDimColor()));
            // The compositor's plate is the press feedback while it is active
            // (g_pressPlateActive); a release always restores the rest colour.
            const lv_color_t c =
                (code == LV_EVENT_PRESSED && !g_pressPlateActive) ? settingsPressedColor(rest, dim) : rest;
            if (icon != nullptr) {
                lv_obj_set_style_img_recolor(icon, c, LV_PART_MAIN);
            }
            if (caption != nullptr) {
                lv_obj_set_style_text_color(caption, c, LV_PART_MAIN);
            }
        },
        LV_EVENT_ALL, &tileClickCtx[index]);

    tagTilePage(tileObj, def->title, "tile");
}

void SettingsUI::buildExitChevron(lv_obj_t *parent, lv_color_t fg, bool topLevel) {
    lv_obj_t *exitBtn = lv_img_create(parent);
    lv_img_set_src(exitBtn, &img_angle_up_40x40);
    lv_obj_align(exitBtn, LV_ALIGN_CENTER, 0, 210);
    // Same place as every other screen's exit chevron, but a smaller click
    // pad: the fifth row slot ends at y 394 and a 45 px pad reaches up to
    // 385, so a whole-row target in that slot (toggle, action, confirm)
    // would overlap the chevron's hit rectangle, and on the tile page the
    // 45 px pad reached 10x6 px into the two lower tiles' hit rectangles
    // (the runner's audit measured it). 34 keeps the hit box 108x84 px and
    // clear of both by 2 px or more.
    lv_obj_set_ext_click_area(exitBtn, 34);
    lv_obj_set_style_img_recolor(exitBtn, fg, LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(exitBtn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(exitBtn, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    if (topLevel) {
        lv_obj_add_event_cb(
            exitBtn, [](lv_event_t *e) { static_cast<SettingsUI *>(lv_event_get_user_data(e))->close(); },
            LV_EVENT_CLICKED, this);
        tagTilePage(exitBtn, "exit", "exit");
    } else {
        lv_obj_add_event_cb(
            exitBtn, [](lv_event_t *e) { static_cast<SettingsUI *>(lv_event_get_user_data(e))->popPage(); },
            LV_EVENT_CLICKED, this);
        tag(exitBtn, "exit", "exit");
    }
}

void SettingsUI::buildCategoryPage(PageEntry &entry) {
    if (entry.root != nullptr) {
        lv_obj_del(entry.root);
        entry.root = nullptr;
    }
    entry.tagsUsed = 0;

    const uint32_t themeIdx = eez_flow_get_selected_theme_index();
    const lv_color_t fg = lv_color_hex(theme_colors[themeIdx][0]);

    lv_obj_t *root = lv_obj_create(coverObj);
    entry.root = root;
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, LV_PART_MAIN);

    // Swipe between pages: a horizontal gesture anywhere on the page, the
    // finger moving left for the next page. LVGL delivers a gesture to the
    // first ancestor of the pressed object that does not bubble gestures,
    // so the root clears the flag and everything under it keeps it. The
    // rest of the press is dropped (lv_indev_wait_release) so the row the
    // swipe started on gets no CLICKED at the release: LVGL 8.4 gates
    // CLICKED on scrolling, not on a gesture, and a swipe across a toggle
    // row would otherwise flip it. A stepper still steps once on the
    // PRESSED it already had if the swipe began on its 40 px button.
    lv_obj_clear_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(
        root,
        [](lv_event_t *e) {
            auto *self = static_cast<SettingsUI *>(lv_event_get_user_data(e));
            lv_indev_t *indev = lv_indev_get_act();
            if (indev == nullptr || self->pageStack.empty()) {
                return;
            }
            const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
            if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT) {
                return;
            }
            PageEntry &top = self->pageStack.back();
            const int rows = top.def->rowCount ? top.def->rowCount(top.ctx) : 0;
            const int pages = rows > 0 ? (rows + kRowsPerPage - 1) / kRowsPerPage : 1;
            const int target = top.page + (dir == LV_DIR_LEFT ? 1 : -1);
            if (target < 0 || target >= pages) {
                return; // at an edge: nothing to show, keep the press alive
            }
            lv_indev_wait_release(indev);
            self->gotoPage(target);
        },
        LV_EVENT_GESTURE, this);

    const int totalRows = entry.def->rowCount ? entry.def->rowCount(entry.ctx) : 0;
    const int totalPages = totalRows > 0 ? (totalRows + kRowsPerPage - 1) / kRowsPerPage : 1;
    if (entry.page >= totalPages) {
        entry.page = totalPages - 1;
    }
    if (entry.page < 0) {
        entry.page = 0;
    }

    // Header: previous-page arrow, title+indicator column, next-page arrow,
    // all centred on one row so the arrows' 56x56 hit pad never has to
    // compete with a stacked title band for the ~68 px available between
    // the status icons and the row block. 240 px wide: the arrows' hit
    // boxes then span x 82..138 at y -188..-132, and the far corner sits
    // 227.4 px from the centre, inside the 228 px edge rule; the title gets
    // the 144 px between them on one line ("Animation" in montserrat 24 is
    // about 125 px; the old 96 px column broke it as "Animatio" / "n").
    // The pages sit side by side in the reader's mind, so the arrows point
    // left and right (owner's request, 2026-09-09; they were up and down
    // chevrons before) and a horizontal swipe on the page does the same,
    // see the GESTURE handler on the root above.
    lv_obj_t *header = lv_obj_create(root);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, 240, 56);
    lv_obj_align(header, LV_ALIGN_CENTER, 0, -160);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    // Every container between a clickable and the cover must bubble, or the
    // cover's PRESSED handler (the standby timer's only view of settings
    // activity) never sees the arrow presses.
    lv_obj_add_flag(header, LV_OBJ_FLAG_EVENT_BUBBLE);
    // Same clipping the row slots below need the flag for (see the loop
    // building kRowY): the arrows' 8 px ext click pad has to reach
    // past this container's own 200 px width to make their 56x56 hit box,
    // and with no spare margin between them and the title column there was
    // nowhere for that pad to go (measured: both arrows audited at 48x56).
    lv_obj_add_flag(header, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 8, LV_PART_MAIN);

    lv_obj_t *upArrow = lv_img_create(header);
    lv_img_set_src(upArrow, &img_angle_left_40x40);
    lv_obj_set_style_img_recolor(upArrow, fg, LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(upArrow, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(upArrow, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_ext_click_area(upArrow, 8); // 40x40 visual -> 56x56 hit box
    lv_obj_add_event_cb(
        upArrow,
        [](lv_event_t *e) {
            auto *self = static_cast<SettingsUI *>(lv_event_get_user_data(e));
            if (!self->pageStack.empty()) {
                self->gotoPage(self->pageStack.back().page - 1);
            }
        },
        LV_EVENT_CLICKED, this);
    if (entry.page == 0) {
        lv_obj_add_flag(upArrow, LV_OBJ_FLAG_HIDDEN);
    }
    tag(upArrow, "page_prev", "page_prev");

    lv_obj_t *mid = lv_obj_create(header);
    lv_obj_remove_style_all(mid);
    lv_obj_set_size(mid, 144, LV_SIZE_CONTENT);
    lv_obj_clear_flag(mid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(mid, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_flex_flow(mid, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(mid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(mid);
    lv_label_set_text(title, entry.def->title);
    // One line: LONG_DOT only truncates once the height is fixed too, and
    // with LV_SIZE_CONTENT height it wrapped the text instead.
    lv_obj_set_size(title, 144, lv_font_get_line_height(&lv_font_montserrat_24));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, fg, LV_PART_MAIN);

    lv_obj_t *indicator = lv_label_create(mid);
    char indBuf[12];
    snprintf(indBuf, sizeof(indBuf), "%d/%d", entry.page + 1, totalPages);
    lv_label_set_text(indicator, indBuf);
    lv_obj_set_style_text_font(indicator, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(indicator, fg, LV_PART_MAIN);

    lv_obj_t *downArrow = lv_img_create(header);
    lv_img_set_src(downArrow, &img_angle_right_40x40);
    lv_obj_set_style_img_recolor(downArrow, fg, LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(downArrow, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(downArrow, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_ext_click_area(downArrow, 8);
    lv_obj_add_event_cb(
        downArrow,
        [](lv_event_t *e) {
            auto *self = static_cast<SettingsUI *>(lv_event_get_user_data(e));
            if (!self->pageStack.empty()) {
                self->gotoPage(self->pageStack.back().page + 1);
            }
        },
        LV_EVENT_CLICKED, this);
    if (entry.page >= totalPages - 1) {
        lv_obj_add_flag(downArrow, LV_OBJ_FLAG_HIDDEN);
    }
    tag(downArrow, "page_next", "page_next");

    // Five 320x56 row slots, contiguous and centred (matches the epic's
    // shared contract); positions hand-verified to keep every corner inside
    // the 228 px safety radius and clear of the header above and the
    // chevron below.
    static constexpr int kRowY[kRowsPerPage] = {-97, -41, 15, 71, 127};
    static constexpr const char *const kRowNames[kRowsPerPage] = {"row0", "row1", "row2", "row3", "row4"};
    for (int i = 0; i < kRowsPerPage; i++) {
        lv_obj_t *slot = lv_obj_create(root);
        lv_obj_remove_style_all(slot);
        lv_obj_set_size(slot, kRowW, kRowH);
        lv_obj_align(slot, LV_ALIGN_CENTER, 0, kRowY[i]);
        lv_obj_clear_flag(slot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(slot, LV_OBJ_FLAG_EVENT_BUBBLE);
        // A row widget's rightmost control can sit flush against this
        // slot's own edge (no spare margin, by design: label column + gaps
        // + two 40 px controls fill kRowW exactly), so its ext click pad
        // needs to extend past the slot's bounds for hit-testing. Without
        // this flag the slot clips that pad a second time at the same
        // boundary its own row container already stopped clipping at
        // (SettingsRows.cpp's createRowContainer sets the same flag on the
        // row itself; both ancestors need it, since lv_indev_search_obj
        // clips at every ancestor lacking it, not just the nearest one).
        // No row widget draws visible content past its own 320x56 bounds,
        // so this has no visual effect for any category.
        lv_obj_add_flag(slot, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        lv_obj_set_style_bg_opa(slot, LV_OPA_TRANSP, LV_PART_MAIN);
        // Role "slot", not "row": a category row widget tags its own outer
        // container "row" (or the whole-row target role: toggle, action,
        // confirm), and rig.rows_on_page() lists those; the slot is chrome.
        tag(slot, kRowNames[i], "slot");
        const int globalIndex = entry.page * kRowsPerPage + i;
        if (globalIndex < totalRows && entry.def->buildRow) {
            entry.def->buildRow(entry.ctx, globalIndex, slot, *this);
        }
    }

    buildExitChevron(root, fg, /*topLevel=*/false);

    ui_.applyPressedFeedbackTo(root);
    builtThemeIdx = static_cast<int>(themeIdx);
    builtAccent = theme_colors[themeIdx][0];
}

// ---------------------------------------------------------------------------
// Debug tags

void SettingsUI::tagTilePage(lv_obj_t *obj, const char *row, const char *role) {
    if (tilePageTagsUsed >= kMaxTilePageTags) {
        return;
    }
    SettingsDebugTag *t = &tilePageTags[tilePageTagsUsed++];
    *t = {row, role, nullptr};
    lv_obj_set_user_data(obj, t);
}

void SettingsUI::tag(lv_obj_t *obj, const char *row, const char *role, const char *text) {
    if (pageStack.empty()) {
        return;
    }
    PageEntry &top = pageStack.back();
    if (top.tagsUsed >= PageEntry::kMaxTags) {
        // An untagged value label makes settingsRowSetValue a silent no-op
        // (findByRole finds nothing), so the row would render blank forever.
        ESP_LOGW("SettingsUI", "SettingsUI: tag cap %d reached, %s/%s untagged", PageEntry::kMaxTags, row, role);
        return;
    }
    SettingsDebugTag *t = &top.tags[top.tagsUsed++];
    *t = {row, role, text};
    lv_obj_set_user_data(obj, t);
}

// ---------------------------------------------------------------------------
// Debug-route accessors

SettingsUI::State SettingsUI::state() const {
    State s;
    s.open = coverObj != nullptr;
    s.depth = static_cast<int>(pageStack.size());
    if (pageStack.empty()) {
        s.category = -1;
        s.page = 0;
        s.pages = 0;
        s.title = "Settings";
        return s;
    }
    const PageEntry &top = pageStack.back();
    s.category = -1;
    for (int i = 0; i < kCategoryCount; i++) {
        if (kCategories[i] == top.def) {
            s.category = i;
            break;
        }
    }
    s.page = top.page;
    const int rowCount = top.def->rowCount ? top.def->rowCount(top.ctx) : 0;
    s.pages = rowCount > 0 ? (rowCount + kRowsPerPage - 1) / kRowsPerPage : 1;
    s.title = top.def->title;
    return s;
}

SettingsUI::FixtureCounters SettingsUI::fixtureCounters() const {
#if defined(GM_TOUCH_PROBE) || defined(GAGGIMATE_SIM)
    void *liveCtx = (!pageStack.empty() && pageStack.back().def == &kCatFixture) ? pageStack.back().ctx : nullptr;
    return fixtureCountersFor(liveCtx);
#else
    return FixtureCounters{};
#endif
}
