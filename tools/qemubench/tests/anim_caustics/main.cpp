/* QEMU self-test for causticsRowKernel, the Xtensa kernel AnimCaustics.cpp's
 * device band() dispatches to for every row (asm-caustics pass, 2026-09-04):
 * a scalar, hand-scheduled 4-wide span loop (three DDS sine-wave gathers,
 * coarse-grid interpolation, branchless abs, then the two-stage
 * shapeLUT -> palette gather) using every one of the 14 usable Xtensa
 * registers with zero spills -- see the kernel's own header comment in
 * AnimCaustics.cpp for the register budget and the instruction-by-instruction
 * reasoning this test exists to back up with real execution, not just static
 * analysis of the assembly.
 *
 * The instruction sequence below (mnemonics, operand registers via the same
 * %[name] roles, ssai/imm values, load/store order) is transcribed verbatim
 * from causticsRowKernel in AnimCaustics.cpp, not regenerated or simplified
 * -- same reasoning tests/anim_lava/main.cpp and tests/blend_row/main.c give
 * for doing the same with their own kernels: AnimCaustics.cpp pulls in
 * BgAnim.h/BgAnimCommon.h (ESP-IDF heap_caps_*, Arduino.h, the bganim
 * namespace, PSRAM allocators) that this freestanding, no-libc harness
 * cannot link against. If AnimCaustics.cpp's kernel changes, this file must
 * be updated to match; a stale copy would falsely PASS its own
 * now-irrelevant old sequence, so the source of truth is always
 * AnimCaustics.cpp itself -- this is a check on that file's arithmetic, not
 * a replacement for reading it.
 *
 * causticsRowRef() below is a fresh, independent scalar transcription of the
 * SAME algorithm (matching AnimCaustics.cpp's bandRef span loop, not copied
 * from the asm), so a PASS means the hand-scheduled instruction sequence
 * computes the same thing as the specification, not that two copies of the
 * same mistake agree.
 *
 * No OS, no libc: prints over UART0 by writing its FIFO register directly
 * (ESP32-S3 UART0 base 0x60000000), same as every other test in this
 * directory.
 */
#include <cstdint>

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

static void uart_put_dec(int v) {
    if (v < 0) {
        uart_putc('-');
        v = static_cast<int>(0u - static_cast<unsigned>(v));
    }
    char buf[12];
    int n = 0;
    if (v == 0) {
        buf[n++] = '0';
    }
    while (v > 0) {
        buf[n++] = static_cast<char>('0' + (v % 10));
        v /= 10;
    }
    while (n > 0) {
        uart_putc(buf[--n]);
    }
}

/* ------------------------------------------------------------------------
 * Verbatim transcription of causticsRowKernel (AnimCaustics.cpp). Same
 * signature, same asm string, same operand roles.
 * ------------------------------------------------------------------------ */
__attribute__((noinline)) static void causticsRowKernel(uint16_t *row, const int16_t *lut, const uint8_t *shapeLUT,
                                                         const uint16_t *rgbRowBase, uint32_t phase0, uint32_t phase1,
                                                         uint32_t phase2, uint32_t step0x4, uint32_t step1x4,
                                                         uint32_t step2x4, int32_t sumCur0, int nSpans) {
    int32_t val = sumCur0;
    int32_t cnt = nSpans; // loop trip count first, then reused as sumNext
    int32_t stepI, t;     // pure scratch, no meaningful value on entry
    asm volatile("loop %[cnt], 2f\n"
                 // --- span-level: advance phase, gather the exact sum at the next span ---
                 "add %[phase0], %[phase0], %[step0]\n"
                 "extui %[t], %[phase0], 22, 10\n"
                 "addx2 %[t], %[t], %[lut]\n"
                 "l16si %[cnt], %[t], 0\n"              // cnt = val0
                 "add %[phase1], %[phase1], %[step1]\n" // filler: val0's load-use gap
                 "extui %[t], %[phase1], 22, 10\n"
                 "addx2 %[t], %[t], %[lut]\n"
                 "l16si %[stepI], %[t], 0\n"            // stepI = val1
                 "add %[phase2], %[phase2], %[step2]\n" // filler: val1's load-use gap
                 "extui %[t], %[phase2], 22, 10\n"
                 "addx2 %[t], %[t], %[lut]\n"
                 "l16si %[t], %[t], 0\n"                  // t = val2 (no filler slot; see AnimCaustics.cpp)
                 "add %[cnt], %[cnt], %[stepI]\n"         // cnt = val0+val1 (both loaded long enough ago)
                 "add %[cnt], %[cnt], %[t]\n"             // cnt = sumNext (pays val2's stall)
                 "sub %[stepI], %[cnt], %[val]\n"         // stepI = sumNext - sumCur (ALU->ALU, no stall)
                 "srai %[stepI], %[stepI], 2\n"           // stepI = stepInterp
                 // --- pixel 0 (val == sumCur, unmodified so far) ---
                 "abs %[t], %[val]\n"
                 "add %[t], %[t], %[shapeLUT]\n"
                 "l8ui %[t], %[t], 0\n"           // t = shading0
                 "add %[val], %[val], %[stepI]\n" // val = val_p1, filler for shading0's load-use gap
                 "addx8 %[t], %[t], %[rgbBase]\n"
                 "l16ui %[t], %[t], 0\n" // t = pixel0 color
                 "s16i %[t], %[row], 0\n"
                 // --- pixel 1 (val == val_p1) ---
                 "abs %[t], %[val]\n"
                 "add %[t], %[t], %[shapeLUT]\n"
                 "l8ui %[t], %[t], 0\n"
                 "add %[val], %[val], %[stepI]\n" // val = val_p2
                 "addx8 %[t], %[t], %[rgbBase]\n"
                 "l16ui %[t], %[t], 2\n"
                 "s16i %[t], %[row], 2\n"
                 // --- pixel 2 (val == val_p2) ---
                 "abs %[t], %[val]\n"
                 "add %[t], %[t], %[shapeLUT]\n"
                 "l8ui %[t], %[t], 0\n"
                 "add %[val], %[val], %[stepI]\n" // val = val_p3
                 "addx8 %[t], %[t], %[rgbBase]\n"
                 "l16ui %[t], %[t], 4\n"
                 "s16i %[t], %[row], 4\n"
                 // --- pixel 3 (val == val_p3) ---
                 "abs %[t], %[val]\n"
                 "add %[t], %[t], %[shapeLUT]\n"
                 "l8ui %[t], %[t], 0\n"
                 "or %[val], %[cnt], %[cnt]\n" // val = sumNext for the next span, filler
                 "addx8 %[t], %[t], %[rgbBase]\n"
                 "l16ui %[t], %[t], 6\n"
                 "s16i %[t], %[row], 6\n"
                 "addi %[row], %[row], 8\n"
                 "2:\n"
                 : [row] "+r"(row), [phase0] "+r"(phase0), [phase1] "+r"(phase1), [phase2] "+r"(phase2),
                   [val] "+r"(val), [cnt] "+r"(cnt), [stepI] "=&r"(stepI), [t] "=&r"(t)
                 : [lut] "r"(lut), [shapeLUT] "r"(shapeLUT), [rgbBase] "r"(rgbRowBase), [step0] "r"(step0x4),
                   [step1] "r"(step1x4), [step2] "r"(step2x4)
                 : "memory");
}

/* ------------------------------------------------------------------------
 * Independent scalar C++ reference, matching AnimCaustics.cpp's bandRef span
 * loop (PHASE_SHIFT=22, SIN_N=1024, GRID=4, rgbLUT stride 4). Written fresh
 * from the algorithm, not derived from the asm above.
 * ------------------------------------------------------------------------ */
static void causticsRowRef(uint16_t *row, const int16_t *lut, const uint8_t *shapeLUT, const uint16_t *rgbRowBase,
                            uint32_t phase0, uint32_t phase1, uint32_t phase2, uint32_t step0x4, uint32_t step1x4,
                            uint32_t step2x4, int32_t sumCur0, int nSpans) {
    uint32_t p0 = phase0, p1 = phase1, p2 = phase2;
    int32_t sumCur = sumCur0;
    for (int span = 0; span < nSpans; span++) {
        const uint32_t np0 = p0 + step0x4;
        const uint32_t np1 = p1 + step1x4;
        const uint32_t np2 = p2 + step2x4;
        const int32_t sumNext = lut[(np0 >> 22) & 1023] + lut[(np1 >> 22) & 1023] + lut[(np2 >> 22) & 1023];
        const int32_t stepInterp = (sumNext - sumCur) >> 2;

        int32_t val = sumCur;
        for (int k = 0; k < 4; k++) {
            const int32_t m = val >> 31;
            const int32_t mag = (val ^ m) - m;
            row[span * 4 + k] = rgbRowBase[shapeLUT[mag] * 4 + k];
            val += stepInterp;
        }

        p0 = np0;
        p1 = np1;
        p2 = np2;
        sumCur = sumNext;
    }
}

/* ------------------------------------------------------------------------
 * Synthetic fixtures. lut/shapeLUT/rgbRowBase are deterministic ramps (not
 * real sine/theme data -- the kernel's arithmetic doesn't care what the
 * tables mean, only that every load lands in range and every value the
 * chain can produce is exercised), sized exactly as production allocates
 * them (SIN_N=1024, SHAPE_N=1552, one row-phase block = 256*4 entries) so
 * every index the kernel can compute is in-bounds.
 * ------------------------------------------------------------------------ */
constexpr int SIN_N = 1024;
constexpr int SHAPE_N = 1552; // K*SIN_AMP + pad = 3*512 + 16, matches AnimCaustics.cpp
constexpr int ROW_BLOCK_N = 256 * 4;

static int16_t g_lut[SIN_N];
static uint8_t g_shapeLUT[SHAPE_N];
static uint16_t g_rgbRowBase[ROW_BLOCK_N];

static void buildFixtures() {
    // Ramp -512..511 across the 1024 entries: covers the full sin1024
    // amplitude range (SIN_AMP=512) including both sign extremes at known
    // indices, so a 3-wave sum can be driven to its full [-1536,1533] range
    // by choosing phase.
    for (int i = 0; i < SIN_N; i++) {
        g_lut[i] = static_cast<int16_t>(i - 512);
    }
    // Deterministic non-trivial ramp covering the full uint8 output range
    // several times over as the index grows 0..1551.
    for (int i = 0; i < SHAPE_N; i++) {
        g_shapeLUT[i] = static_cast<uint8_t>(i * 3 + 5);
    }
    // shading*4+colSlot -> a value that encodes both indices, so a wrong
    // colSlot or a wrong shading index is visible in the mismatch itself
    // rather than by coincidence matching a neighbour.
    for (int shading = 0; shading < 256; shading++) {
        for (int slot = 0; slot < 4; slot++) {
            g_rgbRowBase[shading * 4 + slot] = static_cast<uint16_t>((shading << 4) | (slot << 1) | 1);
        }
    }
}

struct Case {
    const char *name;
    uint32_t phase0, phase1, phase2;
    uint32_t step0x4, step1x4, step2x4;
    int nSpans;
};

// sumCur0 for each case is computed below from lut[] at the given
// phase0/1/2 (lut[i] = i-512, idx = phase>>22), kept consistent with phase
// so this exercises the real relationship between the
// gather and the interpolation, not two disconnected inputs.
static int32_t gatherSum(uint32_t p0, uint32_t p1, uint32_t p2) {
    return g_lut[(p0 >> 22) & 1023] + g_lut[(p1 >> 22) & 1023] + g_lut[(p2 >> 22) & 1023];
}

int main(void) {
    buildFixtures();

    static uint16_t gotRow[512];
    static uint16_t refRow[512];

    // static: a local (non-static) const aggregate this size gets
    // initialized at runtime via a memcpy from a rodata template on this
    // toolchain, and this freestanding link has no libc memcpy (same
    // no-libc reasoning as everywhere else in this harness). static places
    // it directly in .rodata with no runtime copy.
    static const Case cases[] = {
        // Defaults-like: moderate steps, mid-range phase, w=480 (120 spans).
        {"defaults_w480", 0x10000000u, 0x40000000u, 0x80000000u, 0x00300000u, 0x00200000u, 0x00500000u, 120},
        // Half resolution, w=240 (60 spans), different phase/step mix.
        {"halfres_w240", 0x00000000u, 0xC0000000u, 0x7FFF0000u, 0x00080000u, 0x00600000u, 0x00010000u, 60},
        // p=0 speed/scale-like: tiny steps (near-stationary field), single span.
        {"tiny_step_n1", 0x3F800000u, 0x9F800000u, 0x00800000u, 0x00000001u, 0x00000000u, 0x00000002u, 1},
        // p=100 speed/scale-like: phase starts within one step of the uint32
        // wrap boundary and the step magnitude (~0x14000000) matches the
        // fastest realistic per-span rotation (freq up to ~0.116 rad/px at
        // freqScale=1.9, times GRID=4, in DDS units) -- exercises the wrap in
        // the phase adds and extui's handling of the wrapped bit pattern
        // without the pathological lut-table-wraparound jump a much larger,
        // physically-unreachable step would add on top of this synthetic
        // ramp lut (real sin1024 has no such discontinuity; this ramp does,
        // at index 1023->0, so step magnitude is kept realistic here rather
        // than adversarial).
        {"wrap_realistic_step", 0xFFFF0000u, 0x00010000u, 0x80000000u, 0x14000000u, 0x0F000000u, 0x18000000u, 32},
        // Phase values chosen to land exactly on the lut extremes (idx 0 and
        // idx 1023, i.e. lut values -512 and 511) so sumCur0/gather magnitude
        // hits close to the full +-1536 range the abs/shapeLUT chain must
        // handle without overflowing the SHAPE_N=1552 table. Step stays small
        // (well under one lut index per span) so the interpolation cannot
        // itself walk back across the 1023->0 ramp discontinuity.
        {"extreme_mag_pos", 0xFFF00000u, 0xFFF00000u, 0xFFF00000u, 0x00100000u, 0x00100000u, 0x00100000u, 8},
        {"extreme_mag_neg", 0x00000000u, 0x00000000u, 0x00000000u, 0x00100000u, 0x00100000u, 0x00100000u, 8},
    };
    const int nCases = static_cast<int>(sizeof(cases) / sizeof(cases[0]));

    int totalMismatch = 0;
    for (int c = 0; c < nCases; c++) {
        // No struct copy (const reference instead): a by-value Case copy is
        // an aggregate assignment this toolchain also lowers to memcpy at
        // this size, same reasoning as the `static` on `cases` above.
        const Case &cs = cases[c];
        const int32_t sumCur0 = gatherSum(cs.phase0, cs.phase1, cs.phase2);
        const int n = cs.nSpans * 4;

        causticsRowKernel(gotRow, g_lut, g_shapeLUT, g_rgbRowBase, cs.phase0, cs.phase1, cs.phase2, cs.step0x4,
                           cs.step1x4, cs.step2x4, sumCur0, cs.nSpans);
        causticsRowRef(refRow, g_lut, g_shapeLUT, g_rgbRowBase, cs.phase0, cs.phase1, cs.phase2, cs.step0x4,
                        cs.step1x4, cs.step2x4, sumCur0, cs.nSpans);

        int mismatch = 0;
        int firstIdx = -1;
        for (int i = 0; i < n; i++) {
            if (gotRow[i] != refRow[i]) {
                mismatch++;
                if (firstIdx < 0) {
                    firstIdx = i;
                }
            }
        }

        uart_puts("GM_QEMUBENCH_CAUSTICS: case=");
        uart_puts(cs.name);
        uart_puts(" nSpans=");
        uart_put_dec(cs.nSpans);
        uart_puts(" mismatch=");
        uart_put_dec(mismatch);
        if (mismatch > 0) {
            uart_puts(" firstIdx=");
            uart_put_dec(firstIdx);
            uart_puts(" got=");
            uart_put_hex32(gotRow[firstIdx]);
            uart_puts(" want=");
            uart_put_hex32(refRow[firstIdx]);
        }
        uart_puts("\n");
        totalMismatch += mismatch;
    }

    uart_puts("GM_QEMUBENCH_CAUSTICS: cases=");
    uart_put_dec(nCases);
    uart_puts(" total_mismatch=");
    uart_put_dec(totalMismatch);
    uart_puts("\n");

    if (totalMismatch == 0) {
        uart_puts("GM_QEMUBENCH_PIE: PASS causticsRowKernel bit-exact vs independent scalar reference "
                   "(6 cases, defaults/half-res/tiny-step/large-step-wrap/extreme-magnitude)\n");
    } else {
        uart_puts("GM_QEMUBENCH_PIE: FAIL total_mismatch=");
        uart_put_dec(totalMismatch);
        uart_puts("\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");

    for (;;) {
        // Spin so QEMU has a stable state; nothing to return to.
    }
}
