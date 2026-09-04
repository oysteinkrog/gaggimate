/* Real-Xtensa execution check for nebulaFieldPie and nebulaGatherScalar --
 * the two hand-written Xtensa kernels that replace AnimNebula.cpp's
 * per-pixel combine loop (src/display/ui/default/bganim/AnimNebula.cpp,
 * band()'s new fast path). See nebulaFieldPie's header comment in that file
 * for the full derivation (the doubled-buffer rotation, the split
 * bTable/cTable/cDithTable layout, the register map) and
 * nebulaGatherScalar's for the gather's load-scheduling.
 *
 * This is not proven any other way: the host bench (tools/animbench) only
 * ever compiles AnimNebula.cpp's C++ path (band() dispatches to these
 * kernels only under __XTENSA__, which the host is not -- on host, band()
 * is bandRef, the untouched original), and xtensa-asm14 only proves the
 * instructions assemble and that GCC did not spill any register around the
 * asm blocks (confirmed separately: nebulaFieldPie 42 instructions after
 * round 2's wA/wB broadcast-table hoist moved the table build into the
 * caller, band() -- 64 in round 1, when this function built it itself on
 * every call -- nebulaGatherScalar 24, both q0-q7 / a2-a12 with no stack
 * spill inside either loop body) -- neither one actually EXECUTES the EE.SRC.Q /
 * EE.VMUL.S16 / SSAI / ADDX2 sequences. This test does, under Espressif's
 * qemu-system-xtensa fork.
 *
 * The two asm blocks below are transcribed by hand from AnimNebula.cpp
 * (mnemonics, operand names, immediates, load/store order all unchanged) --
 * not regenerated or simplified, matching anim_ember/main.c's precedent for
 * transcribing rather than re-deriving.
 *
 * No OS, no libc: prints over UART0 by writing its FIFO register directly
 * (ESP32-S3 UART0 base 0x60000000), same as anim_ember/blend_row/main.c.
 */
#include <stdint.h>

#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)

static void uart_putc(char c) {
    UART0_FIFO = (uint32_t)(uint8_t)c;
}

static void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') {
            uart_putc('\r');
        }
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
    if (v == 0) {
        buf[n++] = '0';
    }
    while (v > 0) {
        buf[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0) {
        uart_putc(buf[--n]);
    }
}

/* ===========================================================================
 * nebulaFieldPie: verbatim transcription from AnimNebula.cpp's asm block
 * (int16_t out, uint8_t aBase rotated at aOff, uint16_t bTab/cTab/cdTab,
 * n16 sixteen-pixel groups). Round 2 hoisted the wA/wB/ones broadcast
 * table out of this function into the caller (band() builds it once per
 * call instead of once per nebulaFieldPie call, 2x/row in round 1) -- this
 * test's callers below build a matching 24 x int16 table per case and pass
 * it in, same as AnimNebula.cpp's band() now does.
 * ==========================================================================*/
static void nebulaFieldPie(int16_t *out, const uint8_t *aBase, int aOff, const uint16_t *bTab, const uint16_t *cTab,
                            const uint16_t *cdTab, const int16_t *consts, int n16) {
    const uint8_t *pa = aBase + aOff;
    const uint16_t *pb = bTab;
    const uint16_t *pc = cTab;
    const uint16_t *pcd = cdTab;
    int16_t *po = out;
    const int16_t *pct = consts;
    int n = n16;
    __asm__ volatile("ee.ld.128.usar.ip q0, %[pa], 16\n"
                      "ee.vld.128.ip q1, %[ct], 16\n"
                      "ee.vld.128.ip q2, %[ct], 16\n"
                      "ee.vld.128.ip q3, %[ct], 16\n"
                      "1:\n"
                      "ee.vld.128.ip q4, %[pa], 16\n"
                      "ee.src.q q5, q0, q4\n"
                      "ee.orq q0, q4, q4\n"
                      "ee.zero.q q6\n"
                      "ee.vzip.8 q5, q6\n"
                      "ee.vld.128.ip q4, %[pb], 16\n"
                      "ee.vld.128.ip q7, %[pc], 16\n"
                      "ee.vsubs.s16 q5, q5, q7\n"
                      "ee.vsubs.s16 q4, q4, q7\n"
                      "ssai 0\n"
                      "ee.vmul.s16 q5, q5, q1\n"
                      "ee.vmul.s16 q4, q4, q2\n"
                      "ee.vadds.s16 q5, q5, q4\n"
                      "ssai 6\n"
                      "ee.vmul.s16 q5, q5, q3\n"
                      "ee.vld.128.ip q7, %[pcd], 16\n"
                      "ee.vadds.s16 q5, q5, q7\n"
                      "ee.vst.128.ip q5, %[po], 16\n"
                      "ee.vld.128.ip q4, %[pb], 16\n"
                      "ee.vld.128.ip q7, %[pc], 16\n"
                      "ee.vsubs.s16 q6, q6, q7\n"
                      "ee.vsubs.s16 q4, q4, q7\n"
                      "ssai 0\n"
                      "ee.vmul.s16 q6, q6, q1\n"
                      "ee.vmul.s16 q4, q4, q2\n"
                      "ee.vadds.s16 q6, q6, q4\n"
                      "ssai 6\n"
                      "ee.vmul.s16 q6, q6, q3\n"
                      "ee.vld.128.ip q7, %[pcd], 16\n"
                      "ee.vadds.s16 q6, q6, q7\n"
                      "ee.vst.128.ip q6, %[po], 16\n"
                      "addi %[n], %[n], -1\n"
                      "bnez %[n], 1b\n"
                      : [pa] "+r"(pa), [pb] "+r"(pb), [pc] "+r"(pc), [pcd] "+r"(pcd), [po] "+r"(po), [ct] "+r"(pct),
                        [n] "+r"(n)
                      :
                      : "memory");
}

/* Plain C reference, matching nebulaFieldPie's exact 16-bit-lane arithmetic
 * (multiply-then-truncate order, arithmetic shift), same as AnimNebula.cpp's
 * nebulaFieldRef used in the host-side self-tests. */
static void nebulaFieldRef(int16_t *out, const uint8_t *aBase, int aOff, const uint16_t *bTab, const uint16_t *cTab,
                            const uint16_t *cdTab, int wA, int wB, int n16) {
    for (int m = 0; m < n16 * 16; m++) {
        int16_t a = (int16_t)aBase[aOff + m];
        int16_t b = (int16_t)bTab[m];
        int16_t c = (int16_t)cTab[m];
        int16_t cd = (int16_t)cdTab[m];
        int16_t diffA = (int16_t)(a - c);
        int16_t diffB = (int16_t)(b - c);
        int16_t p1 = (int16_t)((int32_t)diffA * (int32_t)wA);
        int16_t p2 = (int16_t)((int32_t)diffB * (int32_t)wB);
        int16_t sum = (int16_t)(p1 + p2);
        int16_t shifted = (int16_t)(sum >> 6);
        out[m] = (int16_t)(cd + shifted);
    }
}

/* ===========================================================================
 * nebulaGatherScalar: verbatim transcription from AnimNebula.cpp's asm
 * block (row[m] = palette[idx[m]], four pixels interleaved, two s32i packs).
 * ==========================================================================*/
static void nebulaGatherScalar(uint16_t *row, const int16_t *idx, const uint16_t *palette, int n4) {
    const int16_t *pi = idx;
    uint16_t *pr = row;
    int n = n4;
    int s0, s1, s2, s3;
    __asm__ volatile("1:\n"
                      "l16si %[s0], %[pi], 0\n"
                      "l16si %[s1], %[pi], 2\n"
                      "l16si %[s2], %[pi], 4\n"
                      "l16si %[s3], %[pi], 6\n"
                      "addx2 %[s0], %[s0], %[pal]\n"
                      "addx2 %[s1], %[s1], %[pal]\n"
                      "addx2 %[s2], %[s2], %[pal]\n"
                      "addx2 %[s3], %[s3], %[pal]\n"
                      "l16ui %[s0], %[s0], 0\n"
                      "l16ui %[s1], %[s1], 0\n"
                      "l16ui %[s2], %[s2], 0\n"
                      "l16ui %[s3], %[s3], 0\n"
                      "slli %[s1], %[s1], 16\n"
                      "or %[s0], %[s0], %[s1]\n"
                      "slli %[s3], %[s3], 16\n"
                      "or %[s2], %[s2], %[s3]\n"
                      "s32i %[s0], %[pr], 0\n"
                      "s32i %[s2], %[pr], 4\n"
                      "addi %[pi], %[pi], 8\n"
                      "addi %[pr], %[pr], 8\n"
                      "addi %[n], %[n], -1\n"
                      "bnez %[n], 1b\n"
                      : [pi] "+r"(pi), [pr] "+r"(pr), [n] "+r"(n), [s0] "=&r"(s0), [s1] "=&r"(s1), [s2] "=&r"(s2),
                        [s3] "=&r"(s3)
                      : [pal] "r"(palette)
                      : "memory");
}

static void nebulaGatherScalarRef(uint16_t *row, const int16_t *idx, const uint16_t *palette, int n4) {
    for (int i = 0; i < n4 * 4; i++) {
        row[i] = palette[idx[i]];
    }
}

/* ===========================================================================
 * Test data and calls
 * ==========================================================================*/
static int g_mismatches = 0;
static int g_firstBadKernel = -1; /* 1 = field, 2 = gather */
static int g_firstBadCall = -1;
static int g_firstBadLane = -1;
static int g_firstBadGot = 0;
static int g_firstBadWant = 0;

static void checkField(int callId, const int16_t *got, const int16_t *want, int n) {
    for (int i = 0; i < n; i++) {
        if (got[i] != want[i]) {
            if (g_mismatches == 0) {
                g_firstBadKernel = 1;
                g_firstBadCall = callId;
                g_firstBadLane = i;
                g_firstBadGot = got[i];
                g_firstBadWant = want[i];
            }
            g_mismatches++;
        }
    }
}

static void checkGather(int callId, const uint16_t *got, const uint16_t *want, int n) {
    for (int i = 0; i < n; i++) {
        if (got[i] != want[i]) {
            if (g_mismatches == 0) {
                g_firstBadKernel = 2;
                g_firstBadCall = callId;
                g_firstBadLane = i;
                g_firstBadGot = got[i];
                g_firstBadWant = want[i];
            }
            g_mismatches++;
        }
    }
}

/* Field call 1: addressing/rotation boundary. aBase is a 512-byte doubled
 * ramp (bytes 0-255 distinct, 256-511 a copy of 0-255 -- the exact scheme
 * band() builds every row via memcpy). wA=64, wB=0, cTab=cdTab=0 make
 * v == a exactly (same isolation trick as AnimNebula.cpp's
 * nebulaFieldAddrSelfTest), so any wrong output is purely an addressing
 * bug. aOff=241: block-aligned start is 240 (241&~15), SAR_BYTE=1, and the
 * very first 16-pixel group already straddles the real/copied-tail boundary
 * at index 255->256 -- the trickiest single point in the whole scheme.
 * n16=8: a full 128-pixel bcTable-period sweep, exactly how band() calls
 * this for the first half of its combine loop at both real widths (240 and
 * 480). */
#define F1_N16 8
#define F1_N (F1_N16 * 16)
static uint8_t g_f1_aBase[512] __attribute__((aligned(16)));
static uint16_t g_f1_b[F1_N] __attribute__((aligned(16)));
static uint16_t g_f1_c[F1_N] __attribute__((aligned(16)));
static uint16_t g_f1_cd[F1_N] __attribute__((aligned(16)));
static int16_t g_f1_got[F1_N] __attribute__((aligned(16)));
static int16_t g_f1_want[F1_N] __attribute__((aligned(16)));

static void fillField1(void) {
    for (int i = 0; i < 256; i++) {
        g_f1_aBase[i] = (uint8_t)i;
        g_f1_aBase[256 + i] = (uint8_t)i;
    }
    for (int k = 0; k < F1_N; k++) {
        g_f1_b[k] = 0;
        g_f1_c[k] = 0;
        g_f1_cd[k] = 0;
    }
}

/* Field call 2: production-shaped, both real-device wA/wB extremes plus a
 * densOff swing, varied a/b/c per lane, aOff=173 (not 16-aligned: SAR_BYTE
 * = 13). n16=8, matching band()'s first-half call exactly (m=0..127,
 * x0start = axI&255). wA=38/wB=16 is turbulence=0 (frame()'s
 * g_wA/g_wB formula); densOff=-55 is density=0's extreme (frame():
 * densOff = (p[1]-50)*1.1, p[1] in [0,100]). */
#define F2_N16 8
#define F2_N (F2_N16 * 16)
static uint8_t g_f2_aBase[512] __attribute__((aligned(16)));
static uint16_t g_f2_b[F2_N] __attribute__((aligned(16)));
static uint16_t g_f2_c[F2_N] __attribute__((aligned(16)));
static uint16_t g_f2_cd[F2_N] __attribute__((aligned(16)));
static int16_t g_f2_got[F2_N] __attribute__((aligned(16)));
static int16_t g_f2_want[F2_N] __attribute__((aligned(16)));

static void fillField2(void) {
    for (int i = 0; i < 256; i++) {
        g_f2_aBase[i] = (uint8_t)((i * 61 + 7) & 0xFF);
        g_f2_aBase[256 + i] = g_f2_aBase[i];
    }
    for (int k = 0; k < F2_N; k++) {
        g_f2_b[k] = (uint16_t)((k * 37 + 3) & 0xFF);
        int c = (k * 53 + 11) & 0xFF;
        g_f2_c[k] = (uint16_t)c;
        int bayer = (k * 11) & 63;
        int dith2 = (bayer - 31) / 4 - 55; /* densOff = -55 */
        g_f2_cd[k] = (uint16_t)(c + dith2);
    }
}

/* Field call 3: production-shaped second half, mirroring band()'s actual
 * two-call split at w=480 (totalM=256): x0start=(axI+128)&255 with
 * axI=173, i.e. aOff=45. wA=28/wB=19 (turbulence=100), densOff=+55
 * (density=100). n16=8. */
#define F3_N16 8
#define F3_N (F3_N16 * 16)
static uint8_t g_f3_aBase[512] __attribute__((aligned(16)));
static uint16_t g_f3_b[F3_N] __attribute__((aligned(16)));
static uint16_t g_f3_c[F3_N] __attribute__((aligned(16)));
static uint16_t g_f3_cd[F3_N] __attribute__((aligned(16)));
static int16_t g_f3_got[F3_N] __attribute__((aligned(16)));
static int16_t g_f3_want[F3_N] __attribute__((aligned(16)));

static void fillField3(void) {
    for (int i = 0; i < 256; i++) {
        g_f3_aBase[i] = (uint8_t)((i * 197 + 41) & 0xFF);
        g_f3_aBase[256 + i] = g_f3_aBase[i];
    }
    for (int k = 0; k < F3_N; k++) {
        g_f3_b[k] = (uint16_t)((k * 89 + 5) & 0xFF);
        int c = (k * 131 + 23) & 0xFF;
        g_f3_c[k] = (uint16_t)c;
        int bayer = (k * 29) & 63;
        int dith2 = (bayer - 31) / 4 + 55; /* densOff = +55 */
        g_f3_cd[k] = (uint16_t)(c + dith2);
    }
}

/* Field call 4: minimal trip count (n16=1, sixteen pixels) at the other
 * bcTable-split length band() actually issues -- w=240's second half is
 * n16=7 (112 pixels), so this covers n16=1 as the true minimum and proves
 * the loop's smallest legal trip count is not mishandled, same reasoning as
 * anim_ember's call 3. aOff=255: the single largest legal offset, forcing
 * SAR_BYTE=15 (the widest possible unaligned shift) on the very first
 * group. */
#define F4_N16 1
#define F4_N (F4_N16 * 16)
static uint8_t g_f4_aBase[512] __attribute__((aligned(16)));
static uint16_t g_f4_b[F4_N] __attribute__((aligned(16)));
static uint16_t g_f4_c[F4_N] __attribute__((aligned(16)));
static uint16_t g_f4_cd[F4_N] __attribute__((aligned(16)));
static int16_t g_f4_got[F4_N] __attribute__((aligned(16)));
static int16_t g_f4_want[F4_N] __attribute__((aligned(16)));

static void fillField4(void) {
    for (int i = 0; i < 256; i++) {
        g_f4_aBase[i] = (uint8_t)(255 - i);
        g_f4_aBase[256 + i] = g_f4_aBase[i];
    }
    for (int k = 0; k < F4_N; k++) {
        g_f4_b[k] = (uint16_t)(k * 3);
        g_f4_c[k] = (uint16_t)(k * 5);
        g_f4_cd[k] = (uint16_t)(k * 5 + 4);
    }
}

/* Gather call 1: full proven index range plus margin. PAD=72 in
 * AnimNebula.cpp (the palette clamp-padding proof in that file's header)
 * makes palette[v] valid for v in [-72, 327]; this sweeps exactly that
 * range, forcing both endpoints, over 100 pixels (n4=25). palette is a ramp
 * (distinct, checkable values) offset so palette[-72] reads the buffer's
 * first entry, matching AnimNebula.cpp's `palette = paletteExt + PAD`
 * layout. */
#define PAL_PAD 72
#define PAL_N (2 * PAL_PAD + 256)
static uint16_t g_palBuf[PAL_N];
#define G1_N4 25
#define G1_N (G1_N4 * 4)
static int16_t g_g1_idx[G1_N];
static uint16_t g_g1_got[G1_N], g_g1_want[G1_N];

static void fillPalette(void) {
    for (int i = 0; i < PAL_N; i++) {
        g_palBuf[i] = (uint16_t)(0xC000 + i); /* distinctive, checkable */
    }
}

static void fillGather1(const uint16_t *palette) {
    (void)palette;
    for (int i = 0; i < G1_N; i++) {
        int v = -PAL_PAD + (i * 397) % (327 - (-PAL_PAD) + 1);
        g_g1_idx[i] = (int16_t)v;
    }
    g_g1_idx[0] = -PAL_PAD;
    g_g1_idx[1] = 327;
    g_g1_idx[G1_N - 2] = -PAL_PAD;
    g_g1_idx[G1_N - 1] = 327;
}

/* Gather call 2: production-shaped widths band() actually issues (60 for
 * w=240, 64 for w=480), values clustered near the empirically-observed
 * range [-62,318] from AnimNebula.cpp's header comment. */
#define G2_N4 64
#define G2_N (G2_N4 * 4)
static int16_t g_g2_idx[G2_N];
static uint16_t g_g2_got[G2_N], g_g2_want[G2_N];

static void fillGather2(void) {
    for (int i = 0; i < G2_N; i++) {
        g_g2_idx[i] = (int16_t)(-62 + (i * 71) % (318 - (-62) + 1));
    }
}

/* Gather call 3: minimal trip count (n4=1, four pixels). */
static int16_t g_g3_idx[4] = {-72, 0, 200, 327};
static uint16_t g_g3_got[4], g_g3_want[4];

/* Per-case wA/wB/ones broadcast tables -- the caller now builds these
 * (band() builds one per band() call; here, one per test case), matching
 * the round-2 hoist. Aligned(16): read via ee.vld.128.ip. */
static const int16_t g_f1_consts[24] __attribute__((aligned(16))) = {64, 64, 64, 64, 64, 64, 64, 64, 0,  0,  0,  0,
                                                                       0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1};
static const int16_t g_f2_consts[24] __attribute__((aligned(16))) = {
    38, 38, 38, 38, 38, 38, 38, 38, 16, 16, 16, 16, 16, 16, 16, 16, 1, 1, 1, 1, 1, 1, 1, 1};
static const int16_t g_f3_consts[24] __attribute__((aligned(16))) = {
    28, 28, 28, 28, 28, 28, 28, 28, 19, 19, 19, 19, 19, 19, 19, 19, 1, 1, 1, 1, 1, 1, 1, 1};
static const int16_t g_f4_consts[24] __attribute__((aligned(16))) = {
    40, 40, 40, 40, 40, 40, 40, 40, 24, 24, 24, 24, 24, 24, 24, 24, 1, 1, 1, 1, 1, 1, 1, 1};

int main(void) {
    fillField1();
    fillField2();
    fillField3();
    fillField4();
    fillPalette();
    const uint16_t *palette = g_palBuf + PAL_PAD;
    fillGather1(palette);
    fillGather2();

    /* Field kernel */
    nebulaFieldPie(g_f1_got, g_f1_aBase, 241, g_f1_b, g_f1_c, g_f1_cd, g_f1_consts, F1_N16);
    nebulaFieldRef(g_f1_want, g_f1_aBase, 241, g_f1_b, g_f1_c, g_f1_cd, 64, 0, F1_N16);
    checkField(1, g_f1_got, g_f1_want, F1_N);

    nebulaFieldPie(g_f2_got, g_f2_aBase, 173, g_f2_b, g_f2_c, g_f2_cd, g_f2_consts, F2_N16);
    nebulaFieldRef(g_f2_want, g_f2_aBase, 173, g_f2_b, g_f2_c, g_f2_cd, 38, 16, F2_N16);
    checkField(2, g_f2_got, g_f2_want, F2_N);

    nebulaFieldPie(g_f3_got, g_f3_aBase, 45, g_f3_b, g_f3_c, g_f3_cd, g_f3_consts, F3_N16);
    nebulaFieldRef(g_f3_want, g_f3_aBase, 45, g_f3_b, g_f3_c, g_f3_cd, 28, 19, F3_N16);
    checkField(3, g_f3_got, g_f3_want, F3_N);

    nebulaFieldPie(g_f4_got, g_f4_aBase, 255, g_f4_b, g_f4_c, g_f4_cd, g_f4_consts, F4_N16);
    nebulaFieldRef(g_f4_want, g_f4_aBase, 255, g_f4_b, g_f4_c, g_f4_cd, 40, 24, F4_N16);
    checkField(4, g_f4_got, g_f4_want, F4_N);

    /* Gather kernel */
    nebulaGatherScalar(g_g1_got, g_g1_idx, palette, G1_N4);
    nebulaGatherScalarRef(g_g1_want, g_g1_idx, palette, G1_N4);
    checkGather(5, g_g1_got, g_g1_want, G1_N);

    nebulaGatherScalar(g_g2_got, g_g2_idx, palette, G2_N4);
    nebulaGatherScalarRef(g_g2_want, g_g2_idx, palette, G2_N4);
    checkGather(6, g_g2_got, g_g2_want, G2_N);

    nebulaGatherScalar(g_g3_got, g_g3_idx, palette, 1);
    nebulaGatherScalarRef(g_g3_want, g_g3_idx, palette, 1);
    checkGather(7, g_g3_got, g_g3_want, 4);

    uart_puts("GM_QEMUBENCH_ANIM: nebulaFieldPie+nebulaGatherScalar mismatches=");
    uart_put_dec(g_mismatches);
    uart_puts(" (of ");
    uart_put_dec(F1_N + F2_N + F3_N + F4_N + G1_N + G2_N + 4);
    uart_puts(" lanes across 4 field calls [addressing-boundary aOff=241, "
              "production wA/wB/densOff extremes x2, min-trip aOff=255] + "
              "3 gather calls [full PAD range, production widths 60/64, min-trip])\n");

    if (g_mismatches == 0) {
        uart_puts("GM_QEMUBENCH_ANIM: PASS nebulaFieldPie and nebulaGatherScalar bit-exact vs "
                  "nebulaFieldRef/nebulaGatherScalarRef (addressing boundary at the doubled-buffer "
                  "real/copy seam, both real per-frame wA/wB/densOff extremes, full PAD=72 palette "
                  "index range, minimal trip counts)\n");
        /* Also emit the PIE-prefixed marker run.sh's exit-code grep actually
         * looks for (see tools/qemubench/run.sh) -- ASM_BRIEF.md's own
         * "GM_QEMUBENCH_ANIM: PASS" wording predates that grep (see
         * anim_fireflies/main.c's identical note), so both are printed
         * rather than picking one and breaking the other. */
        uart_puts("GM_QEMUBENCH_PIE: PASS nebulaFieldPie+nebulaGatherScalar bit-exact vs reference\n");
    } else {
        uart_puts("GM_QEMUBENCH_ANIM: FAIL kernel=");
        uart_put_dec(g_firstBadKernel);
        uart_puts(" call=");
        uart_put_dec(g_firstBadCall);
        uart_puts(" lane=");
        uart_put_dec(g_firstBadLane);
        uart_puts(" got=");
        uart_put_dec(g_firstBadGot);
        uart_puts(" want=");
        uart_put_dec(g_firstBadWant);
        uart_puts("\n");
        uart_puts("GM_QEMUBENCH_PIE: FAIL nebula kernels mismatched\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");

    for (;;) {
        /* Spin so QEMU has a stable state; nothing to return to. */
    }
}
