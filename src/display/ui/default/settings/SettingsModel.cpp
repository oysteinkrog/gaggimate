#include "SettingsModel.h"

#include <cctype>
#include <cstdio>

#include <display/core/constants.h>

namespace settingsui {

// ---- numeric fields --------------------------------------------------------

const NumericSpec kTemperatureOffsetSpec{-20, 20, 1, 5, ClampMode::Clamp, NumericFormat::Integer, " C"};
const NumericSpec kPressureScalingSpec{10, 300, 1, 10, ClampMode::Clamp, NumericFormat::OneDecimal, " bar"};
const NumericSpec kBrewDelaySpec{0, 4000, 50, 250, ClampMode::Clamp, NumericFormat::Integer, " ms"};
const NumericSpec kGrindDelaySpec{0, 4000, 50, 250, ClampMode::Clamp, NumericFormat::Integer, " ms"};
const NumericSpec kMainBrightnessSpec{1, 16, 1, 4, ClampMode::Clamp, NumericFormat::Integer, ""};
const NumericSpec kStandbyBrightnessSpec{0, 16, 1, 4, ClampMode::Clamp, NumericFormat::Integer, ""};
const NumericSpec kStandbyBrightnessTimeoutSpec{30000,  1800000, 30000, 300000,
                                                 ClampMode::Clamp, NumericFormat::MinutesSeconds, ""};
const NumericSpec kStandbyTimeoutSpec{0, 14400000, 60000, 600000, ClampMode::Clamp, NumericFormat::StandbyMinutes, ""};
const NumericSpec kBgAnimFpsSpec{5, 60, 5, 10, ClampMode::Clamp, NumericFormat::Integer, " fps"};
const NumericSpec kBgAnimPlateOpacitySpec{0, 100, 5, 20, ClampMode::Clamp, NumericFormat::Integer, " %"};
const NumericSpec kBgAnimScrimSpec{0, 100, 5, 20, ClampMode::Clamp, NumericFormat::Integer, " %"};
const NumericSpec kBgFadeSpec{0, 1000, 20, 100, ClampMode::Clamp, NumericFormat::Integer, " ms"};
const NumericSpec kScheduleHourSpec{0, 23, 1, 5, ClampMode::Wrap, NumericFormat::Integer, ""};
const NumericSpec kScheduleMinuteSpec{0, 59, 1, 5, ClampMode::Wrap, NumericFormat::Integer, ""};

long clampOrWrap(long value, const NumericSpec &spec) {
    if (spec.clampMode == ClampMode::Wrap) {
        const long span = spec.maxValue - spec.minValue + spec.step;
        long rel = (value - spec.minValue) % span;
        if (rel < 0) {
            rel += span;
        }
        return spec.minValue + rel;
    }
    if (value < spec.minValue) {
        return spec.minValue;
    }
    if (value > spec.maxValue) {
        return spec.maxValue;
    }
    return value;
}

long stepValue(long current, int direction, bool fast, const NumericSpec &spec) {
    // Values handed in from outside the model (an imported schedule field, a
    // stored value from a build with a narrower range) may already be off
    // range; pin to range first so the grid arithmetic below never sees a
    // negative `rel`.
    if (current < spec.minValue) {
        current = spec.minValue;
    } else if (current > spec.maxValue) {
        current = spec.maxValue;
    }
    const long delta = fast ? spec.fastStep : spec.step;
    const long dir = direction < 0 ? -1 : 1;
    const long rel = current - spec.minValue;
    long next;
    if (spec.step > 0 && rel % spec.step != 0) {
        // Off the step grid: the first press lands on the nearest grid line
        // in the direction pressed, rather than adding a full step/fastStep
        // past it.
        const long aligned = dir > 0 ? ((rel / spec.step) + 1) * spec.step : (rel / spec.step) * spec.step;
        next = spec.minValue + aligned;
    } else {
        next = current + dir * delta;
    }
    return clampOrWrap(next, spec);
}

std::string formatNumeric(long value, const NumericSpec &spec) {
    char buf[32];
    switch (spec.format) {
    case NumericFormat::OneDecimal: {
        const long whole = value / 10;
        long frac = value % 10;
        if (frac < 0) {
            frac = -frac;
        }
        std::snprintf(buf, sizeof(buf), "%ld.%ld%s", whole, frac, spec.unit);
        return buf;
    }
    case NumericFormat::MinutesSeconds: {
        const long totalSeconds = value / 1000;
        std::snprintf(buf, sizeof(buf), "%ld:%02ld", totalSeconds / 60, totalSeconds % 60);
        return buf;
    }
    case NumericFormat::StandbyMinutes:
        if (value == 0) {
            return "Never";
        }
        std::snprintf(buf, sizeof(buf), "%ld min", value / 60000);
        return buf;
    case NumericFormat::Integer:
    default:
        std::snprintf(buf, sizeof(buf), "%ld%s", value, spec.unit);
        return buf;
    }
}

// ---- index-based choice lists ----------------------------------------------

int wrapIndex(int index, int count, int direction) {
    if (count <= 0) {
        return 0;
    }
    int n = (index + (direction < 0 ? -1 : 1)) % count;
    if (n < 0) {
        n += count;
    }
    return n;
}

const char *const kStartupModeLabels[2] = {"Standby", "Brew"};
const char *const kThemeModeLabels[2] = {"Dark", "Light"};
const char *const kPlatesLabels[3] = {"Keep", "Hide", "Custom"};
const char *const kFadeCurveLabels[2] = {"Linear", "Smooth"};

int startupModeIndexForValue(int value) { return value == MODE_BREW ? 1 : 0; }
int startupModeValueForIndex(int index) { return index == 1 ? MODE_BREW : MODE_STANDBY; }

// ---- animation names --------------------------------------------------------

int animationCount(const AnimationNameProvider &provider) { return provider.count ? provider.count() : 0; }

const char *animationLabel(const AnimationNameProvider &provider, int index) {
    const int n = animationCount(provider);
    if (n <= 0 || !provider.name) {
        return "";
    }
    if (index < 0) {
        index = 0;
    } else if (index >= n) {
        index = n - 1;
    }
    const char *label = provider.name(index);
    return label ? label : "";
}

// ---- gradients ----------------------------------------------------------------

namespace {

struct LibraryEntry {
    int id;
    std::string name;
};

// Lenient reader for the "id|name|gradient;..." library string: pulls out
// (id, name) pairs for the choice list and drops anything that does not
// parse, rather than reproducing bg_library_valid's stricter grammar (that
// validation runs where the string is written, not here).
std::vector<LibraryEntry> parseGradientLibrary(const std::string &library) {
    std::vector<LibraryEntry> out;
    size_t pos = 0;
    while (pos <= library.size()) {
        const size_t semi = library.find(';', pos);
        const std::string entry = (semi == std::string::npos) ? library.substr(pos) : library.substr(pos, semi - pos);
        if (!entry.empty()) {
            const size_t p1 = entry.find('|');
            const size_t p2 = p1 == std::string::npos ? std::string::npos : entry.find('|', p1 + 1);
            if (p1 != std::string::npos && p2 != std::string::npos && p1 > 0) {
                bool idOk = true;
                int id = 0;
                for (size_t k = 0; k < p1; k++) {
                    if (!std::isdigit(static_cast<unsigned char>(entry[k]))) {
                        idOk = false;
                        break;
                    }
                    id = id * 10 + (entry[k] - '0');
                }
                if (idOk && id > 0) {
                    out.push_back({id, entry.substr(p1 + 1, p2 - p1 - 1)});
                }
            }
        }
        if (semi == std::string::npos) {
            break;
        }
        pos = semi + 1;
    }
    return out;
}

// Splits a ";"-separated map/schedule-less string into its slots, an empty
// string included as one empty slot rather than zero slots (so writing slot
// 0 of an empty map produces one entry, not a leading separator).
std::vector<std::string> splitEntries(const std::string &packed) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        const size_t semi = packed.find(';', start);
        if (semi == std::string::npos) {
            parts.push_back(packed.substr(start));
            break;
        }
        parts.push_back(packed.substr(start, semi - start));
        start = semi + 1;
    }
    return parts;
}

} // namespace

std::vector<GradientChoice> gradientChoices(const ThemeNameProvider &themes, const std::string &library) {
    std::vector<GradientChoice> out;
    out.push_back({"Default", ""});
    const int themeCount = themes.count ? themes.count() : 0;
    for (int t = 0; t < themeCount; t++) {
        const char *name = themes.name ? themes.name(t) : nullptr;
        out.push_back({name ? name : "", std::to_string(t)});
    }
    for (const LibraryEntry &entry : parseGradientLibrary(library)) {
        out.push_back({entry.name, "c" + std::to_string(entry.id)});
    }
    return out;
}

int gradientChoiceIndexForRef(const std::vector<GradientChoice> &choices, const std::string &ref) {
    for (size_t i = 0; i < choices.size(); i++) {
        if (choices[i].ref == ref) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

std::string gradientMapReadRef(const std::string &map, int animId) {
    if (animId < 0) {
        return "";
    }
    const std::vector<std::string> parts = splitEntries(map);
    if (static_cast<size_t>(animId) >= parts.size()) {
        return "";
    }
    return parts[static_cast<size_t>(animId)];
}

std::string gradientMapWriteRef(const std::string &map, int animId, const std::string &newRef) {
    if (animId < 0) {
        return map;
    }
    std::vector<std::string> parts = splitEntries(map);
    while (static_cast<int>(parts.size()) <= animId) {
        parts.push_back("");
    }
    parts[static_cast<size_t>(animId)] = newRef;
    // Trailing empty slots carry no information and would turn an all-default
    // map back into ";" (or ";;") after a ref was set and cleared again; the
    // web UI stores the same state as "". Trim them so the two writers agree.
    while (!parts.empty() && parts.back().empty()) {
        parts.pop_back();
    }
    std::string out;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i != 0) {
            out += ';';
        }
        out += parts[i];
    }
    return out;
}

// ---- palette ------------------------------------------------------------------

const PaletteEntry kPalette[kPaletteCount] = {
    {"White", 0xFFFFFF},     {"Warm white", 0xFFE0B3}, {"Amber", 0xFFBF00}, {"Orange", 0xFF8000},
    {"Red", 0xFF0000},       {"Pink", 0xFF80C0},       {"Purple", 0x8000FF}, {"Blue", 0x0000FF},
    {"Cyan", 0x00FFFF},      {"Teal", 0x008080},       {"Green", 0x00FF00}, {"Black", 0x000000},
};

int paletteIndexOf(int color) {
    for (int i = 0; i < kPaletteCount; i++) {
        if (kPalette[i].color == color) {
            return i;
        }
    }
    return -1;
}

int paletteCurrentIndex(int currentColor) {
    const int idx = paletteIndexOf(currentColor);
    return idx >= 0 ? idx : kPaletteCount;
}

int paletteChoiceCount(int currentColor) { return paletteIndexOf(currentColor) >= 0 ? kPaletteCount : kPaletteCount + 1; }

std::string paletteChoiceLabel(int index, int currentColor) {
    if (index >= 0 && index < kPaletteCount) {
        return kPalette[index].name;
    }
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%06X", static_cast<unsigned>(currentColor) & 0xFFFFFFu);
    return buf;
}

int paletteChoiceColor(int index, int currentColor) {
    if (index >= 0 && index < kPaletteCount) {
        return kPalette[index].color;
    }
    return currentColor;
}

// ---- time zones -----------------------------------------------------------------

namespace {

struct RegionSpan {
    std::string name;
    size_t start;
    size_t count;
};

std::string regionOf(const std::string &fullName) {
    const size_t slash = fullName.find('/');
    return slash == std::string::npos ? fullName : fullName.substr(0, slash);
}

// Groups the provider's entries into contiguous same-region runs, then
// moves "Etc" to the end. Holds for the real table (regions are contiguous
// blocks in table order, per zones.cpp) and for any stub that keeps the
// same shape.
std::vector<RegionSpan> buildRegions(const ZoneProvider &zones) {
    std::vector<RegionSpan> spans;
    const size_t n = zones.count ? zones.count() : 0;
    for (size_t i = 0; i < n; i++) {
        const char *full = zones.name ? zones.name(i) : nullptr;
        const std::string region = regionOf(full ? full : "");
        if (spans.empty() || spans.back().name != region) {
            spans.push_back({region, i, 1});
        } else {
            spans.back().count++;
        }
    }
    for (size_t i = 0; i < spans.size(); i++) {
        if (spans[i].name == "Etc") {
            RegionSpan etc = spans[i];
            spans.erase(spans.begin() + static_cast<long>(i));
            spans.push_back(etc);
            break;
        }
    }
    return spans;
}

} // namespace

int regionCount(const ZoneProvider &zones) { return static_cast<int>(buildRegions(zones).size()); }

std::string regionName(const ZoneProvider &zones, int region) {
    const std::vector<RegionSpan> spans = buildRegions(zones);
    if (region < 0 || static_cast<size_t>(region) >= spans.size()) {
        return "";
    }
    return spans[static_cast<size_t>(region)].name;
}

int cityCount(const ZoneProvider &zones, int region) {
    const std::vector<RegionSpan> spans = buildRegions(zones);
    if (region < 0 || static_cast<size_t>(region) >= spans.size()) {
        return 0;
    }
    return static_cast<int>(spans[static_cast<size_t>(region)].count);
}

std::string zoneName(const ZoneProvider &zones, int region, int city) {
    const std::vector<RegionSpan> spans = buildRegions(zones);
    if (region < 0 || static_cast<size_t>(region) >= spans.size()) {
        return "";
    }
    const RegionSpan &s = spans[static_cast<size_t>(region)];
    if (city < 0 || static_cast<size_t>(city) >= s.count) {
        return "";
    }
    const char *full = zones.name(s.start + static_cast<size_t>(city));
    return full ? full : "";
}

std::string cityLabel(const ZoneProvider &zones, int region, int city) {
    const std::string full = zoneName(zones, region, city);
    if (full.empty()) {
        return full;
    }
    const size_t slash = full.find('/');
    std::string label = slash == std::string::npos ? full : full.substr(slash + 1);
    for (char &c : label) {
        if (c == '_') {
            c = ' ';
        }
    }
    return label;
}

bool locate(const ZoneProvider &zones, const std::string &name, int &region, int &city) {
    const std::vector<RegionSpan> spans = buildRegions(zones);
    for (size_t r = 0; r < spans.size(); r++) {
        for (size_t c = 0; c < spans[r].count; c++) {
            const char *full = zones.name(spans[r].start + c);
            if (full != nullptr && name == full) {
                region = static_cast<int>(r);
                city = static_cast<int>(c);
                return true;
            }
        }
    }
    region = 0;
    city = 0;
    return false;
}

// ---- wake-up schedules ------------------------------------------------------------

ScheduleDraft scheduleDefault() { return ScheduleDraft{}; }

bool scheduleCanAdd(const std::vector<ScheduleDraft> &schedules) { return schedules.size() < 8; }

bool scheduleAdd(std::vector<ScheduleDraft> &schedules) {
    if (!scheduleCanAdd(schedules)) {
        return false;
    }
    schedules.push_back(scheduleDefault());
    return true;
}

bool scheduleRemove(std::vector<ScheduleDraft> &schedules, size_t index) {
    if (schedules.size() <= 1 || index >= schedules.size()) {
        return false;
    }
    schedules.erase(schedules.begin() + static_cast<long>(index));
    return true;
}

namespace {

// "HH:MM" -> (hour, minute); malformed input reads as 00:00 so a caller that
// skips validation still gets a well-formed schedule rather than garbage. An
// hour past 23 or a minute past 59 is malformed too (gm-bzu.61): a value
// stored before the web handler checked it must not reach the steppers,
// whose ranges assume it is in range.
void parseTime(const std::string &time, int &hour, int &minute) {
    hour = 0;
    minute = 0;
    if (time.size() >= 5 && std::isdigit(static_cast<unsigned char>(time[0])) &&
        std::isdigit(static_cast<unsigned char>(time[1])) && time[2] == ':' &&
        std::isdigit(static_cast<unsigned char>(time[3])) && std::isdigit(static_cast<unsigned char>(time[4]))) {
        const int h = (time[0] - '0') * 10 + (time[1] - '0');
        const int m = (time[3] - '0') * 10 + (time[4] - '0');
        if (h < 24 && m < 60) {
            hour = h;
            minute = m;
        }
    }
}

std::string formatTime(int hour, int minute) {
    char buf[6];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", static_cast<int>(hour), static_cast<int>(minute));
    return buf;
}

} // namespace

void scheduleTimeParts(const ScheduleDraft &schedule, int &hour, int &minute) { parseTime(schedule.time, hour, minute); }

void scheduleStepHour(ScheduleDraft &schedule, int direction, bool fast) {
    int hour;
    int minute;
    parseTime(schedule.time, hour, minute);
    hour = static_cast<int>(stepValue(hour, direction, fast, kScheduleHourSpec));
    schedule.time = formatTime(hour, minute);
}

void scheduleStepMinute(ScheduleDraft &schedule, int direction, bool fast) {
    int hour;
    int minute;
    parseTime(schedule.time, hour, minute);
    minute = static_cast<int>(stepValue(minute, direction, fast, kScheduleMinuteSpec));
    schedule.time = formatTime(hour, minute);
}

void scheduleToggleDay(ScheduleDraft &schedule, int dayOfWeek) {
    if (dayOfWeek >= 0 && dayOfWeek < 7) {
        schedule.days[dayOfWeek] = !schedule.days[dayOfWeek];
    }
}

std::string scheduleDaysSummary(const ScheduleDraft &schedule) {
    static constexpr const char *kAbbrev[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    bool allOn = true;
    bool allOff = true;
    bool weekdaysOn = true;
    bool weekdaysOff = true;
    bool weekendOn = true;
    bool weekendOff = true;
    for (int i = 0; i < 7; i++) {
        const bool d = schedule.days[i];
        allOn = allOn && d;
        allOff = allOff && !d;
        if (i < 5) {
            weekdaysOn = weekdaysOn && d;
            weekdaysOff = weekdaysOff && !d;
        } else {
            weekendOn = weekendOn && d;
            weekendOff = weekendOff && !d;
        }
    }
    if (allOn) {
        return "Every day";
    }
    if (allOff) {
        return "Never";
    }
    if (weekdaysOn && weekendOff) {
        return "Weekdays";
    }
    if (weekendOn && weekdaysOff) {
        return "Weekends";
    }
    std::string out;
    for (int i = 0; i < 7; i++) {
        if (schedule.days[i]) {
            if (!out.empty()) {
                out += ' ';
            }
            out += kAbbrev[i];
        }
    }
    return out;
}

std::string scheduleSerialize(const std::vector<ScheduleDraft> &schedules) {
    std::string out;
    for (size_t i = 0; i < schedules.size(); i++) {
        if (i != 0) {
            out += ';';
        }
        out += schedules[i].time;
        out += '|';
        for (int d = 0; d < 7; d++) {
            out += schedules[i].days[d] ? '1' : '0';
        }
    }
    return out;
}

namespace {

// One "HH:MM|ddddddd" entry (13 characters); false leaves `out` untouched so
// the caller can drop it and keep the rest of the list.
bool parseScheduleEntry(const std::string &entry, ScheduleDraft &out) {
    if (entry.size() != 13 || entry[2] != ':' || entry[5] != '|') {
        return false;
    }
    for (const size_t k : {0u, 1u, 3u, 4u}) {
        if (!std::isdigit(static_cast<unsigned char>(entry[k]))) {
            return false;
        }
    }
    const int hour = (entry[0] - '0') * 10 + (entry[1] - '0');
    const int minute = (entry[3] - '0') * 10 + (entry[4] - '0');
    if (hour > 23 || minute > 59) {
        return false;
    }
    bool days[7];
    for (int d = 0; d < 7; d++) {
        const char c = entry[6 + d];
        if (c != '0' && c != '1') {
            return false;
        }
        days[d] = c == '1';
    }
    out.time = entry.substr(0, 5);
    for (int d = 0; d < 7; d++) {
        out.days[d] = days[d];
    }
    return true;
}

} // namespace

std::vector<ScheduleDraft> scheduleParse(const std::string &packed) {
    std::vector<ScheduleDraft> result;
    for (const std::string &entry : splitEntries(packed)) {
        if (entry.empty()) {
            continue;
        }
        ScheduleDraft draft;
        if (parseScheduleEntry(entry, draft)) {
            result.push_back(draft);
        }
    }
    return result;
}

} // namespace settingsui
