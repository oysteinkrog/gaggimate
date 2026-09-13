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
// screens' icons and buttons, so a held tile, row, arrow or chevron reads
// the same as a held menu button. Measured 2026-09-06 on the simulator:
// the generated menu buttons dim 19 to 29 units per pixel of hit box, the
// settings tiles dimmed 0 (no feedback at all) and the whole-row targets
// went fully black; both now use this.
lv_color_t settingsPressedColor(lv_color_t rest, lv_color_t dim);

using SettingsRowStepFn = void (*)(void *user, int dir, bool fast);
using SettingsRowCycleFn = void (*)(void *user, int dir);
using SettingsRowToggleFn = void (*)(void *user, bool value);
using SettingsRowActivateFn = void (*)(void *user);
using SettingsRowUnlockedFn = void (*)(void *user);
using SettingsRowConfirmFn = void (*)(void *user);

// Minus/plus at the right; onStep(user, dir, fast) once on LV_EVENT_PRESSED
// (dir -1/+1, fast always false) and once per LV_EVENT_LONG_PRESSED_REPEAT
// while a button is held (fast true once the hold has run kSettingsRowFastHoldMs).
// Never called from LV_EVENT_CLICKED.
lv_obj_t *settingsRowStepperCreate(SettingsUI &ui, lv_obj_t *parent, const char *rowName, const char *label,
                                    SettingsRowStepFn onStep, void *user);

// '<'/'>' arrows either side of the value; onCycle(user, dir) with the same
// press-and-repeat protocol as the stepper, minus the fast flag.
lv_obj_t *settingsRowChoiceCreate(SettingsUI &ui, lv_obj_t *parent, const char *rowName, const char *label,
                                   SettingsRowCycleFn onCycle, void *user);

// Whole row is the target; value is "On"/"Off", managed by the row itself
// (initial sets the starting state), so onToggle is a notification only,
// not the place a caller formats the display text.
lv_obj_t *settingsRowToggleCreate(SettingsUI &ui, lv_obj_t *parent, const char *rowName, const char *label,
                                   bool initial, SettingsRowToggleFn onToggle, void *user);

// Whole row is the target; onActivate(user) once per completed tap
// (LV_EVENT_CLICKED). Value is optional and caller-managed via
// settingsRowSetValue, left blank if the caller never calls it (a plain
// navigation row).
lv_obj_t *settingsRowActionCreate(SettingsUI &ui, lv_obj_t *parent, const char *rowName, const char *label,
                                   SettingsRowActivateFn onActivate, void *user);

// A stepper (same onStep protocol) that starts locked: the minus/plus
// buttons are hidden and the value line shows "hold to unlock" with a
// growing progress underline while the whole row is held. The buttons are
// revealed and the row's own hit area is cleared (so it stops competing with
// them for the audit's overlap check) only at release, and only if the hold
// ran kSettingsRowUnlockHoldMs; this is also what stops the press that
// crossed the threshold from then landing on a freshly-revealed button
// before the finger lifts. onUnlocked(user) fires once, at that release.
// Releasing early, or GM_TOUCH_PROBE/GAGGIMATE_SIM builds calling
// settingsRowSetLocked(row, false) directly, cancels/relocks without it.
lv_obj_t *settingsRowLockedCreate(SettingsUI &ui, lv_obj_t *parent, const char *rowName, const char *label,
                                   SettingsRowStepFn onStep, SettingsRowUnlockedFn onUnlocked, void *user);
void settingsRowSetLocked(lv_obj_t *row, bool locked);

// Whole row is the target; holding it kSettingsRowConfirmHoldMs with visible
// progress calls onConfirm(user) once (fired the moment the threshold is
// crossed, not deferred to release: nothing else on the row is revealed
// mid-press for a continued hold to land on by surprise, unlike the locked
// row). Releasing before the threshold cancels without calling it. Value is
// optional and caller-managed, same as the action row.
lv_obj_t *settingsRowConfirmCreate(SettingsUI &ui, lv_obj_t *parent, const char *rowName, const char *label,
                                    SettingsRowConfirmFn onConfirm, void *user);

// Label and value only; not clickable, no controls.
lv_obj_t *settingsRowInfoCreate(SettingsUI &ui, lv_obj_t *parent, const char *rowName, const char *label);

// ---- swatch ------------------------------------------------------------------

// The colour ramp a swatch row draws is handed to it as samples, left to
// right, so this file knows nothing about gradients: what a sample means is
// the caller's business (settingsui::swatchBuildRamp565, GradientSwatch.h,
// builds the ones the gradient picker uses). One sample per pixel column of
// the swatch.
constexpr int kSettingsRowSwatchSamples = 96;

// An action row (whole row is the target, same onActivate protocol) with a
// colour ramp at its right and a marker dot that says this is the entry
// currently in force. Built for the gradient picker (gm-nov3.3): a list of
// sixty gradient names carries no meaning without the colours next to it.
//
// The swatch is an lv_canvas over a row-owned RGB565 buffer from LVGL's heap
// (PSRAM on the device), released with the row. Until
// settingsRowSetSwatch() is called the swatch is hidden, which is also what a
// disabled row wants: applyEnabledRecurse dims labels and images by exact
// class and a canvas is neither, so a row that has no gradient to show hides
// the swatch rather than dimming it.
lv_obj_t *settingsRowSwatchCreate(SettingsUI &ui, lv_obj_t *parent, const char *rowName, const char *label,
                                   SettingsRowActivateFn onActivate, void *user);

// A swatch row that also steps (gm-nov3.32): three targets in one slot, the
// centre band and a prev and a next arrow. Tapping the band calls onActivate,
// same as the plain swatch row above; the arrows call onCycle(user, dir) with
// the choice row's press-and-repeat protocol (once on PRESSED, once per
// LV_EVENT_LONG_PRESSED_REPEAT, and no fast tier, because ten gradients a
// second is already fast and a fast tier would skip the one being looked for).
//
// The band is kTextColW wide, so the two arrows land exactly where a choice
// row's do and the geometry audit sees the same three rectangles it already
// passes there. That leaves the band 120 px narrower than a whole-row target,
// so its ramp is a bar across its foot rather than a block at its right: the
// name needs the width more than the ramp does. The marker dot the picker
// uses has no place here (nothing marks a category row as "in force"), so
// this kind has none and settingsRowSetSelected is a no-op on it.
lv_obj_t *settingsRowSwatchStepCreate(SettingsUI &ui, lv_obj_t *parent, const char *rowName, const char *label,
                                       SettingsRowActivateFn onActivate, SettingsRowCycleFn onCycle, void *user);

// Paints the row's swatch from kSettingsRowSwatchSamples RGB565 samples, or
// hides it when `ramp` is null. No-op on a row that is not a swatch row.
// Both swatch kinds take it; a ramp wider or narrower than the row's own
// canvas is sampled across it.
void settingsRowSetSwatch(lv_obj_t *row, const uint16_t *ramp);

// Shows or hides the row's marker dot (tagged role "selected", so a test can
// read which entry a picker page says is in force).
void settingsRowSetSelected(lv_obj_t *row, bool selected);

#endif // GM_SETTINGS_ROWS_H
