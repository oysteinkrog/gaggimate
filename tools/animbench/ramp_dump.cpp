// ramp_dump: the firmware's own palette arithmetic, on the host.
//
// This links the real BgAnimThemes.cpp and BgAnimCommon.cpp, so whatever it
// prints is what the panel would store. It exists so that the JS sampler in
// web/src/config/gradientRamp.js can be proved equal to the firmware rather
// than assumed equal (tools/animbench/web/ramp_parity.js), and so a device
// comparison has a host-side expectation to compare against.
//
// It is a test oracle. Nothing in the firmware or the build depends on it.
//
// Build:  make build/ramp_dump
// Usage:  one query per stdin line, one output line each, in order:
//
//   <wire> <brightnessPct> <kneePct> <gain256> <ramp|wheel|reversed>
//
// <wire> is the gradient string as the settings hold it, "rrggbb[@pos],..."
// with no spaces. The percentages are the two settings as DefaultUI converts
// them (0..100). gain256 is an animation's extra palette gain, 256 for none.
// Output is 256 decimal RGB565 values separated by spaces, or "ERR" if the
// wire string does not parse.

#include "../../src/display/ui/default/bganim/BgAnim.h"
#include "../../src/display/ui/default/bganim/BgAnimCommon.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int main() {
    std::string line;
    char buf[8192];
    while (fgets(buf, sizeof(buf), stdin) != nullptr) {
        line = buf;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        char wire[2048] = {0};
        int brightPct = 100;
        int kneePct = 100;
        int gain = 256;
        char mode[32] = {0};
        if (sscanf(line.c_str(), "%2047s %d %d %d %31s", wire, &brightPct, &kneePct, &gain, mode) != 5) {
            printf("ERR\n");
            continue;
        }
        uint8_t stops[BG_THEME_MAX_STOPS][3];
        uint8_t pos[BG_THEME_MAX_STOPS];
        bool uniform = true;
        const int n = bg_parse_gradient(wire, stops, pos, uniform);
        if (n == 0) {
            printf("ERR\n");
            continue;
        }
        if (uniform) {
            bganim::setThemeStops(stops, n);
        } else {
            bganim::setThemeStopsPos(stops, pos, n);
        }
        // The same conversion DefaultUI applies on every UI pass.
        bganim::setThemeTone(brightPct * 256 / 100, kneePct * 255 / 100);
        // The shape every animation's frame() has: themeGen() releases the
        // theme held by the previous reads and adopts the newest publish, so
        // the build below reads this case's stops and not the first case's
        // (BgAnimCommon.cpp, the held-generation rule; without this call the
        // dump read one stale theme for all 5,184 cases after the merge).
        (void)bganim::themeGen();
        uint16_t ramp[256];
        if (strcmp(mode, "wheel") == 0) {
            bganim::buildThemeWheel(ramp, static_cast<uint16_t>(gain));
        } else {
            bganim::buildThemeRamp(ramp, static_cast<uint16_t>(gain), strcmp(mode, "reversed") == 0);
        }
        for (int i = 0; i < 256; i++) {
            printf(i == 255 ? "%u\n" : "%u ", ramp[i]);
        }
        fflush(stdout);
    }
    return 0;
}
