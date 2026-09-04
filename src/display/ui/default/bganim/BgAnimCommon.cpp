#ifndef GAGGIMATE_SIM

#include "BgAnimCommon.h"
#include "BgAnim.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#if defined(CONFIG_BT_ENABLED)
#include <esp_bt.h>
#endif
#include <esp_memory_utils.h>
#include <atomic>
#include <math.h>

namespace bganim {

const int16_t *sinLut() {
    static int16_t *lut = nullptr;
    if (lut == nullptr) {
        lut = static_cast<int16_t *>(allocHotShared(SIN_N * sizeof(int16_t)));
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
// esp_ptr_external_ram() tests against the running target's real PSRAM window
// rather than a hardcoded one. The literal range this used to compare against
// (0x3C000000..0x3E000000) is the ESP32-S3's, so it silently mis-attributed
// every allocation on any other target -- and the numbers it feeds are the
// SRAM/PSRAM budget the bench reports and the alloc() threshold is tuned
// against, so being wrong there is worse than being merely non-portable.
bool isPsram(const void *p) { return esp_ptr_external_ram(p); }
} // namespace

namespace {
// Whether WiFi and BLE have already taken their internal DRAM.
//
// This gate matters more than the reserve below it. With bgAnimAllScreens the
// animation starts as soon as the UI is built, which is BEFORE Controller
// ::connect() brings up either radio -- measured on this board, it allocates
// with 89 KB of internal DRAM free, and WiFi (47.2 KB) plus BLE (40.7 KB) then
// take 88 KB of it. A free-space check at that moment is not conservative, it
// is simply looking at the wrong number: everything it sees is already spoken
// for. So until both radios have claimed, nothing here may take internal DRAM
// at all, whatever the reserve says.
//
// A build where a radio never starts must not wait forever for it, so the ones
// that skip a radio are compiled out of the test rather than polled.
bool radiosSettled() {
#ifndef GAGGIMATE_NO_RADIO
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) != ESP_OK) {
        return false; // ESP_ERR_WIFI_NOT_INIT: esp_wifi_init has not run yet
    }
#endif
#if defined(CONFIG_BT_ENABLED) && !defined(GM_FAKE_CONTROLLER)
    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED) {
        return false;
    }
#endif
    return true;
}
} // namespace

namespace {
// Written from the HTTP task, read on the render task.
std::atomic<size_t> g_internalReserve{INTERNAL_RESERVE};
}

size_t internalReserve() { return g_internalReserve.load(std::memory_order_relaxed); }

void setInternalReserve(size_t bytes) { g_internalReserve.store(bytes, std::memory_order_relaxed); }

bool internalHasRoomFor(size_t size) {
    if (!radiosSettled()) {
        return false;
    }
    const size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    return freeInternal >= size + g_internalReserve.load(std::memory_order_relaxed);
}

namespace {
// The hot slab (see the header). Static so its cost to internal DRAM is fixed
// at link time; 16-byte aligned so a PIE kernel can ee.vld a table straight
// from its start. The bottom end is per-animation and resets when its live
// count hits zero; the top end holds the shared tables for the life of the
// boot. Both watermarks are render-task state read over HTTP, and a torn read
// there misreports a diagnostic and nothing else.
alignas(16) uint8_t g_hotSlab[HOT_SLAB_BYTES];
size_t g_hotBottom = 0;
size_t g_hotTop = HOT_SLAB_BYTES;
size_t g_hotPeak = 0;
uint32_t g_hotLive = 0;
uint32_t g_hotFail = 0;

constexpr size_t hotRound(size_t n) { return (n + 15u) & ~static_cast<size_t>(15u); }
} // namespace

bool isHot(const void *p) {
    const uint8_t *q = static_cast<const uint8_t *>(p);
    return q >= g_hotSlab && q < g_hotSlab + HOT_SLAB_BYTES;
}

size_t hotUsed() { return g_hotBottom; }
size_t hotShared() { return HOT_SLAB_BYTES - g_hotTop; }
size_t hotPeak() { return g_hotPeak; }
uint32_t hotFailCount() { return g_hotFail; }

#ifdef GM_KBLOB
size_t hotReset() {
    const size_t leaked = g_hotBottom;
    g_hotBottom = 0;
    g_hotLive = 0;
    // Only the shared tables remain accounted for.
    g_allocSram = hotShared();
    return leaked;
}
#endif

void *allocHot(size_t size) {
    const size_t n = hotRound(size);
    // The shared term is reserved whether or not the shared tables exist
    // yet: they are built lazily on first use, and an animation that filled
    // the slab before calling sinLut() would otherwise push a table every
    // later animation borrows out to PSRAM for the life of the boot.
    const size_t limit = g_hotTop < HOT_SLAB_BYTES - HOT_SHARED_RESERVE ? g_hotTop : HOT_SLAB_BYTES - HOT_SHARED_RESERVE;
    if (g_hotBottom + n > limit) {
        g_hotFail++;
        log_w("bganim: %u B hot table does not fit the slab (%u used, %u shared, %u total), falling back to PSRAM",
              static_cast<unsigned>(size), static_cast<unsigned>(g_hotBottom), static_cast<unsigned>(hotShared()),
              static_cast<unsigned>(HOT_SLAB_BYTES));
        return alloc(size);
    }
    void *p = g_hotSlab + g_hotBottom;
    g_hotBottom += n;
    if (g_hotBottom > g_hotPeak) {
        g_hotPeak = g_hotBottom;
    }
    g_hotLive++;
    g_allocSram += size;
    return p;
}

void *allocHotShared(size_t size) {
    const size_t n = hotRound(size);
    if (g_hotTop < g_hotBottom + n) {
        g_hotFail++;
        log_w("bganim: %u B shared table does not fit the slab, falling back to PSRAM", static_cast<unsigned>(size));
        return alloc(size);
    }
    g_hotTop -= n;
    g_allocSram += size;
    return g_hotSlab + g_hotTop;
}

void *alloc(size_t size) {
    // PSRAM by policy (see the header). The internal fall-back is for a build
    // without PSRAM or one that has exhausted it; on this board it never runs.
    void *p = ps_malloc(size);
    if (p == nullptr) {
        p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (p != nullptr) {
            log_w("bganim: %u B in internal SRAM (PSRAM exhausted)", static_cast<unsigned>(size));
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
    if (isHot(p)) {
        const uint8_t *q = static_cast<const uint8_t *>(p);
        if (q < g_hotSlab + g_hotTop) {
            g_allocSram = (g_allocSram > size) ? g_allocSram - size : 0;
            // Bottom end. The most recent table pops straight back so a
            // free-and-reallocate of one table (a palette rebuilt on a theme
            // change) reuses its own bytes; anything older waits for the
            // region reset, which is why in-place rebuilds are preferred.
            if (q + hotRound(size) == g_hotSlab + g_hotBottom) {
                g_hotBottom = static_cast<size_t>(q - g_hotSlab);
            }
            if (g_hotLive > 0 && --g_hotLive == 0) {
                g_hotBottom = 0;
            }
        }
        // Shared (top end) tables are never returned; a release() on one is
        // an animation dropping a borrowed pointer, which costs nothing.
        p = nullptr;
        return;
    }
    // Decrement the pool the pointer actually came from: a request can have
    // fallen back to the other pool when its own was exhausted.
    size_t &counter = isPsram(p) ? g_allocPsram : g_allocSram;
    counter = (counter > size) ? counter - size : 0;
    heap_caps_free(p);
    p = nullptr;
}

const float *cosTableF() {
    static float *lut = nullptr;
    if (lut == nullptr) {
        lut = static_cast<float *>(allocHotShared(256 * sizeof(float)));
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

float ditherAmp(const uint16_t *pal, int n) {
    int cr = 0, cg = 0, cb = 0;
    for (int i = 1; i < n; i++) {
        const uint16_t p = pal[i - 1], q = pal[i];
        if (((q >> 11) & 0x1F) != ((p >> 11) & 0x1F)) {
            cr++;
        }
        if (((q >> 5) & 0x3F) != ((p >> 5) & 0x3F)) {
            cg++;
        }
        if ((q & 0x1F) != (p & 0x1F)) {
            cb++;
        }
    }
    const int flat = n >> 6; // flatter than this and the channel is a constant
    int steps = n;
    if (cr > flat && cr < steps) {
        steps = cr;
    }
    if (cg > flat && cg < steps) {
        steps = cg;
    }
    if (cb > flat && cb < steps) {
        steps = cb;
    }
    if (steps < 1) {
        steps = 1;
    }
    const float a = 0.5f * static_cast<float>(n) / static_cast<float>(steps);
    return a > 16.0f ? 16.0f : a;
}

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
    // 16-byte aligned, which is a hard requirement rather than a preference:
    // AnimNebula walks its rows with the PIE vector unit, and that path seeds
    // SAR_BYTE from row+1 through EE.LD.128.USAR.IP, which forces the low four
    // address bits of the access to zero. On an unaligned base the first load
    // of row 0 would reach up to fifteen bytes behind this allocation. The
    // interpolated values would still come out right, because SAR_BYTE
    // captures the true offset, so this would not show up as wrong pixels --
    // only as a read of memory the animation does not own.
#if defined(__XTENSA__)
    // Sixteen bytes of slack past the last row, so AnimNebula's vector walk can
    // read the aligned block one past a row without a special case for row 255.
    // Every row then costs one scalar fixup (the wrap at index 255) instead of
    // sixteen. This is a simplification, not a speedup: measured against the
    // special-cased version the band time was 17930 us either way (17921 with
    // the slack), which is inside the run-to-run noise on this board.
    uint8_t *t = static_cast<uint8_t *>(heap_caps_aligned_alloc(16, 256 * 256 + 16, MALLOC_CAP_SPIRAM));
    if (t == nullptr) {
        t = static_cast<uint8_t *>(alloc(256 * 256));
    }
#else
    uint8_t *t = static_cast<uint8_t *>(alloc(256 * 256 + 16));
#endif
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
uint8_t g_themeBuf[2][BG_THEME_MAX_STOPS][3] = {
    {{0x08, 0x04, 0x02}, {0x2a, 0x12, 0x06}, {0x6b, 0x34, 0x13}, {0xb8, 0x70, 0x3a}, {0xe8, 0xb2, 0x68}, {0xf8, 0xe6, 0xc8}},
    {{0x08, 0x04, 0x02}, {0x2a, 0x12, 0x06}, {0x6b, 0x34, 0x13}, {0xb8, 0x70, 0x3a}, {0xe8, 0xb2, 0x68}, {0xf8, 0xe6, 0xc8}},
};
// Stop positions, 0..255, ascending, first 0 and last 255. Only consulted
// when g_themeUniform is false: uniform themes (every built-in, and any
// custom string without positions) keep the original equal-spacing
// arithmetic bit for bit, which is what tools/animbench's goldens encode.
uint8_t g_themePos[2][BG_THEME_MAX_STOPS] = {{0}, {0}};
bool g_themeUniform[2] = {true, true};
int g_themeCount[2] = {6, 6};
volatile uint32_t g_themeGen = 0;

// The stops as the theme (or the user's hex string) defines them, before tone.
// Kept separately because brightness and knee have to be re-appliable without
// the caller re-resolving the theme, and applying them in place would compound:
// two brightness writes would multiply, and a knee would clamp against the
// already-kneed values rather than the original ones.
uint8_t g_rawStops[BG_THEME_MAX_STOPS][3] = {
    {0x08, 0x04, 0x02}, {0x2a, 0x12, 0x06}, {0x6b, 0x34, 0x13}, {0xb8, 0x70, 0x3a}, {0xe8, 0xb2, 0x68}, {0xf8, 0xe6, 0xc8},
};
uint8_t g_rawPos[BG_THEME_MAX_STOPS] = {0};
bool g_rawUniform = true;
int g_rawCount = 6;
int g_brightness256 = 256; // Q8, 256 = unchanged
int g_knee = 255;          // 255 = shoulder off

// Applies the tone to g_rawStops and publishes the result. Integer throughout:
// this runs on a settings write, but the same arithmetic has to be describable
// to the preview UI, and exact integer steps make the two agree.
void publishStops() {
    const uint32_t next = g_themeGen + 1;
    const int buf = next & 1;
    const int knee = g_knee;
    const int bright = g_brightness256;
    for (int i = 0; i < g_rawCount; i++) {
        for (int c = 0; c < 3; c++) {
            int v = g_rawStops[i][c];
            // Shoulder first, then brightness. The knee is a property of the
            // theme's shape and is specified against full scale, so it has to
            // act on the theme's own values; brightness then scales whatever
            // shape came out. The other order would move the knee wherever
            // brightness happened to be set.
            if (v > knee) {
                v = knee + ((v - knee) >> 2);
            }
            v = (v * bright) >> 8;
            if (v < 0) {
                v = 0;
            } else if (v > 255) {
                v = 255;
            }
            g_themeBuf[buf][i][c] = static_cast<uint8_t>(v);
        }
    }
    for (int i = 0; i < g_rawCount; i++) {
        g_themePos[buf][i] = g_rawPos[i];
    }
    g_themeUniform[buf] = g_rawUniform;
    g_themeCount[buf] = g_rawCount;
    g_themeGen = next;
}

// Positions as a positional theme would carry them: p_i = i * 255 / (n - 1).
// Stored for uniform themes too so themeStopPositions() always answers.
void fillUniformPositions(uint8_t *pos, int n) {
    for (int i = 0; i < n; i++) {
        pos[i] = static_cast<uint8_t>((i * 255) / (n - 1));
    }
}
} // namespace

void setThemeStops(const uint8_t (*stops)[3], int nStops) {
    if (stops == nullptr || nStops < 2) {
        return;
    }
    if (nStops > BG_THEME_MAX_STOPS) {
        nStops = BG_THEME_MAX_STOPS;
    }
    for (int i = 0; i < nStops; i++) {
        for (int c = 0; c < 3; c++) {
            g_rawStops[i][c] = stops[i][c];
        }
    }
    g_rawCount = nStops;
    fillUniformPositions(g_rawPos, nStops);
    g_rawUniform = true;
    publishStops();
}

void setThemeStopsPos(const uint8_t (*stops)[3], const uint8_t *pos, int nStops) {
    if (pos == nullptr) {
        setThemeStops(stops, nStops);
        return;
    }
    if (stops == nullptr || nStops < 2) {
        return;
    }
    if (nStops > BG_THEME_MAX_STOPS) {
        nStops = BG_THEME_MAX_STOPS;
    }
    // Positions must be ascending; a caller that hands over something else
    // gets it repaired rather than a gradient that reads backwards for part
    // of its range. Outside [pos[0], pos[n-1]] the end colours hold flat.
    int prev = 0;
    for (int i = 0; i < nStops; i++) {
        for (int c = 0; c < 3; c++) {
            g_rawStops[i][c] = stops[i][c];
        }
        int p = pos[i];
        if (p < prev) {
            p = prev;
        }
        g_rawPos[i] = static_cast<uint8_t>(p);
        prev = p;
    }
    g_rawCount = nStops;
    g_rawUniform = false;
    publishStops();
}

void setThemeTone(int brightness256, int knee) {
    if (brightness256 < 0) {
        brightness256 = 0;
    } else if (brightness256 > 256) {
        brightness256 = 256;
    }
    if (knee < 0) {
        knee = 0;
    } else if (knee > 255) {
        knee = 255;
    }
    if (brightness256 == g_brightness256 && knee == g_knee) {
        return; // no generation bump, so no palette rebuild across the fleet
    }
    g_brightness256 = brightness256;
    g_knee = knee;
    publishStops();
}

uint32_t themeGen() { return g_themeGen; }
int themeStopCount() { return g_themeCount[g_themeGen & 1]; }
const uint8_t (*themeStops())[3] { return g_themeBuf[g_themeGen & 1]; }
const uint8_t *themeStopPositions() { return g_themePos[g_themeGen & 1]; }
bool themeUniform() { return g_themeUniform[g_themeGen & 1]; }

void themeRGB(int pos, uint8_t out[3]) {
    const int gen = g_themeGen & 1;
    const uint8_t(*st)[3] = g_themeBuf[gen];
    const int n = g_themeCount[gen];
    if (pos < 0) {
        pos = 0;
    } else if (pos > 255) {
        pos = 255;
    }
    if (g_themeUniform[gen]) {
        const int scaled = pos * (n - 1); // 0 .. 255*(n-1)
        const int seg = scaled >> 8;      // stop index
        const int f = scaled & 255;       // blend within segment
        for (int c = 0; c < 3; c++) {
            out[c] = static_cast<uint8_t>(st[seg][c] + (((st[seg + 1][c] - st[seg][c]) * f) >> 8));
        }
        return;
    }
    // Positional: find the segment holding pos. n is at most 16 and this runs
    // at palette-build time, so a linear scan is the right tool. Before the
    // first stop and after the last the end colour holds, as in CSS.
    const uint8_t *p = g_themePos[gen];
    if (pos <= p[0]) {
        for (int c = 0; c < 3; c++) {
            out[c] = st[0][c];
        }
        return;
    }
    if (pos >= p[n - 1]) {
        for (int c = 0; c < 3; c++) {
            out[c] = st[n - 1][c];
        }
        return;
    }
    int seg = 0;
    while (seg < n - 2 && pos >= p[seg + 1]) {
        seg++;
    }
    const int width = p[seg + 1] - p[seg];
    const int f = width > 0 ? ((pos - p[seg]) * 256) / width : 0; // 0..256
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
    if (!themeUniform()) {
        // Positional theme: the ramp keeps its shape over the first
        // 256 - W entries and the remaining W blend the last stop back into
        // the first, W being one uniform segment's width so the wrap costs the
        // same share of the wheel a uniform theme spends on it.
        const int wrap = 256 / n;
        const int rampLen = 256 - wrap;
        for (int i = 0; i < 256; i++) {
            uint8_t c[3];
            if (i < rampLen) {
                themeRGB((i * 255) / (rampLen - 1), c);
            } else {
                const int f = ((i - rampLen) * 256) / wrap;
                for (int ch = 0; ch < 3; ch++) {
                    c[ch] = static_cast<uint8_t>(st[n - 1][ch] + (((st[0][ch] - st[n - 1][ch]) * f) >> 8));
                }
            }
            uint32_t r = (c[0] * brightness256) >> 8;
            uint32_t g = (c[1] * brightness256) >> 8;
            uint32_t b = (c[2] * brightness256) >> 8;
            out[i] = rgb565(static_cast<uint8_t>(r > 255 ? 255 : r), static_cast<uint8_t>(g > 255 ? 255 : g),
                            static_cast<uint8_t>(b > 255 ? 255 : b));
        }
        return;
    }
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
