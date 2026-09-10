/* Freestanding, harness-mode execution checks for Rain. The production
 * lane kernel and its scalar fallback are copied verbatim below. An
 * independent per pixel reference expresses the same result in a different
 * shape, so a lane boundary bug cannot agree with itself. No libc,
 * allocation, float or OS is needed.
 *
 * The design under test is sparse: one slot per eight pixel lane per row.
 * Slots 0..247 carry the streak profile and are gathered; slots 248..2047
 * are the flat face and are written from an eight pixel background pattern
 * with no gather at all. Both paths and the boundary between them are
 * covered here.
 *
 * Coverage: all 2048 phases at full width; the boundary slots 0, 1, 246,
 * 247, 248 and 2047 placed deliberately; rows whose lanes are all
 * background; rows of adjacent active lanes; all Q4 profile values
 * 1056..3648, which spans both parameter extreme heads (148 and 228) and
 * the floor 66; all dither offsets -179..179 against both Q4 bounds;
 * arbitrary RGB565 palettes including all zero and all one; widths 0 to
 * 480 including 233 and 466; and every halfword alignment of the output,
 * profile, dither, background and phase pointers, so the vector path and
 * the scalar fallback are both reached. Canaries surround every row.
 *
 * One production invariant gets its own case: profile slot 247 holds the
 * same Q4 floor the background pattern was built from, so a lane taken
 * through the gather at slot 247 must produce exactly the pixels the
 * background path writes at slot 248. That equality is what lets the fast
 * path skip the gather without moving a single pixel.
 */
#include <stdint.h>
#define GM_ANIM_IRAM
#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)

static void puts_uart(const char *s) {
    while (*s) {
        if (*s == '\n') UART0_FIFO = '\r';
        UART0_FIFO = (uint8_t)*s++;
    }
}

/* Isolated instruction probe, run before adopting MOVI.32.A in the
 * production kernel. Each selector must extract its own little-endian word.
 * VLD is already covered by the common PIE smoke test. */
static int probeMove(void) {
    const uint32_t words[4] __attribute__((aligned(16))) = {
        0x01234567u, 0x89abcdefu, 0x76543210u, 0xfedcba98u
    };
    const uint32_t *src = words;
    uint32_t a, b, c, d;
    asm volatile("ee.vld.128.ip q0, %[src], 0\n"
                 "nop\n"
                 "ee.movi.32.a q0, %[a], 0\n"
                 "ee.movi.32.a q0, %[b], 1\n"
                 "ee.movi.32.a q0, %[c], 2\n"
                 "ee.movi.32.a q0, %[d], 3\n"
                 : [src] "+&r"(src), [a] "=&r"(a), [b] "=&r"(b), [c] "=&r"(c), [d] "=&r"(d)
                 : : "memory");
    return a == words[0] && b == words[1] && c == words[2] && d == words[3];
}

/* ---- verbatim production scalar lane path (AnimRain.cpp) ---- */
typedef uint32_t RainPair __attribute__((__may_alias__));

GM_ANIM_IRAM __attribute__((always_inline)) inline void rainRowScalar(uint16_t *out, const uint16_t *phase,
    const uint16_t *tab, const int16_t *off, const uint16_t *palette, const uint16_t *bg, int y, int w) {
    const unsigned yBias = (unsigned)y + 232u;
    const int groups = ((uintptr_t)out & 3u) == 0 ? w >> 3 : 0;
    // Pack once per row. Background lanes then have only four stores,
    // with no per-pixel loads, palette gathers or index arithmetic.
    const uint32_t bg0 = (uint32_t)bg[0] | ((uint32_t)bg[1] << 16);
    const uint32_t bg1 = (uint32_t)bg[2] | ((uint32_t)bg[3] << 16);
    const uint32_t bg2 = (uint32_t)bg[4] | ((uint32_t)bg[5] << 16);
    const uint32_t bg3 = (uint32_t)bg[6] | ((uint32_t)bg[7] << 16);
    for (int lane = 0; lane < groups; lane++) {
        const unsigned slot = (phase[lane] + yBias) & 2047u;
        RainPair *packed = (RainPair *)(out + lane * 8);
        if (slot > 247u) {
            // Exactly one write per pixel, without a fill/overdraw pass.
            packed[0] = bg0;
            packed[1] = bg1;
            packed[2] = bg2;
            packed[3] = bg3;
        } else {
            for (int j = 0; j < 8; j += 2) {
                int a = (tab[slot * 8 + j] + off[j]) >> 4;
                int b = (tab[slot * 8 + j + 1] + off[j + 1]) >> 4;
                if (a < 0) a = 0; else if (a > 255) a = 255;
                if (b < 0) b = 0; else if (b > 255) b = 255;
                packed[j >> 1] = (uint32_t)palette[a] | ((uint32_t)palette[b] << 16);
            }
        }
    }
    // A trailing partial lane uses bounded halfword stores, never a full
    // vector access. The same path handles halfword-only aligned output.
    for (int x = groups * 8; x < w; x += 8) {
        const unsigned slot = (phase[x >> 3] + yBias) & 2047u;
        const int count = w - x < 8 ? w - x : 8;
        for (int j = 0; j < count; j++) {
            if (slot > 247u) out[x + j] = bg[j];
            else {
                int idx = (tab[slot * 8 + j] + off[j]) >> 4;
                if (idx < 0) idx = 0; else if (idx > 255) idx = 255;
                out[x + j] = palette[idx];
            }
        }
    }
}

/* ---- verbatim production vector kernel (AnimRain.cpp) ---- */
GM_ANIM_IRAM __attribute__((noinline)) void rainRowAsm(uint16_t *out, const uint16_t *phase,
    const uint16_t *tab, const int16_t *off, const uint16_t *palette, const uint16_t *bg, int y, int w) {
    const unsigned yBias = (unsigned)y + 232u;
    const int groups = (((uintptr_t)tab | (uintptr_t)off | (uintptr_t)bg | (uintptr_t)out) & 15u) == 0 ? w >> 3 : 0;
    if (groups > 0) {
        unsigned hi, lo, cap;
        const int16_t *ditherRow = off;
        const uint16_t *backgroundRow = bg;
        // Predecrement only inside asm, not C pointer arithmetic. The
        // in-loop increment then fills the VLD-to-VADDS dependency gap;
        // no access occurs before out, and out returns at the last group.
        asm volatile("ee.vld.128.ip q1, %[dith], 0\n"
                     "ee.vld.128.ip q2, %[bg], 0\n"
                     "addi    %[out], %[out], -16\n"
                     "loopnez %[n], 1f\n"
                     "l16ui   %[hi], %[phase], 0\n"
                     "movi    %[cap], 247\n"
                     "add     %[hi], %[hi], %[yb]\n"
                     "extui   %[hi], %[hi], 0, 11\n"
                     // Keep the conditional target close: a jump over all
                     // four gathers would need assembler branch relaxation.
                     "bgeu    %[cap], %[hi], 2f\n"
                     "addi    %[out], %[out], 16\n"
                     "ee.vst.128.ip q2, %[out], 0\n"
                     "j       3f\n"
                     "2:\n"
                     "slli    %[hi], %[hi], 4\n"
                     "add     %[hi], %[hi], %[tab]\n"
                     "ee.vld.128.ip q0, %[hi], 0\n"
                     "addi    %[out], %[out], 16\n"
                     "ee.vadds.s16 q0, q0, q1\n"
                     "ee.movi.32.a q0, %[hi], 0\n"
                     "extui   %[lo], %[hi], 4, 8\n"
                     "extui   %[hi], %[hi], 20, 8\n"
                     "addx2   %[lo], %[lo], %[pal]\n"
                     "addx2   %[hi], %[hi], %[pal]\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[hi], %[hi], %[lo]\n"
                     "s32i    %[hi], %[out], 0\n"
                     "ee.movi.32.a q0, %[hi], 1\n"
                     "extui   %[lo], %[hi], 4, 8\n"
                     "extui   %[hi], %[hi], 20, 8\n"
                     "addx2   %[lo], %[lo], %[pal]\n"
                     "addx2   %[hi], %[hi], %[pal]\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[hi], %[hi], %[lo]\n"
                     "s32i    %[hi], %[out], 4\n"
                     "ee.movi.32.a q0, %[hi], 2\n"
                     "extui   %[lo], %[hi], 4, 8\n"
                     "extui   %[hi], %[hi], 20, 8\n"
                     "addx2   %[lo], %[lo], %[pal]\n"
                     "addx2   %[hi], %[hi], %[pal]\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[hi], %[hi], %[lo]\n"
                     "s32i    %[hi], %[out], 8\n"
                     "ee.movi.32.a q0, %[hi], 3\n"
                     "extui   %[lo], %[hi], 4, 8\n"
                     "extui   %[hi], %[hi], 20, 8\n"
                     "addx2   %[lo], %[lo], %[pal]\n"
                     "addx2   %[hi], %[hi], %[pal]\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[hi], %[hi], %[lo]\n"
                     "s32i    %[hi], %[out], 12\n"
                     "3:\n"
                     // Branches must join before a real final instruction:
                     // jumping straight to LEND bypasses the LOOP back edge.
                     "addi    %[phase], %[phase], 2\n"
                     "1:\n"
                     : [out] "+&r"(out), [phase] "+&r"(phase), [dith] "+&r"(ditherRow), [bg] "+&r"(backgroundRow),
                       [hi] "=&r"(hi), [lo] "=&r"(lo), [cap] "=&r"(cap)
                     : [tab] "r"(tab), [pal] "r"(palette), [yb] "r"(yBias), [n] "r"(groups)
                     : "memory");
        out += 8;
    }
    const int remaining = w - groups * 8;
    if (remaining > 0) rainRowScalar(out, phase, tab, off, palette, bg, y, remaining);
}

/* Independent per pixel reference. It never reasons in lanes: it derives
 * the lane, the column and the slot for each x on its own. */
static void rainPixelRef(uint16_t *out, const uint16_t *phase, const uint16_t *tab,
                         const int16_t *off, const uint16_t *palette, const uint16_t *bg,
                         int y, int w) {
    for (int x = 0; x < w; x++) {
        const unsigned lane = (unsigned)x >> 3;
        const unsigned j = (unsigned)x & 7u;
        const unsigned slot = ((unsigned)phase[lane] + (unsigned)y + 232u) & 2047u;
        if (slot > 247u) {
            out[x] = bg[j];
        } else {
            int idx = (tab[slot * 8u + j] + off[j]) >> 4;
            if (idx < 0) idx = 0;
            else if (idx > 255) idx = 255;
            out[x] = palette[idx];
        }
    }
}

#define MAX_W 480
#define MAX_LANES 64
#define GUARD_N 512
#define TABLE_N (248 * 8)
static uint16_t tabStore[TABLE_N + 8] __attribute__((aligned(16)));
static uint16_t phaseStore[MAX_LANES + 8] __attribute__((aligned(16)));
static int16_t dithStore[16] __attribute__((aligned(16)));
static uint16_t bgStore[16] __attribute__((aligned(16)));
static uint16_t pal[256] __attribute__((aligned(16)));
static uint16_t got[GUARD_N] __attribute__((aligned(16)));
static uint16_t want[GUARD_N] __attribute__((aligned(16)));
static uint16_t alt[GUARD_N] __attribute__((aligned(16)));
static unsigned cases, pixels, bgLanes, activeLanes;
static int failed;

static void dec_uart(unsigned v) {
    char buf[12];
    int n = 0;
    do { buf[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) UART0_FIFO = (uint8_t)buf[--n];
}

static void reportMismatch(const char *kernel, int x, unsigned actual, unsigned expected) {
    if (!failed) {
        puts_uart("GM_QEMUBENCH_PIE: FAIL rain "); puts_uart(kernel);
        puts_uart(" case="); dec_uart(cases);
        puts_uart(" buffer_lane="); dec_uart((unsigned)x);
        puts_uart(" got="); dec_uart(actual);
        puts_uart(" want="); dec_uart(expected);
        puts_uart("\n");
    }
    failed = 1;
}

static void paletteFill(unsigned salt) {
    for (unsigned i = 0; i < 256; i++) {
        /* Multiplication by an odd number permutes the 16-bit space. */
        pal[i] = (uint16_t)((i * 257u + salt * 73u) ^ 0xa693u);
    }
}

/* One 11-bit phase per lane. Lanes step by an odd stride so a row mixes
 * background and active lanes and their boundary falls at varying x. */
static void phaseFill(uint16_t *phase, int lanes, unsigned base) {
    for (int lane = 0; lane < lanes; lane++) {
        phase[lane] = (uint16_t)((base + (unsigned)lane * 41u) & 2047u);
    }
}

static void phaseConst(uint16_t *phase, int lanes, unsigned value) {
    for (int lane = 0; lane < lanes; lane++) phase[lane] = (uint16_t)(value & 2047u);
}

static void tableFill(uint16_t *tab, unsigned salt) {
    for (unsigned slot = 0; slot < 248; slot++) {
        for (unsigned cls = 0; cls < 4; cls++) {
            unsigned q = 1056u + ((slot * 193u + cls * 401u + salt) % 2593u);
            if (slot == 247) q = 1056;
            tab[slot * 8 + cls] = tab[slot * 8 + 7 - cls] = (uint16_t)q;
        }
    }
}

static void ditherFill(int16_t *off, unsigned salt) {
    for (unsigned x = 0; x < 8; x++) off[x] = (int16_t)((int)((salt + x * 47u) % 359u) - 179);
}

/* Arbitrary background bits: the kernel must copy them, whatever they are. */
static void bgFill(uint16_t *bg, unsigned salt) {
    for (unsigned x = 0; x < 8; x++) bg[x] = (uint16_t)((salt * 6151u + x * 1237u) ^ 0x5c3bu);
}

/* The production pattern: the face colour the gather would have produced
 * at the floor sample, so both paths agree pixel for pixel. */
static void bgFromFloor(uint16_t *bg, const int16_t *off) {
    for (unsigned x = 0; x < 8; x++) {
        int idx = (1056 + off[x]) >> 4;
        if (idx < 0) idx = 0;
        else if (idx > 255) idx = 255;
        bg[x] = pal[idx];
    }
}

static void countLanes(const uint16_t *phase, int y, int w) {
    for (int lane = 0; lane * 8 < w; lane++) {
        const unsigned slot = ((unsigned)phase[lane] + (unsigned)y + 232u) & 2047u;
        if (slot > 247u) bgLanes++; else activeLanes++;
    }
}

static void runCase(int w, int y, int outputOffset, const uint16_t *phase,
                    const uint16_t *tab, const int16_t *off, const uint16_t *bg) {
    const int first = 8 + outputOffset;
    for (int i = 0; i < GUARD_N; i++) got[i] = want[i] = 0xa55a;
    rainRowAsm(got + first, phase, tab, off, pal, bg, y, w);
    rainPixelRef(want + first, phase, tab, off, pal, bg, y, w);
    cases++;
    pixels += (unsigned)w;
    countLanes(phase, y, w);
    for (int i = 0; i < GUARD_N; i++) {
        if (got[i] != want[i]) reportMismatch("PIE", i, got[i], want[i]);
    }
}

/* Slot 247 through the gather must equal slot 248 through the background
 * pattern. Both rows are rendered by the kernel itself. */
static void runFloorEquality(int w, int outputOffset) {
    const int first = 8 + outputOffset;
    ditherFill(dithStore, 7u);
    paletteFill(11u);
    tableFill(tabStore, 3u);
    bgFromFloor(bgStore, dithStore);
    /* y is 0, so the lane phase alone selects the slot. */
    phaseConst(phaseStore, MAX_LANES, (2048u + 247u - 232u) & 2047u);
    for (int i = 0; i < GUARD_N; i++) got[i] = alt[i] = 0xa55a;
    rainRowAsm(got + first, phaseStore, tabStore, dithStore, pal, bgStore, 0, w);
    phaseConst(phaseStore, MAX_LANES, (2048u + 248u - 232u) & 2047u);
    rainRowAsm(alt + first, phaseStore, tabStore, dithStore, pal, bgStore, 0, w);
    cases++;
    pixels += (unsigned)w;
    for (int i = 0; i < GUARD_N; i++) {
        if (got[i] != alt[i]) reportMismatch("floor equality", i, got[i], alt[i]);
    }
}

int main(void) {
    /* Bare metal only. Production relies on FreeRTOS's lazy CP3 enable. */
    const uint32_t enable = 255;
    asm volatile("wsr %0, cpenable\nrsync\n" : : "a"(enable) : "memory");
    if (!probeMove()) {
        puts_uart("GM_QEMUBENCH_PIE: FAIL rain MOVI.32.A probe\n");
        failed = 1;
    }
    /* Independent phase and y sweeps cross 2047->0 and every slot, mixing
     * background and active lanes within each row at full production
     * width. */
    tableFill(tabStore, 0);
    for (unsigned ph = 0; ph < 2048 && !failed; ph++) {
        phaseFill(phaseStore, MAX_LANES, ph);
        ditherFill(dithStore, ph);
        bgFill(bgStore, ph);
        paletteFill(ph);
        runCase(480, (int)((ph * 37u) & 2047u), (int)((ph & 3u) * 2u), phaseStore, tabStore, dithStore, bgStore);
    }
    /* The boundary slots placed deliberately, over every dither row, as a
     * uniform row (all background past 247, all adjacent active below it)
     * and as alternating lanes so an active lane always neighbours a
     * background lane. */
    {
        static const unsigned slots[] = {0u, 1u, 246u, 247u, 248u, 249u, 1024u, 2047u};
        for (unsigned si = 0; si < sizeof(slots) / sizeof(slots[0]) && !failed; si++) {
            for (int y = 0; y < 8 && !failed; y++) {
                const unsigned target = slots[si];
                ditherFill(dithStore, target + (unsigned)y);
                bgFill(bgStore, target);
                paletteFill(target);
                tableFill(tabStore, target);
                phaseConst(phaseStore, MAX_LANES, (2048u + target - 232u - (unsigned)y) & 2047u);
                runCase(480, y, 0, phaseStore, tabStore, dithStore, bgStore);
                for (int lane = 0; lane < MAX_LANES; lane++) {
                    phaseStore[lane] = (uint16_t)((lane & 1)
                        ? ((2048u + target - 232u - (unsigned)y) & 2047u)
                        : ((2048u + 900u - 232u - (unsigned)y) & 2047u));
                }
                runCase(480, y, 0, phaseStore, tabStore, dithStore, bgStore);
            }
        }
    }
    /* Every Q4 value including both parameter-extreme heads and the floor. */
    paletteFill(31);
    bgFill(bgStore, 5u);
    phaseConst(phaseStore, MAX_LANES, 0u);
    for (unsigned q = 1056; q <= 3648 && !failed; q++) {
        for (int cls = 0; cls < 4; cls++) {
            tabStore[232 * 8 + cls] = tabStore[232 * 8 + 7 - cls] = (uint16_t)q;
        }
        ditherFill(dithStore, q);
        runCase(16, 0, 0, phaseStore, tabStore, dithStore, bgStore);
    }
    /* Every signed offset, explicitly hitting both Q4 sum bounds. */
    for (int d = -179; d <= 179 && !failed; d++) {
        for (int j = 0; j < 8; j++) {
            dithStore[j] = (int16_t)d;
            unsigned q = (j == 0 || j == 7) ? 1056 : 3648;
            tabStore[232 * 8 + j] = (uint16_t)q;
        }
        runCase(8, 0, 0, phaseStore, tabStore, dithStore, bgStore);
    }
    /* The floor equality invariant at several widths and output offsets. */
    for (int oo = 0; oo < 8 && !failed; oo++) {
        runFloorEquality(480, oo);
        runFloorEquality(240, oo);
        runFloorEquality(17, oo);
    }
    /* Each input's halfword alignment varies independently. Odd output
     * offsets and unaligned vector sources must use the scalar fallback.
     * Aligned input with output 0/4/8/12 mod 16 exercises PIE plus S32I. */
    static const int widths[] = {0,1,2,3,7,8,9,15,16,17,31,32,33,233,240,466,480};
    for (unsigned wi = 0; wi < sizeof(widths) / sizeof(widths[0]) && !failed; wi++) {
        for (int outputOffset = 0; outputOffset < 8 && !failed; outputOffset++) {
            for (int mode = 0; mode < 5 && !failed; mode++) {
                for (int offset = 0; offset < 8 && !failed; offset++) {
                    uint16_t *tab = tabStore + (mode == 0 ? offset : 0);
                    int16_t *off = dithStore + (mode == 1 ? offset : 0);
                    uint16_t *phase = phaseStore + (mode == 2 ? offset : 0);
                    uint16_t *bg = bgStore + (mode == 4 ? offset : 0);
                    const unsigned ph = 2047u - (unsigned)offset;
                    tableFill(tab, wi + (unsigned)offset);
                    ditherFill(off, (unsigned)(outputOffset * 53 + offset));
                    bgFill(bg, (unsigned)(wi * 17 + offset));
                    phaseFill(phase, MAX_LANES, ph);
                    paletteFill(ph);
                    if (mode == 3) {
                        for (int i = 0; i < 256; i++) pal[i] = (offset & 1) ? 0xffff : 0;
                    }
                    runCase(widths[wi], outputOffset + offset, outputOffset, phase, tab, off, bg);
                }
            }
        }
    }
    if (!failed) {
        puts_uart("GM_QEMUBENCH_PIE: PASS rain cases="); dec_uart(cases);
        puts_uart(" pixels="); dec_uart(pixels);
        puts_uart(" bg_lanes="); dec_uart(bgLanes);
        puts_uart(" active_lanes="); dec_uart(activeLanes);
        puts_uart(" mismatches=0 guards=0; PIE lane kernel matches the per pixel C reference\n");
    }
    puts_uart("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
