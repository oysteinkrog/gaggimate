// The blend probe (GM_BLEND_PROBE: the bench and loadtest builds): levels
// 1 to 9 of /api/debug/anim?probe=, which take the composite's row apart
// into what it computes and what it waits on. The render loop calls
// probeBlendRow through the hook in SleepAnimationProbe.h; the kernels it
// runs stay in SleepAnimation.cpp behind the probeKernel* wrappers.
#ifndef GAGGIMATE_SIM

// The header defines GM_BLEND_PROBE from the bench and touch-probe flags, so
// it has to come before the test on it.
#include "SleepAnimation.h"
#ifdef GM_BLEND_PROBE

#include "SleepAnimationInternal.h"
#include "SleepAnimationProbe.h"
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <string.h>

namespace {
// Probe builds: the blend walk with its work removed, so the pixel loop can be
// split into what it computes and what it waits on.
//   2 -- read the coverage byte and nothing else (the PSRAM load on its own)
//   3 -- coverage plus the band read-modify-write (adds the SRAM traffic)
// Returns the accumulator so the loads cannot be optimised away.
__attribute__((noinline)) static uint32_t blendRowProbe(uint16_t *__restrict dst, const uint16_t *__restrict a16,
                                                        const uint32_t *__restrict runs, int nRuns, int level) {
    uint32_t acc = 0;
    for (int i = 0; i < nRuns; i++) {
        const uint32_t r = runs[i];
        int x = static_cast<int>(r & 0xFFFFu);
        const int xEnd = static_cast<int>(r >> 16);
        for (; x < xEnd; x++) {
            acc += a16[x];
            if (level >= 3) {
                dst[x] = scale565(dst[x], 24);
            }
        }
    }
    return acc;
}
} // namespace

static_assert(SleepAnimation::kProbeRuns == RUNS_PER_ROW, "probe run capture mirrors the overlay's run table");

// Levels 4 to 7 (benchSetBlendProbe): the production blend kernel over one
// captured overlay row, the same bytes and the same runs for every band, with
// the source and the destination each placed in internal SRAM or PSRAM. The
// first frames capture the row with the most pixels; a level change back to 0
// frees the copies (probeReset) so a new capture starts clean.
uint32_t IRAM_ATTR SleepAnimation::probeBlendRow(uint16_t *drow, const uint16_t *ccol, const uint16_t *ca16, const uint32_t *runs,
                                                 int nRuns, int level, int w, bool pie) {
    if (level <= 1) {
        return 0;
    }
    if (level <= 3) {
        return blendRowProbe(drow, ca16, runs, nRuns, level);
    }
    if (probeCaptureBands < 720) { // about three frames of bands
        uint32_t px = 0;
        int xmax = 0;
        for (int i = 0; i < nRuns; i++) {
            const int x0 = static_cast<int>(runs[i] & 0xFFFFu);
            const int x1 = static_cast<int>(runs[i] >> 16);
            px += static_cast<uint32_t>(x1 - x0);
            xmax = x1 > xmax ? x1 : xmax;
        }
        if (px > probePx && nRuns <= kProbeRuns && xmax <= w) {
            if (probeRowSram == nullptr) {
                const size_t rowBytes = static_cast<size_t>(w) * 2;
                probeRowSram =
                    static_cast<uint8_t *>(heap_caps_malloc(static_cast<size_t>(w) * 3, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
                probeRowPsram = static_cast<uint8_t *>(heap_caps_malloc(static_cast<size_t>(w) * 3, MALLOC_CAP_SPIRAM));
                probeDstSram =
                    static_cast<uint16_t *>(heap_caps_aligned_alloc(16, rowBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
                probeColPlane =
                    static_cast<uint16_t *>(heap_caps_aligned_alloc(16, rowBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
                probeA16Plane =
                    static_cast<uint16_t *>(heap_caps_aligned_alloc(16, rowBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
                probeRefRow =
                    static_cast<uint16_t *>(heap_caps_aligned_alloc(16, rowBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            }
            if (probeRowSram != nullptr && probeRowPsram != nullptr && probeDstSram != nullptr && probeColPlane != nullptr &&
                probeA16Plane != nullptr && probeRefRow != nullptr) {
                // The interleaved copies for levels 4 to 7 (the old kernel)
                // are built from the planes: colour, then alpha with 256
                // back to 255.
                memcpy(probeRuns, runs, static_cast<size_t>(nRuns) * sizeof(uint32_t));
                memcpy(probeColPlane, ccol, static_cast<size_t>(xmax) * 2);
                memcpy(probeA16Plane, ca16, static_cast<size_t>(xmax) * 2);
                for (int x = 0; x < xmax; x++) {
                    uint8_t *q = probeRowSram + static_cast<size_t>(x) * 3;
                    q[0] = static_cast<uint8_t>(ccol[x]);
                    q[1] = static_cast<uint8_t>(ccol[x] >> 8);
                    q[2] = ca16[x] > 255 ? 255 : static_cast<uint8_t>(ca16[x]);
                }
                memcpy(probeRowPsram, probeRowSram, static_cast<size_t>(xmax) * 3);
                probeNRuns = nRuns;
                probePx = px;
                probePxOut.store(px);
                probeMismatch.store(-1);
            }
        }
        probeCaptureBands++;
        return 0;
    }
    if (probeNRuns == 0) {
        return 0;
    }
    if (probeMismatch.load() < 0) {
        // Exactness on the device, once per capture: the planar vector kernel
        // and the scalar blendRow over the same row and background must agree
        // on every pixel. The background is this band's row.
        memcpy(probeRefRow, drow, static_cast<size_t>(w) * 2);
        memcpy(probeDstSram, drow, static_cast<size_t>(w) * 2);
        probeKernelBlendRow(probeRefRow, probeRowSram, probeRuns, probeNRuns);
        probeKernelBlendRowPlanar(probeDstSram, probeColPlane, probeA16Plane, probeRuns, probeNRuns, true);
        int bad = 0;
        for (int x = 0; x < w; x++) {
            bad += probeRefRow[x] != probeDstSram[x];
        }
        probeMismatch.store(bad);
    }
    const uint8_t *src = (level == 4 || level == 7) ? probeRowSram : probeRowPsram;
    uint16_t *dst = (level == 6 || level == 7) ? probeDstSram : drow;
    const int reps = probeReps.load();
    for (int k = 0; k < reps; k++) {
        if (level >= 8) {
            probeKernelBlendRowPlanar(dst, probeColPlane, probeA16Plane, probeRuns, probeNRuns, level == 8);
        } else if (pie) {
            probeKernelBlendRowPie(dst, src, probeRuns, probeNRuns);
        } else {
            probeKernelBlendRow(dst, src, probeRuns, probeNRuns);
        }
    }
    return static_cast<uint32_t>(reps);
}

void SleepAnimation::probeReset() {
    if (probeRowSram != nullptr) {
        heap_caps_free(probeRowSram);
    }
    if (probeRowPsram != nullptr) {
        heap_caps_free(probeRowPsram);
    }
    if (probeDstSram != nullptr) {
        heap_caps_free(probeDstSram);
    }
    if (probeColPlane != nullptr) {
        heap_caps_free(probeColPlane);
    }
    if (probeA16Plane != nullptr) {
        heap_caps_free(probeA16Plane);
    }
    if (probeRefRow != nullptr) {
        heap_caps_free(probeRefRow);
    }
    probeRowSram = nullptr;
    probeRowPsram = nullptr;
    probeDstSram = nullptr;
    probeColPlane = nullptr;
    probeA16Plane = nullptr;
    probeRefRow = nullptr;
    probeMismatch.store(-1);
    probeNRuns = 0;
    probePx = 0;
    probeCaptureBands = 0;
    probePxOut.store(0);
}

#endif // GM_BLEND_PROBE
#endif // GAGGIMATE_SIM
