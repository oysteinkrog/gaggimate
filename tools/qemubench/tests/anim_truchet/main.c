/* Freestanding execution check for AnimTruchet.cpp. The three production
 * functions below are verbatim copies, including their constraints and
 * noinline wrapper. GM_ANIM_IRAM is empty only because the harness linker
 * already puts the entire image in IRAM. The C reference retains bandRef's
 * separate tile copy, screen-anchored dither add, shift, clamp and palette
 * gather. Synthetic tiles span every reachable Q4 value, not just one image.
 * The bilinear blend (truchetLerpAsm, gm-kh2s) is checked against a plain C
 * transcription of bandRef's lerpQ4 at every fraction pair, with guards on
 * both sides of the in-place output. No libc, allocator or libm. Only this
 * bare-metal main enables CP3.
 */
#include <stdint.h>
#define GM_ANIM_IRAM
#define TILE 128
#define SCAN 640
#define GUARD 0xa55a
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
    do { b[n++] = '0' + v % 10; v /= 10; } while (v);
    while (n) UART0_FIFO = b[--n];
}

GM_ANIM_IRAM __attribute__((noinline)) void truchetScanAsm(uint16_t *out, const uint16_t *tileRow,
                                                         const uint8_t *orientation, const int16_t *offset,
                                                         uint32_t hash, int cells) {
    const uint16_t *src;
    uint32_t v, blocks;
    asm volatile("ee.vld.128.ip q7, %[off], 0\n"
                 "beqz    %[cells], 3f\n"
                 "2:\n"
                 "extui   %[v], %[hash], 0, 8\n"
                 "add     %[v], %[v], %[orient]\n"
                 "l8ui    %[v], %[v], 0\n"
                 "addi    %[hash], %[hash], 7\n"
                 "extui   %[v], %[v], 0, 1\n"
                 "slli    %[v], %[v], 15\n"
                 "add     %[src], %[tile], %[v]\n"
                 "movi    %[blocks], 8\n"
                 "loop    %[blocks], 1f\n"
                 "ee.vld.128.ip q0, %[src], 16\n"
                 "ee.vld.128.ip q1, %[src], 16\n"
                 "ee.vadds.s16 q0, q0, q7\n"
                 "ee.vadds.s16 q1, q1, q7\n"
                 "ee.vst.128.ip q0, %[out], 16\n"
                 "ee.vst.128.ip q1, %[out], 16\n"
                 "1:\n"
                 "addi    %[cells], %[cells], -1\n"
                 "bnez    %[cells], 2b\n"
                 "3:\n"
                 : [out] "+&r"(out), [hash] "+&r"(hash), [cells] "+&r"(cells),
                   [src] "=&r"(src), [v] "=&r"(v), [blocks] "=&r"(blocks)
                 : [tile] "r"(tileRow), [orient] "r"(orientation), [off] "r"(offset)
                 : "memory");
}

GM_ANIM_IRAM __attribute__((noinline)) void truchetGatherAsm(uint16_t *out, const uint16_t *src,
                                                           const uint16_t *pal, int pairs) {
    uint32_t p0, p1;
    asm volatile("loopnez %[pairs], 1f\n"
                 "l16ui   %[p0], %[src], 0\n"
                 "l16ui   %[p1], %[src], 2\n"
                 "extui   %[p0], %[p0], 4, 8\n"
                 "extui   %[p1], %[p1], 4, 8\n"
                 "addx2   %[p0], %[p0], %[pal]\n"
                 "addx2   %[p1], %[p1], %[pal]\n"
                 "l16ui   %[p0], %[p0], 0\n"
                 "l16ui   %[p1], %[p1], 0\n"
                 "addi    %[src], %[src], 4\n"
                 "slli    %[p1], %[p1], 16\n"
                 "or      %[p0], %[p0], %[p1]\n"
                 "s32i    %[p0], %[out], 0\n"
                 "addi    %[out], %[out], 4\n"
                 "1:\n"
                 : [out] "+&r"(out), [src] "+&r"(src), [p0] "=&r"(p0), [p1] "=&r"(p1)
                 : [pal] "r"(pal), [pairs] "r"(pairs)
                 : "memory");
}

GM_ANIM_IRAM __attribute__((noinline)) void truchetLerpAsm(uint16_t *a, const uint16_t *b, const int16_t *dither,
                                                         uint32_t fx2, uint32_t fy2, int n8) {
    const uint16_t *aNext = a + 8;
    uint32_t sarByte = 2;
    asm volatile("ee.vld.128.ip q7, %[dither], 0\n"
                 "ee.movi.32.q q5, %[fx2], 0\n"
                 "ee.movi.32.q q5, %[fx2], 1\n"
                 "ee.movi.32.q q5, %[fx2], 2\n"
                 "ee.movi.32.q q5, %[fx2], 3\n"
                 "ee.movi.32.q q6, %[fy2], 0\n"
                 "ee.movi.32.q q6, %[fy2], 1\n"
                 "ee.movi.32.q q6, %[fy2], 2\n"
                 "ee.movi.32.q q6, %[fy2], 3\n"
                 "ssai 4\n"
                 "wur.sar_byte %[sb]\n"
                 "ee.vld.128.ip q0, %[a], 0\n" // block 0 of the row above; a is the store pointer
                 "ee.vld.128.ip q1, %[b], 16\n"
                 "ee.vsubs.s16 q1, q1, q0\n"
                 "ee.vmul.s16 q1, q1, q6\n"
                 "ee.vadds.s16 q2, q0, q1\n" // ly of block 0
                 "loopnez %[n], 1f\n"
                 "ee.vld.128.ip q0, %[an], 16\n" // block k + 1 of both rows
                 "ee.vld.128.ip q1, %[b], 16\n"
                 "ee.vsubs.s16 q1, q1, q0\n"
                 "ee.vmul.s16 q1, q1, q6\n"
                 "ee.vadds.s16 q3, q0, q1\n" // ly of block k + 1
                 "ee.src.q q4, q2, q3\n"     // ly entries 8k + 1 .. 8k + 8
                 "ee.vsubs.s16 q4, q4, q2\n"
                 "ee.vmul.s16 q4, q4, q5\n"
                 "ee.vadds.s16 q4, q4, q2\n"
                 "ee.vadds.s16 q4, q4, q7\n"
                 "ee.vst.128.ip q4, %[a], 16\n" // block k, in place
                 "ee.orq q2, q3, q3\n"
                 "1:\n"
                 : [a] "+&r"(a), [an] "+&r"(aNext), [b] "+&r"(b)
                 : [dither] "r"(dither), [fx2] "r"(fx2), [fy2] "r"(fy2), [sb] "r"(sarByte), [n] "r"(n8)
                 : "memory");
}

/* GCC 14.2's original 16-instruction bandRef loop, transcribed before
 * comparing the PIE copy plus paired gather to it. Same instruction order
 * as xtensa-asm14/AnimTruchet.S's first portable .L7 loop. Kept only in
 * this harness so both the baseline transcription and the compiler edge
 * have execution evidence. The C reference below is the independent oracle.
 */
static __attribute__((noinline)) void gccRowAsm(uint16_t *out, const uint16_t *src,
                                               const int16_t *off, const uint16_t *pal, int n) {
    uint32_t x = 0, v, t;
    const uint32_t cap = 255;
    asm volatile("loopnez %[n], 1f\n"
                 "extui   %[v], %[x], 0, 3\n"
                 "addx2   %[v], %[v], %[off]\n"
                 "l16ui   %[t], %[src], 0\n"
                 "l16si   %[v], %[v], 0\n"
                 "addi    %[x], %[x], 1\n"
                 "add     %[v], %[v], %[t]\n"
                 "srai    %[v], %[v], 4\n"
                 "min     %[v], %[v], %[cap]\n"
                 "movi    %[t], 0\n"
                 "max     %[v], %[v], %[t]\n"
                 "extui   %[v], %[v], 0, 16\n"
                 "addx2   %[v], %[v], %[pal]\n"
                 "l16ui   %[v], %[v], 0\n"
                 "addi    %[src], %[src], 2\n"
                 "s16i    %[v], %[out], 0\n"
                 "addi    %[out], %[out], 2\n"
                 "1:\n"
                 : [out] "+&r"(out), [src] "+&r"(src), [x] "+&r"(x), [v] "=&r"(v), [t] "=&r"(t)
                 : [off] "r"(off), [pal] "r"(pal), [cap] "r"(cap), [n] "r"(n)
                 : "memory");
}

static uint16_t tile[2 * TILE * TILE] __attribute__((aligned(16)));
static uint16_t scanGot[SCAN + 64] __attribute__((aligned(16)));
static uint16_t raw[SCAN] __attribute__((aligned(16)));
static uint16_t got[512] __attribute__((aligned(16)));
static uint16_t baseline[512] __attribute__((aligned(16)));
static uint16_t want[512] __attribute__((aligned(16)));
static uint16_t palette[256] __attribute__((aligned(16)));
static uint8_t orientation[256] __attribute__((aligned(16)));
static int16_t offset[8] __attribute__((aligned(16)));
static int16_t rotated[8] __attribute__((aligned(16)));
static uint32_t calls, pixels, scanned, blended;
static uint16_t lerpA[SCAN + 64] __attribute__((aligned(16)));
static uint16_t lerpB[SCAN + 64] __attribute__((aligned(16)));
static uint16_t lerpSrc[SCAN + 64];
static int16_t lerpLy[SCAN + 64];

static int fail(const char *phase, uint32_t lane, uint32_t actual, uint32_t expected) {
    puts_uart("GM_QEMUBENCH_PIE: FAIL truchet "); puts_uart(phase);
    puts_uart(" call="); dec_uart(calls);
    puts_uart(" lane="); dec_uart(lane);
    puts_uart(" got="); dec_uart(actual);
    puts_uart(" want="); dec_uart(expected);
    puts_uart("\nGM_QEMUBENCH_PIE_DONE\n");
    return 0;
}

/* The page's bandRef loop, in plain C. Its input is the undithered scan.
 * This checks the transformation across the two kernels, not just each
 * stage against a C version of that same stage. */
static void bandRefInner(uint16_t *out, const uint16_t *src, const int16_t *off,
                          const uint16_t *pal, int width) {
    for (int x = 0; x < width; x++) {
        int idx = (src[x] + off[x & 7]) >> 4;
        if (idx < 0) idx = 0;
        else if (idx > 255) idx = 255;
        out[x] = pal[idx];
    }
}

static int rowCase(int ty, uint32_t hash, int cells, int subX, int width, int placement, int checkBaseline) {
    /* Different legal 16-byte scan starts and all four legal output starts
     * modulo 16. Prefix/suffix canaries detect a masked or overlong store. */
    const int scanStart = 8 + (placement & 3) * 8;
    const int outStart = 8 + (placement & 3) * 2;
    uint16_t *s = scanGot + scanStart;
    for (int i = 0; i < SCAN + 64; i++) scanGot[i] = GUARD;
    for (int i = 0; i < 512; i++) got[i] = want[i] = baseline[i] = GUARD;
    for (int i = 0; i < 8; i++) rotated[i] = offset[(i - subX) & 7];
    for (int c = 0; c < cells; c++) {
        int which = orientation[(hash + c * 7u) & 255u] & 1;
        for (int x = 0; x < TILE; x++) raw[c * TILE + x] = tile[which * TILE * TILE + ty * TILE + x];
    }
    truchetScanAsm(s, tile + ty * TILE, orientation, rotated, hash, cells);
    calls++;
    for (int i = 0; i < SCAN + 64; i++) {
        int x = i - scanStart;
        uint16_t expected = GUARD;
        if (x >= 0 && x < cells * TILE) expected = raw[x] + offset[(x - subX) & 7];
        if (scanGot[i] != expected) return fail("scan/guards", i, scanGot[i], expected);
    }
    scanned += cells * TILE;
    truchetGatherAsm(got + outStart, s + subX, palette, width / 2);
    /* Same odd-width tail as production band(), exercised at 1 and 233. */
    if (width & 1) got[outStart + width - 1] = palette[s[subX + width - 1] >> 4];
    bandRefInner(want + outStart, raw + subX, offset, palette, width);
    if (checkBaseline) gccRowAsm(baseline + outStart, raw + subX, offset, palette, width);
    for (int i = 0; i < 512; i++) {
        if (got[i] != want[i]) return fail("gather/guards", i, got[i], want[i]);
        if (checkBaseline && baseline[i] != want[i]) return fail("gcc transcription", i, baseline[i], want[i]);
    }
    pixels += width;
    return 1;
}

/* bandRef's blend, in plain C: the y blend of both rows, then the x blend
 * between each entry and the next, then the dither. Arithmetic shifts. */
static int lerpQ4(int a, int b, int f) { return a + (((b - a) * f) >> 4); }

/* One in-place blend over n8 blocks at a legal aligned start, with guards
 * before the start, after the last output block and across the slack the
 * kernel reads but must not write. Only entries 0..8*n8-1 are outputs;
 * their reference reads a and b up to entry 8*n8 (the slack the production
 * scan carries). */
static int lerpCase(int n8, int fx, int fy, int start, uint32_t seed) {
    const int n = 8 * n8;
    for (int i = 0; i < SCAN + 64; i++) { lerpA[i] = GUARD; lerpB[i] = GUARD; }
    for (int i = 0; i <= n + 7 && start + i < SCAN + 64; i++) {
        seed = seed * 1664525u + 1013904223u;
        /* 416..2880 with both ends reachable, plus a run at the extremes. */
        uint16_t va = (uint16_t)(416 + (seed >> 8) % 2465);
        uint16_t vb = (uint16_t)(416 + (seed >> 20) % 2465);
        if ((i & 15) == 3) va = 416;
        if ((i & 15) == 4) vb = 2880;
        if ((i & 15) == 9) va = 2880;
        if ((i & 15) == 10) vb = 416;
        lerpA[start + i] = va;
        lerpB[start + i] = vb;
        lerpSrc[i] = va;
    }
    for (int i = 0; i <= n; i++) lerpLy[i] = (int16_t)lerpQ4(lerpSrc[i], lerpB[start + i], fy);
    truchetLerpAsm(lerpA + start, lerpB + start, rotated, (uint32_t)fx | ((uint32_t)fx << 16),
                   (uint32_t)fy | ((uint32_t)fy << 16), n8);
    calls++;
    for (int i = 0; i < SCAN + 64; i++) {
        int j = i - start;
        uint16_t expected;
        if (j >= 0 && j < n) expected = (uint16_t)(lerpLy[j] + (((lerpLy[j + 1] - lerpLy[j]) * fx) >> 4) + rotated[j & 7]);
        else if (j >= n && j <= n + 7) expected = lerpSrc[j]; /* slack: read, never written */
        else expected = GUARD;
        if (lerpA[i] != expected) return fail("lerp/guards", i, lerpA[i], expected);
    }
    blended += n;
    return 1;
}

static int testLerp(void) {
    /* Every fraction pair at the production trip count, with the dither row
     * spanning the -410..410 range the kernel proof allows, then the short
     * trip counts, both legal 16-byte starts and a zero trip. */
    for (int fy = 0; fy < 16; fy++) {
        for (int fx = 0; fx < 16; fx++) {
            for (int x = 0; x < 8; x++) rotated[x] = (int16_t)(-410 + (x * 117 + fx * 7 + fy * 13) % 821);
            if (!lerpCase(SCAN / 8, fx, fy, 8, 0x9e3779b9u + fx * 16 + fy)) return 0;
        }
    }
    static const int trips[6] = {0, 1, 2, 3, 17, 79};
    for (int t = 0; t < 6; t++) {
        for (int st = 0; st < 2; st++) {
            for (int x = 0; x < 8; x++) rotated[x] = (int16_t)((x & 1) ? 410 : -410);
            if (!lerpCase(trips[t], 15, 1, 8 + st * 8, 0x1234567u + t)) return 0;
            if (!lerpCase(trips[t], 0, 15, 8 + st * 8, 0x7654321u + t)) return 0;
        }
    }
    return 1;
}

static int testAll(void) {
    for (int i = 0; i < 256; i++) {
        orientation[i] = (uint8_t)(i * 73 + 19); /* all byte values, not just 0/1 */
        palette[i] = (uint16_t)(i * 251 + 0x8123); /* distinct arbitrary RGB565 words */
    }
    /* Full cross product: every tile Q4 value 416..2880 and every signed
     * dither offset -256..256. Includes the full arc/glow 0/100 envelope
     * and every fractional nibble, at every lane position over the sweep. */
    orientation[0] = 0;
    for (int base = 416; base <= 2880; base += 128) {
        for (int x = 0; x < TILE; x++) tile[x] = 416 + (base - 416 + x) % 2465;
        for (int d = -256; d <= 256; d++) {
            for (int x = 0; x < 8; x++) offset[x] = d;
            if (!rowCase(0, 0, 1, 0, 128, d & 3, 0)) return 0;
        }
    }
    /* All rows of both orientation tables, every orientation byte and every
     * scroll remainder 0..127 at all eight screen Bayer phases. Hash high
     * bits include uint32 wrap; only its low byte may affect the orientation.
     * Width selection covers no trip, one pair, tails and both panel sizes. */
    static const int widths[16] = {0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 127, 128, 233, 240, 466, 480};
    static const int bayer[64] = {
        0,32,8,40,2,34,10,42,48,16,56,24,50,18,58,26,
        12,44,4,36,14,46,6,38,60,28,52,20,62,30,54,22,
        3,35,11,43,1,33,9,41,51,19,59,27,49,17,57,25,
        15,47,7,39,13,45,5,37,63,31,55,23,61,29,53,21
    };
    for (int i = 0; i < 2 * TILE * TILE; i++) tile[i] = 416 + (i * 151 + i / TILE * 71) % 2465;
    for (int i = 0; i < 256; i++) orientation[i] = (uint8_t)(i * 73 + 19);
    for (int sub = 0; sub < 128; sub++) {
        for (int phase = 0; phase < 8; phase++) {
            for (int x = 0; x < 8; x++) offset[x] = (bayer[phase * 8 + x] * 512 + 31) / 63 - 256;
            uint32_t hash = 0xffffff00u + ((sub * 17 + phase * 31) & 255);
            if (!rowCase(sub, hash, 5, sub, widths[(sub + phase) & 15], phase, 1)) return 0;
        }
    }
    /* Explicitly traverse every cell count, including zero, and long pairs
     * beginning at the last scroll remainder. No read or write on a zero
     * gather trip. Scan's constant vector load remains valid at cells=0. */
    for (int cells = 0; cells <= 5; cells++) {
        int width = cells ? cells * TILE - 127 : 0;
        if (width > 480) width = 480;
        if (!rowCase(127, 0xffffffffu, cells, cells ? 127 : 0, width, cells, 1)) return 0;
    }
    /* Decoder superset: every possible Q4 palette input 0..4095 against
     * all 65,536 RGB565 output words. Distinct words expose lane swaps and
     * prove unsigned l16ui loads and s32i packing retain bit 15 correctly. */
    for (int bank = 0; bank < 256; bank++) {
        for (int i = 0; i < 256; i++) palette[i] = (uint16_t)((bank << 8) | i);
        for (int base = 0; base < 4096; base += 256) {
            for (int x = 0; x < 256; x++) raw[x] = base + x;
            for (int i = 0; i < 512; i++) got[i] = GUARD;
            truchetGatherAsm(got + 8, raw, palette, 128);
            calls++;
            for (int i = 0; i < 512; i++) {
                uint16_t expected = i >= 8 && i < 264 ? palette[raw[i - 8] >> 4] : GUARD;
                if (got[i] != expected) return fail("full palette", i, got[i], expected);
            }
            pixels += 256;
        }
    }
    return 1;
}

int main(void) {
    /* The firmware kernel must never do this. With no FreeRTOS in the
     * harness there is no lazy coprocessor exception owner, so main enables
     * only PIE/CP3 once, before any kernel executes. */
    uint32_t cp = 8;
    asm volatile("wsr %0, cpenable\nrsync\n" :: "r"(cp) : "memory");
    if (testAll() && testLerp()) {
        puts_uart("GM_QEMUBENCH_PIE: PASS truchet bit-exact calls="); dec_uart(calls);
        puts_uart(" pixels="); dec_uart(pixels);
        puts_uart(" scan_values="); dec_uart(scanned);
        puts_uart(" blended="); dec_uart(blended);
        puts_uart(" mismatches=0 (Q4/dither exhaustive, all RGB565, offsets, guards, GCC baseline, lerp fractions)\n");
        puts_uart("GM_QEMUBENCH_PIE_DONE\n");
    }
    for (;;) {}
}
