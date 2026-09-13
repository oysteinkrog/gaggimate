#ifndef GM_SETTINGS_THEME_PROVIDER_TABLE_H
#define GM_SETTINGS_THEME_PROVIDER_TABLE_H

// Fills a ThemeNameProvider straight from the generated built-in gradient
// table (BgAnimThemeTable.h, written by scripts/gen_gradients.py from
// data/gradients.json).
//
// The host test (test/test_settings_model) is the only caller. It links the
// real bg_theme_* functions, and building a second provider straight from the
// table is how it checks that the two readings of the same generated data
// agree. The fixture provider below is also how the test exercises a table it
// can reorder, which the generated one cannot be: reordering that would change
// stored ids.
//
// No firmware build includes this header. Both the device and the simulator
// reach the same data through bg_theme_count(), bg_theme_name(),
// bg_theme_category(), bg_theme_category_count(), bg_theme_category_name()
// and bg_theme_stops(), and including the table again would put a second copy
// of every stop in flash.

#include "SettingsModel.h"

#include <display/ui/default/bganim/BgAnimThemeTable.h>

namespace settingsui {

namespace themetable {

// An index outside the table reads as index 0, the rule bg_theme_name and the
// other firmware lookups follow: a stored id from a longer table then gives
// the wrong gradient rather than faulting. An empty table has no index 0, so
// the lookups below read null there, which gradientChoices turns into "".
inline int clampIndex(int i, int n) { return (i >= 0 && i < n) ? i : 0; }

} // namespace themetable

// The provider for any table of the generated shape: the gradients in table
// order (an entry's index is its stored id) and the declared category list in
// its own order, which is data/gradients.json's categories array and not the
// order the gradients happen to first mention a category in. A declared
// category no gradient uses is still reported, because the list is the
// owner's group order and a picker decides for itself what to do with an
// empty group.
//
// generatedThemeProvider() below passes the generated table, which is what
// bg_theme_* reads; the host test also passes a small fixture table of its
// own.
inline ThemeNameProvider tableThemeProvider(const bganim_gen::ThemeDef *defs, int defCount,
                                            const char *const *categories, int categoryCount) {
    ThemeNameProvider p;
    p.count = [defCount]() { return defCount; };
    p.name = [defs, defCount](int i) -> const char * {
        return defCount > 0 ? defs[themetable::clampIndex(i, defCount)].name : nullptr;
    };
    p.category = [defs, defCount](int i) -> const char * {
        return defCount > 0 ? defs[themetable::clampIndex(i, defCount)].category : nullptr;
    };
    p.categoryCount = [categoryCount]() { return categoryCount; };
    p.categoryName = [categories, categoryCount](int i) -> const char * {
        return categoryCount > 0 ? categories[themetable::clampIndex(i, categoryCount)] : nullptr;
    };
    p.stops = [defs, defCount](int i) -> ThemeStops {
        return defCount > 0 ? defs[themetable::clampIndex(i, defCount)].stops : nullptr;
    };
    return p;
}

inline ThemeNameProvider generatedThemeProvider() {
    return tableThemeProvider(bganim_gen::THEME_DEFS, bganim_gen::THEME_DEF_COUNT, bganim_gen::THEME_CATEGORIES,
                              bganim_gen::THEME_CATEGORY_COUNT);
}

} // namespace settingsui

#endif // GM_SETTINGS_THEME_PROVIDER_TABLE_H
