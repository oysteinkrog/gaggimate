/* QEMU execution check for Mandala's device kernel, round 2
 * (src/display/ui/default/bganim/AnimMandala.cpp): mandalaBandFwd and
 * mandalaBandBack (hand-scheduled Xtensa scalar, no PIE -- see the file's
 * round-2 top-of-file comment for why round 1's PIE-decode design was
 * dropped), run exactly as band() calls them, checked against a scalar C
 * reference that implements the same algorithm bandRef()'s mandalaRun<Step>
 * does (already proven pixel-exact against the golden frames on the host
 * bench -- this test is about the ASM, not the algorithm).
 *
 * The instruction sequences below are transcribed by hand from
 * mandalaBandFwd and mandalaBandBack in AnimMandala.cpp (same mnemonics,
 * same operand roles, same asm volatile block structure) rather than
 * #included directly, matching every other test in this harness: the real
 * functions live inside a translation unit (anonymous namespace,
 * BgAnimCommon dependencies -- alloc(), themeGen(), heap_caps_malloc) that
 * is not freestanding and cannot be pulled into this bare-metal harness
 * unmodified. xtensa-asm14.sh already proved the real file assembles with
 * zero register spills; this proves the exact instruction sequence executes
 * correctly and produces the exact values the algorithm calls for.
 *
 * No PIE, no FPU: both kernels are pure integer Xtensa core ISA (l16ui,
 * extui, addx2/addx4, mull, l8ui/l16si/l16ui, s16i), so unlike round 1's
 * test this harness needs no coprocessor bring-up at all -- CPENABLE stays
 * untouched, satisfying "a production kernel must never write CPENABLE"
 * trivially (there is nothing here that would need to).
 *
 * Coverage:
 *  - Map entries sweep octantAngle's full range (0, 1, 15, 32, 47, 63, 64)
 *    crossed with radius values spanning 0..254 plus the 0xFF outside-disc
 *    sentinel -- 32 entries.
 *  - mandalaBandFwd: gN_eff at both parameter extremes (g_N = 4, p[1] = 0;
 *    g_N = 12, p[1] = 100), each signed both ways (gN_eff is negated per
 *    row-half) -- four cases. halfOffset is not a parameter (the real
 *    kernel hardcodes 0, see its comment), so the reference is evaluated
 *    with halfOffset = 0 for every Fwd case, matching that.
 *  - mandalaBandBack: the same four gN_eff cases plus halfOffset = 128
 *    (SX_OFFSET for an odd g_N) with gN_eff = -5 -- five cases, matching
 *    round 1's coverage of the odd-g_N path.
 *  - rParams, sin256, sinHalf256, rescaleLUT and paletteLUT are filled with
 *    distinguishing (index-derived, not constant or all-zero) patterns so a
 *    wrong index or a swapped table/pointer shows up as a mismatch instead
 *    of coincidentally reading the same value another index would have
 *    produced. sin256/sinHalf256 are kept within the real kernel's
 *    documented v = sin256[idxA]+sinHalf256[idxB] range ([-191,190], see
 *    AnimMandala.cpp's RESCALE_PAD comment) so rescaleLUT does not need to
 *    be sized past what the real 389-entry table covers.
 *
 * No OS, no libc: prints over UART0 by writing its FIFO register directly,
 * same as every other test in this harness.
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

/* ---- transcribed from AnimMandala.cpp (verbatim mnemonics/operands) ---- */

__attribute__((noinline)) static void mandalaBandFwd(uint16_t *dstPtr, const uint16_t *mapPtr, int count,
                                                       int gN_eff, const uint32_t *rpb, const int16_t *s256,
                                                       const int16_t *sh256, const uint8_t *resc,
                                                       const uint16_t *pal) {
    const uint16_t *mp = mapPtr;
    uint16_t *dp = dstPtr;
    int u1 = count;
    int u2, u3, u4;
    asm volatile(
        "loopnez %[u1], 2f\n"
        "l16ui   %[u2], %[mp], 0\n"
        "addi    %[mp], %[mp], 2\n"
        "extui   %[u1], %[u2], 0, 8\n"
        "srli    %[u2], %[u2], 8\n"
        "addx4   %[u1], %[u1], %[rpb]\n"
        "mull    %[u2], %[u2], %[ge]\n"
        "l32i    %[u3], %[u1], 0\n"
        "extui   %[u2], %[u2], 0, 8\n"
        "srli    %[u4], %[u3], 8\n"
        "addx2   %[u4], %[u2], %[u4]\n"
        "add     %[u1], %[u2], %[u3]\n"
        "extui   %[u4], %[u4], 0, 8\n"
        "addx2   %[u2], %[u4], %[sh256]\n"
        "extui   %[u1], %[u1], 0, 8\n"
        "l16si   %[u2], %[u2], 0\n"
        "addx2   %[u1], %[u1], %[s256]\n"
        "l16si   %[u1], %[u1], 0\n"
        "add     %[u2], %[resc], %[u2]\n"
        "add     %[u2], %[u2], %[u1]\n"
        "l8ui    %[u1], %[u2], 190\n"
        "extui   %[u2], %[u3], 16, 8\n"
        "mull    %[u2], %[u2], %[u1]\n"
        "srli    %[u2], %[u2], 8\n"
        "addx2   %[u2], %[u2], %[pal]\n"
        "l16ui   %[u2], %[u2], 0\n"
        "s16i    %[u2], %[dp], 0\n"
        "addi    %[dp], %[dp], 2\n"
        "2:\n"
        : [mp] "+r"(mp), [dp] "+r"(dp), [u1] "+r"(u1), [u2] "=&r"(u2), [u3] "=&r"(u3), [u4] "=&r"(u4)
        : [rpb] "r"(rpb), [s256] "r"(s256), [sh256] "r"(sh256), [resc] "r"(resc), [pal] "r"(pal), [ge] "r"(gN_eff)
        : "memory");
}

__attribute__((noinline)) static void mandalaBandBack(uint16_t *dstPtr, const uint16_t *mapPtr, int count,
                                                        int gN_eff, int halfOffset, const uint32_t *rpb,
                                                        const int16_t *s256, const int16_t *sh256,
                                                        const uint8_t *resc, const uint16_t *pal) {
    const uint16_t *mp = mapPtr;
    uint16_t *dp = dstPtr;
    int u1 = count;
    int u2, u3, u4;
    asm volatile(
        "loopnez %[u1], 2f\n"
        "l16ui   %[u2], %[mp], 0\n"
        "addi    %[mp], %[mp], -2\n"
        "extui   %[u1], %[u2], 0, 8\n"
        "srli    %[u2], %[u2], 8\n"
        "addx4   %[u1], %[u1], %[rpb]\n"
        "mull    %[u2], %[u2], %[ge]\n"
        "l32i    %[u3], %[u1], 0\n"
        "add     %[u2], %[u2], %[ho]\n"
        "extui   %[u2], %[u2], 0, 8\n"
        "srli    %[u4], %[u3], 8\n"
        "addx2   %[u4], %[u2], %[u4]\n"
        "add     %[u1], %[u2], %[u3]\n"
        "extui   %[u4], %[u4], 0, 8\n"
        "addx2   %[u2], %[u4], %[sh256]\n"
        "extui   %[u1], %[u1], 0, 8\n"
        "l16si   %[u2], %[u2], 0\n"
        "addx2   %[u1], %[u1], %[s256]\n"
        "l16si   %[u1], %[u1], 0\n"
        "add     %[u2], %[resc], %[u2]\n"
        "add     %[u2], %[u2], %[u1]\n"
        "l8ui    %[u1], %[u2], 190\n"
        "extui   %[u2], %[u3], 16, 8\n"
        "mull    %[u2], %[u2], %[u1]\n"
        "srli    %[u2], %[u2], 8\n"
        "addx2   %[u2], %[u2], %[pal]\n"
        "l16ui   %[u2], %[u2], 0\n"
        "s16i    %[u2], %[dp], 0\n"
        "addi    %[dp], %[dp], 2\n"
        "2:\n"
        : [mp] "+r"(mp), [dp] "+r"(dp), [u1] "+r"(u1), [u2] "=&r"(u2), [u3] "=&r"(u3), [u4] "=&r"(u4)
        : [rpb] "r"(rpb), [s256] "r"(s256), [sh256] "r"(sh256), [resc] "r"(resc), [pal] "r"(pal), [ge] "r"(gN_eff),
          [ho] "r"(halfOffset)
        : "memory");
}

/* ---- scalar C reference: mandalaRun<Step>'s algorithm, verified pixel-
 * exact against golden frames on the host bench (tools/animbench). This
 * test is not re-checking the algorithm; it is checking that the asm
 * above computes the same thing. ---- */
static uint16_t refPixel(uint16_t entry, int gN_eff, int halfOffset, const uint32_t *rParamsTab,
                          const int16_t *sin256Tab, const int16_t *sinHalf256Tab, const uint8_t *rescaleLUTTab,
                          const uint16_t *paletteLUTTab) {
    const int r = entry & 0xFF;
    const int oct = entry >> 8;
    const int base = halfOffset + oct * gN_eff;
    const uint32_t params = rParamsTab[(uint8_t)r];
    const uint8_t A = (uint8_t)params;
    const uint8_t B = (uint8_t)(params >> 8);
    const uint8_t vig = (uint8_t)(params >> 16);
    const uint8_t idxA = (uint8_t)(base + A);
    const uint8_t idxB = (uint8_t)(2 * base + B);
    int v = sin256Tab[idxA] + sinHalf256Tab[idxB];
    v = rescaleLUTTab[v + 190];
    v = (v * vig) >> 8;
    return paletteLUTTab[(uint8_t)v];
}

/* ---- test data ---- */

constexpr int N = 32;
alignas(16) static uint16_t mapEntries[N];

// A representative octantAngle sweep (0, 1, 15, 32, 47, 63, 64, 64) crossed
// with a radius sweep spanning 0..254 and the 0xFF outside sentinel.
static void buildMapEntries() {
    static const int octs[8] = {0, 1, 15, 32, 47, 63, 64, 64};
    static const int rs[8] = {0, 1, 63, 128, 199, 240, 254, 0xFF};
    for (int i = 0; i < N; i++) {
        const int oct = octs[i % 8];
        const int r = rs[(i / 8 * 3 + i) % 8]; // decorrelate from the oct index
        mapEntries[i] = (uint16_t)((oct << 8) | r);
    }
}

alignas(16) static uint32_t rParamsTab[256];
// sin256/sinHalf256 kept within the real kernel's v = sinA + sinHalf range
// ([-191, 190], see AnimMandala.cpp's RESCALE_PAD comment) so rescaleLUT
// below does not need to be larger than the real ~389-entry table.
alignas(16) static int16_t sin256Tab[256];
alignas(16) static int16_t sinHalf256Tab[256];
constexpr int RESCALE_N = 400; // v+190 for v in [-191,208]; real range is [-191,190]
static uint8_t rescaleLUTTab[RESCALE_N];
alignas(16) static uint16_t paletteLUTTab[256];

static void buildTables() {
    for (int i = 0; i < 256; i++) {
        const uint8_t A = (uint8_t)(i * 3);
        const uint8_t B = (uint8_t)(i * 5 + 7);
        const uint8_t vig = (uint8_t)(i * 7 + 11);
        rParamsTab[i] = (uint32_t)A | ((uint32_t)B << 8) | ((uint32_t)vig << 16);
        sin256Tab[i] = (int16_t)((i % 200) - 100);          // [-100, 99]
        sinHalf256Tab[i] = (int16_t)(((i * 3) % 180) - 90); // [-90, 89]
        paletteLUTTab[i] = (uint16_t)(0x1000 + i * 0x0101);
    }
    for (int j = 0; j < RESCALE_N; j++) {
        rescaleLUTTab[j] = (uint8_t)(j * 7 + 3);
    }
}

static uint16_t gotPixels[N];
static uint16_t wantPixels[N];

int totalMismatches = 0;
int firstBadCase = -1, firstBadIdx = -1;
uint16_t firstGot = 0, firstWant = 0;
int caseCounter = 0;

static void checkCase(const char *label) {
    for (int i = 0; i < N; i++) {
        if (gotPixels[i] != wantPixels[i]) {
            totalMismatches++;
            if (firstBadCase < 0) {
                firstBadCase = caseCounter;
                firstBadIdx = i;
                firstGot = gotPixels[i];
                firstWant = wantPixels[i];
            }
        }
    }
    uart_puts("GM_QEMUBENCH_MANDALA: case=");
    uart_puts(label);
    uart_puts(" got=");
    for (int i = 0; i < N; i++) {
        uart_put_hex16(gotPixels[i]);
        uart_putc(' ');
    }
    uart_puts("\n");
    caseCounter++;
}

int main(void) {
    buildMapEntries();
    buildTables();

    // mandalaBandFwd: halfOffset is not a parameter (the real kernel
    // hardcodes 0), so only gN_eff varies.
    static const int fwdCases[] = {4, -4, 12, -12};
    for (int gN_eff : fwdCases) {
        for (int i = 0; i < N; i++) {
            gotPixels[i] = 0xDEAD;
        }
        mandalaBandFwd(gotPixels, mapEntries, N, gN_eff, rParamsTab, sin256Tab, sinHalf256Tab, rescaleLUTTab,
                        paletteLUTTab);
        for (int i = 0; i < N; i++) {
            wantPixels[i] =
                refPixel(mapEntries[i], gN_eff, 0, rParamsTab, sin256Tab, sinHalf256Tab, rescaleLUTTab, paletteLUTTab);
        }
        // mag is at most 12 (two digits): the single-digit '0'+mag shortcut
        // used elsewhere in this test would silently print '<' for 12
        // ('0'+12 = ASCII 60), which is exactly the class of bug this
        // codebase's comment discipline exists to catch before it reaches a
        // report -- write both digits explicitly instead.
        char label[40] = "Fwd gN=";
        int p = 7;
        if (gN_eff < 0) {
            label[p++] = '-';
        }
        const int mag = gN_eff < 0 ? -gN_eff : gN_eff;
        if (mag >= 10) {
            label[p++] = (char)('0' + mag / 10);
        }
        label[p++] = (char)('0' + mag % 10);
        label[p] = '\0';
        checkCase(label);
    }

    // mandalaBandBack: the same gN_eff sweep plus the odd-g_N SX_OFFSET=128
    // case, walking mapEntries backward from the last entry -- dst[i] feeds
    // from mapEntries[N-1-i], matching how band() drives it (mapRow+g_cx
    // decrementing) against a forward-walking dst.
    struct BackCase {
        int gN_eff;
        int halfOffset;
        const char *label;
    };
    static const BackCase backCases[] = {
        {4, 0, "Back gN=4 half=0"},     {-4, 0, "Back gN=-4 half=0"},   {12, 0, "Back gN=12 half=0"},
        {-12, 0, "Back gN=-12 half=0"}, {-5, 128, "Back gN=-5 half=128 (odd g_N, SX_OFFSET)"},
    };
    for (const auto &c : backCases) {
        for (int i = 0; i < N; i++) {
            gotPixels[i] = 0xDEAD;
        }
        mandalaBandBack(gotPixels, mapEntries + (N - 1), N, c.gN_eff, c.halfOffset, rParamsTab, sin256Tab,
                         sinHalf256Tab, rescaleLUTTab, paletteLUTTab);
        for (int i = 0; i < N; i++) {
            wantPixels[i] = refPixel(mapEntries[(N - 1) - i], c.gN_eff, c.halfOffset, rParamsTab, sin256Tab,
                                      sinHalf256Tab, rescaleLUTTab, paletteLUTTab);
        }
        checkCase(c.label);
    }

    if (totalMismatches == 0) {
        uart_puts("GM_QEMUBENCH_PIE: PASS mandalaBandFwd+mandalaBandBack bit-exact vs scalar reference (");
        uart_put_dec((int)(sizeof(fwdCases) / sizeof(fwdCases[0])));
        uart_puts(" Fwd cases + ");
        uart_put_dec((int)(sizeof(backCases) / sizeof(backCases[0])));
        uart_puts(" Back cases x ");
        uart_put_dec(N);
        uart_puts(" map entries, octantAngle 0..64, radius 0..254 + outside sentinel, gN_eff/halfOffset at both "
                   "parameter extremes)\n");
    } else {
        uart_puts("GM_QEMUBENCH_PIE: FAIL mismatches=");
        uart_put_dec(totalMismatches);
        uart_puts(" first_bad_case=");
        uart_put_dec(firstBadCase);
        uart_puts(" first_bad_idx=");
        uart_put_dec(firstBadIdx);
        uart_puts(" got=");
        uart_put_hex16(firstGot);
        uart_puts(" want=");
        uart_put_hex16(firstWant);
        uart_puts("\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");

    for (;;) {
    }
}
