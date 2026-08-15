#ifndef GAGGIMATE_SIM

// "Ripples" — rain drops on dark water: up to 4 expanding rings with signed
// height fields that genuinely interfere where they cross. Work is bounded to
// each ring's thin annulus (per-row x-interval); everything else is a cheap
// gradient. Design: anim-water (Fable), 2026-08-15.
//
// Distance field, sqrt-free and integer: along a scan row, the true Euclidean
// distance r(x) = sqrt((x-cx)^2+dy^2) is 1-Lipschitz in x (|dr/dx| <= 1), so
// as integer x steps by exactly 1 the integer floor(r) changes by at most 1.
// Ripple centers are rounded to the nearest pixel once per frame (g_icx/
// g_icy) so the whole per-pixel tracker runs in int32: per ring per row we
// seed integer r once (one sqrtf, at the annulus's left edge x0 — same cost
// class as the existing half-width sqrtf below), then carry (r, r2=r*r,
// dist2) forward per pixel with add/shift only — dist2 += 2*dx+1, and r/r2
// rebracketed via `while (dist2 >= r2+2r+1) { r2+=2r+1; r++; }` (and the
// mirror decrement) — no multiply, no sqrt, amortized O(1). Rounding the
// ripple center to the nearest pixel and r to an integer (vs. continuous
// float distance) costs at most ~1px of phase error against a 27px
// wavelength (~13 degrees) — invisible under the existing dither. Also
// replaced the per-pixel `ad / HALFW` float divide with a precomputed
// reciprocal multiply (ENV_SCALE).
//
// Device codegen (xtensa-asm.sh, real ESP32-S3 GCC): float division has no
// FPU instruction here (compiles to a __divsf3 libcall, ~30-50 cy) and
// bganim::fastCosRad/fastSinRad each call cosTableF() (call8 + lazy-init
// check) on every use — and fmaxf/fminf are libcalls too, not inlined. Any
// call inside a loop body blocks GCC's zero-overhead LOOP codegen entirely.
// So band() caches the cosine table pointer once (g_cosTable, fetched in
// init()) and indexes it directly via local cosRadLocal/sinRadLocal helpers
// instead of calling bganim::fastCosRad/fastSinRad; the row-level `y/479.0f`
// divide is a reciprocal multiply (INV_ROWMAX); and the per-ring x0/x1 clamp
// uses ternaries instead of fmaxf/fminf. The remaining sqrtf calls (window
// half-width + integer-tracker seed) are per-ring-per-row, not per-pixel —
// unavoidable and already the same cost class as before this pass.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

namespace {
using namespace bganim;

constexpr int MAX_RIPPLES = 4;
constexpr float HALFW = 13.0f;
constexpr float WAVEFREQ = 6.2831853f / 27.0f;
constexpr float ENV_SCALE = 255.0f / HALFW; // replaces a per-pixel divide by HALFW
constexpr float INV_ROWMAX = 1.0f / 479.0f;  // replaces a per-row divide by 479.0f
constexpr float RAD_TO_TABLE = 256.0f / 6.2831853f;

// Local equivalents of bganim::fastCosRad/fastSinRad that take an already-
// fetched table pointer, so callers don't pay a cosTableF() call8 per use
// (see file header). Same indexing formula as BgAnimCommon.h.
inline float cosRadLocal(const float *ct, float rad) { return ct[static_cast<int>(rad * RAD_TO_TABLE) & 255]; }
inline float sinRadLocal(const float *ct, float rad) { return cosRadLocal(ct, rad - 1.5707963f); }

struct Ripple {
    float cx, cy;
    uint32_t birthMs;
    bool active;
};
Ripple ripples[MAX_RIPPLES];
uint32_t nextDropMs = 0;
uint32_t rng = 0xC0FFEE;
float *envLUT = nullptr; // 256: 1-(i/255)^2
const float *g_cosTable = nullptr; // cached once so band()/frame() never call cosTableF()
bool inited = false;
uint32_t lastThemeGen = 0xFFFFFFFF;
float crestF[3] = {62, 98, 127}, troughF[3] = {7, 12, 15};

int g_n = 0;
float g_cx[MAX_RIPPLES], g_cy[MAX_RIPPLES], g_r[MAX_RIPPLES], g_amp[MAX_RIPPLES];
int g_icx[MAX_RIPPLES], g_icy[MAX_RIPPLES]; // centers rounded to nearest pixel, for the integer tracker
float g_glow = 1.0f;
uint32_t g_tMs = 0;

bool init(int, int) {
    if (envLUT == nullptr) {
        envLUT = static_cast<float *>(alloc(256 * sizeof(float)));
        if (envLUT == nullptr) {
            return false;
        }
        for (int i = 0; i < 256; i++) {
            const float n = i / 255.0f;
            envLUT[i] = 1.0f - n * n;
        }
    }
    if (g_cosTable == nullptr) {
        // Fetched once here (same lazy-build semantics as fastCosRad's own
        // first call) so band()/frame() can index it directly with zero
        // per-use call overhead.
        g_cosTable = cosTableF();
    }
    if (!inited) {
        inited = true;
        for (auto &r : ripples) {
            r.active = false;
        }
        nextDropMs = 600 + static_cast<uint32_t>(nextRandf(rng) * 2000);
    }
    return true;
}

// Water surface sits in the theme's darkest ~10%; ring crests borrow the
// brightest stop, troughs a dimmed version of it.
void rebuildThemeAssets() {
    uint8_t c[3];
    themeRGB(255, c);
    for (int ch = 0; ch < 3; ch++) {
        crestF[ch] = c[ch] * 0.65f;
        troughF[ch] = crestF[ch] * 0.12f;
    }
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[4]) {
    g_tMs = tMs;
    if (themeGen() != lastThemeGen) {
        rebuildThemeAssets();
        lastThemeGen = themeGen();
    }
    const float interval = lerpf(14000.0f, 1500.0f, p[1] / 100.0f);
    if (tMs >= nextDropMs) {
        for (auto &r : ripples) {
            if (!r.active) {
                r = {nextRandf(rng) * w, nextRandf(rng) * h, tMs, true};
                break;
            }
        }
        nextDropMs = tMs + static_cast<uint32_t>(interval * (0.55f + 0.9f * nextRandf(rng)));
    }
    const float speed = lerpf(25.0f, 220.0f, p[0] / 100.0f);
    const float life = lerpf(7.0f, 2.2f, p[2] / 100.0f);
    g_glow = 0.35f + 1.15f * (p[3] / 100.0f);

    g_n = 0;
    for (auto &r : ripples) {
        if (!r.active) {
            continue;
        }
        const float ageS = (tMs - r.birthMs) * 0.001f;
        if (ageS >= life) {
            r.active = false;
            continue;
        }
        const float radius = speed * ageS;
        const float rise = ageS < 0.18f ? ageS / 0.18f : 1.0f;
        const float amp = rise * expf(-ageS / life);
        if (radius <= 0 || amp < 0.008f) {
            continue;
        }
        g_cx[g_n] = r.cx;
        g_cy[g_n] = r.cy;
        // Round once per frame (not per row/pixel) for the integer tracker;
        // cx/cy are always >= 0 so truncation-after-offset is a valid round.
        g_icx[g_n] = static_cast<int>(r.cx + 0.5f);
        g_icy[g_n] = static_cast<int>(r.cy + 0.5f);
        g_r[g_n] = radius;
        g_amp[g_n] = amp;
        g_n++;
    }
}

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int yy = 0; yy < rows; yy++) {
        const int y = y0 + yy;
        const float vt = y * INV_ROWMAX;
        const float swell = sinRadLocal(g_cosTable, g_tMs * 0.00014f + y * 0.014f) * 2.5f;
        int basePos = static_cast<int>(vt * 20.0f + swell + 3.0f);
        if (basePos < 0) {
            basePos = 0;
        } else if (basePos > 31) {
            basePos = 31;
        }
        uint8_t baseC[3];
        themeRGB(basePos, baseC);
        const float baseR = baseC[0], baseG = baseC[1], baseB = baseC[2];

        struct RowRing {
            float r, amp;
            int x0, x1;
            // Incremental sqrt-free integer distance tracker, seeded once at
            // x0 and stepped per pixel while x is inside [x0, x1] (see file
            // header). curR2 == curR*curR, maintained incrementally so the
            // per-pixel rebracket is add/shift only, never a multiply.
            int curDx, curDist2, curR, curR2;
        };
        RowRing rr[MAX_RIPPLES];
        int nrr = 0;
        for (int i = 0; i < g_n; i++) {
            const float dy = y - g_cy[i];
            const float outer = g_r[i] + HALFW;
            if (fabsf(dy) > outer) {
                continue;
            }
            const float half = sqrtf(outer * outer - dy * dy);
            // fmaxf/fminf compile to libcalls on this target (not inlined) —
            // ternaries instead (see file header).
            const float leftF = g_cx[i] - half;
            const float rightF = g_cx[i] + half;
            const float wMinus1 = static_cast<float>(w - 1);
            const int x0 = static_cast<int>(leftF > 0.0f ? leftF : 0.0f);
            const int x1 = static_cast<int>(rightF < wMinus1 ? rightF : wMinus1);
            if (x1 >= x0) {
                // Seed the integer distance at x0 (one sqrtf per ring per
                // row — not per pixel; same cost class as `half` above).
                const int idy = y - g_icy[i];
                const int idx0 = x0 - g_icx[i];
                const int dist2 = idx0 * idx0 + idy * idy;
                int r0 = static_cast<int>(sqrtf(static_cast<float>(dist2)));
                while ((r0 + 1) * (r0 + 1) <= dist2) {
                    r0++;
                }
                while (r0 * r0 > dist2) {
                    r0--;
                }
                rr[nrr++] = {g_r[i], g_amp[i], x0, x1, idx0, dist2, r0, r0 * r0};
            }
        }

        uint16_t *row = dst + static_cast<size_t>(yy) * w;
        if (nrr == 0) {
            for (int x = 0; x < w; x++) {
                const float dith = (BAYER4[(y & 3) * 4 + (x & 3)] - 7.5f) * 0.55f;
                row[x] = rgb565(clamp8f(baseR + dith), clamp8f(baseG + dith), clamp8f(baseB + dith));
            }
            continue;
        }
        for (int x = 0; x < w; x++) {
            float cr = baseR, cg = baseG, cb = baseB;
            float hAcc = 0;
            for (int i = 0; i < nrr; i++) {
                if (x < rr[i].x0 || x > rr[i].x1) {
                    continue;
                }
                // rr[i].curR is already the tracked integer distance for
                // this x (seeded at x0, stepped at the bottom of this scope).
                const float delta = static_cast<float>(rr[i].curR) - rr[i].r;
                const float ad = fabsf(delta);
                if (ad <= HALFW) {
                    const float env = envLUT[static_cast<int>(ad * ENV_SCALE)];
                    hAcc += rr[i].amp * cosRadLocal(g_cosTable, delta * WAVEFREQ) * env;
                }
                // Step the sqrt-free tracker to x+1: dist2 grows by 2*dx+1 as
                // dx increments by exactly 1 (int add), then rebracket
                // curR/curR2 with add/shift only (no multiply, no sqrt) —
                // amortized O(1), at most one nudge in either direction.
                rr[i].curDist2 += 2 * rr[i].curDx + 1;
                rr[i].curDx += 1;
                while (rr[i].curDist2 >= rr[i].curR2 + 2 * rr[i].curR + 1) {
                    rr[i].curR2 += 2 * rr[i].curR + 1;
                    rr[i].curR++;
                }
                while (rr[i].curDist2 < rr[i].curR2) {
                    rr[i].curR2 -= 2 * rr[i].curR - 1;
                    rr[i].curR--;
                }
            }
            if (hAcc != 0) {
                const float g = hAcc * g_glow;
                if (g > 0) {
                    cr += g * crestF[0];
                    cg += g * crestF[1];
                    cb += g * crestF[2];
                } else {
                    cr += g * troughF[0];
                    cg += g * troughF[1];
                    cb += g * troughF[2];
                }
            }
            const float dith = (BAYER4[(y & 3) * 4 + (x & 3)] - 7.5f) * 0.55f;
            row[x] = rgb565(clamp8f(cr + dith), clamp8f(cg + dith), clamp8f(cb + dith));
        }
    }
}

} // namespace

extern const BgAnimation bg_anim_ripples;
const BgAnimation bg_anim_ripples = {
    "ripples",
    "Ripples",
    {{"speed", "Ring speed", 50}, {"rate", "Drop rate", 40}, {"decay", "Fade", 50}, {"glow", "Glow", 50}},
    init,
    frame,
    band,
};

#endif // GAGGIMATE_SIM
