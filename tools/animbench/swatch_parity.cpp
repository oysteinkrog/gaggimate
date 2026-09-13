// swatch_parity: the settings picker's gradient swatch, held to the panel.
//
// The picker rows in src/display/ui/default/settings/ draw a swatch by
// sampling through GradientSwatch.cpp, which transcribes the firmware's own
// palette arithmetic rather than calling it: the real path
// (bganim::setThemeStops + setThemeTone + buildThemeRamp) works on the globals
// the render task is drawing with, so sampling five gradients to draw five
// rows would publish each of them in turn as the active theme.
//
// A transcription that is only assumed equal is a swatch that can quietly lie
// about what the machine will draw. This links both: the real bganim globals
// and the settings sampler, feeds the same gradients through each, and
// compares all 256 entries of the ramp. It also checks the reason the
// transcription exists, by watching bganim::themeGen() across a batch of
// swatch samples: a swatch must not publish a theme.
//
// Build:  make build/swatch_parity
// Run:    ./build/swatch_parity      (non-zero exit and a report on any
//                                     mismatch; `make check` runs it)

#include "../../src/display/ui/default/bganim/BgAnim.h"
#include "../../src/display/ui/default/bganim/BgAnimCommon.h"
#include "../../src/display/ui/default/settings/GradientSwatch.h"

#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;
int g_compared = 0;

// The tone settings to check each gradient at. 100/100 is the default a
// device ships with; the rest move the brightness scale and the highlight
// shoulder separately and together, including the two ends, because the
// shoulder acts before brightness and the wrong order only shows when both
// are off their defaults.
struct Tone {
    int brightnessPct;
    int kneePct;
};
const Tone kTones[] = {{100, 100}, {60, 100}, {100, 55}, {60, 55}, {35, 20}, {0, 100}, {100, 0}};

// Library gradients, in the wire format the settings store. Between them they
// cover what the picker has to draw: the two-stop minimum, the sixteen-stop
// maximum, explicit positions that are not evenly spaced, two stops at the
// same position (a hard edge), and stops that leave a flat run at one or both
// ends of the ramp.
const char *const kWires[] = {
    "000000,ffffff",
    "3b1f0b,d4a373",
    "000000@40,ffffff@200",                       // flat at both ends
    "ff0000@0,00ff00@10,0000ff@255",              // early crowding, flat at neither end
    "101020,4060a0@64,ffd166@65,20e0a0@200,ffffff", // a repeated-ish position, a hard step
    "000000@0,ffffff@0,000000@255",               // two stops at the same position
    "0a0a0a@10,141414@10,ffffff@240,f0f0f0@255",  // repeated position plus flat runs
    "000000,111111,222222,333333,444444,555555,666666,777777,888888,999999,aaaaaa,bbbbbb,cccccc,dddddd,eeeeee,ffffff",
    "020208@0,101030@17,203060@34,3060a0@51,50a0d0@68,80d0e0@85,a0e0c0@102,c0f0a0@119,e0f080@136,f0e060@153,"
    "f0c040@170,f09030@187,e06020@204,c03010@221,801008@238,300000@255",
};

void compare(const char *what, const settingsui::SwatchGradient &swatch, const uint16_t *firmware, const Tone &tone) {
    int bad = 0;
    int firstBad = -1;
    for (int i = 0; i < 256; i++) {
        const uint16_t got = settingsui::swatchSample565(swatch, i);
        if (got != firmware[i]) {
            if (firstBad < 0) {
                firstBad = i;
                std::printf("MISMATCH %s at brightness=%d knee=%d: index %d swatch=%u firmware=%u\n", what,
                            tone.brightnessPct, tone.kneePct, i, got, firmware[i]);
            }
            bad++;
        }
    }
    g_compared++;
    if (bad > 0) {
        std::printf("         %s: %d of 256 entries differ\n", what, bad);
        g_failures++;
    }
}

// The firmware's own ramp for one gradient at one tone, through the globals
// the render task reads.
void firmwareRamp(const settingsui::SwatchGradient &source, const Tone &tone, uint16_t *out) {
    if (source.uniform) {
        bganim::setThemeStops(source.stops, source.count);
    } else {
        bganim::setThemeStopsPos(source.stops, source.pos, source.count);
    }
    // The conversion DefaultUI::updateState applies before calling it.
    bganim::setThemeTone(tone.brightnessPct * 256 / 100, tone.kneePct * 255 / 100);
    bganim::buildThemeRamp(out, 256, false); // gain 256: no animation's extra gain
}

void checkGradient(const char *what, const settingsui::SwatchGradient &source) {
    for (const Tone &tone : kTones) {
        uint16_t firmware[256];
        firmwareRamp(source, tone, firmware);
        settingsui::SwatchGradient swatch = source;
        settingsui::swatchApplyTone(swatch, tone.brightnessPct, tone.kneePct);
        compare(what, swatch, firmware, tone);
    }
}

} // namespace

int main() {
    // Every built-in, read the way the resolver reads one.
    for (int i = 0; i < bg_theme_count(); i++) {
        settingsui::SwatchGradient g;
        if (!settingsui::swatchFromThemeStops(bg_theme_stops(i), g)) {
            std::printf("MISMATCH built-in %d: swatchFromThemeStops refused it\n", i);
            g_failures++;
            continue;
        }
        char label[64];
        std::snprintf(label, sizeof(label), "built-in %d (%s)", i, bg_theme_name(i));
        checkGradient(label, g);
    }

    // Library gradients, through the same parser the firmware uses.
    for (const char *wire : kWires) {
        settingsui::SwatchGradient g;
        if (!settingsui::swatchFromWire(wire, g)) {
            std::printf("MISMATCH wire %s: swatchFromWire refused it\n", wire);
            g_failures++;
            continue;
        }
        checkGradient(wire, g);
    }

    // A ref resolves to the same gradient the wire string does, which is what
    // the picker rows actually call.
    //
    // The library ids here are 1, 14 and 99999 on purpose (gm-nov3.15). The
    // number of entries a library may hold is twelve; the ids they carry run
    // to 99999. Each of the two high ids stands for a different way of
    // getting one. 14 is what the current allocator produces: it hands out
    // the lowest id nothing reserves, and a ref that still names a deleted
    // entry keeps that id reserved, so a few rounds of copying and deleting
    // leave a three-entry library holding c14. 99999 is the top of the
    // grammar and a legacy id: the allocator this replaced returned the
    // largest id plus one with no ceiling (gm-nov3.21), and the entries it
    // wrote are still on devices. A resolver that stopped at c13 left every
    // saved gradient above it listed, drawn by the panel, and unselectable,
    // since CatGradientPicker.cpp gates a selection on this.
    {
        char library[BG_GRADIENT_LIB_MAX_LEN];
        std::snprintf(library, sizeof(library), "1|Two|%s;14|Wide|%s;99999|Sixteen|%s", kWires[0], kWires[2],
                      kWires[7]);
        struct Case {
            const char *ref;
            int libId;    // 0 for a built-in ref
            const char *wire;
        } cases[] = {{"1", 0, nullptr}, {"c1", 1, kWires[0]}, {"c14", 14, kWires[2]}, {"c99999", 99999, kWires[7]}};
        for (const Case &c : cases) {
            settingsui::SwatchGradient viaRef;
            if (!settingsui::swatchResolveRef(c.ref, library, viaRef)) {
                std::printf("MISMATCH ref %s: swatchResolveRef refused it\n", c.ref);
                g_failures++;
                continue;
            }
            settingsui::SwatchGradient wanted;
            const bool ok = c.wire != nullptr ? settingsui::swatchFromWire(c.wire, wanted)
                                              : settingsui::swatchFromThemeStops(bg_theme_stops(1), wanted);
            if (!ok || std::memcmp(&viaRef, &wanted, sizeof(wanted)) != 0) {
                std::printf("MISMATCH ref %s: resolved to a different gradient than its source\n", c.ref);
                g_failures++;
                continue;
            }
            if (c.libId > 0) {
                // The production library resolution, both entry points: the
                // lookup the renderer's resolver calls, and the resolver
                // itself with the ref in a map slot. The wires above are not
                // built-in 0's stops, so a ref that fell through to the
                // legacy fallback would be caught here rather than passing.
                settingsui::SwatchGradient viaLookup;
                int n = 0;
                if (!bg_library_lookup(library, c.libId, viaLookup.stops, viaLookup.pos, n, viaLookup.uniform) ||
                    n < 2) {
                    std::printf("MISMATCH ref %s: bg_library_lookup refused id %d\n", c.ref, c.libId);
                    g_failures++;
                    continue;
                }
                viaLookup.count = n;
                if (std::memcmp(&viaRef, &viaLookup, sizeof(viaLookup)) != 0) {
                    std::printf("MISMATCH ref %s: swatch and bg_library_lookup disagree\n", c.ref);
                    g_failures++;
                    continue;
                }
                settingsui::SwatchGradient viaResolve;
                int rn = 0;
                bg_resolve_anim_theme(0, c.ref, library, nullptr, 0, nullptr, viaResolve.stops, viaResolve.pos, rn,
                                      viaResolve.uniform);
                viaResolve.count = rn;
                if (std::memcmp(&viaRef, &viaResolve, sizeof(viaResolve)) != 0) {
                    std::printf("MISMATCH ref %s: swatch and bg_resolve_anim_theme disagree\n", c.ref);
                    g_failures++;
                    continue;
                }
            }
            // All 256 ramp entries at every tone, against the firmware's own
            // globals: a ref the resolver accepts has to draw what the panel
            // draws, not merely resolve.
            checkGradient(c.ref, viaRef);
        }
        // "" is not a gradient, and a ref naming an entry that is not there
        // resolves to nothing rather than to the wrong gradient. The rest are
        // the grammar's refusals: a zero id, an id past the accepted range, a
        // sixth digit, trailing junk, and a digit run long enough to overflow
        // an int if the parser had no digit limit (it stops at five, so the
        // accumulator never passes 99999).
        settingsui::SwatchGradient unused;
        const char *refusals[] = {
            "",                      // not a gradient
            "c9", "c13",             // ids the library does not carry
            "99999",                 // a built-in index past this build's table
            "c0", "c00",             // zero is no id: the library starts at 1
            "c100000", "c123456",    // past the range, by a sixth digit
            "0000001", "c0000001",   // the same, padded with leading zeros
            "1x", "014x", "c14x", "c99999x", "c1c", "cc1", "x", "c",  // trailing or leading junk
            "c 14", "c+14", "c-1",   // nothing the grammar has a place for
            "99999999999999999999",  // long enough to overflow an int if the
            "c99999999999999999999", // parser had no digit limit
        };
        for (const char *ref : refusals) {
            if (settingsui::swatchResolveRef(ref, library, unused)) {
                std::printf("MISMATCH ref %s: resolved, should not have\n", ref);
                g_failures++;
            }
        }
        // The ';' a map slot ends with is part of the grammar, not junk: the
        // renderer's own parser reads a ref out of bgAnimThemeMap with it
        // there, so a swatch must resolve what the panel draws from.
        settingsui::SwatchGradient viaSlot;
        if (!settingsui::swatchResolveRef("c14;", library, viaSlot)) {
            std::printf("MISMATCH ref c14;: refused a ref the renderer resolves\n");
            g_failures++;
        }
    }

    // The reason this file exists: drawing a swatch must not publish a theme.
    // The picker samples every row of a page; the active theme has to be what
    // it was before.
    {
        settingsui::SwatchGradient active;
        settingsui::swatchFromThemeStops(bg_theme_stops(0), active);
        bganim::setThemeStops(active.stops, active.count);
        bganim::setThemeTone(100 * 256 / 100, 100 * 255 / 100);
        const uint32_t genBefore = bganim::themeGen();
        uint8_t stopsBefore[BG_THEME_MAX_STOPS][3];
        std::memcpy(stopsBefore, bganim::themeStops(), sizeof(stopsBefore));

        for (const char *wire : kWires) {
            settingsui::SwatchGradient g;
            if (!settingsui::swatchFromWire(wire, g)) {
                continue;
            }
            settingsui::swatchApplyTone(g, 60, 55);
            uint16_t ramp[32];
            settingsui::swatchBuildRamp565(g, ramp, 32);
            (void)ramp;
        }
        if (bganim::themeGen() != genBefore) {
            std::printf("MISMATCH: sampling swatches moved the theme generation %u -> %u\n", genBefore,
                        bganim::themeGen());
            g_failures++;
        }
        if (std::memcmp(stopsBefore, bganim::themeStops(), sizeof(stopsBefore)) != 0) {
            std::printf("MISMATCH: sampling swatches changed the published theme stops\n");
            g_failures++;
        }
    }

    if (g_failures != 0) {
        std::printf("swatch_parity: FAIL (%d of %d ramp comparisons differ)\n", g_failures, g_compared);
        return 1;
    }
    std::printf("swatch_parity: PASS (%d ramps, all 256 entries exact)\n", g_compared);
    return 0;
}
