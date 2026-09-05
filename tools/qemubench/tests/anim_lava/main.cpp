/* Real-Xtensa execution check for lavaFinalizeQuadAsm and lavaFieldGatherAsm,
 * the two hand-written scalar Xtensa kernels added in round 6 (flag-gated,
 * OFF by default, GM_BGANIM_LAVA_ASM) of
 * src/display/ui/default/bganim/AnimLava.cpp. Both kernels are noinline,
 * take plain pointers/ints, and are transcribed instruction-for-instruction
 * into this file (mnemonics, operand names, immediates, load/store order all
 * unchanged), matching this project's established precedent (see
 * tools/qemubench/tests/anim_silk/main.c and
 * tools/qemubench/tests/anim_ember/main.c) for transcribing rather than
 * re-deriving: a PASS here is direct evidence about the exact instruction
 * sequence that file contains, not about the algorithm being reimplemented
 * correctly.
 *
 * This is not proven any other way: the host bench (tools/animbench) never
 * compiles either kernel (band() only reaches them under __XTENSA__ +
 * GM_BGANIM_LAVA_ASM, and bandRef() never calls them at all, see
 * AnimLava.cpp's round-6 file-top comment), and xtensa-asm14.sh only proves
 * the instructions assemble and that GCC's inline-asm register allocator did
 * not spill around either block, it does not prove a `loopnez` actually
 * executes the right number of times, or that the loop body's data flow is
 * correct end to end. This test does, under Espressif's qemu-system-xtensa
 * fork.
 *
 * Reference implementations (lavaFinalizeQuadRef, lavaFieldGatherRef) are a
 * direct, portable transliteration of the per-pixel expressions in
 * AnimLava.cpp's finalizeSpan() and renderRow(), not hardcoded expected
 * constants, since this test's job is to catch a transcription slip in the
 * asm (wrong register, wrong shift amount, wrong byte offset, a lane
 * computed in the wrong order), not to re-verify the algorithm itself (the
 * host bench's golden comparison already covers that, bit for bit, via
 * bandRef()). Kernel and reference run on the SAME synthetic inputs and are
 * compared value by value at runtime, so no expected-value arithmetic needed
 * to be done by hand, only the inputs did.
 *
 * No OS, no libc: prints over UART0 by writing its FIFO register directly
 * (ESP32-S3 UART0 base 0x60000000), same as every other test in this
 * directory.
 */
#include <cstdint>

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

static void uart_put_hex32(uint32_t v) {
    static const char hex[] = "0123456789abcdef";
    for (int shift = 28; shift >= 0; shift -= 4) {
        uart_putc(hex[(v >> shift) & 0xF]);
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

/* ===== lavaFinalizeQuadAsm: verbatim transcription from AnimLava.cpp ===== */
static void lavaFinalizeQuadAsm(uint16_t *out, const int32_t *field, int32_t d0, int32_t d1, int32_t d2, int32_t d3,
                                 const uint16_t *lut, int32_t nQuads) {
    int32_t t0, t1, t2, t3, cap, zero;
    __asm__ volatile("movi    %[cap], 255\n"
                      "movi    %[zero], 0\n"
                      "loopnez %[n], 1f\n"
                      "l32i    %[t3], %[field], 12\n" /* fieldRow[x+3] */
                      "l32i    %[t1], %[field], 4\n"  /* fieldRow[x+1] */
                      "l32i    %[t2], %[field], 8\n"  /* fieldRow[x+2] */
                      "l32i    %[t0], %[field], 0\n"  /* fieldRow[x] */
                      "min     %[t3], %[cap], %[t3]\n"
                      "min     %[t1], %[cap], %[t1]\n"
                      "min     %[t2], %[cap], %[t2]\n"
                      "add     %[t3], %[t3], %[d3]\n"
                      "add     %[t1], %[t1], %[d1]\n"
                      "min     %[t0], %[cap], %[t0]\n"
                      "add     %[t2], %[t2], %[d2]\n"
                      "min     %[t3], %[t3], %[cap]\n"
                      "min     %[t1], %[t1], %[cap]\n"
                      "add     %[t0], %[t0], %[d0]\n"
                      "min     %[t2], %[t2], %[cap]\n"
                      "max     %[t3], %[t3], %[zero]\n"
                      "max     %[t1], %[t1], %[zero]\n"
                      "min     %[t0], %[t0], %[cap]\n"
                      "max     %[t2], %[t2], %[zero]\n"
                      "extui   %[t3], %[t3], 0, 16\n"
                      "extui   %[t1], %[t1], 0, 16\n"
                      "max     %[t0], %[t0], %[zero]\n"
                      "extui   %[t2], %[t2], 0, 16\n"
                      "addx2   %[t3], %[t3], %[lut]\n"
                      "addx2   %[t1], %[t1], %[lut]\n"
                      "extui   %[t0], %[t0], 0, 16\n"
                      "l16ui   %[t3], %[t3], 0\n"
                      "l16ui   %[t1], %[t1], 0\n"
                      "addx2   %[t2], %[t2], %[lut]\n"
                      "addx2   %[t0], %[t0], %[lut]\n"
                      "l16ui   %[t2], %[t2], 0\n"
                      "l16ui   %[t0], %[t0], 0\n"
                      "slli    %[t1], %[t1], 16\n"
                      "slli    %[t3], %[t3], 16\n"
                      "or      %[t1], %[t1], %[t0]\n"
                      "or      %[t3], %[t3], %[t2]\n"
                      "s32i    %[t1], %[out], 0\n"
                      "s32i    %[t3], %[out], 4\n"
                      "addi    %[field], %[field], 16\n"
                      "addi    %[out], %[out], 8\n"
                      "1:\n"
                      : [out] "+r"(out), [field] "+r"(field), [t0] "=&r"(t0), [t1] "=&r"(t1), [t2] "=&r"(t2),
                        [t3] "=&r"(t3), [cap] "=&r"(cap), [zero] "=&r"(zero)
                      : [d0] "r"(d0), [d1] "r"(d1), [d2] "r"(d2), [d3] "r"(d3), [lut] "r"(lut), [n] "r"(nQuads)
                      : "memory");
}

/* Portable reference for lavaFinalizeQuadAsm: for each of the nQuads groups
 * of 4 pixels, out[k] = paletteLUT[clamp(min(field[k], 255) + d[k], 0, 255)],
 * the exact per-pixel expression from finalizeSpan()'s 4-wide loop,
 * transliterated straight across (min-then-clamp order matches what GCC's
 * own compile of that loop does; see AnimLava.cpp's kernel comment). */
static void lavaFinalizeQuadRef(uint16_t *out, const int32_t *field, int32_t d0, int32_t d1, int32_t d2, int32_t d3,
                                 const uint16_t *lut, int32_t nQuads) {
    for (int32_t q = 0; q < nQuads; q++) {
        const int32_t *f = field + q * 4;
        uint16_t *o = out + q * 4;
        int32_t idx0 = f[0] < 255 ? f[0] : 255;
        idx0 += d0;
        idx0 = idx0 < 0 ? 0 : (idx0 > 255 ? 255 : idx0);
        int32_t idx1 = f[1] < 255 ? f[1] : 255;
        idx1 += d1;
        idx1 = idx1 < 0 ? 0 : (idx1 > 255 ? 255 : idx1);
        int32_t idx2 = f[2] < 255 ? f[2] : 255;
        idx2 += d2;
        idx2 = idx2 < 0 ? 0 : (idx2 > 255 ? 255 : idx2);
        int32_t idx3 = f[3] < 255 ? f[3] : 255;
        idx3 += d3;
        idx3 = idx3 < 0 ? 0 : (idx3 > 255 ? 255 : idx3);
        o[0] = lut[idx0];
        o[1] = lut[idx1];
        o[2] = lut[idx2];
        o[3] = lut[idx3];
    }
}

/* ===== lavaFieldGatherAsm: verbatim transcription from AnimLava.cpp ====== */
static void lavaFieldGatherAsm(int32_t *field, int32_t ttQ0, int32_t stepQ0, int32_t step2Q, const int16_t *lut,
                                int32_t n) {
    int32_t ttQ = ttQ0;
    int32_t stepQ = stepQ0;
    int32_t idx, acc;
    __asm__ volatile("loopnez %[n], 1f\n"
                      "srai    %[idx], %[ttq], 11\n" /* idx = ttQ >> LUT_SHIFT */
                      "addx2   %[idx], %[idx], %[lut]\n" /* &lut[idx] */
                      "l32i    %[acc], %[field], 0\n"    /* acc = *field */
                      "l16si   %[idx], %[idx], 0\n"      /* val = lut[idx] (sign-extend: int16_t) */
                      "add     %[ttq], %[ttq], %[stepq]\n" /* ttQ += stepQ */
                      "add     %[idx], %[acc], %[idx]\n" /* acc + val */
                      "s32i    %[idx], %[field], 0\n"    /* *field = acc + val */
                      "add     %[stepq], %[stepq], %[step2q]\n" /* stepQ += step2Q */
                      "addi    %[field], %[field], 4\n"  /* field++ */
                      "1:\n"
                      : [field] "+r"(field), [ttq] "+r"(ttQ), [stepq] "+r"(stepQ), [idx] "=&r"(idx), [acc] "=&r"(acc)
                      : [step2q] "r"(step2Q), [lut] "r"(lut), [n] "r"(n)
                      : "memory");
}

/* Portable reference for lavaFieldGatherAsm: field[i] += lut[ttQ >> 11] for
 * i=0..n-1, with ttQ stepped by stepQ and stepQ stepped by step2Q each
 * iteration, the exact per-pixel expression from renderRow()'s
 * Bresenham-LUT loop, transliterated straight across. */
static void lavaFieldGatherRef(int32_t *field, int32_t ttQ0, int32_t stepQ0, int32_t step2Q, const int16_t *lut,
                                int32_t n) {
    int32_t ttQ = ttQ0;
    int32_t stepQ = stepQ0;
    for (int32_t i = 0; i < n; i++) {
        field[i] += lut[ttQ >> 11];
        ttQ += stepQ;
        stepQ += step2Q;
    }
}

/* Shared palette LUT for every lavaFinalizeQuadAsm case below: 256 entries
 * (the kernel only ever indexes it 0..255, post-clamp), lutStorage[i] =
 * i*7+13, distinctive, monotonic, so a wrong index or a scrambled lane
 * shows up as a detectably wrong value rather than an accidental match. */
static uint16_t paletteLutStorage[256];

/* Shared field-gather LUT for every lavaFieldGatherAsm case below: 8192
 * entries laid out like AnimLava.cpp's lavaBase (a signed shifted-tt value
 * indexes it directly), lutStorage2[i] = (i-4096)*97 (wraps through
 * int16_t, deliberately), spans both negative and positive stored values,
 * unlike a monotonic-positive pattern, so an l16si/l16ui sign-extension
 * mixup on the gathered LUT value would also show up as a mismatch, not
 * just a wrong-address mixup. `lut` points to the middle of the storage so
 * lut[idx] is valid for idx in [-4096, 4095], comfortably covering every
 * idx this test drives, including the deliberately-out-of-production-range
 * ones below (same testing philosophy as anim_silk/main.c's shared LUT: push
 * further than production ever reaches, since the padding makes it safe to
 * actually dereference). */
#define GATHER_LUT_HALF 4096
static int16_t gatherLutStorage[2 * GATHER_LUT_HALF];
static const int16_t *gatherLut;

static int mismatches = 0;
static const char *firstBadKernel = nullptr;
static int firstBadCall = -1;
static int firstBadLane = -1;
static int32_t firstBadGot = 0;
static int32_t firstBadWant = 0;

static void recordMismatch(const char *kernel, int call, int lane, int32_t got, int32_t want) {
    if (firstBadKernel == nullptr) {
        firstBadKernel = kernel;
        firstBadCall = call;
        firstBadLane = lane;
        firstBadGot = got;
        firstBadWant = want;
    }
    mismatches++;
    uart_puts("  MISMATCH kernel=");
    uart_puts(kernel);
    uart_puts(" call=");
    uart_put_dec(call);
    uart_puts(" lane=");
    uart_put_dec(lane);
    uart_puts(" got=0x");
    uart_put_hex32((uint32_t)got);
    uart_puts(" want=0x");
    uart_put_hex32((uint32_t)want);
    uart_puts("\n");
}

static void checkQuadCase(int call, const uint16_t *got, const uint16_t *want, int32_t nQuads) {
    for (int32_t i = 0; i < nQuads * 4; i++) {
        if (got[i] != want[i]) {
            recordMismatch("lavaFinalizeQuadAsm", call, (int)i, got[i], want[i]);
        }
    }
}

static void checkGatherCase(int call, const int32_t *got, const int32_t *want, int32_t n) {
    for (int32_t i = 0; i < n; i++) {
        if (got[i] != want[i]) {
            recordMismatch("lavaFieldGatherAsm", call, (int)i, got[i], want[i]);
        }
    }
}

/* Buffers sized for the largest case exercised below (production width 480,
 * 120 quads / 480 gather pixels). */
static int32_t fieldBuf[480];
static uint16_t gotBuf[480];
static uint16_t wantBuf[480];
static int32_t gotField[480];
static int32_t wantField[480];

int main(void) {
    for (int i = 0; i < 256; i++) {
        paletteLutStorage[i] = (uint16_t)(i * 7 + 13);
    }
    for (int i = 0; i < 2 * GATHER_LUT_HALF; i++) {
        gatherLutStorage[i] = (int16_t)((i - GATHER_LUT_HALF) * 97);
    }
    gatherLut = gatherLutStorage + GATHER_LUT_HALF;

    int call = 0;

    /* ---- finalize case 0: minimal trip count (nQuads=1), one lane per
     * clamp scenario, lane 0 clamps to 0 (very negative dither), lane 1
     * mid-range (no clamp), lane 2 sits exactly on the field>=255 cap
     * boundary, lane 3 clamps to 255 from above (large field, large
     * positive dither). */
    {
        int32_t field[4];
        field[0] = 0;
        field[1] = 100;
        field[2] = 255;
        field[3] = 1000;
        lavaFinalizeQuadAsm(gotBuf, field, -300, 10, 0, 300, paletteLutStorage, 1);
        lavaFinalizeQuadRef(wantBuf, field, -300, 10, 0, 300, paletteLutStorage, 1);
        checkQuadCase(call++, gotBuf, wantBuf, 1);
    }

    /* ---- finalize case 1: nQuads=0 (defensive loopnez check), a span
     * with no full quad left after finalizeSpanAsm's alignment prefix must
     * leave the output untouched, not run once the way a plain `loop`
     * (without the nz) would. */
    {
        int32_t field[4];
        field[0] = 1;
        field[1] = 2;
        field[2] = 3;
        field[3] = 4;
        for (int i = 0; i < 4; i++) {
            gotBuf[i] = 0xDEAD;
        }
        lavaFinalizeQuadAsm(gotBuf, field, 1, 2, 3, 4, paletteLutStorage, 0);
        for (int i = 0; i < 4; i++) {
            if (gotBuf[i] != 0xDEAD) {
                recordMismatch("lavaFinalizeQuadAsm", call, i, gotBuf[i], 0xDEAD);
            }
        }
        call++;
    }

    /* ---- finalize case 2: production width 480 (120 quads), field
     * ramps through negative, mid-range and >255 values across the sweep
     * (f[k] = (k*37 % 600) - 50), four distinct dither constants (a
     * plausible Bayer row), so every lane of every quad sees a different
     * combination of clamp state and dither phase. */
    {
        const int32_t n = 480;
        const int32_t nQuads = n / 4;
        for (int32_t k = 0; k < n; k++) {
            fieldBuf[k] = (k * 37) % 600 - 50;
        }
        lavaFinalizeQuadAsm(gotBuf, fieldBuf, -20, -5, 5, 20, paletteLutStorage, nQuads);
        lavaFinalizeQuadRef(wantBuf, fieldBuf, -20, -5, 5, 20, paletteLutStorage, nQuads);
        checkQuadCase(call++, gotBuf, wantBuf, nQuads);
    }

    /* ---- finalize case 3: production width 240 (60 quads), same shape
     * as case 2, half the width (the panel's half-resolution path). */
    {
        const int32_t n = 240;
        const int32_t nQuads = n / 4;
        for (int32_t k = 0; k < n; k++) {
            fieldBuf[k] = (k * 41) % 500 - 40;
        }
        lavaFinalizeQuadAsm(gotBuf, fieldBuf, 7, -7, 3, -3, paletteLutStorage, nQuads);
        lavaFinalizeQuadRef(wantBuf, fieldBuf, 7, -7, 3, -3, paletteLutStorage, nQuads);
        checkQuadCase(call++, gotBuf, wantBuf, nQuads);
    }

    /* ---- gather case 0: minimal trip count (n=1). */
    {
        gotField[0] = 1000;
        wantField[0] = 1000;
        lavaFieldGatherAsm(gotField, 200 * 2048, 111, 5, gatherLut, 1);
        lavaFieldGatherRef(wantField, 200 * 2048, 111, 5, gatherLut, 1);
        checkGatherCase(call++, gotField, wantField, 1);
    }

    /* ---- gather case 1: n=0 (defensive loopnez check). */
    {
        gotField[0] = 4242;
        lavaFieldGatherAsm(gotField, 0, 0, 0, gatherLut, 0);
        if (gotField[0] != 4242) {
            recordMismatch("lavaFieldGatherAsm", call, 0, gotField[0], 4242);
        }
        call++;
    }

    /* ---- gather case 2: production width 480, full row (xlo=0, xhi=479),
     * idx = ttQ>>11 sweeping from -64 (the margin edge AnimLava.cpp's
     * LUT_MARGIN uses on the real table) up through +894, past the real
     * table's own padding (deliberately, see gatherLutStorage's comment),
     * so both the negative-index and the deep-positive-index paths through
     * SRAI/ADDX2 are exercised. step2Q=0 (no curvature) here; case 4 below
     * covers step2Q != 0. */
    {
        const int32_t n = 480;
        for (int32_t k = 0; k < n; k++) {
            gotField[k] = k * 5 + 3;
            wantField[k] = k * 5 + 3;
        }
        lavaFieldGatherAsm(gotField, -64 * 2048, 4096, 0, gatherLut, n);
        lavaFieldGatherRef(wantField, -64 * 2048, 4096, 0, gatherLut, n);
        checkGatherCase(call++, gotField, wantField, n);
    }

    /* ---- gather case 3: production width 240, full row (xlo=0, xhi=239),
     * same idx shape as case 2 (idx runs -64 to +414 over 240 steps). */
    {
        const int32_t n = 240;
        for (int32_t k = 0; k < n; k++) {
            gotField[k] = k * 7 + 11;
            wantField[k] = k * 7 + 11;
        }
        lavaFieldGatherAsm(gotField, -64 * 2048, 4096, 0, gatherLut, n);
        lavaFieldGatherRef(wantField, -64 * 2048, 4096, 0, gatherLut, n);
        checkGatherCase(call++, gotField, wantField, n);
    }

    /* ---- gather case 4: a span starting and ending at odd, non-aligned
     * offsets (xlo=7, xhi=479, n=473, neither a multiple of 4 nor of any
     * other convenient power of two), with step2Q != 0 so the stepQ +=
     * step2Q curvature term (never exercised above, since cases 2-3 use
     * step2Q=0) is also checked against the reference, alongside a starting
     * idx (sq0>>11 = -37) that is not a round number either. */
    {
        const int32_t n = 473; /* 479 - 7 + 1 */
        for (int32_t k = 0; k < n; k++) {
            gotField[k] = (k * 13) % 900 - 100;
            wantField[k] = gotField[k];
        }
        lavaFieldGatherAsm(gotField, -37 * 2048 + 500, 777, 5, gatherLut, n);
        lavaFieldGatherRef(wantField, -37 * 2048 + 500, 777, 5, gatherLut, n);
        checkGatherCase(call++, gotField, wantField, n);
    }

    uart_puts("GM_QEMUBENCH_PIE: mismatches=");
    uart_put_dec(mismatches);
    uart_puts(" cases=");
    uart_put_dec(call);
    uart_puts("\n");

    if (mismatches == 0) {
        uart_puts("GM_QEMUBENCH_PIE: PASS lavaFinalizeQuadAsm+lavaFieldGatherAsm bit-exact vs "
                  "lavaFinalizeQuadRef+lavaFieldGatherRef reference (9 cases: minimal trip count, "
                  "n=0 defensive loopnez checks, production widths 480 and 240, an odd/unaligned span "
                  "with nonzero curvature, and LUT indices spanning both clamp/margin extremes)\n");
    } else {
        uart_puts("GM_QEMUBENCH_PIE: FAIL first mismatch kernel=");
        uart_puts(firstBadKernel);
        uart_puts(" call=");
        uart_put_dec(firstBadCall);
        uart_puts(" lane=");
        uart_put_dec(firstBadLane);
        uart_puts(" got=0x");
        uart_put_hex32((uint32_t)firstBadGot);
        uart_puts(" want=0x");
        uart_put_hex32((uint32_t)firstBadWant);
        uart_puts("\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");

    for (;;) {
        /* Spin so QEMU has a stable state; nothing to return to. */
    }
}
