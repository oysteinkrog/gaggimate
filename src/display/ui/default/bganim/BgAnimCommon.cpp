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

// Raised by alloc() when a qualifying table was refused internal DRAM only
// because the radios had not settled yet. Render-task written and read; a
// stale read costs one frame of delay, nothing else.
bool g_replaceWanted = false;
} // namespace

bool internalHasRoomFor(size_t size) {
    if (!radiosSettled()) {
        return false;
    }
    const size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    return freeInternal >= size + INTERNAL_RESERVE;
}

bool replaceWanted() { return g_replaceWanted && radiosSettled(); }

void clearReplaceWanted() { g_replaceWanted = false; }

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
    // the pool the network stack lives in.
    //
    // The freeing-on-switch fix this comment used to describe as eventual has
    // since landed: all 13 animations carry a release entry, SleepAnimation
    // calls prev.release() when the selection changes, and release() below
    // refunds the counter for the pool the pointer actually came from. So the
    // live total is now max-over-animations, not sum-over-animations, and the
    // 53 KB runaway above cannot recur by simply cycling the fleet. The budget
    // stays as a backstop, because it still bounds the things release() does
    // not reclaim -- the shared borrowed terms (sinLut, cosTableF, noiseTex256)
    // are held by whoever asked first and deliberately never freed -- and
    // because it is what catches a future animation that forgets its release
    // entry, which is a silent regression rather than a visible one.
    //
    // The cumulative budget stays as the backstop it was built to be, but it is
    // no longer what decides placement on its own: internalHasRoomFor() vetoes
    // any request that would take the free pool below the radios' reserve. See
    // INTERNAL_RESERVE for why a build-time budget alone was not enough.
    // Three independent reasons send a table to PSRAM, and they used to be
    // collapsed into one flag with one message. That message named the
    // cumulative budget and a missing release() entry, so a run where the
    // budget was untouched still reported "SRAM budget spent (0/28672) -- a
    // release() entry is likely missing" on every allocation. It sent a real
    // investigation looking for a leak that does not exist. Keep them apart.
    const bool overSizeLimit = size > SRAM_ALLOC_LIMIT;
    const bool overBudget = g_allocSram + size > SRAM_TOTAL_BUDGET;
    const bool poolTooTight = !internalHasRoomFor(size);
    if (overSizeLimit || overBudget || poolTooTight) {
        // Over the per-allocation limit is by design: those tables are bulk
        // sequential sweeps and PSRAM serves them fine, so logging every one
        // would be noise.
        if (!overSizeLimit) {
            if (overBudget) {
                // NOT expected under the release invariant above, and it
                // silently relocates a table that was sized to be
                // latency-sensitive -- which is how a per-row lookup ends up
                // paying a bus round trip per pixel. Warn every time: it is
                // rare, and it really does mean an animation forgot to release.
                log_w("bganim: %u B to PSRAM, SRAM budget spent (%u/%u) -- a release() entry is likely missing",
                      static_cast<unsigned>(size), static_cast<unsigned>(g_allocSram), static_cast<unsigned>(SRAM_TOTAL_BUDGET));
            } else {
                // The pool was too tight, which on this board is not an
                // anomaly but the permanent state. internalHasRoomFor() only
                // runs once both radios have claimed, and wants
                // size + INTERNAL_RESERVE (48 KB) free of DMA-capable internal
                // DRAM; measured on this panel that pool peaks at 18.6 KB with
                // WiFi and BLE up. So the SRAM placement path is unreachable
                // here by construction, every table lives in PSRAM, and
                // anim_sram reads 0 for the life of the process.
                //
                // That is the correct outcome -- the 48 KB reserve is what
                // stopped the animation eating the pool the network stack
                // needs, back when cycling the fleet took internal SRAM to
                // 53 KB and left the web UI permanently unreachable -- but it
                // does mean SRAM_TOTAL_BUDGET and the size-based placement
                // policy currently decide nothing. Anyone re-tuning either
                // should know that before measuring.
                //
                // Once per boot, because it is the steady state and not news.
                static bool reported = false;
                if (!reported) {
                    reported = true;
                    log_i("bganim: internal DRAM below the %u B radio reserve, all tables go to PSRAM",
                          static_cast<unsigned>(INTERNAL_RESERVE));
                }
                // Pre-settle refusals are provisional: the pool the check saw
                // is not the pool the animation will live with. Flag it so the
                // render task can re-run placement once the radios have
                // claimed (see replaceWanted() in the header).
                if (!radiosSettled()) {
                    g_replaceWanted = true;
                }
            }
        }
        log_d("bganim: %u B -> PSRAM (internal free %u, sram budget %u/%u)", static_cast<unsigned>(size),
              static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT)),
              static_cast<unsigned>(g_allocSram), static_cast<unsigned>(SRAM_TOTAL_BUDGET));
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
