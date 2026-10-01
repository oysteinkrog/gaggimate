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

GM_ANIM_IRAM __attribute__((noinline)) void sundialBeamAsm(int16_t *pixels, uint16_t *work, const uint32_t *sm,
                                                           const uint32_t *rad, int g0q, int g1q, int step0, int step1,
                                                           int radialBias, int n) {
    // q = (g + 24064)*348, exactly the reference before >>16. No reduced
    // precision in the cursor. radialBias = 1466-rowRad; rad[r] includes the
    // contrast-scaled radial cubic for r clamped to 0..587. Production cp
    // stays within [-2254,264], steps within +/-178176, and |q| stays below
    // 100 million at 480 pixels, so the signed accumulators cannot overflow,
    // and q >> 16 stays within +/-1433, which is why the cursor can be
    // narrowed to a 16-bit lane after the shift.
    //
    // Eight pixels an iteration. The three table reads the reference makes per
    // pixel are the reason this used to be a scalar loop: PIE has no gather.
    // EE.LDXQ.32 is the exception, a one-lane indexed 32-bit load, so eight of
    // them plus one unzip fetch eight entries, and smooth and radialAmp are
    // held as 32-bit tables for it. That is nine instructions for what cost
    // about eight per pixel before. 62 instructions per eight pixels, 7.75 a
    // pixel, against 25 for the scalar loop this replaces, and the arithmetic
    // that follows is the same two multiplies and the same add as before.
    //
    // Register budget is the whole file: q0..q3 carry the two cursors as four
    // 32-bit lane pairs and q4..q7 are scratch, so the five loop constants are
    // read from the work area with a walking pointer that resets at the end.
    while (n > 0) {
        if (((uintptr_t)pixels & 15u) != 0 || n < 8) {
            int u0 = g0q >> 16, u1 = g1q >> 16;
            u0 = u0 < 0 ? 0 : (u0 > 256 ? 256 : u0);
            u1 = u1 < 0 ? 0 : (u1 > 256 ? 256 : u1);
            int r = radialBias + *pixels;
            r = r < 0 ? 0 : (r > 587 ? 587 : r);
            *pixels += (int16_t)((rad[r] * ((sm[u0] * sm[u1]) >> 8)) >> 8);
            pixels++;
            n--;
            g0q += step0;
            g1q += step1;
            continue;
        }
        int16_t *c16 = (int16_t *)work;
        int32_t *c32 = (int32_t *)work;
        for (int k = 0; k < 8; k++) c16[k] = 256;          // +0   u clamp ceiling
        for (int k = 0; k < 4; k++) c32[4 + k] = step0 * 8; // +16  cursor stride
        for (int k = 0; k < 4; k++) c32[8 + k] = step1 * 8; // +32
        for (int k = 0; k < 8; k++) c16[24 + k] = (int16_t)radialBias; // +48
        for (int k = 0; k < 8; k++) c16[32 + k] = 587;     // +64  radial ceiling
        for (int k = 0; k < 8; k++) {                      // +80  cursor seeds
            c32[20 + k] = g0q + step0 * k;
            c32[28 + k] = g1q + step1 * k;
        }
        const int blocks = n >> 3;
        const int32_t *seed = c32 + 20;
        const int16_t *cp = c16;
        int16_t *px = pixels;
        asm volatile("ee.vld.128.ip q0, %[seed], 16\n"
                     "ee.vld.128.ip q1, %[seed], 16\n"
                     "ee.vld.128.ip q2, %[seed], 16\n"
                     "ee.vld.128.ip q3, %[seed], 0\n"
                     "loopnez %[n], 1f\n"
                     "ssai 16\n"
                     "ee.vsr.32 q4, q0\n"
                     "ee.vsr.32 q5, q1\n"
                     "ee.vunzip.16 q4, q5\n"
                     "ee.vsr.32 q5, q2\n"
                     "ee.vsr.32 q6, q3\n"
                     "ee.vunzip.16 q5, q6\n"
                     "ee.zero.q q6\n"
                     "ee.vmax.s16 q4, q4, q6\n"
                     "ee.vmax.s16 q5, q5, q6\n"
                     "ee.vld.128.ip q6, %[cp], 16\n"
                     "ee.vmin.s16 q4, q4, q6\n"
                     "ee.vmin.s16 q5, q5, q6\n"
                     "ee.vld.128.ip q6, %[cp], 16\n"
                     "ee.vadds.s32 q0, q0, q6\n"
                     "ee.vadds.s32 q1, q1, q6\n"
                     "ee.vld.128.ip q6, %[cp], 16\n"
                     "ee.vadds.s32 q2, q2, q6\n"
                     "ee.vadds.s32 q3, q3, q6\n"
                     "ee.ldxq.32 q6, q4, %[sm], 0, 0\n"
                     "ee.ldxq.32 q6, q4, %[sm], 1, 1\n"
                     "ee.ldxq.32 q6, q4, %[sm], 2, 2\n"
                     "ee.ldxq.32 q6, q4, %[sm], 3, 3\n"
                     "ee.ldxq.32 q7, q4, %[sm], 0, 4\n"
                     "ee.ldxq.32 q7, q4, %[sm], 1, 5\n"
                     "ee.ldxq.32 q7, q4, %[sm], 2, 6\n"
                     "ee.ldxq.32 q7, q4, %[sm], 3, 7\n"
                     "ee.vunzip.16 q6, q7\n"
                     "ee.ldxq.32 q4, q5, %[sm], 0, 0\n"
                     "ee.ldxq.32 q4, q5, %[sm], 1, 1\n"
                     "ee.ldxq.32 q4, q5, %[sm], 2, 2\n"
                     "ee.ldxq.32 q4, q5, %[sm], 3, 3\n"
                     "ee.ldxq.32 q7, q5, %[sm], 0, 4\n"
                     "ee.ldxq.32 q7, q5, %[sm], 1, 5\n"
                     "ee.ldxq.32 q7, q5, %[sm], 2, 6\n"
                     "ee.ldxq.32 q7, q5, %[sm], 3, 7\n"
                     "ee.vunzip.16 q4, q7\n"
                     "ssai 8\n"
                     "ee.vmul.u16 q6, q6, q4\n"
                     "ee.vld.128.ip q4, %[px], 0\n"
                     "ee.vld.128.ip q5, %[cp], 16\n"
                     "ee.vadds.s16 q4, q4, q5\n"
                     "ee.zero.q q5\n"
                     "ee.vmax.s16 q4, q4, q5\n"
                     "ee.vld.128.ip q5, %[cp], 16\n"
                     "ee.vmin.s16 q4, q4, q5\n"
                     "ee.ldxq.32 q5, q4, %[rad], 0, 0\n"
                     "ee.ldxq.32 q5, q4, %[rad], 1, 1\n"
                     "ee.ldxq.32 q5, q4, %[rad], 2, 2\n"
                     "ee.ldxq.32 q5, q4, %[rad], 3, 3\n"
                     "ee.ldxq.32 q7, q4, %[rad], 0, 4\n"
                     "ee.ldxq.32 q7, q4, %[rad], 1, 5\n"
                     "ee.ldxq.32 q7, q4, %[rad], 2, 6\n"
                     "ee.ldxq.32 q7, q4, %[rad], 3, 7\n"
                     "ee.vunzip.16 q5, q7\n"
                     "ee.vmul.u16 q5, q5, q6\n"
                     "ee.vld.128.ip q7, %[px], 0\n"
                     "ee.vadds.s16 q7, q7, q5\n"
                     "ee.vst.128.ip q7, %[px], 16\n"
                     "addi %[cp], %[cp], -80\n"
                     "1:\n"
                     : [px] "+&r"(px), [cp] "+&r"(cp), [seed] "+&r"(seed)
                     : [sm] "r"(sm), [rad] "r"(rad), [n] "r"(blocks)
                     : "memory");
        const int done = blocks * 8;
        pixels += done;
        n -= done;
        g0q += step0 * done;
        g1q += step1 * done;
    }
}

GM_ANIM_IRAM __attribute__((noinline)) void sundialPaletteAsm(uint16_t *out, int16_t *pixels, const uint16_t *pal,
                                                              const uint32_t *pal32, uint16_t *work, int rowBase, int n) {
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
    if ((((uintptr_t)out | (uintptr_t)pixels) & 15u) == 0 && n >= 8) {
        // EE.LDXQ.32 reads eight palette entries with eight indexed loads and
        // one unzip, where the scalar pair loop below needs eleven instructions
        // for two pixels. palette32 holds the same entries, 32-bit, because the
        // instruction's index is scaled by four. Both pointers are 16-byte
        // aligned here, which EE.VLD.128.IP and EE.VST.128.IP require: they
        // clear the low four address bits without complaint.
        const int16_t *ip = pixels;
        uint16_t *vop = out;
        asm volatile("loopnez %[n], 1f\n"
                     "ee.vld.128.ip q0, %[idx], 16\n"
                     "ee.ldxq.32 q1, q0, %[pal32], 0, 0\n"
                     "ee.ldxq.32 q1, q0, %[pal32], 1, 1\n"
                     "ee.ldxq.32 q1, q0, %[pal32], 2, 2\n"
                     "ee.ldxq.32 q1, q0, %[pal32], 3, 3\n"
                     "ee.ldxq.32 q2, q0, %[pal32], 0, 4\n"
                     "ee.ldxq.32 q2, q0, %[pal32], 1, 5\n"
                     "ee.ldxq.32 q2, q0, %[pal32], 2, 6\n"
                     "ee.ldxq.32 q2, q0, %[pal32], 3, 7\n"
                     "ee.vunzip.16 q1, q2\n"
                     "ee.vst.128.ip q1, %[out], 16\n"
                     "1:\n"
                     : [idx] "+&r"(ip), [out] "+&r"(vop)
                     : [pal32] "r"(pal32), [n] "r"(n >> 3)
                     : "memory");
        const int done = (n >> 3) * 8;
        for (int x = done; x < n; x++) out[x] = pal[pixels[x]];
        return;
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
/* The beam writes 144 bytes of constants and cursor seeds into its work area
 * and the palette pass 64, so the guards below start at 8 and resume past
 * whichever the call under test uses. sm, radial and palette32 are 32-bit
 * because EE.LDXQ.32 scales its index by four. */
#define BEAM_WORK 72
#define PAL_WORK 32
static uint16_t workspace[128] __attribute__((aligned(16)));
static uint32_t sm[257] __attribute__((aligned(16)));
static uint32_t radial[588] __attribute__((aligned(16)));
static uint16_t palette[256] __attribute__((aligned(16)));
static uint32_t palette32[256] __attribute__((aligned(16)));
static uint32_t calls, lanes, failures, vecRuns, scalarRuns;
static int amplitude;

/* This QEMU fork computes ee.ldxq.32's indexed address four bytes too low
 * (espressif/qemu issue #162, reproduced on its own by tests/probe_ldxq32).
 * The beam and the palette pass gather through that instruction, so running
 * them verbatim here measures the emulator's address arithmetic and says
 * nothing about the kernel. The harness hands the vector paths a table
 * pointer one word high, which cancels the emulator's error exactly, so
 * everything the kernel computes is still checked lane by lane: the shifts,
 * the clamps, the unzips, the multiplies, the cursor advance, the stores and
 * the guard zones. What this file cannot check is the gather address itself.
 * That is checked on silicon, by /api/debug/animtest and by the kblob rig
 * hashing the kernel's output against the shipped band().
 *
 * The bias belongs to the vector path alone. The beam's scalar path reads the
 * same pointers, so a call that takes both paths runs twice: once biased,
 * checking the lanes the vector body wrote, and once true, checking the tail.
 * The tail's inputs do not depend on the vector lanes, so the second run's
 * tail is exact. The palette pass needs no such split, because its tail reads
 * the separate 16-bit palette. */
#define QEMU_LDXQ_BIAS 1

static int clamp(int x, int a, int b) { return x < a ? a : (x > b ? b : x); }
static int cubic(int u) { return (u * u * (768 - 2 * u)) >> 16; }
static void prepare(int contrast) {
    amplitude = (60 + contrast * 88 / 100) * 16;
    for (int i = 0; i < 257; i++)
        sm[i] = (uint32_t)cubic(i);
    for (int i = 0; i < 588; i++)
        radial[i] = (uint32_t)((amplitude * cubic(i * 256 / 587)) >> 8);
    /* An injective permutation of all 256 indices. A wrong gather cannot
     * disappear inside a run of repeated theme colours. */
    for (int i = 0; i < 256; i++) {
        palette[i] = (uint16_t)(i * 251 + 37);
        palette32[i] = palette[i];
    }
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
static void beamRun(int n, int shift, int g0q, int g1q, int step0, int step1,
                    int bias, int seed, int ldxqBias, int vecLo, int vecHi,
                    int wantVec) {
    for (int i = 0; i < MAX_N + 2 * GUARD; i++)
        field[i] = (int16_t)0x5234;
    for (int i = 0; i < 128; i++)
        workspace[i] = 0x6789;
    for (int x = 0; x < n; x++)
        field[GUARD + shift + x] = (int16_t)((x * 37 + seed * 71) % 2519 - 2254);
    sundialBeamAsm(field + GUARD + shift, workspace + 8, sm + ldxqBias,
                   radial + ldxqBias, g0q, g1q, step0, step1, bias, n);
    for (int i = 0; i < MAX_N + 2 * GUARD; i++) {
        int x = i - GUARD - shift;
        if (x >= 0 && x < n) {
            int isVec = x >= vecLo && x < vecHi;
            if (isVec != wantVec)
                continue; /* written by the path this run is not checking */
        }
        if (field[i] != expected[i])
            bad("beam", i, field[i], expected[i]);
    }
    for (int i = 0; i < 128; i++)
        if ((i < 8 || i >= 8 + BEAM_WORK) && workspace[i] != 0x6789)
            bad("work", i, workspace[i], 0x6789);
}

static void beamCase(int n, int shift, int g0q, int g1q, int step0, int step1,
                     int bias, int seed) {
    calls++;
    for (int i = 0; i < MAX_N + 2 * GUARD; i++)
        expected[i] = (int16_t)0x5234;
    for (int x = 0; x < n; x++)
        expected[GUARD + shift + x] =
            (int16_t)((x * 37 + seed * 71) % 2519 - 2254);
    beamRef(expected + GUARD + shift, n, g0q, g1q, step0, step1, bias);
    /* The kernel walks single pixels until its cursor is 16-byte aligned,
     * then takes whole blocks of eight, then walks the tail. Eight pixels are
     * sixteen bytes, so alignment holds once reached: this is the exact lane
     * split, and getting it wrong is how the first version of this harness
     * blamed the kernel for a case it had mislabelled. */
    int vecLo = 0;
    while (vecLo < n && (((uintptr_t)(field + GUARD + shift + vecLo) & 15u) != 0))
        vecLo++;
    int vecHi = vecLo;
    if (n - vecLo >= 8)
        vecHi = vecLo + ((n - vecLo) >> 3) * 8;
    if (vecHi > vecLo) {
        beamRun(n, shift, g0q, g1q, step0, step1, bias, seed, QEMU_LDXQ_BIAS,
                vecLo, vecHi, 1);
        vecRuns++;
    }
    if (vecHi - vecLo < n) {
        beamRun(n, shift, g0q, g1q, step0, step1, bias, seed, 0, vecLo, vecHi,
                0);
        scalarRuns++;
    }
    lanes += n;
}

static void rowCase(int n, int shift, int rowBase, int seed, int extreme) {
    calls++;
    for (int i = 0; i < MAX_N + 2 * GUARD; i++) {
        field[i] = expected[i] = (int16_t)0x5234;
        output[i] = 0x4567;
    }
    for (int i = 0; i < 128; i++)
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
                      palette32 + QEMU_LDXQ_BIAS, workspace + 8, rowBase, n);
    for (int i = 0; i < MAX_N + 2 * GUARD; i++) {
        int x = i - GUARD - shift;
        int want =
            x >= 0 && x < n
                ? palette[clamp((expected[GUARD + x] + rowBase) >> 4, 4, 255)]
                : 0x4567;
        if (output[i] != want)
            bad("palette", i, output[i], want);
    }
    for (int i = 0; i < 128; i++)
        if ((i < 8 || i >= 8 + PAL_WORK) && workspace[i] != 0x6789)
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
        uart_puts(" beamvec=");
        uart_uint(vecRuns);
        uart_puts(" beamscalar=");
        uart_uint(scalarRuns);
        uart_puts(" mismatches=0; Q8 pairs, radial range, contrast 0/25/100, "
                  "steps -512..512, widths 0..480, alignments and guards; "
                  "gather addresses proved on silicon, not here\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {
    }
}
