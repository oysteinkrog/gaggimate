#ifndef GM_CAT_SCHEDULES_H
#define GM_CAT_SCHEDULES_H

// The auto wake-up schedule list and editor pages (gm-flw.11), pushed from
// the Machine category's fourth row ("Schedules") through the row-provider
// hook CatMachine.h declares (settingsMachineExtraRowCount/
// settingsMachineBuildExtraRow); CatSchedules.cpp defines the strong
// versions that replace CatMachine.cpp's weak default. Nothing outside
// CatSchedules.cpp needs a declaration from this file: the list and editor
// SettingsCategoryDefs live in that file's own anonymous namespace (unlike
// kCatMachine and its four siblings, they are never placed in the tile
// registry, so the weak/strong linkage trick SettingsUI.h documents does
// not apply to them) and are reached only by pointer, from the row that
// pushes them. This header exists to satisfy the epic's one-header-per-
// category-bead pattern the build's file list expects, not because
// anything needs to include it.

#endif // GM_CAT_SCHEDULES_H
