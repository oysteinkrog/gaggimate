// Placeholder content for the five real settings categories, so the shell's
// navigation is testable before each category bead lands. Every symbol here
// is __attribute__((weak)); a category bead's own Cat<Name>.cpp defines the
// strong version, which replaces this one at link time without editing this
// file, SettingsUI.cpp or SettingsUI.h. The runner bead deletes this file
// once all five strong definitions exist.
#include "SettingsUI.h"

#include <display/ui/default/eez/eez-flow.h>
#include <display/ui/default/eez/images.h>
#include <display/ui/default/eez/screens.h>

#include <cstdio>

namespace {

int placeholderRowCount(void * /*ctx*/) { return 5; }

void placeholderBuildRow(void * /*ctx*/, int index, lv_obj_t *parent, SettingsUI & /*ui*/) {
    lv_obj_t *label = lv_label_create(parent);
    char buf[16];
    snprintf(buf, sizeof(buf), "Row %d", index + 1);
    lv_label_set_text(label, buf);
    lv_obj_center(label);
    lv_obj_set_style_text_color(label, lv_color_hex(theme_colors[eez_flow_get_selected_theme_index()][0]), LV_PART_MAIN);
}

} // namespace

extern __attribute__((weak)) const SettingsCategoryDef kCatTemps = {
    "Temps", &img_thermometer_half_40x40, placeholderRowCount, placeholderBuildRow, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr,
};

extern __attribute__((weak)) const SettingsCategoryDef kCatDisplay = {
    "Display", &img_clock_40x40, placeholderRowCount, placeholderBuildRow, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr,
};

extern __attribute__((weak)) const SettingsCategoryDef kCatAnimation = {
    "Animation", &img_play_40x40, placeholderRowCount, placeholderBuildRow, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr,
};

extern __attribute__((weak)) const SettingsCategoryDef kCatMachine = {
    "Machine", &img_settings_40x40, placeholderRowCount, placeholderBuildRow, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr,
};

extern __attribute__((weak)) const SettingsCategoryDef kCatStatus = {
    "Status", &img_info_40x40, placeholderRowCount, placeholderBuildRow, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr,
};
