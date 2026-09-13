#include "GradientSwatch.h"

#include <cstring>

namespace settingsui {

namespace {

// The uniform positions a gradient with no explicit ones carries, which is
// what both bg_parse_gradient and BgAnimCommon's fillUniformPositions fill in:
// p_i = i * 255 / (n - 1), truncating, so six stops sit at 0, 51, 102, 153,
// 204, 255.
void fillUniformPositions(uint8_t *pos, int n) {
    for (int i = 0; i < n; i++) {
        pos[i] = static_cast<uint8_t>((i * 255) / (n - 1));
    }
}

// bg_theme_stops returns six, and the resolver's fillBuiltin copies exactly
// six: a built-in is not a variable-length gradient.
constexpr int kBuiltinStops = 6;

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

} // namespace

bool swatchFromThemeStops(const uint8_t (*stops)[3], SwatchGradient &out) {
    if (stops == nullptr) {
        return false;
    }
    std::memcpy(out.stops, stops, kBuiltinStops * 3);
    out.count = kBuiltinStops;
    out.uniform = true;
    fillUniformPositions(out.pos, kBuiltinStops);
    return true;
}

bool swatchFromWire(const char *wire, SwatchGradient &out) {
    SwatchGradient parsed;
    const int n = bg_parse_gradient(wire, parsed.stops, parsed.pos, parsed.uniform);
    if (n < 2) {
        return false;
    }
    parsed.count = n;
    out = parsed;
    return true;
}

bool swatchResolveRef(const char *ref, const char *library, SwatchGradient &out) {
    if (ref == nullptr || *ref == '\0') {
        return false; // "" is "no gradient of its own", not a gradient
    }
    if (ref[0] == 'c') {
        int id = 0;
        const char *s = ref + 1;
        if (*s == '\0') {
            return false;
        }
        for (; *s != '\0'; s++) {
            if (*s < '0' || *s > '9') {
                return false;
            }
            id = id * 10 + (*s - '0');
            if (id > BG_GRADIENT_LIB_MAX + 1) {
                return false; // no library id reaches this, so the ref is junk
            }
        }
        SwatchGradient found;
        int n = 0;
        if (!bg_library_lookup(library, id, found.stops, found.pos, n, found.uniform) || n < 2) {
            return false; // a deleted entry, which the caller shows as no swatch
        }
        found.count = n;
        out = found;
        return true;
    }
    int index = 0;
    for (const char *s = ref; *s != '\0'; s++) {
        if (*s < '0' || *s > '9') {
            return false;
        }
        index = index * 10 + (*s - '0');
        if (index > bg_theme_count()) {
            return false;
        }
    }
    if (index < 0 || index >= bg_theme_count()) {
        return false; // an index from a longer table: no swatch rather than
                      // the wrong one, since bg_theme_stops would clamp to 0
    }
    return swatchFromThemeStops(bg_theme_stops(index), out);
}

void swatchApplyTone(SwatchGradient &gradient, int brightnessPct, int kneePct) {
    // The conversion DefaultUI::updateState does on every pass, including its
    // integer division, then setThemeTone's own clamps.
    int bright256 = brightnessPct * 256 / 100;
    int knee = kneePct * 255 / 100;
    if (bright256 < 0) {
        bright256 = 0;
    } else if (bright256 > 256) {
        bright256 = 256;
    }
    if (knee < 0) {
        knee = 0;
    } else if (knee > 255) {
        knee = 255;
    }
    for (int i = 0; i < gradient.count; i++) {
        for (int c = 0; c < 3; c++) {
            int v = gradient.stops[i][c];
            // Shoulder first, then brightness: the knee is specified against
            // full scale and has to act on the gradient's own values, or it
            // would move wherever brightness happened to be set.
            if (v > knee) {
                v = knee + ((v - knee) >> 2);
            }
            v = (v * bright256) >> 8;
            if (v < 0) {
                v = 0;
            } else if (v > 255) {
                v = 255;
            }
            gradient.stops[i][c] = static_cast<uint8_t>(v);
        }
    }
}

void swatchSampleRgb(const SwatchGradient &gradient, int position, uint8_t out[3]) {
    if (!gradient.valid()) {
        out[0] = out[1] = out[2] = 0;
        return;
    }
    const uint8_t(*st)[3] = gradient.stops;
    const int n = gradient.count;
    int pos = position;
    if (pos < 0) {
        pos = 0;
    } else if (pos > 255) {
        pos = 255;
    }
    if (gradient.uniform) {
        const int scaled = pos * (n - 1);
        const int seg = scaled >> 8;
        const int f = scaled & 255;
        for (int c = 0; c < 3; c++) {
            out[c] = static_cast<uint8_t>(st[seg][c] + (((st[seg + 1][c] - st[seg][c]) * f) >> 8));
        }
        return;
    }
    // Positional: flat before the first stop and after the last, as in CSS.
    const uint8_t *p = gradient.pos;
    if (pos <= p[0]) {
        for (int c = 0; c < 3; c++) {
            out[c] = st[0][c];
        }
        return;
    }
    if (pos >= p[n - 1]) {
        for (int c = 0; c < 3; c++) {
            out[c] = st[n - 1][c];
        }
        return;
    }
    int seg = 0;
    while (seg < n - 2 && pos >= p[seg + 1]) {
        seg++;
    }
    const int width = p[seg + 1] - p[seg];
    const int f = width > 0 ? ((pos - p[seg]) * 256) / width : 0;
    for (int c = 0; c < 3; c++) {
        out[c] = static_cast<uint8_t>(st[seg][c] + (((st[seg + 1][c] - st[seg][c]) * f) >> 8));
    }
}

uint16_t swatchSample565(const SwatchGradient &gradient, int position) {
    uint8_t c[3];
    swatchSampleRgb(gradient, position, c);
    return rgb565(c[0], c[1], c[2]);
}

void swatchBuildRamp565(const SwatchGradient &gradient, uint16_t *out, int count) {
    if (out == nullptr || count <= 0) {
        return;
    }
    if (count == 1) {
        out[0] = swatchSample565(gradient, 0);
        return;
    }
    for (int i = 0; i < count; i++) {
        out[i] = swatchSample565(gradient, (i * 255) / (count - 1));
    }
}

} // namespace settingsui
