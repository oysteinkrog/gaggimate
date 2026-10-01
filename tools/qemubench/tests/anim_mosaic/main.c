/* Harness mode, no libc. The macro and kernel below are copied verbatim
 * from AnimMosaic.cpp; keep both copies identical. All row inputs have a
 * readable vector of lookahead, exactly like the production comb table.
 * Run tests/anim_mosaic/probe first for the four signed PIE instructions.
 */
#include <stdint.h>
#define GM_ANIM_IRAM

// clang-format off
#define MOSAIC_GATHER8 \
    "l16ui %[t0], %[idx], 0\n" \
    "l16ui %[t1], %[idx], 2\n" \
    "l16ui %[t2], %[idx], 4\n" \
    "l16ui %[t3], %[idx], 6\n" \
    "addx2 %[t0], %[t0], %[pal]\n" \
    "addx2 %[t1], %[t1], %[pal]\n" \
    "addx2 %[t2], %[t2], %[pal]\n" \
    "addx2 %[t3], %[t3], %[pal]\n" \
    "l16ui %[t0], %[t0], 0\n" \
    "l16ui %[t1], %[t1], 0\n" \
    "l16ui %[t2], %[t2], 0\n" \
    "l16ui %[t3], %[t3], 0\n" \
    "slli %[t1], %[t1], 16\n" \
    "slli %[t3], %[t3], 16\n" \
    "or %[t0], %[t0], %[t1]\n" \
    "or %[t2], %[t2], %[t3]\n" \
    "s32i %[t0], %[out], 0\n" \
    "s32i %[t2], %[out], 4\n" \
    "l16ui %[t0], %[idx], 8\n" \
    "l16ui %[t1], %[idx], 10\n" \
    "l16ui %[t2], %[idx], 12\n" \
    "l16ui %[t3], %[idx], 14\n" \
    "addx2 %[t0], %[t0], %[pal]\n" \
    "addx2 %[t1], %[t1], %[pal]\n" \
    "addx2 %[t2], %[t2], %[pal]\n" \
    "addx2 %[t3], %[t3], %[pal]\n" \
    "l16ui %[t0], %[t0], 0\n" \
    "l16ui %[t1], %[t1], 0\n" \
    "l16ui %[t2], %[t2], 0\n" \
    "l16ui %[t3], %[t3], 0\n" \
    "slli %[t1], %[t1], 16\n" \
    "slli %[t3], %[t3], 16\n" \
    "or %[t0], %[t0], %[t1]\n" \
    "or %[t2], %[t2], %[t3]\n" \
    "s32i %[t0], %[out], 8\n" \
    "s32i %[t2], %[out], 12\n"

// BEGIN VERBATIM QEMU KERNEL
GM_ANIM_IRAM __attribute__((noinline)) void mosaicRowAsm(uint16_t *out, const int16_t *a, const int16_t *b,
                                                        const int16_t *off, const uint16_t *pal,
                                                        int rv, int wy, int n) {
    const int groups = n / 8;
    if (groups > 0) {
        uint16_t indices[8] __attribute__((aligned(16)));
        uint32_t rowWord = (uint16_t)rv;
        rowWord |= rowWord << 16;
        const uint32_t weightWord = (uint32_t)wy | ((uint32_t)wy << 16);
        int t0, t1, t2, t3;
        asm volatile(
            "ee.vld.128.ip q3, %[off], 0\n"
            "ee.movi.32.q q4, %[row], 0\n"
            "ee.movi.32.q q4, %[row], 1\n"
            "ee.movi.32.q q4, %[row], 2\n"
            "ee.movi.32.q q4, %[row], 3\n"
            "ee.vadds.s16 q3, q3, q4\n"
            "ee.zero.q q5\n"
            "movi %[t0], -1\n"
            "extui %[t0], %[t0], 0, 12\n" // 4095, the maximum Q4 field before >>4
            "slli %[t1], %[t0], 16\n"
            "or %[t0], %[t0], %[t1]\n"
            "ee.movi.32.q q6, %[t0], 0\n"
            "ee.movi.32.q q6, %[t0], 1\n"
            "ee.movi.32.q q6, %[t0], 2\n"
            "ee.movi.32.q q6, %[t0], 3\n"
            "movi %[t0], 16\n" // (field * 16) >> SAR(8) equals field >> 4
            "slli %[t1], %[t0], 16\n"
            "or %[t0], %[t0], %[t1]\n"
            "ee.movi.32.q q7, %[t0], 0\n"
            "ee.movi.32.q q7, %[t0], 1\n"
            "ee.movi.32.q q7, %[t0], 2\n"
            "ee.movi.32.q q7, %[t0], 3\n"
            "ssai 8\n"
            "beqz %[wy], 3f\n"
            "ee.movi.32.q q4, %[wy], 0\n"
            "ee.movi.32.q q4, %[wy], 1\n"
            "ee.movi.32.q q4, %[wy], 2\n"
            "ee.movi.32.q q4, %[wy], 3\n"
            "ee.vld.128.ip q0, %[a], 16\n"
            "ee.vld.128.ip q1, %[b], 16\n"
            "loopnez %[n], 1f\n"
            "ee.vsubs.s16 q2, q1, q0\n"
            "ee.vmul.s16 q2, q2, q4\n"
            "ee.vadds.s16 q0, q0, q3\n"
            "ee.vadds.s16 q2, q2, q0\n"
            "ee.vmax.s16 q2, q2, q5\n"
            "ee.vmin.s16 q2, q2, q6\n"
            "ee.vmul.s16 q2, q2, q7\n"
            "ee.vld.128.ip q0, %[a], 16\n"
            "ee.vst.128.ip q2, %[idx], 0\n"
            "ee.vld.128.ip q1, %[b], 16\n"
            MOSAIC_GATHER8
            "addi %[out], %[out], 16\n"
            "1:\n"
            "addi %[b], %[b], -16\n" // rewind the unused lookahead before the scalar tail
            "j 4f\n"
            "3:\n"
            "ee.vld.128.ip q0, %[a], 16\n"
            "loopnez %[n], 2f\n"
            "ee.vadds.s16 q2, q0, q3\n"
            "ee.vmax.s16 q2, q2, q5\n"
            "ee.vmin.s16 q2, q2, q6\n"
            "ee.vmul.s16 q2, q2, q7\n"
            "ee.vld.128.ip q0, %[a], 16\n"
            "ee.vst.128.ip q2, %[idx], 0\n"
            MOSAIC_GATHER8
            "addi %[out], %[out], 16\n"
            "2:\n"
            "4:\n"
            "addi %[a], %[a], -16\n"
            : [out] "+&r"(out), [a] "+&r"(a), [b] "+&r"(b),
              [t0] "=&r"(t0), [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3)
            : [off] "r"(off), [row] "r"(rowWord), [wy] "r"(weightWord), [pal] "r"(pal),
              [n] "r"(groups), [idx] "r"(indices)
            : "memory");
    }
    for (int x = 0; x < n % 8; ++x) {
        const int value = wy ? a[x] + (((b[x] - a[x]) * wy) >> 8) : a[x];
        int v = (value + rv + off[x]) >> 4; // groups always end at Bayer x phase zero
        v = v < 0 ? 0 : (v > 255 ? 255 : v);
        out[x] = pal[v];
    }
}
// END VERBATIM QEMU KERNEL
#undef MOSAIC_GATHER8
// clang-format on
#define MAX_W 480
#define STORAGE (MAX_W + 32)
static int16_t srcA[MAX_W + 8] __attribute__((aligned(16)));
static int16_t srcB[MAX_W + 8] __attribute__((aligned(16)));
static int16_t offsets[8] __attribute__((aligned(16)));
static uint16_t palette[256] __attribute__((aligned(16)));
static uint16_t got[STORAGE] __attribute__((aligned(16)));
static uint16_t want[STORAGE] __attribute__((aligned(16)));
static unsigned calls, pixels, mismatches;
static unsigned firstCall, firstLane, firstGot, firstWant;
#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)
static void puts0(const char *s) {
    while (*s) {
        if (*s == '\n')
            UART0_FIFO = '\r';
        UART0_FIFO = (uint8_t)*s++;
    }
}
static void dec0(unsigned v) {
    char buf[12];
    int n = 0;
    do {
        buf[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        UART0_FIFO = (uint8_t)buf[--n];
}

/* Independent C formulation of the page/bandRef inner loop. Writing single
 * uint16 pixels also independently checks the assembly's packed stores. */
static void rowRef(uint16_t *out, const int16_t *a, const int16_t *b, const int16_t *d, const uint16_t *pal, int rv, int wy,
                   int n) {
    for (int x = 0; x < n; x++) {
        int value = a[x];
        if (wy)
            value += ((b[x] - value) * wy) >> 8;
        int ix = (value + rv + d[x & 7]) >> 4;
        if (ix < 0)
            ix = 0;
        if (ix > 255)
            ix = 255;
        out[x] = pal[ix];
    }
}

static void check(int n, int rv, int wy, int align) {
    const int begin = 8 + align * 2; /* four-byte alignment, all four residues modulo 16 */
    const int end = begin + n + 8;
    for (int i = 0; i < end; i++)
        got[i] = want[i] = (uint16_t)(0xD37Bu ^ (unsigned)i * 997u);
    mosaicRowAsm(got + begin, srcA, srcB, offsets, palette, rv, wy, n);
    rowRef(want + begin, srcA, srcB, offsets, palette, rv, wy, n);
    ++calls;
    pixels += (unsigned)n;
    for (int i = 0; i < end; i++) {
        if (got[i] != want[i]) {
            if (!mismatches) {
                firstCall = calls;
                firstLane = (unsigned)i;
                firstGot = got[i];
                firstWant = want[i];
            }
            ++mismatches;
        }
    }
}

static void fill(unsigned seed) {
    /* Every integer in the full field range is reachable in this synthetic
     * domain. Guard vectors get distinct data so prefetch misuse is visible. */
    for (int x = 0; x < MAX_W + 8; x++) {
        srcA[x] = (int16_t)(160u + ((unsigned)x * 53u + seed * 41u) % 4401u);
        srcB[x] = (int16_t)(4560 - ((unsigned)x * 67u + seed * 137u) % 4401u);
    }
    for (int x = 0; x < 8; x++)
        offsets[x] = (int16_t)(((unsigned)x * 71u + seed * 29u) % 513u - 256);
}

int main(void) {
    /* Bare-metal harness only. Production never enables coprocessors itself. */
    uint32_t cp = 255;
    asm volatile("wsr %0, cpenable\nrsync\n" ::"r"(cp));
    for (unsigned i = 0; i < 256; i++)
        palette[i] = (uint16_t)((i * 257u) ^ 0xA55Au); /* injective RGB565 words */

    /* Every width, zero trips, odd tails, panel modes and output alignments. */
    for (int align = 0; align < 4; align++)
        for (int n = 0; n <= MAX_W; n++) {
            fill((unsigned)n + 503u * (unsigned)align);
            check(n, (n * 37) % 801 - 400, 0, align);
            check(n, (n * 37) % 801 - 400, 1 + (n * 19) % 256, align);
        }
    /* Exhaust the entire weight range, including the page's fourteen actual
     * seam weights, plus the exact endpoint 256 (b is reproduced). */
    for (int wy = 0; wy <= 256; wy++) {
        fill((unsigned)wy + 1709u);
        check(480, (wy * 37) % 801 - 400, wy, wy & 3);
    }
    /* Every source value, row term and Bayer offset at unit resolution. */
    for (int v = 160; v <= 4560; v += 8) {
        for (int x = 0; x < MAX_W + 8; x++) {
            srcA[x] = (int16_t)(160 + (v - 160 + x) % 4401);
            srcB[x] = (int16_t)(4560 - (v - 160 + x) % 4401);
        }
        for (int x = 0; x < 8; x++)
            offsets[x] = (int16_t)(x & 1 ? 256 : -256);
        check(8, (v / 8) & 1 ? 400 : -400, (v / 8) % 257, (v / 8) & 3);
    }
    for (int rv = -400; rv <= 400; rv++) {
        fill((unsigned)(rv + 2301));
        check(16, rv, (rv + 400) % 257, rv & 3);
    }
    for (int d = -256; d <= 256; d++) {
        fill((unsigned)(d + 3409));
        for (int x = 0; x < 8; x++)
            offsets[x] = (int16_t)d;
        check(16, (d * 31 + 8000) % 801 - 400, (d + 256) % 257, d & 3);
    }
    /* Cartesian endpoint combinations. All eight dither endpoints are in
     * each call, so both clamps and both signs of rounded lerps meet. */
    static const int ends[] = {160, 161, 399, 400, 4095, 4096, 4559, 4560};
    static const int rows[] = {-400, -399, -1, 0, 1, 399, 400};
    static const int weights[] = {0, 1, 2, 127, 128, 129, 254, 255, 256};
    static const int ds[] = {-256, -255, -1, 0, 1, 254, 255, 256};
    for (int ai = 0; ai < 8; ai++)
        for (int bi = 0; bi < 8; bi++)
            for (int ri = 0; ri < 7; ri++)
                for (int wi = 0; wi < 9; wi++) {
                    for (int x = 0; x < 16; x++) {
                        srcA[x] = (int16_t)ends[ai];
                        srcB[x] = (int16_t)ends[bi];
                    }
                    for (int x = 0; x < 8; x++)
                        offsets[x] = (int16_t)ds[x];
                    check(8, rows[ri], weights[wi], (ai + bi + ri + wi) & 3);
                }
    /* Q9 cubed raised sine at contrast 0/default/100. Exhaust u=0..512,
     * the actual operand domain of the page's SIN table after raising. */
    static const int amps[] = {1900, 2410, 3600};
    for (int ai = 0; ai < 3; ai++)
        for (int u = 0; u <= 512; u++) {
            for (int x = 0; x < 16; x++) {
                int a = u, b = 512 - u;
                srcA[x] = (int16_t)(560 + ((((a * a) >> 9) * a >> 9) * amps[ai] >> 9) + (x & 1 ? 400 : -400));
                srcB[x] = (int16_t)(560 + ((((b * b) >> 9) * b >> 9) * amps[ai] >> 9) + (x & 1 ? -400 : 400));
            }
            for (int x = 0; x < 8; x++)
                offsets[x] = (int16_t)ds[x];
            check(8, u & 1 ? -400 : 400, u % 257, u & 3);
        }
    /* Speed 0/default/100 and millis wrap feed different tile phases.
     * The synthetic triangle spans the same raised-sine u domain without
     * requiring libm or copying the firmware's sine builder into the test. */
    static const unsigned speeds[] = {4, 10, 48};
    static const uint32_t times[] = {0u, 33u, 1990u, 4960u, 7930u, 1000000000u, 4294900000u, 0xffffffffu};
    for (int si = 0; si < 3; si++)
        for (int ti = 0; ti < 8; ti++)
            for (unsigned rate = 4; rate <= 11; rate++) {
                uint32_t base = times[ti] * speeds[si];
                for (int x = 0; x < MAX_W + 8; x++) {
                    unsigned ph = (((base * rate) >> 10) + (unsigned)x * 127u) & 1023u;
                    int u = (int)(ph <= 512 ? ph : 1024 - ph);
                    int v = 512 - u;
                    srcA[x] = (int16_t)(560 + ((((u * u) >> 9) * u >> 9) * 3600 >> 9));
                    srcB[x] = (int16_t)(560 + ((((v * v) >> 9) * v >> 9) * 1900 >> 9));
                }
                for (int x = 0; x < 8; x++)
                    offsets[x] = (int16_t)ds[x];
                check(480, (int)((base >> 9) % 801u) - 400, (int)((base >> 10) % 257u), (si + ti) & 3);
            }
    /* All 65,536 output words, with every palette index uniquely selected. */
    for (int hi = 0; hi < 256; hi++) {
        for (int x = 0; x < 256; x++) {
            palette[x] = (uint16_t)((hi << 8) | x);
            srcA[x] = (int16_t)(160 + x * 16);
        }
        for (int x = 0; x < 8; x++)
            offsets[x] = 0;
        check(256, -160, 0, hi & 3);
    }
    if (!mismatches) {
        puts0("GM_QEMUBENCH_PIE: PASS mosaicRowAsm calls=");
        dec0(calls);
        puts0(" pixels=");
        dec0(pixels);
        puts0(" mismatches=0 (full ranges, tails, alignment, guards, parameter "
              "extremes, time wrap)\n");
    } else {
        puts0("GM_QEMUBENCH_PIE: FAIL mosaicRowAsm mismatches=");
        dec0(mismatches);
        puts0(" call=");
        dec0(firstCall);
        puts0(" lane=");
        dec0(firstLane);
        puts0(" got=");
        dec0(firstGot);
        puts0(" want=");
        dec0(firstWant);
        puts0("\n");
    }
    puts0("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {
    }
}
