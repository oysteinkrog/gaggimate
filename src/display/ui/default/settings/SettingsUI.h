#ifndef GM_SETTINGS_UI_H
#define GM_SETTINGS_UI_H

// The on-display settings shell: a full-screen cover over the menu screen,
// a tile page (one tile per category) and a paged list page (title, page
// arrows, five row slots, exit chevron). This file is the contract the
// category beads code against: the struct shapes and extern symbols below
// must keep their names; only the categories' own content is out of scope
// here (see CLAUDE.md, on-display settings epic).
//
// Forward-declared rather than included: Controller.h includes DefaultUI.h,
// and DefaultUI.h includes this header for its settingsUI member, so a full
// Controller.h (or PluginManager.h) include here would cycle back into
// DefaultUI.h before it finishes parsing. SettingsUI.cpp includes the real
// headers; this header only stores references to them.
class Controller;
class DefaultUI;
class PluginManager;

#include <lvgl.h>

#include <atomic>
#include <utility>
#include <vector>

class SettingsUI;

// Carried in every tagged settings object's lv_obj_t::user_data so the
// touchmap dump (a later bead) can identify it without guessing from class
// or position. `row` is a stable identifier for whatever the object is (a
// row, a tile, an arrow, the exit chevron, the cover itself), not literally
// limited to row objects. `text` is non-null only for role "value": the
// address of the row-owned canonical string, set once, whose contents
// settingsRowSetValue (a later bead) rewrites in place: the label's own
// buffer holds truncation dots when long, this pointer holds the real value.
struct SettingsDebugTag {
    const char *row;
    const char *role;
    const char *text;
};

// One settings category: a tile on the tile page and the page(s) of rows it
// pushes when opened. rowCount may change while the page is open (a schedule
// list growing/shrinking); buildRow is called once per visible row per
// (re)build, never told to update in place. ctx is the category's draft,
// allocated by the shell at push time and freed after the last commit; a
// stateless category (no editable content) sets createCtx/destroyCtx to
// nullptr and every ctx-taking callback receives nullptr.
struct SettingsCategoryDef {
    const char *title;        // tile caption and page title
    const lv_img_dsc_t *icon; // 40x40 tile icon
    int (*rowCount)(void *ctx);
    void (*buildRow)(void *ctx, int index, lv_obj_t *parent, SettingsUI &ui); // into the 320x56 slot
    void (*enter)(void *ctx);     // once when the page is pushed: snapshot Settings into the draft
    void (*refresh)(void *ctx);   // once per second while visible, may be nullptr
    void (*commit)(void *ctx);    // exactly once when the page is removed, by any route
    void (*reconcile)(void *ctx); // after a web save: re-read untouched fields, may be nullptr
    void *(*createCtx)();         // nullptr for a stateless category
    void (*destroyCtx)(void *ctx);
};

// The five real categories, one definition each in its own Cat<Name>.cpp.
// While the category beads landed one at a time these were stand-ins in a
// file of their own, overridden at link time; that file is gone now that all
// five are real, so a category with no definition is a link error rather
// than a page of numbered stub labels nobody notices.
extern const SettingsCategoryDef kCatTemps;
extern const SettingsCategoryDef kCatDisplay;
extern const SettingsCategoryDef kCatAnimation;
extern const SettingsCategoryDef kCatMachine;
extern const SettingsCategoryDef kCatStatus;

// The sixth, bench/sim-only tile (SettingsFixture.cpp): one of each row
// widget, so the shell's lifecycle and the widgets themselves stay
// exercisable on their own, whatever the five real categories do. It never
// had a stand-in and never needed one.
#if defined(GM_TOUCH_PROBE) || defined(GAGGIMATE_SIM)
extern const SettingsCategoryDef kCatFixture;
#endif

class SettingsUI {
  public:
    SettingsUI(Controller &controller, DefaultUI &ui, PluginManager &plugins);

    static constexpr int kRowW = 320;
    static constexpr int kRowH = 56;
    static constexpr int kRowsPerPage = 5;
    static int rowsPerPage() { return kRowsPerPage; }

    // No-op while already open (close()/onExternalLeave() likewise while
    // already closed). Hides every direct child of the menu screen except
    // the status icons, creates the transparent click-catching cover and
    // shows the tile page.
    void open();
    // Leaves whatever pages are open (each commits once, top-down), restores
    // the menu screen's captured child visibility and deletes the cover.
    void close();
    bool isOpen() const { return coverObj != nullptr; }
    // Called every UI pass, on the simulator too: web-save reconciliation,
    // the 1 s refresh tick and the theme/accent restyle check.
    void service();
    // handleScreenChange calls this before eez_flow_set_screen whenever
    // settings is open: same teardown as close(), so a screen change forced
    // by the standby timeout, a mode change or a brew start never leaves a
    // pending edit uncommitted or a stale cover behind.
    void onExternalLeave();

    // Opens the tile at `index` into kCategories (see SettingsUI.cpp):
    // allocates its ctx, enters it under Settings::Guard and pushes its page.
    void openCategory(int index);
    // Generic push, for a category whose own row handler opens a child page
    // (the pushed def's ctx may itself reference the parent's ctx). enter()
    // runs under Settings::Guard.
    void pushPage(const SettingsCategoryDef *def, void *ctx);
    // Commits the top page (Settings::Guard), destroys its ctx and root,
    // and reveals whatever is now on top (the tile page, or the parent page,
    // rebuilt so it reflects any edit the child made to a shared draft,
    // without re-entering it).
    void popPage();
    // Clamps `page` to the top category's current page count and rebuilds
    // its five row slots from buildRow. Never calls enter or commit.
    void gotoPage(int page);
    // Fully recreates the top page's header, arrows and row slots for its
    // current page from the category's def/ctx/page (theme restyle and the
    // web-reconcile path use this too; a held button is released by it).
    void rebuildPage();

    // Tags an object built by category code (a value label, a stepper's
    // +/- buttons, ...) into the currently-open page's tag set. Only valid
    // while a category page is being built or is open; the shell tags its
    // own chrome (tiles, arrows, the exit chevron, row slots, the cover)
    // itself.
    void tag(lv_obj_t *obj, const char *row, const char *role, const char *text = nullptr);

    Controller &controller() const { return controller_; }
    DefaultUI &ui() const { return ui_; }
    PluginManager &plugins() const { return plugins_; }

    lv_obj_t *cover() const { return coverObj; }

    struct State {
        bool open = false;
        int depth = 0;    // 0 = tile page, 1+ = pushed category pages
        int category = -1; // index into kCategories, -1 at depth 0
        int page = 0;      // 0-based
        int pages = 0;
        const char *title = "Settings";
    };
    State state() const;

    struct FixtureCounters {
        int enter = 0;
        int commit = 0;
        int draft = 0;
        // Added for the row-widget bead (gm-flw.3): live while the Fixture
        // category is open, default otherwise (see fixtureCounters()'s
        // definition and fixtureCountersFor() below).
        int action = 0;
        int confirm = 0;
        bool locked = true;
        int repeats = 0;
        int fastRepeats = 0;
    };
    FixtureCounters fixtureCounters() const;

    // WebUIPlugin's async task fires settings:changed; DefaultUI's listener
    // calls this. service() consumes the flag with an exchange.
    void notifyWebSaved() { webSaved_.store(true, std::memory_order_relaxed); }

  private:
    struct PageEntry {
        const SettingsCategoryDef *def = nullptr;
        void *ctx = nullptr;
        int page = 0;
        lv_obj_t *root = nullptr;
        static constexpr int kMaxTags = 32;
        SettingsDebugTag tags[kMaxTags];
        int tagsUsed = 0;
    };

    void restoreMenuChildren();
    // Commits and tears down every pushed page (top-down), used by both
    // close() and onExternalLeave() so there is one drain path regardless
    // of how many categories are open when it runs.
    void teardownAll();

    void buildTilePage();
    void buildTile(lv_obj_t *parent, int index, const SettingsCategoryDef *def, lv_color_t fg);
    // topLevel: closes settings outright (tile page's chevron) vs. pops one
    // page (a category page's chevron).
    void buildExitChevron(lv_obj_t *parent, lv_color_t fg, bool topLevel);
    void buildCategoryPage(PageEntry &entry);
    void tagTilePage(lv_obj_t *obj, const char *row, const char *role);

    Controller &controller_;
    DefaultUI &ui_;
    PluginManager &plugins_;

    lv_obj_t *coverObj = nullptr;
    lv_obj_t *tilePageObj = nullptr;
    static constexpr int kMaxTilePageTags = 8;
    SettingsDebugTag tilePageTags[kMaxTilePageTags];
    int tilePageTagsUsed = 0;
    SettingsDebugTag coverTag{};

    std::vector<std::pair<lv_obj_t *, bool>> capturedVisible; // (child, was-hidden)
    std::vector<PageEntry> pageStack;

    // What the currently-visible content (tile page or top of stack) was
    // last built/colored with; service() compares against the live theme
    // index/accent each pass.
    int builtThemeIdx = -1;
    uint32_t builtAccent = 0;

    unsigned long lastRefreshMs = 0;
    std::atomic<bool> webSaved_{false};

    struct TileClickCtx {
        SettingsUI *self;
        int index;
    };
    TileClickCtx tileClickCtx[8]{};
};

#if defined(GM_TOUCH_PROBE) || defined(GAGGIMATE_SIM)
// Defined in SettingsFixture.cpp. liveCtx is the Fixture category's ctx
// (a FixtureCtx*, opaque here) when it is the currently open page, else
// nullptr. enter/commit/draft come from SettingsFixture.cpp's own
// process-lifetime counters either way (draft is overwritten from liveCtx
// when non-null, same as before this bead); action/confirm/locked/repeats/
// fastRepeats reflect the live session and read as their default (0/0/
// true/0/0) when liveCtx is null. SettingsUI::fixtureCounters() (SettingsUI.cpp)
// is the only caller, so SettingsUI.cpp itself never needs FixtureCtx's
// layout.
SettingsUI::FixtureCounters fixtureCountersFor(void *liveCtx);
#endif

#endif // GM_SETTINGS_UI_H
