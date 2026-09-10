/* Tide's production kernel, copied verbatim from AnimTide.cpp, and an
 * independent C implementation of bandRef's integer pixel formula.
 * Harness mode: UART only, no libc, heap, constructors, or OS.
 *
 * Production base is 12*16..214*16 and dith is -256..256. The production
 * sweep exhausts their Cartesian product. This covers every value that
 * Speed 0/100, Band width 0/100 (halfW 60/130), Glow 0/100 (peak 44/106),
 * all four moving centres, and theme dither amplitudes can pass here.
 * Additional sweeps cover all 65,536 signed bases and all 65,536 signed
 * offsets, including saturating sums beyond int16. Final clamping makes
 * PIE's intermediate signed saturation equivalent to the C int32 sum.
 *
 * Widths include zero, every small prefix/body/tail combination, odd 233,
 * full 466, and 240/480. All eight halfword offsets within a 16-byte block
 * are checked, a superset of the firmware's four allowed word offsets.
 * Output and scratch guards are checked on every call. Only main enables
 * CP3; the verbatim production kernel never writes CPENABLE.
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
static void dec_uart(uint32_t value) {
    char digits[10];
    int n = 0;
    do {
        digits[n++] = '0' + value % 10;
        value /= 10;
    } while (value != 0);
    while (n) UART0_FIFO = (uint8_t)digits[--n];
}

GM_ANIM_IRAM __attribute__((noinline)) void tideRowAsm(uint16_t *out, const int16_t *off,
                                                     const uint16_t *pal, int base, int w,
                                                     uint16_t *scratch) {
    if (w <= 0) {
        return;
    }
    const uint32_t packedBase = (uint32_t)(uint16_t)base * 0x00010001u;
    uint16_t *sp = scratch;
    int a, b, zero, cap, n;
    asm volatile("movi    %[zero], 0\n"
                 "movi    %[cap], 255\n"
                 "movi    %[n], 4\n"
                 "ee.vld.128.ip q0, %[off], 0\n"
                 "ee.movi.32.q q1, %[base], 0\n"
                 "ee.movi.32.q q1, %[base], 1\n"
                 "ee.movi.32.q q1, %[base], 2\n"
                 "ee.movi.32.q q1, %[base], 3\n"
                 "ee.vadds.s16 q0, q0, q1\n"
                 "ee.vst.128.ip q0, %[sp], 0\n"
                 "loop    %[n], 1f\n"
                 "l16si   %[a], %[sp], 0\n"
                 "l16si   %[b], %[sp], 2\n"
                 "srai    %[a], %[a], 4\n"
                 "srai    %[b], %[b], 4\n"
                 "max     %[a], %[a], %[zero]\n"
                 "max     %[b], %[b], %[zero]\n"
                 "min     %[a], %[a], %[cap]\n"
                 "min     %[b], %[b], %[cap]\n"
                 "addx2   %[a], %[a], %[pal]\n"
                 "addx2   %[b], %[b], %[pal]\n"
                 "l16ui   %[b], %[b], 0\n"
                 "l16ui   %[a], %[a], 0\n"
                 "slli    %[b], %[b], 16\n"
                 "or      %[a], %[a], %[b]\n"
                 "s32i    %[a], %[sp], 0\n"
                 "addi    %[sp], %[sp], 4\n"
                 "1:\n"
                 : [off] "+&r"(off), [sp] "+&r"(sp), [a] "=&r"(a), [b] "=&r"(b),
                   [zero] "=&r"(zero), [cap] "=&r"(cap), [n] "=&r"(n)
                 : [base] "r"(packedBase), [pal] "r"(pal)
                 : "memory");

    int prefix = 0;
    while (prefix < w && ((uintptr_t)out & 15u) != 0) {
        *out++ = scratch[prefix++];
    }
    const int remaining = w - prefix;
    const uint16_t *rotated = scratch + prefix;
    // USAR loads the aligned block containing rotated, and records its
    // low address bits. SRLI fills the vector load-use slot before SRC.Q.
    asm volatile("ee.ld.128.usar.ip q0, %[pat], 0\n"
                 "srli    %[n], %[remaining], 3\n"
                 "ee.src.q q1, q0, q0\n"
                 "loopnez %[n], 1f\n"
                 "ee.vst.128.ip q1, %[out], 16\n"
                 "1:\n"
                 : [pat] "+&r"(rotated), [out] "+&r"(out), [n] "=&r"(n)
                 : [remaining] "r"(remaining)
                 : "memory");
    for (int i = 0; i < (remaining & 7); i++) {
        out[i] = scratch[(prefix + i) & 7];
    }
}

static void tideRowRef(uint16_t *out, const int16_t *off,
                       const uint16_t *pal, int base, int w) {
    for (int x = 0; x < w; x++) {
        const int q4 = base + off[x & 7];
        const int idx = q4 < 0 ? 0 : (q4 > 4080 ? 255 : q4 >> 4);
        out[x] = pal[idx];
    }
}

#define MAX_W 511
#define GUARD 8
static uint16_t got[MAX_W + 3 * GUARD] __attribute__((aligned(16)));
static uint16_t want[MAX_W + 3 * GUARD] __attribute__((aligned(16)));
static uint16_t scratch[3 * GUARD] __attribute__((aligned(16)));
static int16_t offsets[8] __attribute__((aligned(16)));
static uint16_t palette[256] __attribute__((aligned(16)));
static uint32_t calls, pixels, mismatches, firstCall, firstLane, firstGot, firstWant;

static void fail(int lane, uint16_t actual, uint16_t expected) {
    if (!mismatches) {
        firstCall = calls;
        firstLane = (uint32_t)lane;
        firstGot = actual;
        firstWant = expected;
    }
    mismatches++;
}

static void check(int base, int w, int alignment) {
    const int begin = GUARD + alignment;
    const int end = begin + w;
    const uint16_t sentinel = 0x5a39;
    calls++;
    pixels += (uint32_t)w;
    for (int i = 0; i < end + GUARD; i++) got[i] = want[i] = sentinel;
    for (int i = 0; i < 3 * GUARD; i++) scratch[i] = sentinel;
    tideRowRef(want + begin, offsets, palette, base, w);
    tideRowAsm(got + begin, offsets, palette, base, w, scratch + GUARD);
    for (int i = 0; i < end + GUARD; i++) {
        if (got[i] != want[i]) fail(i - begin, got[i], want[i]);
    }
    for (int i = 0; i < 3 * GUARD; i++) {
        if ((i < GUARD || i >= 2 * GUARD || w == 0) && scratch[i] != sentinel) {
            fail(-100 - i, scratch[i], sentinel);
        }
    }
}

int main(void) {
    const uint32_t cp = 8; /* Bare-metal harness only: enable CP3 once. */
    __asm__ volatile("wsr %0, cpenable\nrsync\n" : : "r"(cp) : "memory");
    for (int i = 0; i < 256; i++) palette[i] = (uint16_t)(i * 257u ^ 0xa55au);

    /* Every reachable base crossed with every reachable signed Q4 dither. */
    for (int base = 192; base <= 3424; base += 16) {
        for (int d = -256; d <= 256; d += 8) {
            for (int k = 0; k < 8; k++) offsets[k] = d + k > 256 ? 256 : d + k;
            check(base, 17, (int)(calls & 7));
        }
    }

    /* All signed base operands against offset endpoints and clamp edges. */
    static const int16_t edges[8] = {-32768, -32767, -257, -1, 0, 1, 32766, 32767};
    for (int k = 0; k < 8; k++) offsets[k] = edges[k];
    for (int base = -32768; base <= 32767; base++) check(base, 16, (int)(calls & 7));

    /* All signed offsets against both production and signed base endpoints. */
    static const int bases[4] = {-32768, 192, 3424, 32767};
    for (int d = -32768; d <= 32760; d += 8) {
        for (int k = 0; k < 8; k++) offsets[k] = d + k;
        for (int b = 0; b < 4; b++) check(bases[b], 16, (int)(calls & 7));
    }

    /* Zero trip, scalar-only rows, every remainder, panel widths, long odd tail. */
    static const int widths[5] = {233, 240, 466, 480, 511};
    for (int phase = 0; phase < 8; phase++) {
        for (int k = 0; k < 8; k++) offsets[k] = (int16_t)((((k + phase) * 73) % 513) - 256);
        for (int alignment = 0; alignment < 8; alignment++) {
            for (int w = 0; w <= 33; w++) check(192 + phase * 448, w, alignment);
            for (int i = 0; i < 5; i++) check(192 + phase * 448, widths[i], alignment);
        }
    }

    if (mismatches) {
        puts_uart("GM_QEMUBENCH_PIE: FAIL tideRowAsm mismatches="); dec_uart(mismatches);
        puts_uart(" call="); dec_uart(firstCall);
        puts_uart(" lane="); dec_uart(firstLane);
        puts_uart(" got="); dec_uart(firstGot);
        puts_uart(" want="); dec_uart(firstWant);
        puts_uart("\n");
    } else {
        puts_uart("GM_QEMUBENCH_PIE: PASS tideRowAsm calls="); dec_uart(calls);
        puts_uart(" pixels="); dec_uart(pixels);
        puts_uart(" mismatches=0 (Q4 Cartesian sweep, int16 ranges, 8 alignments, guards, widths 0..33/233/240/466/480/511)\n");
    }
    puts_uart("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
