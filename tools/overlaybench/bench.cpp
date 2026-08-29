// Host-side microbenchmark + golden-correctness harness for the four
// measured hot kernels of SleepAnimation's overlay pipeline (span scan,
// scrim build, overlay blend, half-res expand). Modeled on
// tools/animbench/bench.cpp's golden/compare pattern, but the goldens here
// are raw binary dumps of kernel output (run tables, cell grids, band
// buffers) rather than PPM images -- these kernels have no meaningful
// standalone visual, and "bit-exact vs golden" is the actual contract.
//
//   ./build/bench                       benchmark all kernels, print table
//   ./build/bench --iters 200           timed repeats per kernel (default 200)
//   ./build/bench --golden DIR          write reference outputs (all ref_* variants)
//   ./build/bench --compare DIR         diff every registered variant against golden
//                                       (bit-exact required unless piePending)
//   ./build/bench --dump DIR            write outputs without treating them as golden
//   ./build/bench --list                list every registered variant
//
// Host ns are wall-clock (x86) and do NOT transfer to the ESP32-S3 directly.
// Calibration (see tools/animbench/BASELINE.md, same convention): pure-ALU
// host-ns x ~80 =~ device ns, i.e. device_cycles/px =~ host_ns/px x 19.2 at
// 240 MHz. That calibration assumes no cache misses; every kernel here reads
// PSRAM-resident buffers on device, so BASELINE-OVERLAY.md adds a separate
// PSRAM line-miss term instead of folding it into this multiplier.
#include "common.h"
#include "gen_inputs.h"
#include "kernels/halfres_expand.h"
#include "kernels/overlay_blend.h"
#include "kernels/scrim_build.h"
#include "kernels/span_scan.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace ovb;

namespace {

constexpr uint32_t SEED = 0xC0FFEEu;
constexpr int SCRIM_Q8_DEFAULT = 200; // a representative "on" scrim strength

double nowNs() {
    using namespace std::chrono;
    return duration<double, std::nano>(steady_clock::now().time_since_epoch()).count();
}

bool writeFile(const std::string &path, const void *data, size_t n) {
    FILE *f = fopen(path.c_str(), "wb");
    if (f == nullptr) {
        fprintf(stderr, "cannot write %s\n", path.c_str());
        return false;
    }
    fwrite(data, 1, n, f);
    fclose(f);
    return true;
}

// -1 = missing reference, 0 = mismatch, 1 = match.
int compareFile(const std::string &path, const void *data, size_t n) {
    FILE *f = fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return -1;
    }
    std::vector<uint8_t> ref(n);
    const size_t got = fread(ref.data(), 1, n, f);
    fclose(f);
    if (got != n) {
        return -1;
    }
    return memcmp(ref.data(), data, n) == 0 ? 1 : 0;
}

// device cycle estimate for pure-ALU host time, per the calibration above.
double devCyclesPerPx(double hostNsPerPx) { return hostNsPerPx * 19.2; }

struct KernelReport {
    const char *kernel;
    const char *variant;
    double nsPerCall;
    double nsPerPx;
    long long calls;
    long long px;
};

std::vector<KernelReport> g_reports;

void printReport(const KernelReport &r) {
    printf("%-12s %-14s %10.1f ns/call %10.3f ns/px %10.2f est_dev_cy/px\n", r.kernel, r.variant, r.nsPerCall,
           r.nsPerPx, devCyclesPerPx(r.nsPerPx));
}

// ---------------------------------------------------------------------------
// Shared fixtures, generated once and reused (read-only) by every kernel's
// variants. Downstream kernels always consume the REFERENCE output of the
// upstream kernel, regardless of which variant of that upstream kernel is
// currently being timed -- this pipeline-chains the same way the firmware
// does (scan -> scrim -> blend, expand -> blend) without letting a faulty
// upstream variant corrupt what a downstream kernel is scored against.
struct Fixtures {
    SyntheticOverlay overlay;
    std::vector<uint32_t> refRuns;    // PANEL_H * RUNS_PER_ROW
    std::vector<uint8_t> refRunN;     // PANEL_H
    std::vector<uint8_t> refScrimSrc; // SCRIM_W*SCRIM_H, side effect of the scan
    std::vector<uint8_t> refScrimOut; // SCRIM_W*SCRIM_H, buildScrim's halo grid
    std::vector<uint32_t> refHaloRuns; // SCRIM_H * RUNS_PER_ROW
    std::vector<uint8_t> refHaloN;     // SCRIM_H
    std::vector<uint16_t> halfRes;     // (PANEL_W/2)*(PANEL_H/2)
    std::vector<uint16_t> refBand;     // PANEL_W*PANEL_H, expanded from halfRes
};

Fixtures buildFixtures() {
    Fixtures fx;
    fx.overlay = genSyntheticOverlay(SEED);

    fx.refRuns.assign(static_cast<size_t>(PANEL_H) * RUNS_PER_ROW, 0);
    fx.refRunN.assign(PANEL_H, 0);
    fx.refScrimSrc.assign(static_cast<size_t>(SCRIM_W) * SCRIM_H, 0);
    for (int y = 0; y < PANEL_H; y++) {
        uint32_t *rowRuns = fx.refRuns.data() + static_cast<size_t>(y) * RUNS_PER_ROW;
        uint8_t *cellRow = fx.refScrimSrc.data() + static_cast<size_t>(y >> SCRIM_SHIFT) * SCRIM_W;
        const int n = scanRow_ref(fx.overlay.panelRowAlpha(y), PANEL_W, rowRuns, cellRow);
        fx.refRunN[y] = static_cast<uint8_t>(n);
    }

    fx.refScrimOut.assign(static_cast<size_t>(SCRIM_W) * SCRIM_H, 0);
    fx.refHaloRuns.assign(static_cast<size_t>(SCRIM_H) * RUNS_PER_ROW, 0);
    fx.refHaloN.assign(SCRIM_H, 0);
    {
        std::vector<uint8_t> tmp(static_cast<size_t>(SCRIM_W) * SCRIM_H);
        buildScrim_ref(fx.refScrimSrc.data(), tmp.data(), fx.refScrimOut.data(), SCRIM_W, SCRIM_H, SCRIM_Q8_DEFAULT,
                      fx.refHaloRuns.data(), fx.refHaloN.data());
    }

    fx.halfRes = genSyntheticHalfRes(SEED);
    fx.refBand.assign(static_cast<size_t>(PANEL_W) * PANEL_H, 0);
    {
        constexpr int HW = PANEL_W / 2;
        constexpr int HH = PANEL_H / 2;
        for (int sr = 0; sr < HH; sr++) {
            const uint16_t *src = fx.halfRes.data() + static_cast<size_t>(sr) * HW;
            uint16_t *row0 = fx.refBand.data() + static_cast<size_t>(sr * 2) * PANEL_W;
            uint16_t *row1 = row0 + PANEL_W;
            expandRow_ref(src, row0, row1, HW);
        }
    }
    return fx;
}

// ---------------------------------------------------------------------------
// Kernel 1: span scan. Times scanning all PANEL_H rows (a whole publish's
// worth of scan work) per iteration.
void runSpanScan(const Fixtures &fx, int iters, const std::string &goldenDir, const std::string &compareDir,
                 const std::string &dumpDir, int *compareFailures) {
    std::vector<uint32_t> runs(static_cast<size_t>(PANEL_H) * RUNS_PER_ROW);
    std::vector<uint8_t> runN(PANEL_H);
    std::vector<uint8_t> scrimSrc(static_cast<size_t>(SCRIM_W) * SCRIM_H);

    for (int vi = 0; vi < kScanRowVariantCount; vi++) {
        const auto &variant = kScanRowVariants[vi];
        std::fill(runs.begin(), runs.end(), 0u);
        std::fill(runN.begin(), runN.end(), 0u);
        std::fill(scrimSrc.begin(), scrimSrc.end(), 0u);

        double totalNs = 0;
        for (int it = 0; it < iters; it++) {
            std::fill(scrimSrc.begin(), scrimSrc.end(), 0u); // one publish = fresh cell rows
            const double t0 = nowNs();
            for (int y = 0; y < PANEL_H; y++) {
                uint32_t *rowRuns = runs.data() + static_cast<size_t>(y) * RUNS_PER_ROW;
                uint8_t *cellRow = scrimSrc.data() + static_cast<size_t>(y >> SCRIM_SHIFT) * SCRIM_W;
                const int n = variant.fn(fx.overlay.panelRowAlpha(y), PANEL_W, rowRuns, cellRow);
                runN[y] = static_cast<uint8_t>(n);
            }
            totalNs += nowNs() - t0;
        }
        const double nsPerCall = totalNs / iters;         // one full-frame scan
        const double nsPerPx = nsPerCall / (PANEL_W * PANEL_H);
        g_reports.push_back({"span_scan", variant.name, nsPerCall, nsPerPx, iters, PANEL_W * PANEL_H});

        // golden covers runs + runN + the derived scrimSrc, concatenated.
        std::vector<uint8_t> blob;
        blob.resize(runs.size() * sizeof(uint32_t) + runN.size() + scrimSrc.size());
        uint8_t *p = blob.data();
        memcpy(p, runs.data(), runs.size() * sizeof(uint32_t));
        p += runs.size() * sizeof(uint32_t);
        memcpy(p, runN.data(), runN.size());
        p += runN.size();
        memcpy(p, scrimSrc.data(), scrimSrc.size());

        const std::string name = "/span_scan-" + std::string(variant.name) + ".bin";
        if (!goldenDir.empty()) {
            writeFile(goldenDir + name, blob.data(), blob.size());
        }
        if (!dumpDir.empty()) {
            writeFile(dumpDir + name, blob.data(), blob.size());
        }
        if (!compareDir.empty()) {
            const int ok = compareFile(compareDir + name, blob.data(), blob.size());
            printf("  golden span_scan/%-12s %s\n", variant.name,
                  ok < 0 ? "NO REFERENCE" : (ok ? "OK" : "MISMATCH"));
            if (variant.bitExactRequired && ok <= 0) {
                (*compareFailures)++;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Kernel 2: scrim build. Times one whole-grid rebuild (all seven passes) per
// iteration, from the fixture's derived scrimSrc.
void runScrimBuild(const Fixtures &fx, int iters, const std::string &goldenDir, const std::string &compareDir,
                   const std::string &dumpDir, int *compareFailures) {
    std::vector<uint8_t> tmp(static_cast<size_t>(SCRIM_W) * SCRIM_H);
    std::vector<uint8_t> out(static_cast<size_t>(SCRIM_W) * SCRIM_H);
    std::vector<uint32_t> haloRuns(static_cast<size_t>(SCRIM_H) * RUNS_PER_ROW);
    std::vector<uint8_t> haloN(SCRIM_H);

    for (int vi = 0; vi < kBuildScrimVariantCount; vi++) {
        const auto &variant = kBuildScrimVariants[vi];

        double totalNs = 0;
        for (int it = 0; it < iters; it++) {
            const double t0 = nowNs();
            variant.fn(fx.refScrimSrc.data(), tmp.data(), out.data(), SCRIM_W, SCRIM_H, SCRIM_Q8_DEFAULT,
                      haloRuns.data(), haloN.data());
            totalNs += nowNs() - t0;
        }
        const double nsPerCall = totalNs / iters; // one whole-grid rebuild
        const long long cells = static_cast<long long>(SCRIM_W) * SCRIM_H;
        const double nsPerPx = nsPerCall / (cells * (1 << SCRIM_SHIFT) * (1 << SCRIM_SHIFT)); // per panel px covered
        g_reports.push_back({"scrim_build", variant.name, nsPerCall, nsPerPx, iters,
                             cells * (1 << SCRIM_SHIFT) * (1 << SCRIM_SHIFT)});

        std::vector<uint8_t> blob;
        blob.resize(out.size() + haloRuns.size() * sizeof(uint32_t) + haloN.size());
        uint8_t *p = blob.data();
        memcpy(p, out.data(), out.size());
        p += out.size();
        memcpy(p, haloRuns.data(), haloRuns.size() * sizeof(uint32_t));
        p += haloRuns.size() * sizeof(uint32_t);
        memcpy(p, haloN.data(), haloN.size());

        const std::string name = "/scrim_build-" + std::string(variant.name) + ".bin";
        if (!goldenDir.empty()) {
            writeFile(goldenDir + name, blob.data(), blob.size());
        }
        if (!dumpDir.empty()) {
            writeFile(dumpDir + name, blob.data(), blob.size());
        }
        if (!compareDir.empty()) {
            const int ok = compareFile(compareDir + name, blob.data(), blob.size());
            printf("  golden scrim_build/%-12s %s\n", variant.name, ok < 0 ? "NO REFERENCE" : (ok ? "OK" : "MISMATCH"));
            if (variant.bitExactRequired && ok <= 0) {
                (*compareFailures)++;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Kernel 3: overlay blend. Times the two-pass row body over all PANEL_H rows
// of a fresh band-buffer copy per iteration (the scrim's per-cell-row inputs
// and the overlay's per-row glyph runs both come from the REFERENCE upstream
// output, so every variant here is scored on identical inputs).
void runOverlayBlend(const Fixtures &fx, int iters, const std::string &goldenDir, const std::string &compareDir,
                     const std::string &dumpDir, int *compareFailures) {
    std::vector<uint16_t> scratch(fx.refBand.size());

    for (int vi = 0; vi < kBlendStageVariantCount; vi++) {
        const auto &variant = kBlendStageVariants[vi];

        double totalNs = 0;
        for (int it = 0; it < iters; it++) {
            scratch = fx.refBand; // untimed: fresh band per publish, like the device
            const double t0 = nowNs();
            for (int y = 0; y < PANEL_H; y++) {
                const int cy = y >> SCRIM_SHIFT;
                const uint8_t *invRow = fx.refScrimOut.data() + static_cast<size_t>(cy) * SCRIM_W;
                const uint32_t *haloRuns = fx.refHaloRuns.data() + static_cast<size_t>(cy) * RUNS_PER_ROW;
                const int nHalo = fx.refHaloN[cy];
                const uint8_t *colour = fx.overlay.panelRowStart(y);
                const uint32_t *runs = fx.refRuns.data() + static_cast<size_t>(y) * RUNS_PER_ROW;
                const int nRuns = fx.refRunN[y];
                uint16_t *dst = scratch.data() + static_cast<size_t>(y) * PANEL_W;
                variant.fn(dst, invRow, haloRuns, nHalo, colour, runs, nRuns, PANEL_W);
            }
            totalNs += nowNs() - t0;
        }
        const double nsPerCall = totalNs / iters; // one whole-frame blend
        const double nsPerPx = nsPerCall / (PANEL_W * PANEL_H);
        g_reports.push_back({"overlay_blend", variant.name, nsPerCall, nsPerPx, iters, PANEL_W * PANEL_H});

        const std::string name = "/overlay_blend-" + std::string(variant.name) + ".bin";
        const size_t bytes = scratch.size() * sizeof(uint16_t);
        if (!goldenDir.empty()) {
            writeFile(goldenDir + name, scratch.data(), bytes);
        }
        if (!dumpDir.empty()) {
            writeFile(dumpDir + name, scratch.data(), bytes);
        }
        if (!compareDir.empty()) {
            const int ok = compareFile(compareDir + name, scratch.data(), bytes);
            printf("  golden overlay_blend/%-12s %s\n", variant.name,
                  ok < 0 ? "NO REFERENCE" : (ok ? "OK" : "MISMATCH"));
            if (variant.bitExactRequired && ok <= 0) {
                (*compareFailures)++;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Kernel 4: half-res expand. Times expanding all PANEL_H/2 source rows into
// a fresh full-resolution band buffer per iteration.
void runHalfresExpand(const Fixtures &fx, int iters, const std::string &goldenDir, const std::string &compareDir,
                      const std::string &dumpDir, int *compareFailures) {
    constexpr int HW = PANEL_W / 2;
    constexpr int HH = PANEL_H / 2;
    std::vector<uint16_t> out(static_cast<size_t>(PANEL_W) * PANEL_H);

    for (int vi = 0; vi < kExpandRowVariantCount; vi++) {
        const auto &variant = kExpandRowVariants[vi];

        double totalNs = 0;
        for (int it = 0; it < iters; it++) {
            const double t0 = nowNs();
            for (int sr = 0; sr < HH; sr++) {
                const uint16_t *src = fx.halfRes.data() + static_cast<size_t>(sr) * HW;
                uint16_t *row0 = out.data() + static_cast<size_t>(sr * 2) * PANEL_W;
                uint16_t *row1 = row0 + PANEL_W;
                variant.fn(src, row0, row1, HW);
            }
            totalNs += nowNs() - t0;
        }
        const double nsPerCall = totalNs / iters; // one whole-frame expand
        const double nsPerPx = nsPerCall / (PANEL_W * PANEL_H);
        g_reports.push_back({"halfres_expand", variant.name, nsPerCall, nsPerPx, iters, PANEL_W * PANEL_H});

        const std::string name = "/halfres_expand-" + std::string(variant.name) + ".bin";
        const size_t bytes = out.size() * sizeof(uint16_t);
        if (!goldenDir.empty()) {
            writeFile(goldenDir + name, out.data(), bytes);
        }
        if (!dumpDir.empty()) {
            writeFile(dumpDir + name, out.data(), bytes);
        }
        if (!compareDir.empty()) {
            const int ok = compareFile(compareDir + name, out.data(), bytes);
            printf("  golden halfres_expand/%-12s %s\n", variant.name,
                  ok < 0 ? "NO REFERENCE" : (ok ? "OK" : "MISMATCH"));
            if (variant.bitExactRequired && ok <= 0) {
                (*compareFailures)++;
            }
        }
    }
}

void listVariants() {
    printf("span_scan:\n");
    for (int i = 0; i < kScanRowVariantCount; i++) {
        printf("  %s%s\n", kScanRowVariants[i].name, kScanRowVariants[i].piePending ? " (PIE, device-pending)" : "");
    }
    printf("scrim_build:\n");
    for (int i = 0; i < kBuildScrimVariantCount; i++) {
        printf("  %s%s\n", kBuildScrimVariants[i].name, kBuildScrimVariants[i].piePending ? " (PIE, device-pending)" : "");
    }
    printf("overlay_blend:\n");
    for (int i = 0; i < kBlendStageVariantCount; i++) {
        printf("  %s%s\n", kBlendStageVariants[i].name, kBlendStageVariants[i].piePending ? " (PIE, device-pending)" : "");
    }
    printf("halfres_expand:\n");
    for (int i = 0; i < kExpandRowVariantCount; i++) {
        printf("  %s%s\n", kExpandRowVariants[i].name, kExpandRowVariants[i].piePending ? " (PIE, device-pending)" : "");
    }
}

} // namespace

int main(int argc, char **argv) {
    int iters = 200;
    std::string goldenDir, compareDir, dumpDir;
    bool doList = false;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--iters" && i + 1 < argc) {
            iters = atoi(argv[++i]);
        } else if (a == "--golden" && i + 1 < argc) {
            goldenDir = argv[++i];
        } else if (a == "--compare" && i + 1 < argc) {
            compareDir = argv[++i];
        } else if (a == "--dump" && i + 1 < argc) {
            dumpDir = argv[++i];
        } else if (a == "--list") {
            doList = true;
        } else {
            fprintf(stderr, "unknown arg %s\n", a.c_str());
            return 1;
        }
    }

    if (doList) {
        listVariants();
        return 0;
    }

    const Fixtures fx = buildFixtures();
    int compareFailures = 0;

    runSpanScan(fx, iters, goldenDir, compareDir, dumpDir, &compareFailures);
    runScrimBuild(fx, iters, goldenDir, compareDir, dumpDir, &compareFailures);
    runOverlayBlend(fx, iters, goldenDir, compareDir, dumpDir, &compareFailures);
    runHalfresExpand(fx, iters, goldenDir, compareDir, dumpDir, &compareFailures);

    printf("\n%-12s %-14s %14s %14s %16s\n", "kernel", "variant", "ns/call", "ns/px", "est_dev_cy/px*");
    for (const auto &r : g_reports) {
        printReport(r);
    }
    printf("\n* pure-ALU estimate (host_ns/px x 19.2, the animbench x80-at-240MHz\n");
    printf("  calibration). Add PSRAM line-miss cost separately -- see BASELINE-OVERLAY.md.\n");

    if (!compareDir.empty()) {
        printf("\n%s (%d failing)\n", compareFailures == 0 ? "GOLDENS OK" : "GOLDEN MISMATCH", compareFailures);
    }
    return compareFailures == 0 ? 0 : 1;
}
