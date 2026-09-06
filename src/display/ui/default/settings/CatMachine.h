#ifndef GM_CAT_MACHINE_H
#define GM_CAT_MACHINE_H

// The Machine category's draft and the row-provider hook the schedule
// editor (gm-flw.11, next wave) codes against. CatMachine.cpp (this bead,
// gm-flw.10) defines the strong kCatMachine and the three deferred rows
// (startup mode, standby timeout, auto wake-up); the schedule editor's own
// file appends a fourth "Schedules" action row through the hook below and
// pushes its own page(s) with a MachineDraft* (this struct) as their ctx.
// Neither file knows the other's internals: this header is the whole
// contract between them.
#include "SettingsModel.h"
#include "SettingsUI.h"

#include <display/core/constants.h>

#include <vector>

struct MachineDraft {
    int startupMode = MODE_STANDBY; // MODE_STANDBY/MODE_BREW (constants.h)
    long standbyTimeoutMs = 0;      // ms, 0 = never (kStandbyTimeoutSpec)
    bool autowakeupEnabled = false;
    std::vector<settingsui::ScheduleDraft> schedules;

    // Set by this file's own rows (startupMode/standbyTimeout/autowakeup);
    // schedulesTouched is set by the schedule editor whenever it changes
    // the list. CatMachine.cpp's commit() writes only the fields a visit
    // actually touched (shared contract: deferred fields, per-field
    // last-writer-wins), converting schedules to AutoWakeupSchedule and
    // calling setAutoWakeupSchedules only when schedulesTouched.
    bool startupModeTouched = false;
    bool standbyTimeoutTouched = false;
    bool autowakeupEnabledTouched = false;
    bool schedulesTouched = false;
};

// Weak in CatMachine.cpp (default: no fourth row), strong in the schedule
// editor's own file once gm-flw.11 lands, the same weak/strong split
// SettingsPlaceholders.cpp uses for kCat<Name> (SettingsUI.h): this wave's
// build links with no schedules file present, and row 4 simply does not
// exist until it lands. extraRowCount(draft) is added to this file's own
// row count (3); buildExtraRow is called only for the resulting index (3,
// never 0..2), and is expected to push the editor's own page(s) from its
// row's onActivate with `draft` as the pushed page's ctx.
int settingsMachineExtraRowCount(MachineDraft *draft);
void settingsMachineBuildExtraRow(MachineDraft *draft, int index, lv_obj_t *parent, SettingsUI &ui);

#endif // GM_CAT_MACHINE_H
