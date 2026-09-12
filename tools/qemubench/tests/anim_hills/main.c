/* Real-Xtensa execution check for the three PIE kernels in
 * src/display/ui/default/bganim/AnimHills.cpp: hillsFill8, hillsLerp8 and
 * hillsPack8. Neither tools/animbench/xtensa-asm14.sh (which proves the
 * mnemonics and the register allocation, never execution) nor the host
 * bench (where band() dispatches to the portable twins, because no compiler
 * here can assemble an EE.* block) actually runs these instructions. This
 * test does, under Espressif's qemu-system-xtensa fork, following the
 * precedent of tests/anim_kaleido/main.c and tests/anim_silk2/main.c: the
 * kernel bodies below are verbatim transcriptions of the ones in
 * AnimHills.cpp, with the mnemonics, operand names, immediates and load and
 * store order unchanged, and they are checked against plain C references.
 *
 * hillsLerp8 is the page's pcMix32 on one 8-bit channel held in a 16-bit
 * lane: d + (((f - d) * a) >> 8), with the shift arithmetic so a negative
 * step floors. The reference is that expression in C, and the sweep runs
 * every alpha in 0..256 over every ordering of the channel corners, so the
 * signed multiply's shift direction on a negative product is executed, not
 * assumed. hillsPack8 turns three planes into RGB565, and its reference is
 * the shift and or written out.
 *
 * CPENABLE is written once by main() because this is bare metal and there
 * is no lazy coprocessor-enable handler. The kernels themselves never touch
 * it, which is the rule for a production kernel: FreeRTOS enables CP3 per
 * task through the coprocessor-disabled exception, and that is also how
 * another task's vector state gets saved.
 *
 * No OS and no libc: printing goes straight to the UART0 FIFO register
 * (ESP32-S3 UART0 base 0x60000000), the same as every other test here.
 */
#include <stdint.h>

#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)

static void uart_putc(char c) { UART0_FIFO = (uint32_t)(uint8_t)c; }

static void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

static void uart_put_dec(int v) {
    if (v < 0) {
        uart_putc('-');
        v = -v;
    }
    char buf[12];
    int n = 0;
    if (v == 0) buf[n++] = '0';
    while (v > 0) {
        buf[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0) uart_putc(buf[--n]);
}

static void uart_put_hex16(uint32_t v) {
    static const char hex[] = "0123456789abcdef";
    for (int shift = 12; shift >= 0; shift -= 4) uart_putc(hex[(v >> shift) & 0xF]);
}

/* ------------------------------------------------------------------ */
/* The kernels, transcribed verbatim from AnimHills.cpp.               */
/* ------------------------------------------------------------------ */

__attribute__((noinline)) void hillsFill8(uint16_t *dst, const uint16_t *colV, int groups) {
    uint16_t *wr = dst;
    const uint16_t *cv = colV;
    asm volatile("ee.vld.128.ip q0, %[cv], 0\n"
                 "loopnez %[n], 1f\n"
                 "ee.vst.128.ip q0, %[wr], 16\n"
                 "1:\n"
                 : [wr] "+r"(wr), [cv] "+r"(cv)
                 : [n] "r"(groups)
                 : "memory");
}

__attribute__((noinline)) void hillsLerp8(uint16_t *dst, const uint16_t *fg, const uint16_t *aQ8, int groups) {
    uint16_t *rd = dst;
    uint16_t *wr = dst;
    const uint16_t *fv = fg;
    const uint16_t *av = aQ8;
    asm volatile("ssai 8\n"
                 "loopnez %[n], 1f\n"
                 "ee.vld.128.ip q2, %[av], 16\n"
                 "ee.vld.128.ip q0, %[fv], 16\n"
                 "ee.vld.128.ip q1, %[rd], 16\n"
                 "ee.vsubs.s16 q0, q0, q1\n"
                 "ee.vmul.s16 q0, q0, q2\n"
                 "ee.vadds.s16 q0, q0, q1\n"
                 "ee.vst.128.ip q0, %[wr], 16\n"
                 "1:\n"
                 : [rd] "+r"(rd), [wr] "+r"(wr), [fv] "+r"(fv), [av] "+r"(av)
                 : [n] "r"(groups)
                 : "memory");
}

__attribute__((noinline)) void hillsPack8(uint16_t *dst, const uint16_t *r, const uint16_t *g, const uint16_t *b,
                                          const uint16_t *ct, int groups) {
    uint16_t *wr = dst;
    const uint16_t *rp = r;
    const uint16_t *gp = g;
    const uint16_t *bp = b;
    const uint16_t *cp = ct;
    asm volatile("ee.vld.128.ip q5, %[cp], 16\n"
                 "ee.vld.128.ip q6, %[cp], 16\n"
                 "ee.vld.128.ip q7, %[cp], 16\n"
                 "loopnez %[n], 1f\n"
                 "ee.vld.128.ip q0, %[rp], 16\n"
                 "ee.vld.128.ip q1, %[gp], 16\n"
                 "ee.vld.128.ip q2, %[bp], 16\n"
                 "ssai 3\n"
                 "ee.vmul.u16 q0, q0, q5\n"
                 "ee.vmul.u16 q2, q2, q5\n"
                 "ssai 2\n"
                 "ee.vmul.u16 q1, q1, q5\n"
                 "ssai 0\n"
                 "ee.vmul.u16 q0, q0, q6\n"
                 "ee.vmul.u16 q1, q1, q7\n"
                 "ee.orq q0, q0, q1\n"
                 "ee.orq q0, q0, q2\n"
                 "ee.vst.128.ip q0, %[wr], 16\n"
                 "1:\n"
                 : [wr] "+r"(wr), [rp] "+r"(rp), [gp] "+r"(gp), [bp] "+r"(bp), [cp] "+r"(cp)
                 : [n] "r"(groups)
                 : "memory");
}

/* ------------------------------------------------------------------ */
/* References: the portable twins from AnimHills.cpp.                  */
/* ------------------------------------------------------------------ */

static void lerpRef(uint16_t *dst, const uint16_t *fg, const uint16_t *aQ8, int groups) {
    const int n = groups * 8;
    for (int i = 0; i < n; i++) {
        const int d = dst[i];
        dst[i] = (uint16_t)(d + (((fg[i] - d) * aQ8[i]) >> 8));
    }
}

static void packRef(uint16_t *dst, const uint16_t *r, const uint16_t *g, const uint16_t *b, int groups) {
    const int n = groups * 8;
    for (int i = 0; i < n; i++) dst[i] = (uint16_t)(((r[i] >> 3) << 11) | ((g[i] >> 2) << 5) | (b[i] >> 3));
}

static void fillRef(uint16_t *dst, uint16_t colour, int groups) {
    const int n = groups * 8;
    for (int i = 0; i < n; i++) dst[i] = colour;
}

/* ------------------------------------------------------------------ */
/* Fixtures. Every buffer a kernel touches is 16-byte aligned, because
 * ee.vld.128.ip and ee.vst.128.ip mask the low four address bits instead
 * of trapping. The production code gets the same guarantee from allocHot
 * and from a scalar prefix on the destination.                        */
/* ------------------------------------------------------------------ */

#define CAP 512
#define GUARD 16

/* ones, 2048, 32: the order the pack kernel loads them. */
static uint16_t constTab[3 * 8] __attribute__((aligned(16)));
static uint16_t bgBuf[CAP] __attribute__((aligned(16)));
static uint16_t fgBuf[CAP] __attribute__((aligned(16)));
static uint16_t alBuf[CAP] __attribute__((aligned(16)));
static uint16_t gBuf[CAP] __attribute__((aligned(16)));
static uint16_t bBuf[CAP] __attribute__((aligned(16)));
static uint16_t gotBuf[CAP + GUARD] __attribute__((aligned(16)));
static uint16_t wantBuf[CAP] __attribute__((aligned(16)));
static uint16_t colBuf[8] __attribute__((aligned(16)));

static int g_checked = 0;
static int g_bad = 0;
static int g_firstCase = -1, g_firstLane = -1;
static uint32_t g_firstGot = 0, g_firstWant = 0;

static void note(int caseId, int lane, uint32_t got, uint32_t want) {
    if (g_bad == 0) {
        g_firstCase = caseId;
        g_firstLane = lane;
        g_firstGot = got;
        g_firstWant = want;
    }
    g_bad++;
}

static void constInit(void) {
    for (int i = 0; i < 8; i++) {
        constTab[i] = 1;
        constTab[8 + i] = 2048;
        constTab[16 + i] = 32;
    }
}

/* One lerp call: run the kernel and the reference on the same destination,
 * compare every lane and the guard words past the written range. */
static void runLerp(int caseId, int groups) {
    const int n = groups * 8;
    for (int i = 0; i < GUARD; i++) gotBuf[CAP + i] = 0xA5A5;
    for (int i = 0; i < n; i++) {
        gotBuf[i] = bgBuf[i];
        wantBuf[i] = bgBuf[i];
    }
    /* A word past the range the kernel may not touch. */
    if (n < CAP) gotBuf[n] = 0x5C5C;

    hillsLerp8(gotBuf, fgBuf, alBuf, groups);
    lerpRef(wantBuf, fgBuf, alBuf, groups);

    for (int i = 0; i < n; i++) {
        g_checked++;
        if (gotBuf[i] != wantBuf[i]) note(caseId, i, gotBuf[i], wantBuf[i]);
    }
    if (n < CAP && gotBuf[n] != 0x5C5C) note(caseId + 2000, n, gotBuf[n], 0x5C5C);
    for (int i = 0; i < GUARD; i++) {
        if (gotBuf[CAP + i] != 0xA5A5) note(caseId + 3000, CAP + i, gotBuf[CAP + i], 0xA5A5);
    }
}

/* A cheap deterministic spread; no libc and no float here. */
static uint32_t rngState = 0x13579bdfu;
static uint32_t rnd(void) {
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
}

/* Case 1: every channel corner against every channel corner, at the alpha
 * values where the arithmetic can turn over. 0 and 255 are the extremes a
 * plane can hold; 8, 7, 248 and 252 are what a palette colour expands to
 * around a field boundary. */
static void testCorners(void) {
    static const uint16_t corner[8] = {0, 255, 8, 7, 248, 252, 128, 127};
    static const uint16_t alpha[8] = {0, 1, 2, 127, 128, 129, 255, 256};
    int caseId = 1;
    for (int b = 0; b < 8; b++) {
        for (int f = 0; f < 8; f++) {
            for (int i = 0; i < 8; i++) {
                bgBuf[i] = corner[b];
                fgBuf[i] = corner[f];
                alBuf[i] = alpha[i];
            }
            runLerp(caseId++, 1);
        }
    }
}

/* Case 2: the whole alpha range 0..256, eight lanes at a time, over the
 * darkest and brightest values in both directions and a mid pair, so every
 * alpha value the animation can produce is executed on a rising and on a
 * falling step. */
static void testAlphaSweep(void) {
    int caseId = 200;
    static const uint16_t pairs[4][2] = {{0, 255}, {255, 0}, {66, 165}, {165, 66}};
    for (int p = 0; p < 4; p++) {
        for (int a0 = 0; a0 <= 256; a0 += 8) {
            for (int i = 0; i < 8; i++) {
                bgBuf[i] = pairs[p][0];
                fgBuf[i] = pairs[p][1];
                const int a = a0 + i;
                alBuf[i] = (uint16_t)(a > 256 ? 256 : a);
            }
            runLerp(caseId++, 1);
        }
    }
}

/* Case 3: production geometry. A 16-pixel tile is what the animation's edge
 * pass hands the kernel, and a 480-pixel and a 240-pixel row are the widest
 * calls a plane can take. Weights come from the same two profiles the
 * animation builds, a quadratic haze and a smoothstep, so the operand
 * distribution is the real one and not only the corners. */
static void testProduction(void) {
    /* The haze profile, GLOW_PEAK 185 falling quadratically over 88 quarter
     * pixels, and the smoothstep, both as AnimHills.cpp builds them but in
     * integers so this file stays free of float. */
    static uint8_t glowTab[96];
    static uint16_t smooth[257];
    for (int k = 0; k < 96; k++) {
        const int num = k < 88 ? 88 - k : 0; /* (1 - u) in 88ths */
        glowTab[k] = (uint8_t)((185 * num * num + (88 * 88) / 2) / (88 * 88));
    }
    for (int k = 0; k <= 256; k++) {
        /* 256 * u * u * (3 - 2u) with u = k / 256, rounded, in 32-bit
         * integers: k * k * (768 - 2k) / 65536, whose largest value is
         * about 5e7, so no 64-bit helper call is needed in a freestanding
         * image. */
        smooth[k] = (uint16_t)((k * k * (768 - 2 * k) + 32768) / 65536);
    }

    int caseId = 400;
    for (int rep = 0; rep < 24; rep++) {
        for (int i = 0; i < 16; i++) {
            bgBuf[i] = (uint16_t)(rnd() & 255u);
            fgBuf[i] = (uint16_t)(rnd() & 255u);
            const int dd = (int)(rnd() % 5632u) - 2816; /* +-11 rows in Q8 */
            if (dd < 0) {
                const int gi = (-dd) >> 6;
                alBuf[i] = glowTab[gi > 95 ? 95 : gi];
            } else {
                int k = (dd * 18) >> 8; /* fadeInv for the 14 row layer */
                if (k > 256) k = 256;
                const int a = smooth[k];
                alBuf[i] = (uint16_t)(185 + (((256 - 185) * a) >> 8));
            }
        }
        runLerp(caseId++, 2);
    }
    /* Full width rows: 480 pixels is 60 groups, 240 is 30. */
    for (int wcase = 0; wcase < 2; wcase++) {
        const int n = wcase == 0 ? 480 : 240;
        for (int i = 0; i < n; i++) {
            bgBuf[i] = (uint16_t)(rnd() & 255u);
            fgBuf[i] = (uint16_t)(rnd() & 255u);
            alBuf[i] = (uint16_t)(rnd() % 257u);
        }
        runLerp(caseId++, n / 8);
    }
    /* Zero groups: loopnez has to skip the body and touch nothing. */
    for (int i = 0; i < 8; i++) {
        bgBuf[i] = 18;
        fgBuf[i] = 255;
        alBuf[i] = 256;
    }
    runLerp(caseId++, 0);
}

/* Case 4: the fill kernel, at every group count the animation can ask for
 * including zero, with a guard word after the range. */
static void testFill(void) {
    static const int counts[7] = {0, 1, 2, 3, 30, 59, 60};
    static const uint16_t colours[7] = {0, 255, 8, 252, 128, 7, 248};
    int caseId = 600;
    for (int c = 0; c < 7; c++) {
        const int groups = counts[c];
        const uint16_t colour = colours[c];
        for (int i = 0; i < 8; i++) colBuf[i] = colour;
        for (int i = 0; i < CAP + GUARD; i++) gotBuf[i] = 0xA5A5;
        for (int i = 0; i < CAP; i++) wantBuf[i] = 0xA5A5;

        hillsFill8(gotBuf, colBuf, groups);
        fillRef(wantBuf, colour, groups);

        const int n = groups * 8;
        for (int i = 0; i < n + 8; i++) {
            const uint16_t want = i < n ? wantBuf[i] : 0xA5A5;
            g_checked++;
            if (gotBuf[i] != want) note(caseId, i, gotBuf[i], want);
        }
        caseId++;
    }
}

/* Case 5: the pack kernel. Every channel value 0..255 goes through each
 * plane in turn (the other two planes carry a spread), then random planes
 * at the two production widths, then zero groups, all with guard words. */
static void runPack(int caseId, int groups) {
    const int n = groups * 8;
    for (int i = 0; i < GUARD; i++) gotBuf[CAP + i] = 0xA5A5;
    if (n < CAP) gotBuf[n] = 0x5C5C;
    hillsPack8(gotBuf, bgBuf, gBuf, bBuf, constTab, groups);
    packRef(wantBuf, bgBuf, gBuf, bBuf, groups);
    for (int i = 0; i < n; i++) {
        g_checked++;
        if (gotBuf[i] != wantBuf[i]) note(caseId, i, gotBuf[i], wantBuf[i]);
    }
    if (n < CAP && gotBuf[n] != 0x5C5C) note(caseId + 2000, n, gotBuf[n], 0x5C5C);
    for (int i = 0; i < GUARD; i++) {
        if (gotBuf[CAP + i] != 0xA5A5) note(caseId + 3000, CAP + i, gotBuf[CAP + i], 0xA5A5);
    }
}

static void testPack(void) {
    int caseId = 800;
    for (int plane = 0; plane < 3; plane++) {
        for (int i = 0; i < 256; i++) {
            const uint16_t sweep = (uint16_t)i;
            const uint16_t other = (uint16_t)((i * 37 + 11) & 255);
            bgBuf[i] = plane == 0 ? sweep : other;
            gBuf[i] = plane == 1 ? sweep : (uint16_t)(255 - other);
            bBuf[i] = plane == 2 ? sweep : (uint16_t)((other * 3) & 255);
        }
        runPack(caseId++, 32);
    }
    for (int wcase = 0; wcase < 2; wcase++) {
        const int n = wcase == 0 ? 480 : 240;
        for (int i = 0; i < n; i++) {
            bgBuf[i] = (uint16_t)(rnd() & 255u);
            gBuf[i] = (uint16_t)(rnd() & 255u);
            bBuf[i] = (uint16_t)(rnd() & 255u);
        }
        runPack(caseId++, n / 8);
    }
    runPack(caseId++, 0);
}

int main(void) {
    uint32_t cp = 8; /* CP3 only; bare metal has no lazy-enable handler. */
    asm volatile("wsr %0, cpenable\nrsync\n" ::"r"(cp) : "memory");
    constInit();

    testFill();
    testCorners();
    testAlphaSweep();
    testProduction();
    testPack();

    if (g_bad == 0) {
        uart_puts("GM_QEMUBENCH_PIE: PASS hillsFill8, hillsLerp8 and hillsPack8 bit-exact against their portable "
                  "twins, over 8-bit channel corners in both directions, the whole alpha range 0..256, "
                  "production 16 lane tiles with real haze and smoothstep weights, 480 and 240 lane rows, "
                  "every channel value through the pack, zero group calls, and guard words past every written "
                  "range. lanes=");
        uart_put_dec(g_checked);
        uart_puts(" mismatches=0\n");
    } else {
        uart_puts("GM_QEMUBENCH_PIE: FAIL kernel=hills case=");
        uart_put_dec(g_firstCase);
        uart_puts(" lane=");
        uart_put_dec(g_firstLane);
        uart_puts(" got=0x");
        uart_put_hex16(g_firstGot);
        uart_puts(" want=0x");
        uart_put_hex16(g_firstWant);
        uart_puts(" mismatches=");
        uart_put_dec(g_bad);
        uart_puts("\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");

    for (;;) {
        /* Spin so QEMU has a stable state; nothing to return to. */
    }
}
