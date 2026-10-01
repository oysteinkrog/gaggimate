/* Freestanding execution check for the literal production kernel. The C
 * reference independently implements the page/bandRef inner loop, including
 * its clamp, so a packed lane carry, wrong dither sign, phase wrap or store
 * overrun is visible. The test enables CP3 once; the kernel never does.
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
static void put_dec(uint32_t v) {
    char b[12];
    int n = 0;
    do { b[n++] = '0' + v % 10; v /= 10; } while (v);
    while (n) UART0_FIFO = b[--n];
}

// BEGIN VERBATIM PRODUCTION KERNELS
// GCC 14.2, xtensa-asm14.sh with the firmware flags, emits a 14-instruction
// hardware loop for bandRef. This scalar body transcribes its order: all
// three loads have an independent instruction before the consuming op.
// It handles 0..7 trailing pixels, starting at x modulo 8 == 0, and also
// makes zero-width calls safe. Return the wrapped phase for QEMU to check.
GM_ANIM_IRAM __attribute__((noinline)) uint32_t stripesScalarAsm(uint16_t *out, const uint16_t *tab,
                                                               const int16_t *off, const uint16_t *pal,
                                                               uint32_t ph, uint32_t step, int n) {
    uint32_t x = 0, a, b;
    asm volatile("loopnez %[n], 1f\n"
                 "extui   %[a], %[x], 0, 3\n"
                 "extui   %[b], %[ph], 20, 12\n"
                 "addx2   %[a], %[a], %[off]\n"
                 "addx2   %[b], %[b], %[tab]\n"
                 "l16si   %[a], %[a], 0\n"
                 "l16ui   %[b], %[b], 0\n"
                 "add     %[ph], %[ph], %[step]\n"
                 "add     %[a], %[a], %[b]\n"
                 "srai    %[a], %[a], 4\n"
                 "addx2   %[a], %[a], %[pal]\n"
                 "l16ui   %[a], %[a], 0\n"
                 "addi    %[x], %[x], 1\n"
                 "s16i    %[a], %[out], 0\n"
                 "addi    %[out], %[out], 2\n"
                 "1:\n"
                 : [out] "+&r"(out), [ph] "+&r"(ph), [x] "+&r"(x), [a] "=&r"(a), [b] "=&r"(b)
                 : [n] "r"(n), [tab] "r"(tab), [off] "r"(off), [pal] "r"(pal), [step] "r"(step)
                 : "memory");
    return ph;
}

// The edge beyond GCC's scalar schedule is keeping the eight Bayer offsets
// in q1 for the whole row and adding them to eight field samples in one
// VADDS.S16. Scalar DDS/table gathers fill q0 in pixel pairs; MOVI.32.A
// then extracts pairs for two interleaved palette gathers and one S32I.
// There is no vector gather and no scratch-memory spill between stages.
//
// Main body: 4*11 sample/pack instructions + 1 vector add + 4*10 palette/
// store instructions + 1 output advance = 86 instructions per 8 pixels,
// 10.75/pixel against GCC's 14. All load-use gaps are filled, including
// across the loop back edge. That is an issue-count lower bound for hot
// SRAM, not a device timing claim. Production timing decides the default.
//
// frame() clamps tab so tab + off lands in 0..4095 whatever the parameters
// are: signed saturation is inactive and extracting bits 4..11 is the exact
// >>4, with no negative sum for the unsigned extract to misread.
// off is one complete 16-byte row in allocHot's aligned Bayer table, so
// the only VLD span is aligned by construction and never crosses its end.
// out needs only the contract's four-byte alignment, including widths 233
// and 466. S32I stores full pairs only; the scalar tail touches no padding.
//
// q0/q1 are free because GCC never allocates q registers and supplies no
// q-register clobber syntax. SAR is untouched. Never write CPENABLE: the
// FreeRTOS lazy CP3 exception owns enabling and saving the task's PIE state.
// MOVI.32.A selectors 0..3 were separately executed by the QEMU probe in
// tests/anim_stripes/probe_movi before this kernel was written.
GM_ANIM_IRAM __attribute__((noinline)) uint32_t stripesRowAsm(uint16_t *out, const uint16_t *tab,
                                                            const int16_t *off, const uint16_t *pal,
                                                            uint32_t ph, uint32_t step, int n) {
    if (n <= 0) return ph;
    const int blocks = n >> 3;
    if (blocks != 0) {
        const int16_t *d = off;
        uint32_t lo, hi;
        asm volatile("ee.vld.128.ip q1, %[d], 0\n"
                     "loopnez %[n], 1f\n"
                     "extui   %[lo], %[ph], 20, 12\n"
                     "add     %[ph], %[ph], %[step]\n"
                     "extui   %[hi], %[ph], 20, 12\n"
                     "addx2   %[lo], %[lo], %[tab]\n"
                     "addx2   %[hi], %[hi], %[tab]\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "add     %[ph], %[ph], %[step]\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[lo], %[lo], %[hi]\n"
                     "ee.movi.32.q q0, %[lo], 0\n"
                     "extui   %[lo], %[ph], 20, 12\n"
                     "add     %[ph], %[ph], %[step]\n"
                     "extui   %[hi], %[ph], 20, 12\n"
                     "addx2   %[lo], %[lo], %[tab]\n"
                     "addx2   %[hi], %[hi], %[tab]\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "add     %[ph], %[ph], %[step]\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[lo], %[lo], %[hi]\n"
                     "ee.movi.32.q q0, %[lo], 1\n"
                     "extui   %[lo], %[ph], 20, 12\n"
                     "add     %[ph], %[ph], %[step]\n"
                     "extui   %[hi], %[ph], 20, 12\n"
                     "addx2   %[lo], %[lo], %[tab]\n"
                     "addx2   %[hi], %[hi], %[tab]\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "add     %[ph], %[ph], %[step]\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[lo], %[lo], %[hi]\n"
                     "ee.movi.32.q q0, %[lo], 2\n"
                     "extui   %[lo], %[ph], 20, 12\n"
                     "add     %[ph], %[ph], %[step]\n"
                     "extui   %[hi], %[ph], 20, 12\n"
                     "addx2   %[lo], %[lo], %[tab]\n"
                     "addx2   %[hi], %[hi], %[tab]\n"
                     "l16ui   %[lo], %[lo], 0\n"
                     "l16ui   %[hi], %[hi], 0\n"
                     "add     %[ph], %[ph], %[step]\n"
                     "slli    %[hi], %[hi], 16\n"
                     "or      %[lo], %[lo], %[hi]\n"
                     "ee.movi.32.q q0, %[lo], 3\n"
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
                     : [out] "+&r"(out), [ph] "+&r"(ph), [d] "+&r"(d), [lo] "=&r"(lo), [hi] "=&r"(hi)
                     : [n] "r"(blocks), [tab] "r"(tab), [pal] "r"(pal), [step] "r"(step)
                     : "memory");
    }
    if ((n & 7) != 0) return stripesScalarAsm(out, tab, off, pal, ph, step, n & 7);
    return ph;
}
// END VERBATIM PRODUCTION KERNELS

static uint32_t stripesRowRef(uint16_t *out, const uint16_t *tab,
                              const int16_t *off, const uint16_t *pal,
                              uint32_t ph, uint32_t step, int n) {
    for (int x = 0; x < n; ++x) {
        int idx = ((int)tab[ph >> 20] + off[x & 7]) >> 4;
        if (idx < 0) idx = 0;
        if (idx > 255) idx = 255;
        out[x] = pal[idx];
        ph += step;
    }
    return ph;
}

#define STORAGE 544
static uint16_t tab[4096] __attribute__((aligned(16)));
static uint16_t pal[256] __attribute__((aligned(16)));
static int16_t dith[64] __attribute__((aligned(16)));
static uint16_t got[STORAGE] __attribute__((aligned(16)));
static uint16_t want[STORAGE] __attribute__((aligned(16)));
static uint32_t calls, pixels, mismatches, firstCall, firstLane, firstGot, firstWant;
static uint32_t rng = 0x73547269u;

static uint32_t next_rand(void) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}
static void mismatch(int lane, uint32_t g, uint32_t w) {
    if (!mismatches) {
        firstCall = calls; firstLane = lane; firstGot = g; firstWant = w;
    }
    ++mismatches;
}

/* Compare the entire guarded buffers, not just the live pixels. All four
 * possible four-byte alignments modulo 16 are passed directly to the row
 * kernel. No vector stores are allowed to touch the guards or tail. */
static void run_case(int n, int alignment, int rowPhase, uint32_t ph, uint32_t step, int scalar) {
    ++calls;
    const int at = 8 + 2 * alignment;
    for (int i = 0; i < STORAGE; ++i) got[i] = want[i] = 0x5aa5;
    const int16_t *off = dith + rowPhase * 8;
    uint32_t g = scalar ? stripesScalarAsm(got + at, tab, off, pal, ph, step, n)
                        : stripesRowAsm(got + at, tab, off, pal, ph, step, n);
    const uint32_t w = stripesRowRef(want + at, tab, off, pal, ph, step, n);
    if (g != w) mismatch(STORAGE, g, w);
    for (int i = 0; i < STORAGE; ++i) {
        if (got[i] != want[i]) mismatch(i, got[i], want[i]);
    }
    if (n > 0) pixels += n;
}

/* The widest window frame() can hand the kernel: the coarsest grain leaves
 * a dither offset of +-512 (ditherAmp caps at 16 and the grain parameter
 * doubles it), and it clamps the table into 512..3583 to match, so tab + off
 * covers 0..4095 and nothing outside it. */
static void fill_tables(void) {
    for (int i = 0; i < 4096; ++i) tab[i] = 512 + next_rand() % 3072;
    // One-to-one palette, with both all-zero and all-one words, so wrong
    // indices cannot hide behind a quantized theme's repeated colours.
    for (int i = 0; i < 256; ++i) pal[i] = (uint16_t)(i * 257);
    for (int i = 0; i < 64; ++i) dith[i] = (int)(next_rand() % 1025) - 512;
}

int main(void) {
    uint32_t enable = 8;
    asm volatile("wsr %0, cpenable\nisync\n" : : "r"(enable));
    fill_tables();

    // Every table index, in both phase directions, including UINT32 wrap.
    for (int batch = 0; batch < 8; ++batch) {
        run_case(512, batch & 3, batch, (uint32_t)batch << 29, 1u << 20, 0);
        run_case(512, batch & 3, batch, ((uint32_t)batch << 29) - 1, 0u - (1u << 20), 0);
    }
    // Every table value and signed dither endpoint, with one full vector.
    for (int v = 512; v <= 3583; ++v) {
        tab[0] = v;
        for (int i = 0; i < 8; ++i) {
            static const int16_t edge[] = {-512, -511, -1, 0, 1, 511, 512, -128};
            dith[i] = edge[(i + v) & 7];
        }
        run_case(8, v & 3, 0, 0, 0, 0);
    }
    // Every offset the coarsest grain can produce, against the lowest and
    // the highest table value that is legal with it, so the sum sits on 0
    // and on 4095 for every one of them.
    for (int d = -512; d <= 512; ++d) {
        for (int i = 0; i < 64; ++i) dith[i] = d;
        tab[0] = (uint16_t)(d < 0 ? -d : 0);
        run_case(9, d & 3, 0, 0xfffffu, 0, 0);
        tab[0] = (uint16_t)(4095 - (d > 0 ? d : 0));
        run_case(9, d & 3, 0, 0xfffffu, 0, 0);
    }
    fill_tables();
    // Widths around every vector/pair boundary plus actual device widths.
    // Full 32-bit step extremes cover every sign, not just the production
    // maximum of floor(2^32 / 430) = 9988296 units per pixel.
    static const int widths[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17, 31, 32, 33, 233, 240, 466, 480, 511, 512};
    static const uint32_t steps[] = {0, 1, 0xffffffffu, 0xfffffu, 0x100000u, 0x100001u,
                                    0x7fffffffu, 0x80000000u, 0x80000001u,
                                    7279605u, 0u - 7279605u, 9988296u, 0u - 9988296u};
    static const uint32_t phases[] = {0, 1, 0xfffffu, 0x100000u, 0x7fffffffu, 0x80000000u, 0xffffffffu};
    for (unsigned n = 0; n < sizeof(widths) / sizeof(widths[0]); ++n) {
        for (unsigned s = 0; s < sizeof(steps) / sizeof(steps[0]); ++s) {
            for (unsigned ph = 0; ph < sizeof(phases) / sizeof(phases[0]); ++ph) {
                run_case(widths[n], (n + s + ph) & 3, (n + s + ph) & 7, phases[ph], steps[s], 0);
            }
        }
    }
    // Independent direct checks of the verbatim scalar transcription,
    // including zero trip and all possible scalar tails.
    for (int n = 0; n <= 17; ++n) {
        run_case(n, n & 3, n & 7, next_rand(), next_rand(), 1);
    }
    // Parameter endpoints as they reach the kernel: pitch 0/100 sets the
    // step, and depth, black level and grain set the table window frame()
    // clamps into. All waveform values are bounded by that window;
    // synthetic s traverses 0..8192, including both extremes. Exercise both
    // rotation directions, row phases and phase wrap at each combination.
    for (int pitch = 0; pitch <= 100; pitch += 100) {
        const uint32_t cycle = 590 - (pitch * 16 + 5) / 10;
        const uint32_t step = 0xffffffffu / cycle + ((0xffffffffu % cycle) == cycle - 1);
        for (int depth = 0; depth <= 100; depth += 100) {
            const int span = 70 + (depth * 65 + 50) / 100;
            for (int floorP = 0; floorP <= 100; floorP += 100) {
                const int base = (floorP * 88 + 50) / 100;
                for (int grainP = 0; grainP <= 100; grainP += 100) {
                    const int dmax = grainP * 512 / 100;
                    const int lo = dmax, hi = 4095 - dmax;
                    for (int i = 0; i < 64; ++i) dith[i] = (int16_t)((i & 1) ? dmax : -dmax);
                    for (int i = 0; i < 4096; ++i) {
                        const int s = (i == 4095) ? 8192 : i * 2;
                        int v = (base << 4) + ((s * span) >> 9);
                        if (v < lo) v = lo;
                        else if (v > hi) v = hi;
                        tab[i] = (uint16_t)v;
                    }
                    for (int r = 0; r < 8; ++r) {
                        run_case(480, r & 3, r, next_rand(), step, 0);
                        run_case(240, r & 3, r, next_rand(), 0u - step, 0);
                    }
                }
            }
        }
    }
    // Deterministic random phases/steps cover interior bit patterns and
    // varying palettes exercise the complete RGB565 word operand range.
    for (int i = 0; i < 512; ++i) {
        if ((i & 31) == 0) {
            fill_tables();
            for (int c = 0; c < 256; ++c) pal[c] = (uint16_t)next_rand();
        }
        run_case(next_rand() % 513, i & 3, i & 7, next_rand(), next_rand(), 0);
    }
    if (mismatches) {
        puts_uart("GM_QEMUBENCH_PIE: FAIL stripes call="); put_dec(firstCall);
        puts_uart(" lane="); put_dec(firstLane);
        puts_uart(" got="); put_dec(firstGot);
        puts_uart(" want="); put_dec(firstWant);
        puts_uart(" mismatches="); put_dec(mismatches); puts_uart("\n");
    } else {
        puts_uart("GM_QEMUBENCH_PIE: PASS stripes calls="); put_dec(calls);
        puts_uart(" pixels="); put_dec(pixels);
        puts_uart(" mismatches=0 (Q4, signed dither to +-512, sums on 0 and 4095, DDS wraps, tails, alignment, all eight parameter extremes)\n");
    }
    puts_uart("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
