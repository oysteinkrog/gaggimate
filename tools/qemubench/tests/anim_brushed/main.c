/* Freestanding harness-mode execution test for AnimBrushed.cpp.
 * The production kernel below is verbatim, including attributes, constraints,
 * C tail and comments. The separate C reference implements bandRef's scalar
 * equation without pixel packing or vector arithmetic.
 *
 * Exhaustive coverage: every valid (column,row) combination with columns
 * 0..4095 and row terms -438..438, all 4096 field sums and all 65536 RGB565
 * palette words. Also: lengths 0..480, every tail, output alignment 0/4/8/12
 * mod 16, a palette aligned to only 2 bytes, signed lane boundaries beyond
 * the production range, canaries, and absolute-row band shapes at widths
 * 233/240/466/480. Speed, grain, reflection and contrast 0 and 100 only
 * change the table inputs; their full proven ranges are swept here.
 *
 * Only main enables CP3, once, in the bare-metal harness. Production leaves
 * CPENABLE to FreeRTOS. No libc, malloc, floats or global constructors.
 */
#include <stdint.h>
#define GM_ANIM_IRAM
#define asm __asm__
#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)
static void puts_uart(const char *s) {
    while (*s) {
        if (*s == '\n') UART0_FIFO = '\r';
        UART0_FIFO = (uint8_t)*s++;
    }
}
static void dec_uart(uint32_t v) {
    char digits[11];
    int n = 0;
    do { digits[n++] = '0' + v % 10; v /= 10; } while (v);
    while (n) UART0_FIFO = digits[--n];
}
/* BEGIN VERBATIM PRODUCTION KERNEL */
GM_ANIM_IRAM __attribute__((noinline)) void brushedRowAsm(uint16_t *out, const int16_t *ct,
                                                         const uint16_t *pal, int rt, int n) {
    const int blocks = n >> 3;
    int32_t lo, hi;
    if (blocks != 0) {
        uint32_t rowPair = (uint16_t)rt;
        rowPair |= rowPair << 16;
        asm volatile("ee.movi.32.q q1, %[rt], 0\n"
                     "ee.movi.32.q q1, %[rt], 1\n"
                     "ee.movi.32.q q1, %[rt], 2\n"
                     "ee.movi.32.q q1, %[rt], 3\n"
                     "loopnez %[n], 1f\n"
                     "ee.vld.128.ip q0, %[ct], 0\n"
                     "addi    %[ct], %[ct], 16\n"
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
                     "addi    %[out], %[out], 16\n"
                     "1:\n"
                     : [out] "+&r"(out), [ct] "+&r"(ct), [lo] "=&r"(lo), [hi] "=&r"(hi)
                     : [rt] "r"(rowPair), [pal] "r"(pal), [n] "r"(blocks)
                     : "memory");
    }
    const int pairs = (n & 7) >> 1;
    asm volatile("loopnez %[n], 2f\n"
                 "l16si   %[hi], %[ct], 2\n"
                 "l16si   %[lo], %[ct], 0\n"
                 "add     %[hi], %[hi], %[rt]\n"
                 "srai    %[hi], %[hi], 4\n"
                 "add     %[lo], %[lo], %[rt]\n"
                 "addx2   %[hi], %[hi], %[pal]\n"
                 "srai    %[lo], %[lo], 4\n"
                 "l16ui   %[hi], %[hi], 0\n"
                 "addx2   %[lo], %[lo], %[pal]\n"
                 "l16ui   %[lo], %[lo], 0\n"
                 "slli    %[hi], %[hi], 16\n"
                 "or      %[hi], %[hi], %[lo]\n"
                 "s32i    %[hi], %[out], 0\n"
                 "addi    %[ct], %[ct], 4\n"
                 "addi    %[out], %[out], 4\n"
                 "2:\n"
                 : [out] "+&r"(out), [ct] "+&r"(ct), [lo] "=&r"(lo), [hi] "=&r"(hi)
                 : [rt] "r"(rt), [pal] "r"(pal), [n] "r"(pairs)
                 : "memory");
    if (n & 1) {
        *out = pal[(*ct + rt) >> 4];
    }
}
/* END VERBATIM PRODUCTION KERNEL */

static void brushedRowRef(uint16_t *out, const int16_t *ct,
                          const uint16_t *pal, int rt, int n) {
    for (int x = 0; x < n; x++) out[x] = pal[((int)ct[x] + rt) >> 4];
}

#define STORAGE 5008
static int16_t columns[480] __attribute__((aligned(16)));
static int16_t phases[8 * 480] __attribute__((aligned(16)));
static int16_t rows[480];
static uint16_t palStore[257] __attribute__((aligned(16)));
/* Palette deliberately only 2-byte aligned: only columns use PIE loads. */
static uint16_t *pal = palStore + 1;
static uint16_t got[STORAGE] __attribute__((aligned(16)));
static uint16_t want[STORAGE] __attribute__((aligned(16)));
static uint32_t calls, pixels, mismatches, firstCall, firstLane, firstGot, firstWant;

static void prepare(int off, int n) {
    /* Guard every store span, including n==0. Avoid an O(STORAGE) sweep for
     * each short case while still checking both neighboring vector spans. */
    for (int i = off - 8; i < off + n + 8; i++) got[i] = want[i] = 0xa55a;
}
static void check(int off, int n) {
    for (int i = off - 8; i < off + n + 8; i++) {
        if (got[i] != want[i]) {
            if (!mismatches) {
                firstCall = calls; firstLane = i; firstGot = got[i]; firstWant = want[i];
            }
            mismatches++;
        }
    }
    calls++;
    pixels += n;
}
static void test_row(int rt, int n, int align) {
    const int off = 8 + 2 * align;
    prepare(off, n);
    brushedRowRef(want + off, columns, pal, rt, n);
    brushedRowAsm(got + off, columns, pal, rt, n);
    check(off, n);
}

int main(void) {
    const uint32_t enable = 8;
    asm volatile("wsr %0, cpenable\nisync\n" : : "r"(enable) : "memory");
    for (int i = 0; i < 256; i++) pal[i] = (uint16_t)(i * 251u + 7u);
    brushedRowAsm(0, 0, 0, 0, 0); // both LOOPNEZ zero-trip paths touch nothing

    /* Every integer column/row combination allowed by the frame's bounds.
     * Chunking also drives intermediate tail lengths and all alignments. */
    for (int rt = -438; rt <= 438; rt++) {
        const int begin = rt < 0 ? -rt : 0;
        const int end = rt > 0 ? 4095 - rt : 4095;
        for (int v = begin; v <= end;) {
            int n = end - v + 1;
            if (n > 480) n = 480;
            for (int x = 0; x < n; x++) columns[x] = (int16_t)(v + x);
            test_row(rt, n, calls & 3);
            v += n;
        }
    }
    /* All RGB565 bit patterns, each index distinguishable within its call. */
    for (int page = 0; page < 256; page++) {
        for (int i = 0; i < 256; i++) {
            pal[i] = (uint16_t)(page * 256 + i);
            columns[i] = (int16_t)(i * 16 + (i & 15));
        }
        test_row(0, 256, page & 3);
    }
    for (int i = 0; i < 256; i++) pal[i] = (uint16_t)(i * 251u + 7u);

    /* Every length, tail and legal destination alignment, at both row-term
     * extremes and zero. Sum stays in 438..3657, valid with either sign. */
    for (int align = 0; align < 4; align++) {
        for (int r = -1; r <= 1; r++) {
            const int rt = r * 438;
            for (int n = 0; n <= 480; n++) {
                for (int x = 0; x < n; x++) columns[x] = (int16_t)(438 + (x * 73 % 3220) - rt);
                test_row(rt, n, align);
            }
        }
    }
    /* Signed lane interpretation beyond the real table range. Keep the sum
     * 0..4095, where scalar and vector addition are provably equivalent. */
    static const int boundary[] = {-32767, -32766, -4095, -439, -438, -1, 0, 1, 438, 439, 28672, 32766, 32767};
    for (unsigned k = 0; k < sizeof(boundary) / sizeof(boundary[0]); k++) {
        const int rt = boundary[k];
        int cap = rt + 32767;
        if (cap > 4095) cap = 4095;
        for (int x = 0; x < 480; x++) columns[x] = (int16_t)((x * 97 % (cap + 1)) - rt);
        test_row(rt, 480, k & 3);
    }

    /* Reproduce the production row dispatch with eight padded phase tables.
     * Every check compares directly to absolute-y C rows, so parity skips
     * and odd band starts cannot inherit a preceding call's row state. */
    static const int widths[] = {233, 240, 466, 480};
    static const int heights[] = {1, 2, 3, 7, 8};
    for (int wi = 0; wi < 4; wi++) {
        const int w = widths[wi], stride = (w + 7) & ~7;
        for (int ph = 0; ph < 8; ph++) {
            for (int x = 0; x < w; x++) phases[ph * stride + x] = (int16_t)(438 + (x * 47 + ph * 131) % 3220);
        }
        for (int y = 0; y < 480; y++) rows[y] = (int16_t)(y * 53 % 877 - 438);
        for (int shape = 0; shape < 7; shape++) {
            if ((w & 1) && shape > 0 && shape < 5) continue;
            const int bandH = shape < 5 ? heights[shape] : 1;
            const int step = shape < 5 ? bandH : 2;
            for (int y0 = shape == 6 ? 1 : 0; y0 < 480; y0 += step) {
                int count = bandH;
                if (count > 480 - y0) count = 480 - y0;
                const int n = count * w, off = 8 + 2 * (calls & 3);
                prepare(off, n);
                for (int r = 0; r < count; r++) {
                    const int y = y0 + r;
                    const int16_t *ct = phases + (y & 7) * stride;
                    brushedRowRef(want + off + r * w, ct, pal, rows[y], w);
                    brushedRowAsm(got + off + r * w, ct, pal, rows[y], w);
                }
                check(off, n);
            }
        }
    }
    if (mismatches) {
        puts_uart("GM_QEMUBENCH_PIE: FAIL brushedRowAsm call="); dec_uart(firstCall);
        puts_uart(" lane="); dec_uart(firstLane);
        puts_uart(" got="); dec_uart(firstGot);
        puts_uart(" want="); dec_uart(firstWant);
        puts_uart(" mismatches="); dec_uart(mismatches); puts_uart("\n");
    } else {
        puts_uart("GM_QEMUBENCH_PIE: PASS brushedRowAsm calls="); dec_uart(calls);
        puts_uart(" pixels="); dec_uart(pixels);
        puts_uart(" mismatches=0 (exhaustive field/row ranges, RGB565, tails, alignment, band shapes)\n");
    }
    puts_uart("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
