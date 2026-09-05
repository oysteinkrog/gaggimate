/* Real-Xtensa execution check for silkFastCell16Asm and silkExactCell16Asm,
 * the two hand-written scalar Xtensa kernels that replace AnimSilk.cpp's
 * per-cell inner loops (src/display/ui/default/bganim/AnimSilk.cpp, band()'s
 * "FAST cell" and "EXACT cell" branches). PIE has no vector gather on this
 * chip (tools/animbench/ASM_BRIEF.md), and both loops are dominated by a
 * data-dependent g_lut/pal[idx] gather, so both kernels are plain scalar
 * Xtensa.
 *
 * This is the sixth-pass port of the fifth pass's silkFastCell8Asm and
 * silkExactCell8Asm: bandRef() moved to a 16-pixel grid with paired 32-bit
 * stores and dropped the palette offset that used to be baked into the
 * dither table (see AnimSilk.cpp's file header and PALETTE_REAL_OFF's own
 * comment), so both kernels needed a real port, not a rename. The
 * references below (silkFastCell16Ref, silkExactCell16Ref) are transcribed
 * straight from the current bandRef()'s FAST and EXACT cell bodies, not
 * from the retired 8-pixel algorithm.
 *
 * This is not proven any other way: the host bench (tools/animbench) only
 * ever compiles AnimSilk.cpp's portable C++ path (band() dispatches to
 * these kernels only under __XTENSA__, which the host is not; bandRef()
 * never calls them at all), and xtensa-asm14 only proves the instructions
 * assemble and reports whether GCC's own compile of the equivalent bandRef()
 * loops spilled any register (it did not, for either loop, checked
 * separately), neither one actually EXECUTES the loop or the SRAI/ADDX2/MULL
 * sequence, and a hardware LOOP instruction executing correctly is exactly
 * the kind of thing static inspection can't confirm. This test does, under
 * Espressif's qemu-system-xtensa fork.
 *
 * Both asm blocks below are transcribed by hand from AnimSilk.cpp
 * (mnemonics, operand names, immediates, load/store order, instruction
 * count all unchanged), not regenerated or simplified, so a PASS here is
 * direct evidence about the exact sequence that file contains, matching
 * tools/qemubench/tests/anim_ember/main.c's precedent for transcribing
 * rather than re-deriving.
 *
 * No OS, no libc: prints over UART0 by writing its FIFO register directly
 * (ESP32-S3 UART0 base 0x60000000), same as anim_ember/main.c,
 * anim_starfield/main.c and blend_row/main.c.
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

static void uart_put_hex16(uint16_t v) {
    static const char hex[] = "0123456789abcdef";
    uart_putc(hex[(v >> 12) & 0xF]);
    uart_putc(hex[(v >> 8) & 0xF]);
    uart_putc(hex[(v >> 4) & 0xF]);
    uart_putc(hex[v & 0xF]);
}

static void uart_put_dec(int v) {
    char buf[12];
    int n = 0;
    unsigned int u;
    if (v < 0) {
        uart_putc('-');
        u = (unsigned int)(-(v + 1)) + 1u;
    } else {
        u = (unsigned int)v;
    }
    if (u == 0) {
        uart_putc('0');
        return;
    }
    while (u > 0) {
        buf[n++] = (char)('0' + (u % 10u));
        u /= 10u;
    }
    while (n > 0) {
        uart_putc(buf[--n]);
    }
}

/* ===== silkFastCell16Asm: verbatim transcription from AnimSilk.cpp ====== */
static void silkFastCell16Asm(uint16_t *out, int32_t PQ0, int32_t dPhalf, const int32_t *dq,
                               const uint16_t *pal) {
    int32_t pq = PQ0;
    int32_t dq0v = dq[0];
    int32_t dq1v = dq[1];
    int32_t dq2v = dq[2];
    int32_t dq3v = dq[3];
    int32_t t0, t1;
    __asm__ volatile("add    %[t0], %[pq], %[dq0]\n" /* store 0 */
                      "srai   %[t0], %[t0], 20\n"
                      "addx2  %[pq], %[dph], %[pq]\n" /* PQ1 = PQ0 + dP2 (2*dPhalf via ADDX2) */
                      "addx2  %[t0], %[t0], %[pal]\n"
                      "l16ui  %[t0], %[t0], 0\n"
                      "slli   %[t1], %[t0], 16\n"
                      "add    %[t0], %[t0], %[t1]\n"
                      "s32i   %[t0], %[out], 0\n"
                      "add    %[t0], %[pq], %[dq1]\n" /* store 1 */
                      "srai   %[t0], %[t0], 20\n"
                      "addx2  %[pq], %[dph], %[pq]\n" /* PQ2 */
                      "addx2  %[t0], %[t0], %[pal]\n"
                      "l16ui  %[t0], %[t0], 0\n"
                      "slli   %[t1], %[t0], 16\n"
                      "add    %[t0], %[t0], %[t1]\n"
                      "s32i   %[t0], %[out], 4\n"
                      "add    %[t0], %[pq], %[dq2]\n" /* store 2 */
                      "srai   %[t0], %[t0], 20\n"
                      "addx2  %[pq], %[dph], %[pq]\n" /* PQ3 */
                      "addx2  %[t0], %[t0], %[pal]\n"
                      "l16ui  %[t0], %[t0], 0\n"
                      "slli   %[t1], %[t0], 16\n"
                      "add    %[t0], %[t0], %[t1]\n"
                      "s32i   %[t0], %[out], 8\n"
                      "add    %[t0], %[pq], %[dq3]\n" /* store 3 */
                      "srai   %[t0], %[t0], 20\n"
                      "addx2  %[pq], %[dph], %[pq]\n" /* PQ4 */
                      "addx2  %[t0], %[t0], %[pal]\n"
                      "l16ui  %[t0], %[t0], 0\n"
                      "slli   %[t1], %[t0], 16\n"
                      "add    %[t0], %[t0], %[t1]\n"
                      "s32i   %[t0], %[out], 12\n"
                      "add    %[t0], %[pq], %[dq0]\n" /* store 4 */
                      "srai   %[t0], %[t0], 20\n"
                      "addx2  %[pq], %[dph], %[pq]\n" /* PQ5 */
                      "addx2  %[t0], %[t0], %[pal]\n"
                      "l16ui  %[t0], %[t0], 0\n"
                      "slli   %[t1], %[t0], 16\n"
                      "add    %[t0], %[t0], %[t1]\n"
                      "s32i   %[t0], %[out], 16\n"
                      "add    %[t0], %[pq], %[dq1]\n" /* store 5 */
                      "srai   %[t0], %[t0], 20\n"
                      "addx2  %[pq], %[dph], %[pq]\n" /* PQ6 */
                      "addx2  %[t0], %[t0], %[pal]\n"
                      "l16ui  %[t0], %[t0], 0\n"
                      "slli   %[t1], %[t0], 16\n"
                      "add    %[t0], %[t0], %[t1]\n"
                      "s32i   %[t0], %[out], 20\n"
                      "add    %[t0], %[pq], %[dq2]\n" /* store 6 */
                      "srai   %[t0], %[t0], 20\n"
                      "addx2  %[pq], %[dph], %[pq]\n" /* PQ7 */
                      "addx2  %[t0], %[t0], %[pal]\n"
                      "l16ui  %[t0], %[t0], 0\n"
                      "slli   %[t1], %[t0], 16\n"
                      "add    %[t0], %[t0], %[t1]\n"
                      "s32i   %[t0], %[out], 24\n"
                      "add    %[t0], %[pq], %[dq3]\n" /* store 7, no further PQ step */
                      "srai   %[t0], %[t0], 20\n"
                      "addx2  %[t0], %[t0], %[pal]\n"
                      "l16ui  %[t0], %[t0], 0\n"
                      "slli   %[t1], %[t0], 16\n"
                      "add    %[t0], %[t0], %[t1]\n"
                      "s32i   %[t0], %[out], 28\n"
                      : [pq] "+r"(pq), [t0] "=&r"(t0), [t1] "=&r"(t1)
                      : [dph] "r"(dPhalf), [dq0] "r"(dq0v), [dq1] "r"(dq1v), [dq2] "r"(dq2v), [dq3] "r"(dq3v),
                        [pal] "r"(pal), [out] "r"(out)
                      : "memory");
}

/* Portable reference for silkFastCell16Asm: store k (k=0..7) covers pixels
 * 2k and 2k+1, both set to lut[(PQ_k + dq[k&3]) >> 20], with PQ_0 = PQ0 and
 * PQ_{k+1} = PQ_k + 2*dPhalf for k=0..6 (seven steps for eight stores,
 * matching bandRef()'s seven "PQ += dP2"). Evaluated the same way the
 * kernel's own PQ accumulator is stepped, not recomputed from scratch, so a
 * sign or truncation difference in the stepping itself would also show up
 * here. */
static void silkFastCell16Ref(uint16_t *out, int32_t PQ0, int32_t dPhalf, const int32_t *dq,
                               const uint16_t *pal) {
    int32_t pq = PQ0;
    for (int k = 0; k < 8; k++) {
        int32_t idx = (pq + dq[k & 3]) >> 20;
        uint16_t v = pal[idx];
        out[2 * k] = v;
        out[2 * k + 1] = v;
        if (k != 7) {
            pq += 2 * dPhalf;
        }
    }
}

/* ===== silkExactCell16Asm: verbatim transcription from AnimSilk.cpp =====
 * Transcribed instruction for instruction from GCC 14's own compiled EXACT
 * cell loop for the current bandRef() (tools/animbench/xtensa-asm14.sh
 * AnimSilk, then xtensa-asm14/AnimSilk.S): a hardware zero-overhead LOOP of
 * 8 iterations, each producing one pixel PAIR, not the fifth pass's
 * per-pixel loop. kctr is a fresh 0-based counter standing in for the
 * pair's phase (valid because every call site's x is a multiple of
 * SILK_GRID=16, so pair k's true (x>>1)&3 equals a fresh counter's k&3
 * exactly, the same argument silkFastCell16Asm's dq[] cycling relies on).
 * mull's result has exactly ONE independent instruction (the sq ramp-step
 * add) before the add that consumes it, matching GCC's own schedule for
 * this data layout, same idea the fifth pass's round 3 kernel already used.
 * The palette gather goes through a SEPARATE pal pointer, not lut: the
 * sixth pass moved PALETTE_REAL_OFF out of the dither table (see
 * AnimSilk.cpp's PALETTE_REAL_OFF comment), so lut and pal are no longer
 * the same base the way the retired kernel's single "lut" argument assumed.
 */
static void silkExactCell16Asm(uint16_t *out, int32_t sQ0, int32_t stepQ2, int32_t envBase,
                                const uint8_t *dx2AtX, const int32_t *ditherRow, const uint16_t *lut,
                                const uint16_t *pal) {
    int32_t sq = sQ0;
    const uint8_t *dx2p = dx2AtX;
    uint16_t *outp = out;
    int32_t kctr = 0;
    int32_t u, v1, ph;
    __asm__ volatile("movi   %[u], 8\n"
                      "loop   %[u], 1f\n"
                      "l8ui   %[v1], %[dx2p], 0\n"          /* dx2 */
                      "addi   %[dx2p], %[dx2p], 2\n"        /* dx2p += 2 (next pair) */
                      "srai   %[u], %[sq], 8\n"             /* s = sq >> 8 */
                      "addx2  %[u], %[u], %[lut]\n"         /* &lut[s] */
                      "extui  %[ph], %[kctr], 0, 2\n"       /* k & 3 */
                      "l16ui  %[u], %[u], 0\n"              /* nc_q8 = lut[s] */
                      "sub    %[v1], %[envb], %[v1]\n"      /* env_q8 = envBase - dx2 */
                      "addx4  %[ph], %[ph], %[ditherrow]\n" /* &ditherRow[k&3] */
                      "l32i   %[ph], %[ph], 0\n"            /* dith = ditherRow[k&3] */
                      "mull   %[u], %[u], %[v1]\n"          /* nc_q8 * env_q8 */
                      "add    %[sq], %[sq], %[stepq]\n"     /* sq += stepQ2 (mull-gap filler) */
                      "add    %[u], %[u], %[ph]\n"          /* idxq = mull_result + dith */
                      "srai   %[u], %[u], 16\n"             /* idx */
                      "addx2  %[u], %[u], %[pal]\n"         /* &pal[idx] */
                      "l16ui  %[u], %[u], 0\n"              /* v = pal[idx] */
                      "addi   %[kctr], %[kctr], 1\n"        /* kctr++ */
                      "slli   %[v1], %[u], 16\n"            /* v << 16 */
                      "add    %[u], %[u], %[v1]\n"          /* v | (v << 16) */
                      "s32i   %[u], %[outp], 0\n"           /* out[pair] = v duplicated */
                      "addi   %[outp], %[outp], 4\n"        /* outp += 4 (one pixel pair) */
                      "1:\n"
                      : [sq] "+r"(sq), [dx2p] "+r"(dx2p), [outp] "+r"(outp), [kctr] "+r"(kctr), [u] "=&r"(u),
                        [v1] "=&r"(v1), [ph] "=&r"(ph)
                      : [stepq] "r"(stepQ2), [envb] "r"(envBase), [lut] "r"(lut), [pal] "r"(pal),
                        [ditherrow] "r"(ditherRow)
                      : "memory");
}

/* Portable reference for silkExactCell16Asm: for k=0..7, s = sq>>8,
 * nc = lut[s], dx2 = dx2AtX[2*k] (one dx2 read per PAIR, at the pair's
 * first pixel, matching the kernel's stride-2 walking pointer), env =
 * envBase - dx2, dith = ditherRow[k & 3], idx = (nc*env + dith) >> 16, and
 * both pixels of the pair are set to pal[idx]. sq is stepped by stepQ2
 * after computing s, matching bandRef()'s "s = sQ>>8; ...; sQ += stepQ2"
 * ordering exactly. */
static void silkExactCell16Ref(uint16_t *out, int32_t sQ0, int32_t stepQ2, int32_t envBase,
                                const uint8_t *dx2AtX, const int32_t *ditherRow, const uint16_t *lut,
                                const uint16_t *pal) {
    int32_t sq = sQ0;
    for (int k = 0; k < 8; k++) {
        int32_t s = sq >> 8;
        int32_t nc = lut[s];
        int32_t dx2 = dx2AtX[2 * k];
        int32_t dith = ditherRow[k & 3];
        int32_t env = envBase - dx2;
        int32_t idxq = nc * env + dith;
        int32_t idx = idxq >> 16;
        uint16_t v = pal[idx];
        out[2 * k] = v;
        out[2 * k + 1] = v;
        sq += stepQ2;
    }
}

/* Shared LUT for every case below: 8192 entries, lutStorage[i] = i*3+7 (a
 * distinctive, monotonic, non-power-of-two pattern so a wrong index reads a
 * detectably wrong value rather than accidentally matching). `lut` points
 * to the middle of the storage so lut[idx] is valid for idx in
 * [-4096, 4095], and `pal` points 64 entries further in (well inside the
 * same storage) to exercise a lut/pal split that is NOT the same pointer,
 * the way band()'s dispatch always calls these kernels since the sixth
 * pass moved PALETTE_REAL_OFF out of the dither table. Both kernels' index
 * arithmetic can legitimately produce a value slightly outside a [0, N)
 * window at the edges of their real operating range (AnimSilk.cpp's file
 * header proves a provably tiny underflow/overflow case for both the
 * contrast and palette indices), and this test's synthetic inputs
 * deliberately push further than production ever reaches too, to verify
 * SRAI's sign extension explicitly (see fastB and exactA below). */
#define LUT_HALF 4096
#define PAL_OFFSET 64
static uint16_t lutStorage[2 * LUT_HALF];
static const uint16_t *lut;
static const uint16_t *pal;

static int mismatches = 0;
static int firstBadCase = -1;
static int firstBadPixel = -1;
static uint16_t firstBadGot = 0;
static uint16_t firstBadWant = 0;

static void checkCell(const char *name, int caseId, const uint16_t *got, const uint16_t *want) {
    for (int i = 0; i < 16; i++) {
        if (got[i] != want[i]) {
            if (firstBadCase < 0) {
                firstBadCase = caseId;
                firstBadPixel = i;
                firstBadGot = got[i];
                firstBadWant = want[i];
            }
            mismatches++;
            uart_puts("  MISMATCH ");
            uart_puts(name);
            uart_puts(" case=");
            uart_put_dec(caseId);
            uart_puts(" pixel=");
            uart_put_dec(i);
            uart_puts(" got=");
            uart_put_hex16(got[i]);
            uart_puts(" want=");
            uart_put_hex16(want[i]);
            uart_puts("\n");
        }
    }
}

int main(void) {
    for (int i = 0; i < 2 * LUT_HALF; i++) {
        lutStorage[i] = (uint16_t)(i * 3 + 7);
    }
    lut = lutStorage + LUT_HALF;
    pal = lut + PAL_OFFSET;

    uint16_t gotBuf[16];
    uint16_t wantBuf[16];

    /* Fast-kernel case A: clean, distinct, all-positive indices.
     * idx(k) = 2000 + k + {0,2,4,6}[k&3], every store a different index, so
     * a scrambled store order or wrong dq-phase pairing shows up
     * immediately as a swapped value rather than an accidental match.
     * PQ0 = 2000<<20, dPhalf = 1<<19 (dP2 = 2*dPhalf = 1<<20, one index
     * unit per store step), dq = {0,2,4,6}<<20.
     *
     * Every local array below (here and in the later cases) is filled with
     * element-by-element assignments rather than a brace initializer: GCC
     * lowered a >= 4-element constant brace initializer to a call to
     * memcpy() from a rodata blob, and this link is freestanding
     * (-nostdlib, no libc), so that call is an undefined reference at link
     * time. Scalar stores never trigger that lowering. */
    {
        int32_t dq[4];
        dq[0] = 0;
        dq[1] = 2 << 20;
        dq[2] = 4 << 20;
        dq[3] = 6 << 20;
        int32_t PQ0 = 2000 << 20;
        int32_t dPhalf = 1 << 19;
        silkFastCell16Asm(gotBuf, PQ0, dPhalf, dq, pal);
        silkFastCell16Ref(wantBuf, PQ0, dPhalf, dq, pal);
        checkCell("fastA", 0, gotBuf, wantBuf);
    }

    /* Fast-kernel case B: negative-index territory.
     * idx(k) = -3 + k + {0,1,2,3}[k&3], base index -3 so several stores
     * land below zero, which is the case that would catch an SRAI/SRLI
     * mixup (an unsigned/logical shift would turn a small negative sum
     * into a huge positive one instead of -1, -2, -3). */
    {
        int32_t dq[4];
        dq[0] = 0;
        dq[1] = 1 << 20;
        dq[2] = 2 << 20;
        dq[3] = 3 << 20;
        int32_t PQ0 = -(3 << 20); /* shift the positive magnitude, then negate: avoids
                                    * shifting a negative value, which is only defined
                                    * behavior since C++20 / with -fwrapv on this
                                    * toolchain's C mode, giving the same result, -3145728. */
        int32_t dPhalf = 1 << 19;
        silkFastCell16Asm(gotBuf, PQ0, dPhalf, dq, pal);
        silkFastCell16Ref(wantBuf, PQ0, dPhalf, dq, pal);
        checkCell("fastB", 1, gotBuf, wantBuf);
    }

    /* Fast-kernel case C: production-scale magnitudes.
     * Mirrors AnimSilk.cpp's own proven bounds (file header): Pcur/Pnext
     * are nc_q8*env_q8 products, max 255<<16 = 16,711,680; PQ0 = Pcur<<4
     * (SILK_GRID_SHIFT) and dPhalf = Pnext - Pcur, exactly what band()
     * passes. dq spans g_ditherQ's documented [-16,16]<<16 range scaled by
     * (1<<SILK_GRID_SHIFT), i.e. dqArr's real [-16<<20, 16<<20] range, to
     * check the same near-int32 add/shift path production actually
     * exercises never overflows or misbehaves in the transcription (the
     * header proves it does not overflow int32; this checks the asm agrees
     * with the reference at that scale, not just that it fits). */
    {
        int32_t dq[4];
        dq[0] = -(16 << 20);
        dq[1] = -(5 << 20);
        dq[2] = 5 << 20;
        dq[3] = 16 << 20;
        int32_t Pcur = 255 << 16; /* max nc_q8*env_q8 */
        int32_t Pnext = 0;        /* min */
        int32_t PQ0 = Pcur << 4;
        int32_t dPhalf = Pnext - Pcur;
        silkFastCell16Asm(gotBuf, PQ0, dPhalf, dq, pal);
        silkFastCell16Ref(wantBuf, PQ0, dPhalf, dq, pal);
        checkCell("fastC", 2, gotBuf, wantBuf);
    }

    /* Exact-kernel case A: negative-then-positive s, cycling dither,
     * grid's first cell (x=0).
     * sQ0/stepQ2 sweep s = sq>>8 through -2,-1,0,1,2,3,4,5 across the 8
     * pair-iterations (exercises the contrast gather's SRAI sign handling
     * the same way fastB exercises the palette gather's). dx2 gives every
     * PAIR a distinct, increasing value at stride 2 (env stays real and
     * nonzero, so MULL sees genuine magnitudes); ditherRow gives 4 distinct
     * values that repeat at pairs 4-7 (k&3), matching production's
     * period-4 dither exactly, so a wrong phase (using k instead of k&3, or
     * an off-by-one in the cycle) shows up as a mismatch at pair 4 even
     * though pairs 0-3 already passed. This case also stands in for width
     * 480 and 240 and both row parities: this kernel never sees w, y0, or
     * yph directly (band() folds all of that into sQ0/envBase/ditherRow
     * before calling it), so the parameter combinations that matter to the
     * kernel are these scalar values, not the row geometry that produced
     * them; the row-geometry-to-argument mapping itself is exercised by
     * tools/animbench's host golden comparison (bandRef() and band() are
     * the same code on host) across both widths and parities already. */
    {
        int32_t sQ0 = -2 * 256;
        int32_t stepQ2 = 256;
        int32_t envBase = 256;
        uint8_t dx2[16];
        dx2[0] = 0;
        dx2[2] = 16;
        dx2[4] = 32;
        dx2[6] = 48;
        dx2[8] = 64;
        dx2[10] = 80;
        dx2[12] = 96;
        dx2[14] = 112;
        /* Odd (unread) slots set to a sentinel: if the kernel or reference
         * ever reads an odd offset, this stops it from accidentally
         * matching a real value. */
        dx2[1] = 200;
        dx2[3] = 200;
        dx2[5] = 200;
        dx2[7] = 200;
        dx2[9] = 200;
        dx2[11] = 200;
        dx2[13] = 200;
        dx2[15] = 200;
        int32_t ditherRow[4];
        ditherRow[0] = -800000;
        ditherRow[1] = -260000;
        ditherRow[2] = 260000;
        ditherRow[3] = 800000;
        silkExactCell16Asm(gotBuf, sQ0, stepQ2, envBase, dx2, ditherRow, lut, pal);
        silkExactCell16Ref(wantBuf, sQ0, stepQ2, envBase, dx2, ditherRow, lut, pal);
        checkCell("exactA", 3, gotBuf, wantBuf);
    }

    /* Exact-kernel case B: production-scale magnitudes, grid's last
     * cell, dither amplitude at the ditherAmp() cap of 16.
     * s ranges over a realistic slice of contrastLUT's domain (s in
     * [1400,1407], well inside [0,3072]); dx2 spans env's full [0,256]
     * documented range across the 8 pairs; ditherRow spans g_ditherQ's
     * full documented [-16,16]<<16 range across its 4 entries (the
     * ditherAmp() cap the palette pad exists for, see PAD's comment: an
     * unclamped gather past it is the bug the fuzzer once missed), same
     * construction as fastC above. This is the combination glow=0/glow=100
     * param extremes push toward (steep curvature -> more EXACT cells), so
     * it is the closest this synthetic test gets to "the param extremes as
     * they reach the kernel" for this path, and sQ0 is chosen so the last
     * grid node (x = w - SILK_GRID for w=480 or w=240) is a plausible
     * s value for either supported width. */
    {
        int32_t sQ0 = 1400 << 8;
        int32_t stepQ2 = 64; /* s advances by 1 every 4 pairs: gentle, in-range drift */
        int32_t envBase = 256;
        uint8_t dx2[16];
        dx2[0] = 0;
        dx2[2] = 32;
        dx2[4] = 64;
        dx2[6] = 96;
        dx2[8] = 128;
        dx2[10] = 160;
        dx2[12] = 192;
        dx2[14] = 224;
        dx2[1] = 250;
        dx2[3] = 250;
        dx2[5] = 250;
        dx2[7] = 250;
        dx2[9] = 250;
        dx2[11] = 250;
        dx2[13] = 250;
        dx2[15] = 250;
        int32_t ditherRow[4];
        ditherRow[0] = -(16 << 16);
        ditherRow[1] = -(5 << 16);
        ditherRow[2] = 5 << 16;
        ditherRow[3] = 16 << 16;
        silkExactCell16Asm(gotBuf, sQ0, stepQ2, envBase, dx2, ditherRow, lut, pal);
        silkExactCell16Ref(wantBuf, sQ0, stepQ2, envBase, dx2, ditherRow, lut, pal);
        checkCell("exactB", 4, gotBuf, wantBuf);
    }

    /* Exact-kernel case C: dither amplitude 0, grid's first cell.
     * The opposite extreme from exactB's dither cap: ditherAmp() returns 0
     * for a palette flat enough to need no dithering at all (a real,
     * reachable palette state, not just a theoretical bound), so
     * ditherRow is all zero here and idx collapses to plain
     * (nc*env) >> 16 with no per-pixel bias term. s stays in-range and
     * mostly flat (stepQ2 small) since a flat palette is what pairs with
     * zero dither in production. This and exactA between them cover both
     * ends of ditherAmp()'s range and both grid ends this kernel can be
     * called for; see exactA's comment for why the kernel itself never
     * sees which cell or row produced these scalars, only their values. */
    {
        int32_t sQ0 = 500 << 8;
        int32_t stepQ2 = 16;
        int32_t envBase = 200;
        uint8_t dx2[16];
        dx2[0] = 10;
        dx2[2] = 20;
        dx2[4] = 30;
        dx2[6] = 40;
        dx2[8] = 50;
        dx2[10] = 60;
        dx2[12] = 70;
        dx2[14] = 80;
        dx2[1] = 255;
        dx2[3] = 255;
        dx2[5] = 255;
        dx2[7] = 255;
        dx2[9] = 255;
        dx2[11] = 255;
        dx2[13] = 255;
        dx2[15] = 255;
        int32_t ditherRow[4];
        ditherRow[0] = 0;
        ditherRow[1] = 0;
        ditherRow[2] = 0;
        ditherRow[3] = 0;
        silkExactCell16Asm(gotBuf, sQ0, stepQ2, envBase, dx2, ditherRow, lut, pal);
        silkExactCell16Ref(wantBuf, sQ0, stepQ2, envBase, dx2, ditherRow, lut, pal);
        checkCell("exactC", 5, gotBuf, wantBuf);
    }

    uart_puts("GM_QEMUBENCH_PIE: mismatches=");
    uart_put_dec(mismatches);
    uart_puts("\n");

    if (mismatches == 0) {
        uart_puts("GM_QEMUBENCH_PIE: PASS silkFastCell16Asm+silkExactCell16Asm bit-exact vs "
                  "silkFastCell16Ref+silkExactCell16Ref reference (6 cases: fastA fastB fastC exactA exactB "
                  "exactC, 16 pixels each as 8 paired stores, spanning negative/zero/positive index territory, "
                  "production-scale magnitudes, dither amplitude 0 and the ditherAmp() cap, and the grid's "
                  "first/last cell)\n");
    } else {
        uart_puts("GM_QEMUBENCH_PIE: FAIL first mismatch case=");
        uart_put_dec(firstBadCase);
        uart_puts(" pixel=");
        uart_put_dec(firstBadPixel);
        uart_puts(" got=");
        uart_put_hex16(firstBadGot);
        uart_puts(" want=");
        uart_put_hex16(firstBadWant);
        uart_puts("\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");

    for (;;) {
        /* Spin so QEMU has a stable state; nothing to return to. */
    }
}
