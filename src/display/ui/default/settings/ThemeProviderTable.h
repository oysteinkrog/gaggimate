#ifndef GM_SETTINGS_THEME_PROVIDER_TABLE_H
#define GM_SETTINGS_THEME_PROVIDER_TABLE_H

// Fills a ThemeNameProvider straight from the generated built-in gradient
// table (BgAnimThemeTable.h, written by scripts/gen_gradients.py from
// data/gradients.json).
//
// Two builds use it. The simulator cannot link BgAnimThemes.cpp, so
// CatAnimation.cpp builds its provider here. The host test
// (test/test_settings_model) uses it to exercise the same path the simulator
// runs: that test links the real bg_theme_* functions, so without this it
// would only ever prove the device path.
//
// A device build must not include this header. It reaches the same data
// through bg_theme_count(), bg_theme_name(), bg_theme_category(),
// bg_theme_category_count(), bg_theme_category_name() and bg_theme_stops(),
// and including the table again would put a second copy of every stop in
// flash.

#include "SettingsModel.h"

#include <display/ui/default/bganim/BgAnimThemeTable.h>

namespace settingsui {

namespace themetable {

// An index outside the table reads as index 0, the rule bg_theme_name and the
// other firmware lookups follow: a stored id from a longer table then gives
// the wrong gradient rather than faulting.
inline int clampTheme(int i) { return (i >= 0 && i < bganim_gen::THEME_DEF_COUNT) ? i : 0; }
inline int clampCategory(int i) { return (i >= 0 && i < bganim_gen::THEME_CATEGORY_COUNT) ? i : 0; }

} // namespace themetable

inline ThemeNameProvider generatedThemeProvider() {
    ThemeNameProvider p;
    p.count = []() { return bganim_gen::THEME_DEF_COUNT; };
    p.name = [](int i) { return bganim_gen::THEME_DEFS[themetable::clampTheme(i)].name; };
    p.category = [](int i) { return bganim_gen::THEME_DEFS[themetable::clampTheme(i)].category; };
    p.categoryCount = []() { return bganim_gen::THEME_CATEGORY_COUNT; };
    p.categoryName = [](int i) { return bganim_gen::THEME_CATEGORIES[themetable::clampCategory(i)]; };
    p.stops = [](int i) -> ThemeStops { return bganim_gen::THEME_DEFS[themetable::clampTheme(i)].stops; };
    return p;
}

} // namespace settingsui

#endif // GM_SETTINGS_THEME_PROVIDER_TABLE_H
