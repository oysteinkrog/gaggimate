#ifndef GAGGIMATE_SIM

#include "BgAnim.h"
#include <ctype.h>
#include <string.h>

// Built-in color themes: 6 RGB stops each, dark -> bright. Mirrored in
// web/src/config/bgAnimations.js (BG_THEMES) — keep in sync, append only
// (the theme index is persisted in settings).

namespace {

struct Theme {
    const char *name;
    uint8_t stops[6][3];
};

const Theme THEMES[] = {
    {"Espresso", {{0x08, 0x04, 0x02}, {0x2a, 0x12, 0x06}, {0x6b, 0x34, 0x13}, {0xb8, 0x70, 0x3a}, {0xe8, 0xb2, 0x68}, {0xf8, 0xe6, 0xc8}}},
    {"Ocean", {{0x02, 0x06, 0x0c}, {0x06, 0x28, 0x4a}, {0x0a, 0x52, 0x76}, {0x25, 0x96, 0xbe}, {0x66, 0xd3, 0xe8}, {0xd8, 0xf6, 0xff}}},
    {"Violet Dusk", {{0x0a, 0x05, 0x12}, {0x2a, 0x10, 0x50}, {0x5c, 0x2a, 0x94}, {0x9a, 0x5a, 0xd4}, {0xd0, 0x9a, 0xf0}, {0xf4, 0xe2, 0xff}}},
    {"Forest", {{0x02, 0x08, 0x03}, {0x0c, 0x2c, 0x12}, {0x1e, 0x5c, 0x28}, {0x46, 0x96, 0x3c}, {0x8c, 0xd4, 0x64}, {0xe6, 0xff, 0xc8}}},
    {"Sunset", {{0x0c, 0x04, 0x10}, {0x4a, 0x10, 0x30}, {0x95, 0x20, 0x38}, {0xd4, 0x54, 0x2c}, {0xf8, 0x9c, 0x3c}, {0xff, 0xe8, 0xa0}}},
    {"Fire", {{0x0a, 0x02, 0x00}, {0x40, 0x10, 0x04}, {0x8c, 0x28, 0x08}, {0xd8, 0x5c, 0x10}, {0xf8, 0xa4, 0x28}, {0xff, 0xe8, 0xb0}}},
    {"Ice", {{0x02, 0x04, 0x08}, {0x10, 0x20, 0x3c}, {0x2c, 0x4a, 0x74}, {0x54, 0x86, 0xb4}, {0x9c, 0xc8, 0xe4}, {0xea, 0xfa, 0xff}}},
    {"Mono", {{0x00, 0x00, 0x00}, {0x20, 0x20, 0x20}, {0x48, 0x48, 0x48}, {0x80, 0x80, 0x80}, {0xc0, 0xc0, 0xc0}, {0xff, 0xff, 0xff}}},
    {"Rose", {{0x0e, 0x04, 0x07}, {0x3c, 0x10, 0x20}, {0x7a, 0x24, 0x40}, {0xc0, 0x48, 0x68}, {0xee, 0x8c, 0xa4}, {0xff, 0xdc, 0xe6}}},
    {"Gold", {{0x06, 0x04, 0x02}, {0x2e, 0x20, 0x08}, {0x6e, 0x50, 0x14}, {0xb4, 0x8c, 0x24}, {0xe8, 0xc4, 0x53}, {0xff, 0xf0, 0xb8}}},
    {"Aurora", {{0x01, 0x08, 0x06}, {0x06, 0x30, 0x20}, {0x0c, 0x64, 0x44}, {0x14, 0xa8, 0x78}, {0x48, 0xe0, 0xb0}, {0xc8, 0xff, 0xec}}},
    {"Cyber", {{0x05, 0x00, 0x08}, {0x24, 0x04, 0x48}, {0x50, 0x10, 0x90}, {0x90, 0x18, 0xd8}, {0xe0, 0x30, 0xf8}, {0xff, 0x9c, 0xf0}}},
    {"Ember Coal", {{0x0a, 0x06, 0x04}, {0x2b, 0x0a, 0x06}, {0x6b, 0x1a, 0x08}, {0xb8, 0x42, 0x0f}, {0xe2, 0x75, 0x1f}, {0xf4, 0xa9, 0x4a}}},
    {"Deep Space", {{0x05, 0x05, 0x0f}, {0x15, 0x0a, 0x28}, {0x34, 0x18, 0x40}, {0x6b, 0x2f, 0x5e}, {0xb3, 0x47, 0x7d}, {0xe6, 0xb3, 0xd6}}},
    {"Teal Reef", {{0x05, 0x0a, 0x0f}, {0x0a, 0x1c, 0x28}, {0x12, 0x3a, 0x44}, {0x1f, 0x6b, 0x6e}, {0x3f, 0xb3, 0xa8}, {0xbd, 0xee, 0xe0}}},
    {"Sakura", {{0x0c, 0x06, 0x0a}, {0x34, 0x18, 0x28}, {0x6e, 0x30, 0x50}, {0xb4, 0x5c, 0x80}, {0xe8, 0x96, 0xb0}, {0xff, 0xe0, 0xec}}},
    {"Lime", {{0x04, 0x08, 0x02}, {0x16, 0x30, 0x0a}, {0x32, 0x60, 0x16}, {0x5e, 0xa0, 0x24}, {0x9e, 0xe4, 0x4c}, {0xea, 0xff, 0xc0}}},
    {"Arctic Night", {{0x02, 0x02, 0x06}, {0x0a, 0x14, 0x24}, {0x1a, 0x30, 0x48}, {0x34, 0x58, 0x7c}, {0x6c, 0x94, 0xbc}, {0xc4, 0xe4, 0xf8}}},
};

constexpr int THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);

int hexNibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    c = static_cast<char>(tolower(c));
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    return -1;
}

// Parses "rrggbb[@pos][,rrggbb[@pos]...]" up to end (or the ';' that ends a
// library entry). Optional # prefixes, whitespace tolerated. Returns the
// number of stops parsed (0 if any entry is malformed); positions default to
// even spacing and uniform reports whether every stop left it that way.
int parseGradient(const char *s, uint8_t stops[BG_THEME_MAX_STOPS][3], uint8_t pos[BG_THEME_MAX_STOPS], bool &uniform) {
    uniform = true;
    if (s == nullptr) {
        return 0;
    }
    int n = 0;
    bool hasPos[BG_THEME_MAX_STOPS] = {false};
    while (*s != '\0' && *s != ';') {
        while (*s == ' ' || *s == ',' || *s == '#') {
            s++;
        }
        if (*s == '\0' || *s == ';') {
            break;
        }
        if (n >= BG_THEME_MAX_STOPS) {
            return 0;
        }
        for (int ch = 0; ch < 3; ch++) {
            const int hi = hexNibble(s[0]);
            const int lo = hi < 0 ? -1 : hexNibble(s[1]);
            if (lo < 0) {
                return 0;
            }
            stops[n][ch] = static_cast<uint8_t>((hi << 4) | lo);
            s += 2;
        }
        if (*s == '@') {
            s++;
            int v = 0;
            int digits = 0;
            while (*s >= '0' && *s <= '9' && digits < 4) {
                v = v * 10 + (*s - '0');
                s++;
                digits++;
            }
            if (digits == 0 || v > 255) {
                return 0;
            }
            pos[n] = static_cast<uint8_t>(v);
            hasPos[n] = true;
            uniform = false;
        }
        if (*s != '\0' && *s != ',' && *s != ' ' && *s != ';') {
            return 0;
        }
        n++;
    }
    if (n < 2) {
        return 0;
    }
    // Fill in what the string left out, then enforce monotonic order so a
    // hand-edited string cannot fold the ramp back. The ends are not pinned:
    // like a CSS gradient, the colour is held flat before the first stop and
    // after the last, which lets the editor drag any stop.
    for (int i = 0; i < n; i++) {
        if (!hasPos[i]) {
            pos[i] = static_cast<uint8_t>((i * 255) / (n - 1));
        }
    }
    for (int i = 1; i < n; i++) {
        if (pos[i] < pos[i - 1]) {
            pos[i] = pos[i - 1];
        }
    }
    return n;
}

int parseCustom(const char *s, uint8_t stops[BG_THEME_MAX_STOPS][3]) {
    uint8_t pos[BG_THEME_MAX_STOPS];
    bool uniform = true;
    return parseGradient(s, stops, pos, uniform);
}

// Reads a decimal id (1..99999) at s, advancing it; -1 if there is none.
int parseId(const char *&s) {
    int v = 0;
    int digits = 0;
    while (*s >= '0' && *s <= '9' && digits < 5) {
        v = v * 10 + (*s - '0');
        s++;
        digits++;
    }
    return digits > 0 ? v : -1;
}

// Advances s past the current ';'-separated entry (to the character after
// the separator, or the terminator).
void skipEntry(const char *&s) {
    while (*s != '\0' && *s != ';') {
        s++;
    }
    if (*s == ';') {
        s++;
    }
}

// Walks the library "id|name|gradient;..." calling visit(id, name, nameLen,
// gradient) per entry; returns false at the first malformed entry (or past
// BG_GRADIENT_LIB_MAX). visit returns true to stop the walk early.
template <typename F> bool walkLibrary(const char *lib, F visit) {
    if (lib == nullptr) {
        return true;
    }
    const char *s = lib;
    int count = 0;
    while (*s != '\0') {
        if (*s == ';') { // stray separator, tolerate
            s++;
            continue;
        }
        if (++count > BG_GRADIENT_LIB_MAX) {
            return false;
        }
        const int id = parseId(s);
        if (id <= 0 || *s != '|') {
            return false;
        }
        s++;
        const char *name = s;
        while (*s != '\0' && *s != '|' && *s != ';') {
            s++;
        }
        if (*s != '|') {
            return false;
        }
        // The UI caps names at BG_GRADIENT_NAME_MAX characters; this sees
        // UTF-8 bytes, so allow the widest encoding rather than reject a
        // library over one accented letter.
        const int nameLen = static_cast<int>(s - name);
        if (nameLen == 0 || nameLen > BG_GRADIENT_NAME_MAX * 4) {
            return false;
        }
        s++;
        const char *gradient = s;
        uint8_t stops[BG_THEME_MAX_STOPS][3];
        uint8_t pos[BG_THEME_MAX_STOPS];
        bool uniform = true;
        if (parseGradient(gradient, stops, pos, uniform) == 0) {
            return false;
        }
        if (visit(id, name, nameLen, gradient)) {
            return true;
        }
        skipEntry(s);
    }
    return true;
}

// Locates the ref for animId in "ref;ref;..." — nullptr when the map has no
// entry for it (or an empty one). The ref runs to the next ';' or the end.
const char *mapRef(const char *map, int animId) {
    if (map == nullptr || animId < 0) {
        return nullptr;
    }
    const char *s = map;
    for (int i = 0; i < animId; i++) {
        if (*s == '\0') {
            return nullptr;
        }
        skipEntry(s);
    }
    return (*s == '\0' || *s == ';') ? nullptr : s;
}

// One ref: "" | digits | 'c' digits. Sets builtin (>= 0) or libId (> 0).
bool parseRef(const char *ref, int &builtin, int &libId) {
    builtin = -1;
    libId = -1;
    if (ref == nullptr) {
        return true;
    }
    const char *s = ref;
    if (*s == 'c') {
        s++;
        libId = parseId(s);
        if (libId <= 0) {
            return false;
        }
    } else {
        builtin = parseId(s);
        if (builtin < 0) {
            return false;
        }
    }
    return *s == '\0' || *s == ';';
}

} // namespace

int bg_theme_count() { return THEME_COUNT; }

const char *bg_theme_name(int i) { return THEMES[(i >= 0 && i < THEME_COUNT) ? i : 0].name; }

const uint8_t (*bg_theme_stops(int i))[3] { return THEMES[(i >= 0 && i < THEME_COUNT) ? i : 0].stops; }

void bg_resolve_theme(int themeId, const char *custom, uint8_t stops[BG_THEME_MAX_STOPS][3], int &nStops) {
    if (themeId == THEME_COUNT) {
        const int n = parseCustom(custom, stops);
        if (n > 0) {
            nStops = n;
            return;
        }
        themeId = 0;
    }
    const uint8_t(*src)[3] = bg_theme_stops(themeId);
    memcpy(stops, src, 6 * 3);
    nStops = 6;
}

int bg_parse_gradient(const char *s, uint8_t stops[BG_THEME_MAX_STOPS][3], uint8_t pos[BG_THEME_MAX_STOPS],
                      bool &uniform) {
    return parseGradient(s, stops, pos, uniform);
}

int bg_format_gradient(const uint8_t stops[][3], const uint8_t *pos, int nStops, bool uniform, char *out, int outLen) {
    static const char HEX[] = "0123456789abcdef";
    int len = 0;
    for (int i = 0; i < nStops; i++) {
        if (len + 11 + 1 > outLen) {
            out[0] = '\0';
            return 0;
        }
        if (i > 0) {
            out[len++] = ',';
        }
        for (int ch = 0; ch < 3; ch++) {
            out[len++] = HEX[stops[i][ch] >> 4];
            out[len++] = HEX[stops[i][ch] & 15];
        }
        if (!uniform) {
            out[len++] = '@';
            int v = pos[i];
            if (v >= 100) {
                out[len++] = static_cast<char>('0' + v / 100);
                v %= 100;
                out[len++] = static_cast<char>('0' + v / 10);
                out[len++] = static_cast<char>('0' + v % 10);
            } else if (v >= 10) {
                out[len++] = static_cast<char>('0' + v / 10);
                out[len++] = static_cast<char>('0' + v % 10);
            } else {
                out[len++] = static_cast<char>('0' + v);
            }
        }
    }
    out[len] = '\0';
    return len;
}

bool bg_library_valid(const char *lib) {
    if (lib != nullptr && strlen(lib) > static_cast<size_t>(BG_GRADIENT_LIB_MAX_LEN)) {
        return false;
    }
    return walkLibrary(lib, [](int, const char *, int, const char *) { return false; });
}

bool bg_library_lookup(const char *lib, int id, uint8_t stops[BG_THEME_MAX_STOPS][3], uint8_t pos[BG_THEME_MAX_STOPS],
                       int &nStops, bool &uniform) {
    const char *found = nullptr;
    walkLibrary(lib, [&](int entryId, const char *, int, const char *gradient) {
        if (entryId == id) {
            found = gradient;
            return true;
        }
        return false;
    });
    if (found == nullptr) {
        return false;
    }
    nStops = parseGradient(found, stops, pos, uniform);
    return nStops > 0;
}

bool bg_map_valid(const char *map) {
    if (map == nullptr) {
        return true;
    }
    if (strlen(map) > 256) {
        return false;
    }
    const char *s = map;
    while (*s != '\0') {
        int builtin;
        int libId;
        if (*s != ';' && !parseRef(s, builtin, libId)) {
            return false;
        }
        skipEntry(s);
    }
    return true;
}

void bg_resolve_anim_theme(int animId, const char *map, const char *library, int themeId, const char *custom,
                           uint8_t stops[BG_THEME_MAX_STOPS][3], uint8_t pos[BG_THEME_MAX_STOPS], int &nStops,
                           bool &uniform) {
    int builtin;
    int libId;
    if (parseRef(mapRef(map, animId), builtin, libId)) {
        if (libId > 0 && bg_library_lookup(library, libId, stops, pos, nStops, uniform)) {
            return;
        }
        if (builtin >= 0 && builtin < THEME_COUNT) {
            themeId = builtin;
        }
    }
    bg_resolve_theme(themeId, custom, stops, nStops);
    uniform = true;
    for (int i = 0; i < nStops; i++) {
        pos[i] = static_cast<uint8_t>((i * 255) / (nStops - 1));
    }
}

#endif // GAGGIMATE_SIM
