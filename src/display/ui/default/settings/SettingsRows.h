#ifndef GM_SETTINGS_ROWS_H
#define GM_SETTINGS_ROWS_H

// The seven row widgets every settings category is built from (stepper,
// choice, toggle, action, locked, confirm, info), each sized for the
// shell's 320x56 slot (SettingsUI::kRowW/kRowH). A category's buildRow
// calls exactly one of the settingsRow*Create functions below per row,
// keeps the returned lv_obj_t* (or the caller's own pointer to it) for as
// long as the row is on screen, and drives its display through
// settingsRowSetValue/settingsRowSetEnabled; the row never reaches back
// into a category's own model.
//
// Hold and progress state (elapsed time, fast flag, unlock/confirm
// progress) lives entirely in the row's own runtime context and is driven
// only by the LVGL events delivered to the row's objects (PRESSED,
// LONG_PRESSED, LONG_PRESSED_REPEAT, RELEASED, PRESS_LOST, DELETE); there
// is no free-standing lv_timer anywhere in this file, because LVGL's
// obj_del_core resets the indev on a deleted object without sending it a
// cancel event, so a timer holding a raw row pointer across a page rebuild
// mid-hold would dangle (CLAUDE.md, UI-pipeline invariants).
#include "SettingsUI.h"

#include <lvgl.h>

#include <cstddef>
#include <cstdint>

// 2 s of continuous hold (measured from LV_EVENT_PRESSED) is when a stepper
// reports fast=true and a locked/confirm row's held-down progress is judged
// against its own threshold below.
constexpr uint32_t kSettingsRowFastHoldMs = 2000;
constexpr uint32_t kSettingsRowUnlockHoldMs = 1000;
constexpr uint32_t kSettingsRowConfirmHoldMs = 2000;

// settingsRowSetValue's canonical storage, bounded so a row never grows an
// unbounded string mid-life. "America/Argentina/ComodRivadavia" (33 chars,
// the longest value the epic's shared contract names) fits with room to
// spare.
constexpr size_t kSettingsRowValueCap = 48;

// Copies value into the row's own canonical buffer (the address the row's
// "value"-role SettingsDebugTag carries) and updates the displayed label,
// which truncates with LV_LABEL_LONG_DOT if it does not fit; the tag keeps
// exporting the untruncated text for the touchmap dump. No-op if row has no
// value object (info rows always have one; every kind does).
void settingsRowSetValue(lv_obj_t *row, const char *value);

// Dims every label/image descendant, adds LV_STATE_DISABLED to every
// clickable descendant (LVGL's own lv_obj_hit_test refuses a disabled
// object, so a held button stops receiving PRESSING/repeat the moment its
// re-search no longer matches it, which is also what cancels a hold in
// progress: no separate cancellation path is needed here). Re-enabling
// starts clean because the only state a hold carries, a press timestamp,
// was already reset by the PRESS_LOST that disabling produced.
void settingsRowSetEnabled(lv_obj_t *row, bool enabled);

// The pressed colour every settings target uses: 40% of the way from its
// rest colour toward the touch dim colour (Settings::getTouchDimColor),
// the same shift DefaultUI::applyPressedFeedbackTo gives the generated
// screens' icons and buttons, so a held tile, row, arrow or exit button reads
// the same as a held menu button. Measured 2026-09-06 on the simulator:
// the generated menu buttons dim 19 to 29 units per pixel of hit box, the
// settings tiles dimmed 0 (no feedback at all) and the whole-row targets
// went fully black; both now use this.
lv_color_t settingsPressedColor(lv_color_t rest, lv_color_t dim);

// The frame every settings target draws (gm-3vj.50): a 2 px border in fg,
// radius 8, no fill, and a 2 px ext click pad, so an object 4 px smaller
// than its hit box on each axis draws its outline 2 px inside that box.
// Under LV_STATE_DISABLED the border goes transparent: a target that
// cannot be pressed shows no frame.
void settingsStyleFrame(lv_obj_t *obj, lv_color_t fg);

// A framed button with a hit box hitW wide and 56 tall: an icon, a short
// text, or both, centred, in the theme colour. Clickable, bubbling, press
// lock cleared, and recoloured toward the touch dim colour while pressed
// (settingsPressedColor) unless the compositor's plate is the feedback.
// The caller positions it and wires its action. Used by the row controls,
// the page header's arrows and the exit button.
lv_obj_t *settingsFrameButtonCreate(SettingsUI &ui, lv_obj_t *parent, lv_coord_t hitW, const lv_img_dsc_t *icon,
                                    const char *text);

using SettingsRowStepFn = void (*)(void *user, int dir, bool fast);
using SettingsRowCycleFn = void (*)(void *user, int dir);
using SettingsRowToggleFn = void (*)(void *user, bool value);
using SettingsRowActivateFn = void (*)(void *user);
using SettingsRowUnlockedFn = void (*)(void *user);
using SettingsRowConfirmFn = void (*)(void *user);

// Plain text column, then minus and plus as two framed 56x56 buttons at the
// right; onStep(user, dir, fast) once on LV_EVENT_PRESSED
// (dir -1/+1, fast always false) and once per LV_EVENT_LONG_PRESSED_REPEAT
// while a button is held (fast true once the hold has run kSettingsRowFastHoldMs).
// Never called from LV_EVENT_CLICKED.
lv_obj_t *settingsRowStepperCreate(SettingsUI &ui, lv_obj_t *parent, const char *rowName, const char *label,
                                    SettingsRowStepFn onStep, void *user);

// Plain text column, then '<' and '>' as two framed 56x56 buttons at the
// right; onCycle(user, dir) with the same
// press-and-repeat protocol as the stepper, minus the fast flag.
lv_obj_t *settingsRowChoiceCreate(SettingsUI &ui, lv_obj_t *parent, const char *rowName, const char *label,
                                   SettingsRowCycleFn onCycle, void *user);

// Whole row is the target and draws the frame, with a switch outline at the
// right whose knob shows the state; value is "On"/"Off", managed by the row itself
// (initial sets the starting state), so onToggle is a notification only,
// not the place a caller formats the display text.
lv_obj_t *settingsRowToggleCreate(SettingsUI &ui, lv_obj_t *parent, const char *rowName, const char *label,
                                   bool initial, SettingsRowToggleFn onToggle, void *user);

// Whole row is the target and draws the frame, with "Tap" at the right;
// onActivate(user) once per completed tap
// (LV_EVENT_CLICKED). Value is optional and caller-managed via
// settingsRowSetValue, left blank if the caller never calls it (a plain
// navigation row).
lv_obj_t *settingsRowActionCreate(SettingsUI &ui, lv_obj_t *parent, const char *rowName, const char *label,
                                   SettingsRowActivateFn onActivate, void *user);

// A stepper (same onStep protocol) that starts locked: the minus/plus
// buttons are hidden, the value line shows "hold to unlock", and a framed
// "Hold 1 s" button in their place is the target, with a growing progress
// underline inside it while it is held. The buttons are revealed and the
// Hold button hidden (so it stops competing with them for the audit's
// overlap check) only at release, and only if the hold ran
// kSettingsRowUnlockHoldMs; this is also what stops the press that
// crossed the threshold from then landing on a freshly-revealed button
// before the finger lifts. onUnlocked(user) fires once, at that release.
// Releasing early, or GM_TOUCH_PROBE/GAGGIMATE_SIM builds calling
// settingsRowSetLocked(row, false) directly, cancels/relocks without it.
lv_obj_t *settingsRowLockedCreate(SettingsUI &ui, lv_obj_t *parent, const char *rowName, const char *label,
                                   SettingsRowStepFn onStep, SettingsRowUnlockedFn onUnlocked, void *user);
void settingsRowSetLocked(lv_obj_t *row, bool locked);

// Whole row is the target and draws the frame, with "Hold 2 s" at the right;
// holding it kSettingsRowConfirmHoldMs with visible
// progress calls onConfirm(user) once (fired the moment the threshold is
// crossed, not deferred to release: nothing else on the row is revealed
// mid-press for a continued hold to land on by surprise, unlike the locked
// row). Releasing before the threshold cancels without calling it. Value is
// optional and caller-managed, same as the action row.
lv_obj_t *settingsRowConfirmCreate(SettingsUI &ui, lv_obj_t *parent, const char *rowName, const char *label,
                                    SettingsRowConfirmFn onConfirm, void *user);

// Label and value only; not clickable, no controls.
lv_obj_t *settingsRowInfoCreate(SettingsUI &ui, lv_obj_t *parent, const char *rowName, const char *label);

#endif // GM_SETTINGS_ROWS_H
