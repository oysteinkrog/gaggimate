#ifndef GAGGIMATE_SIM

// "Ripples" — rain drops on dark water: up to 4 expanding rings with signed
// height fields that genuinely interfere where they cross. Design: anim-water
// (Fable), 2026-08-15; annulus-span + clamp-LUT restructure, 2026-08-15.
//
// Distance field, sqrt-free and integer: along a scan row, the true Euclidean
// distance r(x) = sqrt((x-cx)^2+dy^2) is 1-Lipschitz in x (|dr/dx| <= 1), so
// as integer x steps by exactly 1 the integer floor(r) changes by at most 1.
// Ripple centers are rounded to the nearest pixel once per frame (g_icx/
// g_icy) so the whole per-pixel tracker runs in int32: per ring per row we
// seed integer r once (one sqrtf, at the crossing's left edge x0), then carry
// (r, r2=r*r, dist2) forward per pixel with add/shift only — dist2 +=
// 2*dx+1, and r/r2 rebracketed via `while (dist2 >= r2+2r+1) { r2+=2r+1;
// r++; }` (and the mirror decrement) — no multiply, no sqrt, amortized O(1).
// Rounding the ripple center to the nearest pixel and r to an integer (vs.
// continuous float distance) costs at most ~1px of phase error against a
// 27px wavelength (~13 degrees) — invisible under the existing dither.
//
// Row-window fix (this pass): a ring is a thin annulus (radial thickness
// 2*HALFW), not a filled disk. The previous version bounded a ring-row's
// x-range using only the OUTER edge (r+HALFW), which for a mature ring
// (radius routinely exceeds the 480px screen width before its amplitude
// decays away) produces an x-window spanning nearly the entire row even
// though only the two ~26px-wide crossings where the annulus actually
// intersects the row can contribute anything (the ring's hollow interior is
// plain water). That's why band_ms stayed ~10x over the ~0.20ms host target
// even after removing the per-pixel libm calls: the per-pixel tracker/
// envelope loop was still walking O(radius) pixels per row, most of which
// always evaluate to "no contribution". Instead we now solve for BOTH the
// outer edge (r+HALFW) and inner edge (r-HALFW) circles' intersection with
// the row, giving up to two disjoint thin crossings (left/right) per ring
// per row — width ~2*HALFW regardless of radius. When the ring hasn't
// grown past HALFW yet, or the row cuts through the annulus's vertical cap
// (inner circle doesn't reach this row), it falls back to one wide-ish
// window (rare: only the ~2*HALFW rows nearest a mature ring's top/bottom).
// A small WIN_MARGIN pads every window because the window is computed from
// the exact float radius while the per-pixel envelope test runs on the
// rounded-integer tracker (~1px slack, see above) — padding a few extra
// pixels that will simply fail the envelope test is far cheaper than
// clipping a real contribution.
//
// Per ring-row, each 1-2 crossings becomes a RowBand entry (own tracker
// seed). RowBand x-ranges are sorted and merged into disjoint spans; pixels
// outside every span never touch the ring math at all. For those pixels
// (and whole rings-free rows) we exploit that the background dither
// (BAYER4[(y&3)*4+(x&3)]) only takes 4 distinct values per row, so a 4-entry
// "tile" of pre-clamped, pre-packed rgb565 values is computed once per row
// and copied for every non-ring pixel — replacing a per-pixel float clamp +
// rgb565 pack with a single array read.
//
// Device codegen (xtensa-asm.sh, real ESP32-S3 GCC): float division has no
// FPU instruction here (compiles to a __divsf3 libcall, ~30-50 cy),
// fmaxf/fminf are libcalls too (not inlined), and bganim::fastCosRad/
// fastSinRad each call cosTableF() (call8 + lazy-init check) on every use.
// Any call inside a loop body blocks GCC's zero-overhead LOOP codegen
// entirely. So band() caches the cosine table pointer once (g_cosTable,
// fetched in init()) and indexes it directly via local cosRadLocal/
// sinRadLocal helpers; the row-level `y/479.0f` divide is a reciprocal
// multiply (INV_ROWMAX); x0/x1 clamps use ternaries instead of fmaxf/fminf;
// and clamp8f's branchy `v<0?0:(v>255?255:cast(v))` chain (3 branches per
// channel, every pixel) is replaced by a single padded uint8 LUT (clampU8),
// indexed by `(int)v + CLAMP_PAD` — bit-for-bit equivalent to clamp8f (see
// the derivation in init()) but branch-free. The remaining sqrtf calls
// (crossing half-widths + integer-tracker seeds) are per-ring-per-row, not
// per-pixel — cheap regardless of host/device, same cost class as before.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

namespace {
using namespace bganim;

constexpr int MAX_RIPPLES = 4;
constexpr int MAX_BANDS = MAX_RIPPLES * 2; // a ring can split into a left+right annulus crossing per row
constexpr float HALFW = 13.0f;
constexpr float WAVEFREQ = 6.2831853f / 27.0f;
constexpr float ENV_SCALE = 255.0f / HALFW; // replaces a per-pixel divide by HALFW
constexpr float INV_ROWMAX = 1.0f / 479.0f;  // replaces a per-row divide by 479.0f
constexpr float RAD_TO_TABLE = 256.0f / 6.2831853f;
constexpr float WIN_MARGIN = 2.0f; // px slack on every crossing window (see file header)

// Padded clamp-to-uint8 LUT sizing. Worst case for cr/cg/cb (see clamp8f
// call sites below): baseR/baseG/baseB in [0,255] (themeRGB output); ring
// term g*crestF/troughF where |g| = |hAcc * g_glow| and hAcc sums up to
// MAX_RIPPLES per-ring contributions, each bounded by amp*cos*env <=
// amp_max ~= 0.975 (rise in [0,1], expf(-ageS/life) < 1 with its peak at
// ageS=0.18s -> ~0.975 for the largest tunable life of 7s), so
// |hAcc| <= 4*0.975 = 3.9; g_glow in [0.35, 1.5] (p[3] 0..100) so
// |g| <= 5.85. crestF[ch] <= 255*0.65 = 165.75 (crest branch, g>0):
// cr_max ~= 255 + 5.85*165.75 + dith(4.125) ~= 1228.7. troughF[ch] <=
// crestF*0.12 <= 19.89 (trough branch, g<0): cr_min ~= 0 - 5.85*19.89 -
// 4.125 ~= -120.5. So the true range is about [-121, 1229]; CLAMP_PAD/SIZE
// below add a comfortable margin on both ends.
constexpr int CLAMP_PAD = 200;
constexpr int CLAMP_SIZE = 1600; // covers b = (int)v in [-200, 1399]

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
float *envLUT = nullptr;     // 256: 1-(i/255)^2
uint8_t *clampU8 = nullptr;  // CLAMP_SIZE: clamp(idx-CLAMP_PAD, 0, 255) — see derivation above
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
    if (clampU8 == nullptr) {
        clampU8 = static_cast<uint8_t *>(alloc(CLAMP_SIZE));
        if (clampU8 == nullptr) {
            return false;
        }
        for (int i = 0; i < CLAMP_SIZE; i++) {
            const int b = i - CLAMP_PAD;
            clampU8[i] = b < 0 ? 0 : (b > 255 ? 255 : static_cast<uint8_t>(b));
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
    struct RowBand {
        float r, amp;
        int x0, x1;
        // Incremental sqrt-free integer distance tracker, seeded once at x0
        // and stepped per pixel while x is inside [x0, x1] (see file
        // header). curR2 == curR*curR, maintained incrementally so the
        // per-pixel rebracket is add/shift only, never a multiply.
        int curDx, curDist2, curR, curR2;
    };

    const float wMinus1 = static_cast<float>(w - 1);

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

        // The background dither only takes 4 distinct values per row
        // (BAYER4 indexes x&3), so precompute the 4 possible output pixels
        // once and reuse them for every pixel not touched by a ring — no
        // per-pixel clamp/pack for the (usually large) ring-free majority
        // of each row.
        uint16_t tile[4];
        for (int k = 0; k < 4; k++) {
            const float dith = (BAYER4[(y & 3) * 4 + k] - 7.5f) * 0.55f;
            tile[k] = rgb565(clampU8[static_cast<int>(baseR + dith) + CLAMP_PAD],
                              clampU8[static_cast<int>(baseG + dith) + CLAMP_PAD],
                              clampU8[static_cast<int>(baseB + dith) + CLAMP_PAD]);
        }

        RowBand rb[MAX_BANDS];
        int nrb = 0;
        auto seedBand = [&](int ringIdx, int bx0, int bx1) {
            if (bx1 < bx0) {
                return;
            }
            // Seed the integer distance at bx0 (one sqrtf per crossing per
            // row — not per pixel).
            const int idy = y - g_icy[ringIdx];
            const int idx0 = bx0 - g_icx[ringIdx];
            const int dist2 = idx0 * idx0 + idy * idy;
            int r0 = static_cast<int>(sqrtf(static_cast<float>(dist2)));
            while ((r0 + 1) * (r0 + 1) <= dist2) {
                r0++;
            }
            while (r0 * r0 > dist2) {
                r0--;
            }
            rb[nrb++] = {g_r[ringIdx], g_amp[ringIdx], bx0, bx1, idx0, dist2, r0, r0 * r0};
        };

        for (int i = 0; i < g_n; i++) {
            const float dy = y - g_cy[i];
            const float dy2 = dy * dy;
            const float outerR = g_r[i] + HALFW + WIN_MARGIN;
            const float outer2 = outerR * outerR;
            if (dy2 > outer2) {
                continue;
            }
            const float halfOuter = sqrtf(outer2 - dy2);
            const float innerR = g_r[i] - HALFW - WIN_MARGIN;
            bool split = false;
            if (innerR > 0.0f) {
                const float inner2 = innerR * innerR;
                if (dy2 < inner2) {
                    // Row cuts through the annulus proper (not just its cap):
                    // two thin crossings, ~2*(HALFW+WIN_MARGIN) wide each,
                    // instead of one wide disk-width window. This is the fix
                    // — previously the window always spanned the full outer
                    // disk (leftF..rightF below), which for a mature ring
                    // (radius >> HALFW) meant walking almost the entire row
                    // width per ring per row even though the ring's hollow
                    // interior never contributes.
                    split = true;
                    const float halfInner = sqrtf(inner2 - dy2);
                    {
                        const float leftF = g_cx[i] - halfOuter;
                        const float rightF = g_cx[i] - halfInner;
                        const int bx0 = static_cast<int>(leftF > 0.0f ? leftF : 0.0f);
                        const int bx1 = static_cast<int>(rightF < wMinus1 ? rightF : wMinus1);
                        seedBand(i, bx0, bx1);
                    }
                    {
                        const float leftF = g_cx[i] + halfInner;
                        const float rightF = g_cx[i] + halfOuter;
                        const int bx0 = static_cast<int>(leftF > 0.0f ? leftF : 0.0f);
                        const int bx1 = static_cast<int>(rightF < wMinus1 ? rightF : wMinus1);
                        seedBand(i, bx0, bx1);
                    }
                }
            }
            if (!split) {
                // Ring hasn't grown past HALFW yet, or this row only clips
                // the annulus's vertical cap (rare — a ~2*(HALFW+margin)-row
                // band near a mature ring's top/bottom): fall back to the
                // single outer-disk window, same as before this pass.
                const float leftF = g_cx[i] - halfOuter;
                const float rightF = g_cx[i] + halfOuter;
                const int bx0 = static_cast<int>(leftF > 0.0f ? leftF : 0.0f);
                const int bx1 = static_cast<int>(rightF < wMinus1 ? rightF : wMinus1);
                seedBand(i, bx0, bx1);
            }
        }

        uint16_t *row = dst + static_cast<size_t>(yy) * w;
        if (nrb == 0) {
            for (int x = 0; x < w; x++) {
                row[x] = tile[x & 3];
            }
            continue;
        }

        // Merge the (up to MAX_BANDS) crossing windows into disjoint spans
        // so every pixel outside all of them can skip straight to the tile
        // lookup instead of paying nrb bound-checks. nrb is tiny (<= 8), so
        // an insertion sort is cheap and branch-predictable.
        for (int a = 1; a < nrb; a++) {
            RowBand key = rb[a];
            int b = a - 1;
            while (b >= 0 && rb[b].x0 > key.x0) {
                rb[b + 1] = rb[b];
                b--;
            }
            rb[b + 1] = key;
        }
        int spanX0[MAX_BANDS], spanX1[MAX_BANDS];
        int nSpans = 0;
        for (int a = 0; a < nrb; a++) {
            if (nSpans > 0 && rb[a].x0 <= spanX1[nSpans - 1] + 1) {
                if (rb[a].x1 > spanX1[nSpans - 1]) {
                    spanX1[nSpans - 1] = rb[a].x1;
                }
            } else {
                spanX0[nSpans] = rb[a].x0;
                spanX1[nSpans] = rb[a].x1;
                nSpans++;
            }
        }

        int x = 0;
        for (int s = 0; s < nSpans; s++) {
            for (; x < spanX0[s]; x++) {
                row[x] = tile[x & 3];
            }
            const int spanEnd = spanX1[s];
            for (; x <= spanEnd; x++) {
                float cr = baseR, cg = baseG, cb = baseB;
                float hAcc = 0;
                for (int i = 0; i < nrb; i++) {
                    if (x < rb[i].x0 || x > rb[i].x1) {
                        continue;
                    }
                    // rb[i].curR is already the tracked integer distance for
                    // this x (seeded at x0, stepped at the bottom of this scope).
                    const float delta = static_cast<float>(rb[i].curR) - rb[i].r;
                    const float ad = fabsf(delta);
                    if (ad <= HALFW) {
                        const float env = envLUT[static_cast<int>(ad * ENV_SCALE)];
                        hAcc += rb[i].amp * cosRadLocal(g_cosTable, delta * WAVEFREQ) * env;
                    }
                    // Step the sqrt-free tracker to x+1: dist2 grows by
                    // 2*dx+1 as dx increments by exactly 1 (int add), then
                    // rebracket curR/curR2 with add/shift only (no multiply,
                    // no sqrt) — amortized O(1), at most one nudge either way.
                    rb[i].curDist2 += 2 * rb[i].curDx + 1;
                    rb[i].curDx += 1;
                    while (rb[i].curDist2 >= rb[i].curR2 + 2 * rb[i].curR + 1) {
                        rb[i].curR2 += 2 * rb[i].curR + 1;
                        rb[i].curR++;
                    }
                    while (rb[i].curDist2 < rb[i].curR2) {
                        rb[i].curR2 -= 2 * rb[i].curR - 1;
                        rb[i].curR--;
                    }
                }
                const float dith = (BAYER4[(y & 3) * 4 + (x & 3)] - 7.5f) * 0.55f;
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
                row[x] = rgb565(clampU8[static_cast<int>(cr + dith) + CLAMP_PAD],
                                 clampU8[static_cast<int>(cg + dith) + CLAMP_PAD],
                                 clampU8[static_cast<int>(cb + dith) + CLAMP_PAD]);
            }
        }
        for (; x < w; x++) {
            row[x] = tile[x & 3];
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
