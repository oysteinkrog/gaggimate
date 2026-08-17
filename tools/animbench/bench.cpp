// animbench — host-side profiler + golden-image harness for the bganim fleet.
//
//   ./bench                          benchmark all animations, print table
//   ./bench --anim 4                 benchmark one animation (registry id)
//   ./bench --frames 240             frames per animation (default 240)
//   ./bench --golden DIR             also write reference PPMs (frames 30/120/210)
//   ./bench --compare DIR            render those frames, diff vs stored PPMs
//   ./bench --dump DIR               write PPMs without treating them as golden
//
// Timing is host wall-clock (x86), so absolute numbers do NOT transfer to the
// ESP32-S3 — but relative per-animation cost and the libm call counts do. On
// device the budget is ~34 CPU cycles/pixel total (480x480 @ 30 fps @ 240 MHz);
// a single per-pixel sinf (~150 cy soft-float) blows it 4x on its own.
#include "../../src/display/ui/default/bganim/BgAnim.h"
#include "../../src/display/ui/default/bganim/BgAnimCommon.h"
#include "mathcount.h"

#include <chrono>
#include <string.h>
#include <string>
#include <vector>

namespace {

constexpr int W = 480;
constexpr int H = 480;
// Must match SleepAnimation.cpp's BAND_H. band_ms below times a whole frame's
// worth of band() calls, so the pixel count is identical at any band height and
// only the per-call terms change -- but they change by more than the harness
// noise, and NOT in a consistent direction, so a mismatched height here quietly
// biases individual animations either way. Measured 8 vs 16 on this host, five
// runs each, tight enough to separate from the 2-9% run-to-run spread:
//
//   aurora   0.204 -> 0.222  (+9%)   fixed per-call cost dominates
//   caustics 0.130 -> 0.136  (+5%)
//   nebula   0.288 -> 0.294  (+2%)
//   lava     0.269 -> 0.250  (-8%)   working-set locality dominates
//   ember    0.175 -> 0.166  (-5%)
//
// The negative rows are the reason this is worth pinning: halving the band also
// halves the live output window (8 x 480 x 2 = 7.7 KB vs 15.4 KB), and for the
// animations with a large per-row working set that cache win outweighs paying
// the per-call overhead twice as often.
//
// This drifted once. It was written as 16 when 16 was production, then 7fede9c9
// ("perf(sleep-anim): BAND_H 16 -> 8") changed the device 5.5 hours later and
// this constant was not updated, so numbers taken between then and this fix are
// off by the amounts above. BASELINE.md and the ~x80 calibration predate the
// drift and are consistent with the 16 they were measured at. Overridable so an
// old number can still be reproduced: -DGM_BENCH_BAND_H=16.
#ifndef GM_BENCH_BAND_H
#define GM_BENCH_BAND_H 8
#endif
constexpr int BAND_H = GM_BENCH_BAND_H;
constexpr uint32_t FRAME_MS = 33;
const int GOLDEN_FRAMES[] = {30, 120, 210};

double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

void writePpm(const std::string &path, const uint16_t *fb) {
    FILE *f = fopen(path.c_str(), "wb");
    if (f == nullptr) {
        fprintf(stderr, "cannot write %s\n", path.c_str());
        exit(1);
    }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        const uint16_t c = fb[i];
        // RGB565 -> RGB888 with bit replication (same as a viewer would).
        uint8_t px[3] = {static_cast<uint8_t>(((c >> 11) & 0x1F) * 255 / 31),
                         static_cast<uint8_t>(((c >> 5) & 0x3F) * 255 / 63),
                         static_cast<uint8_t>((c & 0x1F) * 255 / 31)};
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

// Returns {meanAbsDiff, maxAbsDiff} over RGB888 channels, or {-1,-1} if the
// reference is missing/mismatched.
struct Diff {
    double mean;
    int max;
};
Diff diffPpm(const std::string &path, const uint16_t *fb) {
    FILE *f = fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return {-1, -1};
    }
    int w = 0, h = 0, maxv = 0;
    if (fscanf(f, "P6 %d %d %d", &w, &h, &maxv) != 3 || w != W || h != H) {
        fclose(f);
        return {-1, -1};
    }
    fgetc(f); // single whitespace after header
    double sum = 0;
    int maxd = 0;
    for (int i = 0; i < W * H; i++) {
        uint8_t ref[3];
        if (fread(ref, 1, 3, f) != 3) {
            fclose(f);
            return {-1, -1};
        }
        const uint16_t c = fb[i];
        const int cur[3] = {((c >> 11) & 0x1F) * 255 / 31, ((c >> 5) & 0x3F) * 255 / 63, (c & 0x1F) * 255 / 31};
        for (int k = 0; k < 3; k++) {
            const int d = abs(cur[k] - ref[k]);
            sum += d;
            if (d > maxd) {
                maxd = d;
            }
        }
    }
    fclose(f);
    return {sum / (W * H * 3.0), maxd};
}

unsigned long long libmTotal() {
    const auto &g = mathcount::g;
    return g.sinf_n + g.cosf_n + g.sqrtf_n + g.expf_n + g.exp2f_n + g.powf_n + g.atan2f_n + g.fmodf_n + g.logf_n +
           g.floorf_n + g.tanhf_n + g.lroundf_n;
}

// Weighted soft-float cost estimate (device cycles) of the counted libm calls.
unsigned long long libmDeviceCycles() {
    const auto &g = mathcount::g;
    return g.sinf_n * 150 + g.cosf_n * 150 + g.sqrtf_n * 90 + g.expf_n * 200 + g.exp2f_n * 150 + g.powf_n * 400 +
           g.atan2f_n * 300 + g.fmodf_n * 40 + g.logf_n * 200 + g.floorf_n * 40 + g.tanhf_n * 300 + g.lroundf_n * 40;
}

} // namespace

int main(int argc, char **argv) {
    int onlyAnim = -1;
    int frames = 240;
    std::string goldenDir, compareDir, dumpDir;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--anim" && i + 1 < argc) {
            onlyAnim = atoi(argv[++i]);
        } else if (a == "--frames" && i + 1 < argc) {
            frames = atoi(argv[++i]);
        } else if (a == "--golden" && i + 1 < argc) {
            goldenDir = argv[++i];
        } else if (a == "--compare" && i + 1 < argc) {
            compareDir = argv[++i];
        } else if (a == "--dump" && i + 1 < argc) {
            dumpDir = argv[++i];
        } else {
            fprintf(stderr, "unknown arg %s\n", a.c_str());
            return 1;
        }
    }

    std::vector<uint16_t> fb(W * H);
    printf("%-11s %9s %9s %9s %12s %14s %s\n", "anim", "frame_ms", "band_ms", "total_ms", "libm/frame", "est_dev_ms*",
           "worst libm (per frame)");

    for (int id = 0; id < bg_animation_count(); id++) {
        if (onlyAnim >= 0 && id != onlyAnim) {
            continue;
        }
        const BgAnimation &anim = bg_animation(id);
        uint8_t p[4] = {anim.params[0].def, anim.params[1].def, anim.params[2].def, anim.params[3].def};
        if (!anim.init(W, H)) {
            printf("%-11s INIT FAILED\n", anim.id);
            continue;
        }
        // Warm-up frame builds lazy LUTs outside the timed region.
        anim.frame(0, W, H, p);
        anim.band(fb.data(), 0, BAND_H, W, 0, p);

        mathcount::reset();
        double frameMs = 0, bandMs = 0;
        for (int i = 0; i < frames; i++) {
            const uint32_t t = 1000 + i * FRAME_MS;
            double t0 = nowMs();
            anim.frame(t, W, H, p);
            frameMs += nowMs() - t0;
            t0 = nowMs();
            for (int y0 = 0; y0 < H; y0 += BAND_H) {
                anim.band(fb.data() + static_cast<size_t>(y0) * W, y0, BAND_H, W, t, p);
            }
            bandMs += nowMs() - t0;

            for (int gf : GOLDEN_FRAMES) {
                if (i != gf) {
                    continue;
                }
                char name[64];
                snprintf(name, sizeof(name), "/%s-%03d.ppm", anim.id, gf);
                if (!goldenDir.empty()) {
                    writePpm(goldenDir + name, fb.data());
                }
                if (!dumpDir.empty()) {
                    writePpm(dumpDir + name, fb.data());
                }
                if (!compareDir.empty()) {
                    const Diff d = diffPpm(compareDir + name, fb.data());
                    printf("  diff %-9s f%03d: mean %.3f max %d %s\n", anim.id, gf, d.mean, d.max,
                           d.mean < 0 ? "(NO REFERENCE)" : (d.mean <= 3.0 && d.max <= 48 ? "OK" : "CHECK VISUALLY"));
                }
            }
        }

        const auto &g = mathcount::g;
        struct {
            const char *n;
            unsigned long long v;
        } worst[] = {{"sinf", g.sinf_n}, {"cosf", g.cosf_n},     {"sqrtf", g.sqrtf_n}, {"expf", g.expf_n},
                     {"exp2f", g.exp2f_n}, {"powf", g.powf_n},   {"atan2f", g.atan2f_n}, {"fmodf", g.fmodf_n},
                     {"logf", g.logf_n},   {"floorf", g.floorf_n}, {"tanhf", g.tanhf_n}, {"lroundf", g.lroundf_n}};
        std::string top;
        for (int k = 0; k < 3; k++) {
            int best = -1;
            for (int j = 0; j < 12; j++) {
                if (worst[j].v > 0 && (best < 0 || worst[j].v > worst[best].v)) {
                    best = j;
                }
            }
            if (best < 0) {
                break;
            }
            char buf[48];
            snprintf(buf, sizeof(buf), "%s%s=%.0f", top.empty() ? "" : " ", worst[best].n,
                     static_cast<double>(worst[best].v) / frames);
            top += buf;
            worst[best].v = 0;
        }

        // est_dev_ms: libm soft-float cost alone, converted to ms at 240 MHz.
        // A lower bound on device frame cost — ALU/memory work comes on top.
        const double estDevMs = static_cast<double>(libmDeviceCycles()) / frames / 240000.0;
        printf("%-11s %9.3f %9.3f %9.3f %12.0f %13.2f  %s\n", anim.id, frameMs / frames, bandMs / frames,
               (frameMs + bandMs) / frames, static_cast<double>(libmTotal()) / frames, estDevMs,
               top.empty() ? "-" : top.c_str());
    }
    return 0;
}
