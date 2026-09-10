/* Freestanding harness for Quilt's exact production kernels. The two
 * functions between the COPY markers are byte-for-byte copies from
 * AnimQuilt.cpp, including the alignment wrapper. C below independently
 * reimplements bandRef's pixel equation, never the PIE lane arithmetic.
 *
 * Every ct in [-985,985] is crossed with every rt in [831,2289], a
 * conservative superset of all relief/speed/pitch values 0..100 and the
 * maximum theme-derived dither. Additional sweeps take each signed input
 * through all 65536 int16 values with the other zero, so every palette
 * index and both sign bits are covered without saturating the vector add.
 * All halfword offsets modulo 16, small trip counts, tails, and production
 * widths 480/240/466/233 are checked, including output guard words.
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

/* BEGIN VERBATIM PRODUCTION KERNELS */
GM_ANIM_IRAM __attribute__((noinline)) void quiltScalarAsm(uint16_t *dst, const int16_t *ct, int rt,
                                                         const uint16_t *palette, int n) {
    int v;
    asm volatile("loopnez %[n], 1f\n"
                 "l16si   %[v], %[ct], 0\n"
                 "addi    %[ct], %[ct], 2\n"
                 "add     %[v], %[v], %[rt]\n"
                 "extui   %[v], %[v], 4, 8\n"
                 "addx2   %[v], %[v], %[pal]\n"
                 "l16ui   %[v], %[v], 0\n"
                 "s16i    %[v], %[dst], 0\n"
                 "addi    %[dst], %[dst], 2\n"
                 "1:\n"
                 : [ct] "+&r"(ct), [dst] "+&r"(dst), [v] "=&r"(v)
                 : [rt] "r"(rt), [pal] "r"(palette), [n] "r"(n)
                 : "memory");
}

// The compiler cannot use PIE or move q-register words directly into its
// scalar gather schedule. Eight sums use one VADDS.S16; MOVI.32.A extracts
// four pairs straight to ARs, with no index scratch table or PSRAM traffic.
// EXTUI selects bits 4..11 and 20..27 of each packed pair, preserving the
// page's signed-shift-and-mask even for a negative sum. Palette reads are
// genuine gathers and remain scalar. Load the high pixel first, low second,
// then shift high and OR low: neither load has an immediate consumer.
//
// Bounds at relief 0..100: |dH| <= 540, |h| <= 189, |light| <= 512,
// |dith| <= 256. Thus ct is in [-985,985], rt in [831,2289], and their
// sum in [-154,3274]. VADDS.S16 cannot saturate anywhere in that superset.
//
// Main body: 43 instructions per eight pixels (5.375/pixel), 129 bytes
// before density relaxation, with no exposed load-use interlocks under the
// documented pipeline model. About 5.375 cycles/pixel is only an ideal
// issue estimate; vector memory throughput, stores and task preemption
// still need device timing. Compared with GCC's eight-instruction loop,
// this also halves output stores. Host timings do not measure this edge.
//
// MOVI.32.A selectors 0..3 were separately probed in QEMU before use.
// q0 and q1 are free: GCC never allocates q registers and has no q clobber
// syntax. The block changes neither SAR nor CPENABLE. FreeRTOS owns the
// lazy CP3 enable/context save; production must never enable it directly.
//
// Input needs only halfword alignment at entry. A scalar prefix aligns ct
// to 16 bytes before any VLD. Output needs four-byte alignment for S32I:
// if aligning ct would leave dst at 2 mod 4, use the scalar loop for this
// row. This covers odd-width Bayer phases without touching a neighbour.
// Full vectors never read beyond n. There is no speculative next load.
GM_ANIM_IRAM __attribute__((noinline)) void quiltRowAsm(uint16_t *dst, const int16_t *ct, int rt,
                                                      const uint16_t *palette, int n) {
    if (n <= 0) return;
    if ((((uintptr_t)ct ^ (uintptr_t)dst) & 2u) != 0) {
        quiltScalarAsm(dst, ct, rt, palette, n);
        return;
    }
    int prefix = (int)((16u - ((uintptr_t)ct & 15u)) & 15u) / 2;
    if (prefix > n) prefix = n;
    if (prefix != 0) {
        quiltScalarAsm(dst, ct, rt, palette, prefix);
        dst += prefix;
        ct += prefix;
        n -= prefix;
    }
    const int blocks = n >> 3;
    if (blocks != 0) {
        uint32_t packed = (uint16_t)rt;
        packed |= packed << 16; // the same Q4 row term in both halfwords
        uint32_t hi, lo;
        // Predecrement only inside asm, never form a C pointer before the
        // array. The per-block ADDI restores the current output address
        // while covering the vector-load latency. The final ADDI leaves
        // dst pointing just past the vectors for the scalar tail.
        asm volatile("ee.movi.32.q q1, %[rt], 0\n"
                     "ee.movi.32.q q1, %[rt], 1\n"
                     "ee.movi.32.q q1, %[rt], 2\n"
                     "ee.movi.32.q q1, %[rt], 3\n"
                     "addi    %[dst], %[dst], -16\n"
                     "loopnez %[n], 1f\n"
                     "ee.vld.128.ip q0, %[ct], 16\n"
                     "addi    %[dst], %[dst], 16\n"
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
                     "s32i    %[hi], %[dst], 0\n"
                     "ee.movi.32.a q0, %[hi], 1\n"
                     "extui   %[lo], %[hi], 4, 8\n"
                     "extui   %[hi], %[hi], 20, 8\n"
                     "addx2   %[lo], %[lo], %[pal]\n"
                     "addx2   %[hi], %[hi], %[pal]\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[hi], %[hi], %[lo]\n"
                     "s32i    %[hi], %[dst], 4\n"
                     "ee.movi.32.a q0, %[hi], 2\n"
                     "extui   %[lo], %[hi], 4, 8\n"
                     "extui   %[hi], %[hi], 20, 8\n"
                     "addx2   %[lo], %[lo], %[pal]\n"
                     "addx2   %[hi], %[hi], %[pal]\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[hi], %[hi], %[lo]\n"
                     "s32i    %[hi], %[dst], 8\n"
                     "ee.movi.32.a q0, %[hi], 3\n"
                     "extui   %[lo], %[hi], 4, 8\n"
                     "extui   %[hi], %[hi], 20, 8\n"
                     "addx2   %[lo], %[lo], %[pal]\n"
                     "addx2   %[hi], %[hi], %[pal]\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[hi], %[hi], %[lo]\n"
                     "s32i    %[hi], %[dst], 12\n"
                     "1:\n"
                     "addi    %[dst], %[dst], 16\n"
                     : [ct] "+&r"(ct), [dst] "+&r"(dst), [hi] "=&r"(hi), [lo] "=&r"(lo)
                     : [rt] "r"(packed), [pal] "r"(palette), [n] "r"(blocks)
                     : "memory");
    }
    const int tail = n & 7;
    if (tail != 0) quiltScalarAsm(dst, ct, rt, palette, tail);
}
/* END VERBATIM PRODUCTION KERNELS */

static void quiltRowRef(uint16_t *dst, const int16_t *ct, int rt,
                        const uint16_t *palette, int n) {
    for (int x = 0; x < n; x++) {
        int sum = ct[x] + rt;
        dst[x] = palette[((uint32_t)sum >> 4) & 255];
    }
}

#define STORAGE 512
static int16_t source[STORAGE] __attribute__((aligned(16)));
static uint16_t got[STORAGE] __attribute__((aligned(16)));
static uint16_t want[STORAGE] __attribute__((aligned(16)));
static uint16_t palette[256] __attribute__((aligned(16)));
static int calls, pixels, mismatches;
static int firstCall, firstLane, firstGot, firstWant;

static void check(int srcOff, int dstOff, int n, int rt) {
    calls++;
    pixels += n;
    // One full guard vector on either side catches alignment-mask writes.
    // Avoid sweeping unused storage on 65536 one-vector boundary calls.
    const int checked = 16 + dstOff + n;
    for (int i = 0; i < checked; i++) got[i] = want[i] = 0xdead;
    // Offsets start past a full guard vector. Odd dstOff also tests the
    // scalar fallback's natural two-byte alignment, beyond the public ABI.
    quiltRowAsm(got + 8 + dstOff, source + 8 + srcOff, rt, palette, n);
    quiltRowRef(want + 8 + dstOff, source + 8 + srcOff, rt, palette, n);
    for (int i = 0; i < checked; i++) {
        if (got[i] != want[i]) {
            if (mismatches == 0) {
                firstCall = calls;
                firstLane = i - 8 - dstOff;
                firstGot = got[i];
                firstWant = want[i];
            }
            mismatches++;
        }
    }
}

int main(void) {
    // Bare-metal harness only. Production leaves CPENABLE to FreeRTOS.
    uint32_t cp = 8;
    asm volatile("wsr %0, cpenable\nisync\n" : : "r"(cp) : "memory");
    uart_puts("quilt: alignment and lengths\n");
    // Injective mapping over 0..255 with different high/low bytes and
    // entries at both RGB565 word extremes. Wrong gathers cannot alias.
    for (int i = 0; i < 256; i++) palette[i] = (uint16_t)(i * 251);
    palette[255] = 0xffff;

    static const int lengths[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 16, 17,
                           23, 31, 32, 33, 233, 240, 466, 480};
    static const int rows[] = {831, 1000, 1560, 2048, 2289};
    for (int i = 0; i < STORAGE; i++) source[i] = (int16_t)((i * 197) % 1971 - 985);
    for (int r = 0; r < 5; r++) {
        for (int s = 0; s < 8; s++) {
            for (int d = 0; d < 8; d++) {
                for (unsigned j = 0; j < sizeof(lengths) / sizeof(lengths[0]); j++)
                    check(s, d, lengths[j], rows[r]);
            }
        }
    }

    uart_puts("quilt: exhaustive production bounds\n");
    // Exhaust the Cartesian production bound, including all Q4 fractions
    // around zero and every 16-unit palette boundary. Every full block is
    // aligned here, so this sweep must execute the PIE path.
    for (int rt = 831; rt <= 2289; rt++) {
        for (int base = -985; base <= 985; base += 480) {
            int n = 986 - base;
            if (n > 480) n = 480;
            for (int i = 0; i < n; i++) source[8 + i] = (int16_t)(base + i);
            check(0, 0, n, rt);
        }
    }
    uart_puts("quilt: full signed storage ranges\n");
    // Full signed storage ranges, not just typical pillow amplitudes.
    for (int base = -32768; base <= 32767; base += 480) {
        int n = 32768 - base;
        if (n > 480) n = 480;
        for (int i = 0; i < n; i++) source[8 + i] = (int16_t)(base + i);
        check(0, 0, n, 0);
    }
    for (int i = 0; i < STORAGE; i++) source[i] = 0;
    for (int rt = -32768; rt <= 32767; rt++) check(0, 0, 8, rt);

    // Explicit zero-trip scalar kernel check, independent of the row
    // wrapper's early return. It must not overwrite even one guard.
    got[0] = 0xbeef;
    quiltScalarAsm(got, source, 1560, palette, 0);
    if (got[0] != 0xbeef) {
        if (!mismatches) { firstCall = calls + 1; firstLane = 0; firstGot = got[0]; firstWant = 0xbeef; }
        mismatches++;
    }
    if (mismatches) {
        uart_puts("GM_QEMUBENCH_PIE: FAIL quilt call="); uart_dec(firstCall);
        uart_puts(" lane="); uart_dec(firstLane);
        uart_puts(" got="); uart_dec(firstGot);
        uart_puts(" want="); uart_dec(firstWant);
        uart_puts(" mismatches="); uart_dec(mismatches);
    } else {
        uart_puts("GM_QEMUBENCH_PIE: PASS quilt calls="); uart_dec(calls);
        uart_puts(" pixels="); uart_dec(pixels);
        uart_puts(" mismatches=0 guards=0");
    }
    uart_puts("\nGM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
