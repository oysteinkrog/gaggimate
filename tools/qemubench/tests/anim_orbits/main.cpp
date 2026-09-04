/* QEMU execution check for fillBgPie, the PIE background-fill kernel in
 * src/display/ui/default/bganim/AnimOrbits.cpp's band(). Orbits is a sparse
 * particle system: path points and orbit-body stamps are a few hundred
 * scatter writes a frame, kept scalar (no vector gather/scatter exists for
 * them), but every one of the 480x480 frame's other pixels goes through
 * this one fill, so it is the only part of Orbits' band() that scales with
 * pixel count and the only part worth a PIE kernel.
 *
 * The instruction sequence below is transcribed by hand from fillBgPie in
 * AnimOrbits.cpp (same mnemonics, same operand registers, same asm volatile
 * block, same "build the broadcast value in a 16-byte aligned stack buffer
 * and load it once" structure) rather than #included directly, matching
 * tests/blend_row/main.c's approach and for the same reason stated there:
 * the real function lives inside a translation unit (anonymous namespace,
 * BgAnimCommon dependencies, themeRGB/alloc/etc.) that is not freestanding
 * and cannot be pulled into this bare-metal harness unmodified. This proves
 * the exact instruction sequence executes correctly on Espressif's
 * qemu-system-xtensa PIE implementation; xtensa-asm14.sh only proved it
 * assembles.
 *
 * Coverage:
 *  - Every real (rows, w) shape band() is called with: rows in {1, 2, 8}
 *    (device full-res, device interlaced-half-res single row, and the host
 *    bench's BAND_H) crossed with w in {240, 480} (half-res, full-res),
 *    giving nOct = (rows*w)/8 in {30, 60, 120, 240, 480}.
 *  - bg patterns spanning bit positions that would expose a masking or
 *    byte-order bug: all-zero, all-one, both alternating patterns, two
 *    "real" RGB565 colors, and the two single-bit extremes (bit 0, bit 15).
 *  - A guard region immediately before and after the destination buffer,
 *    checked untouched after every call: EE.VST.128.IP's post-increment and
 *    the loop's trip count (nOct, not nOct-1 or nOct+1) are exactly the
 *    things a transcription error would get wrong, and a guard is the only
 *    way an overrun shows up as a reported mismatch instead of silently
 *    corrupting adjacent memory.
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
    for (int shift = 12; shift >= 0; shift -= 4) {
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
        buf[n++] = static_cast<char>('0' + (v % 10));
        v /= 10;
    }
    while (n > 0) {
        uart_putc(buf[--n]);
    }
}

/* Transcribed verbatim from fillBgPie in
 * src/display/ui/default/bganim/AnimOrbits.cpp (mnemonics, operand
 * registers, and instruction order all unchanged). */
__attribute__((noinline)) static void fillBgPie(uint16_t *dst, int nOct, uint16_t bg) {
    alignas(16) uint16_t bcast[8] = {bg, bg, bg, bg, bg, bg, bg, bg};
    uint16_t *wr = dst;
    const uint16_t *src = bcast;
    int n = nOct;
    asm volatile("ee.vld.128.ip q0, %[src], 0\n" // q0 = bg x8, resident for the whole loop
                 "1:\n"
                 "ee.vst.128.ip q0, %[wr], 16\n"
                 "addi %[n], %[n], -1\n"
                 "bnez %[n], 1b\n"
                 : [wr] "+r"(wr), [src] "+r"(src), [n] "+r"(n)
                 :
                 : "memory");
}

/* Scalar reference: the exact loop bandRef() (and band() on any
 * non-Xtensa build) runs to fill the same range. */
static void fillBgScalar(uint16_t *dst, int total, uint16_t bg) {
    for (int i = 0; i < total; i++) {
        dst[i] = bg;
    }
}

constexpr int GUARD_WORDS = 8;
constexpr int MAX_OCT = 480; // rows=8, w=480: the host bench's shape, and the largest any real caller uses
constexpr int BUF_WORDS = MAX_OCT * 8;
constexpr uint16_t GUARD_PATTERN = 0xDEAD;

// Guard | dst buffer | guard, all one array so an overrun in either
// direction lands in a checked region instead of unrelated memory. 16-byte
// aligned so dst (GUARD_WORDS in, GUARD_WORDS=8 half-words=16 bytes) is
// itself 16-byte aligned, matching what band() always hands the real
// kernel.
alignas(16) static uint16_t arena[GUARD_WORDS + BUF_WORDS + GUARD_WORDS];
alignas(16) static uint16_t refbuf[BUF_WORDS];

static int run_case(int nOct, uint16_t bg) {
    const int total = nOct * 8;
    uint16_t *dst = arena + GUARD_WORDS;

    for (int i = 0; i < GUARD_WORDS + BUF_WORDS + GUARD_WORDS; i++) {
        arena[i] = GUARD_PATTERN;
    }
    for (int i = 0; i < total; i++) {
        refbuf[i] = 0x1234; // pre-fill with something other than bg or the guard pattern
    }
    fillBgScalar(refbuf, total, bg);

    fillBgPie(dst, nOct, bg);

    int mismatches = 0;
    int firstBad = -1; // >=0: index into [0,total); <0: guard-region marker
    for (int i = 0; i < total; i++) {
        if (dst[i] != refbuf[i]) {
            mismatches++;
            if (firstBad < 0) {
                firstBad = i;
            }
        }
    }
    for (int i = 0; i < GUARD_WORDS; i++) {
        if (arena[i] != GUARD_PATTERN) {
            mismatches++;
            if (firstBad < 0) {
                firstBad = -100 - i; // pre-buffer guard clobbered
            }
        }
    }
    for (int i = 0; i < GUARD_WORDS; i++) {
        if (arena[GUARD_WORDS + BUF_WORDS + i] != GUARD_PATTERN) {
            mismatches++;
            if (firstBad < 0) {
                firstBad = -200 - i; // post-buffer guard clobbered
            }
        }
    }

    if (mismatches != 0) {
        uart_puts("GM_QEMUBENCH_ANIM_ORBITS: FAIL case nOct=");
        uart_put_dec(nOct);
        uart_puts(" bg=");
        uart_put_hex16(bg);
        uart_puts(" mismatches=");
        uart_put_dec(mismatches);
        uart_puts(" first=");
        uart_put_dec(firstBad);
        if (firstBad >= 0) {
            uart_puts(" got=");
            uart_put_hex16(dst[firstBad]);
            uart_puts(" want=");
            uart_put_hex16(refbuf[firstBad]);
        }
        uart_puts("\n");
    }
    return mismatches;
}

extern "C" int main(void) {
    int totalMismatches = 0;
    int totalCases = 0;

    // Every real (rows, w) shape: rows in {1,2,8}, w in {240,480}.
    static const int kNOcts[] = {30, 60, 120, 240, 480};
    // bg bit patterns: black, white, both alternating patterns, two "real"
    // RGB565 colors (arbitrary but nonzero in every field), and the two
    // single-bit extremes.
    static const uint16_t kBgs[] = {0x0000, 0xFFFF, 0xAAAA, 0x5555, 0x4A08, 0x2C7F, 0x0001, 0x8000};

    for (unsigned ni = 0; ni < sizeof(kNOcts) / sizeof(kNOcts[0]); ni++) {
        for (unsigned bi = 0; bi < sizeof(kBgs) / sizeof(kBgs[0]); bi++) {
            totalMismatches += run_case(kNOcts[ni], kBgs[bi]);
            totalCases++;
        }
    }

    uart_puts("GM_QEMUBENCH_PIE: cases=");
    uart_put_dec(totalCases);
    uart_puts(" mismatches=");
    uart_put_dec(totalMismatches);
    uart_puts("\n");

    if (totalMismatches == 0) {
        uart_puts("GM_QEMUBENCH_PIE: PASS fillBgPie bit-exact vs scalar fill, all (nOct, bg) shapes, guards intact\n");
    } else {
        uart_puts("GM_QEMUBENCH_PIE: FAIL mismatches=");
        uart_put_dec(totalMismatches);
        uart_puts("\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");

    for (;;) {
        // Spin so QEMU has a stable state; nothing to return to.
    }
}
