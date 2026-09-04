/* Real-Xtensa execution check for AnimRipples.cpp's two hand-written
 * kernels (bganim asm pass, 2026-09-04, see that file's header for the
 * full design rationale and the accumulation-order equivalence proof this
 * relies on):
 *
 *   fillTileSpanPie   -- Kernel A, PIE vector fill of the background
 *                        tile[x&3] dither pattern.
 *   accumulateBandAsm -- Kernel B, hand-scheduled scalar Xtensa FPU code
 *                        that accumulates one ring-crossing band's
 *                        contribution into a height buffer and advances
 *                        its sqrt-free integer distance tracker.
 *
 * Both kernel bodies below are transcribed verbatim (same instructions,
 * same operand registers via the same named-operand asm, same immediates)
 * from AnimRipples.cpp -- not regenerated or simplified -- so a PASS here
 * is direct evidence about the exact code that file contains, following
 * tests/blend_group8/main.c's precedent in this directory. They cannot be
 * #included directly: the real functions live in an anonymous namespace
 * inside a translation unit that pulls in bganim::alloc/themeRGB/etc,
 * none of which exist in this freestanding, no-libc, no-libm harness.
 *
 * Both kernels need CPENABLE set before their first FPU (Kernel B) or PIE
 * (Kernel A) instruction executes under bare-metal QEMU (AnimRipples.cpp's
 * header explains why: a bare add.s probe double-faulted -- repeated jumps
 * to the DoubleException vector -- until `movi a4,0xff; wsr.cpenable a4;
 * rsync` ran first; Kernel A's ee.vld.128.ip hit the identical fault the
 * first time this test ran). Neither kernel sets CPENABLE itself: on real
 * hardware that write would bypass the OS's lazy coprocessor-enable path,
 * which is also where a preempted task's live register state gets saved
 * before this kernel's task takes over the hardware -- self-enabling skips
 * that save and can corrupt another task's float state (see the comment on
 * accumulateBandAsm in AnimRipples.cpp). Both kernel bodies here are
 * verbatim transcriptions of that CPENABLE-free production code, so this
 * test does the enabling itself, once, in main() below -- the harness has
 * no OS and no other task to corrupt, so it is the one place this write
 * belongs.
 *
 * No OS, no libc, no libm: prints over UART0 by writing its FIFO register
 * directly (ESP32-S3 UART0 base 0x60000000), same as every other test in
 * this directory. The C reference implementations below deliberately avoid
 * fabsf() and any other libm call (a ternary stands in for fabsf) because
 * -fno-builtin (see ../../build.sh) turns such calls into real, unresolved
 * symbols in this freestanding link rather than inlining them.
 */
#include <stdint.h>

/* This target has no hardware float divide (__divsf3 would be a libgcc
 * softfloat call, and this freestanding link has no libgcc -- see
 * ../../build.sh's -nostdlib), and -fno-builtin turns even a fixed 4-byte
 * __builtin_memcpy into a real call to a `memcpy` symbol that does not
 * exist here at -O1. Every division below is between two COMPILE-TIME
 * literal constants (folds to a single constant, no runtime __divsf3);
 * bit-reinterpretation uses a union instead of memcpy. */
static inline uint32_t floatBits(float f) {
    union {
        float f;
        uint32_t u;
    } c;
    c.f = f;
    return c.u;
}

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
    for (int i = 28; i >= 0; i -= 4) {
        uart_putc(hex[(v >> i) & 0xF]);
    }
}
static void uart_put_int(int v) {
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

/* ---- Kernel A: verbatim transcription of fillTileSpanPie ---- */
__attribute__((noinline)) static void fillTileSpanPie(uint16_t *row, int x0, int x1, const uint16_t tile[4]) {
    int x = x0;
    const int alignedStart = (x0 + 7) & ~7;
    const int prefixEnd = alignedStart < x1 ? alignedStart : x1;
    for (; x < prefixEnd; x++) {
        row[x] = tile[x & 3];
    }
    const int n8 = (x1 - x) >> 3;
    if (n8 > 0) {
        uint16_t *wr = row + x;
        uint16_t pat[8] __attribute__((aligned(16))) = {tile[0], tile[1], tile[2], tile[3],
                                                          tile[0], tile[1], tile[2], tile[3]};
        const uint16_t *patPtr = pat;
        int n = n8;
        __asm__ volatile("ee.vld.128.ip q0, %[pp], 0\n"
                          "1:\n"
                          "ee.vst.128.ip q0, %[wr], 16\n"
                          "addi %[n], %[n], -1\n"
                          "bnez %[n], 1b\n"
                          : [wr] "+r"(wr), [n] "+r"(n)
                          : [pp] "r"(patPtr)
                          : "memory");
        x += n8 << 3;
    }
    for (; x < x1; x++) {
        row[x] = tile[x & 3];
    }
}

/* Scalar reference for Kernel A: exactly what AnimRipples.cpp calls
 * fillTileSpanRef -- the portable path bandRef() actually runs. The store
 * goes through a volatile pointer only in THIS harness: GCC's loop-to-
 * memcpy idiom recognition otherwise turns the plain loop into a real call
 * to `memcpy`, unresolved in this -nostdlib link (see the file header).
 * The real fillTileSpanRef in AnimRipples.cpp is unaffected -- production
 * firmware is not built with -fno-builtin/-ffreestanding, so that pattern
 * never fires there; this is a harness-only workaround, not a functional
 * change to what is being verified (the vector kernel's output values). */
static void fillTileSpanRef(uint16_t *row, int x0, int x1, const uint16_t tile[4]) {
    volatile uint16_t *vrow = row;
    for (int x = x0; x < x1; x++) {
        vrow[x] = tile[x & 3];
    }
}

/* ---- Kernel B: verbatim transcription of accumulateBandAsm ---- */
struct RowBand {
    float r, amp;
    int x0, x1;
    int curDx, curDist2, curR, curR2;
};

__attribute__((noinline)) static void accumulateBandAsm(float *hAccBuf, const RowBand &b, const float *cosTable,
                                                         const float *envLUT) {
    float *bufPtr = hAccBuf + b.x0;
    int n = b.x1 - b.x0 + 1;
    if (n <= 0) {
        return;
    }
    int curDx = b.curDx, curDist2 = b.curDist2, curR = b.curR, curR2 = b.curR2;
    const uint32_t ampBits = floatBits(b.amp);
    const uint32_t rBits = floatBits(b.r);
    int s0 = 0, s1 = 0;
    __asm__ volatile(
        /* No CPENABLE write here: production's accumulateBandAsm does not
         * have one (see that function's comment in AnimRipples.cpp for why
         * a kernel must never self-enable a coprocessor under a real OS --
         * it bypasses the port's save/restore of another task's live FPU
         * state). This transcription stays verbatim, so main()'s one-time
         * CPENABLE bring-up (this harness's only OS-equivalent bookkeeping,
         * and the only safe place for it since there is no other task here
         * to corrupt) covers this kernel's first FPU use instead. */
        "wfr f0, %[amp]\n"
        "wfr f1, %[rr]\n"
        "movi %[s0], 0x41500000\n" /* 13.0f (HALFW) */
        "wfr f2, %[s0]\n"
        "movi %[s0], 0x419cec4f\n" /* 19.615385f (255.0f/13.0f, ENV_SCALE) */
        "wfr f3, %[s0]\n"
        "movi %[s0], 0x3e6e4bae\n" /* 0.23271057f (6.2831853f/27.0f, WAVEFREQ) */
        "wfr f4, %[s0]\n"
        "movi %[s0], 0x4222f983\n" /* 40.7436638f (256.0f/6.2831853f, RAD_TO_TABLE) */
        "wfr f5, %[s0]\n"
        "1:\n"
        /* hAccBuf[x] loaded first, used last (add.s at the bottom): hoisted
         * here so the envLUT/cosTable gather chain below hides its load
         * latency, matching the same reorder in production AnimRipples.cpp
         * accumulateBandAsm (this block is a verbatim transcription). */
        "lsi f12, %[buf], 0\n"
        "float.s f6, %[curR], 0\n"
        "sub.s f7, f6, f1\n"
        "abs.s f8, f7\n"
        "ole.s b0, f8, f2\n"
        "bf b0, 2f\n"
        "mul.s f9, f8, f3\n"
        "trunc.s %[s0], f9, 0\n"
        "addx4 %[s1], %[s0], %[envp]\n"
        "lsi f10, %[s1], 0\n"
        "mul.s f9, f7, f4\n"
        "mul.s f9, f9, f5\n"
        "trunc.s %[s0], f9, 0\n"
        "extui %[s0], %[s0], 0, 8\n"
        "addx4 %[s1], %[s0], %[cosp]\n"
        "lsi f11, %[s1], 0\n"
        "mul.s f9, f0, f11\n"
        "mul.s f9, f9, f10\n"
        "add.s f12, f12, f9\n"
        "ssi f12, %[buf], 0\n"
        "2:\n"
        "slli %[s0], %[curDx], 1\n"
        "addi %[s0], %[s0], 1\n"
        "add %[curDist2], %[curDist2], %[s0]\n"
        "addi %[curDx], %[curDx], 1\n"
        "3:\n"
        "slli %[s0], %[curR], 1\n"
        "addi %[s0], %[s0], 1\n"
        "add %[s1], %[curR2], %[s0]\n"
        "blt %[curDist2], %[s1], 4f\n"
        "mov %[curR2], %[s1]\n"
        "addi %[curR], %[curR], 1\n"
        "j 3b\n"
        "4:\n"
        "bge %[curDist2], %[curR2], 5f\n"
        "slli %[s0], %[curR], 1\n"
        "addi %[s0], %[s0], -1\n"
        "sub %[curR2], %[curR2], %[s0]\n"
        "addi %[curR], %[curR], -1\n"
        "j 4b\n"
        "5:\n"
        "addi %[buf], %[buf], 4\n"
        "addi %[n], %[n], -1\n"
        "bnez %[n], 1b\n"
        : [buf] "+r"(bufPtr), [n] "+r"(n), [curDx] "+r"(curDx), [curDist2] "+r"(curDist2), [curR] "+r"(curR),
          [curR2] "+r"(curR2), [s0] "+r"(s0), [s1] "+r"(s1)
        : [amp] "r"(ampBits), [rr] "r"(rBits), [envp] "r"(envLUT), [cosp] "r"(cosTable)
        : "memory", "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10", "f11", "f12", "b0");
}

/* Portable reference for Kernel B: same formula as AnimRipples.cpp's
 * accumulateBandRef, with fabsf replaced by a ternary (see file header). */
static const float kHalfw = 13.0f;
static const float kEnvScale = 255.0f / 13.0f;
static const float kWaveFreq = 6.2831853f / 27.0f;
static const float kRadToTable = 256.0f / 6.2831853f;

static void accumulateBandRef(float *hAccBuf, const RowBand &b, const float *cosTable, const float *envLUT) {
    int curDx = b.curDx, curDist2 = b.curDist2, curR = b.curR, curR2 = b.curR2;
    for (int x = b.x0; x <= b.x1; x++) {
        const float delta = (float)curR - b.r;
        const float ad = delta < 0.0f ? -delta : delta;
        if (ad <= kHalfw) {
            const int idxEnv = (int)(ad * kEnvScale);
            const float rad = delta * kWaveFreq;
            const int idxCos = (int)(rad * kRadToTable) & 255;
            hAccBuf[x] += b.amp * cosTable[idxCos] * envLUT[idxEnv];
        }
        curDist2 += 2 * curDx + 1;
        curDx += 1;
        while (curDist2 >= curR2 + 2 * curR + 1) {
            curR2 += 2 * curR + 1;
            curR++;
        }
        while (curDist2 < curR2) {
            curR2 -= 2 * curR - 1;
            curR--;
        }
    }
}

/* envLUT[256] = 1 - (i/255)^2, computed with a loop (no libm needed).
 * cosTable[256] is a real 256-sample cosine table over one period,
 * precomputed host-side (Python's math.cos) and embedded as literals --
 * cosf() is not available in this freestanding link, and the CONTENT of
 * the table does not need to match BgAnimCommon's g_cosTable exactly for
 * this test (which only checks the kernel against its own C reference on
 * whatever table both are handed); using a real cosine table rather than
 * an arbitrary pattern keeps the values realistic. */
static float envLUT[256];
static const float cosTable[256] __attribute__((aligned(16))) = {
    1.0f, 0.999698819f, 0.998795456f, 0.997290457f, 0.995184727f, 0.992479535f, 0.98917651f, 0.985277642f,
    0.98078528f, 0.97570213f, 0.970031253f, 0.963776066f, 0.956940336f, 0.949528181f, 0.941544065f, 0.932992799f,
    0.923879533f, 0.914209756f, 0.903989293f, 0.893224301f, 0.881921264f, 0.870086991f, 0.85772861f, 0.844853565f,
    0.831469612f, 0.817584813f, 0.803207531f, 0.788346428f, 0.773010453f, 0.757208847f, 0.740951125f, 0.724247083f,
    0.707106781f, 0.689540545f, 0.671558955f, 0.653172843f, 0.634393284f, 0.615231591f, 0.595699304f, 0.575808191f,
    0.555570233f, 0.53499762f, 0.514102744f, 0.492898192f, 0.471396737f, 0.44961133f, 0.427555093f, 0.405241314f,
    0.382683432f, 0.359895037f, 0.336889853f, 0.31368174f, 0.290284677f, 0.266712757f, 0.24298018f, 0.21910124f,
    0.195090322f, 0.170961889f, 0.146730474f, 0.122410675f, 0.0980171403f, 0.0735645636f, 0.0490676743f, 0.0245412285f,
    6.123234e-17f, -0.0245412285f, -0.0490676743f, -0.0735645636f, -0.0980171403f, -0.122410675f, -0.146730474f, -0.170961889f,
    -0.195090322f, -0.21910124f, -0.24298018f, -0.266712757f, -0.290284677f, -0.31368174f, -0.336889853f, -0.359895037f,
    -0.382683432f, -0.405241314f, -0.427555093f, -0.44961133f, -0.471396737f, -0.492898192f, -0.514102744f, -0.53499762f,
    -0.555570233f, -0.575808191f, -0.595699304f, -0.615231591f, -0.634393284f, -0.653172843f, -0.671558955f, -0.689540545f,
    -0.707106781f, -0.724247083f, -0.740951125f, -0.757208847f, -0.773010453f, -0.788346428f, -0.803207531f, -0.817584813f,
    -0.831469612f, -0.844853565f, -0.85772861f, -0.870086991f, -0.881921264f, -0.893224301f, -0.903989293f, -0.914209756f,
    -0.923879533f, -0.932992799f, -0.941544065f, -0.949528181f, -0.956940336f, -0.963776066f, -0.970031253f, -0.97570213f,
    -0.98078528f, -0.985277642f, -0.98917651f, -0.992479535f, -0.995184727f, -0.997290457f, -0.998795456f, -0.999698819f,
    -1.0f, -0.999698819f, -0.998795456f, -0.997290457f, -0.995184727f, -0.992479535f, -0.98917651f, -0.985277642f,
    -0.98078528f, -0.97570213f, -0.970031253f, -0.963776066f, -0.956940336f, -0.949528181f, -0.941544065f, -0.932992799f,
    -0.923879533f, -0.914209756f, -0.903989293f, -0.893224301f, -0.881921264f, -0.870086991f, -0.85772861f, -0.844853565f,
    -0.831469612f, -0.817584813f, -0.803207531f, -0.788346428f, -0.773010453f, -0.757208847f, -0.740951125f, -0.724247083f,
    -0.707106781f, -0.689540545f, -0.671558955f, -0.653172843f, -0.634393284f, -0.615231591f, -0.595699304f, -0.575808191f,
    -0.555570233f, -0.53499762f, -0.514102744f, -0.492898192f, -0.471396737f, -0.44961133f, -0.427555093f, -0.405241314f,
    -0.382683432f, -0.359895037f, -0.336889853f, -0.31368174f, -0.290284677f, -0.266712757f, -0.24298018f, -0.21910124f,
    -0.195090322f, -0.170961889f, -0.146730474f, -0.122410675f, -0.0980171403f, -0.0735645636f, -0.0490676743f, -0.0245412285f,
    -1.8369702e-16f, 0.0245412285f, 0.0490676743f, 0.0735645636f, 0.0980171403f, 0.122410675f, 0.146730474f, 0.170961889f,
    0.195090322f, 0.21910124f, 0.24298018f, 0.266712757f, 0.290284677f, 0.31368174f, 0.336889853f, 0.359895037f,
    0.382683432f, 0.405241314f, 0.427555093f, 0.44961133f, 0.471396737f, 0.492898192f, 0.514102744f, 0.53499762f,
    0.555570233f, 0.575808191f, 0.595699304f, 0.615231591f, 0.634393284f, 0.653172843f, 0.671558955f, 0.689540545f,
    0.707106781f, 0.724247083f, 0.740951125f, 0.757208847f, 0.773010453f, 0.788346428f, 0.803207531f, 0.817584813f,
    0.831469612f, 0.844853565f, 0.85772861f, 0.870086991f, 0.881921264f, 0.893224301f, 0.903989293f, 0.914209756f,
    0.923879533f, 0.932992799f, 0.941544065f, 0.949528181f, 0.956940336f, 0.963776066f, 0.970031253f, 0.97570213f,
    0.98078528f, 0.985277642f, 0.98917651f, 0.992479535f, 0.995184727f, 0.997290457f, 0.998795456f, 0.999698819f,
};

static int g_bad = 0;

static void checkFillTile(int caseId, int x0, int x1, int rowLen) {
    static uint16_t rowA[64] __attribute__((aligned(16)));
    static uint16_t rowB[64] __attribute__((aligned(16)));
    const uint16_t tile[4] = {0x1111, 0x2222, 0x3333, 0x4444};
    // volatile: defeats GCC's loop-to-memset idiom recognition, which would
    // otherwise emit a real call to `memset` -- a symbol this freestanding,
    // -nostdlib link has no definition for (see the file header).
    for (int i = 0; i < rowLen; i++) {
        (reinterpret_cast<volatile uint16_t *>(rowA))[i] = 0xBEEF;
        (reinterpret_cast<volatile uint16_t *>(rowB))[i] = 0xBEEF;
    }
    fillTileSpanPie(rowA, x0, x1, tile);
    fillTileSpanRef(rowB, x0, x1, tile);
    int mism = 0;
    for (int i = 0; i < rowLen; i++) {
        if (rowA[i] != rowB[i]) {
            mism++;
        }
    }
    uart_puts("GM_QEMUBENCH_PIE: fillTile case=");
    uart_put_int(caseId);
    uart_puts(" x0=");
    uart_put_int(x0);
    uart_puts(" x1=");
    uart_put_int(x1);
    uart_puts(" mismatches=");
    uart_put_int(mism);
    uart_puts("\n");
    g_bad += mism;
}

static void checkAccumulate(int caseId, const RowBand &b) {
    static float bufA[64] __attribute__((aligned(16)));
    static float bufB[64] __attribute__((aligned(16)));
    for (int i = 0; i < 64; i++) { // volatile: see checkFillTile's comment on the memset idiom
        (reinterpret_cast<volatile float *>(bufA))[i] = 0.0f;
        (reinterpret_cast<volatile float *>(bufB))[i] = 0.0f;
    }
    accumulateBandAsm(bufA, b, cosTable, envLUT);
    accumulateBandRef(bufB, b, cosTable, envLUT);
    int mism = 0;
    uint32_t firstBadBitsA = 0, firstBadBitsB = 0;
    int firstBadX = -1;
    for (int x = b.x0; x <= b.x1; x++) {
        const uint32_t ba = floatBits(bufA[x]);
        const uint32_t bb = floatBits(bufB[x]);
        if (ba != bb) {
            if (mism == 0) {
                firstBadX = x;
                firstBadBitsA = ba;
                firstBadBitsB = bb;
            }
            mism++;
        }
    }
    uart_puts("GM_QEMUBENCH_PIE: accumulateBand case=");
    uart_put_int(caseId);
    uart_puts(" x0=");
    uart_put_int(b.x0);
    uart_puts(" x1=");
    uart_put_int(b.x1);
    uart_puts(" r=");
    uart_put_hex32(floatBits(b.r));
    uart_puts(" amp=");
    uart_put_hex32(floatBits(b.amp));
    uart_puts(" curR0=");
    uart_put_int(b.curR);
    uart_puts(" mismatches=");
    uart_put_int(mism);
    if (mism > 0) {
        uart_puts(" firstBadX=");
        uart_put_int(firstBadX);
        uart_puts(" asm=");
        uart_put_hex32(firstBadBitsA);
        uart_puts(" ref=");
        uart_put_hex32(firstBadBitsB);
    }
    uart_puts("\n");
    g_bad += mism;
}

int main(void) {
    /* Harness-only coprocessor bring-up: bare-metal QEMU resets CPENABLE to 0
     * (no FreeRTOS to lazily enable a coprocessor on first fault, the way
     * real hardware does -- see AnimRipples.cpp's accumulateBandAsm comment
     * and SleepAnimation.cpp's scale565Oct comment for the PIE case).
     * Kernel A's fillTileSpanPie is the FIRST asm below to run and uses PIE
     * (ee.vld/vst), so without this it double-faults immediately (repeating
     * traps between the ROM DoubleException vector and this harness's
     * catchall, CCOUNT frozen at 0); accumulateBandAsm needs the same thing
     * for its FPU use. Neither kernel carries this write itself in
     * production -- on real hardware self-enabling a coprocessor bypasses
     * the OS's save/restore of whichever OTHER task currently owns that
     * register file, which this bare-metal harness has no equivalent
     * concept of (no OS, no other task, nothing to corrupt) and no lazy
     * fault-driven enable either, so something has to set CPENABLE
     * explicitly somewhere. Doing it once here, rather than inside either
     * transcribed kernel, keeps both verbatim transcriptions of the
     * CPENABLE-free production code. Setting CPENABLE=0xff once is
     * sufficient for the rest of this program: nothing clears it
     * afterward. */
    __asm__ volatile("movi a4, 0xff\n"
                      "wsr.cpenable a4\n"
                      "rsync\n" ::
                          : "a4");

    for (int i = 0; i < 256; i++) {
        const float n = i * (1.0f / 255.0f); /* reciprocal multiply, not a runtime divide */
        envLUT[i] = 1.0f - n * n;
    }

    /* ---- Kernel A: exercise prefix-only, prefix+vector+suffix, exactly
     * aligned (no prefix/suffix), and a full 16-wide span. ---- */
    checkFillTile(0, 5, 7, 16);   /* 2px, scalar prefix only, n8==0 */
    checkFillTile(1, 3, 29, 32);  /* unaligned x0, prefix + 2 vector groups + suffix */
    checkFillTile(2, 8, 24, 32);  /* already 8-aligned both ends, pure vector, no scalar */
    checkFillTile(3, 0, 16, 32);  /* whole aligned span from row start */
    checkFillTile(4, 0, 4, 16);   /* short span, x0 already aligned, no vector groups */

    /* ---- Kernel B: parameter extremes (amp 0 and near-max, r spanning
     * the window on both sides of curR, a tracker seed that forces the
     * rebracket while-loop to iterate MORE than once so the backward
     * branches (3b/4b) get real coverage, not just the common 0-1-iteration
     * path), plus idxCos negative-delta wraparound (delta<0 exercises the
     * extui-based &255 against a negative trunc.s result). ---- */
    // Single aggregate initializer (not per-element assignment): assigning
    // into an existing RowBand element from a brace-init list is what
    // triggered the memcpy idiom above for fillTileSpanRef's loop -- same
    // class of GCC pattern-recognition, same harness-only workaround.
    // clang-format off
    static const RowBand cases[7] = {
        // case 0: default-ish, ad well inside HALFW, delta positive.
        {/*r*/ 20.0f, /*amp*/ 0.5f, /*x0*/ 0, /*x1*/ 15, /*curDx*/ 0, /*curDist2*/ 400, /*curR*/ 20, /*curR2*/ 400},
        // case 1: amp == 0 (p[3]==0 edge -- glow floor still lets amp reach
        // 0 via the rise/decay envelope at very early or very late age).
        {20.0f, 0.0f, 0, 15, 0, 400, 20, 400},
        // case 2: amp near its ~0.975 ceiling (see AnimRipples.cpp's
        // CLAMP_PAD derivation for that bound), delta negative (curR < r)
        // -- exercises the abs.s negative branch and idxCos's negative-
        // delta path (trunc.s of a negative product, then extui &255).
        {30.0f, 0.975f, 10, 25, 0, 100, 10, 100},
        // case 3: r == 0 (a ring seeded at radius 0, the amp<0.008 guard
        // in frame() would normally exclude this, but the KERNEL itself
        // must not assume r>0 -- nothing in its math divides by r).
        {0.0f, 0.3f, 0, 9, 0, 0, 0, 0},
        // case 4: curDist2 seeded far ABOVE curR2 (r0 under-seeded
        // relative to dist2), forcing the first rebracket while-loop to
        // execute several iterations on pixel 0 before the tracker catches
        // up -- real coverage of the "j 3b" backward branch beyond a
        // single pass.
        {50.0f, 0.4f, 0, 5, 0, /*curDist2*/ 2500, /*curR*/ 10, /*curR2*/ 100},
        // case 5: curDist2 seeded far BELOW curR2 (mirror of case 4),
        // forcing the second while-loop's "j 4b" backward branch to
        // iterate several times too.
        {50.0f, 0.4f, 0, 5, 0, /*curDist2*/ 100, /*curR*/ 50, /*curR2*/ 2500},
        // case 6: a single-pixel band (x0==x1), the tightest loop trip count.
        {12.0f, 0.6f, 7, 7, 0, 144, 12, 144},
    };
    // clang-format on

    for (int i = 0; i < 7; i++) {
        checkAccumulate(i, cases[i]);
    }

    if (g_bad == 0) {
        uart_puts("GM_QEMUBENCH_PIE: PASS fillTileSpanPie+accumulateBandAsm bit-exact vs C references "
                   "(5 fillTile cases, 7 accumulateBand cases, 0/all mismatches)\n");
    } else {
        uart_puts("GM_QEMUBENCH_PIE: FAIL total mismatches=");
        uart_put_int(g_bad);
        uart_puts("\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {
    }
}
