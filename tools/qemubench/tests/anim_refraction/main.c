/* Freestanding instruction probes and, below, the production Refraction
 * kernel and an independent C reference. Harness mode supplies vectors and
 * the windowed ABI startup. Only this bare-metal main enables CP3. */
#include <stdint.h>

#define GM_ANIM_IRAM
#define ALIGN16 __attribute__((aligned(16)))
#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)

static void puts_uart(const char *s) {
    while (*s) {
        if (*s == '\n') UART0_FIFO = '\r';
        UART0_FIFO = (uint8_t)*s++;
    }
}

static void dec_uart(uint32_t v) {
    char b[12];
    int n = 0;
    do { b[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) UART0_FIFO = (uint8_t)b[--n];
}

static int failures;
static void bad(const char *stage, int call, int lane, uint32_t got, uint32_t want) {
    if (!failures) {
        puts_uart("GM_QEMUBENCH_PIE: FAIL "); puts_uart(stage);
        puts_uart(" call="); dec_uart(call);
        puts_uart(" lane="); dec_uart(lane);
        puts_uart(" got="); dec_uart(got);
        puts_uart(" want="); dec_uart(want); puts_uart("\n");
    }
    failures++;
}

/* Each min/max operation is isolated between known loads and a store.
 * These mnemonics and MOVI.32.A are outside ASM_BRIEF's previously executed
 * list, so this probe was built and run before writing the pixel kernel.
 * SRC.Q is also checked for all eight uint16 source alignments. */
static void instruction_probes(void) {
    static const int16_t a[8] ALIGN16 = {-32768, -1, 0, 1, 4079, 4080, 4081, 32767};
    static const int16_t b[8] ALIGN16 = {0, 0, 0, 0, 4080, 4080, 4080, 4080};
    int16_t mn[8] ALIGN16, mx[8] ALIGN16;
    const int16_t *ap = a, *bp = b;
    int16_t *np = mn, *xp = mx;
    asm volatile("ee.vld.128.ip q0, %[a], 0\n"
                 "ee.vld.128.ip q1, %[b], 0\n"
                 "nop\n"
                 "ee.vmin.s16 q2, q0, q1\n"
                 "ee.vst.128.ip q2, %[mn], 0\n"
                 "ee.vmax.s16 q3, q0, q1\n"
                 "ee.vst.128.ip q3, %[mx], 0\n"
                 : [a] "+r"(ap), [b] "+r"(bp), [mn] "+r"(np), [mx] "+r"(xp)
                 : : "memory");
    for (int i = 0; i < 8; i++) {
        int lo = a[i] < b[i] ? a[i] : b[i];
        int hi = a[i] > b[i] ? a[i] : b[i];
        if (mn[i] != lo) bad("vmin.s16", 0, i, mn[i], lo);
        if (mx[i] != hi) bad("vmax.s16", 0, i, mx[i], hi);
    }
    static const uint32_t words[4] ALIGN16 = {0x12345678u, 0x89abcdefu, 0xfedcba98u, 0x76543210u};
    uint32_t w0, w1, w2, w3;
    const uint32_t *wp = words;
    asm volatile("ee.vld.128.ip q0, %[p], 0\n"
                 "nop\n"
                 "ee.movi.32.a q0, %[w0], 0\n"
                 "ee.movi.32.a q0, %[w1], 1\n"
                 "ee.movi.32.a q0, %[w2], 2\n"
                 "ee.movi.32.a q0, %[w3], 3\n"
                 : [p] "+r"(wp), [w0] "=&r"(w0), [w1] "=&r"(w1), [w2] "=&r"(w2), [w3] "=&r"(w3)
                 : : "memory");
    if (w0 != words[0]) bad("movi.32.a", 0, 0, w0, words[0]);
    if (w1 != words[1]) bad("movi.32.a", 0, 1, w1, words[1]);
    if (w2 != words[2]) bad("movi.32.a", 0, 2, w2, words[2]);
    if (w3 != words[3]) bad("movi.32.a", 0, 3, w3, words[3]);

    uint16_t src[24] ALIGN16, dst[8] ALIGN16;
    for (int i = 0; i < 24; i++) src[i] = (uint16_t)(i * 1237 + 41);
    for (int off = 0; off < 8; off++) {
        const uint16_t *s = src + off;
        uint16_t *d = dst;
        asm volatile("ee.ld.128.usar.ip q0, %[s], 16\n"
                     "ee.ld.128.usar.ip q1, %[s], 16\n"
                     "nop\n"
                     "ee.src.q q2, q0, q1\n"
                     "ee.vst.128.ip q2, %[d], 0\n"
                     : [s] "+r"(s), [d] "+r"(d) : : "memory");
        for (int i = 0; i < 8; i++) {
            if (dst[i] != src[off + i]) bad("src.q", off, i, dst[i], src[off + i]);
        }
    }
}

/* GCC 14.2's original .L4 loop, transcribed before taking the PIE edge.
 * 37 instructions per pair, already scheduled without load-use stalls.
 * Kept as a third path in the test, not linked into production firmware. */
static __attribute__((noinline)) void gccPairAsm(uint16_t *out, const uint16_t *tex,
        const int16_t *d, const uint16_t *pal, int off, int add, int n) {
    int x = 0, cursor = off + 1, zero = 0;
    int t0, t1, t2, t3;
    asm volatile("loopnez %[n], 1f\n"
                 "addi.n %[t0], %[cursor], -1\n"
                 "extui %[t1], %[cursor], 0, 9\n"
                 "addi.n %[t2], %[x], 1\n"
                 "extui %[t0], %[t0], 0, 9\n"
                 "addx2 %[t1], %[t1], %[tex]\n"
                 "extui %[t2], %[t2], 0, 3\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "extui %[t3], %[x], 0, 3\n"
                 "addx2 %[t0], %[t0], %[tex]\n"
                 "addx2 %[t2], %[t2], %[d]\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "l16si %[t2], %[t2], 0\n"
                 "addx2 %[t3], %[t3], %[d]\n"
                 "l16si %[t3], %[t3], 0\n"
                 "add.n %[t1], %[t1], %[add]\n"
                 "add.n %[t0], %[t0], %[add]\n"
                 "add.n %[t1], %[t1], %[t2]\n"
                 "add.n %[t0], %[t0], %[t3]\n"
                 "srai %[t1], %[t1], 4\n"
                 "movi %[t3], 0xff\n"
                 "srai %[t0], %[t0], 4\n"
                 "min %[t1], %[t1], %[t3]\n"
                 "min %[t0], %[t0], %[t3]\n"
                 "max %[t1], %[t1], %[zero]\n"
                 "extui %[t1], %[t1], 0, 16\n"
                 "max %[t0], %[t0], %[zero]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "extui %[t0], %[t0], 0, 16\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t1], %[t1], %[t0]\n"
                 "s32i %[t1], %[out], 0\n"
                 "addi.n %[x], %[x], 2\n"
                 "addi.n %[out], %[out], 4\n"
                 "addi.n %[cursor], %[cursor], 2\n"
                 "1:\n"
                 : [out] "+&r"(out), [x] "+&r"(x), [cursor] "+&r"(cursor),
                   [t0] "=&r"(t0), [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3)
                 : [tex] "r"(tex), [d] "r"(d), [pal] "r"(pal), [add] "r"(add),
                   [zero] "r"(zero), [n] "r"(n)
                 : "memory");
}

/* BEGIN VERBATIM PRODUCTION KERNEL */
GM_ANIM_IRAM __attribute__((noinline)) void refractionRowAsm(uint16_t *out, const uint16_t *tex,
        const int16_t *d, const uint16_t *pal, const int16_t *caps, int off, int add, int w) {
    const uint32_t bias = (uint32_t)(uint16_t)add * 65537u; // two copies of signed add
    int x = 0;
    while (w - x >= 8) {
        // A 512-pixel chunk can start at any off without leaving the duplicate.
        // 512 is divisible by eight, so a subsequent chunk keeps Bayer phase.
        const int n = (w - x < 512 ? w - x : 512) >> 3;
        const uint16_t *src = tex + ((off + x) & 511);
        const int16_t *dp = d, *cp = caps;
        uint32_t t0, t1, pair;
        asm volatile("ee.ld.128.usar.ip q0, %[src], 16\n"
                     "ee.vld.128.ip q3, %[d], 0\n"
                     "ee.ld.128.usar.ip q1, %[src], 16\n"
                     "ee.vld.128.ip q5, %[cap], 0\n"
                     "ee.movi.32.q q4, %[bias], 0\n"
                     "ee.movi.32.q q4, %[bias], 1\n"
                     "ee.movi.32.q q4, %[bias], 2\n"
                     "ee.movi.32.q q4, %[bias], 3\n"
                     "ee.zero.q q6\n"
                     "ee.vadds.s16 q4, q4, q3\n"
                     "loopnez %[n], 1f\n"
                     "ee.src.q q2, q0, q1\n"
                     "ee.orq q0, q1, q1\n"
                     "ee.vadds.s16 q2, q2, q4\n"
                     "ee.vmax.s16 q2, q2, q6\n"
                     "ee.vmin.s16 q2, q2, q5\n"
                     "ee.movi.32.a q2, %[pair], 0\n"
                     "extui %[t0], %[pair], 4, 8\n"
                     "extui %[t1], %[pair], 20, 8\n"
                     "addx2 %[t0], %[t0], %[pal]\n"
                     "addx2 %[t1], %[t1], %[pal]\n"
                     "l16ui %[t0], %[t0], 0\n"
                     "l16ui %[t1], %[t1], 0\n"
                     "ee.movi.32.a q2, %[pair], 1\n"
                     "slli %[t1], %[t1], 16\n"
                     "or %[t0], %[t0], %[t1]\n"
                     "s32i %[t0], %[out], 0\n"
                     "extui %[t0], %[pair], 4, 8\n"
                     "extui %[t1], %[pair], 20, 8\n"
                     "addx2 %[t0], %[t0], %[pal]\n"
                     "addx2 %[t1], %[t1], %[pal]\n"
                     "l16ui %[t0], %[t0], 0\n"
                     "l16ui %[t1], %[t1], 0\n"
                     "ee.movi.32.a q2, %[pair], 2\n"
                     "slli %[t1], %[t1], 16\n"
                     "or %[t0], %[t0], %[t1]\n"
                     "s32i %[t0], %[out], 4\n"
                     "extui %[t0], %[pair], 4, 8\n"
                     "extui %[t1], %[pair], 20, 8\n"
                     "addx2 %[t0], %[t0], %[pal]\n"
                     "addx2 %[t1], %[t1], %[pal]\n"
                     "l16ui %[t0], %[t0], 0\n"
                     "l16ui %[t1], %[t1], 0\n"
                     "ee.movi.32.a q2, %[pair], 3\n"
                     "slli %[t1], %[t1], 16\n"
                     "or %[t0], %[t0], %[t1]\n"
                     "s32i %[t0], %[out], 8\n"
                     "extui %[t0], %[pair], 4, 8\n"
                     "extui %[t1], %[pair], 20, 8\n"
                     "addx2 %[t0], %[t0], %[pal]\n"
                     "addx2 %[t1], %[t1], %[pal]\n"
                     "l16ui %[t0], %[t0], 0\n"
                     "l16ui %[t1], %[t1], 0\n"
                     "ee.ld.128.usar.ip q1, %[src], 16\n"
                     "slli %[t1], %[t1], 16\n"
                     "or %[t0], %[t0], %[t1]\n"
                     "s32i %[t0], %[out], 12\n"
                     "addi %[out], %[out], 16\n"
                     "1:\n"
                     : [out] "+&r"(out), [src] "+&r"(src), [d] "+r"(dp), [cap] "+r"(cp),
                       [t0] "=&r"(t0), [t1] "=&r"(t1), [pair] "=&r"(pair)
                     : [bias] "r"(bias), [n] "r"(n), [pal] "r"(pal)
                     : "memory");
        x += n * 8;
    }
    // At most seven pixels, also covers widths below eight and odd widths.
    for (; x < w; x++) {
        int v = (tex[(off + x) & 511] + add + d[x & 7]) >> 4;
        v = v < 0 ? 0 : (v > 255 ? 255 : v);
        *out++ = pal[v];
    }
}

/* END VERBATIM PRODUCTION KERNEL */

/* Independent C translation of the page/bandRef inner loop. It keeps the
 * texture wrap, signed shift, and clamp after the shift, unlike the PIE path. */
static void refractionRowRef(uint16_t *out, const uint16_t *tex, const int16_t *d,
        const uint16_t *pal, int off, int add, int w) {
    for (int x = 0; x < w; x++) {
        int v = (tex[(x + off) & 511] + add + d[x & 7]) >> 4;
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        out[x] = pal[v];
    }
}

#define TEX_STORAGE 1032
#define BUFFER 1048
#define SENTINEL 0xa55a
static uint16_t tex[TEX_STORAGE] ALIGN16;
static uint16_t pal[256] ALIGN16;
static int16_t dither[64] ALIGN16;
static int16_t caps[8] ALIGN16;
static uint16_t got[BUFFER] ALIGN16, want[BUFFER] ALIGN16, gccGot[BUFFER] ALIGN16;
static uint32_t cases, pixels;

static void duplicate_texture(void) {
    for (int i = 512; i < TEX_STORAGE; i++) tex[i] = tex[i & 511];
}

static void check_case(int off, int add, int w, int phase, int alignment) {
    /* 4-byte aligned destinations at 0,4,8,12 mod 16, with eight sentinel
     * halfwords on either side. The PIE kernel never rounds an output down. */
    const int start = 8 + alignment * 2;
    const int end = start + w + 8;
    const int evenEnd = start + (w & ~1);
    for (int i = 0; i < end; i++) got[i] = want[i] = gccGot[i] = SENTINEL;
    const int16_t *d = dither + phase * 8;
    refractionRowRef(want + start, tex, d, pal, off, add, w);
    /* Alternate execution order, and reuse q registers across calls. */
    if (cases & 1) {
        refractionRowAsm(got + start, tex, d, pal, caps, off, add, w);
        gccPairAsm(gccGot + start, tex, d, pal, off, add, w >> 1);
    } else {
        gccPairAsm(gccGot + start, tex, d, pal, off, add, w >> 1);
        refractionRowAsm(got + start, tex, d, pal, caps, off, add, w);
    }
    for (int i = 0; i < end; i++) {
        if (got[i] != want[i]) bad("PIE/guard", cases, i, got[i], want[i]);
        const uint16_t v = i >= start && i < evenEnd ? want[i] : SENTINEL;
        if (gccGot[i] != v) bad("GCC transcription/guard", cases, i, gccGot[i], v);
    }
    cases++;
    pixels += (uint32_t)w;
}

static void operand_sweeps(void) {
    static const int widths[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17, 31,
                          32, 33, 233, 240, 466, 480, 511, 512, 513, 1025};
    /* Every Q4 texture value 0..4080, even fractional values the real texture
     * does not store. Every rowAdd -704..704 and dither -205..205 is visited.
     * The strides are coprime to the domains, avoiding gaps in the sweep. */
    for (int v = 0; v <= 4080; v++) {
        for (int i = 0; i < 512; i++) tex[i] = (uint16_t)((v + i * 97) % 4081);
        duplicate_texture();
        for (int i = 0; i < 64; i++) dither[i] = (int16_t)((v * 11 + i * 17) % 411 - 205);
        for (int i = 0; i < 256; i++) pal[i] = (uint16_t)((i * 257) ^ v);
        check_case(v & 511, v % 1409 - 704, widths[v % 24], v & 7, v & 3);
    }
    /* Every texture offset, every destination alignment, row/phase extremes,
     * and an actual full-width streaming wrap. */
    for (int i = 0; i < 512; i++) tex[i] = (uint16_t)((i * 16) & 4080);
    duplicate_texture();
    for (int i = 0; i < 64; i++) dither[i] = (i & 1) ? 205 : -205;
    for (int i = 0; i < 256; i++) pal[i] = (uint16_t)(i * 257);
    for (int off = 0; off < 512; off++) {
        for (int a = 0; a < 4; a++) {
            check_case(off, (off & 1) ? -704 : 704, 480, off & 7, a);
        }
    }
    /* Every possible RGB565 palette word is gathered. The texture runs
     * through all 256 indices twice, and the palette pages cover 0..65535. */
    for (int i = 0; i < 64; i++) dither[i] = 0;
    for (int page = 0; page < 256; page++) {
        for (int i = 0; i < 256; i++) pal[i] = (uint16_t)(page * 256 + i);
        check_case(page, 0, 512, page & 7, page & 3);
    }
}

/* Synthetic bounded sine samples for the parameter corner tests. The host
 * golden/page comparison verifies the actual shared sine table and picture.
 * This freestanding test needs no libm: its four-quadrant triangular samples
 * cover the same -512..512 operand range, including both endpoints. */
static int sample(uint32_t phase) {
    int i = (int)(phase & 1023);
    return i < 256 ? i * 2 : (i < 768 ? 1024 - i * 2 : i * 2 - 2048);
}

static void parameter_corners(void) {
    static const int widths[] = {480, 240, 466, 233};
    static const uint32_t times[] = {0, 1, 1990, 4960, 7930, 0x15555555u, 0x7fffffffu, 0xffffffffu};
    for (int c = 0; c < 16; c++) {
        const int speed = (c & 1) ? 100 : 0, bend = (c & 2) ? 100 : 0;
        const int width = (c & 4) ? 100 : 0, contrast = (c & 8) ? 100 : 0;
        const int low = 90 + width * 190 / 100, high = 150 - width * 110 / 100;
        const int amp = 280 + contrast * 380 / 100;
        for (int i = 0; i < 512; i++) {
            int v = (sample(i * 2) * low + sample(i * 4 + 300) * (low / 2) +
                     sample(i * 6 + 700) * high + sample(i * 10 + 150) * (high / 3)) / (3 * 512);
            int idx = 128 + v * amp / 512;
            tex[i] = (uint16_t)((idx < 0 ? 0 : (idx > 255 ? 255 : idx)) * 16);
        }
        duplicate_texture();
        for (int i = 0; i < 256; i++) pal[i] = (uint16_t)(i * 257);
        for (int i = 0; i < 64; i++) dither[i] = (int16_t)((i * 13 + c * 71) % 411 - 205);
        for (int wi = 0; wi < 4; wi++) {
            int h = widths[wi];
            for (int ti = 0; ti < 8; ti++) {
                uint32_t base = times[ti] * (uint32_t)(4 + speed * 44 / 100);
                uint32_t ph1 = base >> 9, ph2 = (base * 3u) >> 11, ph3 = base >> 12;
                for (int yi = 0; yi < 8; yi++) {
                    int y = yi < 3 ? yi : (yi == 3 ? 7 : (yi == 4 ? 8 :
                            (yi == 5 ? h / 2 : h + yi - 8)));
                    int b1 = sample(((y * (1536 * 16 / h)) >> 4) + ph1) * (bend * 90 / 100) / 256;
                    int b2 = sample(((y * (560 * 16 / h)) >> 4) + ph2) * (bend * 210 / 100) / 256;
                    int b3 = sample(((y * (240 * 16 / h)) >> 4) - ph3) * (bend * 150 / 100) / 256;
                    int off = (int)(((uint32_t)(b1 + b2 + b3 + 2048) + (base >> 9)) & 511);
                    int add = (sample(((y * 27) >> 4) - (base >> 8)) * 704) >> 9;
                    check_case(off, add, widths[wi], y & 7, yi & 3);
                }
            }
        }
    }
}

int main(void) {
    uint32_t cp = 8;
    asm volatile("wsr %0, cpenable\nrsync\n" : : "r"(cp) : "memory");
    instruction_probes();
    for (int i = 0; i < 8; i++) caps[i] = 4080;
    operand_sweeps();
    parameter_corners();
    if (!failures) {
        puts_uart("GM_QEMUBENCH_PIE: PASS refraction cases="); dec_uart(cases);
        puts_uart(" pixels="); dec_uart(pixels);
        puts_uart(" mismatches=0 (PIE, GCC transcription, Q4 ranges, offsets, tails, parameter corners)\n");
    }
    puts_uart("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
