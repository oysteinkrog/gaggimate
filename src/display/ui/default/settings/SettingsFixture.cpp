// The Fixture category (GM_TOUCH_PROBE and GAGGIMATE_SIM builds only): one
// of each row widget (SettingsRows.h) over 11 rows / 3 pages, so the
// widgets and the shell's lifecycle (push/page/pop, commit-once, web-save
// reconcile) are exercisable on the bench and the simulator before any real
// category exists. Moved out of SettingsUI.cpp by the row-widget bead
// (gm-flw.3), which is also when the widgets themselves arrived; before
// that this category's 11 rows were all plain labels.
//
// The enter/commit counters live for the process lifetime, not the ctx's: a
// second open must show enter=2, not enter=1 again. The row counters below
// (action, confirm, repeats, fastRepeats) are the opposite: they live in
// FixtureCtx and reset every time the category is freshly entered, because
// nothing in the epic needs them to survive a close.
#if defined(GM_TOUCH_PROBE) || defined(GAGGIMATE_SIM)

#include "SettingsRows.h"
#include "SettingsUI.h"

#include <display/ui/default/eez/images.h>

#include <Arduino.h>
#include <cstdio>

#include "esp_log.h"

namespace {

struct FixtureCtx {
    int draft = 0;         // stepper (row 1), step 1 / fast step 4
    int choiceIndex = 0;   // choice (row 2), 3 entries
    int actionCount = 0;   // action (row 4)
    int lockedDraft = 0;   // locked stepper (row 5), same step/fast-step as the plain stepper
    bool lockedLocked = true;
    int confirmCount = 0;  // confirm (row 6)
    // Every onStep call on the plain stepper bumps one of these (never
    // both): repeats counts calls with fast==false, which includes the
    // single LV_EVENT_PRESSED call as well as any slow LV_EVENT_LONG_PRESSED_REPEAT
    // ones, and fastRepeats counts calls with fast==true (always a repeat,
    // since fast never applies to the initial press). This is deliberate,
    // not an oversight: the debug route's contract is draft_delta ==
    // repeats*step + fastRepeats*fastStep, and a press contributes exactly
    // step*dir, the same as a slow repeat, so folding it into "repeats"
    // keeps that arithmetic exact without needing to tell a press apart
    // from a repeat (onStep's public signature carries no such flag).
    int stepRepeats = 0;
    int stepFastRepeats = 0;

    // Nulled by each row's own LV_EVENT_DELETE (the "delete-event pointer
    // cleanup" precedent this epic already uses for buildScaleScreen), so a
    // page rebuild between two onStep/refresh calls never leaves one of
    // these dangling.
    lv_obj_t *stepperRow = nullptr;
    lv_obj_t *choiceRow = nullptr;
    lv_obj_t *actionRow = nullptr;
    lv_obj_t *lockedRow = nullptr;
    lv_obj_t *confirmRow = nullptr;
    lv_obj_t *uptimeRow = nullptr;
};

int g_fixtureEnterCount = 0;
int g_fixtureCommitCount = 0;
int g_fixtureLastDraft = 0;

constexpr const char *kChoiceLabels[3] = {"Low", "Medium", "High"};

void stepperOnStep(void *user, int dir, bool fast) {
    auto *fc = static_cast<FixtureCtx *>(user);
    fc->draft += (fast ? 4 : 1) * dir;
    if (fast) {
        fc->stepFastRepeats++;
    } else {
        fc->stepRepeats++;
    }
    if (fc->stepperRow != nullptr) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", fc->draft);
        settingsRowSetValue(fc->stepperRow, buf);
    }
}

void choiceOnCycle(void *user, int dir) {
    auto *fc = static_cast<FixtureCtx *>(user);
    fc->choiceIndex = ((fc->choiceIndex + dir) % 3 + 3) % 3;
    if (fc->choiceRow != nullptr) {
        settingsRowSetValue(fc->choiceRow, kChoiceLabels[fc->choiceIndex]);
    }
}

void actionOnActivate(void *user) {
    auto *fc = static_cast<FixtureCtx *>(user);
    fc->actionCount++;
    if (fc->actionRow != nullptr) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", fc->actionCount);
        settingsRowSetValue(fc->actionRow, buf);
    }
}

void lockedOnStep(void *user, int dir, bool fast) {
    auto *fc = static_cast<FixtureCtx *>(user);
    fc->lockedDraft += (fast ? 4 : 1) * dir;
    if (fc->lockedRow != nullptr) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", fc->lockedDraft);
        settingsRowSetValue(fc->lockedRow, buf);
    }
}

void lockedOnUnlocked(void *user) {
    auto *fc = static_cast<FixtureCtx *>(user);
    fc->lockedLocked = false;
    if (fc->lockedRow != nullptr) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", fc->lockedDraft);
        settingsRowSetValue(fc->lockedRow, buf);
    }
}

void confirmOnConfirm(void *user) {
    auto *fc = static_cast<FixtureCtx *>(user);
    fc->confirmCount++;
    if (fc->confirmRow != nullptr) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", fc->confirmCount);
        settingsRowSetValue(fc->confirmRow, buf);
    }
}

int fixtureRowCount(void * /*ctx*/) { return 11; }

void fixtureBuildRow(void *ctx, int index, lv_obj_t *parent, SettingsUI &ui) {
    auto *fc = static_cast<FixtureCtx *>(ctx);
    switch (index) {
    case 0: { // stepper
        lv_obj_t *row = settingsRowStepperCreate(ui, parent, "stepper", "Stepper", stepperOnStep, fc);
        fc->stepperRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<FixtureCtx *>(lv_event_get_user_data(e))->stepperRow = nullptr; },
            LV_EVENT_DELETE, fc);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", fc->draft);
        settingsRowSetValue(row, buf);
        break;
    }
    case 1: { // choice
        lv_obj_t *row = settingsRowChoiceCreate(ui, parent, "choice", "Choice", choiceOnCycle, fc);
        fc->choiceRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<FixtureCtx *>(lv_event_get_user_data(e))->choiceRow = nullptr; },
            LV_EVENT_DELETE, fc);
        settingsRowSetValue(row, kChoiceLabels[fc->choiceIndex]);
        break;
    }
    case 2: // toggle: the widget owns its own On/Off text; nothing for the fixture to track
        settingsRowToggleCreate(ui, parent, "toggle", "Toggle", false, nullptr, fc);
        break;
    case 3: { // action
        lv_obj_t *row = settingsRowActionCreate(ui, parent, "action", "Action", actionOnActivate, fc);
        fc->actionRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<FixtureCtx *>(lv_event_get_user_data(e))->actionRow = nullptr; },
            LV_EVENT_DELETE, fc);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", fc->actionCount);
        settingsRowSetValue(row, buf);
        break;
    }
    case 4: { // locked stepper
        lv_obj_t *row = settingsRowLockedCreate(ui, parent, "locked", "Locked", lockedOnStep, lockedOnUnlocked, fc);
        fc->lockedRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<FixtureCtx *>(lv_event_get_user_data(e))->lockedRow = nullptr; },
            LV_EVENT_DELETE, fc);
        // settingsRowLockedCreate already shows "hold to unlock" while
        // locked; nothing to push here unless the ctx says it is already
        // unlocked (a rebuild after a prior unlock in this same session).
        if (!fc->lockedLocked) {
            settingsRowSetLocked(row, false);
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", fc->lockedDraft);
            settingsRowSetValue(row, buf);
        }
        break;
    }
    case 5: { // confirm
        lv_obj_t *row = settingsRowConfirmCreate(ui, parent, "confirm", "Confirm", confirmOnConfirm, fc);
        fc->confirmRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<FixtureCtx *>(lv_event_get_user_data(e))->confirmRow = nullptr; },
            LV_EVENT_DELETE, fc);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", fc->confirmCount);
        settingsRowSetValue(row, buf);
        break;
    }
    case 6: { // info, refresh-driven uptime
        lv_obj_t *row = settingsRowInfoCreate(ui, parent, "uptime", "Uptime");
        fc->uptimeRow = row;
        lv_obj_add_event_cb(
            row, [](lv_event_t *e) { static_cast<FixtureCtx *>(lv_event_get_user_data(e))->uptimeRow = nullptr; },
            LV_EVENT_DELETE, fc);
        char buf[16];
        snprintf(buf, sizeof(buf), "%lu s", millis() / 1000);
        settingsRowSetValue(row, buf);
        break;
    }
    default: { // rows 8..11 (index 7..10): plain info, no state
        char rowName[16];
        snprintf(rowName, sizeof(rowName), "info%d", index + 1);
        char label[24];
        snprintf(label, sizeof(label), "Fixture row %d", index + 1);
        lv_obj_t *row = settingsRowInfoCreate(ui, parent, rowName, label);
        settingsRowSetValue(row, "info");
        break;
    }
    }
}

void fixtureEnter(void * /*ctx*/) { g_fixtureEnterCount++; }

// Once a second while the category is open, whichever page is showing:
// buildRow only runs when a row's slot is (re)built, so the uptime row's
// text would otherwise go stale between page rebuilds.
void fixtureRefresh(void *ctx) {
    auto *fc = static_cast<FixtureCtx *>(ctx);
    if (fc->uptimeRow == nullptr) {
        return; // page 1 (rows 5..9) is not the one currently showing
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu s", millis() / 1000);
    settingsRowSetValue(fc->uptimeRow, buf);
}

void fixtureCommit(void *ctx) {
    g_fixtureCommitCount++;
    auto *fc = static_cast<FixtureCtx *>(ctx);
    g_fixtureLastDraft = fc->draft;
    ESP_LOGI("SettingsUI", "SettingsFixture: enter=%d commit=%d draft=%d", g_fixtureEnterCount, g_fixtureCommitCount,
             fc->draft);
}

void *fixtureCreateCtx() { return new FixtureCtx(); }

void fixtureDestroyCtx(void *ctx) { delete static_cast<FixtureCtx *>(ctx); }

} // namespace

const SettingsCategoryDef kCatFixture = {
    "Fixture", &img_check_40x40, fixtureRowCount, fixtureBuildRow, fixtureEnter, fixtureRefresh, fixtureCommit,
    nullptr, fixtureCreateCtx, fixtureDestroyCtx,
};

SettingsUI::FixtureCounters fixtureCountersFor(void *liveCtx) {
    SettingsUI::FixtureCounters c;
    c.enter = g_fixtureEnterCount;
    c.commit = g_fixtureCommitCount;
    c.draft = g_fixtureLastDraft;
    if (liveCtx != nullptr) {
        const auto *fc = static_cast<const FixtureCtx *>(liveCtx);
        c.draft = fc->draft;
        c.action = fc->actionCount;
        c.confirm = fc->confirmCount;
        c.locked = fc->lockedLocked;
        c.repeats = fc->stepRepeats;
        c.fastRepeats = fc->stepFastRepeats;
    }
    return c;
}

#endif // GM_TOUCH_PROBE || GAGGIMATE_SIM
