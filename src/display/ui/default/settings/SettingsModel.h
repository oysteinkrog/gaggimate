#ifndef GM_SETTINGS_MODEL_H
#define GM_SETTINGS_MODEL_H

// Host-testable value model behind the on-display settings screens: ranges,
// steps, wrap rules, display formats, choice lists, the time zone split and
// wake-up schedule editing. Plain C++17, no LVGL/Arduino/ESP-IDF dependency
// (pio test -e native_settingsui), so every rule here is proved on the host
// before a screen bead wires a row to it.
//
// Two firmware tables this model does not own, the animation roster and the
// built-in gradient themes, are function-pointer tables that pull in the
// animation kernels, so they are reached through the provider structs below.
// A screen bead wires those to the real bg_animation()/bg_theme_name() and
// the test wires them to a stub. The zone table is reached the same way
// through ZoneProvider; unlike the other two it has a cheap, dependency-free
// real implementation (zones_count()/zones_entry(), zones.h/zones.cpp), so
// the test wires ZoneProvider to that for the round-trip check instead of a
// stub.
//
// Three places hold the same lists and ranges and must agree. This file owns
// the display's editing ranges, steps, wrap rules and formats. The web UI
// mirrors (never reads) the roster in web/src/config/bgAnimations.js and the
// zone table in web/src/config/zones.js, and holds the web form's own input
// constraints in web/src/pages/Settings/tabs/*.jsx. Settings itself clamps
// only a few fields on store (Settings.cpp) and takes the rest as given, so
// a value that reaches the display from the web path can be off this file's
// grid or outside its range: read it through clampOrWrap, and clamp any id
// before indexing with it.

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace settingsui {

// ---- numeric fields --------------------------------------------------------

enum class ClampMode { Clamp, Wrap };

enum class NumericFormat {
    Integer,        // "<value><unit>"
    OneDecimal,     // value in tenths -> "<whole>.<tenth><unit>"
    MinutesSeconds, // value in ms -> "M:SS"
    StandbyMinutes, // value in ms; 0 -> "Never", else "<minutes> min"
};

struct NumericSpec {
    long minValue;
    long maxValue;
    long step;     // grid spacing and the short-press increment
    long fastStep; // increment used after 2 s of holding (shared contract)
    ClampMode clampMode;
    NumericFormat format;
    const char *unit; // suffix incl. any leading space, "" when none
};

extern const NumericSpec kTemperatureOffsetSpec;
extern const NumericSpec kPressureScalingSpec; // value in tenths of a bar
extern const NumericSpec kBrewDelaySpec;
extern const NumericSpec kGrindDelaySpec;
extern const NumericSpec kMainBrightnessSpec;
extern const NumericSpec kStandbyBrightnessSpec;
extern const NumericSpec kStandbyBrightnessTimeoutSpec; // "Dim after", ms
extern const NumericSpec kStandbyTimeoutSpec;            // ms, 0 = never
extern const NumericSpec kBgAnimFpsSpec;
extern const NumericSpec kBgAnimPlateOpacitySpec;
extern const NumericSpec kBgAnimScrimSpec;
extern const NumericSpec kScheduleHourSpec;
extern const NumericSpec kScheduleMinuteSpec;

// Clamps or wraps a value that may be off the spec's step grid (an imported
// value, for instance); used standalone and by stepValue()'s result.
long clampOrWrap(long value, const NumericSpec &spec);

// One press (fast after a 2 s hold) in `direction` (+1/-1). A value that is
// not on the spec's step grid moves to the nearest grid line in that
// direction first ("Dim after" imported off-grid, for example); a value
// already on the grid moves by a full step/fastStep, then clamps or wraps.
long stepValue(long current, int direction, bool fast, const NumericSpec &spec);

std::string formatNumeric(long value, const NumericSpec &spec);

// ---- index-based choice lists, wrap at both ends ---------------------------

int wrapIndex(int index, int count, int direction);

extern const char *const kStartupModeLabels[2]; // {"Standby", "Brew"}
int startupModeIndexForValue(int value);        // MODE_STANDBY/MODE_BREW -> 0/1
int startupModeValueForIndex(int index);        // 0/1 -> MODE_STANDBY/MODE_BREW

// Index equals the stored value for both of these; no mapping needed.
extern const char *const kThemeModeLabels[2]; // {"Dark", "Light"}
extern const char *const kPlatesLabels[3];    // {"Keep", "Hide", "Custom"}

// ---- animation names (firmware table, provider-supplied) ------------------

struct AnimationNameProvider {
    std::function<int()> count;
    std::function<const char *(int)> name;
};

int animationCount(const AnimationNameProvider &provider);
const char *animationLabel(const AnimationNameProvider &provider, int index);

// ---- gradients --------------------------------------------------------------

struct ThemeNameProvider {
    std::function<int()> count;
    std::function<const char *(int)> name;
};

// One entry a gradient row can land on: a label to show and the
// bgAnimThemeMap ref that entry writes ("" for Default/global theme, a
// decimal built-in theme index, or "c<id>" for a library entry). Built in
// this fixed order: Default, every built-in theme, every library entry.
struct GradientChoice {
    std::string label;
    std::string ref;
};

std::vector<GradientChoice> gradientChoices(const ThemeNameProvider &themes, const std::string &library);
// The index in `choices` whose ref matches; 0 (Default) when nothing does,
// e.g. a map ref naming a library entry that was since deleted.
int gradientChoiceIndexForRef(const std::vector<GradientChoice> &choices, const std::string &ref);

// Reads/writes one ";"-separated slot of a bgAnimThemeMap string, leaving
// every other slot's text untouched; a map shorter than animId reads as "".
std::string gradientMapReadRef(const std::string &map, int animId);
std::string gradientMapWriteRef(const std::string &map, int animId, const std::string &newRef);

// ---- palette ----------------------------------------------------------------

constexpr int kPaletteCount = 12;

struct PaletteEntry {
    const char *name;
    int color; // 0xRRGGBB
};

extern const PaletteEntry kPalette[kPaletteCount];

int paletteIndexOf(int color); // -1 when color is none of the 12 named entries

// The list a row cycles through: the 12 named entries, plus a 13th "#rrggbb"
// entry named after `currentColor` when it is not one of them, so cycling
// away from an imported off-palette colour never silently jumps to the
// nearest name.
int paletteChoiceCount(int currentColor);
std::string paletteChoiceLabel(int index, int currentColor);
int paletteChoiceColor(int index, int currentColor);
int paletteCurrentIndex(int currentColor); // 0..11, or 12 for the off-palette slot

// ---- time zones (zone table, provider-supplied) ----------------------------

struct ZoneProvider {
    std::function<size_t()> count;
    std::function<const char *(size_t)> name; // full IANA name, e.g. "Europe/Oslo"
};

// Every function below groups the provider's entries into regions by
// scanning for contiguous runs of the same text before the first '/', which
// holds for the real 461-entry table (zones.cpp) read in table order; "Etc"
// is moved to the last region regardless of where its run falls.
int regionCount(const ZoneProvider &zones);
std::string regionName(const ZoneProvider &zones, int region);
int cityCount(const ZoneProvider &zones, int region);
// The part of the IANA name after the region, with '_' shown as ' '
// ("Argentina/Buenos_Aires" -> "Argentina/Buenos Aires").
std::string cityLabel(const ZoneProvider &zones, int region, int city);
std::string zoneName(const ZoneProvider &zones, int region, int city); // full IANA name, unchanged
// Finds (region, city) for a stored IANA name; false (region/city left at 0,
// the resolve_timezone fallback slot) when it matches nothing.
bool locate(const ZoneProvider &zones, const std::string &name, int &region, int &city);

// ---- wake-up schedules ------------------------------------------------------

struct ScheduleDraft {
    std::string time = "07:00";                                             // "HH:MM"
    bool days[7] = {true, true, true, true, true, true, true}; // Mon..Sun
};

ScheduleDraft scheduleDefault();
bool scheduleCanAdd(const std::vector<ScheduleDraft> &schedules); // false at 8 or more
bool scheduleAdd(std::vector<ScheduleDraft> &schedules);          // no-op when !canAdd
bool scheduleRemove(std::vector<ScheduleDraft> &schedules, size_t index); // never below one
// The schedule's "HH:MM" split into its two numbers, reading anything
// malformed (including an empty string) as 00:00, the same rule the two
// step functions below already apply internally. A caller must go through
// this rather than slicing schedule.time: the web handler stores whatever
// string the browser sent (WebUIPlugin.cpp), so "|1111111" is a legal
// stored entry and substr(3, 2) on it throws out_of_range, which on a
// firmware built without exceptions aborts.
void scheduleTimeParts(const ScheduleDraft &schedule, int &hour, int &minute);
void scheduleStepHour(ScheduleDraft &schedule, int direction, bool fast);
void scheduleStepMinute(ScheduleDraft &schedule, int direction, bool fast);
void scheduleToggleDay(ScheduleDraft &schedule, int dayOfWeek); // 0=Mon..6=Sun
std::string scheduleDaysSummary(const ScheduleDraft &schedule);

std::string scheduleSerialize(const std::vector<ScheduleDraft> &schedules);
// Drops a malformed entry and keeps the rest; never caps the count (a
// stored list longer than scheduleCanAdd() would allow is kept whole).
std::vector<ScheduleDraft> scheduleParse(const std::string &packed);

} // namespace settingsui

#endif // GM_SETTINGS_MODEL_H
