// The one bganim translation unit that compiles on the host as well as on
// the device. Everything here is plain C++ over BgAnim.h and the generated
// table: the gradient parser, the library reader, the ref validators and the
// three-step resolver, with no Arduino, ESP-IDF or render kernel behind any
// of it. The rest of this directory is wrapped in #ifndef GAGGIMATE_SIM
// because it carries the kernels; this file is not, so the simulator and the
// host test both run the real rules rather than a stub that answers "no" to
// every question (gm-nov3.3). Keep it that way: a device-only dependency
// added here silently takes the simulator's gradient tests back to the stub.

#include "BgAnim.h"
#include "BgAnimThemeTable.h"
#include <algorithm>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <vector>

// Built-in color themes: 6 RGB stops each, dark -> bright. The table comes
// from data/gradients.json through scripts/gen_gradients.py, which writes the
// web mirror from the same source, so a gradient is added in one place. The
// list is append only: an entry's index is what the settings store.

namespace {

using Theme = bganim_gen::ThemeDef;

#ifdef GM_BGANIM_TEST_TABLE
// Host tests swap this for a longer table (bg_test_set_theme_table), which is
// how the frozen legacy sentinel is checked against a table this build does
// not ship. Firmware never defines the macro and keeps the constants.
const Theme *THEMES = bganim_gen::THEME_DEFS;
int THEME_COUNT = bganim_gen::THEME_DEF_COUNT;
#else
constexpr const Theme *THEMES = bganim_gen::THEME_DEFS;
constexpr int THEME_COUNT = bganim_gen::THEME_DEF_COUNT;
#endif

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

const char *bg_theme_category(int i) { return THEMES[(i >= 0 && i < THEME_COUNT) ? i : 0].category; }

int bg_theme_category_count() { return bganim_gen::THEME_CATEGORY_COUNT; }

const char *bg_theme_category_name(int i) {
    const int n = bganim_gen::THEME_CATEGORY_COUNT;
    return bganim_gen::THEME_CATEGORIES[(i >= 0 && i < n) ? i : 0];
}

const uint8_t (*bg_theme_stops(int i))[3] { return THEMES[(i >= 0 && i < THEME_COUNT) ? i : 0].stops; }

bool bg_custom_valid(const char *custom) {
    uint8_t stops[BG_THEME_MAX_STOPS][3];
    return parseCustom(custom, stops) > 0;
}

void bg_resolve_theme(int themeId, const char *custom, uint8_t stops[BG_THEME_MAX_STOPS][3], int &nStops) {
    // The legacy namespace, frozen at BG_THEME_LEGACY_CUSTOM rather than read
    // off the table length (BgAnim.h says why at length). Anything outside
    // 0..18 is a legacy integer this firmware never wrote and reads as 0.
    if (themeId == BG_THEME_LEGACY_CUSTOM) {
        const int n = parseCustom(custom, stops);
        if (n > 0) {
            nStops = n;
            return;
        }
        themeId = 0;
    } else if (themeId < 0 || themeId >= BG_THEME_LEGACY_CUSTOM) {
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

bool bg_ref_valid(const char *ref) {
    int builtin;
    int libId;
    if (ref == nullptr || *ref == '\0') {
        return true;
    }
    return parseRef(ref, builtin, libId);
}

namespace {

// A built-in straight out of the table, evenly spaced, which is how built-in
// gradients have always been drawn.
void fillBuiltin(int themeId, uint8_t stops[BG_THEME_MAX_STOPS][3], uint8_t pos[BG_THEME_MAX_STOPS], int &nStops,
                 bool &uniform) {
    memcpy(stops, bg_theme_stops(themeId), 6 * 3);
    nStops = 6;
    uniform = true;
    for (int i = 0; i < 6; i++) {
        pos[i] = static_cast<uint8_t>((i * 255) / 5);
    }
}

} // namespace

void bg_resolve_anim_theme(int animId, const char *map, const char *library, const char *globalRef, int themeId,
                           const char *custom, uint8_t stops[BG_THEME_MAX_STOPS][3], uint8_t pos[BG_THEME_MAX_STOPS],
                           int &nStops, bool &uniform) {
    int builtin;
    int libId;
    // Step one: this animation's own override. An explicit ref is its own
    // namespace, so a built-in it names is read straight out of the table and
    // never routed through the legacy branch below: on a build whose table is
    // long enough, the ref "18" is the built-in at index 18, even while a
    // legacy 18 in bgAnimTheme still means the custom gradient.
    if (parseRef(mapRef(map, animId), builtin, libId)) {
        if (libId > 0 && bg_library_lookup(library, libId, stops, pos, nStops, uniform)) {
            return;
        }
        if (builtin >= 0 && builtin < THEME_COUNT) {
            fillBuiltin(builtin, stops, pos, nStops, uniform);
            return; // an override that names a built-in wins outright
        }
    }
    // Step two: the global default, which unlike themeId can name one of the
    // user's own gradients. A ref that does not resolve (a deleted library
    // entry, an index from a longer table) falls through, the same way a
    // dangling map entry does.
    if (globalRef != nullptr && *globalRef != '\0' && parseRef(globalRef, builtin, libId)) {
        if (libId > 0 && bg_library_lookup(library, libId, stops, pos, nStops, uniform)) {
            return;
        }
        if (builtin >= 0 && builtin < THEME_COUNT) {
            fillBuiltin(builtin, stops, pos, nStops, uniform);
            return;
        }
    }
    // Step three: what the setting did before the global ref existed.
    bg_resolve_theme(themeId, custom, stops, nStops);
    uniform = true;
    for (int i = 0; i < nStops; i++) {
        pos[i] = static_cast<uint8_t>((i * 255) / (nStops - 1));
    }
}

#ifdef GM_BGANIM_TEST_TABLE
void bg_test_set_theme_table(const bganim_gen::ThemeDef *defs, int count) {
    THEMES = defs != nullptr ? defs : bganim_gen::THEME_DEFS;
    THEME_COUNT = defs != nullptr ? count : bganim_gen::THEME_DEF_COUNT;
}
#endif

// ---- carrying the legacy custom gradient into the library ----------------

namespace {

// The name a migrated entry gets. Never used to find one: a device migrated
// by the first version of this code has an entry called "Custom" and a user
// can rename or delete it, so the match below is on the gradient itself.
constexpr const char *kMigratedName = "Custom";

// Does ref name something that exists right now? An unresolved ref is the
// reason the legacy fallback behind it has to be kept.
bool refResolves(const char *ref, const char *library) {
    int builtin;
    int libId;
    if (ref == nullptr || *ref == '\0' || !parseRef(ref, builtin, libId)) {
        return false;
    }
    if (libId > 0) {
        uint8_t stops[BG_THEME_MAX_STOPS][3];
        uint8_t pos[BG_THEME_MAX_STOPS];
        int nStops = 0;
        bool uniform = true;
        return bg_library_lookup(library, libId, stops, pos, nStops, uniform);
    }
    return builtin >= 0 && builtin < THEME_COUNT;
}

// The ids a migration may not hand out: every id an entry carries, plus every
// id a stored ref names, whether or not it resolves today. A dangling "c5"
// that suddenly resolved would change what that animation draws.
//
// A list rather than a flag per id, because the accepted range runs to
// BG_GRADIENT_ID_MAX and the ids in use are not dense: the web editor
// allocates by incrementing the largest one, so a three-entry library can
// hold 1, 40 and 41. The list stays short whatever the ids are. There are at
// most BG_GRADIENT_LIB_MAX entries and the map is capped at 256 characters,
// so a few dozen ids in all, and the lowest free one is never far above that
// count.
struct ReservedIds {
    std::vector<int> ids;

    void add(int id) {
        if (id > 0 && id <= BG_GRADIENT_ID_MAX) {
            ids.push_back(id);
        }
    }

    // Every library id named by "ref;ref;...", one map or the global ref.
    void addRefs(const char *refs) {
        if (refs == nullptr) {
            return;
        }
        const char *s = refs;
        while (*s != '\0') {
            if (*s != ';') {
                int builtin;
                int libId;
                if (parseRef(s, builtin, libId)) {
                    add(libId);
                }
            }
            skipEntry(s);
        }
    }

    // The lowest id nothing has reserved, or -1 when the range is exhausted.
    int firstFree() {
        std::sort(ids.begin(), ids.end());
        int candidate = 1;
        for (const int id : ids) {
            if (id < candidate) {
                continue; // a duplicate, or an id below where the scan is
            }
            if (id > candidate) {
                break; // the gap under it is free
            }
            candidate = id + 1;
        }
        return candidate <= BG_GRADIENT_ID_MAX ? candidate : -1;
    }
};

// Does the production lookup for id give back exactly this gradient, evenly
// spaced? Walking the library and looking an id up do not have to agree:
// duplicate ids validate, and bg_library_lookup answers with the first entry
// carrying one. So an entry that matches the legacy gradient is a usable
// destination only when its own id resolves to it. Publishing a ref to an id
// that resolves elsewhere and then retiring the legacy fields is how the
// user's gradient would be lost.
bool idResolvesTo(const char *library, int id, const char *wanted) {
    uint8_t stops[BG_THEME_MAX_STOPS][3];
    uint8_t pos[BG_THEME_MAX_STOPS];
    int nStops = 0;
    bool uniform = true;
    if (!bg_library_lookup(library, id, stops, pos, nStops, uniform) || nStops == 0 || !uniform) {
        return false;
    }
    char canon[BG_GRADIENT_STR_MAX];
    return bg_format_gradient(stops, pos, nStops, true, canon, sizeof(canon)) > 0 && strcmp(canon, wanted) == 0;
}

} // namespace

BgGradientMigration bg_plan_gradient_migration(const char *library, const char *custom, int themeId,
                                               const char *globalRef, const char *map) {
    BgGradientMigration plan;

    uint8_t stops[BG_THEME_MAX_STOPS][3];
    uint8_t pos[BG_THEME_MAX_STOPS];
    bool uniform = true;
    const int n = parseGradient(custom, stops, pos, uniform);
    if (n == 0) {
        plan.reason = "no legacy custom gradient stored";
        return plan;
    }
    // The legacy resolver throws explicit positions away and interpolates
    // evenly, so the copy has to be the uniform form, or the picture changes
    // the moment it is drawn from the library instead.
    char wanted[BG_GRADIENT_STR_MAX];
    if (bg_format_gradient(stops, pos, n, true, wanted, sizeof(wanted)) == 0) {
        plan.action = BgGradientMigration::Deferred;
        plan.reason = "the custom gradient does not fit the library format";
        return plan;
    }

    if (!bg_library_valid(library)) {
        plan.action = BgGradientMigration::Deferred;
        plan.reason = "the stored gradient library is malformed";
        return plan;
    }

    ReservedIds reserved;
    int entryCount = 0;
    int matchId = -1;
    walkLibrary(library, [&](int id, const char *, int, const char *) {
        entryCount++;
        reserved.add(id);
        // A destination is judged by what its id resolves to, not by what the
        // entry in front of us holds. Same semantics means the same colours
        // in the same order AND even spacing: an entry with the same colours
        // at its own positions draws differently, and it is the user's, so it
        // is left alone and a new entry is allocated instead.
        if (matchId < 0 && idResolvesTo(library, id, wanted)) {
            matchId = id;
        }
        return false;
    });
    reserved.addRefs(globalRef);
    reserved.addRefs(map);

    if (matchId > 0) {
        plan.entryId = matchId;
    } else {
        if (entryCount >= BG_GRADIENT_LIB_MAX) {
            plan.action = BgGradientMigration::Deferred;
            plan.reason = "the gradient library is full";
            return plan;
        }
        const int id = reserved.firstFree();
        if (id < 0) {
            plan.action = BgGradientMigration::Deferred;
            plan.reason = "no free gradient id";
            return plan;
        }
        const int written = snprintf(plan.entry, sizeof(plan.entry), "%d|%s|%s", id, kMigratedName, wanted);
        if (written <= 0 || static_cast<size_t>(written) >= sizeof(plan.entry)) {
            plan.action = BgGradientMigration::Deferred;
            plan.reason = "the custom gradient does not fit the library format";
            return plan;
        }
        const size_t have = library != nullptr ? strlen(library) : 0;
        const size_t grown = have + (have > 0 ? 1 : 0) + static_cast<size_t>(written);
        if (grown > static_cast<size_t>(BG_GRADIENT_LIB_MAX_LEN)) {
            plan.action = BgGradientMigration::Deferred;
            plan.reason = "the gradient library is full";
            return plan;
        }
        plan.appendEntry = true;
        plan.entryId = id;
    }

    // Is the custom gradient what the global fallback draws right now? Only
    // then does the migration publish a ref of its own.
    const bool globalEmpty = globalRef == nullptr || *globalRef == '\0';
    const bool legacyNamesCustom = themeId == BG_THEME_LEGACY_CUSTOM;
    if (globalEmpty && legacyNamesCustom) {
        plan.setGlobalRef = true;
        snprintf(plan.globalRef, sizeof(plan.globalRef), "c%d", plan.entryId);
    }

    // Retiring the legacy pair is safe only when nothing still needs it:
    //  - the migration is publishing the replacement ref itself, or
    //  - bgAnimTheme does not name the custom gradient at all, so the legacy
    //    fallback draws a built-in and the string is already inert, or
    //  - the stored global ref already resolves to this very entry, which is
    //    what a re-run after a partly persisted migration sees.
    // Anything else means an unresolved ref could come back to the legacy
    // fallback, so both legacy fields stay exactly as they are.
    char ownRef[8];
    snprintf(ownRef, sizeof(ownRef), "c%d", plan.entryId);
    const bool globalIsThisEntry = !globalEmpty && strcmp(globalRef, ownRef) == 0 && refResolves(globalRef, library);
    if (plan.setGlobalRef || !legacyNamesCustom || globalIsThisEntry) {
        plan.retireLegacy = true;
        plan.themeAfter = legacyNamesCustom ? 0 : themeId;
    } else {
        plan.reason = "a newer global gradient is stored; the legacy fallback stays";
    }

    plan.action = (plan.appendEntry || plan.setGlobalRef || plan.retireLegacy) ? BgGradientMigration::Migrate
                                                                              : BgGradientMigration::None;
    if (plan.action == BgGradientMigration::None && plan.reason[0] == '\0') {
        plan.reason = "already carried over";
    }
    return plan;
}

BgMigrateResult bg_run_gradient_migration(const BgGradientStore &store, const BgGradientMigration &plan) {
    if (plan.action == BgGradientMigration::None) {
        return BgMigrateResult::NothingToDo;
    }
    if (plan.action == BgGradientMigration::Deferred) {
        return BgMigrateResult::Deferred;
    }
    if (store.library == nullptr || store.setLibrary == nullptr || store.setGlobalRef == nullptr ||
        store.setLegacy == nullptr || store.flush == nullptr) {
        return BgMigrateResult::Incomplete;
    }
    // Step one: the destination entry, durable before anything points at it.
    if (plan.appendEntry) {
        const char *have = store.library(store.user);
        const size_t haveLen = have != nullptr ? strlen(have) : 0;
        const size_t need = haveLen + (haveLen > 0 ? 1 : 0) + strlen(plan.entry) + 1;
        if (need > static_cast<size_t>(BG_GRADIENT_LIB_MAX_LEN) + 1) {
            return BgMigrateResult::Incomplete;
        }
        // Heap, not stack: this runs at boot on a task whose stack is sized
        // for the UI, and the library can be 3800 characters.
        std::vector<char> buf(need);
        if (haveLen > 0) {
            memcpy(buf.data(), have, haveLen);
            buf[haveLen] = ';';
        }
        memcpy(buf.data() + haveLen + (haveLen > 0 ? 1 : 0), plan.entry, strlen(plan.entry) + 1);
        // Never publish a library that would not parse: a malformed one makes
        // every ref into it fall back, silently.
        if (!bg_library_valid(buf.data())) {
            return BgMigrateResult::Incomplete;
        }
        store.setLibrary(store.user, buf.data());
        if (!store.flush(store.user)) {
            return BgMigrateResult::Incomplete;
        }
    }
    // Step two: the ref, durable before the legacy fallback is retired.
    if (plan.setGlobalRef) {
        store.setGlobalRef(store.user, plan.globalRef);
        if (!store.flush(store.user)) {
            return BgMigrateResult::Incomplete;
        }
    }
    // Step three. Not a verified clear: Preferences reports a failed
    // empty-string write as success (Property.h, nvsPutString), so this can
    // silently not happen. Everything above is already durable and the next
    // boot re-plans from whatever actually landed, which is why nothing
    // downstream may treat a cleared string as proof of anything.
    if (plan.retireLegacy) {
        store.setLegacy(store.user, plan.themeAfter, "");
        store.flush(store.user);
    }
    return BgMigrateResult::Done;
}

