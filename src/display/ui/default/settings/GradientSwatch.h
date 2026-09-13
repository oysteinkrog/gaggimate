#ifndef GM_SETTINGS_GRADIENT_SWATCH_H
#define GM_SETTINGS_GRADIENT_SWATCH_H

// Samples a gradient the way the panel draws it, without touching the
// animation's live theme (gm-nov3.3). The gradient picker's rows show a
// swatch, and a swatch that is not the toned ramp the renderer builds is a
// picture of a gradient the machine will not draw.
//
// Why this is not a call into bganim. setThemeStops/setThemeTone/themeRGB
// (BgAnimCommon.cpp) work on one set of globals: the theme the render task is
// drawing with right now. Sampling five gradients to draw five rows through
// that path would publish each of them in turn as the active theme, so the
// screen behind the settings cover would flicker through every gradient the
// page lists. It also cannot be reached at all from the simulator, where
// BgAnimCommon.cpp does not compile.
//
// So the arithmetic is transcribed here, once, from
// BgAnimCommon.cpp's publishStops() and themeRGB() plus the
// DefaultUI::updateState call site that converts the two settings
// percentages. It is a fourth reading of the same rules (the firmware, the
// web sampler in web/src/config/gradientRamp.js, tools/animbench/ramp_dump.cpp
// and this), and like the web one it is held to the firmware rather than
// trusted: tools/animbench/swatch_parity.cpp feeds the same gradients through
// this file and through the real bganim globals and compares every entry of
// the 256-entry ramp, and `make check` in tools/animbench runs it. Do not
// "fix" anything here to make a swatch look better; change the firmware and
// let the parity check follow.
//
// What a swatch deliberately does not reproduce: an animation's own extra
// palette gain (buildThemeRamp's brightness256 argument, which most
// animations leave at 256) and Plasma's cyclic wheel. A row shows the
// standard ramp, which is what the gradient is; what one animation then does
// with it is that animation's business.
//
// Plain C++17 over BgAnim.h, no LVGL: the row widget draws with it, the host
// test checks it and the parity tool links it.

#include <display/ui/default/bganim/BgAnim.h>

#include <cstdint>

namespace settingsui {

// A gradient ready to sample: exactly the shape bg_parse_gradient fills and
// bg_resolve_anim_theme hands the renderer. `uniform` is a property of where
// the gradient came from, not of the numbers: a built-in and a library entry
// written without explicit positions are sampled by different arithmetic from
// one written with evenly spaced explicit positions, and the two disagree by
// a few units at the top of the ramp.
struct SwatchGradient {
    uint8_t stops[BG_THEME_MAX_STOPS][3] = {};
    uint8_t pos[BG_THEME_MAX_STOPS] = {};
    int count = 0;
    bool uniform = true;

    bool valid() const { return count >= 2; }
};

// One GradientChoice ref as the picker holds it, resolved against the
// built-in table and the stored library string: "<n>" is built-in n (six
// stops, evenly spaced, the uniform path), "c<id>" is a library entry (2 to
// 16 stops, its own positions when it carries them), and "" resolves to
// nothing because it is not a gradient. False leaves `out` untouched, which
// is a row with no swatch rather than a wrong one.
//
// The grammar is production's, digit limit included: one to five digits, so
// library ids run 1 to 99999, and a ref may end at a ';' because production
// parses map slots with the same parser. An id is not bounded by the number
// of entries a library may hold; see the note above swatchResolveRef's
// parser in GradientSwatch.cpp. A built-in index past this build's table
// resolves to nothing, the way the renderer falls through it.
bool swatchResolveRef(const char *ref, const char *library, SwatchGradient &out);

// A built-in's six stops as the resolver reads them (evenly spaced, uniform).
// `stops` is what bg_theme_stops()/ThemeNameProvider::stops returns; null
// gives an invalid gradient.
bool swatchFromThemeStops(const uint8_t (*stops)[3], SwatchGradient &out);

// bg_parse_gradient over a library entry's own wire string, for a caller that
// already has one.
bool swatchFromWire(const char *wire, SwatchGradient &out);

// publishStops(): the highlight shoulder acts on the gradient's own values,
// then brightness scales whatever shape came out, in that order. The two
// arguments are the stored percentages (bgAnimBrightness, bgAnimHighlightKnee),
// converted here the way DefaultUI::updateState converts them before calling
// setThemeTone. In place: the result is what the renderer would publish.
void swatchApplyTone(SwatchGradient &gradient, int brightnessPct, int kneePct);

// themeRGB() on a toned gradient: the colour at 0..255 along the ramp, out of
// range clamped. An invalid gradient reads black.
void swatchSampleRgb(const SwatchGradient &gradient, int position, uint8_t out[3]);

// The same sample as the RGB565 the panel stores, which is the only honest
// comparison between two colours on this display.
uint16_t swatchSample565(const SwatchGradient &gradient, int position);

// buildThemeRamp() at gain 256: `count` evenly spaced samples across the
// whole ramp, into out[0..count-1]. count < 2 writes at most one sample (the
// ramp's start), and an invalid gradient writes black.
void swatchBuildRamp565(const SwatchGradient &gradient, uint16_t *out, int count);

} // namespace settingsui

#endif // GM_SETTINGS_GRADIENT_SWATCH_H
