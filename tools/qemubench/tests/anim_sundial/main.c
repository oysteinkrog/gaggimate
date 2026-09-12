/* Freestanding harness for Sundial. The three kernel functions below are
 * verbatim copies of AnimSundial.cpp. The independent reference evaluates
 * the page's cubic polynomials, never the kernel's lookup tables.
 * CP3 is enabled only in this bare-metal main, never in a kernel. */
#include <stdint.h>
#define GM_ANIM_IRAM
#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)
static void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n')
            UART0_FIFO = '\r';
        UART0_FIFO = (uint8_t)*s++;
    }
}
static void uart_uint(uint32_t v) {
    char b[11];
    int n = 0;
    do {
        b[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        UART0_FIFO = (uint8_t)b[--n];
}

GM_ANIM_IRAM __attribute__((noinline)) void sundialColumnsAsm(int16_t *out, const int16_t *col, const int16_t *off, int n) {
    int blocks = n >> 4;
    int16_t *dst = out;
    const int16_t *src = col;
    // Six instructions per 16 pixels, 0.375/pixel. Two independent loads
    // precede the adds, hiding their load-use gaps. Bayer repeats every eight.
    asm volatile("ee.vld.128.ip q4, %[off], 0\n"
                 "loopnez %[n], 1f\n"
                 "ee.vld.128.ip q0, %[src], 16\n"
                 "ee.vld.128.ip q1, %[src], 16\n"
                 "ee.vadds.s16 q0, q0, q4\n"
                 "ee.vadds.s16 q1, q1, q4\n"
                 "ee.vst.128.ip q0, %[dst], 16\n"
                 "ee.vst.128.ip q1, %[dst], 16\n"
                 "1:\n"
                 : [src] "+&r"(src), [dst] "+&r"(dst)
                 : [off] "r"(off), [n] "r"(blocks)
                 : "memory");
    for (int x = blocks * 16; x < n; x++)
        out[x] = col[x] + off[x & 7];
}

GM_ANIM_IRAM __attribute__((noinline)) void sundialBeamAsm(int16_t *pixels, uint16_t *work, const uint16_t *sm,
                                                           const uint16_t *rad, int g0q, int g1q, int step0, int step1,
                                                           int radialBias, int n) {
    // q = (g + 24064)*348, exactly the reference before >>16. No reduced
    // precision in the cursor. radialBias = 1466-rowRad; rad[r] includes the
    // contrast-scaled radial cubic for r clamped to 0..587. Production cp
    // stays within [-2254,264], steps within +/-178176, and |q| stays below
    // 100 million at 480 pixels, so the signed accumulators cannot overflow.
    // The two VMULs plus three loads, one field load, add and store cost
    // eight instructions per eight beam pixels, plus SSAI and block control.
    // Together with the scalar gather this is 26 instructions/beam pixel
    // before that setup, on top of the 6.625 instructions/pixel face path.
    // These are issue-count lower bounds, not measured LX7 cycle timings.
    while (n > 0) {
        if (((uintptr_t)pixels & 15u) != 0 || n < 8) {
            int u0 = g0q >> 16, u1 = g1q >> 16;
            u0 = u0 < 0 ? 0 : (u0 > 256 ? 256 : u0);
            u1 = u1 < 0 ? 0 : (u1 > 256 ? 256 : u1);
            int r = radialBias + *pixels;
            r = r < 0 ? 0 : (r > 587 ? 587 : r);
            *pixels += (rad[r] * ((sm[u0] * sm[u1]) >> 8)) >> 8;
            pixels++;
            n--;
            g0q += step0;
            g1q += step1;
            continue;
        }
        int16_t *src = pixels;
        uint16_t *tmp = work;
        int t0, t1;
        // Twenty-five instructions per pixel, at most 75 bytes in the 256-byte loop
        // limit. Three table loads each have an independent cursor/pointer
        // update before the store consumes the loaded value. Thirteen ARs,
        // no spill or division inside the loop. work holds three eight-lane
        // vectors: sm0, sm1, radial amplitude, at byte offsets 0, 16, 32.
        asm volatile("loop %[eight], 1f\n"
                     "srai %[t0], %[g0], 16\n"
                     "movi %[t1], 256\n"
                     "max %[t0], %[t0], %[zero]\n"
                     "min %[t0], %[t0], %[t1]\n"
                     "addx2 %[t0], %[t0], %[sm]\n"
                     "l16ui %[t0], %[t0], 0\n"
                     "add %[g0], %[g0], %[s0]\n"
                     "s16i %[t0], %[tmp], 0\n"
                     "srai %[t0], %[g1], 16\n"
                     "max %[t0], %[t0], %[zero]\n"
                     "min %[t0], %[t0], %[t1]\n"
                     "addx2 %[t0], %[t0], %[sm]\n"
                     "l16ui %[t0], %[t0], 0\n"
                     "add %[g1], %[g1], %[s1]\n"
                     "s16i %[t0], %[tmp], 16\n"
                     "l16si %[t0], %[src], 0\n"
                     "movi %[t1], 587\n"
                     "add %[t0], %[t0], %[bias]\n"
                     "max %[t0], %[t0], %[zero]\n"
                     "min %[t0], %[t0], %[t1]\n"
                     "addx2 %[t0], %[t0], %[rad]\n"
                     "l16ui %[t0], %[t0], 0\n"
                     "addi %[src], %[src], 2\n"
                     "s16i %[t0], %[tmp], 32\n"
                     "addi %[tmp], %[tmp], 2\n"
                     "1:\n"
                     : [src] "+&r"(src), [tmp] "+&r"(tmp), [g0] "+&r"(g0q), [g1] "+&r"(g1q), [t0] "=&r"(t0), [t1] "=&r"(t1)
                     : [sm] "r"(sm), [rad] "r"(rad), [s0] "r"(step0), [s1] "r"(step1), [bias] "r"(radialBias), [zero] "r"(0),
                       [eight] "r"(8)
                     : "memory");
        tmp = work;
        // 256*256 needs a 32-bit product. VMUL.U16 shifts that full product
        // before narrowing, preserving the endpoint 256 and BOTH >>8 stages.
        // The column and contribution sum stays in int16 at all knob extremes.
        // VMUL defines its result in stage 2: decrementing n before the add
        // fills the last multiply-use gap. The post-store advances pixels.
        asm volatile("ssai 8\n"
                     "ee.vld.128.ip q0, %[tmp], 16\n"
                     "ee.vld.128.ip q1, %[tmp], 16\n"
                     "ee.vld.128.ip q2, %[tmp], 0\n"
                     "ee.vmul.u16 q0, q0, q1\n"
                     "ee.vld.128.ip q3, %[px], 0\n"
                     "ee.vmul.u16 q0, q0, q2\n"
                     "addi %[n], %[n], -8\n"
                     "ee.vadds.s16 q3, q3, q0\n"
                     "ee.vst.128.ip q3, %[px], 16\n"
                     : [tmp] "+&r"(tmp), [px] "+&r"(pixels), [n] "+&r"(n)
                     :
                     : "memory");
    }
}

GM_ANIM_IRAM __attribute__((noinline)) void sundialPaletteAsm(uint16_t *out, int16_t *pixels, const uint16_t *pal, uint16_t *work,
                                                              int rowBase, int n) {
    // Four aligned vectors. Clamping in Q4 to [64,4080] before >>4 gives
    // exactly the page's palette clamp to [4,255]. Multiplication by one
    // with SAR=4 is the PIE arithmetic right shift for 16-bit lanes.
    for (int k = 0; k < 8; k++) {
        work[k] = rowBase;
        work[8 + k] = 64;
        work[16 + k] = 4080;
        work[24 + k] = 1;
    }
    int16_t *src = pixels, *dst = pixels;
    const uint16_t *constants = work;
    const int blocks = n >> 4;
    // Twelve instructions per 16 pixels, 0.75/pixel, with loads interleaved.
    asm volatile("ee.vld.128.ip q4, %[c], 16\n"
                 "ee.vld.128.ip q5, %[c], 16\n"
                 "ee.vld.128.ip q6, %[c], 16\n"
                 "ee.vld.128.ip q7, %[c], 0\n"
                 "ssai 4\n"
                 "loopnez %[n], 1f\n"
                 "ee.vld.128.ip q0, %[src], 16\n"
                 "ee.vld.128.ip q1, %[src], 16\n"
                 "ee.vadds.s16 q0, q0, q4\n"
                 "ee.vadds.s16 q1, q1, q4\n"
                 "ee.vmax.s16 q0, q0, q5\n"
                 "ee.vmax.s16 q1, q1, q5\n"
                 "ee.vmin.s16 q0, q0, q6\n"
                 "ee.vmin.s16 q1, q1, q6\n"
                 "ee.vmul.s16 q0, q0, q7\n"
                 "ee.vmul.s16 q1, q1, q7\n"
                 "ee.vst.128.ip q0, %[dst], 16\n"
                 "ee.vst.128.ip q1, %[dst], 16\n"
                 "1:\n"
                 : [src] "+&r"(src), [dst] "+&r"(dst), [c] "+&r"(constants)
                 : [n] "r"(blocks)
                 : "memory");
    for (int x = blocks * 16; x < n; x++) {
        int v = (pixels[x] + rowBase) >> 4;
        pixels[x] = v < 4 ? 4 : (v > 255 ? 255 : v);
    }
    const int16_t *idx = pixels;
    uint16_t *op = out;
    int t0, t1;
    // Eleven instructions per pair, 5.5/pixel. Two independently addressed
    // loads separate every load from its consumer; the output needs only the
    // contract's four-byte alignment, since no vector store touches out.
    asm volatile("loopnez %[n], 1f\n"
                 "l16ui %[t0], %[idx], 0\n"
                 "l16ui %[t1], %[idx], 2\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "addi %[idx], %[idx], 4\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t0], %[t0], %[t1]\n"
                 "s32i %[t0], %[out], 0\n"
                 "addi %[out], %[out], 4\n"
                 "1:\n"
                 : [idx] "+&r"(idx), [out] "+&r"(op), [t0] "=&r"(t0), [t1] "=&r"(t1)
                 : [pal] "r"(pal), [n] "r"(n >> 1)
                 : "memory");
    if (n & 1)
        out[n - 1] = pal[pixels[n - 1]];
}

#define MAX_N 480
#define GUARD 16
static int16_t columns[MAX_N + 2 * GUARD] __attribute__((aligned(16)));
static int16_t field[MAX_N + 2 * GUARD] __attribute__((aligned(16)));
static int16_t expected[MAX_N + 2 * GUARD] __attribute__((aligned(16)));
static uint16_t output[MAX_N + 2 * GUARD] __attribute__((aligned(16)));
static int16_t offsets[8] __attribute__((aligned(16)));
static uint16_t workspace[64] __attribute__((aligned(16)));
static uint16_t sm[257] __attribute__((aligned(16)));
static uint16_t radial[588] __attribute__((aligned(16)));
static uint16_t palette[256] __attribute__((aligned(16)));
static uint32_t calls, lanes, failures;
static int amplitude;

static int clamp(int x, int a, int b) { return x < a ? a : (x > b ? b : x); }
static int cubic(int u) { return (u * u * (768 - 2 * u)) >> 16; }
static void prepare(int contrast) {
    amplitude = (60 + contrast * 88 / 100) * 16;
    for (int i = 0; i < 257; i++)
        sm[i] = (uint16_t)cubic(i);
    for (int i = 0; i < 588; i++)
        radial[i] = (uint16_t)((amplitude * cubic(i * 256 / 587)) >> 8);
    /* An injective permutation of all 256 indices. A wrong gather cannot
     * disappear inside a run of repeated theme colours. */
    for (int i = 0; i < 256; i++)
        palette[i] = (uint16_t)(i * 251 + 37);
}

static void bad(const char *stage, int x, int got, int want) {
    if (failures++ == 0) {
        uart_puts("GM_QEMUBENCH_PIE: FAIL sundial stage=");
        uart_puts(stage);
        uart_puts(" call=");
        uart_uint(calls);
        uart_puts(" lane=");
        uart_uint(x);
        uart_puts(" got=");
        uart_uint((uint16_t)got);
        uart_puts(" want=");
        uart_uint((uint16_t)want);
        uart_puts("\n");
    }
}

/* Direct C version of bandRef's inner loop. g0q/g1q are the exact products
 * (g+SOFT_HALF)*INV_SOFT passed to the kernel, not rounded distances. */
static void beamRef(int16_t *p, int n, int g0q, int g1q, int step0, int step1,
                    int bias) {
    for (int x = 0; x < n; x++) {
        int u0 = clamp(g0q >> 16, 0, 256), u1 = clamp(g1q >> 16, 0, 256);
        int ur = (bias + p[x]) * 256 / 587;
        if (ur > 256)
            ur = 256;
        if (u0 > 0 && u1 > 0 && ur > 0) {
            int amp = (amplitude * cubic(ur)) >> 8;
            p[x] += (amp * ((cubic(u0) * cubic(u1)) >> 8)) >> 8;
        }
        g0q += step0;
        g1q += step1;
    }
}

/* Every call checks red zones around the beam, column, output and work spans.
 * shift=0..7 covers all 16-bit source alignments; output has only four-byte
 * alignment, with all four possible offsets modulo 16. */
static void beamCase(int n, int shift, int g0q, int g1q, int step0, int step1,
                     int bias, int seed) {
    calls++;
    for (int i = 0; i < MAX_N + 2 * GUARD; i++)
        field[i] = expected[i] = (int16_t)0x5234;
    for (int i = 0; i < 64; i++)
        workspace[i] = 0x6789;
    for (int x = 0; x < n; x++) {
        /* cp spans every integer -2254..264 over the sweep. This covers the
         * surface, shading=100 and ditherAmp's maximum 154 Q4 offset. */
        int v = (x * 37 + seed * 71) % 2519 - 2254;
        field[GUARD + shift + x] = expected[GUARD + shift + x] = (int16_t)v;
    }
    beamRef(expected + GUARD + shift, n, g0q, g1q, step0, step1, bias);
    sundialBeamAsm(field + GUARD + shift, workspace + 8, sm, radial, g0q, g1q,
                   step0, step1, bias, n);
    for (int i = 0; i < MAX_N + 2 * GUARD; i++)
        if (field[i] != expected[i])
            bad("beam", i, field[i], expected[i]);
    for (int i = 0; i < 64; i++)
        if ((i < 8 || i >= 32) && workspace[i] != 0x6789)
            bad("work", i, workspace[i], 0x6789);
    lanes += n;
}

static void rowCase(int n, int shift, int rowBase, int seed, int extreme) {
    calls++;
    for (int i = 0; i < MAX_N + 2 * GUARD; i++) {
        field[i] = expected[i] = (int16_t)0x5234;
        output[i] = 0x4567;
    }
    for (int i = 0; i < 64; i++)
        workspace[i] = 0x6789;
    for (int k = 0; k < 8; k++)
        offsets[k] =
            extreme ? 0 : (int16_t)(((k * 97 + seed * 29) % 309) - 154);
    for (int x = 0; x < n; x++) {
        /* Both endpoints of the production column range, with zero dither
         * in this case. The cubic/PIE sums never approach int16 saturation. */
        columns[GUARD + x] =
            extreme ? (int16_t)((x & 1) ? 110 : -1990)
                    : (int16_t)((x * 37 + seed * 71) % 2101 - 1990);
        expected[GUARD + x] = columns[GUARD + x] + offsets[x & 7];
    }
    sundialColumnsAsm(field + GUARD, columns + GUARD, offsets, n);
    for (int i = 0; i < MAX_N + 2 * GUARD; i++)
        if (field[i] != expected[i])
            bad("columns", i, field[i], expected[i]);
    sundialPaletteAsm(output + GUARD + shift, field + GUARD, palette,
                      workspace + 8, rowBase, n);
    for (int i = 0; i < MAX_N + 2 * GUARD; i++) {
        int x = i - GUARD - shift;
        int want =
            x >= 0 && x < n
                ? palette[clamp((expected[GUARD + x] + rowBase) >> 4, 4, 255)]
                : 0x4567;
        if (output[i] != want)
            bad("palette", i, output[i], want);
    }
    for (int i = 0; i < 64; i++)
        if ((i < 8 || i >= 40) && workspace[i] != 0x6789)
            bad("constants", i, workspace[i], 0x6789);
    lanes += n;
}

int main(void) {
    uint32_t cp = 8;
    asm volatile("wsr %0, cpenable\nrsync\n" ::"r"(cp));
    static const int widths[] = {0,  1,  2,  7,  8,   9,   15,  16,
                                 17, 31, 32, 33, 233, 240, 466, 480};
    static const int bases[] = {-32768, -4096, -1,   0,    63,   64,   65,
                                1224,   1664,  2104, 4079, 4080, 4095, 32767};
    static const int contrasts[] = {0, 25, 100};
    for (int c = 0; c < 3; c++) {
        prepare(contrasts[c]);
        for (int k = 0; k < 16; k++)
            for (int a = 0; a < 8; a++) {
                int n = widths[k];
                beamCase(n, a, -65536, 256 * 65536 + 65535, 178176, -178176,
                         1466, k + a);
                for (int b = 0; b < 14; b++)
                    rowCase(n, (a & 3) * 2, bases[b], k + a, b == 0 || b == 13);
            }
        /* Every ordered pair of Q8 ray weights, with all fractional boundary
         * bits retained and every radial-table entry reached by the sweep. */
        for (int u0 = 0; u0 <= 256; u0++)
            beamCase(257, u0 & 7, u0 * 65536 + (u0 & 1 ? 65535 : 0), 0, 0,
                     65536, 1466, u0);
        /* All possible signed sine-direction values, both step extrema, and
         * phases on either side of the clamp endpoints. */
        for (int d = -512; d <= 512; d++)
            beamCase(480, d & 7, 0, 256 * 65536, d * 348, -d * 348, 1466,
                     d + 512);
        /* Exact clamp edges, negative radial numerators and beam saturation. */
        for (int r = -1; r <= 588; r++)
            beamCase(8, r & 7, 256 * 65536, 256 * 65536, 0, 0, r + 2254, r + 1);
    }
    if (!failures) {
        uart_puts("GM_QEMUBENCH_PIE: PASS sundial kernels bit-exact; calls=");
        uart_uint(calls);
        uart_puts(" lanes=");
        uart_uint(lanes);
        uart_puts(" mismatches=0; Q8 pairs, radial range, contrast 0/25/100, "
                  "steps -512..512, widths 0..480, alignments and guards\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {
    }
}
