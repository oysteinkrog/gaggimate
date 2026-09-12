/* Glint: freestanding execution of the production kernels, copied verbatim
 * from AnimGlint.cpp. The independent references keep the page's multiply
 * inside the pixel loop. No libc, allocation, float, or constructor runtime.
 *
 * Scale covers every byte times every A in 0..323, including brightness
 * 0/100 after the pulse and length taper. Pair tests cover every Q8 cursor
 * fraction and every K in 1..8160, including minimum four-pixel half widths.
 * Alignment tests include the page's 264-byte stride and scalar fallbacks.
 * Row tests exercise empty, clipped, one-pixel, odd-edge and full-width runs,
 * 480/240/466/233 geometry, all eight y phases and all four legal destination
 * alignments. Canaries surround every output, including zero-trip calls.
 */
#include <stdint.h>
#define GM_ANIM_IRAM
#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)

static void putsU(const char *s) {
    while (*s) {
        if (*s == '\n') UART0_FIFO = '\r';
        UART0_FIFO = (uint8_t)*s++;
    }
}
static void decU(uint32_t n) {
    char s[10];
    int used = 0;
    do { s[used++] = '0' + n % 10; n /= 10; } while (n);
    while (used) UART0_FIFO = s[--used];
}

/* Begin verbatim production functions. */
GM_ANIM_IRAM __attribute__((noinline)) void glintScaleAsm(uint16_t *out, const uint8_t *src, int A, int n) {
    while (n > 0 && (((uintptr_t)out | (uintptr_t)src) & 15u)) {
        *out++ = (uint16_t)((*src++ * A) >> 8);
        --n;
    }
    const int groups = n >> 4;
    const uint32_t aa = (uint32_t)A | ((uint32_t)A << 16);
    asm volatile("ee.movi.32.q q7, %[aa], 0\n"
                 "ee.movi.32.q q7, %[aa], 1\n"
                 "ee.movi.32.q q7, %[aa], 2\n"
                 "ee.movi.32.q q7, %[aa], 3\n"
                 "ssai 8\n"
                 "loopnez %[n], 1f\n"
                 "ee.vld.128.ip q0, %[src], 16\n"
                 "ee.zero.q q1\n"
                 "ee.vzip.8 q0, q1\n"
                 "ee.vmul.u16 q0, q0, q7\n"
                 "ee.vmul.u16 q1, q1, q7\n"
                 "ee.vst.128.ip q0, %[out], 16\n"
                 "ee.vst.128.ip q1, %[out], 16\n"
                 "1:\n"
                 : [out] "+&r"(out), [src] "+&r"(src)
                 : [aa] "r"(aa), [n] "r"(groups)
                 : "memory");
    for (int i = 0; i < (n & 15); i++) out[i] = (uint16_t)((src[i] * A) >> 8);
}

GM_ANIM_IRAM __attribute__((noinline)) void glintFillAsm(uint16_t *out, const uint16_t *colors, int x, int n) {
    while (n > 0 && (((uintptr_t)out & 15u) || ((uintptr_t)colors & 15u))) {
        *out++ = colors[x++ & 7];
        --n;
    }
    const int groups = n >> 3;
    if (groups > 0) {
        const uint16_t *pat = colors + 8;
        asm volatile("ee.vld.128.ip q0, %[pat], 0\n"
                     "loopnez %[n], 1f\n"
                     "ee.vst.128.ip q0, %[out], 16\n"
                     "1:\n"
                     : [out] "+&r"(out), [pat] "+&r"(pat)
                     : [n] "r"(groups)
                     : "memory");
    }
    x += groups * 8;
    for (int i = 0; i < (n & 7); i++) out[i] = colors[(x + i) & 7];
}

GM_ANIM_IRAM __attribute__((noinline)) uint32_t glintPairsAsm(uint16_t *out, const uint16_t *prof,
                                                            const uint16_t *pal, const uint16_t *bg,
                                                            uint32_t acc, int K, int x, int nPairs) {
    uint32_t t0, t1, t2, t3, cap;
    asm volatile("movi %[cap], 255\n"
                 "loopnez %[n], 1f\n"
                 "srli %[t0], %[acc], 8\n"
                 "add %[acc], %[acc], %[K]\n"
                 "srli %[t1], %[acc], 8\n"
                 "addx2 %[t0], %[t0], %[prof]\n"
                 "extui %[t2], %[x], 0, 3\n"
                 "addx2 %[t1], %[t1], %[prof]\n"
                 "addx2 %[t2], %[t2], %[bg]\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "l16ui %[t3], %[t2], 0\n"
                 "l16ui %[t2], %[t2], 2\n"
                 "add %[t0], %[t0], %[t3]\n"
                 "add %[t1], %[t1], %[t2]\n"
                 "min %[t0], %[t0], %[cap]\n"
                 "min %[t1], %[t1], %[cap]\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "add %[acc], %[acc], %[K]\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t0], %[t0], %[t1]\n"
                 "s32i %[t0], %[out], 0\n"
                 "addi %[out], %[out], 4\n"
                 "addi %[x], %[x], 2\n"
                 "1:\n"
                 : [out] "+&r"(out), [acc] "+&r"(acc), [x] "+&r"(x),
                   [t0] "=&r"(t0), [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3), [cap] "=&r"(cap)
                 : [prof] "r"(prof), [pal] "r"(pal), [bg] "r"(bg), [K] "r"(K), [n] "r"(nPairs)
                 : "memory");
    return acc;
}

GM_ANIM_IRAM __attribute__((noinline)) void glintRowAsm(uint16_t *row, const uint16_t *rec,
                                                      const uint8_t *prof, const uint16_t *pal,
                                                      const uint16_t *bg, uint16_t *scaled,
                                                      uint16_t *colors, int w) {
    const int phase = (int)((0u - (uintptr_t)row) & 15u) >> 1;
    for (int k = 0; k < 8; k++) {
        colors[k] = pal[bg[k]];
        colors[8 + k] = pal[bg[(phase + k) & 7]];
    }
    const int x0 = rec[0], x1 = rec[1];
    if (x1 <= x0) {
        glintFillAsm(row, colors, 0, w);
        return;
    }
    glintScaleAsm(scaled, prof, rec[4], 256);
    glintFillAsm(row, colors, 0, x0);
    int x = x0;
    uint32_t acc = rec[3];
    const int K = rec[2];
    if (x & 1) {
        int v = scaled[acc >> 8] + bg[x & 7];
        row[x++] = pal[v > 255 ? 255 : v];
        acc += K;
    }
    const int pairs = (x1 - x) >> 1;
    acc = glintPairsAsm(row + x, scaled, pal, bg, acc, K, x, pairs);
    x += pairs * 2;
    if (x < x1) {
        const int v = scaled[acc >> 8] + bg[x & 7];
        row[x] = pal[v > 255 ? 255 : v];
    }
    glintFillAsm(row + x1, colors, x1, w - x1);
}
/* End verbatim production functions. */

#define ROW_N 528
#define SCALE_N 288
#define GUARD 0xa65bu
static uint8_t source[8 * 264 + 32] __attribute__((aligned(16)));
static uint16_t scaleGot[SCALE_N] __attribute__((aligned(16)));
static uint16_t scaleWant[SCALE_N] __attribute__((aligned(16)));
static uint16_t got[ROW_N] __attribute__((aligned(16)));
static uint16_t want[ROW_N] __attribute__((aligned(16)));
static uint16_t scaled[256] __attribute__((aligned(16)));
static uint16_t pal[256] __attribute__((aligned(16)));
static uint16_t bg[8] __attribute__((aligned(16)));
static uint16_t colors[24] __attribute__((aligned(16)));
static uint32_t cases, scaleCases, pairCases, fillCases, rowCases;
static uint32_t mismatches, firstCase, firstLane, firstGot, firstWant;

static void mismatch(uint32_t lane, uint32_t a, uint32_t b) {
    if (a != b) {
        if (!mismatches) {
            firstCase = cases;
            firstLane = lane;
            firstGot = a;
            firstWant = b;
        }
        ++mismatches;
    }
}
static void clearPair(uint16_t *a, uint16_t *b, int n) {
    for (int i = 0; i < n; ++i) a[i] = b[i] = GUARD;
}
static void compare(const uint16_t *a, const uint16_t *b, int n) {
    for (int i = 0; i < n; ++i) mismatch((uint32_t)i, a[i], b[i]);
}
static void scaleRef(uint16_t *out, const uint8_t *src, int A, int n) {
    for (int i = 0; i < n; ++i) out[i] = (uint16_t)((src[i] * A) >> 8);
}
static uint32_t pairsRef(uint16_t *out, const uint16_t *prof, const uint16_t *palette,
                         const uint16_t *ground, uint32_t acc, int K, int x, int pairs) {
    for (int i = 0; i < pairs * 2; ++i) {
        unsigned v = prof[acc >> 8] + ground[(x + i) & 7];
        out[i] = palette[v > 255 ? 255 : v];
        acc += (uint32_t)K;
    }
    return acc;
}
static void rowRef(uint16_t *out, const uint16_t *rec, const uint8_t *prof, int w) {
    for (int x = 0; x < w; ++x) {
        int v = bg[x & 7];
        if (x >= rec[0] && x < rec[1]) {
            const unsigned acc = rec[3] + (unsigned)(x - rec[0]) * rec[2];
            v += (prof[acc >> 8] * rec[4]) >> 8;
        }
        out[x] = pal[v > 255 ? 255 : v];
    }
}
static void checkScale(int srcOff, int dstOff, int A, int n) {
    ++cases; ++scaleCases;
    clearPair(scaleGot, scaleWant, SCALE_N);
    glintScaleAsm(scaleGot + 8 + dstOff, source + srcOff, A, n);
    scaleRef(scaleWant + 8 + dstOff, source + srcOff, A, n);
    compare(scaleGot, scaleWant, SCALE_N);
}
static void checkPairs(uint32_t acc, int K, int x, int nPairs) {
    ++cases; ++pairCases;
    clearPair(got, want, ROW_N);
    uint32_t a = glintPairsAsm(got + 8, scaled, pal, bg, acc, K, x, nPairs);
    uint32_t b = pairsRef(want + 8, scaled, pal, bg, acc, K, x, nPairs);
    compare(got, want, ROW_N);
    mismatch(ROW_N, a, b);
}
static void testScale(void) {
    /* Every byte value appears once per 256 samples. Both actual source
     * alignments from the 264-byte page layout take the vector body. */
    for (int A = 0; A <= 323; ++A) {
        checkScale(0, 0, A, 256);
        checkScale(264, 0, A, 256);
    }
    static const int lengths[] = {0, 1, 7, 8, 9, 15, 16, 17, 31, 32, 127, 128, 233, 239, 240, 255, 256};
    for (int si = 0; si < 16; ++si) {
        for (int di = 0; di < 8; ++di) {
            for (unsigned l = 0; l < sizeof(lengths) / sizeof(lengths[0]); ++l) {
                checkScale(si, di, 0, lengths[l]);
                checkScale(si, di, 256, lengths[l]);
                checkScale(si, di, 323, lengths[l]);
            }
        }
    }
}
static void testPairs(void) {
    for (int i = 0; i < 256; ++i) scaled[i] = (uint16_t)((i * 127) % 322);
    for (int k = 0; k < 8; ++k) bg[k] = (uint16_t)(k * 4);
    /* All 65,536 cursor states and all four even background phases. Last
     * cursor uses a zero step to exercise index 255 twice without overflow. */
    for (uint32_t acc = 0; acc <= 65535u; ++acc) {
        checkPairs(acc, acc == 65535u ? 0 : 1, (int)(acc & 6u), 1);
    }
    for (int K = 1; K <= 8160; ++K) {
        unsigned acc = (unsigned)(K * 73) & 4095u;
        int pairs = (int)((65535u - acc) / (unsigned)K + 1u) / 2;
        if (pairs > 240) pairs = 240;
        checkPairs(acc, K, K & 6, pairs);
    }
    /* Every background value, including the production maximum 28 and
     * defensive full uint16 profile values. Saturation must precede gather. */
    for (int i = 0; i < 256; ++i) {
        for (int k = 0; k < 8; ++k) bg[k] = (uint16_t)i;
        scaled[i] = (uint16_t)(i * 257);
        checkPairs((unsigned)i << 8, 0, i & 6, 8);
    }
    checkPairs(65535u, 8160, 6, 0);
}
static void testFill(void) {
    for (int offset = 0; offset < 8; offset += 2) {
        uint16_t *row = got + 8 + offset;
        const int phase = (int)((0u - (uintptr_t)row) & 15u) >> 1;
        for (int k = 0; k < 8; ++k) colors[k] = (uint16_t)(k * 9362u);
        for (int k = 0; k < 8; ++k) colors[8 + k] = colors[(phase + k) & 7];
        for (int x = 0; x < 8; ++x) {
            for (int n = 0; n <= 480; ++n) {
                ++cases; ++fillCases;
                clearPair(got, want, ROW_N);
                glintFillAsm(row + x, colors, x, n);
                for (int i = 0; i < n; ++i) want[8 + offset + x + i] = colors[(x + i) & 7];
                compare(got, want, ROW_N);
            }
        }
    }
    /* Unaligned table fallback must not issue a rounded-down vector load. */
    for (int offset = 1; offset < 8; ++offset) {
        for (int k = 0; k < 16; ++k) colors[offset + k] = (uint16_t)(k * 4095u);
        ++cases; ++fillCases;
        clearPair(got, want, ROW_N);
        glintFillAsm(got + 8, colors + offset, 0, 480);
        for (int i = 0; i < 480; ++i) want[8 + i] = colors[offset + (i & 7)];
        compare(got, want, ROW_N);
    }
}
static void testRows(void) {
    static const int widths[] = {1, 2, 3, 7, 8, 9, 15, 16, 17, 63, 233, 240, 466, 480};
    static const int amplitudes[] = {0, 1, 99, 128, 170, 223, 229, 255, 256, 300, 323};
    for (unsigned wi = 0; wi < sizeof(widths) / sizeof(widths[0]); ++wi) {
        const int w = widths[wi];
        for (int ph = 0; ph < 8; ++ph) {
            for (int offset = 0; offset < 8; offset += 2) {
                for (int shape = 0; shape < 8; ++shape) {
                    uint16_t rec[5] = {0, (uint16_t)w, 1, 0, 0};
                    if (shape == 1) { rec[0] = 1; rec[1] = 0; }
                    if (shape == 2) { rec[0] = (uint16_t)(w / 3); rec[1] = rec[0] + 1; }
                    if (shape == 3) rec[1] = (uint16_t)((w + 1) / 2);
                    if (shape == 4) rec[0] = (uint16_t)(w / 2);
                    if (shape == 5 && w > 2) { rec[0] = 1; rec[1] = (uint16_t)(w - 1); }
                    if (shape == 6) { rec[0] = (uint16_t)(w - 1); rec[1] = (uint16_t)w; }
                    int n = rec[1] > rec[0] ? rec[1] - rec[0] : 0;
                    rec[3] = (uint16_t)((ph * 8191 + shape * 257) & 32767);
                    int K = n > 0 ? (65535 - rec[3]) / n : 8160;
                    if (K > 8160) K = 8160;
                    rec[2] = (uint16_t)K;
                    if (shape == 7) { rec[2] = 1; rec[3] = (uint16_t)(65536 - w); }
                    rec[4] = (uint16_t)amplitudes[(wi + ph + shape + offset) % 11];
                    for (int k = 0; k < 8; ++k) bg[k] = (uint16_t)((ph * 3 + k * 7) % 29);
                    ++cases; ++rowCases;
                    clearPair(got, want, ROW_N);
                    glintRowAsm(got + 8 + offset, rec, source + ph * 264, pal, bg, scaled, colors, w);
                    rowRef(want + 8 + offset, rec, source + ph * 264, w);
                    compare(got, want, ROW_N);
                }
            }
        }
    }
}
int main(void) {
    /* Bare-metal setup only. Production kernels never write CPENABLE. */
    uint32_t cp = 8;
    asm volatile("wsr %0, cpenable\nrsync\n" : : "r"(cp) : "memory");
    for (unsigned i = 0; i < sizeof(source); ++i) source[i] = (uint8_t)(i * 73u + 19u);
    for (int i = 0; i < 256; ++i) pal[i] = (uint16_t)(i * 251u + 7u);
    pal[0] = 0; pal[255] = 65535;
    testScale();
    testPairs();
    testFill();
    testRows();
    if (mismatches) {
        putsU("GM_QEMUBENCH_PIE: FAIL glint case="); decU(firstCase);
        putsU(" lane="); decU(firstLane);
        putsU(" got="); decU(firstGot);
        putsU(" want="); decU(firstWant);
        putsU(" mismatches="); decU(mismatches);
    } else {
        putsU("GM_QEMUBENCH_PIE: PASS glint cases="); decU(cases);
        putsU(" scale="); decU(scaleCases);
        putsU(" pairs="); decU(pairCases);
        putsU(" fill="); decU(fillCases);
        putsU(" rows="); decU(rowCases);
        putsU(" mismatches=0");
    }
    putsU("\nGM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
