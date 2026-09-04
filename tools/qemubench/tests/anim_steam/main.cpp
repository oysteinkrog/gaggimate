/* Real-Xtensa execution check for AnimSteam's hand-written asm kernel
 * (src/display/ui/default/bganim/AnimSteam.cpp, fillRowPie) -- the eight-
 * pixel PIE store that replaced the background wash's branchy scalar
 * 2-pixels-per-store loop, which never got GCC's zero-overhead LOOP.
 *
 * Round 3 update: this file used to also transcribe a hand-written scalar
 * kernel for the blob-splat inner loop (stampRowScalar). That kernel does
 * not exist any more. Rounds 1 and 2 both made the blob stamp branch-free
 * (round 1: pad alphaLUT to 1024 entries so an out-of-disc index reads
 * back zero; round 2: clamp the index to 63 with `min` against the
 * original 64-entry table) on the theory that a data-dependent branch
 * costs more than the unconditional work it would otherwise skip. Device
 * measurement said the opposite on real content: Steam's blobs spend most
 * of their bounding box either outside the disc or at near-zero alpha (the
 * lifecycle envelope caps alpha at 0.4, split across up to 55 concurrent
 * blobs), so the original two `continue`s -- tested and skipped, not
 * computed through -- were faster than paying full blend cost on every
 * pixel unconditionally. Round 3 restored the original branchy algorithm
 * for the stamp (AnimSteam.cpp's stampBlobs, shared between bandRef and
 * the Xtensa band()) as plain C++, so it is proven by the same host
 * goldens and xtensa-asm14 codegen inspection every other portable path
 * in this pass is, not by a bare-metal QEMU run -- that rung exists to
 * catch bugs a hand-written inline-asm operand constraint can hide, which
 * does not apply to code with no inline asm in it. fillRowPie is still
 * hand-written asm and still gets this check.
 *
 * fillRowPie is transcribed by hand from AnimSteam.cpp (mnemonics, operand
 * registers as GCC would assign them, immediates, instruction order all
 * unchanged) rather than #included, for the same reason blend_row/main.c
 * gives: the real source is not freestanding (BgAnim.h, math.h, global
 * animation state, non-trivial statics), and this harness has no libc, no
 * malloc, no globals with constructors. fillRowRef implements the same
 * fill formula AnimSteam.cpp's bandPortable()/band() both use, so a PASS
 * here is evidence about the actual instruction sequence in the kernel,
 * checked against the actual arithmetic it must reproduce, not a
 * lookalike written fresh against this harness.
 *
 * No OS, no libc: prints over UART0 by writing its FIFO register directly
 * (ESP32-S3 UART0 base 0x60000000), same as pie_smoke_full/main.c and
 * blend_row/main.c.
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

static void uart_put_udec(uint32_t v) {
    char buf[10];
    int n = 0;
    if (v == 0) {
        uart_putc('0');
        return;
    }
    while (v > 0 && n < 10) {
        buf[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0) {
        uart_putc(buf[--n]);
    }
}

/* ---- fillRowPie: verbatim from AnimSteam.cpp ---- */
__attribute__((noinline)) static void fillRowPie(uint16_t *dst, const uint16_t *bc, int w8) {
    uint16_t *wr = dst;
    const uint16_t *bcp = bc;
    int n = w8;
    asm volatile("ee.vld.128.ip q0, %[bc], 0\n"
                 "loop %[n], 1f\n"
                 "ee.vst.128.ip q0, %[dst], 16\n"
                 "1:\n"
                 : [dst] "+r"(wr), [n] "+r"(n)
                 : [bc] "r"(bcp)
                 : "memory");
}

static void fillRowRef(uint16_t *dst, uint16_t c, int w) {
    for (int i = 0; i < w; i++) {
        dst[i] = c;
    }
}

static uint32_t g_fails = 0;

static void checkRow(const char *label, uint16_t *kernelRow, uint16_t *refRow, int count) {
    for (int i = 0; i < count; i++) {
        if (kernelRow[i] != refRow[i]) {
            if (g_fails == 0) {
                uart_puts("GM_QEMUBENCH_ANIM: first mismatch in ");
                uart_puts(label);
                uart_puts(" at i=");
                uart_put_udec((uint32_t)i);
                uart_puts("\n");
            }
            g_fails++;
        }
    }
}

int main(void) {
    /* Enable the coprocessors (CP0 FPU, CP3 PIE) once, here, before any
     * ee.* instruction runs. Production kernel code must never write
     * CPENABLE directly -- ESP-IDF/FreeRTOS enable a task's coprocessors
     * lazily, on first fault, specifically so it can also install that
     * task's save/restore across context switches; a hand-written CPENABLE
     * write bypasses that and can corrupt another task's coprocessor state.
     * This bare-metal harness has no OS and no other task to corrupt, so
     * this is the one place that write is legitimate (same convention as
     * tests/anim_ripples/main.cpp). */
    __asm__ volatile("movi a4, 0xff\n"
                      "wsr.cpenable a4\n"
                      "rsync\n" ::: "a4");

    uart_puts("GM_QEMUBENCH_ANIM: AnimSteam fillRowPie vs portable reference\n");

    /* Representative colours, w8 = 1 (minimum trip count -- `loop` with a
     * zero trip count wraps LCOUNT to ~4 billion instead of skipping, so
     * this is the smallest value fillRowPie is ever called with) and
     * w8 = 60 (the real w = 480 full-resolution production case). */
    static const uint16_t colours[] = {0x0000, 0xFFFF, 0xF800, 0x07E0, 0x001F, 0xF81F, 0x8410, 0x2104};
    static uint16_t bc[8] __attribute__((aligned(16)));
    static uint16_t dst[480] __attribute__((aligned(16)));
    static uint16_t ref[480] __attribute__((aligned(16)));
    for (unsigned ci = 0; ci < sizeof(colours) / sizeof(colours[0]); ci++) {
        const uint16_t c = colours[ci];
        for (int k = 0; k < 8; k++) {
            bc[k] = c;
        }
        /* w8 = 1 */
        for (int i = 0; i < 8; i++) {
            dst[i] = (uint16_t)~c;
            ref[i] = (uint16_t)~c;
        }
        fillRowPie(dst, bc, 1);
        fillRowRef(ref, c, 8);
        checkRow("fillRowPie w8=1", dst, ref, 8);
        /* w8 = 60 (w = 480, the full-resolution production case) */
        for (int i = 0; i < 480; i++) {
            dst[i] = (uint16_t)~c;
            ref[i] = (uint16_t)~c;
        }
        fillRowPie(dst, bc, 60);
        fillRowRef(ref, c, 480);
        checkRow("fillRowPie w8=60", dst, ref, 480);
    }

    if (g_fails == 0) {
        uart_puts("GM_QEMUBENCH_PIE: PASS AnimSteam fillRowPie bit-exact vs portable reference "
                   "(8 colours x {w8=1,w8=60})\n");
        uart_puts("GM_QEMUBENCH_ANIM: PASS AnimSteam fillRowPie bit-exact vs portable reference\n");
    } else {
        uart_puts("GM_QEMUBENCH_PIE: FAIL mismatches=");
        uart_put_udec(g_fails);
        uart_puts("\n");
        uart_puts("GM_QEMUBENCH_ANIM: FAIL mismatches=");
        uart_put_udec(g_fails);
        uart_puts("\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");

    for (;;) {
        /* Spin so QEMU has a stable state; nothing to return to. */
    }
}
