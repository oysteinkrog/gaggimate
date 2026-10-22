#ifndef GAGGIMATE_SIM

#include "BgAnimCommon.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <math.h>

namespace bganim {

const int16_t *sinLut() {
    static int16_t *lut = nullptr;
    if (lut == nullptr) {
        lut = static_cast<int16_t *>(alloc(SIN_N * sizeof(int16_t)));
        if (lut != nullptr) {
            for (int i = 0; i < SIN_N; i++) {
                lut[i] = static_cast<int16_t>(lroundf(sinf(i * (2.0f * static_cast<float>(M_PI) / SIN_N)) * SIN_AMP));
            }
        }
    }
    return lut;
}

size_t g_allocSram = 0;
size_t g_allocPsram = 0;

namespace {
// Internal SRAM on the S3 is the 0x3FC.. data range; PSRAM maps at 0x3C...
bool isPsram(const void *p) {
    const uintptr_t a = reinterpret_cast<uintptr_t>(p);
    return a >= 0x3C000000u && a < 0x3E000000u;
}
} // namespace

void *alloc(size_t size) {
    // Animations allocate their LUTs lazily on first use and never free them,
    // so switching through the whole fleet in one power cycle accumulates every
    // table. Requesting all of that from internal SRAM first would be roughly
    // 280 KB against a 327 KB pool the firmware has already claimed ~110 KB of
    // — and WiFi/BLE/TLS allocate from the same pool at runtime, so an
    // animation could starve the network stack.
    //
    // Small tables stay in SRAM, where their random-access latency actually
    // matters. Anything large is a bulk table read in sequential sweeps, which
    // PSRAM (8 MB, cache-line prefetched) serves fine.
    //
    // CAVEAT for future tables: this size test is a proxy for access pattern,
    // and the proxy fails for a large table indexed by a value computed per
    // pixel. Aurora used to keep (v*v)>>12 in a 16 KB table for exactly that
    // kind of index; being over the threshold put it in PSRAM and every pixel
    // paid a bus round trip to avoid one multiply. Deleting the table was
    // worth -21% on that animation. If a new table is over the limit AND its
    // index is not monotonic across a row, either shrink it under the limit or
    // compute the value instead -- do not assume PSRAM will serve it.
    //
    // The per-allocation size test above is necessary but NOT sufficient, and
    // that gap was a real bug: it bounds one table but says nothing about the
    // sum. Measured on device, walking the bench sweep through the fleet took
    // the internal-SRAM total to 4,992 -> 8,448 -> 30,984 -> 42,068 -> 50,772
    // -> 53,396 bytes, monotonically, none of it ever freed. The web UI went
    // unreachable the moment it crossed 53 KB, and stayed unreachable.
    //
    // The failure is silent and does not look like memory pressure. lwIP's
    // tcp_listen_input() drops an incoming SYN with no RST when tcp_alloc()
    // returns null, so the symptom is a TCP connect TIMEOUT, while ICMP -- which
    // allocates nothing -- keeps answering normally and uptime keeps climbing.
    // For most of a session that reads as "the board hung", which is what it
    // was misdiagnosed as, repeatedly.
    //
    // So the budget is cumulative, checked before the request rather than after.
    // The first tables to ask still land in SRAM where the latency matters; once
    // the fleet has taken its share, later tables go to PSRAM instead of eating
    // the pool the network stack lives in. This is only a ceiling: it does not
    // reclaim anything, because animations hold their tables in static pointers
    // and re-initialise only when those are null. Freeing on switch would cap
    // the peak at max-over-animations instead of sum-over-animations, and is the
    // right eventual fix; it needs a release entry point on every animation.
    const bool overBudget = g_allocSram + size > SRAM_TOTAL_BUDGET;
    if (size > SRAM_ALLOC_LIMIT || overBudget) {
        void *big = ps_malloc(size);
        if (big != nullptr) {
            g_allocPsram += size;
            return big;
        }
        // No PSRAM (or it is exhausted): fall through and try SRAM anyway
        // rather than failing the animation outright.
    }
    void *p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (p == nullptr) {
        p = ps_malloc(size);
        if (p != nullptr) {
            log_w("bganim: %u B in PSRAM (internal SRAM full)", static_cast<unsigned>(size));
        }
    }
    if (p != nullptr) {
        (isPsram(p) ? g_allocPsram : g_allocSram) += size;
    }
    return p;
}

void release(void *&p, size_t size) {
    if (p == nullptr) {
        return;
    }
    // Decrement the pool the pointer actually came from, not the one the
    // placement policy would have picked: a request over the per-allocation
    // limit, or one made after the budget was spent, went to PSRAM, and an
    // internal request can also have fallen back to PSRAM when SRAM was full.
    size_t &counter = isPsram(p) ? g_allocPsram : g_allocSram;
    counter = (counter > size) ? counter - size : 0;
    heap_caps_free(p);
    p = nullptr;
}

const float *cosTableF() {
    static float *lut = nullptr;
    if (lut == nullptr) {
        lut = static_cast<float *>(alloc(256 * sizeof(float)));
        if (lut != nullptr) {
            for (int i = 0; i < 256; i++) {
                lut[i] = cosf(i * (2.0f * static_cast<float>(M_PI) / 256.0f));
            }
        }
    }
    return lut;
}

const uint8_t BAYER4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
const uint8_t BAYER8[64] = {0,  32, 8,  40, 2,  34, 10, 42, 48, 16, 56, 24, 50, 18, 58, 26, 12, 44, 4,  36, 14, 46,
                            6,  38, 60, 28, 52, 20, 62, 30, 54, 22, 3,  35, 11, 43, 1,  33, 9,  41, 51, 19, 59, 27,
                            49, 17, 57, 25, 15, 47, 7,  39, 13, 45, 5,  37, 63, 31, 55, 23, 61, 29, 53, 21};

void buildPalette(uint16_t *out, const uint8_t (*keys)[3], int nKeys, uint16_t brightness256) {
    for (int i = 0; i < 256; i++) {
        const float pos = i * (static_cast<float>(nKeys) / 256.0f);
        const int k0 = static_cast<int>(pos) % nKeys;
        const int k1 = (k0 + 1) % nKeys;
        const float f = pos - floorf(pos);
        const uint32_t r = static_cast<uint32_t>((keys[k0][0] + (keys[k1][0] - keys[k0][0]) * f)) * brightness256 >> 8;
        const uint32_t g = static_cast<uint32_t>((keys[k0][1] + (keys[k1][1] - keys[k0][1]) * f)) * brightness256 >> 8;
        const uint32_t b = static_cast<uint32_t>((keys[k0][2] + (keys[k1][2] - keys[k0][2]) * f)) * brightness256 >> 8;
        out[i] = rgb565(static_cast<uint8_t>(r > 255 ? 255 : r), static_cast<uint8_t>(g > 255 ? 255 : g),
                        static_cast<uint8_t>(b > 255 ? 255 : b));
    }
}

const uint8_t *noiseTex256() {
    static uint8_t *tex = nullptr;
    if (tex != nullptr) {
        return tex;
    }
    uint8_t *t = static_cast<uint8_t *>(alloc(256 * 256));
    if (t == nullptr) {
        return nullptr;
    }
    constexpr int PERIOD = 16;
    float lattice[PERIOD * PERIOD];
    uint32_t rng = 1337;
    for (int i = 0; i < PERIOD * PERIOD; i++) {
        lattice[i] = nextRandf(rng);
    }
    const auto latticeAt = [&](int ix, int iy) { return lattice[(iy & (PERIOD - 1)) * PERIOD + (ix & (PERIOD - 1))]; };
    const auto smooth = [](float v) { return v * v * v * (v * (v * 6.0f - 15.0f) + 10.0f); };
    constexpr float CELL = 256.0f / PERIOD;
    for (int y = 0; y < 256; y++) {
        const float gy = y / CELL;
        const int iy0 = static_cast<int>(gy);
        const float fy = smooth(gy - iy0);
        for (int x = 0; x < 256; x++) {
            const float gx = x / CELL;
            const int ix0 = static_cast<int>(gx);
            const float fx = smooth(gx - ix0);
            const float v00 = latticeAt(ix0, iy0), v10 = latticeAt(ix0 + 1, iy0);
            const float v01 = latticeAt(ix0, iy0 + 1), v11 = latticeAt(ix0 + 1, iy0 + 1);
            const float a = v00 + (v10 - v00) * fx;
            const float b = v01 + (v11 - v01) * fx;
            t[y * 256 + x] = clamp8f((a + (b - a) * fy) * 255.0f);
        }
    }
    tex = t;
    return tex;
}

// ---- active color theme --------------------------------------------------

namespace {
// Double buffer behind an atomic generation counter: the writer fills the
// inactive buffer then increments the generation (buffer index = gen & 1).
// A torn read would need two settings writes inside one frame — harmless.
uint8_t g_themeBuf[2][8][3] = {
    {{0x08, 0x04, 0x02}, {0x2a, 0x12, 0x06}, {0x6b, 0x34, 0x13}, {0xb8, 0x70, 0x3a}, {0xe8, 0xb2, 0x68}, {0xf8, 0xe6, 0xc8}},
    {{0x08, 0x04, 0x02}, {0x2a, 0x12, 0x06}, {0x6b, 0x34, 0x13}, {0xb8, 0x70, 0x3a}, {0xe8, 0xb2, 0x68}, {0xf8, 0xe6, 0xc8}},
};
int g_themeCount[2] = {6, 6};
volatile uint32_t g_themeGen = 0;
} // namespace

void setThemeStops(const uint8_t (*stops)[3], int nStops) {
    if (stops == nullptr || nStops < 2) {
        return;
    }
    if (nStops > 8) {
        nStops = 8;
    }
    const uint32_t next = g_themeGen + 1;
    const int buf = next & 1;
    for (int i = 0; i < nStops; i++) {
        for (int c = 0; c < 3; c++) {
            g_themeBuf[buf][i][c] = stops[i][c];
        }
    }
    g_themeCount[buf] = nStops;
    g_themeGen = next;
}

uint32_t themeGen() { return g_themeGen; }
int themeStopCount() { return g_themeCount[g_themeGen & 1]; }
const uint8_t (*themeStops())[3] { return g_themeBuf[g_themeGen & 1]; }

void themeRGB(int pos, uint8_t out[3]) {
    const uint8_t(*st)[3] = themeStops();
    const int n = themeStopCount();
    if (pos < 0) {
        pos = 0;
    } else if (pos > 255) {
        pos = 255;
    }
    const int scaled = pos * (n - 1);      // 0 .. 255*(n-1)
    const int seg = scaled >> 8;           // stop index
    const int f = scaled & 255;            // blend within segment
    for (int c = 0; c < 3; c++) {
        out[c] = static_cast<uint8_t>(st[seg][c] + (((st[seg + 1][c] - st[seg][c]) * f) >> 8));
    }
}

void buildThemeRamp(uint16_t *out, uint16_t brightness256, bool reversed) {
    for (int i = 0; i < 256; i++) {
        uint8_t c[3];
        themeRGB(reversed ? 255 - i : i, c);
        const uint32_t r = (c[0] * brightness256) >> 8;
        const uint32_t g = (c[1] * brightness256) >> 8;
        const uint32_t b = (c[2] * brightness256) >> 8;
        out[i] = rgb565(static_cast<uint8_t>(r > 255 ? 255 : r), static_cast<uint8_t>(g > 255 ? 255 : g),
                        static_cast<uint8_t>(b > 255 ? 255 : b));
    }
}

void buildThemeWheel(uint16_t *out, uint16_t brightness256) {
    const uint8_t(*st)[3] = themeStops();
    const int n = themeStopCount();
    for (int i = 0; i < 256; i++) {
        const int scaled = i * n; // wrap: n segments, last blends into stop 0
        const int seg = scaled >> 8;
        const int f = scaled & 255;
        const int nextSeg = (seg + 1) % n;
        uint32_t c[3];
        for (int ch = 0; ch < 3; ch++) {
            const int v = st[seg][ch] + (((st[nextSeg][ch] - st[seg][ch]) * f) >> 8);
            c[ch] = (static_cast<uint32_t>(v) * brightness256) >> 8;
            if (c[ch] > 255) {
                c[ch] = 255;
            }
        }
        out[i] = rgb565(static_cast<uint8_t>(c[0]), static_cast<uint8_t>(c[1]), static_cast<uint8_t>(c[2]));
    }
}

} // namespace bganim

#endif // GAGGIMATE_SIM
