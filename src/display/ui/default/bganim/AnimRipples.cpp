#ifndef GAGGIMATE_SIM

// "Ripples" — rain drops on dark water: up to 4 expanding rings with signed
// height fields that genuinely interfere where they cross. Work is bounded to
// each ring's thin annulus (per-row x-interval); everything else is a cheap
// gradient. Design: anim-water (Fable), 2026-08-15.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>

namespace {
using namespace bganim;

constexpr int MAX_RIPPLES = 4;
constexpr float HALFW = 13.0f;
constexpr float WAVEFREQ = 6.2831853f / 27.0f;

struct Ripple {
    float cx, cy;
    uint32_t birthMs;
    bool active;
};
Ripple ripples[MAX_RIPPLES];
uint32_t nextDropMs = 0;
uint32_t rng = 0xC0FFEE;
float *envLUT = nullptr; // 256: 1-(i/255)^2
bool inited = false;

int g_n = 0;
float g_cx[MAX_RIPPLES], g_cy[MAX_RIPPLES], g_r[MAX_RIPPLES], g_amp[MAX_RIPPLES];
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
    if (!inited) {
        inited = true;
        for (auto &r : ripples) {
            r.active = false;
        }
        nextDropMs = 600 + static_cast<uint32_t>(nextRandf(rng) * 2000);
    }
    return true;
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[4]) {
    g_tMs = tMs;
    const float interval = lerpf(14000.0f, 1500.0f, p[0] / 100.0f);
    if (tMs >= nextDropMs) {
        for (auto &r : ripples) {
            if (!r.active) {
                r = {nextRandf(rng) * w, nextRandf(rng) * h, tMs, true};
                break;
            }
        }
        nextDropMs = tMs + static_cast<uint32_t>(interval * (0.55f + 0.9f * nextRandf(rng)));
    }
    const float speed = lerpf(38.0f, 150.0f, p[1] / 100.0f);
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
        g_r[g_n] = radius;
        g_amp[g_n] = amp;
        g_n++;
    }
}

void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int yy = 0; yy < rows; yy++) {
        const int y = y0 + yy;
        const float vt = y / 479.0f;
        const float swell = fastSinRad(g_tMs * 0.00007f + y * 0.014f) * 2.5f;
        const float baseR = 4 + 5 * vt + swell;
        const float baseG = 7 + 7 * vt + swell;
        const float baseB = 14 + 12 * vt + swell * 1.4f;

        struct RowRing {
            float cx, r, amp, dy;
            int x0, x1;
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
            const int x0 = static_cast<int>(fmaxf(0.0f, g_cx[i] - half));
            const int x1 = static_cast<int>(fminf(static_cast<float>(w - 1), g_cx[i] + half));
            if (x1 >= x0) {
                rr[nrr++] = {g_cx[i], g_r[i], g_amp[i], dy, x0, x1};
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
                const float dx = x - rr[i].cx;
                const float d = sqrtf(dx * dx + rr[i].dy * rr[i].dy); // only inside the annulus
                const float delta = d - rr[i].r;
                const float ad = fabsf(delta);
                if (ad > HALFW) {
                    continue;
                }
                const float env = envLUT[static_cast<int>((ad / HALFW) * 255.0f)];
                hAcc += rr[i].amp * fastCosRad(delta * WAVEFREQ) * env;
            }
            if (hAcc != 0) {
                const float g = hAcc * g_glow;
                if (g > 0) {
                    cr += g * 95;
                    cg += g * 150;
                    cb += g * 195;
                } else {
                    cr += g * 9;
                    cg += g * 13;
                    cb += g * 22;
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
    {{"rate", "Drop rate", 30}, {"speed", "Ring speed", 40}, {"decay", "Fade", 50}, {"glow", "Glow", 50}},
    init,
    frame,
    band,
};

#endif // GAGGIMATE_SIM
