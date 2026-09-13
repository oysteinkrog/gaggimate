#ifndef GM_CAT_GRADIENT_PICKER_H
#define GM_CAT_GRADIENT_PICKER_H

// The gradient picker (gm-nov3.3): two pushed pages that let one row of the
// Animation category choose a gradient by looking at it.
//
// It replaces three prev/next arrow cycles over one flat list. The arrows
// were never slow (they wrap and repeat on hold, so crossing sixty entries
// takes about four seconds); what they could not do is show what a gradient
// looks like before it is chosen, and at sixty built-ins the names stop
// carrying the difference on their own.
//
// Shape: the first page lists Global (per-animation pickers only), then My
// gradients when the library is not empty, then each declared category that
// has built-ins in it, each with a count. Opening one of those lists its
// gradients with a name, a swatch and a marker on the one in force. Choosing
// a gradient returns all the way to the category that opened the picker;
// the exit chevron pops one level and selects nothing. The header arrows and
// a horizontal swipe paginate inside a page and do neither.
//
// This header is the whole contract with CatAnimation.cpp: the picker knows
// nothing about which setting it is editing, and the caller knows nothing
// about how the pages are built.
#include "SettingsModel.h"
#include "SettingsUI.h"

#include <string>

// The built-in gradient table as every settings screen reads it, and what the
// global gradient is called and resolves to right now. All three are defined
// in CatAnimation.cpp, which owns the Animation category's reading of the
// stored gradient fields; the picker shows them and never re-derives them.
// settingsGlobalGradientRef returns "" while a retained pre-library custom
// gradient is what the global draws, which is a state no ref names and no
// picker offers (gm-nov3.7).
const settingsui::ThemeNameProvider &settingsThemeProvider();
std::string settingsGlobalGradientLabel();
std::string settingsGlobalGradientRef();

// What the picker does with a choice, and how it keeps the page that opened
// it honest while it is open. Every function pointer is called on the UI
// task with `user` as its only argument, and `user` must outlive the picker:
// it is the opening category's ctx, which the shell keeps alive underneath.
struct SettingsGradientPickerSpec {
    // Page title for the first page. Must outlive the picker; the three
    // callers pass string literals.
    const char *title = "Gradient";

    // Offer "Global" as the first entry, which selects the ref "". A
    // per-animation row does; the row that sets the global itself does not,
    // because "the global is the global" is not a value it can take.
    bool allowGlobal = false;

    // The ref in force for the slot being edited, in the picker's own
    // grammar: "" for Global, a decimal built-in index, or "c<id>". Read at
    // push and again after a web save, so the marker follows an external
    // change instead of pointing at what was in force when the page opened.
    // The returned pointer is copied at once, so a caller may hand back the
    // c_str() of a string it owns.
    const char *(*currentRef)(void *user) = nullptr;

    // A choice. `ref` is the same grammar, already validated against the
    // stored library under Settings::Guard, so the callee only has to write
    // it where it belongs.
    void (*onPick)(void *user, const char *ref) = nullptr;

    // Bring the opening category's retained draft up to date after a web
    // save. SettingsUI::service() reconciles only the top page, so while the
    // picker is open the Animation category's own reconcile never runs and
    // this is the only thing that keeps its untouched fields current.
    void (*onReconcileParent)(void *user) = nullptr;

    // Is the slot this picker was opened for still a thing worth editing?
    // False closes the picker without selecting: a web save that turned the
    // standby animation off leaves its gradient row disabled, and a picker
    // that stayed open over it would write a slot nothing reads. It is
    // deliberately not "has the animation changed": the picker stays on the
    // slot it captured, the way the Parameters page stays on the animation it
    // was opened for, rather than retargeting under the finger.
    bool (*stillValid)(void *user) = nullptr;

    void *user = nullptr;
};

// Pushes the picker's first page. Called from a row's own click handler, on
// the UI task with the page stack settled, so it may push straight away.
void settingsGradientPickerPush(SettingsUI &ui, const SettingsGradientPickerSpec &spec);

#endif // GM_CAT_GRADIENT_PICKER_H
