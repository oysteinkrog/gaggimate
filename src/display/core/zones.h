#ifndef ZONES_H
#define ZONES_H

#include <cstddef>
#include <cstring>

// One IANA zone name paired with its POSIX TZ string. The table itself is a
// single definition in zones.cpp; everything else reaches it through the
// two accessors below, so this header stays free of Arduino/ESP-IDF and the
// host-tested settings model (SettingsModel.cpp, via ZoneProvider) can read
// it without pulling in a device build.
typedef struct {
    const char *name;
    const char *zones;
} zones_t;

size_t zones_count();
// Clamps an out-of-range index to 0, like bg_animation() does for animation
// ids.
const zones_t &zones_entry(size_t i);

// Looks up the POSIX TZ string for an IANA name ("Europe/Oslo"); the
// build's default when nothing matches.
inline const char *resolve_timezone(const char *time_zone_label) {
    if (time_zone_label != nullptr) {
        const size_t n = zones_count();
        for (size_t i = 0; i < n; i++) {
            const zones_t &z = zones_entry(i);
            if (std::strcmp(time_zone_label, z.name) == 0) {
                return z.zones;
            }
        }
    }
    return "GMT0";
}

#endif // ZONES_H
