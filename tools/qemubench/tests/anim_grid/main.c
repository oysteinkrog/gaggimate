/* Real-Xtensa execution check for the two hand-written kernels in
 * src/display/ui/default/bganim/AnimGrid.cpp, gridFillRowAsm and
 * gridLineRowAsm (both on by default behind GM_BGANIM_GRID_ASM).
 *
 * Neither of the other rungs executes them. tools/animbench/xtensa-asm14.sh
 * proves the mnemonics assemble and that GCC can allocate the registers,
 * never that the instructions compute the right thing; the host bench
 * compiles the portable twins instead, because band() only dispatches to
 * assembly under __XTENSA__. This test runs the real EE.VLD.128.IP /
 * EE.VST.128.IP / MIN / MAX / MULL / L8UI sequences under Espressif's
 * qemu-system-xtensa fork, following the precedent of
 * tests/anim_weave/main.c and tests/anim_silk2/main.c: the kernel bodies
 * below are verbatim transcriptions of the ones in AnimGrid.cpp (mnemonics,
 * operand names, immediates, constraint lists and instruction order all
 * unchanged) and the C references reimplement the portable twins from the
 * same file, so a PASS is direct evidence the two agree bit for bit on real
 * hardware instructions for every case below.
 *
 * What the kernel claims, and therefore what this has to prove. The picture
 * only needs the full per-pixel chain where a pixel is on a line. Off both
 * lines the palette index is bg + ((PROF[g]*amp)>>8) + dither[x&7], eight
 * fixed values for the whole row, so a run of eight such pixels is one
 * 128-bit store. The kernel decides per eight-pixel block with one test,
 * fold >= lwPlus, where fold is min(wv, 32768-wv) of the block's first pixel
 * and lwPlus = lw + ((7*du)>>8) + 1 bounds how far fold can travel across
 * the block. The test must be conservative, never optimistic: a block that
 * fails it falls through to the full chain for all eight pixels. So the
 * cases below hammer the boundary itself (fold exactly at lwPlus, one below,
 * one above), the fold triangle's corners (wv 0, 16384, 32767), the u
 * accumulator wrapping through 2^32, du of 0 and of values large enough that
 * lwPlus saturates and every block goes slow, and hv exactly 0 and negative
 * (where the kernel drops bandRef's max(hv,0) and relies on MULL plus SRAI
 * giving something the MAX against g discards).
 *
 * Guard halfwords sit immediately before and after every output row and are
 * checked after each call. EE.VST.128.IP masks the low four address bits
 * silently instead of trapping, so an alignment mistake would corrupt a
 * neighbour rather than fault, and the guards are what would catch it.
 *
 * CPENABLE is written once by this bare-metal main, never by a kernel: both
 * kernels use CP3 (PIE) and on real hardware FreeRTOS enables it lazily per
 * task through the coprocessor-disabled exception, which is also how another
 * task's PIE state gets saved. See CLAUDE.md's "Animation kernels" section on
 * why a production kernel must never write it.
 *
 * No OS, no libc: prints over UART0 by writing its FIFO register directly
 * (ESP32-S3 UART0 base 0x60000000), same as every other test here.
 */
#include <stdint.h>

#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)

static void uart_putc(char c) { UART0_FIFO = (uint32_t)(uint8_t)c; }

static void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s++);
    }
}

static void uart_put_hex32(uint32_t v) {
    static const char hex[] = "0123456789abcdef";
    for (int shift = 28; shift >= 0; shift -= 4) {
        uart_putc(hex[(v >> shift) & 0xF]);
    }
}

static void uart_put_dec(long v) {
    if (v < 0) {
        uart_putc('-');
        v = -v;
    }
    char buf[24];
    int n = 0;
    if (v == 0) {
        buf[n++] = '0';
    }
    while (v > 0) {
        buf[n++] = (char)('0' + (int)(v % 10));
        v /= 10;
    }
    while (n > 0) {
        uart_putc(buf[--n]);
    }
}

/* ------------------------------------------------------------------ */
/* The per-row constant block, byte for byte as AnimGrid.cpp declares it.
 * The kernel addresses lwPlus, dstEnd and pat by these offsets, so the
 * layout is part of the contract under test. */
typedef struct __attribute__((aligned(16))) {
    uint16_t bgd[8];  /* 0  */
    uint16_t lwPlus;  /* 16 */
    uint16_t nBlocks; /* 18 */
    uint32_t dstEnd;  /* 20 */
    uint32_t pad[2];  /* 24 */
    uint16_t pat[8];  /* 32 */
} GridRowAsm;

_Static_assert(sizeof(GridRowAsm) == 48, "GridRowAsm size");
_Static_assert(__builtin_offsetof(GridRowAsm, lwPlus) == 16, "lwPlus offset");
_Static_assert(__builtin_offsetof(GridRowAsm, nBlocks) == 18, "nBlocks offset");
_Static_assert(__builtin_offsetof(GridRowAsm, dstEnd) == 20, "dstEnd offset");
_Static_assert(__builtin_offsetof(GridRowAsm, pat) == 32, "pat offset");

/* ------------------------------------------------------------------ */
/* gridFillRowAsm: verbatim transcription from AnimGrid.cpp. */
static void gridFillRowAsm(uint16_t *dst, const uint16_t *pat, int nBlocks) {
    uint16_t *dstp = dst;
    const uint16_t *patp = pat;
    __asm__ volatile("ee.vld.128.ip q0, %[pat], 0\n"
                     "loopnez %[n], 1f\n"
                     "ee.vst.128.ip q0, %[dst], 16\n"
                     "1:\n"
                     : [dst] "+r"(dstp), [pat] "+r"(patp)
                     : [n] "r"(nBlocks)
                     : "memory");
}

/* gridLineRowAsm: verbatim transcription from AnimGrid.cpp, including the
 * GM_GRID_PIXEL macro it builds its eight-pixel slow path from. */
#define GM_GRID_PIXEL(OFF)                                                                                   \
    "extui  %[t0], %[u], 8, 15\n"                                                                            \
    "neg    %[t1], %[t0]\n"                                                                                  \
    "extui  %[t1], %[t1], 0, 15\n"                                                                           \
    "min    %[t0], %[t0], %[t1]\n"                                                                           \
    "sub    %[t0], %[lw], %[t0]\n"                                                                            \
    "mull   %[t0], %[t0], %[kv]\n"                                                                           \
    "l16ui  %[t2], %[rowp], " OFF "\n"                                                                       \
    "srai   %[t0], %[t0], 16\n"                                                                              \
    "max    %[t0], %[t0], %[g]\n"                                                                            \
    "add    %[t1], %[prof], %[t0]\n"                                                                         \
    "l8ui   %[t1], %[t1], 0\n"                                                                               \
    "add    %[u], %[u], %[du]\n"                                                                             \
    "mull   %[t1], %[t1], %[amp]\n"                                                                          \
    "srai   %[t1], %[t1], 8\n"                                                                               \
    "add    %[t1], %[t1], %[t2]\n"                                                                           \
    "addx2  %[t1], %[t1], %[pal]\n"                                                                          \
    "l16ui  %[t1], %[t1], 0\n"                                                                               \
    "s16i   %[t1], %[dst], " OFF "\n"

static void gridLineRowAsm(uint16_t *dst, uint32_t u, uint32_t du, int lw, int kv, int g, int amp,
                           const uint8_t *prof, const uint16_t *pal, const GridRowAsm *rowp) {
    uint16_t *dstp = dst;
    uint32_t up = u;
    int32_t t0, t1, t2;
    __asm__ volatile("addi   %[t0], %[rowp], 32\n"
                     "ee.vld.128.ip q0, %[t0], 0\n"
                     "2:\n"
                     "extui  %[t0], %[u], 8, 15\n"
                     "neg    %[t1], %[t0]\n"
                     "extui  %[t1], %[t1], 0, 15\n"
                     "min    %[t0], %[t0], %[t1]\n"
                     "l16ui  %[t1], %[rowp], 16\n"
                     "bge    %[t0], %[t1], 3f\n"
                     "j      5f\n"
                     "3:\n"
                     "ee.vst.128.ip q0, %[dst], 16\n"
                     "addx8  %[u], %[du], %[u]\n"
                     "4:\n"
                     "l32i   %[t1], %[rowp], 20\n"
                     "bltu   %[dst], %[t1], 2b\n"
                     "j      6f\n"
                     "5:\n" GM_GRID_PIXEL("0") GM_GRID_PIXEL("2") GM_GRID_PIXEL("4") GM_GRID_PIXEL("6")
                         GM_GRID_PIXEL("8") GM_GRID_PIXEL("10") GM_GRID_PIXEL("12") GM_GRID_PIXEL("14")
                     "addi   %[dst], %[dst], 16\n"
                     "j      4b\n"
                     "6:\n"
                     : [dst] "+r"(dstp), [u] "+r"(up), [t0] "=&r"(t0), [t1] "=&r"(t1), [t2] "=&r"(t2)
                     : [du] "r"(du), [lw] "r"(lw), [kv] "r"(kv), [g] "r"(g), [amp] "r"(amp),
                       [prof] "r"(prof), [pal] "r"(pal), [rowp] "r"(rowp)
                     : "memory");
}

/* ------------------------------------------------------------------ */
/* Plain-C references: AnimGrid.cpp's portable twins, same block dispatch
 * and same per-pixel arithmetic. */
static void gridFillRowRef(uint16_t *dst, const uint16_t *pat, int nBlocks) {
    for (int b = 0; b < nBlocks; b++) {
        for (int k = 0; k < 8; k++) {
            dst[k] = pat[k];
        }
        dst += 8;
    }
}

static void gridLineRowRef(uint16_t *dst, uint32_t u, uint32_t du, int lw, int kv, int g, int amp,
                           const uint8_t *prof, const uint16_t *pal, const GridRowAsm *rowp) {
    for (int b = 0; b < rowp->nBlocks; b++) {
        const int wv0 = (int)((u >> 8) & 32767);
        const int back0 = (-wv0) & 32767;
        const int fold0 = wv0 < back0 ? wv0 : back0;
        if (fold0 >= rowp->lwPlus) {
            for (int k = 0; k < 8; k++) {
                dst[k] = rowp->pat[k];
            }
            u += du * 8u;
            dst += 8;
            continue;
        }
        for (int k = 0; k < 8; k++) {
            const int wv = (int)((u >> 8) & 32767);
            const int back = (-wv) & 32767;
            const int fold = wv < back ? wv : back;
            int n = ((lw - fold) * kv) >> 16;
            if (n < g) {
                n = g;
            }
            const int i = rowp->bgd[k] + ((prof[n] * amp) >> 8);
            dst[k] = pal[i];
            u += du;
        }
        dst += 8;
    }
}

/* ------------------------------------------------------------------ */
/* An independent second reference for the slow path: bandRef()'s own
 * arithmetic, written the way that function writes it (max(hv,0) before the
 * reciprocal, then max(n,g)), rather than the twin's shortcut. Checking the
 * kernel against both is what proves the shortcut is not just consistent
 * with the twin but equal to the spec bandRef defines. Only meaningful for a
 * row the block test sends entirely to the slow path, so it is applied to
 * the whole row and compared per pixel wherever the twin took the slow
 * branch. */
static void gridPixelSpec(uint16_t *dst, uint32_t u, uint32_t du, int lw, int kv, int g, int amp,
                          const uint8_t *prof, const uint16_t *pal, const GridRowAsm *rowp, int nPix) {
    for (int x = 0; x < nPix; x++) {
        const int wv = (int)((u >> 8) & 32767);
        const int back = (-wv) & 32767;
        const int fold = wv < back ? wv : back;
        const int hv = lw - fold;
        const int positive = hv > 0 ? hv : 0;
        int n = (positive * kv) >> 16;
        if (g > n) {
            n = g;
        }
        dst[x] = pal[rowp->bgd[x & 7] + ((prof[n] * amp) >> 8)];
        u += du;
    }
}

/* ------------------------------------------------------------------ */
static long g_calls = 0, g_pixels = 0, g_mismatches = 0, g_guardBad = 0, g_specBad = 0;
static long g_fastBlocks = 0, g_slowBlocks = 0;
static int g_firstBadCall = -1, g_firstBadIdx = 0;
static uint32_t g_firstBadGot = 0, g_firstBadWant = 0;

static void note_bad(int idx, uint32_t got, uint32_t want) {
    if (g_mismatches == 0) {
        g_firstBadCall = (int)g_calls;
        g_firstBadIdx = idx;
        g_firstBadGot = got;
        g_firstBadWant = want;
    }
    g_mismatches++;
}

/* Distinguishable tables: a wrong index reads a value no correct index
 * could. prof is an affine ramp mod 256 with an odd stride, pal likewise, so
 * neighbouring indices never collide. */
#define PAL_N 2048
static uint8_t g_prof[256];
static uint16_t g_pal[PAL_N];

#define MAXPIX 512
/* The row starts 8 halfwords in, so it is 16-byte aligned the way the
 * panel's band buffers are, which is what EE.VST.128.IP requires (it masks
 * the low four address bits silently rather than trapping). The two guard
 * halfwords on each side sit immediately outside the row, and because the
 * row length is a multiple of 8 the trailing pair is itself the next
 * 16-byte slot, so a single stray block store lands on them. */
#define ROWOFF 8
static uint16_t g_outStore[MAXPIX + 2 * ROWOFF] __attribute__((aligned(16)));
static uint16_t g_refStore[MAXPIX + 2 * ROWOFF] __attribute__((aligned(16)));
static uint16_t g_specStore[MAXPIX + 2 * ROWOFF] __attribute__((aligned(16)));

static uint32_t g_rngState = 0x13579bdfu;
static uint32_t rng(void) {
    g_rngState = g_rngState * 1664525u + 1013904223u;
    return g_rngState;
}

/* One kernel call, both paths, guards checked. bgdBase picks the eight
 * background indices; every value stays small enough that the palette index
 * bgd + ((prof*amp)>>8) is inside PAL_N whatever the kernel computes. */
static void check(uint32_t u, uint32_t du, int lw, int kv, int g, int amp, int nBlocks, int bgdBase,
                  int patBase) {
    const int nPix = nBlocks * 8;
    if (nPix > MAXPIX) {
        return;
    }
    GridRowAsm rowp __attribute__((aligned(16)));
    for (int k = 0; k < 8; k++) {
        rowp.bgd[k] = (uint16_t)(bgdBase + k * 3);
        rowp.pat[k] = (uint16_t)(patBase + k * 7 + 1);
    }
    rowp.nBlocks = (uint16_t)nBlocks;
    int lwPlus = lw + (int)((7u * du) >> 8) + 1;
    if (lwPlus > 32767) {
        lwPlus = 32767;
    }
    rowp.lwPlus = (uint16_t)lwPlus;

    uint16_t *out = g_outStore + ROWOFF;
    uint16_t *ref = g_refStore + ROWOFF;
    uint16_t *spec = g_specStore + ROWOFF;
    rowp.dstEnd = (uint32_t)(uintptr_t)(out + nPix);

    g_outStore[ROWOFF - 2] = 0x71c3;
    g_outStore[ROWOFF - 1] = 0x9a5e;
    g_outStore[ROWOFF + nPix] = 0x2d84;
    g_outStore[ROWOFF + nPix + 1] = 0xb607;
    for (int i = 0; i < nPix; i++) {
        out[i] = 0xbaad;
        ref[i] = 0xbaad;
    }

    gridLineRowAsm(out, u, du, lw, kv, g, amp, g_prof, g_pal, &rowp);
    gridLineRowRef(ref, u, du, lw, kv, g, amp, g_prof, g_pal, &rowp);
    for (int i = 0; i < nPix; i++) {
        if (out[i] != ref[i]) {
            note_bad(i, out[i], ref[i]);
        }
    }
    if (g_outStore[ROWOFF - 2] != 0x71c3 || g_outStore[ROWOFF - 1] != 0x9a5e ||
        g_outStore[ROWOFF + nPix] != 0x2d84 || g_outStore[ROWOFF + nPix + 1] != 0xb607) {
        g_guardBad++;
    }

    /* Where the block test went slow, the result must also equal bandRef's
     * own spec arithmetic. Recomputed per block so a fast block, whose
     * pixels the spec would compute the same way anyway, is skipped rather
     * than assumed. */
    gridPixelSpec(spec, u, du, lw, kv, g, amp, g_prof, g_pal, &rowp, nPix);
    uint32_t uu = u;
    for (int b = 0; b < nBlocks; b++) {
        const int wv0 = (int)((uu >> 8) & 32767);
        const int back0 = (-wv0) & 32767;
        const int fold0 = wv0 < back0 ? wv0 : back0;
        if (fold0 >= rowp.lwPlus) {
            g_fastBlocks++;
        } else {
            g_slowBlocks++;
            for (int k = 0; k < 8; k++) {
                if (out[b * 8 + k] != spec[b * 8 + k]) {
                    g_specBad++;
                }
            }
        }
        uu += du * 8u;
    }

    g_calls++;
    g_pixels += nPix;
}

/* One fill-kernel call, guards checked. */
static void checkFill(int nBlocks, int patBase) {
    const int nPix = nBlocks * 8;
    if (nPix > MAXPIX) {
        return;
    }
    GridRowAsm rowp __attribute__((aligned(16)));
    for (int k = 0; k < 8; k++) {
        rowp.pat[k] = (uint16_t)(patBase + k * 11 + 3);
    }
    uint16_t *out = g_outStore + ROWOFF;
    uint16_t *ref = g_refStore + ROWOFF;
    g_outStore[ROWOFF - 2] = 0x4f11;
    g_outStore[ROWOFF - 1] = 0xc0de;
    g_outStore[ROWOFF + nPix] = 0x8ace;
    g_outStore[ROWOFF + nPix + 1] = 0x1357;
    for (int i = 0; i < nPix; i++) {
        out[i] = 0xbaad;
        ref[i] = 0xbaad;
    }
    gridFillRowAsm(out, rowp.pat, nBlocks);
    gridFillRowRef(ref, rowp.pat, nBlocks);
    for (int i = 0; i < nPix; i++) {
        if (out[i] != ref[i]) {
            note_bad(i, out[i], ref[i]);
        }
    }
    if (g_outStore[ROWOFF - 2] != 0x4f11 || g_outStore[ROWOFF - 1] != 0xc0de ||
        g_outStore[ROWOFF + nPix] != 0x8ace || g_outStore[ROWOFF + nPix + 1] != 0x1357) {
        g_guardBad++;
    }
    g_calls++;
    g_pixels += nPix;
}

/* ------------------------------------------------------------------ */
/* Instruction probe: proves this QEMU build really executes the two PIE
 * instructions the kernels depend on, so a pass cannot come from them being
 * quietly skipped. */
static int probe(void) {
    static uint16_t src[8] __attribute__((aligned(16)));
    static uint16_t dst[8] __attribute__((aligned(16)));
    for (int i = 0; i < 8; i++) {
        src[i] = (uint16_t)(0x1111 * (i + 1));
        dst[i] = 0;
    }
    uint16_t *s = src, *d = dst;
    __asm__ volatile("ee.vld.128.ip q0, %[s], 0\n"
                     "ee.vst.128.ip q0, %[d], 16\n"
                     : [s] "+r"(s), [d] "+r"(d)
                     :
                     : "memory");
    if (d != dst + 8) {
        return 0; /* the post-increment did not happen */
    }
    for (int i = 0; i < 8; i++) {
        if (dst[i] != src[i]) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
int main(void) {
    uint32_t cp = 8; /* CP3 = PIE */
    __asm__ volatile("wsr %0, cpenable\nrsync\n" : : "r"(cp) : "memory");

    if (!probe()) {
        uart_puts("GM_QEMUBENCH_PIE: FAIL grid PIE instruction probe\nGM_QEMUBENCH_PIE_DONE\n");
        for (;;) {
        }
    }
    uart_puts("GM_QEMUBENCH_PIE: grid PIE instruction probe OK\n");

    for (int i = 0; i < 256; i++) {
        g_prof[i] = (uint8_t)(i * 7 + 13);
    }
    for (int i = 0; i < PAL_N; i++) {
        g_pal[i] = (uint16_t)(i * 29 + 5);
    }

    /* The fill kernel: every trip count from the zero-trip case up, and the
     * panel's own 480 and 240 wide rows. */
    for (int nb = 0; nb <= 64; nb++) {
        checkFill(nb, nb * 5);
    }
    checkFill(60, 1000); /* 480 px */
    checkFill(30, 2000); /* 240 px */

    /* Production's own operand ranges. lw is 2611..4301 over the panel's
     * rows, kv = round(255*65536/lw) is 3885..6400, du = (A[y]*dens)>>8 is
     * 32802..196608 at dens 128..384, g and amp are bytes, bg+dither is
     * 59..85. */
    static const int lws[] = {2611, 2612, 3072, 3456, 4300, 4301};
    static const int dus[] = {32802, 65604, 131072, 196608};
    for (int li = 0; li < 6; li++) {
        const int lw = lws[li];
        const int kv = (255 * 65536 + lw / 2) / lw;
        for (int di = 0; di < 4; di++) {
            for (int gi = 0; gi <= 255; gi += 51) {
                for (int ai = 0; ai <= 119; ai += 17) {
                    check(rng(), (uint32_t)dus[di], lw, kv, gi, ai, 60, 59, 700);
                }
            }
        }
    }

    /* The block test's own boundary. For a chosen du, lwPlus is fixed, so u
     * is placed to put the first pixel's fold exactly at lwPlus and one unit
     * either side, on both the rising and the falling limb of the fold
     * triangle. A single wrong comparison sense here changes the picture. */
    for (int di = 0; di < 4; di++) {
        const uint32_t du = (uint32_t)dus[di];
        const int lw = 3072;
        const int kv = (255 * 65536 + lw / 2) / lw;
        int lwPlus = lw + (int)((7u * du) >> 8) + 1;
        if (lwPlus > 32767) {
            lwPlus = 32767;
        }
        for (int delta = -2; delta <= 2; delta++) {
            const int target = lwPlus + delta;
            if (target < 0 || target > 16384) {
                continue;
            }
            /* rising limb: wv == target */
            check(((uint32_t)target << 8), du, lw, kv, 0, 100, 60, 59, 700);
            check(((uint32_t)target << 8) | 0xffu, du, lw, kv, 0, 100, 60, 59, 700);
            /* falling limb: wv == 32768 - target */
            check(((uint32_t)(32768 - target) << 8), du, lw, kv, 0, 100, 60, 59, 700);
        }
    }

    /* The fold triangle's corners and the u accumulator wrapping through
     * 2^32, which is where the kernel's EXTUI of bits 8..22 has to behave
     * like the twin's mask on a value that has just overflowed. */
    static const uint32_t corners[] = {
        0u,           255u,        256u,        (16384u << 8), (16384u << 8) - 1u,
        (16384u << 8) + 1u,        (32767u << 8),              (32768u << 8) - 1u,
        0xffffffffu,  0xffffff00u, 0xfffffff0u, 0x7fffffffu,
        0x80000000u,  0x800000ffu, 0xff000000u, 0x00800000u,
    };
    for (int ci = 0; ci < (int)(sizeof(corners) / sizeof(corners[0])); ci++) {
        for (int di = 0; di < 4; di++) {
            for (int li = 0; li < 6; li++) {
                const int lw = lws[li];
                const int kv = (255 * 65536 + lw / 2) / lw;
                check(corners[ci], (uint32_t)dus[di], lw, kv, 0, 119, 60, 59, 700);
                check(corners[ci], (uint32_t)dus[di], lw, kv, 255, 119, 60, 59, 700);
            }
        }
    }

    /* hv exactly 0 (fold == lw), and hv at its most negative (fold at the
     * triangle's peak), which is the case the kernel handles by letting MULL
     * and SRAI go negative and relying on MAX against g. */
    for (int li = 0; li < 6; li++) {
        const int lw = lws[li];
        const int kv = (255 * 65536 + lw / 2) / lw;
        for (int gi = 0; gi <= 255; gi += 5) {
            check(((uint32_t)lw << 8), 0u, lw, kv, gi, 119, 8, 59, 700);       /* hv == 0 */
            check(((uint32_t)16384u << 8), 0u, lw, kv, gi, 119, 8, 59, 700);   /* hv most negative */
            check(0u, 0u, lw, kv, gi, 119, 8, 59, 700);                        /* hv == lw, n saturates */
        }
    }

    /* Degenerate and extreme kernel inputs: du 0 (a whole row on one phase),
     * du large enough that lwPlus saturates and every block is slow, lw 0
     * and lw at the fold peak, kv 0 and kv large, amp 0 and 255, g 0 and
     * 255. None of these reach the kernel from frame() at legal params; they
     * are here because the kernel must not depend on that. */
    static const uint32_t wildDu[] = {0u, 1u, 255u, 256u, 4194304u, 0xffffffffu};
    static const int wildLw[] = {0, 1, 16384, 16385, 32767};
    static const int wildKv[] = {0, 1, 6400, 65535};
    for (int di = 0; di < 6; di++) {
        for (int li = 0; li < 5; li++) {
            for (int ki = 0; ki < 4; ki++) {
                check(rng(), wildDu[di], wildLw[li], wildKv[ki], 0, 0, 16, 0, 300);
                check(rng(), wildDu[di], wildLw[li], wildKv[ki], 255, 255, 16, 0, 300);
                check(rng(), wildDu[di], wildLw[li], wildKv[ki], 128, 1, 16, 0, 300);
            }
        }
    }

    /* Trip counts: the panel's 60 and 30 blocks, the smallest legal row, and
     * the zero-block case the caller never passes but the loop must survive.
     * band() guarantees w >= 8 and a multiple of 8, so one block is the
     * floor in production. */
    static const int blockCounts[] = {1, 2, 3, 4, 5, 7, 8, 15, 16, 30, 60, 64};
    for (int bi = 0; bi < 12; bi++) {
        for (int li = 0; li < 6; li++) {
            const int lw = lws[li];
            const int kv = (255 * 65536 + lw / 2) / lw;
            check(rng(), (uint32_t)dus[li & 3], lw, kv, (int)(rng() & 255), (int)(rng() % 120),
                  blockCounts[bi], 59, 700);
        }
    }

    /* Bulk random sweep over the production ranges, so nothing above is
     * load-bearing on its own. */
    for (long i = 0; i < 20000; i++) {
        const int lw = 2611 + (int)(rng() % 1691);
        const int kv = (255 * 65536 + lw / 2) / lw;
        const uint32_t du = 32802u + (rng() % (196608u - 32802u + 1u));
        check(rng(), du, lw, kv, (int)(rng() & 255), (int)(rng() % 120), 60, 59, 700);
    }

    uart_puts("grid: calls=");
    uart_put_dec(g_calls);
    uart_puts(" pixels=");
    uart_put_dec(g_pixels);
    uart_puts(" fastBlocks=");
    uart_put_dec(g_fastBlocks);
    uart_puts(" slowBlocks=");
    uart_put_dec(g_slowBlocks);
    uart_puts("\n");

    if (g_mismatches == 0 && g_guardBad == 0 && g_specBad == 0 && g_fastBlocks > 0 && g_slowBlocks > 0) {
        uart_puts("GM_QEMUBENCH_PIE: PASS grid kernels equal their C references, guards intact, "
                  "slow path equals bandRef spec\n");
    } else {
        uart_puts("GM_QEMUBENCH_PIE: FAIL grid mismatches=");
        uart_put_dec(g_mismatches);
        uart_puts(" guardBad=");
        uart_put_dec(g_guardBad);
        uart_puts(" specBad=");
        uart_put_dec(g_specBad);
        uart_puts(" fastBlocks=");
        uart_put_dec(g_fastBlocks);
        uart_puts(" slowBlocks=");
        uart_put_dec(g_slowBlocks);
        if (g_mismatches) {
            uart_puts(" firstCall=");
            uart_put_dec(g_firstBadCall);
            uart_puts(" idx=");
            uart_put_dec(g_firstBadIdx);
            uart_puts(" got=0x");
            uart_put_hex32(g_firstBadGot);
            uart_puts(" want=0x");
            uart_put_hex32(g_firstBadWant);
        }
        uart_puts("\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {
    }
    return 0;
}
