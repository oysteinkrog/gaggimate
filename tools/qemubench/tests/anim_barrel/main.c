/* Barrel (bganim id 39) kernel equivalence test, harness mode.
 *
 * Runs barrelRowAsm, copied verbatim from
 * src/display/ui/default/bganim/AnimBarrel.cpp, against a plain C
 * reimplementation of that file's bandRef() inner loop, on synthetic rows
 * built to put every operand at its boundary. QEMU is the only place both
 * run: the host bench compiles the portable twin, the device runs the PIE
 * kernel, and nothing else executes ee.* instructions bit-exactly.
 *
 * What the inputs cover.
 *   Row phase r: 0, 1, 65535, 65536, 65537, 131071 and several interior
 *     values, which is the whole 17-bit range at both fold boundaries.
 *   Fold value v: each row is built so that v lands exactly on 0, 1, 65535,
 *     65536 (the crest, where min(v, 131072-v) has two equal arms), 65537
 *     and 131071, whatever r is, by storing the phase as (target - r) masked.
 *     That is the one case worth engineering an input for: a kernel that
 *     folded with a comparison instead of a minimum, or that masked to the
 *     wrong width, agrees everywhere except there.
 *   Shade: 0 (a column outside the cylinder), 1, 128, 254, 255, 202 and 346.
 *     The last two are the largest a column can hold at the two ends of Band
 *     depth, which scales the shade table so the band crest stays on one
 *     palette index; 346 needs nine bits in the packed word where 255 needed
 *     eight, so the kernel's shift and multiply are checked past the old
 *     byte boundary.
 *   Height base: 8738 (Band depth 50, the old fixed constant), 17476 (Band
 *     depth 0, the shallowest setting and the widest height domain) and 0
 *     (Band depth 100, where the troughs reach the palette floor). This is
 *     the third broadcast vector, and gm-3vj.42 made it a parameter, so the
 *     height it feeds the 16-bit multiply now reaches 42052 rather than
 *     33314. The product does not grow with it, because the shade scale
 *     moves the other way: the largest the animation can ask for is
 *     202 * 42052 = 8,494,504, against 255 * 33314 = 8,495,070 at the
 *     default. This sweep runs every shade against every height base, which
 *     is wider than the animation itself can reach, so it also covers the
 *     combination the bound argument rules out.
 *   Dither: the real init() range 20 to 24, and 0 and 255 in the wide row,
 *     so the 32-bit add of the dither is checked well past what the Bayer
 *     table can ask for.
 *   Geometry: 60 groups (the 480-pixel panel), 30 groups (the 240-pixel
 *     half-resolution path) and 1 group, the smallest trip count, which is
 *     where an off-by-one in the pointer walk shows up.
 *   Parameters: Bands 0 and 100 reach the kernel only as the phase table's
 *     multiplier, and Cylinder shade only as the palette contents, so both
 *     extremes are covered by sweeping the phase and shade columns above
 *     rather than by rebuilding a table this freestanding image cannot
 *     build. Speed reaches the kernel only as r, and Band tilt, Barrel
 *     width, Edge fade and Light angle only as r, the phase column and the
 *     shade column, all three of which are swept here.
 *
 * The palette is filled so that a one-index error is visible in the value
 * (i * 259 + 7, which is injective over 0..255 in 16 bits). It holds
 * PAL_N entries rather than the animation's 256 because the sweeps here go
 * deliberately past what the animation can ask for: the wide dither row
 * adds up to 252 and the widest shade and height are tried together even
 * though Band depth's scaling never puts them together, which reaches index
 * 474. Both the kernel and the reference would read the same address past a
 * 256-entry table and still agree, so the short table hid nothing, but a
 * test that reads its own array out of bounds is not worth keeping.
 *
 * Freestanding: no libc, no malloc, no constructors. CPENABLE is written
 * once by this main, never by the kernel, exactly as on the device, where
 * FreeRTOS enables PIE lazily per task.
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
static void uart_put_dec(int32_t v) {
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
static void uart_put_hex32(uint32_t v) {
    for (int i = 28; i >= 0; i -= 4) {
        const uint32_t d = (v >> i) & 0xFu;
        uart_putc((char)(d < 10 ? '0' + d : 'a' + d - 10));
    }
}

#define PERIOD 131072u
#define MASKV (PERIOD - 1u)
#define H_BASE_DEF 8738u
#define KERNEL_PX 8
#define MAXPX 480
#define VEC_WORDS 24
#define VEC_SCRATCH 16

/* ---- kernel under test, copied verbatim from AnimBarrel.cpp ---- */
__attribute__((noinline)) static void barrelRowAsm(uint16_t *out, const uint32_t *colp, const int32_t *dith,
                                                   const uint16_t *pal, uint32_t *cvec, int groups) {
    uint16_t *outp = out;
    const uint32_t *cp = colp;
    const int32_t *dp = dith;
    uint32_t *vp = cvec;
    int n = groups;
    int32_t t0, t1, t2, t3;
    asm volatile(
        "ee.vld.128.ip q0, %[vp], 16\n"
        "ee.vld.128.ip q1, %[vp], 16\n"
        "ee.vld.128.ip q2, %[vp], 16\n"
        "ee.vld.128.ip q3, %[vp], 16\n"
        "beqz %[n], 2f\n"
        "1:\n"
        "ee.vld.128.ip q4, %[dp], 16\n"
        "ee.vld.128.ip q5, %[cp], 16\n"
        "ee.vadds.s32 q6, q5, q3\n"
        "ee.andq q6, q6, q0\n"
        "ee.vsubs.s32 q7, q1, q6\n"
        "ee.vmin.s32 q6, q6, q7\n"
        "ee.vadds.s32 q7, q6, q6\n"
        "ee.vadds.s32 q6, q7, q6\n"
        "ssai 3\n"
        "ee.vsr.32 q6, q6\n"
        "ee.vadds.s32 q6, q6, q2\n"
        "ssai 17\n"
        "ee.vsr.32 q5, q5\n"
        "ssai 16\n"
        "ee.vmul.u16 q7, q5, q6\n"
        "ee.vadds.s32 q7, q7, q4\n"
        "ee.vst.128.ip q7, %[vp], 16\n"
        "ee.vld.128.ip q4, %[dp], -16\n"
        "ee.vld.128.ip q5, %[cp], 16\n"
        "ee.vadds.s32 q6, q5, q3\n"
        "ee.andq q6, q6, q0\n"
        "ee.vsubs.s32 q7, q1, q6\n"
        "ee.vmin.s32 q6, q6, q7\n"
        "ee.vadds.s32 q7, q6, q6\n"
        "ee.vadds.s32 q6, q7, q6\n"
        "ssai 3\n"
        "ee.vsr.32 q6, q6\n"
        "ee.vadds.s32 q6, q6, q2\n"
        "ssai 17\n"
        "ee.vsr.32 q5, q5\n"
        "ssai 16\n"
        "ee.vmul.u16 q7, q5, q6\n"
        "ee.vadds.s32 q7, q7, q4\n"
        "ee.vst.128.ip q7, %[vp], -16\n"
        "l32i %[t0], %[vp], 0\n"
        "l32i %[t1], %[vp], 4\n"
        "addx2 %[t0], %[t0], %[pal]\n"
        "addx2 %[t1], %[t1], %[pal]\n"
        "l16ui %[t0], %[t0], 0\n"
        "l16ui %[t1], %[t1], 0\n"
        "l32i %[t2], %[vp], 8\n"
        "l32i %[t3], %[vp], 12\n"
        "slli %[t1], %[t1], 16\n"
        "or %[t0], %[t0], %[t1]\n"
        "s32i %[t0], %[out], 0\n"
        "addx2 %[t2], %[t2], %[pal]\n"
        "addx2 %[t3], %[t3], %[pal]\n"
        "l16ui %[t2], %[t2], 0\n"
        "l16ui %[t3], %[t3], 0\n"
        "l32i %[t0], %[vp], 16\n"
        "l32i %[t1], %[vp], 20\n"
        "slli %[t3], %[t3], 16\n"
        "or %[t2], %[t2], %[t3]\n"
        "s32i %[t2], %[out], 4\n"
        "addx2 %[t0], %[t0], %[pal]\n"
        "addx2 %[t1], %[t1], %[pal]\n"
        "l16ui %[t0], %[t0], 0\n"
        "l16ui %[t1], %[t1], 0\n"
        "l32i %[t2], %[vp], 24\n"
        "l32i %[t3], %[vp], 28\n"
        "slli %[t1], %[t1], 16\n"
        "or %[t0], %[t0], %[t1]\n"
        "s32i %[t0], %[out], 8\n"
        "addx2 %[t2], %[t2], %[pal]\n"
        "addx2 %[t3], %[t3], %[pal]\n"
        "l16ui %[t2], %[t2], 0\n"
        "l16ui %[t3], %[t3], 0\n"
        "slli %[t3], %[t3], 16\n"
        "or %[t2], %[t2], %[t3]\n"
        "s32i %[t2], %[out], 12\n"
        "addi %[out], %[out], 16\n"
        "addi %[n], %[n], -1\n"
        "bnez %[n], 1b\n"
        "2:\n"
        : [out] "+r"(outp), [cp] "+r"(cp), [dp] "+r"(dp), [vp] "+r"(vp), [n] "+r"(n), [t0] "=&r"(t0),
          [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3)
        : [pal] "r"(pal)
        : "memory");
}

/* ---- reference: AnimBarrel.cpp's pixelRef, as plain C ---- */
static void barrelRowRef(uint16_t *out, const uint32_t *colp, const int32_t *dith, const uint16_t *pal, uint32_t r,
                         uint32_t hbase, int groups) {
    for (int x = 0; x < groups * KERNEL_PX; x++) {
        const uint32_t packed = colp[x];
        const uint32_t v = (packed + r) & MASKV;
        const uint32_t f = v <= (PERIOD >> 1) ? v : PERIOD - v;
        const uint32_t hh = hbase + ((f * 3u) >> 3);
        const int32_t i = (int32_t)(((packed >> 17) * hh) >> 16) + dith[x & 7];
        out[x] = pal[i];
    }
}

/* ---- fixtures ---- */
static uint32_t g_cvec[VEC_WORDS] __attribute__((aligned(16)));
static uint32_t g_col[MAXPX] __attribute__((aligned(16)));
static int32_t g_dith[8] __attribute__((aligned(16)));
#define PAL_N 512
static uint16_t g_pal[PAL_N] __attribute__((aligned(16)));
static uint16_t g_got[MAXPX] __attribute__((aligned(16)));
static uint16_t g_want[MAXPX] __attribute__((aligned(16)));

static const uint32_t g_rvals[] = {0u, 1u, 255u, 65535u, 65536u, 65537u, 98304u, 131071u, 12345u, 65024u};
#define NR ((int)(sizeof(g_rvals) / sizeof(g_rvals[0])))

/* Fold targets: the value v must take for the pixel, whatever r is. */
static const uint32_t g_targets[] = {0u, 1u, 2u, 7u, 65535u, 65536u, 65537u, 98304u, 131070u, 131071u, 32768u, 4u};
#define NT ((int)(sizeof(g_targets) / sizeof(g_targets[0])))

static const uint32_t g_shades[] = {255u, 0u, 1u, 254u, 128u, 42u, 200u, 7u, 202u, 346u, 300u};
#define NS ((int)(sizeof(g_shades) / sizeof(g_shades[0])))

/* Height ramp bases: Band depth 50 (the old constant), 0 and 100. */
static const uint32_t g_hbases[] = {H_BASE_DEF, 17476u, 0u};
#define NB ((int)(sizeof(g_hbases) / sizeof(g_hbases[0])))

static int g_bad = 0;
static int g_firstBadCall = -1;
static int g_firstBadPx = -1;
static uint32_t g_firstBadGot = 0;
static uint32_t g_firstBadWant = 0;
static int g_calls = 0;
static int g_pixels = 0;

static void runOne(int call, int groups, uint32_t r, uint32_t hbase, int ditherWide) {
    const int px = groups * KERNEL_PX;
    for (int i = 0; i < 4; i++) {
        g_cvec[i] = MASKV;
        g_cvec[4 + i] = PERIOD;
        g_cvec[8 + i] = hbase;
        g_cvec[12 + i] = r;
    }
    for (int i = VEC_SCRATCH; i < VEC_WORDS; i++) {
        g_cvec[i] = 0;
    }
    for (int x = 0; x < px; x++) {
        const uint32_t target = g_targets[(x + call) % NT];
        const uint32_t phase = (target - r) & MASKV;
        const uint32_t shade = g_shades[(x / 3 + call) % NS];
        g_col[x] = (shade << 17) | phase;
    }
    /* The narrow row is init()'s real range; the wide row goes well past it
     * so the dither add is checked beyond what the Bayer table can ask. */
    for (int k = 0; k < 8; k++) {
        g_dith[k] = ditherWide ? (int32_t)(k * 36) : (int32_t)(20 + (k % 5));
    }
    for (int x = 0; x < px; x++) {
        g_got[x] = 0xDEADu;
        g_want[x] = 0xBEEFu;
    }
    barrelRowRef(g_want, g_col, g_dith, g_pal, r, hbase, groups);
    barrelRowAsm(g_got, g_col, g_dith, g_pal, g_cvec, groups);
    g_calls++;
    g_pixels += px;
    for (int x = 0; x < px; x++) {
        if (g_got[x] != g_want[x]) {
            g_bad++;
            if (g_firstBadCall < 0) {
                g_firstBadCall = call;
                g_firstBadPx = x;
                g_firstBadGot = g_got[x];
                g_firstBadWant = g_want[x];
            }
        }
    }
    /* The kernel must not disturb the four broadcast vectors it loads: it
     * walks that pointer forward over them and then stores its scratch past
     * their end, so a wrong post-increment would land on the constants. */
    for (int i = 0; i < 4; i++) {
        if (g_cvec[i] != MASKV || g_cvec[4 + i] != PERIOD || g_cvec[8 + i] != hbase || g_cvec[12 + i] != r) {
            g_bad++;
            if (g_firstBadCall < 0) {
                g_firstBadCall = call;
                g_firstBadPx = -2; /* -2 marks a clobbered constant vector */
                g_firstBadGot = g_cvec[i];
                g_firstBadWant = MASKV;
            }
        }
    }
}

int main(void) {
    uint32_t cp = 8; /* CP3 = PIE, this bare-metal main only */
    asm volatile("wsr %0, cpenable\nrsync\n" : : "r"(cp) : "memory");

    for (int i = 0; i < PAL_N; i++) {
        g_pal[i] = (uint16_t)(i * 259 + 7);
    }

    int call = 0;
    for (int b = 0; b < NB; b++) {
        const uint32_t hb = g_hbases[b];
        for (int i = 0; i < NR; i++) {
            runOne(call++, 60, g_rvals[i], hb, 0); /* 480 px, the panel */
            runOne(call++, 30, g_rvals[i], hb, 0); /* 240 px, half resolution */
            runOne(call++, 1, g_rvals[i], hb, 0);  /* smallest trip count */
            runOne(call++, 60, g_rvals[i], hb, 1); /* wide dither */
        }
    }

    if (g_bad == 0) {
        uart_puts("GM_QEMUBENCH_PIE: PASS barrelRowAsm bit-exact vs its C reference over ");
        uart_put_dec(g_calls);
        uart_puts(" calls and ");
        uart_put_dec(g_pixels);
        uart_puts(" pixels (row phase 0/1/65535/65536/65537/131071 and interior, fold value driven "
                  "onto both arms of the 65536 crest, shade 0/1/128/254/255/202/300/346, "
                  "height base 8738/17476/0, dither 20..24 and 0..252, 60/30/1 groups)\n");
    } else {
        uart_puts("GM_QEMUBENCH_PIE: FAIL kernel=barrelRowAsm call=");
        uart_put_dec(g_firstBadCall);
        uart_puts(" px=");
        uart_put_dec(g_firstBadPx);
        uart_puts(" got=0x");
        uart_put_hex32(g_firstBadGot);
        uart_puts(" want=0x");
        uart_put_hex32(g_firstBadWant);
        uart_puts(" bad=");
        uart_put_dec(g_bad);
        uart_puts("\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {
        /* Spin so QEMU has a stable state; nothing to return to. */
    }
}
