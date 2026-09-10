/* Floor's three production kernels copied verbatim from AnimFloor.cpp.
 * Harness mode, freestanding C, no libc. The C reference uses the page's
 * signed row flatten/fog arithmetic and bandRef's Q16.16 pixel loop.
 * Checks all byte values and all flattening values, every fog value,
 * arbitrary Q16.16 steps including modulo-2^32 wraps, every dither nibble,
 * zero/odd/short/production widths, all legal output alignments, guards,
 * every RGB565 fill colour, and the original GCC loop transcription.
 */
#include <stdint.h>
#define GM_ANIM_IRAM
#include "gcc_baseline.h"
#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)
static void puts0(const char *s) { while (*s) UART0_FIFO = (uint8_t)*s++; }
static void dec(uint32_t v) {
    char s[11]; int n = 0;
    do { s[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) UART0_FIFO = (uint8_t)s[--n];
}
static uint32_t seed = 0xf100f123u;
static uint32_t rand32(void) {
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed;
}
static unsigned calls, lanes, mismatches;
static void check(int which, int lane, uint32_t got, uint32_t want) {
    if (got == want) return;
    if (mismatches++ == 0) {
        puts0("GM_QEMUBENCH_PIE: FAIL floor kernel="); dec((uint32_t)which);
        puts0(" call="); dec(calls); puts0(" lane="); dec((uint32_t)lane);
        puts0(" got="); dec(got); puts0(" want="); dec(want); puts0("\n");
    }
}

GM_ANIM_IRAM __attribute__((noinline)) void floorBuildRowAsm(int16_t *out, const uint8_t *src,
                                                           int fl, int anchor) {
    const uint32_t coeffs[3] = {106u, (uint32_t)fl, (uint32_t)anchor};
    const uint32_t *coeff = coeffs;
    int n = 4; // 64 texels / 16 bytes per vector input
    asm volatile("ee.vldbc.16.ip q2, %[coeff], 4\n"
                 "ee.vldbc.16.ip q3, %[coeff], 4\n"
                 "ee.vldbc.16.ip q4, %[coeff], 0\n"
                 "ssai 8\n"
                 "loopnez %[n], 1f\n"
                 "ee.vld.128.ip q0, %[src], 16\n"
                 "ee.zero.q q1\n"
                 "ee.vzip.8 q0, q1\n"
                 "ee.vsubs.s16 q0, q0, q2\n"
                 "ee.vsubs.s16 q1, q1, q2\n"
                 "ee.vmul.s16 q0, q0, q3\n"
                 "ee.vmul.s16 q1, q1, q3\n"
                 "ee.vadds.s16 q0, q0, q4\n"
                 "ee.vadds.s16 q1, q1, q4\n"
                 "ee.vst.128.ip q0, %[out], 16\n"
                 "ee.vst.128.ip q1, %[out], 16\n"
                 "1:\n"
                 : [out] "+&r"(out), [src] "+&r"(src), [coeff] "+&r"(coeff)
                 : [n] "r"(n)
                 : "memory");
}

GM_ANIM_IRAM __attribute__((noinline)) uint32_t floorPairRowAsm(uint16_t *out, const int16_t *row,
                                                              const uint16_t *pal, uint32_t u,
                                                              uint32_t du, uint32_t packed, int nPairs) {
    int t0, t1, t2, t3;
    asm volatile("ssai 8\n"
                 "loopnez %[n], 1f\n"
                 "extui %[t0], %[u], 16, 6\n"
                 "add %[u], %[u], %[du]\n"
                 "extui %[t1], %[u], 16, 6\n"
                 "add %[u], %[u], %[du]\n"
                 "addx2 %[t0], %[t0], %[row]\n"
                 "addx2 %[t1], %[t1], %[row]\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "extui %[t2], %[packed], 0, 4\n"
                 "extui %[t3], %[packed], 4, 4\n"
                 "add %[t0], %[t0], %[t2]\n"
                 "add %[t1], %[t1], %[t3]\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "src %[packed], %[packed], %[packed]\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t0], %[t0], %[t1]\n"
                 "s32i %[t0], %[out], 0\n"
                 "addi %[out], %[out], 4\n"
                 "1:\n"
                 : [out] "+&r"(out), [u] "+&r"(u), [packed] "+&r"(packed),
                   [t0] "=&r"(t0), [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3)
                 : [row] "r"(row), [pal] "r"(pal), [du] "r"(du), [n] "r"(nPairs)
                 : "memory");
    return u;
}

GM_ANIM_IRAM __attribute__((noinline)) void floorFillAsm(uint16_t *out, int n, int color) {
    while (n > 0 && ((uintptr_t)out & 15u)) {
        *out++ = (uint16_t)color;
        --n;
    }
    const uint32_t word = (uint32_t)color | ((uint32_t)color << 16);
    const int n8 = n >> 3;
    asm volatile("ee.movi.32.q q0, %[word], 0\n"
                 "ee.movi.32.q q0, %[word], 1\n"
                 "ee.movi.32.q q0, %[word], 2\n"
                 "ee.movi.32.q q0, %[word], 3\n"
                 "loopnez %[n], 1f\n"
                 "ee.vst.128.ip q0, %[out], 16\n"
                 "1:\n"
                 : [out] "+&r"(out)
                 : [word] "r"(word), [n] "r"(n8)
                 : "memory");
    for (int i = 0; i < (n & 7); i++) {
        out[i] = (uint16_t)color;
    }
}

#define CAP 512
static uint8_t texture[64] __attribute__((aligned(16)));
static int16_t folded[64] __attribute__((aligned(16)));
static int16_t reference[64] __attribute__((aligned(16)));
static uint16_t palette[768] __attribute__((aligned(16)));
static uint16_t got[CAP] __attribute__((aligned(16)));
static uint16_t want[CAP] __attribute__((aligned(16)));
static uint16_t baseline[CAP] __attribute__((aligned(16)));
static int16_t dither[8];

static void buildRowRef(int16_t *out, const uint8_t *src, int fl, int anchor) {
    for (int i = 0; i < 64; i++) out[i] = (int16_t)(anchor + (((int)src[i] - 106) * fl >> 8));
}
/* Independent scalar pixel specification, with the page's signed dither.
 * Using individual 16-bit stores also checks the kernel's word packing.
 */
static uint32_t rowRef(uint16_t *out, const int16_t *row, const uint16_t *pal,
                       uint32_t u, uint32_t du, const int16_t *dr, int n) {
    for (int x = 0; x < n; x++) {
        out[x] = pal[row[(u >> 16) & 63] + dr[x & 7] + 2];
        u += du;
    }
    return u;
}

static void testBuild(void) {
    static const int fogs[] = {0, 1, 254, 255};
    /* All 256*256 (texture,flat) combinations at four fog boundaries.
     * This includes the narrower production tex=40..170 range and both
     * signs of the shifted product, especially -1 rounded toward -infinity.
     */
    for (int fl = 0; fl < 256; fl++) {
        for (int f = 0; f < 4; f++) {
            for (int block = 0; block < 4; block++) {
                for (int i = 0; i < 64; i++) texture[i] = (uint8_t)(block * 64 + i);
                const int anchor = 424 - fogs[f];
                floorBuildRowAsm(folded, texture, fl, anchor);
                buildRowRef(reference, texture, fl, anchor);
                calls++;
                for (int i = 0; i < 64; i++) check(1, i, (uint16_t)folded[i], (uint16_t)reference[i]);
                lanes += 64;
            }
        }
    }
    for (int f = 0; f < 256; f++) {
        const int fl = (int)(rand32() & 255);
        for (int i = 0; i < 64; i++) texture[i] = (uint8_t)rand32();
        floorBuildRowAsm(folded, texture, fl, 424 - f);
        buildRowRef(reference, texture, fl, 424 - f);
        calls++;
        for (int i = 0; i < 64; i++) check(1, i, (uint16_t)folded[i], (uint16_t)reference[i]);
        lanes += 64;
    }
}

static void testGather(void) {
    static const int widths[] = {0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 63, 64, 127, 128, 233, 240, 466, 480};
    static const uint32_t steps[] = {
        0u, 1u, 65535u, 65536u, 65537u, 16908u, 303407u, /* production depth endpoints */
        0x003fffffu, 0x00400000u, 0x7fffffffu, 0x80000000u, 0xfffffffeu, 0xffffffffu
    };
    for (int pass = 0; pass < 8192; pass++) {
        const int n = widths[pass % (int)(sizeof(widths) / sizeof(widths[0]))];
        const int off = 8 + 2 * ((pass / 18) & 3); /* all four legal mod-16 alignments */
        uint32_t u = pass < 1024 ? ((uint32_t)(pass & 63) << 16) | ((pass & 64) ? 65535u : 0u) : rand32();
        const uint32_t du = pass < 4096 ? steps[(pass / 72) % 13] : rand32();
        uint32_t packed = 0;
        for (int i = 0; i < 8; i++) {
            /* Production nibbles are 0..4. The wider 0..15 input range
             * also checks the rotation with high bits set in all positions. */
            const uint32_t d = pass < 4096 ? rand32() % 5 : rand32() & 15;
            packed |= d << (i * 4); dither[i] = (int16_t)((int)d - 2);
        }
        for (int i = 0; i < 64; i++) {
            /* Full safe padded-palette range; include both endpoints. */
            folded[i] = (int16_t)(i == 0 ? 0 : i == 63 ? 752 : rand32() % 753);
        }
        /* Affine permutation: wrong palette indices produce distinct words. */
        const uint32_t salt = rand32();
        for (int i = 0; i < 768; i++) palette[i] = (uint16_t)(i * 4051u + salt);
        for (int i = 0; i < CAP; i++) got[i] = want[i] = baseline[i] = 0x5aa5;
        const uint32_t end = floorPairRowAsm(got + off, folded, palette, u, du, packed, n >> 1);
        const uint32_t expected = rowRef(want + off, folded, palette, u, du, dither, n);
        const uint32_t gccEnd = floorGccRowAsm(baseline + off, folded, dither, palette, u, du, n);
        /* The production band's exact odd tail, indexed by absolute x. */
        if (n & 1) got[off + n - 1] = palette[folded[(end >> 16) & 63] + dither[(n - 1) & 7] + 2];
        calls++;
        check(2, n, end, u + du * (uint32_t)(n & ~1));
        check(4, n, gccEnd, expected);
        for (int i = 0; i < CAP; i++) {
            check(2, i - off, got[i], want[i]);
            check(4, i - off, baseline[i], want[i]);
        }
        lanes += (unsigned)n;
    }
}

static void testFill(void) {
    /* Every RGB565 word, including all channel high bits, combined with
     * every 2-byte offset and zero, prefix-only, vector and tail lengths. */
    for (unsigned color = 0; color < 65536; color++) {
        const int off = 8 + (int)(color & 7);
        const int n = (int)((color >> 3) % 34);
        for (int i = 0; i < 64; i++) got[i] = 0x5aa5;
        floorFillAsm(got + off, n, (int)color);
        calls++;
        for (int i = 0; i < 64; i++) check(3, i - off, got[i], i >= off && i < off + n ? color : 0x5aa5);
        lanes += (unsigned)n;
    }
    static const int widths[] = {233, 240, 466, 480};
    for (int j = 0; j < 4; j++) {
        for (int off = 8; off < 16; off++) {
            for (int i = 0; i < CAP; i++) got[i] = 0x5aa5;
            floorFillAsm(got + off, widths[j], 0xf81f);
            calls++;
            for (int i = 0; i < CAP; i++) check(3, i - off, got[i], i >= off && i < off + widths[j] ? 0xf81f : 0x5aa5);
            lanes += (unsigned)widths[j];
        }
    }
}

int main(void) {
    /* Bare-metal harness only. Production leaves CPENABLE to FreeRTOS. */
    uint32_t cp = 8;
    asm volatile("wsr %0, cpenable\nisync\n" : : "r"(cp));
    testBuild(); testGather(); testFill();
    if (!mismatches) {
        puts0("GM_QEMUBENCH_PIE: PASS floor kernels bit-exact; calls="); dec(calls);
        puts0(" lanes="); dec(lanes); puts0(" mismatches=0 (row transform, Q16.16 wraps, dither, widths, alignment, guards, all RGB565 fills, GCC baseline)\n");
    } else {
        puts0("GM_QEMUBENCH_PIE: FAIL floor mismatches="); dec(mismatches); puts0("\n");
    }
    puts0("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
