/* Freestanding harness for Gyroid's production kernel. The function between
 * the COPY markers is a byte-for-byte copy of gyroidRowAsm in
 * src/display/ui/default/bganim/AnimGyroid.cpp. gyroidRowRef below is written
 * from the page entry's pixel equation, never from the lane arithmetic, so a
 * shared mistake in the vector schedule cannot pass.
 *
 * Operand domain, which is exactly what frame() can produce:
 *   col low half   0..256      128 + (sin1024/4), sin amplitude 512
 *   col high half  0..32768    16384 + (sin*sin)/16, 32768 sets the sign bit
 *   r              -128..128   sin1024/4
 *   d              -16384..16384
 *   wid            42..86      42 + round(passage width * 0.44)
 *   dr entry       0..92       ground level 4..88 plus Bayer offset -4..4
 * The field f then spans -49152..49152 and the palette index 0..178. The
 * dither lane carries two sliders since gm-3vj.41, Ground level and Grain,
 * which is why it reaches both palette ends; the kernel clamps neither, so
 * the sweep below has to visit 0 and 92.
 *
 * Sweeps: every (low, r) pair, every high, every d, every wid, all eight
 * Bayer rows, the corner cross product, every production and boundary
 * length including the sub-quad and tail cases, three 16-byte column
 * offsets, and four-byte-aligned destination offsets. dst alignment is
 * limited to four bytes on purpose: BgAnim.h guarantees it and guarantees an
 * even width whenever rows > 1, which is what the paired s32i stores need.
 * Guard words on both sides of every destination catch an over-run store.
 */
#include <stdint.h>
#define GM_ANIM_IRAM
#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)

static void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') UART0_FIFO = '\r';
        UART0_FIFO = (uint8_t)*s++;
    }
}
static void uart_dec(int n) {
    char buf[12];
    unsigned v = (unsigned)n;
    if (n < 0) { UART0_FIFO = '-'; v = 0u - v; }
    int count = 0;
    do { buf[count++] = '0' + v % 10; v /= 10; } while (v);
    while (count) UART0_FIFO = buf[--count];
}

/* BEGIN VERBATIM PRODUCTION KERNEL */
GM_ANIM_IRAM __attribute__((noinline)) void gyroidRowAsm(
    uint16_t *out, const uint32_t *col, const uint16_t *pal,
    const uint32_t *bias, const int32_t *dr, int r, int d, int wid, int w) {
    const uint32_t rword = (uint16_t)r; // signed low half, upper half zero
    const int delta = d - 16384;
    const uint32_t *mask = bias + 4;
    asm volatile("ee.movi.32.q q4, %[r], 0\n"
                 "ee.movi.32.q q4, %[r], 1\n"
                 "ee.movi.32.q q4, %[r], 2\n"
                 "ee.movi.32.q q4, %[r], 3\n"
                 "ee.movi.32.q q5, %[d], 0\n"
                 "ee.movi.32.q q5, %[d], 1\n"
                 "ee.movi.32.q q5, %[d], 2\n"
                 "ee.movi.32.q q5, %[d], 3\n"
                 "ee.movi.32.q q6, %[wid], 0\n"
                 "ee.movi.32.q q6, %[wid], 1\n"
                 "ee.movi.32.q q6, %[wid], 2\n"
                 "ee.movi.32.q q6, %[wid], 3\n"
                 "ee.vld.128.ip q3, %[mask], 0\n"
                 "ee.zero.q q7\n"
                 : [mask] "+&r"(mask)
                 : [r] "r"(rword), [d] "r"(delta), [wid] "r"(wid)
                 : "memory");
    int phase = 0;
    const int toggle = 16; // four int32 dither lanes, half the 8-column period
    const int nQuads = w >> 2;
    const int32_t *dp;
    uint32_t t0, t1, t2, t3;
    asm volatile("loopnez %[n], 1f\n"
                 "ee.vld.128.ip q0, %[col], 16\n"
                 "ee.vld.128.ip q2, %[bias], 0\n"
                 "ee.andq q1, q0, q3\n"
                 "ee.vsubs.s16 q1, q1, q2\n"
                 "ssai 0\n"
                 "ee.vmul.s16 q1, q1, q4\n"
                 "ssai 16\n"
                 "ee.vsr.32 q2, q0\n"
                 "ee.vsl.32 q1, q1\n"
                 "ee.andq q2, q2, q3\n"
                 "ee.vsr.32 q1, q1\n"
                 "ee.vadds.s32 q2, q2, q5\n"
                 "ee.vadds.s32 q1, q1, q2\n"
                 "ee.vsubs.s32 q2, q7, q1\n"
                 "ee.vmax.s32 q1, q1, q2\n"
                 "ssai 8\n"
                 "ee.vsr.32 q1, q1\n"
                 "ee.vsubs.s32 q1, q6, q1\n"
                 "add %[dp], %[dr], %[phase]\n"
                 "ee.vld.128.ip q0, %[dp], 0\n"
                 "ee.vmax.s32 q1, q1, q7\n"
                 "ee.vadds.s32 q1, q1, q0\n"
                 "ee.movi.32.a q1, %[t0], 0\n"
                 "ee.movi.32.a q1, %[t1], 1\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "ee.movi.32.a q1, %[t2], 2\n"
                 "slli %[t1], %[t1], 16\n"
                 "ee.movi.32.a q1, %[t3], 3\n"
                 "or %[t0], %[t0], %[t1]\n"
                 "s32i %[t0], %[out], 0\n"
                 "addx2 %[t2], %[t2], %[pal]\n"
                 "addx2 %[t3], %[t3], %[pal]\n"
                 "l16ui %[t2], %[t2], 0\n"
                 "l16ui %[t3], %[t3], 0\n"
                 "xor %[phase], %[phase], %[toggle]\n"
                 "slli %[t3], %[t3], 16\n"
                 "or %[t2], %[t2], %[t3]\n"
                 "s32i %[t2], %[out], 4\n"
                 "addi %[out], %[out], 8\n"
                 "1:\n"
                 : [out] "+&r"(out), [col] "+&r"(col), [phase] "+&r"(phase),
                   [dp] "=&r"(dp), [t0] "=&r"(t0), [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3),
                   [bias] "+&r"(bias)
                 : [dr] "r"(dr), [pal] "r"(pal), [toggle] "r"(toggle), [n] "r"(nQuads)
                 : "memory");
    for (int x = nQuads * 4; x < w; ++x) {
        const uint32_t pk = *col++;
        int f = ((int)(pk & 65535u) - 128) * r + (int)(pk >> 16) - 16384 + d;
        if (f < 0) f = -f;
        const int g = wid - (f >> 8);
        *out++ = pal[(g > 0 ? g : 0) + dr[x & 7]];
    }
}
/* END VERBATIM PRODUCTION KERNEL */

/* The page's pixel equation, written out. dr already carries the ground
 * level, so this adds the clamped ramp to it exactly as bandRef's floor plus
 * max(g,0) plus the Bayer offset does. */
static void gyroidRowRef(uint16_t *out, const uint32_t *col, const uint16_t *pal,
                         const int32_t *dr, int r, int d, int wid, int w) {
    for (int x = 0; x < w; x++) {
        const uint32_t pk = col[x];
        const int low = (int)(pk & 0xffffu);
        const int high = (int)(pk >> 16);
        int f = (low - 128) * r + high - 16384 + d;
        if (f < 0) f = -f;
        int g = wid - (f >> 8);
        if (g < 0) g = 0;
        out[x] = pal[g + dr[x & 7]];
    }
}

#define STORAGE 544
#define GUARD 8
static uint32_t columns[STORAGE] __attribute__((aligned(16)));
static int32_t dither[64] __attribute__((aligned(16)));
static uint32_t bias[8] __attribute__((aligned(16)));
static uint16_t palette[256] __attribute__((aligned(16)));
static uint16_t got[STORAGE] __attribute__((aligned(16)));
static uint16_t want[STORAGE] __attribute__((aligned(16)));

static int calls, pixels, mismatches;
static int firstCall, firstLane, firstGot, firstWant;

/* colOff counts 16-byte column groups; dstOff counts 4-byte pixel pairs, the
 * finest destination alignment BgAnim.h's contract allows. */
static void check(int colOff, int dstOff, int n, int r, int d, int wid, int row) {
    const int cs = colOff * 4;
    const int ds = GUARD + dstOff * 2;
    const int checked = ds + n + GUARD;
    calls++;
    pixels += n;
    for (int i = 0; i < checked; i++) got[i] = want[i] = 0xdead;
    gyroidRowAsm(got + ds, columns + cs, palette, bias, dither + row * 8, r, d, wid, n);
    gyroidRowRef(want + ds, columns + cs, palette, dither + row * 8, r, d, wid, n);
    for (int i = 0; i < checked; i++) {
        if (got[i] != want[i]) {
            if (mismatches == 0) {
                firstCall = calls;
                firstLane = i - ds;
                firstGot = got[i];
                firstWant = want[i];
            }
            mismatches++;
        }
    }
}

static void setCol(int i, int low, int high) {
    columns[i] = ((uint32_t)high << 16) | (uint32_t)low;
}

int main(void) {
    /* Bare-metal harness only. Production leaves CPENABLE to FreeRTOS, which
     * enables CP3 lazily on the first PIE instruction of the render task. */
    uint32_t cp = 8;
    asm volatile("wsr %0, cpenable\nisync\n" : : "r"(cp) : "memory");

    for (int i = 0; i < 4; i++) {
        bias[i] = 128;
        bias[4 + i] = 65535;
    }
    /* Injective over 0..255 with different high and low bytes, and both
     * RGB565 word extremes present, so a wrong gather cannot alias. */
    for (int i = 0; i < 256; i++) palette[i] = (uint16_t)(i * 251);
    palette[0] = 0x0000;
    palette[255] = 0xffff;
    /* Ground level 4..88 in even steps, one per Bayer row, plus the widest
     * Bayer offsets -4..4 across the lanes: entry 0 in row 0 and entry 92 in
     * row 7, so both palette ends of the index are exercised. */
    static const int bayerOff[8] = {-4, -3, -1, 0, 1, 3, 4, 2};
    for (int row = 0; row < 8; row++)
        for (int lane = 0; lane < 8; lane++)
            dither[row * 8 + lane] = 4 + row * 12 + bayerOff[lane];

    uart_puts("gyroid: lengths, column offsets, destination offsets, Bayer rows\n");
    static const int lengths[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 15, 16,
                                  17, 23, 31, 32, 33, 63, 64, 65, 233, 240, 466, 480};
    static const int rs[] = {-128, -37, 0, 61, 128};
    static const int ds[] = {-16384, -1, 0, 1, 16384};
    static const int wids[] = {42, 66, 86};
    for (int i = 0; i < STORAGE; i++)
        setCol(i, (i * 37) % 257, ((i * 691) % 257) * 128);
    for (int a = 0; a < 5; a++)
        for (int b = 0; b < 5; b++)
            for (int c = 0; c < 3; c++)
                for (int colOff = 0; colOff < 3; colOff++)
                    for (int dstOff = 0; dstOff < 4; dstOff++)
                        for (unsigned j = 0; j < sizeof(lengths) / sizeof(lengths[0]); j++)
                            check(colOff, dstOff, lengths[j], rs[a], ds[b], wids[c],
                                  (a * 5 + b + dstOff) & 7);

    uart_puts("gyroid: every low half against every multiplier\n");
    /* 257 x 257: the whole domain of the 16-bit lane multiply, at the high
     * half and field offset that put the product's own sign into f. */
    for (int i = 0; i <= 256; i++) setCol(i, i, 16384);
    for (int r = -128; r <= 128; r++) check(0, 0, 257, r, 0, 66, r & 7);

    uart_puts("gyroid: every high half\n");
    /* 0..32768 in full, including 32768 where the halfword sign bit is set
     * and only the mask keeps the arithmetic shift from carrying it down. */
    for (int base = 0; base <= 32768; base += 480) {
        int n = 32769 - base;
        if (n > 480) n = 480;
        for (int i = 0; i < n; i++) setCol(i, 128, base + i);
        check(0, 0, n, 0, 0, 66, base & 7);
    }
    /* The same high sweep with the multiply and the row term at their
     * extremes, so f reaches -49152 and 49152. */
    for (int i = 0; i < 480; i++) setCol(i, 256, (i * 32768) / 479);
    for (int r = -128; r <= 128; r += 8) {
        check(0, 0, 480, r, -16384, 42, 3);
        check(0, 0, 480, r, 16384, 86, 6);
    }

    uart_puts("gyroid: every row term\n");
    for (int i = 0; i < 8; i++) setCol(i, i * 32, i * 4096);
    for (int d = -16384; d <= 16384; d++) check(0, 0, 8, 96, d, 66, d & 7);

    uart_puts("gyroid: corner cross product and every width\n");
    static const int lows[] = {0, 1, 127, 128, 129, 255, 256};
    static const int highs[] = {0, 1, 16383, 16384, 16385, 32767, 32768};
    for (unsigned a = 0; a < sizeof(lows) / sizeof(lows[0]); a++) {
        for (unsigned b = 0; b < sizeof(highs) / sizeof(highs[0]); b++) {
            for (int i = 0; i < 16; i++) setCol(i, lows[a], highs[b]);
            for (int r = -128; r <= 128; r += 64)
                for (int d = -16384; d <= 16384; d += 8192)
                    for (int wid = 42; wid <= 86; wid++)
                        check(0, 0, 16, r, d, wid, (a + b) & 7);
        }
    }

    uart_puts("gyroid: mixed production rows\n");
    /* Pseudo-random rows inside the production domain, at every Bayer row
     * and both parities of quad count. */
    uint32_t seed = 0x1234567u;
    for (int trial = 0; trial < 240; trial++) {
        for (int i = 0; i < STORAGE; i++) {
            seed = seed * 1664525u + 1013904223u;
            const int low = (int)((seed >> 8) % 257u);
            seed = seed * 1664525u + 1013904223u;
            const int high = (int)((seed >> 8) % 32769u);
            setCol(i, low, high);
        }
        seed = seed * 1664525u + 1013904223u;
        const int r = (int)((seed >> 8) % 257u) - 128;
        seed = seed * 1664525u + 1013904223u;
        const int d = (int)((seed >> 8) % 32769u) - 16384;
        seed = seed * 1664525u + 1013904223u;
        const int wid = 42 + (int)((seed >> 8) % 45u);
        check(trial % 3, trial % 4, (trial & 1) ? 480 : 466, r, d, wid, trial & 7);
    }

    if (mismatches) {
        uart_puts("GM_QEMUBENCH_PIE: FAIL gyroid call=");
        uart_dec(firstCall);
        uart_puts(" lane=");
        uart_dec(firstLane);
        uart_puts(" got=");
        uart_dec(firstGot);
        uart_puts(" want=");
        uart_dec(firstWant);
        uart_puts(" mismatches=");
        uart_dec(mismatches);
    } else {
        uart_puts("GM_QEMUBENCH_PIE: PASS gyroid calls=");
        uart_dec(calls);
        uart_puts(" pixels=");
        uart_dec(pixels);
        uart_puts(" mismatches=0 guards=0");
    }
    uart_puts("\nGM_QEMUBENCH_PIE_DONE\n");
    for (;;) {
    }
}
