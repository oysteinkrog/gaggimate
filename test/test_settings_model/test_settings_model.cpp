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
//
// Groups:
//   A -- numeric spec: clamp, wrap, off-grid snap, fast step
//   B -- formatters
//   C -- index-based choice lists (wrap at both ends)
//   D -- gradients: choice list, map read/write
//   E -- palette
//   F -- time zones (real 461-entry table)
//   G -- wake-up schedules

#include <unity.h>

#include <cstddef>
#include <string>
#include <vector>

#include "display/core/zones.cpp"
#include "display/ui/default/bganim/BgAnimThemes.cpp"
#include "display/ui/default/settings/SettingsModel.cpp"

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
// Group D -- gradients: choice list, map read/write
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

// The web handler stores any time string the browser sends, so a stored
// entry's time can be empty or otherwise malformed; the editor rows read it
// through scheduleTimeParts, which must answer 00:00 for those rather than
// let the caller slice the string (substr past the end throws, and the
// firmware is built without exceptions).
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
// Unity entrypoint
// ---------------------------------------------------------------------------

void setUp(void) { /* no framework-level setup needed */ }
void tearDown(void) { /* no framework-level teardown needed */ }

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
    return UNITY_END();
}
