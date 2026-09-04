/* Real-Xtensa execution check for silkFastCell8Asm and silkExactCell8Asm --
 * the two hand-written scalar Xtensa kernels that replace AnimSilk.cpp's
 * per-cell inner loops (src/display/ui/default/bganim/AnimSilk.cpp, band()'s
 * "FAST cell" and "EXACT cell" branches). PIE has no vector gather on this
 * chip (tools/animbench/ASM_BRIEF.md), and both loops are dominated by a
 * data-dependent g_lut[idx] gather, so both kernels are plain scalar
 * Xtensa. silkFastCell8Asm is fully unrolled over the 8 pixels a cell
 * always covers (its C++ source in bandRef() is itself 8 flat statements,
 * not a loop -- nothing for GCC or this kernel to loop over). As of round 3,
 * silkExactCell8Asm is NOT unrolled: it uses a hardware zero-overhead LOOP
 * over a compact per-pixel body, matching GCC 14's own compiled EXACT-cell
 * loop instruction-for-instruction rather than the flat 8x unroll rounds 1
 * and 2 used (see the kernel's own comment in AnimSilk.cpp for why the
 * unrolled version lost to GCC's compile despite fewer static instructions
 * in earlier rounds -- code size in IRAM, not register pressure).
 *
 * This is not proven any other way: the host bench (tools/animbench) only
 * ever compiles AnimSilk.cpp's portable C++ path (band() dispatches to
 * these kernels only under __XTENSA__, which the host is not; bandRef()
 * never calls them at all), and xtensa-asm14 only proves the instructions
 * assemble and that GCC did not spill any register around either block
 * (confirmed separately: 57 instructions / 0 spills for the fast kernel,
 * 24 instructions [entry/loop-setup/18-instruction body/retw] / 0 spills
 * for the exact kernel) -- neither one actually EXECUTES the loop or the
 * SRAI/ADDX2/MULL sequence, and a hardware LOOP instruction executing
 * correctly is exactly the kind of thing static inspection can't confirm.
 * This test does, under Espressif's qemu-system-xtensa fork.
 *
 * Both asm blocks below are transcribed by hand from AnimSilk.cpp
 * (mnemonics, operand names, immediates, load/store order, instruction
 * count all unchanged) -- not regenerated or simplified, so a PASS here is
 * direct evidence about the exact sequence that file contains, matching
 * tools/qemubench/tests/anim_ember/main.c's precedent for transcribing
 * rather than re-deriving.
 *
 * Reference implementations (silkFastCell8Ref, silkExactCell8Ref) are a
 * direct, portable transliteration of the per-pixel expressions in
 * AnimSilk.cpp's bandRef() -- not hardcoded expected constants, since this
 * test's job is to catch a transcription slip in the ASM (wrong register,
 * wrong shift amount, wrong byte offset, scrambled pixel order), not to
 * re-verify the algorithm itself (the host bench's golden comparison
 * already covers that, bit for bit, via bandRef()). Both kernel and
 * reference run on the SAME synthetic inputs and are compared pixel by
 * pixel at runtime, so no expected-value arithmetic needed to be done by
 * hand -- only the inputs did (see the comment above each test case for how
 * they were chosen and why they stay in bounds).
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

/* ===== silkFastCell8Asm: verbatim transcription from AnimSilk.cpp ======= */
static void silkFastCell8Asm(uint16_t *out, int32_t PQ0, int32_t dP, const int32_t *dq, const uint16_t *lut) {
    int32_t pq = PQ0;
    int32_t dq0v = dq[0];
    int32_t dq1v = dq[1];
    int32_t dq2v = dq[2];
    int32_t dq3v = dq[3];
    int32_t t0, t1;
    __asm__ volatile("add    %[t0], %[pq], %[dq0]\n" /* pixels 0,1 */
                      "srai   %[t0], %[t0], 19\n"
                      "add    %[pq], %[pq], %[dp]\n"
                      "add    %[t1], %[pq], %[dq1]\n"
                      "srai   %[t1], %[t1], 19\n"
                      "addx2  %[t0], %[t0], %[lut]\n"
                      "addx2  %[t1], %[t1], %[lut]\n"
                      "l16ui  %[t0], %[t0], 0\n"
                      "l16ui  %[t1], %[t1], 0\n"
                      "add    %[pq], %[pq], %[dp]\n"
                      "slli   %[t1], %[t1], 16\n"
                      "or     %[t0], %[t0], %[t1]\n"
                      "s32i   %[t0], %[out], 0\n"
                      "add    %[t0], %[pq], %[dq2]\n" /* pixels 2,3 */
                      "srai   %[t0], %[t0], 19\n"
                      "add    %[pq], %[pq], %[dp]\n"
                      "add    %[t1], %[pq], %[dq3]\n"
                      "srai   %[t1], %[t1], 19\n"
                      "addx2  %[t0], %[t0], %[lut]\n"
                      "addx2  %[t1], %[t1], %[lut]\n"
                      "l16ui  %[t0], %[t0], 0\n"
                      "l16ui  %[t1], %[t1], 0\n"
                      "add    %[pq], %[pq], %[dp]\n"
                      "slli   %[t1], %[t1], 16\n"
                      "or     %[t0], %[t0], %[t1]\n"
                      "s32i   %[t0], %[out], 4\n"
                      "add    %[t0], %[pq], %[dq0]\n" /* pixels 4,5 */
                      "srai   %[t0], %[t0], 19\n"
                      "add    %[pq], %[pq], %[dp]\n"
                      "add    %[t1], %[pq], %[dq1]\n"
                      "srai   %[t1], %[t1], 19\n"
                      "addx2  %[t0], %[t0], %[lut]\n"
                      "addx2  %[t1], %[t1], %[lut]\n"
                      "l16ui  %[t0], %[t0], 0\n"
                      "l16ui  %[t1], %[t1], 0\n"
                      "add    %[pq], %[pq], %[dp]\n"
                      "slli   %[t1], %[t1], 16\n"
                      "or     %[t0], %[t0], %[t1]\n"
                      "s32i   %[t0], %[out], 8\n"
                      "add    %[t0], %[pq], %[dq2]\n" /* pixels 6,7 */
                      "srai   %[t0], %[t0], 19\n"
                      "add    %[pq], %[pq], %[dp]\n"
                      "add    %[t1], %[pq], %[dq3]\n"
                      "srai   %[t1], %[t1], 19\n"
                      "addx2  %[t0], %[t0], %[lut]\n"
                      "addx2  %[t1], %[t1], %[lut]\n"
                      "l16ui  %[t0], %[t0], 0\n"
                      "l16ui  %[t1], %[t1], 0\n"
                      "slli   %[t1], %[t1], 16\n"
                      "or     %[t0], %[t0], %[t1]\n"
                      "s32i   %[t0], %[out], 12\n"
                      : [pq] "+r"(pq), [t0] "=&r"(t0), [t1] "=&r"(t1)
                      : [dp] "r"(dP), [dq0] "r"(dq0v), [dq1] "r"(dq1v), [dq2] "r"(dq2v), [dq3] "r"(dq3v),
                        [lut] "r"(lut), [out] "r"(out)
                      : "memory");
}

/* Portable reference for silkFastCell8Asm: out[k] = lut[(PQ0 + k*dP +
 * dq[k&3]) >> 19] for k=0..7, PQ0+k*dP evaluated the same way band()'s
 * scalar path (bandRef()) evaluates it -- one running accumulator stepped
 * by dP each pixel, not recomputed from scratch, so a sign or truncation
 * difference in the stepping itself would also show up here. */
static void silkFastCell8Ref(uint16_t *out, int32_t PQ0, int32_t dP, const int32_t *dq, const uint16_t *lut) {
    int32_t pq = PQ0;
    for (int k = 0; k < 8; k++) {
        int32_t idx = (pq + dq[k & 3]) >> 19;
        out[k] = lut[idx];
        if (k != 7) {
            pq += dP;
        }
    }
}

/* ===== silkExactCell8Asm: verbatim transcription from AnimSilk.cpp ======
 * Round 3 rewrite: no longer an 8x unroll. GCC's own compile of the
 * equivalent EXACT-cell loop (tools/animbench/xtensa-asm14.sh AnimSilk) uses
 * a real hardware zero-overhead LOOP over an 18-instruction body rather than
 * unrolling, and rounds 1-2's unrolled kernels (110-113 static instructions,
 * 303 bytes of IRAM) lost to that compiled loop on the device despite
 * matching or beating it on instruction count -- the missed variable was
 * IRAM code size / fetch bandwidth (IRAM has no icache: every byte is
 * fetched fresh on every call), not register pressure or scheduling. This
 * transcription is instruction-for-instruction off GCC's compiled loop body,
 * not re-derived by hand. kctr is a fresh 0-based counter standing in for
 * GCC's own x&3 phase computation (valid because every call site's x is a
 * multiple of SILK_GRID=8, so pixel k's true x&3 == k&3 -- see the comment
 * on this function in AnimSilk.cpp). dith now comes from a per-row pointer
 * (ditherRow, indexed by k&3 via EXTUI+ADDX4) instead of 4 preloaded
 * registers -- ditherRow's contents are what dithPhase[] held in round 2,
 * just addressed at runtime like GCC does instead of unrolled away. mull's
 * result has exactly ONE independent instruction (the sq ramp-step add)
 * before the add that consumes it, matching GCC's own current schedule for
 * this data layout (round 2 targeted a stale two-instruction gap measured
 * against a pre-restructuring compile).
 */
static void silkExactCell8Asm(uint16_t *out, int32_t sQ0, int32_t stepQ, int32_t envBase, const uint8_t *dx2AtX,
                               const int32_t *ditherRow, const uint16_t *lut) {
    int32_t sq = sQ0;
    const uint8_t *dx2p = dx2AtX;
    uint16_t *outp = out;
    int32_t kctr = 0;
    int32_t u, v1, ph;
    __asm__ volatile("movi   %[u], 8\n"
                      "loop   %[u], 1f\n"
                      "l8ui   %[v1], %[dx2p], 0\n"           /* dx2 */
                      "srai   %[u], %[sq], 8\n"              /* s = sq >> 8 */
                      "addx2  %[u], %[u], %[lut]\n"          /* &lut[s] */
                      "extui  %[ph], %[kctr], 0, 2\n"        /* k & 3 */
                      "l16ui  %[u], %[u], 0\n"               /* nc_q8 = lut[s] */
                      "addx4  %[ph], %[ph], %[ditherrow]\n"  /* &ditherRow[k&3] */
                      "sub    %[v1], %[envb], %[v1]\n"       /* env_q8 = envBase - dx2 */
                      "l32i   %[ph], %[ph], 0\n"             /* dith = ditherRow[k&3] */
                      "mull   %[u], %[u], %[v1]\n"           /* nc_q8 * env_q8 */
                      "add    %[sq], %[sq], %[stepq]\n"      /* sq += stepQ (mull-gap filler) */
                      "add    %[u], %[u], %[ph]\n"           /* idxq = mull_result + dith */
                      "srai   %[u], %[u], 16\n"              /* idx */
                      "addx2  %[u], %[u], %[lut]\n"          /* &lut[idx] */
                      "addi   %[dx2p], %[dx2p], 1\n"         /* dx2p++ */
                      "l16ui  %[u], %[u], 0\n"               /* palette = lut[idx] */
                      "addi   %[kctr], %[kctr], 1\n"         /* kctr++ */
                      "s16i   %[u], %[outp], 0\n"            /* out[x] = palette */
                      "addi   %[outp], %[outp], 2\n"         /* outp++ */
                      "1:\n"
                      : [sq] "+r"(sq), [dx2p] "+r"(dx2p), [outp] "+r"(outp), [kctr] "+r"(kctr), [u] "=&r"(u),
                        [v1] "=&r"(v1), [ph] "=&r"(ph)
                      : [stepq] "r"(stepQ), [envb] "r"(envBase), [lut] "r"(lut), [ditherrow] "r"(ditherRow)
                      : "memory");
}

/* Portable reference for silkExactCell8Asm: out[k] = lut[(nc*env + dith) >>
 * 16] where nc = lut[sq>>8], env = envBase - dx2AtX[k],
 * dith = ditherRow[k & 3], sq stepped by stepQ each pixel -- the exact
 * per-pixel expression from bandRef()'s EXACT-cell loop body, transliterated
 * straight across. dith cycles a 4-entry table (period 4 in x, same as
 * production -- see AnimSilk.cpp's g_ditherQ) rather than taking an
 * independent value per pixel, matching the kernel's ditherRow argument. */
static void silkExactCell8Ref(uint16_t *out, int32_t sQ0, int32_t stepQ, int32_t envBase, const uint8_t *dx2AtX,
                               const int32_t *ditherRow, const uint16_t *lut) {
    int32_t sq = sQ0;
    for (int k = 0; k < 8; k++) {
        int32_t s = sq >> 8;
        int32_t nc = lut[s];
        int32_t dx2 = dx2AtX[k];
        int32_t dith = ditherRow[k & 3];
        int32_t env = envBase - dx2;
        int32_t idxq = nc * env + dith;
        int32_t idx = idxq >> 16;
        out[k] = lut[idx];
        sq += stepQ;
    }
}

/* Shared LUT for every case below: 8192 entries, lutStorage[i] = i*3+7 (a
 * distinctive, monotonic, non-power-of-two pattern so a wrong index reads a
 * detectably wrong value rather than accidentally matching). `lut` points
 * to the middle of the storage so lut[idx] is valid for idx in
 * [-4096, 4095] -- both kernels' index arithmetic can legitimately produce
 * a value slightly outside [0, N) at the edges of their real operating
 * range (AnimSilk.cpp's file header proves a "provably tiny 1-step
 * underflow" case; this test's synthetic inputs also deliberately push
 * further negative than production ever reaches, to verify SRAI's sign
 * extension explicitly -- see fastCase B below), and this padding makes
 * that safe to actually dereference instead of only reasoning about it. */
#define LUT_HALF 4096
static uint16_t lutStorage[2 * LUT_HALF];
static const uint16_t *lut;

static int mismatches = 0;
static int firstBadCase = -1;
static int firstBadPixel = -1;
static uint16_t firstBadGot = 0;
static uint16_t firstBadWant = 0;

static void checkCell(const char *name, int caseId, const uint16_t *got, const uint16_t *want) {
    for (int i = 0; i < 8; i++) {
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

    uint16_t gotBuf[8];
    uint16_t wantBuf[8];

    /* ---- fast-kernel case A: clean, distinct, all-positive indices -----
     * idx(k) = 2000 + k + {0,2,4,6}[k&3], every pixel a different index,
     * chosen so a scrambled pixel order or wrong dq-phase pairing shows up
     * immediately as a swapped value rather than an accidental match.
     * PQ0 = 2000<<19, dP = 1<<19 (one index unit per pixel step),
     * dq = {0,2,4,6}<<19.
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
        dq[1] = 2 << 19;
        dq[2] = 4 << 19;
        dq[3] = 6 << 19;
        int32_t PQ0 = 2000 << 19;
        int32_t dP = 1 << 19;
        silkFastCell8Asm(gotBuf, PQ0, dP, dq, lut);
        silkFastCell8Ref(wantBuf, PQ0, dP, dq, lut);
        checkCell("fastA", 0, gotBuf, wantBuf);
    }

    /* ---- fast-kernel case B: negative-index territory ------------------
     * idx(k) = -3 + k + {0,1,2,3}[k&3], base index -3 so several pixels
     * land below zero -- this is the case that would catch an SRAI/SRLI
     * mixup (an unsigned/logical shift would turn a small negative sum
     * into a huge positive one instead of -1, -2, -3). */
    {
        int32_t dq[4];
        dq[0] = 0;
        dq[1] = 1 << 19;
        dq[2] = 2 << 19;
        dq[3] = 3 << 19;
        int32_t PQ0 = -(3 << 19); /* shift the positive magnitude, then negate: avoids
                                    * shifting a negative value, which is only defined
                                    * behavior since C++20 / with -fwrapv on this
                                    * toolchain's C mode -- same result, -1572864. */
        int32_t dP = 1 << 19;
        silkFastCell8Asm(gotBuf, PQ0, dP, dq, lut);
        silkFastCell8Ref(wantBuf, PQ0, dP, dq, lut);
        checkCell("fastB", 1, gotBuf, wantBuf);
    }

    /* ---- fast-kernel case C: production-scale magnitudes ---------------
     * Mirrors AnimSilk.cpp's own proven bounds (file header): Pcur/Pnext
     * are nc_q8*env_q8 products, max 255<<16 = 16,711,680; dq is
     * ra[].dith<<3 with dith carrying the PALETTE_REAL_OFF<<16 bias plus
     * the dither range, real magnitude order 2.35e8 before the <<3. This
     * case uses Pcur at its maximum and Pnext at zero (dP at its most
     * negative extreme) with dq spanning dith's documented
     * [-52224,+45696] range around that bias, to check the same
     * near-int32 add/shift path production actually exercises never
     * overflows or misbehaves in the transcription (the header proves it
     * does not overflow int32; this checks the asm agrees with the
     * reference at that scale, not just that it fits). */
    {
        const int32_t paletteRealOffQ16 = 3328 << 16; /* PALETTE_REAL_OFF<<16, see AnimSilk.cpp */
        int32_t dq[4];
        dq[0] = (paletteRealOffQ16 - 52224) << 3;
        dq[1] = (paletteRealOffQ16 - 17408) << 3;
        dq[2] = (paletteRealOffQ16 + 17408) << 3;
        dq[3] = (paletteRealOffQ16 + 45696) << 3;
        int32_t Pcur = 255 << 16; /* max nc_q8*env_q8 */
        int32_t Pnext = 0;        /* min */
        int32_t PQ0 = Pcur << 3;
        int32_t dP = Pnext - Pcur;
        silkFastCell8Asm(gotBuf, PQ0, dP, dq, lut);
        silkFastCell8Ref(wantBuf, PQ0, dP, dq, lut);
        checkCell("fastC", 2, gotBuf, wantBuf);
    }

    /* ---- exact-kernel case A: negative-then-positive s, cycling dither --
     * sQ0/stepQ sweep s = sq>>8 through -2,-1,0,1,2,3,4,5 (exercises the
     * contrast gather's SRAI sign handling the same way fastB exercises
     * the palette gather's). dx2 gives every pixel a distinct, increasing
     * value (env stays real and nonzero, so MULL sees genuine magnitudes);
     * ditherRow gives 4 distinct values that repeat at pixels 4-7 (k&3),
     * matching production's period-4 dither exactly, so a wrong phase
     * (using k instead of k&3, or an off-by-one in the cycle) shows up as a
     * mismatch at pixel 4 even though pixels 0-3 already passed. */
    {
        int32_t sQ0 = -2 * 256;
        int32_t stepQ = 256;
        int32_t envBase = 256;
        uint8_t dx2[8];
        dx2[0] = 0;
        dx2[1] = 16;
        dx2[2] = 32;
        dx2[3] = 48;
        dx2[4] = 64;
        dx2[5] = 80;
        dx2[6] = 96;
        dx2[7] = 112;
        int32_t ditherRow[4];
        ditherRow[0] = 3407616;
        ditherRow[1] = 3669056;
        ditherRow[2] = 3930592;
        ditherRow[3] = 4191224;
        silkExactCell8Asm(gotBuf, sQ0, stepQ, envBase, dx2, ditherRow, lut);
        silkExactCell8Ref(wantBuf, sQ0, stepQ, envBase, dx2, ditherRow, lut);
        checkCell("exactA", 3, gotBuf, wantBuf);
    }

    /* ---- exact-kernel case B: production-scale magnitudes --------------
     * s ranges over a realistic slice of contrastLUT's domain (s in
     * [1400,1407], well inside [0,3072]); dx2 spans env's full [0,256]
     * documented range across the 8 pixels; ditherRow spans its full
     * documented [PALETTE_REAL_OFF<<16 - 52224, +45696] range across its 4
     * entries, same construction as fastC above. This is the combination
     * glow=0/glow=100 param extremes push toward (steep curvature -> more
     * EXACT cells), so it is the closest this synthetic test gets to "the
     * param extremes as they reach the kernel" for this path. */
    {
        int32_t sQ0 = 1400 << 8;
        int32_t stepQ = 32; /* s advances by 1 every 8 pixels: gentle, in-range drift */
        int32_t envBase = 256;
        const int32_t paletteRealOffQ16 = 3328 << 16;
        uint8_t dx2[8];
        dx2[0] = 0;
        dx2[1] = 32;
        dx2[2] = 64;
        dx2[3] = 96;
        dx2[4] = 128;
        dx2[5] = 160;
        dx2[6] = 192;
        dx2[7] = 224;
        int32_t ditherRow[4];
        ditherRow[0] = paletteRealOffQ16 - 52224;
        ditherRow[1] = paletteRealOffQ16 - 17408;
        ditherRow[2] = paletteRealOffQ16 + 17408;
        ditherRow[3] = paletteRealOffQ16 + 45696;
        silkExactCell8Asm(gotBuf, sQ0, stepQ, envBase, dx2, ditherRow, lut);
        silkExactCell8Ref(wantBuf, sQ0, stepQ, envBase, dx2, ditherRow, lut);
        checkCell("exactB", 4, gotBuf, wantBuf);
    }

    uart_puts("GM_QEMUBENCH_PIE: mismatches=");
    uart_put_dec(mismatches);
    uart_puts("\n");

    if (mismatches == 0) {
        uart_puts("GM_QEMUBENCH_PIE: PASS silkFastCell8Asm+silkExactCell8Asm bit-exact vs "
                  "silkFastCell8Ref+silkExactCell8Ref reference (5 cases: fastA fastB fastC exactA exactB, "
                  "8 pixels each, spanning negative/zero/positive index territory and production-scale "
                  "magnitudes)\n");
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
