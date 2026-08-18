// Measures how much light each animation puts UNDER the overlay text, which is
// the "too bright to read" problem as a number rather than an impression.
//
// Ember was fixed by hand (its radius LUT is a reversed ramp, so index 0 was the
// brightest stop and the glow core sat exactly where the UI puts its readouts).
// That fix was per-animation and per-eyeball. This sweeps all 13 animations
// against all 18 themes through the LIVE theme-switch path -- setThemeStops()
// then frame(), which is what the running firmware does -- and reports the
// contrast a white readout would actually get.
//
// Metric: WCAG contrast ratio, (L1+0.05)/(L2+0.05) on linearized relative
// luminance, for white text over the measured background. Large text needs 3.0
// to be legible and 4.5 to be comfortable. The number that matters is not the
// mean but the high percentile: a bright blob drifting under a digit ruins the
// glyph even when the region averages fine, so p99 over the text disc across
// several timesteps is the headline and the mean is context.
//
// Text disc: r < 0.35 of the panel radius, a generous cover for centred
// readouts on the 480x480 round panel. r < 0.20 is reported too as the tight
// core, and the 0.60-1.00 ring as the periphery -- the center/edge ratio is what
// separates "this animation is bright everywhere" (a theme problem) from "this
// animation concentrates light in the middle" (a shape problem, ember's case).
//
// Build:
//   g++ -O2 -std=gnu++17 -Ishim -I. -include mathcount.h lumaprofile.cpp \
//       mathcount.cpp ../../src/display/ui/default/bganim/*.cpp -o build/lumaprofile -lm
// Usage: ./build/lumaprofile [--csv] [--profile <animId>]
#include "../../src/display/ui/default/bganim/BgAnim.h"
#include "../../src/display/ui/default/bganim/BgAnimCommon.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int W = 480, H = 480;
constexpr int BAND_H = 8; // production band height
const uint32_t TIMES[] = {1000, 3000, 8000, 20000, 45000};
constexpr int NT = sizeof(TIMES) / sizeof(TIMES[0]);

constexpr double R_CORE = 0.20, R_TEXT = 0.35, R_RING_LO = 0.60;

// sRGB -> linear, per WCAG.
double lin(double c) { return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }

double relLuma565(uint16_t p) {
    const double r = (((p >> 11) & 0x1F) * 255.0 / 31.0) / 255.0;
    const double g = (((p >> 5) & 0x3F) * 255.0 / 63.0) / 255.0;
    const double b = ((p & 0x1F) * 255.0 / 31.0) / 255.0;
    return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b);
}

double contrastWhite(double lbg) { return 1.05 / (lbg + 0.05); }

struct Acc {
    // 1024-bin histogram of relative luminance, for percentiles without storing pixels.
    long hist[1024] = {0};
    double sum = 0;
    long n = 0;
    void add(double l) {
        int b = static_cast<int>(l * 1023.0 + 0.5);
        if (b < 0) { b = 0; }
        if (b > 1023) { b = 1023; }
        hist[b]++;
        sum += l;
        n++;
    }
    double mean() const { return n ? sum / n : 0.0; }
    double pct(double p) const {
        if (n == 0) { return 0.0; }
        const long target = static_cast<long>(p * n);
        long c = 0;
        for (int i = 0; i < 1024; i++) {
            c += hist[i];
            if (c >= target) { return i / 1023.0; }
        }
        return 1.0;
    }
};

} // namespace

// Candidate remedies, applied to the theme stops before the animation sees
// them -- which is what a theme-level luminance control would do in firmware.
//   scale:    every stop * k. Uniform, trivially cheap, flattens saturation too.
//   shoulder: stops below the knee untouched, above it compressed toward the
//             knee. Keeps mid-tone colour and only bends the highlights, which
//             is where the unreadable pixels are.
enum Mode { M_NONE, M_SCALE, M_SHOULDER };

void applyMode(uint8_t stops[BG_THEME_MAX_STOPS][3], int n, Mode m, double k) {
    if (m == M_NONE) { return; }
    for (int i = 0; i < n; i++) {
        for (int c = 0; c < 3; c++) {
            double v = stops[i][c] / 255.0;
            if (m == M_SCALE) {
                v *= k;
            } else { // shoulder: knee at k, everything above compressed into [k, k + (1-k)*0.25]
                if (v > k) { v = k + (v - k) * 0.25; }
            }
            if (v < 0) { v = 0; }
            if (v > 1) { v = 1; }
            stops[i][c] = static_cast<uint8_t>(v * 255.0 + 0.5);
        }
    }
}

int main(int argc, char **argv) {
    bool csv = false;
    int profileAnim = -1;
    Mode mode = M_NONE;
    double k = 1.0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--csv") == 0) { csv = true; }
        else if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc) { profileAnim = atoi(argv[++i]); }
        else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) { mode = M_SCALE; k = atof(argv[++i]); }
        else if (strcmp(argv[i], "--shoulder") == 0 && i + 1 < argc) { mode = M_SHOULDER; k = atof(argv[++i]); }
    }

    const double cx = (W - 1) / 2.0, cy = (H - 1) / 2.0;
    const double panelR = W / 2.0;
    // Precompute the normalized radius of every pixel once.
    std::vector<float> rmap(static_cast<size_t>(W) * H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            const double dx = x - cx, dy = y - cy;
            rmap[static_cast<size_t>(y) * W + x] = static_cast<float>(std::sqrt(dx * dx + dy * dy) / panelR);
        }
    }

    std::vector<uint16_t> fb(static_cast<size_t>(W) * H);
    if (csv) { printf("anim,theme,core_p99,text_mean,text_p99,ring_mean,contrast_text_p99,center_edge_ratio\n"); }

    struct Row { std::string anim; std::string worstTheme; double textP99, textMean, ringMean, coreP99, minContrast, ratio; };
    std::vector<Row> rows;

    for (int id = 0; id < bg_animation_count(); id++) {
        const BgAnimation &anim = bg_animation(id);
        uint8_t p[4];
        bg_parse_params(nullptr, id, p);
        if (!anim.init(W, H)) { printf("%-10s INIT FAILED\n", anim.id); continue; }

        Row best{anim.id, "", -1, 0, 0, 0, 1e9, 0};
        for (int t = 0; t < bg_theme_count(); t++) {
            uint8_t stops[BG_THEME_MAX_STOPS][3];
            int nStops = 0;
            bg_resolve_theme(t, nullptr, stops, nStops);
            applyMode(stops, nStops, mode, k);
            bganim::setThemeStops(stops, nStops);

            Acc core, text, ring;
            for (int f = 0; f < NT; f++) {
                anim.frame(TIMES[f], W, H, p);
                for (int y = 0; y < H; y += BAND_H) {
                    anim.band(fb.data() + static_cast<size_t>(y) * W, y, BAND_H, W, TIMES[f], p);
                }
                for (size_t i = 0; i < fb.size(); i++) {
                    const double r = rmap[i];
                    if (r > 1.0) { continue; } // outside the round panel; never visible
                    const double l = relLuma565(fb[i]);
                    if (r < R_CORE) { core.add(l); }
                    if (r < R_TEXT) { text.add(l); }
                    if (r >= R_RING_LO) { ring.add(l); }
                }
            }
            const double tp99 = text.pct(0.99);
            const double c = contrastWhite(tp99);
            const double ratio = ring.mean() > 1e-6 ? text.mean() / ring.mean() : 0.0;
            if (csv) {
                printf("%s,%s,%.4f,%.4f,%.4f,%.4f,%.2f,%.2f\n", anim.id, bg_theme_name(t), core.pct(0.99),
                       text.mean(), tp99, ring.mean(), c, ratio);
            }
            if (profileAnim == id) {
                printf("  %-14s text_p99 %.4f  contrast %.2f  c/e %.2f\n", bg_theme_name(t), tp99, c, ratio);
            }
            if (c < best.minContrast) {
                best = {anim.id, bg_theme_name(t), tp99, text.mean(), ring.mean(), core.pct(0.99), c, ratio};
            }
        }
        rows.push_back(best);
        if (anim.release != nullptr) { anim.release(); }
    }

    if (csv) { return 0; }

    printf("\nWorst theme per animation. Contrast is for WHITE text over text_p99.\n");
    printf("Bar: 3.0 legible for large text, 4.5 comfortable. c/e = text_mean / ring_mean.\n\n");
    printf("%-10s %-13s %8s %8s %8s %8s %6s  %s\n", "anim", "worst theme", "core_p99", "txt_mean", "txt_p99",
           "ring_mean", "c/e", "contrast");
    for (const Row &r : rows) {
        const char *verdict = r.minContrast >= 4.5 ? "" : (r.minContrast >= 3.0 ? "  THIN" : "  FAIL");
        printf("%-10s %-13s %8.4f %8.4f %8.4f %8.4f %6.2f  %6.2f%s\n", r.anim.c_str(), r.worstTheme.c_str(),
               r.coreP99, r.textMean, r.textP99, r.ringMean, r.ratio, r.minContrast, verdict);
    }
    return 0;
}
