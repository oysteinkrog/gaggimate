// Unit tests: the on-display settings value model (ranges, steps, formats,
// choice lists, zone split, wake-up schedules). Host-side, no LVGL/Arduino/
// ESP-IDF runtime -- pio test -e native_settingsui.
//
// Direct-includes the TUs under test, same pattern as native_autotune
// (test_autotune_simc.cpp #includes Autotune.cpp): SettingsModel.cpp,
// zones.cpp and BgAnimThemes.cpp all have zero Arduino/ESP-IDF dependency,
// so the native linker never needs the rest of src/display (which does).
// BgAnimThemes.cpp is included only here, never by SettingsModel.cpp itself
// (see the provider comment at the top of SettingsModel.h) -- it lets group
// D check the model's gradient-map writer against the real bg_map_valid()
// and read real built-in theme names, without the model depending on it.
// Because it is linked here, group D builds its provider out of the same
// bg_theme_* functions CatAnimation.cpp wires up, on the device and on the
// simulator alike (gm-nov3.3 removed the simulator's separate reading of the
// generated table). A fixture table goes in through bg_test_set_theme_table,
// so a case about category order exercises those accessors rather than a
// second implementation written for the test.
//
// Groups:
//   A -- numeric spec: clamp, wrap, off-grid snap, fast step
//   B -- formatters
//   C -- index-based choice lists (wrap at both ends)
//   D -- gradients: theme providers, choice list, map read/write
//   E -- palette
//   F -- time zones (real 461-entry table)
//   G -- wake-up schedules
//   H -- background animation parameters (the packed bgAnimParams string)
//   I -- the frozen legacy bgAnimTheme namespace and explicit refs
//   J -- the legacy custom gradient's migration
//   K -- fault injection across the persistence steps
//   L -- the gradient pick transaction (validate and assign as one)

// Group I and J below need a built-in table longer than the one this build
// ships, because the whole point of freezing the legacy sentinel is what
// happens when the table grows past 18. Defined before every include so
// BgAnim.h and BgAnimThemes.cpp both see it.
#define GM_BGANIM_TEST_TABLE 1

#include <unity.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "display/core/zones.cpp"
#include "display/ui/default/bganim/BgAnimThemes.cpp"
#include "display/ui/default/settings/SettingsModel.cpp"
// Header-only and free of Arduino, LVGL and controller: group K instantiates
// the production pick transaction against a fake Settings (gm-nov3.17).
#include "display/ui/default/settings/GradientPickTransaction.h"
// The generated table, which group D reads directly to say what the
// bg_theme_* accessors above it should return, and whose ThemeDef is the
// shape of the fixture tables it swaps in.
#include "display/ui/default/bganim/BgAnimThemeTable.h"

using namespace settingsui;

// ---------------------------------------------------------------------------
// Group A -- numeric spec: clamp, wrap, off-grid snap, fast step
// ---------------------------------------------------------------------------

static void test_clamp_both_ends() {
    // Brew/grind delay: 0..4000, step 50. Stepping past either end holds.
    TEST_ASSERT_EQUAL(0, stepValue(0, -1, false, kBrewDelaySpec));
    TEST_ASSERT_EQUAL(4000, stepValue(4000, 1, false, kBrewDelaySpec));
    TEST_ASSERT_EQUAL(0, stepValue(0, -1, false, kGrindDelaySpec));
    TEST_ASSERT_EQUAL(4000, stepValue(4000, 1, false, kGrindDelaySpec));
    // Temperature offset: -20..20.
    TEST_ASSERT_EQUAL(-20, stepValue(-20, -1, false, kTemperatureOffsetSpec));
    TEST_ASSERT_EQUAL(20, stepValue(20, 1, false, kTemperatureOffsetSpec));
    // Pressure (tenths of a bar): 10..300.
    TEST_ASSERT_EQUAL(10, stepValue(10, -1, false, kPressureScalingSpec));
    TEST_ASSERT_EQUAL(300, stepValue(300, 1, false, kPressureScalingSpec));
}

static void test_wrap_hour_and_minute() {
    TEST_ASSERT_EQUAL(0, stepValue(23, 1, false, kScheduleHourSpec));
    TEST_ASSERT_EQUAL(23, stepValue(0, -1, false, kScheduleHourSpec));
    TEST_ASSERT_EQUAL(0, stepValue(59, 1, false, kScheduleMinuteSpec));
    TEST_ASSERT_EQUAL(59, stepValue(0, -1, false, kScheduleMinuteSpec));
}

static void test_fast_step() {
    TEST_ASSERT_EQUAL(5, stepValue(0, 1, true, kTemperatureOffsetSpec));
    TEST_ASSERT_EQUAL(1050, stepValue(800, 1, true, kBrewDelaySpec));
    // Wrapping fields take a fast step too: hour 20 + fast(5) wraps to 1.
    TEST_ASSERT_EQUAL(1, stepValue(20, 1, true, kScheduleHourSpec));
}

static void test_standby_timeout_edge() {
    // "stepping down from 1 min reaches Never; stepping up from Never gives 1 min"
    TEST_ASSERT_EQUAL(0, stepValue(60000, -1, false, kStandbyTimeoutSpec));
    TEST_ASSERT_EQUAL(60000, stepValue(0, 1, false, kStandbyTimeoutSpec));
    TEST_ASSERT_EQUAL(14400000, stepValue(14400000, 1, false, kStandbyTimeoutSpec));
}

static void test_dim_after_off_grid_snap() {
    // 45 s is off the 30 s grid; stepping up lands on 60 s, not 75 s.
    TEST_ASSERT_EQUAL(60000, stepValue(45000, 1, false, kStandbyBrightnessTimeoutSpec));
    // Stepping down from the same off-grid value lands on the grid line below.
    TEST_ASSERT_EQUAL(30000, stepValue(45000, -1, false, kStandbyBrightnessTimeoutSpec));
}

// ---------------------------------------------------------------------------
// Group B -- formatters
// ---------------------------------------------------------------------------

static void test_format_integer_and_unit() {
    TEST_ASSERT_EQUAL_STRING("-3 C", formatNumeric(-3, kTemperatureOffsetSpec).c_str());
    TEST_ASSERT_EQUAL_STRING("800 ms", formatNumeric(800, kBrewDelaySpec).c_str());
    TEST_ASSERT_EQUAL_STRING("30 fps", formatNumeric(30, kBgAnimFpsSpec).c_str());
    TEST_ASSERT_EQUAL_STRING("35 %", formatNumeric(35, kBgAnimPlateOpacitySpec).c_str());
    TEST_ASSERT_EQUAL_STRING("120 ms", formatNumeric(120, kBgFadeSpec).c_str());
}

// Screen fade: 20 ms steps, 100 ms fast, clamped at 0 and 1000, and an
// off-grid web value snaps to the grid on the first press.
static void test_fade_spec() {
    TEST_ASSERT_EQUAL(140, stepValue(120, 1, false, kBgFadeSpec));
    TEST_ASSERT_EQUAL(220, stepValue(120, 1, true, kBgFadeSpec));
    TEST_ASSERT_EQUAL(0, stepValue(0, -1, false, kBgFadeSpec));
    TEST_ASSERT_EQUAL(1000, stepValue(1000, 1, true, kBgFadeSpec));
    TEST_ASSERT_EQUAL(140, stepValue(125, 1, false, kBgFadeSpec));
    TEST_ASSERT_EQUAL(1000, clampOrWrap(5000, kBgFadeSpec));
    TEST_ASSERT_EQUAL_STRING("Linear", kFadeCurveLabels[0]);
    TEST_ASSERT_EQUAL_STRING("Smooth", kFadeCurveLabels[1]);
}

static void test_format_one_decimal() {
    TEST_ASSERT_EQUAL_STRING("16.0 bar", formatNumeric(160, kPressureScalingSpec).c_str());
    TEST_ASSERT_EQUAL_STRING("1.5 bar", formatNumeric(15, kPressureScalingSpec).c_str());
}

static void test_format_minutes_seconds() {
    TEST_ASSERT_EQUAL_STRING("1:30", formatNumeric(90000, kStandbyBrightnessTimeoutSpec).c_str());
    TEST_ASSERT_EQUAL_STRING("0:45", formatNumeric(45000, kStandbyBrightnessTimeoutSpec).c_str());
}

static void test_format_standby_minutes() {
    TEST_ASSERT_EQUAL_STRING("Never", formatNumeric(0, kStandbyTimeoutSpec).c_str());
    TEST_ASSERT_EQUAL_STRING("15 min", formatNumeric(900000, kStandbyTimeoutSpec).c_str());
}

// ---------------------------------------------------------------------------
// Group C -- index-based choice lists (wrap at both ends)
// ---------------------------------------------------------------------------

static void test_wrap_index_choice_lists() {
    // Startup mode, Theme, Plates: wrap at both ends.
    TEST_ASSERT_EQUAL(0, wrapIndex(1, 2, 1));
    TEST_ASSERT_EQUAL(1, wrapIndex(0, 2, -1));
    TEST_ASSERT_EQUAL(0, wrapIndex(2, 3, 1));
    TEST_ASSERT_EQUAL(2, wrapIndex(0, 3, -1));

    TEST_ASSERT_EQUAL(MODE_STANDBY, startupModeValueForIndex(0));
    TEST_ASSERT_EQUAL(MODE_BREW, startupModeValueForIndex(1));
    TEST_ASSERT_EQUAL(0, startupModeIndexForValue(MODE_STANDBY));
    TEST_ASSERT_EQUAL(1, startupModeIndexForValue(MODE_BREW));
}

static void test_wrap_animation_names_stub() {
    // Stub provider: the tests never link the real animation kernel roster.
    static const char *const kNames[3] = {"Plasma", "Lava", "Silk"};
    AnimationNameProvider provider;
    provider.count = []() { return 3; };
    provider.name = [](int i) { return kNames[i]; };

    TEST_ASSERT_EQUAL(3, animationCount(provider));
    TEST_ASSERT_EQUAL_STRING("Plasma", animationLabel(provider, 0));
    TEST_ASSERT_EQUAL_STRING("Silk", animationLabel(provider, 2));
    // wrapIndex + animationLabel is how a row cycles the choice.
    const int wrapped = wrapIndex(2, animationCount(provider), 1);
    TEST_ASSERT_EQUAL(0, wrapped);
    TEST_ASSERT_EQUAL_STRING("Plasma", animationLabel(provider, wrapped));
}

// ---------------------------------------------------------------------------
// Group D -- gradients: theme providers, choice list, map read/write
// ---------------------------------------------------------------------------

static void test_gradient_choices_real_themes() {
    ThemeNameProvider themes;
    themes.count = []() { return bg_theme_count(); };
    themes.name = [](int i) { return bg_theme_name(i); };

    const std::string library = "3|Custom Sky|ff0000,0000ff";
    const std::vector<GradientChoice> choices = gradientChoices(themes, library);
    // Default, every built-in theme, one library entry.
    TEST_ASSERT_EQUAL(static_cast<size_t>(1 + bg_theme_count() + 1), choices.size());
    TEST_ASSERT_EQUAL_STRING("Default", choices[0].label.c_str());
    TEST_ASSERT_EQUAL_STRING("", choices[0].ref.c_str());
    TEST_ASSERT_EQUAL_STRING(bg_theme_name(0), choices[1].label.c_str());
    TEST_ASSERT_EQUAL_STRING("0", choices[1].ref.c_str());
    TEST_ASSERT_EQUAL_STRING("Custom Sky", choices.back().label.c_str());
    TEST_ASSERT_EQUAL_STRING("c3", choices.back().ref.c_str());

    // Wraps like any other choice list.
    const int lastIndex = static_cast<int>(choices.size()) - 1;
    TEST_ASSERT_EQUAL(0, wrapIndex(lastIndex, static_cast<int>(choices.size()), 1));

    TEST_ASSERT_EQUAL(lastIndex, gradientChoiceIndexForRef(choices, "c3"));
    // A ref naming a deleted library entry falls back to Default (index 0).
    TEST_ASSERT_EQUAL(0, gradientChoiceIndexForRef(choices, "c99"));
}

// The tables the bg_theme_* accessors read, back as shipped. tearDown() calls
// it after every case, so a fixture table cannot leak into the next one even
// when an assertion aborts the case before its own restore.
static void useShippedTable() { bg_test_set_theme_table(nullptr, 0); }

// The production provider, function for function with the six lines of
// CatAnimation.cpp: both builds read the built-in gradients through these
// accessors (gm-nov3.3), so there is no second production reading to check
// against. Built here rather than taken from CatAnimation.cpp, which this
// test cannot link (LVGL).
static ThemeNameProvider firmwareThemeProvider() {
    ThemeNameProvider p;
    p.count = bg_theme_count;
    p.name = bg_theme_name;
    p.category = bg_theme_category;
    p.categoryCount = bg_theme_category_count;
    p.categoryName = bg_theme_category_name;
    p.stops = bg_theme_stops;
    return p;
}

static void check_provider_against_table(const char *who, const ThemeNameProvider &p) {
    char msg[128];
    TEST_ASSERT_EQUAL(bganim_gen::THEME_DEF_COUNT, p.count());
    for (int i = 0; i < bganim_gen::THEME_DEF_COUNT; i++) {
        const bganim_gen::ThemeDef &def = bganim_gen::THEME_DEFS[i];
        snprintf(msg, sizeof(msg), "%s: name %d", who, i);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(def.name, p.name(i), msg);
        snprintf(msg, sizeof(msg), "%s: category %d (%s)", who, i, def.name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(def.category, p.category(i), msg);
        // Six stops, three channels each, byte for byte.
        const uint8_t(*stops)[3] = p.stops(i);
        TEST_ASSERT_NOT_NULL(stops);
        for (int s = 0; s < 6; s++) {
            for (int ch = 0; ch < 3; ch++) {
                snprintf(msg, sizeof(msg), "%s: %s stop %d ch %d", who, def.name, s, ch);
                TEST_ASSERT_EQUAL_UINT8_MESSAGE(def.stops[s][ch], stops[s][ch], msg);
            }
        }
    }
    // The declared list, in its declared order.
    TEST_ASSERT_EQUAL(bganim_gen::THEME_CATEGORY_COUNT, p.categoryCount());
    for (int c = 0; c < bganim_gen::THEME_CATEGORY_COUNT; c++) {
        snprintf(msg, sizeof(msg), "%s: category name %d", who, c);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(bganim_gen::THEME_CATEGORIES[c], p.categoryName(c), msg);
    }
    // Out of range reads as index 0 on both paths, so a stored id from a
    // longer table gives a wrong gradient and never a fault.
    TEST_ASSERT_EQUAL_STRING(p.name(0), p.name(-1));
    TEST_ASSERT_EQUAL_STRING(p.name(0), p.name(bganim_gen::THEME_DEF_COUNT));
    TEST_ASSERT_EQUAL_STRING(p.category(0), p.category(-1));
    TEST_ASSERT_EQUAL_STRING(p.category(0), p.category(bganim_gen::THEME_DEF_COUNT));
    TEST_ASSERT_EQUAL_PTR(p.stops(0), p.stops(-1));
    TEST_ASSERT_EQUAL_PTR(p.stops(0), p.stops(bganim_gen::THEME_DEF_COUNT));
    TEST_ASSERT_EQUAL_STRING(p.categoryName(0), p.categoryName(-1));
    TEST_ASSERT_EQUAL_STRING(p.categoryName(0), p.categoryName(bganim_gen::THEME_CATEGORY_COUNT));
}

static void test_theme_provider_device_path_matches_table() {
    // Every built-in's name, category and six stops, read the way the device
    // reads them, against the generated data.
    check_provider_against_table("firmware", firmwareThemeProvider());
    // Every declared category is used by at least one built-in, so a picker
    // never shows an empty group. The generator does not enforce this; the
    // owner's list is meant to describe the gradients that exist.
    for (int c = 0; c < bganim_gen::THEME_CATEGORY_COUNT; c++) {
        bool used = false;
        for (int i = 0; i < bganim_gen::THEME_DEF_COUNT && !used; i++) {
            used = strcmp(bganim_gen::THEME_DEFS[i].category, bganim_gen::THEME_CATEGORIES[c]) == 0;
        }
        TEST_ASSERT_TRUE_MESSAGE(used, bganim_gen::THEME_CATEGORIES[c]);
    }
    // And every built-in's category is declared, so none of them falls out of
    // a picker that walks the declared list.
    for (int i = 0; i < bganim_gen::THEME_DEF_COUNT; i++) {
        bool declared = false;
        for (int c = 0; c < bganim_gen::THEME_CATEGORY_COUNT && !declared; c++) {
            declared = strcmp(bganim_gen::THEME_DEFS[i].category, bganim_gen::THEME_CATEGORIES[c]) == 0;
        }
        TEST_ASSERT_TRUE_MESSAGE(declared, bganim_gen::THEME_DEFS[i].name);
    }
}

static void test_gradient_choices_carry_categories() {
    const std::string library = "3|Custom Sky|ff0000,0000ff;4|Second|00ff00,ffffff";
    const std::vector<GradientChoice> choices = gradientChoices(firmwareThemeProvider(), library);
    TEST_ASSERT_EQUAL(static_cast<size_t>(1 + bganim_gen::THEME_DEF_COUNT + 2), choices.size());

    // The default entry: no ref, and no category, because it is not a
    // gradient. A picker gives it a group of its own.
    TEST_ASSERT_EQUAL_STRING("Default", choices[0].label.c_str());
    TEST_ASSERT_EQUAL_STRING("", choices[0].ref.c_str());
    TEST_ASSERT_EQUAL_STRING("", choices[0].category.c_str());

    // Built-ins keep their table order, their decimal ref and their category.
    for (int i = 0; i < bganim_gen::THEME_DEF_COUNT; i++) {
        const GradientChoice &c = choices[1 + i];
        TEST_ASSERT_EQUAL_STRING(bganim_gen::THEME_DEFS[i].name, c.label.c_str());
        TEST_ASSERT_EQUAL_STRING(std::to_string(i).c_str(), c.ref.c_str());
        TEST_ASSERT_EQUAL_STRING(bganim_gen::THEME_DEFS[i].category, c.category.c_str());
    }

    // Saved gradients keep "c<id>" and carry no category: the picker groups
    // them under My gradients, not under one of the declared eight.
    const GradientChoice &first = choices[1 + bganim_gen::THEME_DEF_COUNT];
    TEST_ASSERT_EQUAL_STRING("Custom Sky", first.label.c_str());
    TEST_ASSERT_EQUAL_STRING("c3", first.ref.c_str());
    TEST_ASSERT_EQUAL_STRING("", first.category.c_str());
    TEST_ASSERT_EQUAL_STRING("Second", choices.back().label.c_str());
    TEST_ASSERT_EQUAL_STRING("c4", choices.back().ref.c_str());
    TEST_ASSERT_EQUAL_STRING("", choices.back().category.c_str());
}

static void test_gradient_choices_without_category_accessor() {
    // The two-field provider a caller that does not group still works, and
    // every category reads empty rather than faulting on an unset accessor.
    ThemeNameProvider themes;
    themes.count = []() { return 2; };
    themes.name = [](int i) { return i == 0 ? "One" : "Two"; };
    const std::vector<GradientChoice> choices = gradientChoices(themes, "");
    TEST_ASSERT_EQUAL(static_cast<size_t>(3), choices.size());
    TEST_ASSERT_EQUAL_STRING("", choices[1].category.c_str());
    TEST_ASSERT_EQUAL_STRING("", choices[2].category.c_str());
}

static void test_theme_category_order_is_declared_not_encountered() {
    // A fixture table whose first gradient sits in the second declared
    // category, and whose third declared category has no gradient at all,
    // swapped under the bg_theme_* accessors themselves so this case reads
    // the production provider both builds use. The shipped table cannot be
    // reordered for a test, because an entry's index is its stored id, and it
    // has no empty category, so a fixture is the only way to see what those
    // accessors do with a declared order the gradients disagree with.
    //
    // The declared list is the picker's group order, so it must come back in
    // its declared order, empty category included, whatever the gradients
    // mention first.
    static const bganim_gen::ThemeDef kDefs[3] = {
        {"Cyber",
         "Neon",
         {{0x05, 0x00, 0x08}, {0x24, 0x04, 0x48}, {0x50, 0x10, 0x90}, {0x90, 0x18, 0xd8}, {0xe0, 0x30, 0xf8},
          {0xff, 0x9c, 0xf0}}},
        {"Espresso",
         "Coffee",
         {{0x08, 0x04, 0x02}, {0x2a, 0x12, 0x06}, {0x6b, 0x34, 0x13}, {0xb8, 0x70, 0x3a}, {0xe8, 0xb2, 0x68},
          {0xf8, 0xe6, 0xc8}}},
        {"Glow",
         "Neon",
         {{0x00, 0x00, 0x00}, {0x20, 0x20, 0x20}, {0x40, 0x40, 0x40}, {0x80, 0x80, 0x80}, {0xc0, 0xc0, 0xc0},
          {0xff, 0xff, 0xff}}},
    };
    static const char *const kCategories[3] = {"Coffee", "Neon", "Pastel"};

    // The fixture is only difficult while these two things hold, so check
    // them rather than trusting the table above to stay that way.
    TEST_ASSERT_EQUAL_STRING_MESSAGE("Neon", kDefs[0].category, "first gradient must not be in the first category");
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_TRUE_MESSAGE(strcmp(kDefs[i].category, "Pastel") != 0, "Pastel must stay empty");
    }

    bg_test_set_theme_table(kDefs, 3, kCategories, 3);
    const ThemeNameProvider themes = firmwareThemeProvider();

    // Declared order, straight off the provider. Encounter order would have
    // put Neon first and dropped Pastel entirely.
    TEST_ASSERT_EQUAL(3, themes.categoryCount());
    TEST_ASSERT_EQUAL_STRING("Coffee", themes.categoryName(0));
    TEST_ASSERT_EQUAL_STRING("Neon", themes.categoryName(1));
    TEST_ASSERT_EQUAL_STRING("Pastel", themes.categoryName(2));
    // Out of range reads as index 0, the rule the rest of the table lookups
    // follow.
    TEST_ASSERT_EQUAL_STRING("Coffee", themes.categoryName(-1));
    TEST_ASSERT_EQUAL_STRING("Coffee", themes.categoryName(3));

    // The gradients keep table order, which is stored-id order, and each one
    // still reports its own category.
    TEST_ASSERT_EQUAL(3, themes.count());
    TEST_ASSERT_EQUAL_STRING("Cyber", themes.name(0));
    TEST_ASSERT_EQUAL_STRING("Espresso", themes.name(1));
    TEST_ASSERT_EQUAL_STRING("Glow", themes.name(2));
    TEST_ASSERT_EQUAL_STRING("Neon", themes.category(0));
    TEST_ASSERT_EQUAL_STRING("Coffee", themes.category(1));
    TEST_ASSERT_EQUAL_STRING("Neon", themes.category(2));
    // The swatch path reads the same fixture rows.
    TEST_ASSERT_EQUAL_UINT8_ARRAY(reinterpret_cast<const uint8_t *>(kDefs[2].stops),
                                  reinterpret_cast<const uint8_t *>(themes.stops(2)), 6 * 3);

    // And the choice list a picker lands on: index order, decimal refs and
    // per-gradient categories, with Default carrying neither.
    const std::vector<GradientChoice> choices = gradientChoices(themes, "");
    TEST_ASSERT_EQUAL(static_cast<size_t>(4), choices.size());
    TEST_ASSERT_EQUAL_STRING("Default", choices[0].label.c_str());
    TEST_ASSERT_EQUAL_STRING("", choices[0].ref.c_str());
    TEST_ASSERT_EQUAL_STRING("", choices[0].category.c_str());
    TEST_ASSERT_EQUAL_STRING("Cyber", choices[1].label.c_str());
    TEST_ASSERT_EQUAL_STRING("0", choices[1].ref.c_str());
    TEST_ASSERT_EQUAL_STRING("Neon", choices[1].category.c_str());
    TEST_ASSERT_EQUAL_STRING("Espresso", choices[2].label.c_str());
    TEST_ASSERT_EQUAL_STRING("1", choices[2].ref.c_str());
    TEST_ASSERT_EQUAL_STRING("Coffee", choices[2].category.c_str());
    TEST_ASSERT_EQUAL_STRING("Glow", choices[3].label.c_str());
    TEST_ASSERT_EQUAL_STRING("2", choices[3].ref.c_str());
    TEST_ASSERT_EQUAL_STRING("Neon", choices[3].category.c_str());

    // Grouping those choices the way a picker does, by walking the declared
    // list in order: Coffee first with Espresso in it, then Neon with two,
    // then Pastel with none. Every gradient lands in exactly one group.
    int grouped = 0;
    const int expectedPerCategory[3] = {1, 2, 0};
    for (int c = 0; c < themes.categoryCount(); c++) {
        int inThisCategory = 0;
        for (size_t i = 1; i < choices.size(); i++) {
            if (choices[i].category == themes.categoryName(c)) {
                inThisCategory++;
                grouped++;
            }
        }
        TEST_ASSERT_EQUAL_INT_MESSAGE(expectedPerCategory[c], inThisCategory, themes.categoryName(c));
    }
    TEST_ASSERT_EQUAL(3, grouped);
}

static void test_gradient_groups_follow_declared_order_and_drop_empties() {
    // The picker's first page is built from these two calls
    // (CatGradientPicker.cpp): the built-in groups in declared order, and
    // the saved gradients as one group of their own.
    static const bganim_gen::ThemeDef kDefs[3] = {
        {"Cyber",
         "Neon",
         {{0x05, 0x00, 0x08}, {0x24, 0x04, 0x48}, {0x50, 0x10, 0x90}, {0x90, 0x18, 0xd8}, {0xe0, 0x30, 0xf8},
          {0xff, 0x9c, 0xf0}}},
        {"Espresso",
         "Coffee",
         {{0x08, 0x04, 0x02}, {0x2a, 0x12, 0x06}, {0x6b, 0x34, 0x13}, {0xb8, 0x70, 0x3a}, {0xe8, 0xb2, 0x68},
          {0xf8, 0xe6, 0xc8}}},
        {"Glow",
         "Neon",
         {{0x00, 0x00, 0x00}, {0x20, 0x20, 0x20}, {0x40, 0x40, 0x40}, {0x80, 0x80, 0x80}, {0xc0, 0xc0, 0xc0},
          {0xff, 0xff, 0xff}}},
    };
    static const char *const kCategories[3] = {"Coffee", "Neon", "Pastel"};
    bg_test_set_theme_table(kDefs, 3, kCategories, 3);
    const ThemeNameProvider themes = firmwareThemeProvider();
    const std::string library = "3|Custom Sky|ff0000,0000ff;4|Second|00ff00,ffffff";
    const std::vector<GradientChoice> choices = gradientChoices(themes, library);

    const std::vector<GradientGroup> groups = gradientBuiltinGroups(themes, choices);
    // Pastel has no gradient in it, so it is not a group: a page of nothing
    // is worse than one row fewer.
    TEST_ASSERT_EQUAL(static_cast<size_t>(2), groups.size());
    TEST_ASSERT_EQUAL_STRING("Coffee", groups[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("Neon", groups[1].name.c_str());
    TEST_ASSERT_EQUAL(static_cast<size_t>(1), groups[0].choices.size());
    TEST_ASSERT_EQUAL(static_cast<size_t>(2), groups[1].choices.size());
    // The entries are indices into `choices`, in table order within a group.
    TEST_ASSERT_EQUAL_STRING("Espresso", choices[groups[0].choices[0]].label.c_str());
    TEST_ASSERT_EQUAL_STRING("Cyber", choices[groups[1].choices[0]].label.c_str());
    TEST_ASSERT_EQUAL_STRING("Glow", choices[groups[1].choices[1]].label.c_str());

    // Saved gradients are their own group and never land in a built-in one.
    const std::vector<int> saved = gradientLibraryChoices(themes, choices);
    TEST_ASSERT_EQUAL(static_cast<size_t>(2), saved.size());
    TEST_ASSERT_EQUAL_STRING("c3", choices[saved[0]].ref.c_str());
    TEST_ASSERT_EQUAL_STRING("c4", choices[saved[1]].ref.c_str());

    // Default (choices[0]) belongs to neither: the picker offers it as
    // Global, above the groups, and only for a per-animation row.
    for (const GradientGroup &g : groups) {
        for (int index : g.choices) {
            TEST_ASSERT_TRUE(index != 0);
        }
    }
    for (int index : saved) {
        TEST_ASSERT_TRUE(index != 0);
    }
}

static void test_gradient_groups_keep_every_builtin_reachable() {
    // A gradient whose category is not one of the declared ones would
    // otherwise be in no group at all, and so unreachable from the picker.
    // It goes in a trailing "Other" group instead.
    static const bganim_gen::ThemeDef kDefs[2] = {
        {"Espresso",
         "Coffee",
         {{0x08, 0x04, 0x02}, {0x2a, 0x12, 0x06}, {0x6b, 0x34, 0x13}, {0xb8, 0x70, 0x3a}, {0xe8, 0xb2, 0x68},
          {0xf8, 0xe6, 0xc8}}},
        {"Stray",
         "Nowhere",
         {{0x00, 0x00, 0x00}, {0x20, 0x20, 0x20}, {0x40, 0x40, 0x40}, {0x80, 0x80, 0x80}, {0xc0, 0xc0, 0xc0},
          {0xff, 0xff, 0xff}}},
    };
    static const char *const kCategories[1] = {"Coffee"};
    bg_test_set_theme_table(kDefs, 2, kCategories, 1);
    const ThemeNameProvider themes = firmwareThemeProvider();
    const std::vector<GradientChoice> choices = gradientChoices(themes, "");
    const std::vector<GradientGroup> groups = gradientBuiltinGroups(themes, choices);

    TEST_ASSERT_EQUAL(static_cast<size_t>(2), groups.size());
    TEST_ASSERT_EQUAL_STRING("Coffee", groups[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("Other", groups[1].name.c_str());
    TEST_ASSERT_EQUAL_STRING("Stray", choices[groups[1].choices[0]].label.c_str());

    // Every built-in is in exactly one group, which is what "reachable"
    // means for the picker.
    int seen = 0;
    for (const GradientGroup &g : groups) {
        seen += static_cast<int>(g.choices.size());
    }
    TEST_ASSERT_EQUAL(themes.count(), seen);
}

static void test_gradient_map_read_write_empty() {
    TEST_ASSERT_EQUAL_STRING("", gradientMapReadRef("", 0).c_str());
    const std::string written = gradientMapWriteRef("", 0, "c3");
    TEST_ASSERT_EQUAL_STRING("c3", written.c_str());
    TEST_ASSERT_TRUE(bg_map_valid(written.c_str()));
}

static void test_gradient_map_read_write_shorter_than_n() {
    // Only one entry exists; reading slot 2 (out of range) reads as "".
    TEST_ASSERT_EQUAL_STRING("", gradientMapReadRef("1", 2).c_str());
    const std::string written = gradientMapWriteRef("1", 2, "c5");
    TEST_ASSERT_EQUAL_STRING("1;;c5", written.c_str());
    TEST_ASSERT_TRUE(bg_map_valid(written.c_str()));
    // Slot 0 is untouched.
    TEST_ASSERT_EQUAL_STRING("1", gradientMapReadRef(written, 0).c_str());
}

// The id boundary, on the firmware side of the mirror. parseId reads at most
// five decimal digits, so BG_GRADIENT_ID_MAX is not a policy number, it is the
// largest id the grammar can express: 100000 is not a big id here, it is a
// malformed one, and so is anything with a character left over after the
// digits. The web mirror is held to these same answers by
// tools/gradient_mirror_check.mjs, because the settings handler drops a field
// its parser rejects and keeps the previous stored value, with no error on
// either side (gm-nov3.21).
static void test_gradient_id_boundary_ref_and_library() {
    TEST_ASSERT_EQUAL_INT(99999, BG_GRADIENT_ID_MAX);

    TEST_ASSERT_TRUE(bg_ref_valid(""));
    TEST_ASSERT_TRUE(bg_ref_valid("99999"));
    TEST_ASSERT_TRUE(bg_ref_valid("c99999"));
    TEST_ASSERT_FALSE(bg_ref_valid("100000"));
    TEST_ASSERT_FALSE(bg_ref_valid("c100000"));
    // A library id of zero is no id at all, and neither is a bare 'c'.
    TEST_ASSERT_FALSE(bg_ref_valid("c0"));
    TEST_ASSERT_FALSE(bg_ref_valid("c"));
    // Trailing characters fail the ref rather than being ignored.
    TEST_ASSERT_FALSE(bg_ref_valid("5x"));
    TEST_ASSERT_FALSE(bg_ref_valid("c5x"));
    TEST_ASSERT_FALSE(bg_ref_valid("c 5"));
    // Six digits fail even when the value would fit: the parser stops at five
    // and the sixth is then a trailing character.
    TEST_ASSERT_FALSE(bg_ref_valid("099999"));

    TEST_ASSERT_TRUE(bg_map_valid("c99999"));
    TEST_ASSERT_TRUE(bg_map_valid(";;c99999;"));
    TEST_ASSERT_FALSE(bg_map_valid("c100000"));
    TEST_ASSERT_FALSE(bg_map_valid("c0"));
    TEST_ASSERT_FALSE(bg_map_valid("5x"));

    TEST_ASSERT_TRUE(bg_library_valid(""));
    TEST_ASSERT_TRUE(bg_library_valid("99999|A|010203,040506"));
    TEST_ASSERT_FALSE(bg_library_valid("100000|A|010203,040506"));
    TEST_ASSERT_FALSE(bg_library_valid("0|A|010203,040506"));
    TEST_ASSERT_FALSE(bg_library_valid("99999x|A|010203,040506"));
    TEST_ASSERT_FALSE(bg_library_valid("1||010203,040506"));
    // One malformed entry fails the whole library, so the page cannot show a
    // gradient that sits after it.
    TEST_ASSERT_FALSE(bg_library_valid("1|A|010203,040506;bad;2|B|010203,040506"));

    // An id at the ceiling is a working entry, not just an accepted string.
    uint8_t stops[BG_THEME_MAX_STOPS][3];
    uint8_t pos[BG_THEME_MAX_STOPS];
    int nStops = 0;
    bool uniform = true;
    TEST_ASSERT_TRUE(bg_library_lookup("99999|Top|010203,040506", 99999, stops, pos, nStops, uniform));
    TEST_ASSERT_EQUAL_INT(2, nStops);
}

static void test_gradient_map_write_trims_trailing_empty_slots() {
    // Clearing the only set ref returns the map to "", the web UI's own
    // representation of an all-default map, instead of ";" or ";;".
    TEST_ASSERT_EQUAL_STRING("1", gradientMapWriteRef("1;c3", 1, "").c_str());
    TEST_ASSERT_EQUAL_STRING("", gradientMapWriteRef(";", 1, "").c_str());
    TEST_ASSERT_EQUAL_STRING("", gradientMapWriteRef("", 3, "").c_str());
    // Interior empty slots stay, since later slots depend on their position.
    TEST_ASSERT_EQUAL_STRING("1;;c5", gradientMapWriteRef("1;c7;c5", 1, "").c_str());
}

static void test_gradient_map_write_preserves_neighbours() {
    // Entries before and after N must survive a write to N.
    const std::string map = "2;c7;;5";
    const std::string written = gradientMapWriteRef(map, 2, "c9");
    TEST_ASSERT_EQUAL_STRING("2;c7;c9;5", written.c_str());
    TEST_ASSERT_TRUE(bg_map_valid(written.c_str()));
    TEST_ASSERT_EQUAL_STRING("2", gradientMapReadRef(written, 0).c_str());
    TEST_ASSERT_EQUAL_STRING("c7", gradientMapReadRef(written, 1).c_str());
    TEST_ASSERT_EQUAL_STRING("5", gradientMapReadRef(written, 3).c_str());
}

// ---------------------------------------------------------------------------
// Group E -- palette
// ---------------------------------------------------------------------------

static void test_palette_on_palette_lookup() {
    TEST_ASSERT_EQUAL(0, paletteIndexOf(0xFFFFFF)); // White
    TEST_ASSERT_EQUAL(11, paletteIndexOf(0x000000)); // Black
    TEST_ASSERT_EQUAL(0, paletteCurrentIndex(0xFFFFFF));
    TEST_ASSERT_EQUAL(kPaletteCount, paletteChoiceCount(0xFFFFFF));
    TEST_ASSERT_EQUAL_STRING("White", paletteChoiceLabel(0, 0xFFFFFF).c_str());
}

static void test_palette_off_palette_lookup() {
    const int stray = 0x123456;
    TEST_ASSERT_EQUAL(-1, paletteIndexOf(stray));
    TEST_ASSERT_EQUAL(kPaletteCount, paletteCurrentIndex(stray));
    TEST_ASSERT_EQUAL(kPaletteCount + 1, paletteChoiceCount(stray));
    TEST_ASSERT_EQUAL_STRING("#123456", paletteChoiceLabel(kPaletteCount, stray).c_str());
    TEST_ASSERT_EQUAL(stray, paletteChoiceColor(kPaletteCount, stray));
    // Sitting at rest on the off-palette slot never silently changes it.
    TEST_ASSERT_EQUAL(stray, paletteChoiceColor(paletteCurrentIndex(stray), stray));
}

// ---------------------------------------------------------------------------
// Group F -- time zones (real 461-entry table)
// ---------------------------------------------------------------------------

static ZoneProvider realZoneProvider() {
    ZoneProvider zones;
    zones.count = []() -> size_t { return zones_count(); };
    zones.name = [](size_t i) -> const char * { return zones_entry(i).name; };
    return zones;
}

static void test_zone_region_counts_and_etc_last() {
    const ZoneProvider zones = realZoneProvider();
    const int regions = regionCount(zones);
    TEST_ASSERT_EQUAL(11, regions);
    TEST_ASSERT_EQUAL_STRING("Etc", regionName(zones, regions - 1).c_str());
    TEST_ASSERT_EQUAL(35, cityCount(zones, regions - 1));
    TEST_ASSERT_EQUAL_STRING("Africa", regionName(zones, 0).c_str());
    TEST_ASSERT_EQUAL(52, cityCount(zones, 0));
}

static void test_zone_city_label_strips_region_and_underscores() {
    const ZoneProvider zones = realZoneProvider();
    int region = -1;
    int city = -1;
    TEST_ASSERT_TRUE(locate(zones, "America/Argentina/Buenos_Aires", region, city));
    TEST_ASSERT_EQUAL_STRING("America", regionName(zones, region).c_str());
    TEST_ASSERT_EQUAL_STRING("Argentina/Buenos Aires", cityLabel(zones, region, city).c_str());
    TEST_ASSERT_EQUAL_STRING("America/Argentina/Buenos_Aires", zoneName(zones, region, city).c_str());
}

static void test_zone_locate_unknown_falls_back() {
    const ZoneProvider zones = realZoneProvider();
    int region = -1;
    int city = -1;
    const bool found = locate(zones, "Moon/Base_Alpha", region, city);
    TEST_ASSERT_FALSE(found);
    TEST_ASSERT_EQUAL(0, region);
    TEST_ASSERT_EQUAL(0, city);
}

static void test_zone_round_trip_all_461() {
    const ZoneProvider zones = realZoneProvider();
    const size_t n = zones_count();
    TEST_ASSERT_EQUAL(461, static_cast<int>(n));
    for (size_t i = 0; i < n; i++) {
        const std::string original = zones_entry(i).name;
        int region = -1;
        int city = -1;
        const bool found = locate(zones, original, region, city);
        TEST_ASSERT_TRUE_MESSAGE(found, original.c_str());
        const std::string roundTripped = zoneName(zones, region, city);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(original.c_str(), roundTripped.c_str(), original.c_str());
    }
}

// ---------------------------------------------------------------------------
// Group G -- wake-up schedules
// ---------------------------------------------------------------------------

static void test_schedule_add_remove_floor_and_can_add() {
    std::vector<ScheduleDraft> schedules;
    schedules.push_back(scheduleDefault());
    TEST_ASSERT_FALSE(scheduleRemove(schedules, 0)); // floor of one
    TEST_ASSERT_EQUAL(1u, schedules.size());

    for (int i = 0; i < 7; i++) {
        TEST_ASSERT_TRUE(scheduleCanAdd(schedules));
        TEST_ASSERT_TRUE(scheduleAdd(schedules));
    }
    TEST_ASSERT_EQUAL(8u, schedules.size());
    TEST_ASSERT_FALSE(scheduleCanAdd(schedules)); // false at 8 or more
    TEST_ASSERT_FALSE(scheduleAdd(schedules));
    TEST_ASSERT_EQUAL(8u, schedules.size());

    TEST_ASSERT_TRUE(scheduleRemove(schedules, 0));
    TEST_ASSERT_EQUAL(7u, schedules.size());
}

static void test_schedule_import_9_entries_kept_whole() {
    std::string packed;
    for (int i = 0; i < 9; i++) {
        if (i != 0) packed += ';';
        packed += "07:00|1111100";
    }
    const std::vector<ScheduleDraft> schedules = scheduleParse(packed);
    TEST_ASSERT_EQUAL(9u, schedules.size());
    TEST_ASSERT_FALSE(scheduleCanAdd(schedules)); // over the add floor, but kept whole
}

static void test_schedule_toggle_day_and_step_time() {
    ScheduleDraft s = scheduleDefault();
    TEST_ASSERT_TRUE(s.days[0]);
    scheduleToggleDay(s, 0);
    TEST_ASSERT_FALSE(s.days[0]);
    scheduleToggleDay(s, 0);
    TEST_ASSERT_TRUE(s.days[0]);

    TEST_ASSERT_EQUAL_STRING("07:00", s.time.c_str());
    scheduleStepHour(s, 1, false);
    TEST_ASSERT_EQUAL_STRING("08:00", s.time.c_str());
    // Minute wraps independently of hour: 0 - 1 wraps to 59, hour untouched.
    scheduleStepMinute(s, -1, false);
    TEST_ASSERT_EQUAL_STRING("08:59", s.time.c_str());
}

// A stored entry's time can be empty or otherwise malformed. The web handler
// used to store any time string the browser sent; since 2026-09-09 its
// isScheduleTime drops an entry whose time is not HH:MM in range, so no live
// writer makes one any more. The old ones survive, because the NVS codec keeps
// any entry containing a "|", so a device that stored one before that change
// still carries it. The editor rows read it through scheduleTimeParts, which
// must answer 00:00 for those rather than let the caller slice the string
// (substr past the end throws, and the firmware is built without exceptions).
static void test_schedule_time_parts_reads_malformed_as_midnight() {
    int hour = -1;
    int minute = -1;

    ScheduleDraft ok = scheduleDefault();
    ok.time = "23:45";
    scheduleTimeParts(ok, hour, minute);
    TEST_ASSERT_EQUAL(23, hour);
    TEST_ASSERT_EQUAL(45, minute);

    ScheduleDraft empty = scheduleDefault();
    empty.time = "";
    scheduleTimeParts(empty, hour, minute);
    TEST_ASSERT_EQUAL(0, hour);
    TEST_ASSERT_EQUAL(0, minute);

    ScheduleDraft truncated = scheduleDefault();
    truncated.time = "07:";
    scheduleTimeParts(truncated, hour, minute);
    TEST_ASSERT_EQUAL(0, hour);
    TEST_ASSERT_EQUAL(0, minute);

    ScheduleDraft garbage = scheduleDefault();
    garbage.time = "ab:cd";
    scheduleTimeParts(garbage, hour, minute);
    TEST_ASSERT_EQUAL(0, hour);
    TEST_ASSERT_EQUAL(0, minute);
}

static void test_schedule_summary_strings() {
    ScheduleDraft everyDay = scheduleDefault();
    TEST_ASSERT_EQUAL_STRING("Every day", scheduleDaysSummary(everyDay).c_str());

    ScheduleDraft never = scheduleDefault();
    for (int i = 0; i < 7; i++) scheduleToggleDay(never, i);
    TEST_ASSERT_EQUAL_STRING("Never", scheduleDaysSummary(never).c_str());

    ScheduleDraft weekdays = scheduleDefault();
    scheduleToggleDay(weekdays, 5); // Sat off
    scheduleToggleDay(weekdays, 6); // Sun off
    TEST_ASSERT_EQUAL_STRING("Weekdays", scheduleDaysSummary(weekdays).c_str());

    ScheduleDraft weekends = scheduleDefault();
    for (int i = 0; i < 5; i++) scheduleToggleDay(weekends, i); // Mon..Fri off
    TEST_ASSERT_EQUAL_STRING("Weekends", scheduleDaysSummary(weekends).c_str());

    ScheduleDraft custom = scheduleDefault();
    for (int i = 0; i < 7; i++) scheduleToggleDay(custom, i); // all off
    scheduleToggleDay(custom, 0); // Mon
    scheduleToggleDay(custom, 1); // Tue
    scheduleToggleDay(custom, 4); // Fri
    TEST_ASSERT_EQUAL_STRING("Mon Tue Fri", scheduleDaysSummary(custom).c_str());
}

static void test_schedule_serialize_parse_round_trip_and_malformed_dropped() {
    std::vector<ScheduleDraft> schedules;
    ScheduleDraft a = scheduleDefault();
    ScheduleDraft b = scheduleDefault();
    b.time = "18:30";
    for (int i = 0; i < 7; i++) b.days[i] = (i == 5 || i == 6);
    schedules.push_back(a);
    schedules.push_back(b);

    const std::string packed = scheduleSerialize(schedules);
    TEST_ASSERT_EQUAL_STRING("07:00|1111111;18:30|0000011", packed.c_str());

    const std::vector<ScheduleDraft> parsed = scheduleParse(packed);
    TEST_ASSERT_EQUAL(2u, parsed.size());
    TEST_ASSERT_EQUAL_STRING(packed.c_str(), scheduleSerialize(parsed).c_str());

    // One malformed entry among well-formed ones is dropped, the rest kept.
    const std::string withGarbage = "07:00|1111100;not-a-schedule;18:30|0000011";
    const std::vector<ScheduleDraft> keptGood = scheduleParse(withGarbage);
    TEST_ASSERT_EQUAL(2u, keptGood.size());
    TEST_ASSERT_EQUAL_STRING("07:00", keptGood[0].time.c_str());
    TEST_ASSERT_EQUAL_STRING("18:30", keptGood[1].time.c_str());
}

// ---------------------------------------------------------------------------
// Group H -- background animation parameters (packed bgAnimParams string)
// ---------------------------------------------------------------------------

// Stand-in for one animation's BgAnimParamDef defaults: four defined
// parameters and four unused slots, which the registry reports as 0. The
// real table is unreachable from the host (it carries the render kernels),
// and every function under test takes the defaults from its caller for
// exactly that reason, so a literal here is the whole fixture.
static const uint8_t kDefs4[kBgAnimParamSlots] = {50, 45, 60, 20, 0, 0, 0, 0};

static void assert_slots(const uint8_t *got, const uint8_t *want) {
    for (int i = 0; i < kBgAnimParamSlots; i++) {
        char msg[24];
        std::snprintf(msg, sizeof(msg), "slot %d", i);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(want[i], got[i], msg);
    }
}

static void test_bgparams_read_defaults_when_unset() {
    uint8_t got[kBgAnimParamSlots];
    // Nothing stored at all.
    bgParamsRead("", 3, kDefs4, got);
    assert_slots(got, kDefs4);
    // Stored, but not this far: three groups, animation 3 asked for.
    bgParamsRead("1,2;3,4;5,6", 3, kDefs4, got);
    assert_slots(got, kDefs4);
    // An explicitly empty group is "keep the defaults" too.
    bgParamsRead("1,2;;5,6", 1, kDefs4, got);
    assert_slots(got, kDefs4);
    // A null defaults pointer reads as all zeroes.
    static const uint8_t zeroes[kBgAnimParamSlots] = {0, 0, 0, 0, 0, 0, 0, 0};
    bgParamsRead("", 0, nullptr, got);
    assert_slots(got, zeroes);
}

static void test_bgparams_read_overrides_and_short_group() {
    uint8_t got[kBgAnimParamSlots];
    // Whole group present: every slot comes from the string.
    bgParamsRead("0,0;7,8,9,10,11,12,13,14", 1, kDefs4, got);
    static const uint8_t all8[kBgAnimParamSlots] = {7, 8, 9, 10, 11, 12, 13, 14};
    assert_slots(got, all8);
    // Two values stored: the other six keep their defaults. This is the
    // compatibility rule that let a stored group of four survive
    // BG_ANIM_PARAMS going from 4 to 8 (gm-3vj.1).
    bgParamsRead("35,40", 0, kDefs4, got);
    static const uint8_t shortGroup[kBgAnimParamSlots] = {35, 40, 60, 20, 0, 0, 0, 0};
    assert_slots(got, shortGroup);
}

static void test_bgparams_read_clamps_and_stops_at_garbage() {
    uint8_t got[kBgAnimParamSlots];
    // Out of range in both directions, clamped to 0..100.
    bgParamsRead("250,-8", 0, kDefs4, got);
    static const uint8_t clamped[kBgAnimParamSlots] = {100, 0, 60, 20, 0, 0, 0, 0};
    assert_slots(got, clamped);
    // A value that is not a number ends the group there; every slot from it
    // on keeps its default, rather than reading as 0.
    bgParamsRead("30,oops,70", 0, kDefs4, got);
    static const uint8_t stopped[kBgAnimParamSlots] = {30, 45, 60, 20, 0, 0, 0, 0};
    assert_slots(got, stopped);
    // A group that is garbage from its first character keeps every default.
    bgParamsRead("nonsense", 0, kDefs4, got);
    assert_slots(got, kDefs4);
}

static void test_bgparams_write_group_repacks_at_eight() {
    static const uint8_t values[kBgAnimParamSlots] = {5, 10, 15, 20, 0, 0, 0, 0};
    // Written at eight values even though the animation defines four, so a
    // later build that gives it more parameters shows what was stored here
    // rather than that build's defaults.
    TEST_ASSERT_EQUAL_STRING("5,10,15,20,0,0,0,0", bgParamsWriteGroup("", 0, values).c_str());
    // Values out of range clamp on the way in.
    static const uint8_t wild[kBgAnimParamSlots] = {200, 0, 0, 0, 0, 0, 0, 101};
    TEST_ASSERT_EQUAL_STRING("100,0,0,0,0,0,0,100", bgParamsWriteGroup("", 0, wild).c_str());
}

static void test_bgparams_write_group_leaves_neighbours_alone() {
    static const uint8_t values[kBgAnimParamSlots] = {1, 2, 3, 4, 5, 6, 7, 8};
    // The neighbours keep their own text, short groups included: this writer
    // never expands a group it was not asked to change.
    TEST_ASSERT_EQUAL_STRING("9,9;1,2,3,4,5,6,7,8;7", bgParamsWriteGroup("9,9;50;7", 1, values).c_str());
    // Animations between the stored end and animId are appended empty.
    TEST_ASSERT_EQUAL_STRING("9,9;;;1,2,3,4,5,6,7,8", bgParamsWriteGroup("9,9", 3, values).c_str());
}

static void test_bgparams_write_slot_fills_the_rest_from_defaults() {
    // Slot 2 of animation 1, whose group is not stored: the other seven
    // slots are written at the animation's defaults, not at zero.
    TEST_ASSERT_EQUAL_STRING("4,4;50,45,80,20,0,0,0,0", bgParamsWriteSlot("4,4", 1, kDefs4, 2, 80).c_str());
    // Slot 0 over a stored short group: the stored second value survives.
    TEST_ASSERT_EQUAL_STRING("25,40,60,20,0,0,0,0", bgParamsWriteSlot("35,40", 0, kDefs4, 0, 25).c_str());
    // Out of range clamps; a slot index off the end changes nothing.
    TEST_ASSERT_EQUAL_STRING("100,45,60,20,0,0,0,0", bgParamsWriteSlot("", 0, kDefs4, 0, 999).c_str());
    TEST_ASSERT_EQUAL_STRING("35,40", bgParamsWriteSlot("35,40", 0, kDefs4, kBgAnimParamSlots, 10).c_str());
}

static void test_bgparams_clear_group_restores_defaults() {
    const std::string cleared = bgParamsClearGroup("1,2;3,4;5,6", 1);
    TEST_ASSERT_EQUAL_STRING("1,2;;5,6", cleared.c_str());
    uint8_t got[kBgAnimParamSlots];
    bgParamsRead(cleared, 1, kDefs4, got);
    assert_slots(got, kDefs4);
    // The neighbours are untouched by the clear.
    bgParamsRead(cleared, 0, kDefs4, got);
    static const uint8_t first[kBgAnimParamSlots] = {1, 2, 60, 20, 0, 0, 0, 0};
    assert_slots(got, first);
    // Clearing the last group that carried anything trims back to "", the
    // state a device that never edited a parameter stores.
    TEST_ASSERT_EQUAL_STRING("1,2", bgParamsClearGroup("1,2;3,4", 1).c_str());
    TEST_ASSERT_EQUAL_STRING("", bgParamsClearGroup("3,4", 0).c_str());
    // Clearing past the stored end is a no-op, not a string full of ';'.
    TEST_ASSERT_EQUAL_STRING("1,2", bgParamsClearGroup("1,2", 5).c_str());
}

static void test_bgparams_spec_steps_and_clamps() {
    TEST_ASSERT_EQUAL(0, kBgAnimParamSpec.minValue);
    TEST_ASSERT_EQUAL(100, kBgAnimParamSpec.maxValue);
    TEST_ASSERT_EQUAL(5, stepValue(0, 1, false, kBgAnimParamSpec));
    TEST_ASSERT_EQUAL(10, stepValue(0, 1, true, kBgAnimParamSpec));
    TEST_ASSERT_EQUAL(0, stepValue(0, -1, false, kBgAnimParamSpec));
    TEST_ASSERT_EQUAL(100, stepValue(100, 1, false, kBgAnimParamSpec));
    // A slider-written value off the step-5 grid moves to the nearest grid
    // line in the direction pressed, not a full step past it.
    TEST_ASSERT_EQUAL(45, stepValue(43, 1, false, kBgAnimParamSpec));
    TEST_ASSERT_EQUAL(40, stepValue(43, -1, false, kBgAnimParamSpec));
    TEST_ASSERT_EQUAL_STRING("55", formatNumeric(55, kBgAnimParamSpec).c_str());
}

// ---------------------------------------------------------------------------
// Group I -- the frozen legacy bgAnimTheme namespace and explicit refs
//
// bgAnimTheme used to be the whole setting: 0..17 the built-ins of an
// 18-entry table, 18 the single custom gradient in bgAnimCustomTheme. Devices
// in the field store both. Every case here is run twice, against the table
// this build ships and against a 60-entry one whose first 18 entries are the
// same, because the bug this guards against only appears once the table is
// longer than the sentinel.
// ---------------------------------------------------------------------------

// A 60-entry table: the original 18 unchanged, then 42 distinguishable ones.
// It is built from the first 18 rather than from the whole shipped table so
// that entries 18 and above stay synthetic and keep carrying their index, which
// is how these cases name which built-in a gradient resolved to. The shipped
// table has 60 entries of its own since gm-nov3.5, and a table built on top of
// that one would have no marked entry at 18 at all.
static const bganim_gen::ThemeDef *bigTable(int &count) {
    static std::vector<bganim_gen::ThemeDef> table;
    static std::vector<std::string> names;
    if (table.empty()) {
        for (int i = 0; i < BG_THEME_LEGACY_CUSTOM; i++) {
            table.push_back(bganim_gen::THEME_DEFS[i]);
        }
        names.reserve(64);
        for (int i = BG_THEME_LEGACY_CUSTOM; i < 60; i++) {
            names.push_back("Test " + std::to_string(i));
        }
        for (int i = BG_THEME_LEGACY_CUSTOM, k = 0; i < 60; i++, k++) {
            bganim_gen::ThemeDef def{};
            def.name = names[static_cast<size_t>(k)].c_str();
            def.category = "Test";
            for (int s = 0; s < 6; s++) {
                // Stop 0 carries the index, so a resolved gradient names which
                // built-in it came from.
                def.stops[s][0] = static_cast<uint8_t>(i);
                def.stops[s][1] = static_cast<uint8_t>(s);
                def.stops[s][2] = 0x40;
            }
            table.push_back(def);
        }
    }
    count = static_cast<int>(table.size());
    return table.data();
}

static void useBigTable() {
    int n = 0;
    const bganim_gen::ThemeDef *defs = bigTable(n);
    bg_test_set_theme_table(defs, n);
}

// The table a firmware from before gm-nov3.5 had: the original 18 and nothing
// past the legacy Custom sentinel. This is what a rollback lands on, and it is
// the only way to reach that state now that the shipped table is longer.
static void useShortTable() {
    bg_test_set_theme_table(bganim_gen::THEME_DEFS, BG_THEME_LEGACY_CUSTOM);
}

// The custom gradient every fixture below uses, and its canonical uniform
// form (what a library copy of it has to be).
static const char *kCustom = "112233,445566,778899";
static const char *kCustomPositioned = "112233@0,445566@10,778899@255";

static std::string legacyStops(int themeId, const char *custom) {
    uint8_t stops[BG_THEME_MAX_STOPS][3];
    int n = 0;
    bg_resolve_theme(themeId, custom, stops, n);
    std::string out;
    char buf[16];
    for (int i = 0; i < n; i++) {
        std::snprintf(buf, sizeof(buf), "%02x%02x%02x,", stops[i][0], stops[i][1], stops[i][2]);
        out += buf;
    }
    return out;
}

// What bg_resolve_anim_theme draws, as "rrggbb,... |uniform".
static std::string resolved(int animId, const char *map, const char *library, const char *globalRef, int themeId,
                           const char *custom) {
    uint8_t stops[BG_THEME_MAX_STOPS][3];
    uint8_t pos[BG_THEME_MAX_STOPS];
    int n = 0;
    bool uniform = true;
    bg_resolve_anim_theme(animId, map, library, globalRef, themeId, custom, stops, pos, n, uniform);
    std::string out;
    char buf[24];
    for (int i = 0; i < n; i++) {
        std::snprintf(buf, sizeof(buf), "%02x%02x%02x@%d,", stops[i][0], stops[i][1], stops[i][2], pos[i]);
        out += buf;
    }
    out += uniform ? "|u" : "|p";
    return out;
}

static std::string builtinStops(int themeId) {
    uint8_t stops[BG_THEME_MAX_STOPS][3];
    int n = 0;
    bg_resolve_theme(themeId, "", stops, n);
    std::string out;
    char buf[16];
    for (int i = 0; i < n; i++) {
        std::snprintf(buf, sizeof(buf), "%02x%02x%02x,", stops[i][0], stops[i][1], stops[i][2]);
        out += buf;
    }
    return out;
}

static void test_legacy_integers_keep_their_meaning() {
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 0) {
            useShippedTable();
        } else {
            useBigTable();
        }
        // 0..17: the original built-ins, the same six stops in both passes.
        for (int t = 0; t < BG_THEME_LEGACY_CUSTOM; t++) {
            uint8_t stops[BG_THEME_MAX_STOPS][3];
            int n = 0;
            bg_resolve_theme(t, "", stops, n);
            TEST_ASSERT_EQUAL(6, n);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(bganim_gen::THEME_DEFS[t].stops, stops, 18);
        }
        // 18 with a valid custom string: the custom string, still.
        TEST_ASSERT_EQUAL_STRING("112233,445566,778899,", legacyStops(BG_THEME_LEGACY_CUSTOM, kCustom).c_str());
        // 18 with an empty or malformed one: built-in 0, in both passes. It
        // must never become the built-in that lands at index 18.
        TEST_ASSERT_EQUAL_STRING(builtinStops(0).c_str(), legacyStops(BG_THEME_LEGACY_CUSTOM, "").c_str());
        TEST_ASSERT_EQUAL_STRING(builtinStops(0).c_str(), legacyStops(BG_THEME_LEGACY_CUSTOM, "zzz").c_str());
        TEST_ASSERT_EQUAL_STRING(builtinStops(0).c_str(), legacyStops(BG_THEME_LEGACY_CUSTOM, "112233").c_str());
        // Legacy integers this firmware never wrote: built-in 0, in both
        // passes. On the long table 30 must not become built-in 30.
        for (int bad : {-1, 19, 20, 30, 59, 1000}) {
            TEST_ASSERT_EQUAL_STRING(builtinStops(0).c_str(), legacyStops(bad, kCustom).c_str());
        }
    }
    useShippedTable();
}

static void test_explicit_builtin_refs_resolve_directly() {
    useBigTable();
    // A valid explicit ref names the built-in at that index, even while the
    // legacy pair still says "the custom gradient". This is the separation
    // the whole bead is about: two namespaces over the same digits.
    const std::string want18 = resolved(0, "", "", "18", 0, "");
    const std::string want30 = resolved(0, "", "", "30", 0, "");
    TEST_ASSERT_EQUAL_STRING(want18.c_str(),
                             resolved(0, "", "", "18", BG_THEME_LEGACY_CUSTOM, kCustom).c_str());
    TEST_ASSERT_EQUAL_STRING(want30.c_str(),
                             resolved(0, "", "", "30", BG_THEME_LEGACY_CUSTOM, kCustom).c_str());
    // The built-in's own colours, not the custom string's.
    TEST_ASSERT_TRUE(want18.rfind("120040@0,", 0) == 0); // stop 0 carries index 18
    TEST_ASSERT_TRUE(want30.rfind("1e0040@0,", 0) == 0); // ... and 30
    // The same through a per-animation override.
    TEST_ASSERT_EQUAL_STRING(want30.c_str(),
                             resolved(1, ";30", "", "", BG_THEME_LEGACY_CUSTOM, kCustom).c_str());
    // An override wins over the global outright.
    TEST_ASSERT_EQUAL_STRING(want18.c_str(), resolved(1, ";18", "", "30", 0, "").c_str());

    // On an 18-entry table neither ref resolves, so both fall through to the
    // legacy pair, which still means the custom gradient.
    useShortTable();
    const std::string legacy = resolved(0, "", "", "", BG_THEME_LEGACY_CUSTOM, kCustom);
    TEST_ASSERT_EQUAL_STRING(legacy.c_str(), resolved(0, "", "", "18", BG_THEME_LEGACY_CUSTOM, kCustom).c_str());
    TEST_ASSERT_EQUAL_STRING(legacy.c_str(), resolved(0, "", "", "30", BG_THEME_LEGACY_CUSTOM, kCustom).c_str());
}

static void test_upgrade_rollback_upgrade_keeps_the_same_gradient() {
    // A global built-in ref of 30, with the rollback mirror the four writers
    // apply: bgAnimTheme 0, because 30 has no meaning in the legacy namespace.
    const int mirror = bg_legacy_mirror_for_ref("30", 60);
    TEST_ASSERT_EQUAL(0, mirror);

    useBigTable();
    const std::string up = resolved(0, "", "", "30", mirror, "");
    TEST_ASSERT_TRUE(up.rfind("1e0040@0,", 0) == 0);
    useShortTable();
    const std::string back = resolved(0, "", "", "30", mirror, "");
    TEST_ASSERT_EQUAL_STRING(builtinStops(0).c_str(), legacyStops(mirror, "").c_str());
    TEST_ASSERT_TRUE(back.rfind("080402@0,", 0) == 0); // built-in 0, Espresso
    useBigTable();
    TEST_ASSERT_EQUAL_STRING(up.c_str(), resolved(0, "", "", "30", mirror, "").c_str());

    // The same for index 18, which is the one that used to mean "custom".
    const int mirror18 = bg_legacy_mirror_for_ref("18", 60);
    TEST_ASSERT_EQUAL(0, mirror18);
    const std::string up18 = resolved(0, "", "", "18", mirror18, "");
    TEST_ASSERT_TRUE(up18.rfind("120040@0,", 0) == 0);
    useShortTable();
    TEST_ASSERT_TRUE(resolved(0, "", "", "18", mirror18, "").rfind("080402@0,", 0) == 0);
    useBigTable();
    TEST_ASSERT_EQUAL_STRING(up18.c_str(), resolved(0, "", "", "18", mirror18, "").c_str());

    // A per-animation override that does not resolve falls through the global,
    // on both builds.
    TEST_ASSERT_EQUAL_STRING(up.c_str(), resolved(1, ";c9", "", "30", mirror, "").c_str());
    useShortTable();
    TEST_ASSERT_TRUE(resolved(1, ";c9", "", "30", mirror, "").rfind("080402@0,", 0) == 0);
}

static void test_legacy_mirror_policy() {
    // 0..17 mirror unchanged.
    for (int t = 0; t < BG_THEME_LEGACY_CUSTOM; t++) {
        TEST_ASSERT_EQUAL(t, bg_legacy_mirror_for_ref(std::to_string(t).c_str(), 18));
        TEST_ASSERT_EQUAL(t, bg_legacy_mirror_for_ref(std::to_string(t).c_str(), 60));
    }
    // Appended built-ins mirror as 0, never as 17 and never as themselves.
    TEST_ASSERT_EQUAL(0, bg_legacy_mirror_for_ref("18", 60));
    TEST_ASSERT_EQUAL(0, bg_legacy_mirror_for_ref("30", 60));
    TEST_ASSERT_EQUAL(0, bg_legacy_mirror_for_ref("59", 60));
    // A library ref, an empty ref and a malformed one leave bgAnimTheme alone.
    TEST_ASSERT_EQUAL(-1, bg_legacy_mirror_for_ref("c1", 60));
    TEST_ASSERT_EQUAL(-1, bg_legacy_mirror_for_ref("", 60));
    TEST_ASSERT_EQUAL(-1, bg_legacy_mirror_for_ref(nullptr, 60));
    TEST_ASSERT_EQUAL(-1, bg_legacy_mirror_for_ref("3x", 60));
    // So does an index this build's table does not have: the ref does not
    // resolve, so the legacy fallback is what draws and must not move.
    TEST_ASSERT_EQUAL(-1, bg_legacy_mirror_for_ref("30", 18));
    TEST_ASSERT_EQUAL(-1, bg_legacy_mirror_for_ref("99", 60));

    // The sequence the four writers have to survive: pick an appended
    // built-in, then a library entry, before anything is saved. The mirror
    // must not be left holding an appended index.
    int theme = 7;
    int m = bg_legacy_mirror_for_ref("30", 60);
    if (m >= 0) {
        theme = m;
    }
    TEST_ASSERT_EQUAL(0, theme);
    m = bg_legacy_mirror_for_ref("c1", 60);
    if (m >= 0) {
        theme = m;
    }
    TEST_ASSERT_EQUAL(0, theme);
    // And 17, which does mean the same thing in both namespaces.
    m = bg_legacy_mirror_for_ref("17", 60);
    TEST_ASSERT_EQUAL(17, m);
}

static void test_legacy_builtin_reading() {
    TEST_ASSERT_EQUAL(-1, bg_legacy_builtin(BG_THEME_LEGACY_CUSTOM, true));
    TEST_ASSERT_EQUAL(0, bg_legacy_builtin(BG_THEME_LEGACY_CUSTOM, false));
    TEST_ASSERT_EQUAL(5, bg_legacy_builtin(5, true));
    TEST_ASSERT_EQUAL(17, bg_legacy_builtin(17, false));
    TEST_ASSERT_EQUAL(0, bg_legacy_builtin(19, true));
    TEST_ASSERT_EQUAL(0, bg_legacy_builtin(-4, true));
    TEST_ASSERT_TRUE(bg_custom_valid(kCustom));
    TEST_ASSERT_TRUE(bg_custom_valid(kCustomPositioned));
    TEST_ASSERT_FALSE(bg_custom_valid(""));
    TEST_ASSERT_FALSE(bg_custom_valid(nullptr));
    TEST_ASSERT_FALSE(bg_custom_valid("112233"));
}

// ---------------------------------------------------------------------------
// Group J -- the legacy custom gradient's migration
//
// The planner is pure and the runner drives it through an abstract store, so
// both are the code the boot path runs (DefaultUI::migrateBgAnimGradients
// supplies a store over Settings). The store below is a fake NVS with the
// same failure shapes the real one has.
// ---------------------------------------------------------------------------

struct FakeNvs {
    // What is durable, and what has been set but not flushed.
    std::string library, globalRef, custom, map;
    int theme = 0;
    std::string pendLibrary, pendGlobalRef, pendCustom;
    int pendTheme = 0;
    bool haveLibrary = false, haveGlobalRef = false, haveLegacy = false;
    // Which flush call fails: 1 for the first, 2 for the second, 0 for none.
    int failFlushAt = 0;
    // A clear that reports success and does not land, which is what
    // Preferences does with an empty string (Property.h, nvsPutString).
    bool loseTheClear = false;
    int flushes = 0;
    int writes = 0;
};

static const char *fakeLibrary(void *user) { return static_cast<FakeNvs *>(user)->library.c_str(); }
static void fakeSetLibrary(void *user, const char *v) {
    auto *f = static_cast<FakeNvs *>(user);
    f->pendLibrary = v;
    f->haveLibrary = true;
}
static void fakeSetGlobalRef(void *user, const char *v) {
    auto *f = static_cast<FakeNvs *>(user);
    f->pendGlobalRef = v;
    f->haveGlobalRef = true;
}
static void fakeSetLegacy(void *user, int themeId, const char *custom) {
    auto *f = static_cast<FakeNvs *>(user);
    f->pendTheme = themeId;
    f->pendCustom = custom;
    f->haveLegacy = true;
}
static bool fakeFlush(void *user) {
    auto *f = static_cast<FakeNvs *>(user);
    f->flushes++;
    const bool fail = f->failFlushAt == f->flushes;
    if (f->haveLibrary && !fail) {
        f->library = f->pendLibrary;
        f->writes++;
    }
    if (f->haveGlobalRef && !fail) {
        f->globalRef = f->pendGlobalRef;
        f->writes++;
    }
    if (f->haveLegacy && !fail) {
        f->theme = f->pendTheme;
        f->writes++;
        // The theme writes first (registration order), then the string. A
        // lost clear leaves the old custom string durable and still reports
        // success, exactly as the real path does.
        if (!f->loseTheClear) {
            f->custom = f->pendCustom;
        }
    }
    f->haveLibrary = f->haveGlobalRef = f->haveLegacy = false;
    return !fail;
}

static BgGradientStore fakeStore(FakeNvs &nvs) {
    BgGradientStore s;
    s.user = &nvs;
    s.library = fakeLibrary;
    s.setLibrary = fakeSetLibrary;
    s.setGlobalRef = fakeSetGlobalRef;
    s.setLegacy = fakeSetLegacy;
    s.flush = fakeFlush;
    return s;
}

// One boot: plan from what is durable, run it, report the result.
static BgMigrateResult boot(FakeNvs &nvs) {
    const BgGradientMigration plan = bg_plan_gradient_migration(nvs.library.c_str(), nvs.custom.c_str(), nvs.theme,
                                                               nvs.globalRef.c_str(), nvs.map.c_str());
    BgGradientStore store = fakeStore(nvs);
    return bg_run_gradient_migration(store, plan);
}

// What every animation with no override of its own draws.
static std::string effective(const FakeNvs &nvs) {
    return resolved(0, nvs.map.c_str(), nvs.library.c_str(), nvs.globalRef.c_str(), nvs.theme, nvs.custom.c_str());
}

static int libraryEntryCount(const std::string &library) {
    if (library.empty()) {
        return 0;
    }
    int n = 1;
    for (char c : library) {
        if (c == ';') {
            n++;
        }
    }
    return n;
}

static void test_migrate_active_custom_gains_entry_and_ref() {
    FakeNvs nvs;
    nvs.custom = kCustom;
    nvs.theme = BG_THEME_LEGACY_CUSTOM;
    nvs.map = "3;;c4"; // the map is the user's and is never touched
    const std::string before = effective(nvs);

    TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::Done), static_cast<int>(boot(nvs)));
    TEST_ASSERT_EQUAL_STRING("1|Custom|112233,445566,778899", nvs.library.c_str());
    TEST_ASSERT_EQUAL_STRING("c1", nvs.globalRef.c_str());
    TEST_ASSERT_EQUAL_STRING("", nvs.custom.c_str());
    TEST_ASSERT_EQUAL(0, nvs.theme);
    TEST_ASSERT_EQUAL_STRING("3;;c4", nvs.map.c_str());
    // Same stops and the same interpolation, before and after.
    TEST_ASSERT_EQUAL_STRING(before.c_str(), effective(nvs).c_str());

    // A second boot changes nothing and adds nothing.
    const std::string lib = nvs.library;
    const int writes = nvs.writes;
    TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::NothingToDo), static_cast<int>(boot(nvs)));
    TEST_ASSERT_EQUAL_STRING(lib.c_str(), nvs.library.c_str());
    TEST_ASSERT_EQUAL(writes, nvs.writes);
}

static void test_migrate_positioned_custom_keeps_uniform_rendering() {
    FakeNvs nvs;
    nvs.custom = kCustomPositioned;
    nvs.theme = BG_THEME_LEGACY_CUSTOM;
    const std::string before = effective(nvs);
    TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::Done), static_cast<int>(boot(nvs)));
    // The copy is the uniform form: the legacy resolver threw the positions
    // away, so keeping them would change the picture.
    TEST_ASSERT_EQUAL_STRING("1|Custom|112233,445566,778899", nvs.library.c_str());
    TEST_ASSERT_EQUAL_STRING(before.c_str(), effective(nvs).c_str());
    TEST_ASSERT_TRUE(before.find("|u") != std::string::npos);

    // An existing positional entry keeps its own semantics and is not reused.
    FakeNvs other;
    other.library = "1|Mine|112233@0,445566@10,778899@255";
    other.custom = kCustom;
    other.theme = BG_THEME_LEGACY_CUSTOM;
    TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::Done), static_cast<int>(boot(other)));
    TEST_ASSERT_EQUAL_STRING("1|Mine|112233@0,445566@10,778899@255;2|Custom|112233,445566,778899",
                             other.library.c_str());
    TEST_ASSERT_EQUAL_STRING("c2", other.globalRef.c_str());
    // The user's own entry still draws with its own positions.
    TEST_ASSERT_TRUE(resolved(0, "", other.library.c_str(), "c1", 0, "").find("|p") != std::string::npos);
}

static void test_migrate_already_migrated_device_is_not_reactivated() {
    // What the first version of this migration left behind: the entry, every
    // map slot pointed at it, bgAnimTheme reset, and the custom string still
    // there. Its presence is not evidence that it is in use.
    FakeNvs nvs;
    nvs.library = "1|Custom|112233,445566,778899";
    nvs.map = "c1;c1;c1";
    nvs.theme = 0;
    nvs.custom = kCustom;
    const std::string before = effective(nvs);

    TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::Done), static_cast<int>(boot(nvs)));
    // No new entry, no global ref invented, the map untouched.
    TEST_ASSERT_EQUAL_STRING("1|Custom|112233,445566,778899", nvs.library.c_str());
    TEST_ASSERT_EQUAL_STRING("", nvs.globalRef.c_str());
    TEST_ASSERT_EQUAL_STRING("c1;c1;c1", nvs.map.c_str());
    TEST_ASSERT_EQUAL(0, nvs.theme);
    TEST_ASSERT_EQUAL_STRING("", nvs.custom.c_str());
    TEST_ASSERT_EQUAL_STRING(before.c_str(), effective(nvs).c_str());
}

static void test_migrate_preserves_newer_selections() {
    // A newer built-in global ref, with the legacy pair left at "custom".
    {
        FakeNvs nvs;
        nvs.custom = kCustom;
        nvs.theme = BG_THEME_LEGACY_CUSTOM;
        nvs.globalRef = "5";
        const std::string before = effective(nvs);
        boot(nvs);
        TEST_ASSERT_EQUAL_STRING("5", nvs.globalRef.c_str());
        TEST_ASSERT_EQUAL(BG_THEME_LEGACY_CUSTOM, nvs.theme);
        TEST_ASSERT_EQUAL_STRING(kCustom, nvs.custom.c_str());
        TEST_ASSERT_EQUAL_STRING(before.c_str(), effective(nvs).c_str());
    }
    // A library global ref the user picked.
    {
        FakeNvs nvs;
        nvs.library = "4|Mine|010203,040506";
        nvs.custom = kCustom;
        nvs.theme = BG_THEME_LEGACY_CUSTOM;
        nvs.globalRef = "c4";
        const std::string before = effective(nvs);
        boot(nvs);
        TEST_ASSERT_EQUAL_STRING("c4", nvs.globalRef.c_str());
        TEST_ASSERT_EQUAL_STRING(before.c_str(), effective(nvs).c_str());
    }
    // A non-empty global ref that does not resolve in this build. It is the
    // user's newer choice and could resolve after an upgrade, so it stays,
    // and while it does not resolve the legacy fallback behind it stays too.
    {
        FakeNvs nvs;
        nvs.custom = kCustom;
        nvs.theme = BG_THEME_LEGACY_CUSTOM;
        nvs.globalRef = "30";
        const std::string before = effective(nvs);
        boot(nvs);
        TEST_ASSERT_EQUAL_STRING("30", nvs.globalRef.c_str());
        TEST_ASSERT_EQUAL(BG_THEME_LEGACY_CUSTOM, nvs.theme);
        TEST_ASSERT_EQUAL_STRING(kCustom, nvs.custom.c_str());
        TEST_ASSERT_EQUAL_STRING(before.c_str(), effective(nvs).c_str());
        // The copy is there, so the data is safe, and one entry only however
        // many times it boots.
        TEST_ASSERT_EQUAL(1, libraryEntryCount(nvs.library));
        boot(nvs);
        boot(nvs);
        TEST_ASSERT_EQUAL(1, libraryEntryCount(nvs.library));
    }
    // Mixed map overrides and an inactive custom string: the legacy pair
    // names a built-in, so the string is inert, and every ref stays.
    {
        FakeNvs nvs;
        nvs.library = "2|Mine|010203,040506";
        nvs.map = "7;c2;;3";
        nvs.theme = 7;
        nvs.custom = kCustom;
        std::vector<std::string> before;
        for (int a = 0; a < 5; a++) {
            before.push_back(resolved(a, nvs.map.c_str(), nvs.library.c_str(), nvs.globalRef.c_str(), nvs.theme,
                                      nvs.custom.c_str()));
        }
        boot(nvs);
        TEST_ASSERT_EQUAL_STRING("7;c2;;3", nvs.map.c_str());
        TEST_ASSERT_EQUAL_STRING("", nvs.globalRef.c_str());
        TEST_ASSERT_EQUAL(7, nvs.theme);
        for (int a = 0; a < 5; a++) {
            TEST_ASSERT_EQUAL_STRING(before[static_cast<size_t>(a)].c_str(),
                                     resolved(a, nvs.map.c_str(), nvs.library.c_str(), nvs.globalRef.c_str(),
                                              nvs.theme, nvs.custom.c_str())
                                         .c_str());
        }
    }
}

static void test_migrate_destination_choice() {
    // An existing entry with the same gradient is reused, whatever it is
    // called and whatever id it has.
    {
        FakeNvs nvs;
        nvs.library = "7|Anything|112233,445566,778899";
        nvs.custom = kCustom;
        nvs.theme = BG_THEME_LEGACY_CUSTOM;
        boot(nvs);
        TEST_ASSERT_EQUAL_STRING("7|Anything|112233,445566,778899", nvs.library.c_str());
        TEST_ASSERT_EQUAL_STRING("c7", nvs.globalRef.c_str());
    }
    // Id 1 occupied: the next free one is taken, and nothing is overwritten.
    {
        FakeNvs nvs;
        nvs.library = "1|Theirs|010203,040506";
        nvs.custom = kCustom;
        nvs.theme = BG_THEME_LEGACY_CUSTOM;
        boot(nvs);
        TEST_ASSERT_EQUAL_STRING("1|Theirs|010203,040506;2|Custom|112233,445566,778899", nvs.library.c_str());
        TEST_ASSERT_EQUAL_STRING("c2", nvs.globalRef.c_str());
    }
    // Non-contiguous ids, including one at the top of the accepted range.
    {
        FakeNvs nvs;
        nvs.library = "1|A|010203,040506;3|B|060504,030201;99999|C|0a0b0c,0d0e0f";
        nvs.custom = kCustom;
        nvs.theme = BG_THEME_LEGACY_CUSTOM;
        boot(nvs);
        TEST_ASSERT_EQUAL_STRING("c2", nvs.globalRef.c_str());
        TEST_ASSERT_TRUE(nvs.library.find("1|A|") != std::string::npos);
        TEST_ASSERT_TRUE(nvs.library.find("99999|C|") != std::string::npos);
        TEST_ASSERT_TRUE(nvs.library.find(";2|Custom|") != std::string::npos);
    }
    // An id a dangling ref already names is not handed out: making that ref
    // resolve would change what the animation draws.
    {
        FakeNvs nvs;
        nvs.map = ";c1;c2";
        nvs.custom = kCustom;
        nvs.theme = BG_THEME_LEGACY_CUSTOM;
        boot(nvs);
        TEST_ASSERT_EQUAL_STRING("3|Custom|112233,445566,778899", nvs.library.c_str());
        TEST_ASSERT_EQUAL_STRING("c3", nvs.globalRef.c_str());
    }
}

// True when nothing in the library carries this id, i.e. a ref to it still
// dangles. Uses the production lookup, which is what a ref is resolved with.
static bool libraryHasId(const std::string &library, int id) {
    uint8_t stops[BG_THEME_MAX_STOPS][3];
    uint8_t pos[BG_THEME_MAX_STOPS];
    int n = 0;
    bool uniform = true;
    return bg_library_lookup(library.c_str(), id, stops, pos, n, uniform);
}

static void test_migrate_never_reuses_an_ambiguous_duplicate_id() {
    // Duplicate ids validate, and bg_library_lookup answers with the first
    // entry carrying one. Here the entry that matches the legacy gradient is
    // the second of the two, so it is not what c1 draws. Reusing it would
    // publish c1, retire the legacy string, and leave every animation drawing
    // 000000..ffffff: the user's gradient gone.
    {
        FakeNvs nvs;
        nvs.library = "1|Other|000000,ffffff;1|Match|112233,445566,778899";
        nvs.custom = kCustom;
        nvs.theme = BG_THEME_LEGACY_CUSTOM;
        TEST_ASSERT_TRUE(bg_library_valid(nvs.library.c_str()));
        const std::string lib = nvs.library;
        const std::string before = effective(nvs);

        TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::Done), static_cast<int>(boot(nvs)));

        // Both existing entries are exactly as they were, and the copy went
        // to an id of its own.
        TEST_ASSERT_EQUAL_STRING((lib + ";2|Custom|112233,445566,778899").c_str(), nvs.library.c_str());
        TEST_ASSERT_EQUAL_STRING("c2", nvs.globalRef.c_str());
        // The published ref resolves, through the production lookup, to the
        // gradient that was retired.
        TEST_ASSERT_EQUAL_STRING("", nvs.custom.c_str());
        TEST_ASSERT_EQUAL(0, nvs.theme);
        TEST_ASSERT_EQUAL_STRING(before.c_str(), effective(nvs).c_str());
        // And c1 still draws what it drew: the first entry, untouched.
        TEST_ASSERT_EQUAL_STRING(
            resolved(0, "", lib.c_str(), "c1", 0, "").c_str(),
            resolved(0, "", nvs.library.c_str(), "c1", 0, "").c_str());
        // Nothing left for the next boot.
        TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::NothingToDo), static_cast<int>(boot(nvs)));
    }
    // The mirror case: the duplicated id names the entry that matches, so
    // reuse is safe and nothing is appended.
    {
        FakeNvs nvs;
        nvs.library = "1|Match|112233,445566,778899;1|Other|000000,ffffff";
        nvs.custom = kCustom;
        nvs.theme = BG_THEME_LEGACY_CUSTOM;
        const std::string lib = nvs.library;
        const std::string before = effective(nvs);

        TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::Done), static_cast<int>(boot(nvs)));
        TEST_ASSERT_EQUAL_STRING(lib.c_str(), nvs.library.c_str());
        TEST_ASSERT_EQUAL_STRING("c1", nvs.globalRef.c_str());
        TEST_ASSERT_EQUAL_STRING(before.c_str(), effective(nvs).c_str());
    }
    // A duplicate whose colours match but at its own positions is not a
    // destination either, whichever entry the id names.
    {
        FakeNvs nvs;
        nvs.library = "1|Positioned|112233@0,445566@10,778899@255;1|Other|000000,ffffff";
        nvs.custom = kCustom;
        nvs.theme = BG_THEME_LEGACY_CUSTOM;
        const std::string lib = nvs.library;
        const std::string before = effective(nvs);

        TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::Done), static_cast<int>(boot(nvs)));
        TEST_ASSERT_EQUAL_STRING((lib + ";2|Custom|112233,445566,778899").c_str(), nvs.library.c_str());
        TEST_ASSERT_EQUAL_STRING("c2", nvs.globalRef.c_str());
        TEST_ASSERT_EQUAL_STRING(before.c_str(), effective(nvs).c_str());
    }
}

static void test_migrate_allocates_past_the_first_thirteen_ids() {
    // Thirteen dangling map refs and an empty library. The allocator takes the
    // lowest id in 1..BG_GRADIENT_ID_MAX that no library entry holds and no
    // stored ref names, so these thirteen dangling refs are what force it past
    // the entry limit to 14. That is the point of the case: an id above the
    // library's own capacity arises in normal use, from refs alone.
    {
        FakeNvs nvs;
        nvs.map = "c1;c2;c3;c4;c5;c6;c7;c8;c9;c10;c11;c12;c13";
        nvs.custom = kCustom;
        nvs.theme = BG_THEME_LEGACY_CUSTOM;
        const std::string map = nvs.map;
        std::vector<std::string> before;
        for (int a = 0; a < 15; a++) {
            before.push_back(resolved(a, nvs.map.c_str(), nvs.library.c_str(), nvs.globalRef.c_str(), nvs.theme,
                                      nvs.custom.c_str()));
        }

        TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::Done), static_cast<int>(boot(nvs)));
        TEST_ASSERT_EQUAL_STRING("14|Custom|112233,445566,778899", nvs.library.c_str());
        TEST_ASSERT_EQUAL_STRING("c14", nvs.globalRef.c_str());
        TEST_ASSERT_EQUAL_STRING(map.c_str(), nvs.map.c_str());
        // The thirteen refs still dangle, so what they draw has not moved.
        for (int id = 1; id <= 13; id++) {
            TEST_ASSERT_FALSE(libraryHasId(nvs.library, id));
        }
        for (int a = 0; a < 15; a++) {
            TEST_ASSERT_EQUAL_STRING(before[static_cast<size_t>(a)].c_str(),
                                     resolved(a, nvs.map.c_str(), nvs.library.c_str(), nvs.globalRef.c_str(),
                                              nvs.theme, nvs.custom.c_str())
                                         .c_str());
        }
    }
    // The same with an entry at the top of the accepted range in the way:
    // the new id goes in the gap the refs leave, and 99999 is untouched.
    {
        FakeNvs nvs;
        nvs.library = "99999|Top|010203,040506";
        nvs.map = "c1;c2;c3;c4;c5;c6;c7;c8;c9;c10;c11;c12;c13";
        nvs.custom = kCustom;
        nvs.theme = BG_THEME_LEGACY_CUSTOM;
        const std::string before = effective(nvs);
        const std::string top = resolved(0, "", nvs.library.c_str(), "c99999", 0, "");

        TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::Done), static_cast<int>(boot(nvs)));
        TEST_ASSERT_EQUAL_STRING("99999|Top|010203,040506;14|Custom|112233,445566,778899", nvs.library.c_str());
        TEST_ASSERT_EQUAL_STRING("c14", nvs.globalRef.c_str());
        TEST_ASSERT_EQUAL_STRING(top.c_str(), resolved(0, "", nvs.library.c_str(), "c99999", 0, "").c_str());
        TEST_ASSERT_EQUAL_STRING(before.c_str(), effective(nvs).c_str());
        for (int id = 1; id <= 13; id++) {
            TEST_ASSERT_FALSE(libraryHasId(nvs.library, id));
        }
    }
    // A dangling ref at the top of the range reserves only itself.
    {
        FakeNvs nvs;
        nvs.globalRef = "c99999";
        nvs.map = "c99999";
        nvs.custom = kCustom;
        nvs.theme = BG_THEME_LEGACY_CUSTOM;
        const std::string before = effective(nvs);

        TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::Done), static_cast<int>(boot(nvs)));
        TEST_ASSERT_EQUAL_STRING("1|Custom|112233,445566,778899", nvs.library.c_str());
        // The stored global ref is not this migration's to replace, so the
        // legacy pair stays behind it and what draws does not move.
        TEST_ASSERT_EQUAL_STRING("c99999", nvs.globalRef.c_str());
        TEST_ASSERT_EQUAL_STRING(kCustom, nvs.custom.c_str());
        TEST_ASSERT_EQUAL_STRING(before.c_str(), effective(nvs).c_str());
        TEST_ASSERT_FALSE(libraryHasId(nvs.library, 99999));
    }
}

static void test_migrate_defers_rather_than_evicting() {
    // A full library with nothing matching: deferred, nothing written, and
    // the custom gradient still resolves.
    {
        FakeNvs nvs;
        for (int i = 1; i <= BG_GRADIENT_LIB_MAX; i++) {
            if (i > 1) {
                nvs.library += ';';
            }
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%d|G%d|0%d0203,040506", i, i, i % 10);
            nvs.library += buf;
        }
        const std::string lib = nvs.library;
        nvs.custom = kCustom;
        nvs.theme = BG_THEME_LEGACY_CUSTOM;
        const std::string before = effective(nvs);
        TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::Deferred), static_cast<int>(boot(nvs)));
        TEST_ASSERT_EQUAL_STRING(lib.c_str(), nvs.library.c_str());
        TEST_ASSERT_EQUAL_STRING("", nvs.globalRef.c_str());
        TEST_ASSERT_EQUAL_STRING(kCustom, nvs.custom.c_str());
        TEST_ASSERT_EQUAL(0, nvs.writes);
        TEST_ASSERT_EQUAL_STRING(before.c_str(), effective(nvs).c_str());
    }
    // The serialized length limit, with room to spare on the entry count, so
    // the deferral can only be the length.
    //
    // Entry content alone cannot reach the cap: the widest entry the grammar
    // allows is a five-digit id, a 96-byte name and sixteen positioned stops,
    // 278 characters, so twelve of them are 3347 against a 3800-character
    // cap. The fixture uses eleven such entries and pads the last gradient
    // with the spaces the parser tolerates between stops, which is what a
    // hand-edited library can look like. The earlier version of this fixture
    // padded a name instead, past the length walkLibrary accepts, so the
    // library was malformed and the planner deferred for that reason without
    // ever reaching the length check.
    {
        FakeNvs nvs;
        for (int i = 1; i <= BG_GRADIENT_LIB_MAX - 1; i++) {
            if (i > 1) {
                nvs.library += ';';
            }
            nvs.library += std::to_string(i) + "|" + std::string(BG_GRADIENT_NAME_MAX * 4, 'x') + "|";
            for (int s = 0; s < BG_THEME_MAX_STOPS; s++) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%s0102%02x@%d", s > 0 ? "," : "", s, 100 + s * 10);
                nvs.library += buf;
            }
        }
        nvs.library.append(static_cast<size_t>(BG_GRADIENT_LIB_MAX_LEN) - nvs.library.size(), ' ');
        // Exactly at the cap, valid, and one entry short of the entry limit.
        TEST_ASSERT_EQUAL(BG_GRADIENT_LIB_MAX_LEN, static_cast<int>(nvs.library.size()));
        TEST_ASSERT_TRUE(bg_library_valid(nvs.library.c_str()));
        TEST_ASSERT_EQUAL(BG_GRADIENT_LIB_MAX - 1, libraryEntryCount(nvs.library));

        const std::string lib = nvs.library;
        nvs.custom = kCustom;
        nvs.theme = BG_THEME_LEGACY_CUSTOM;
        const std::string before = effective(nvs);
        TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::Deferred), static_cast<int>(boot(nvs)));
        TEST_ASSERT_EQUAL_STRING(lib.c_str(), nvs.library.c_str());
        TEST_ASSERT_EQUAL_STRING("", nvs.globalRef.c_str());
        TEST_ASSERT_EQUAL_STRING(kCustom, nvs.custom.c_str());
        TEST_ASSERT_EQUAL(BG_THEME_LEGACY_CUSTOM, nvs.theme);
        TEST_ASSERT_EQUAL(0, nvs.writes);
        TEST_ASSERT_EQUAL_STRING(before.c_str(), effective(nvs).c_str());
    }
    // A malformed library: never appended to, never replaced.
    {
        FakeNvs nvs;
        nvs.library = "1|Broken|not-a-gradient";
        nvs.custom = kCustom;
        nvs.theme = BG_THEME_LEGACY_CUSTOM;
        TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::Deferred), static_cast<int>(boot(nvs)));
        TEST_ASSERT_EQUAL_STRING("1|Broken|not-a-gradient", nvs.library.c_str());
        TEST_ASSERT_EQUAL(0, nvs.writes);
        TEST_ASSERT_EQUAL_STRING(kCustom, nvs.custom.c_str());
    }
    // Nothing to carry over at all.
    {
        FakeNvs nvs;
        nvs.theme = BG_THEME_LEGACY_CUSTOM;
        TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::NothingToDo), static_cast<int>(boot(nvs)));
        TEST_ASSERT_EQUAL(0, nvs.writes);
        nvs.custom = "zzz";
        TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::NothingToDo), static_cast<int>(boot(nvs)));
        TEST_ASSERT_EQUAL(0, nvs.writes);
        // And clearing the string does not turn the 18 into a built-in.
        TEST_ASSERT_EQUAL_STRING(builtinStops(0).c_str(), legacyStops(nvs.theme, nvs.custom.c_str()).c_str());
    }
}

// ---------------------------------------------------------------------------
// Group K -- fault injection across the persistence steps
//
// Every fixture here is run twice, against the table this build ships and
// against the longer test table. The planner reads the table (refResolves
// asks whether a built-in ref resolves), so the staged persistence has to
// come out the same on a device whose firmware has more built-ins than the
// one that wrote the settings.
// ---------------------------------------------------------------------------

template <typename F> static void underBothThemeTables(F body) {
    useShippedTable();
    body();
    useBigTable();
    body();
    useShippedTable();
}

static void migrate_survives_a_failed_library_write() {
    FakeNvs nvs;
    nvs.custom = kCustom;
    nvs.theme = BG_THEME_LEGACY_CUSTOM;
    nvs.failFlushAt = 1;
    const std::string before = effective(nvs);
    TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::Incomplete), static_cast<int>(boot(nvs)));
    // Nothing durable changed, so the source is still there and the picture
    // is still the custom gradient.
    TEST_ASSERT_EQUAL_STRING("", nvs.library.c_str());
    TEST_ASSERT_EQUAL_STRING("", nvs.globalRef.c_str());
    TEST_ASSERT_EQUAL_STRING(kCustom, nvs.custom.c_str());
    TEST_ASSERT_EQUAL_STRING(before.c_str(), effective(nvs).c_str());

    // The next boot completes it, with one entry and no duplicate.
    nvs.failFlushAt = 0;
    nvs.flushes = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::Done), static_cast<int>(boot(nvs)));
    TEST_ASSERT_EQUAL(1, libraryEntryCount(nvs.library));
    TEST_ASSERT_EQUAL_STRING("c1", nvs.globalRef.c_str());
    TEST_ASSERT_EQUAL_STRING(before.c_str(), effective(nvs).c_str());
}

static void migrate_survives_a_failed_ref_write() {
    FakeNvs nvs;
    nvs.custom = kCustom;
    nvs.theme = BG_THEME_LEGACY_CUSTOM;
    nvs.failFlushAt = 2; // the library lands, the ref does not
    const std::string before = effective(nvs);
    TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::Incomplete), static_cast<int>(boot(nvs)));
    TEST_ASSERT_EQUAL(1, libraryEntryCount(nvs.library));
    TEST_ASSERT_EQUAL_STRING("", nvs.globalRef.c_str());
    // The legacy fields are untouched, so the picture has not moved.
    TEST_ASSERT_EQUAL(BG_THEME_LEGACY_CUSTOM, nvs.theme);
    TEST_ASSERT_EQUAL_STRING(kCustom, nvs.custom.c_str());
    TEST_ASSERT_EQUAL_STRING(before.c_str(), effective(nvs).c_str());

    // A reboot re-plans, reuses the entry that is already there and finishes.
    nvs.failFlushAt = 0;
    nvs.flushes = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::Done), static_cast<int>(boot(nvs)));
    TEST_ASSERT_EQUAL(1, libraryEntryCount(nvs.library));
    TEST_ASSERT_EQUAL_STRING("c1", nvs.globalRef.c_str());
    TEST_ASSERT_EQUAL_STRING("", nvs.custom.c_str());
    TEST_ASSERT_EQUAL_STRING(before.c_str(), effective(nvs).c_str());
}

static void migrate_survives_a_clear_that_lies() {
    FakeNvs nvs;
    nvs.custom = kCustom;
    nvs.theme = BG_THEME_LEGACY_CUSTOM;
    nvs.loseTheClear = true;
    const std::string before = effective(nvs);
    TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::Done), static_cast<int>(boot(nvs)));
    // The string is still in storage even though the write reported success.
    TEST_ASSERT_EQUAL_STRING(kCustom, nvs.custom.c_str());
    TEST_ASSERT_EQUAL_STRING("c1", nvs.globalRef.c_str());
    TEST_ASSERT_EQUAL(0, nvs.theme);
    TEST_ASSERT_EQUAL_STRING(before.c_str(), effective(nvs).c_str());

    // The next boot is safe: it reuses the entry, does not invent a second
    // global ref, and does not put the inactive string back in charge.
    nvs.loseTheClear = false;
    TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::Done), static_cast<int>(boot(nvs)));
    TEST_ASSERT_EQUAL(1, libraryEntryCount(nvs.library));
    TEST_ASSERT_EQUAL_STRING("c1", nvs.globalRef.c_str());
    TEST_ASSERT_EQUAL_STRING("", nvs.custom.c_str());
    TEST_ASSERT_EQUAL_STRING(before.c_str(), effective(nvs).c_str());

    // An inactive string whose clear is lost the same way: still no global
    // ref invented on the boot after.
    FakeNvs inactive;
    inactive.library = "1|Custom|112233,445566,778899";
    inactive.map = "c1;c1";
    inactive.theme = 0;
    inactive.custom = kCustom;
    inactive.loseTheClear = true;
    boot(inactive);
    TEST_ASSERT_EQUAL_STRING(kCustom, inactive.custom.c_str());
    inactive.loseTheClear = false;
    boot(inactive);
    TEST_ASSERT_EQUAL_STRING("", inactive.globalRef.c_str());
    TEST_ASSERT_EQUAL(0, inactive.theme);
    TEST_ASSERT_EQUAL(1, libraryEntryCount(inactive.library));
}

static void migrate_reboot_after_each_durable_step() {
    // Reboot with only the library durable: the entry is reused and the ref
    // is published, never a second entry.
    {
        FakeNvs nvs;
        nvs.library = "1|Custom|112233,445566,778899";
        nvs.custom = kCustom;
        nvs.theme = BG_THEME_LEGACY_CUSTOM;
        TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::Done), static_cast<int>(boot(nvs)));
        TEST_ASSERT_EQUAL(1, libraryEntryCount(nvs.library));
        TEST_ASSERT_EQUAL_STRING("c1", nvs.globalRef.c_str());
        TEST_ASSERT_EQUAL_STRING("", nvs.custom.c_str());
    }
    // Reboot with the library and the ref durable but the legacy pair not:
    // the ref already points at the copy, so the clear can finish.
    {
        FakeNvs nvs;
        nvs.library = "1|Custom|112233,445566,778899";
        nvs.globalRef = "c1";
        nvs.custom = kCustom;
        nvs.theme = BG_THEME_LEGACY_CUSTOM;
        const std::string before = effective(nvs);
        TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::Done), static_cast<int>(boot(nvs)));
        TEST_ASSERT_EQUAL(1, libraryEntryCount(nvs.library));
        TEST_ASSERT_EQUAL_STRING("c1", nvs.globalRef.c_str());
        TEST_ASSERT_EQUAL_STRING("", nvs.custom.c_str());
        TEST_ASSERT_EQUAL(0, nvs.theme);
        TEST_ASSERT_EQUAL_STRING(before.c_str(), effective(nvs).c_str());
    }
    // Reboot with the theme written but the string clear lost, which is the
    // one order doSave can produce inside the last step.
    {
        FakeNvs nvs;
        nvs.library = "1|Custom|112233,445566,778899";
        nvs.globalRef = "c1";
        nvs.custom = kCustom;
        nvs.theme = 0;
        const std::string before = effective(nvs);
        TEST_ASSERT_EQUAL(static_cast<int>(BgMigrateResult::Done), static_cast<int>(boot(nvs)));
        TEST_ASSERT_EQUAL_STRING("", nvs.custom.c_str());
        TEST_ASSERT_EQUAL(1, libraryEntryCount(nvs.library));
        TEST_ASSERT_EQUAL_STRING(before.c_str(), effective(nvs).c_str());
    }
}

static void test_migrate_survives_a_failed_library_write() {
    underBothThemeTables(migrate_survives_a_failed_library_write);
}
static void test_migrate_survives_a_failed_ref_write() { underBothThemeTables(migrate_survives_a_failed_ref_write); }
static void test_migrate_survives_a_clear_that_lies() { underBothThemeTables(migrate_survives_a_clear_that_lies); }
static void test_migrate_reboot_after_each_durable_step() {
    underBothThemeTables(migrate_reboot_after_each_durable_step);
}

// ---------------------------------------------------------------------------
// Group L -- the gradient pick transaction (gm-nov3.17)
// ---------------------------------------------------------------------------
//
// What this checks, and what it cannot. gradientPickTransaction is the
// production template the display's gradient picker runs a tap through
// (CatGradientPicker.cpp's applyPick is its only call site). The template is
// the code under test here; the Ops below are a fake one, with a modelled
// lock and a scripted web writer that behaves the way the device's does: it
// blocks while the lock is held and its write lands at the release.
//
// So these cases prove that the template keeps the resolve, the target check
// and the assignment inside one lock scope, and that no library change can
// become visible between the resolve and the assignment. If someone splits
// the transaction again, the writer's deletion lands in the gap and
// test_gradient_pick_excludes_a_deletion_during_the_transaction fails.
//
// They do not prove that the device's Settings::Guard is a real mutex: that
// is Settings.cpp (xSemaphoreTakeRecursive, untouched by this bead), and a
// no-op under GAGGIMATE_SIM, which is why the simulator cannot show any of
// this.

namespace pickfake {

// The stored library, plus the lock that orders whole transactions against
// the web save. `revision` moves on every write, which is how a test tells
// that the data under a transaction did not change while it ran.
struct World {
    std::vector<std::string> refs;
    int revision = 0;
    int lockDepth = 0;

    // A web deletion waiting on the lock, the way a batchUpdate on the async
    // task waits on Settings::lock().
    bool pending = false;
    std::string pendingRef;
    int blocked = 0; // times a write had to wait for the lock

    bool has(const std::string &ref) const {
        for (const std::string &r : refs) {
            if (r == ref) {
                return true;
            }
        }
        return false;
    }

    // What the web task does. Outside the lock it lands at once; inside, it
    // waits and is applied when the last holder releases.
    void webDelete(const std::string &ref) {
        if (lockDepth > 0) {
            pending = true;
            pendingRef = ref;
            blocked++;
            return;
        }
        erase(ref);
    }

    void erase(const std::string &ref) {
        for (size_t i = 0; i < refs.size(); i++) {
            if (refs[i] == ref) {
                refs.erase(refs.begin() + static_cast<long>(i));
                revision++;
                return;
            }
        }
    }

    void release() {
        if (pending) {
            pending = false;
            erase(pendingRef);
        }
    }
};

struct Guard {
    World &world;
    explicit Guard(World &w) : world(w) { world.lockDepth++; }
    ~Guard() {
        if (--world.lockDepth == 0) {
            world.release();
        }
    }
    Guard(const Guard &) = delete;
    Guard &operator=(const Guard &) = delete;
};

// The fake picker. `duringResolve` is the concurrent web save: it runs at the
// moment the transaction has finished resolving the ref, which is the exact
// window this bead is about.
struct Ops {
    using Guard = pickfake::Guard;

    World &world;
    bool target = true;
    std::string duringResolve; // a ref the web task tries to delete, or ""

    int resolves = 0;
    int assigns = 0;
    std::string assignedRef;
    int assignedAtRevision = -1;
    int assignedAtDepth = 0;
    bool assignedRefWasPresent = false;

    explicit Ops(World &w) : world(w) {}

    World &guarded() { return world; }

    bool refResolves(const char *ref) {
        resolves++;
        const bool ok = world.has(ref);
        if (!duringResolve.empty()) {
            world.webDelete(duringResolve);
        }
        return ok;
    }

    bool targetValid() { return target; }

    void assign(const char *ref) {
        assigns++;
        assignedRef = ref;
        assignedAtRevision = world.revision;
        assignedAtDepth = world.lockDepth;
        assignedRefWasPresent = world.has(ref);
    }
};

World twoEntries() {
    World w;
    w.refs.push_back("c1");
    w.refs.push_back("c2");
    return w;
}

} // namespace pickfake

// The ordinary tap: resolved and assigned inside one lock scope, with the
// stored data unchanged in between.
static void test_gradient_pick_assigns_inside_one_lock() {
    pickfake::World world = pickfake::twoEntries();
    pickfake::Ops ops(world);
    const int before = world.revision;
    TEST_ASSERT_EQUAL(static_cast<int>(settingsui::GradientPickOutcome::Assigned),
                      static_cast<int>(settingsui::gradientPickTransaction(ops, "c2")));
    TEST_ASSERT_EQUAL(1, ops.assigns);
    TEST_ASSERT_EQUAL_STRING("c2", ops.assignedRef.c_str());
    TEST_ASSERT_TRUE(ops.assignedAtDepth > 0);         // the lock was held at the write
    TEST_ASSERT_EQUAL(before, ops.assignedAtRevision); // nothing landed in between
    TEST_ASSERT_TRUE(ops.assignedRefWasPresent);
    TEST_ASSERT_EQUAL(0, world.lockDepth); // and released on the way out
}

// The entry was already gone when the finger landed: the tap is refused and
// nothing is written, so nothing is marked touched either.
static void test_gradient_pick_refuses_a_ref_deleted_before_the_tap() {
    pickfake::World world = pickfake::twoEntries();
    world.webDelete("c2");
    pickfake::Ops ops(world);
    TEST_ASSERT_EQUAL(static_cast<int>(settingsui::GradientPickOutcome::RefGone),
                      static_cast<int>(settingsui::gradientPickTransaction(ops, "c2")));
    TEST_ASSERT_EQUAL(0, ops.assigns);
    TEST_ASSERT_EQUAL(0, world.lockDepth);
}

// The regression this bead is about. The web task tries to delete the very
// entry the tap has just resolved. It cannot land while the transaction
// holds the lock, so the assignment still sees the library the resolve
// approved; the deletion takes effect afterwards. A transaction that
// released the lock after resolving would assign a ref that had already
// stopped naming anything.
static void test_gradient_pick_excludes_a_deletion_during_the_transaction() {
    pickfake::World world = pickfake::twoEntries();
    pickfake::Ops ops(world);
    ops.duringResolve = "c2";
    TEST_ASSERT_EQUAL(static_cast<int>(settingsui::GradientPickOutcome::Assigned),
                      static_cast<int>(settingsui::gradientPickTransaction(ops, "c2")));
    TEST_ASSERT_EQUAL(1, world.blocked); // the writer did try
    TEST_ASSERT_EQUAL(1, ops.assigns);
    TEST_ASSERT_TRUE(ops.assignedRefWasPresent);  // validated against the library in force
    TEST_ASSERT_EQUAL(0, ops.assignedAtRevision); // which had not moved
    TEST_ASSERT_FALSE(world.has("c2"));           // and the deletion landed at the release
    TEST_ASSERT_EQUAL(1, world.revision);
}

// A deletion of some other entry during the transaction is excluded the same
// way: one transaction, not one field.
static void test_gradient_pick_excludes_an_unrelated_deletion_too() {
    pickfake::World world = pickfake::twoEntries();
    pickfake::Ops ops(world);
    ops.duringResolve = "c1";
    TEST_ASSERT_EQUAL(static_cast<int>(settingsui::GradientPickOutcome::Assigned),
                      static_cast<int>(settingsui::gradientPickTransaction(ops, "c2")));
    TEST_ASSERT_EQUAL(0, ops.assignedAtRevision);
    TEST_ASSERT_TRUE(ops.assignedRefWasPresent);
    TEST_ASSERT_FALSE(world.has("c1"));
    TEST_ASSERT_TRUE(world.has("c2"));
}

// The slot the picker was opened for is gone (an animation id that has left
// the roster). The ref resolved, but nothing is written.
static void test_gradient_pick_writes_nothing_when_the_target_is_gone() {
    pickfake::World world = pickfake::twoEntries();
    pickfake::Ops ops(world);
    ops.target = false;
    TEST_ASSERT_EQUAL(static_cast<int>(settingsui::GradientPickOutcome::TargetGone),
                      static_cast<int>(settingsui::gradientPickTransaction(ops, "c2")));
    TEST_ASSERT_EQUAL(1, ops.resolves);
    TEST_ASSERT_EQUAL(0, ops.assigns);
    TEST_ASSERT_EQUAL(0, world.lockDepth);
}

// The Global entry names no gradient, so there is nothing to resolve. It is
// still assigned under the lock, because the writes behind it (the global ref
// and its legacy mirror) have to agree with each other.
static void test_gradient_pick_global_skips_resolution_but_keeps_the_lock() {
    pickfake::World world; // empty library: nothing would resolve
    pickfake::Ops ops(world);
    TEST_ASSERT_EQUAL(static_cast<int>(settingsui::GradientPickOutcome::Assigned),
                      static_cast<int>(settingsui::gradientPickTransaction(ops, "")));
    TEST_ASSERT_EQUAL(0, ops.resolves);
    TEST_ASSERT_EQUAL(1, ops.assigns);
    TEST_ASSERT_EQUAL_STRING("", ops.assignedRef.c_str());
    TEST_ASSERT_TRUE(ops.assignedAtDepth > 0);
}

// A null ref is the same case as Global, and reaches assign() as "" rather
// than as a null the callee would have to defend against.
static void test_gradient_pick_null_ref_is_global() {
    pickfake::World world = pickfake::twoEntries();
    pickfake::Ops ops(world);
    TEST_ASSERT_EQUAL(static_cast<int>(settingsui::GradientPickOutcome::Assigned),
                      static_cast<int>(settingsui::gradientPickTransaction(ops, nullptr)));
    TEST_ASSERT_EQUAL(0, ops.resolves);
    TEST_ASSERT_EQUAL_STRING("", ops.assignedRef.c_str());
}

// ---------------------------------------------------------------------------
// Unity entrypoint
// ---------------------------------------------------------------------------

void setUp(void) { /* no framework-level setup needed */ }
// Group D, I and J swap a fixture table under the bg_theme_* accessors; put
// the shipped one back whatever the case did, including aborting on a failed
// assertion.
void tearDown(void) { useShippedTable(); }

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_clamp_both_ends);
    RUN_TEST(test_wrap_hour_and_minute);
    RUN_TEST(test_fast_step);
    RUN_TEST(test_standby_timeout_edge);
    RUN_TEST(test_dim_after_off_grid_snap);

    RUN_TEST(test_format_integer_and_unit);
    RUN_TEST(test_format_one_decimal);
    RUN_TEST(test_fade_spec);
    RUN_TEST(test_format_minutes_seconds);
    RUN_TEST(test_format_standby_minutes);

    RUN_TEST(test_wrap_index_choice_lists);
    RUN_TEST(test_wrap_animation_names_stub);

    RUN_TEST(test_gradient_choices_real_themes);
    RUN_TEST(test_theme_provider_device_path_matches_table);
    RUN_TEST(test_gradient_choices_carry_categories);
    RUN_TEST(test_gradient_choices_without_category_accessor);
    RUN_TEST(test_theme_category_order_is_declared_not_encountered);
    RUN_TEST(test_gradient_groups_follow_declared_order_and_drop_empties);
    RUN_TEST(test_gradient_groups_keep_every_builtin_reachable);
    RUN_TEST(test_gradient_id_boundary_ref_and_library);
    RUN_TEST(test_gradient_map_read_write_empty);
    RUN_TEST(test_gradient_map_read_write_shorter_than_n);
    RUN_TEST(test_gradient_map_write_trims_trailing_empty_slots);
    RUN_TEST(test_gradient_map_write_preserves_neighbours);

    RUN_TEST(test_palette_on_palette_lookup);
    RUN_TEST(test_palette_off_palette_lookup);

    RUN_TEST(test_zone_region_counts_and_etc_last);
    RUN_TEST(test_zone_city_label_strips_region_and_underscores);
    RUN_TEST(test_zone_locate_unknown_falls_back);
    RUN_TEST(test_zone_round_trip_all_461);

    RUN_TEST(test_schedule_add_remove_floor_and_can_add);
    RUN_TEST(test_schedule_import_9_entries_kept_whole);
    RUN_TEST(test_schedule_toggle_day_and_step_time);
    RUN_TEST(test_schedule_time_parts_reads_malformed_as_midnight);
    RUN_TEST(test_schedule_summary_strings);
    RUN_TEST(test_schedule_serialize_parse_round_trip_and_malformed_dropped);

    RUN_TEST(test_bgparams_read_defaults_when_unset);
    RUN_TEST(test_bgparams_read_overrides_and_short_group);
    RUN_TEST(test_bgparams_read_clamps_and_stops_at_garbage);
    RUN_TEST(test_bgparams_write_group_repacks_at_eight);
    RUN_TEST(test_bgparams_write_group_leaves_neighbours_alone);
    RUN_TEST(test_bgparams_write_slot_fills_the_rest_from_defaults);
    RUN_TEST(test_bgparams_clear_group_restores_defaults);
    RUN_TEST(test_bgparams_spec_steps_and_clamps);

    RUN_TEST(test_legacy_integers_keep_their_meaning);
    RUN_TEST(test_explicit_builtin_refs_resolve_directly);
    RUN_TEST(test_upgrade_rollback_upgrade_keeps_the_same_gradient);
    RUN_TEST(test_legacy_mirror_policy);
    RUN_TEST(test_legacy_builtin_reading);

    RUN_TEST(test_migrate_active_custom_gains_entry_and_ref);
    RUN_TEST(test_migrate_positioned_custom_keeps_uniform_rendering);
    RUN_TEST(test_migrate_already_migrated_device_is_not_reactivated);
    RUN_TEST(test_migrate_preserves_newer_selections);
    RUN_TEST(test_migrate_destination_choice);
    RUN_TEST(test_migrate_never_reuses_an_ambiguous_duplicate_id);
    RUN_TEST(test_migrate_allocates_past_the_first_thirteen_ids);
    RUN_TEST(test_migrate_defers_rather_than_evicting);

    RUN_TEST(test_migrate_survives_a_failed_library_write);
    RUN_TEST(test_migrate_survives_a_failed_ref_write);
    RUN_TEST(test_migrate_survives_a_clear_that_lies);
    RUN_TEST(test_migrate_reboot_after_each_durable_step);

    RUN_TEST(test_gradient_pick_assigns_inside_one_lock);
    RUN_TEST(test_gradient_pick_refuses_a_ref_deleted_before_the_tap);
    RUN_TEST(test_gradient_pick_excludes_a_deletion_during_the_transaction);
    RUN_TEST(test_gradient_pick_excludes_an_unrelated_deletion_too);
    RUN_TEST(test_gradient_pick_writes_nothing_when_the_target_is_gone);
    RUN_TEST(test_gradient_pick_global_skips_resolution_but_keeps_the_lock);
    RUN_TEST(test_gradient_pick_null_ref_is_global);
    return UNITY_END();
}
