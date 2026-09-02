/* QEMU self-test for blendGroup8General, the Xtensa PIE (EE.*) group-8
 * blend kernel in tools/animbench/kernels-blend/ (blend-asm-lead's lane,
 * read-only from here -- see that directory's own comments for the
 * derivation and the three layers of host-side proof this only needed to
 * add a fourth: does the REAL instruction sequence, executed by QEMU's
 * PIE implementation, match the reference? The host proofs modeled
 * ee.vadds.s16 saturation and the ssai/SAR threading rather than
 * executing them -- this is what actually executes them.
 *
 * blend_pie_kernel.cpp is #included directly (not linked as a separate
 * object) so blendGroup8General's real body runs unmodified -- no
 * reimplementation, no copy that could drift. It's C++ (namespace,
 * alignas, __restrict), which is why this file is .cpp and build.sh grew
 * g++ support for it; blend_pie_kernel.cpp's own build_and_report.sh
 * already proved this exact source compiles cleanly with
 * -std=gnu++20 -fno-exceptions -fno-rtti -mlongcalls, so this test uses
 * the same flags rather than a second, potentially-drifted set.
 *
 * Two-part coverage:
 *  1. All 216 directed BLEND_PIXEL_CASES, each broadcast across all 8
 *     lanes. Sound because blendGroup8General applies the identical
 *     per-lane instruction sequence with no cross-lane dependency (every
 *     ee.* op here is lane-parallel) -- broadcasting one scalar case to
 *     all 8 lanes and checking all 8 outputs match is equivalent to
 *     testing that case through the real vector path, and gives the same
 *     alpha-space coverage (0..254, saturation boundaries included) the
 *     216-case table was built for without hand-authoring 216 separate
 *     8-lane groups.
 *  2. BLEND_GROUP_CASES' genuinely heterogeneous lanes (skipping any case
 *     whose alpha bytes are all 0xFF -- that's the allOpaque dispatch
 *     path, a plain EE.VLD/EE.VST copy in blendRow_pie_general_asm, not
 *     this kernel), which exercises per-lane independence with values
 *     that actually differ lane-to-lane, not just the broadcast case.
 */
#include <cstdint>

#include "../../../animbench/kernels-blend/blend_pie_kernel.cpp"
#include "../../../animbench/kernels-blend/blend_qemu_vectors.h"

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

extern "C" int main(void) {
    using namespace blendopt;

    int pixel_fail = 0;
    int pixel_first_fail = -1;

    for (int i = 0; i < 216; i++) {
        const BlendPixelCase &c = BLEND_PIXEL_CASES[i];
        alignas(16) uint16_t dst[8];
        alignas(16) uint16_t col[8];
        alignas(16) uint16_t av[8];
        alignas(16) uint16_t iv[8];
        for (int k = 0; k < 8; k++) {
            dst[k] = c.bg;
            col[k] = c.fg;
            av[k] = (uint16_t)c.a;
            iv[k] = (uint16_t)(256u - c.a);
        }

        blendGroup8General(dst, col, av, iv);

        bool ok = true;
        for (int k = 0; k < 8; k++) {
            if (dst[k] != c.expect) {
                ok = false;
            }
        }
        if (!ok) {
            pixel_fail++;
            if (pixel_first_fail < 0) {
                pixel_first_fail = i;
            }
        }
    }

    uart_puts("GM_QEMUBENCH_BLEND: pixel_cases=216 fail=");
    uart_put_hex16((uint16_t)pixel_fail);
    uart_puts("\n");
    if (pixel_first_fail >= 0) {
        const BlendPixelCase &c = BLEND_PIXEL_CASES[pixel_first_fail];
        alignas(16) uint16_t dst[8];
        alignas(16) uint16_t col[8];
        alignas(16) uint16_t av[8];
        alignas(16) uint16_t iv[8];
        for (int k = 0; k < 8; k++) {
            dst[k] = c.bg;
            col[k] = c.fg;
            av[k] = (uint16_t)c.a;
            iv[k] = (uint16_t)(256u - c.a);
        }
        blendGroup8General(dst, col, av, iv);
        uart_puts("GM_QEMUBENCH_BLEND: first pixel mismatch idx=");
        uart_put_hex16((uint16_t)pixel_first_fail);
        uart_puts(" fg=");
        uart_put_hex16(c.fg);
        uart_puts(" bg=");
        uart_put_hex16(c.bg);
        uart_puts(" a=");
        uart_put_hex16((uint16_t)c.a);
        uart_puts(" expect=");
        uart_put_hex16(c.expect);
        uart_puts(" got=");
        uart_put_hex16(dst[0]);
        uart_puts("\n");
    }

    int group_fail = 0;
    int group_tested = 0;
    for (int g = 0; g < 2; g++) {
        const BlendGroupCase &gc = BLEND_GROUP_CASES[g];
        bool allOpaque = true;
        for (int k = 0; k < 8; k++) {
            if (gc.colour[k * 3 + 2] != 0xFF) {
                allOpaque = false;
            }
        }
        if (allOpaque) {
            continue; /* allOpaque dispatch path, not this kernel */
        }
        group_tested++;

        alignas(16) uint16_t dst[8];
        alignas(16) uint16_t col[8];
        alignas(16) uint16_t av[8];
        alignas(16) uint16_t iv[8];
        for (int k = 0; k < 8; k++) {
            dst[k] = gc.dstBefore[k];
            uint8_t r = gc.colour[k * 3 + 0];
            uint8_t gch = gc.colour[k * 3 + 1];
            uint8_t a = gc.colour[k * 3 + 2];
            col[k] = (uint16_t)(r | (gch << 8));
            av[k] = a;
            iv[k] = (uint16_t)(256u - a);
        }

        blendGroup8General(dst, col, av, iv);

        bool ok = true;
        int first_lane = -1;
        for (int k = 0; k < 8; k++) {
            if (dst[k] != gc.dstExpect[k]) {
                ok = false;
                if (first_lane < 0) {
                    first_lane = k;
                }
            }
        }

        uart_puts("GM_QEMUBENCH_BLEND: group case=");
        uart_puts(gc.label);
        uart_puts(ok ? " PASS\n" : " FAIL\n");
        if (!ok) {
            group_fail++;
            uart_puts("GM_QEMUBENCH_BLEND: first lane mismatch=");
            uart_put_hex16((uint16_t)first_lane);
            uart_puts(" got=");
            uart_put_hex16(dst[first_lane]);
            uart_puts(" expect=");
            uart_put_hex16(gc.dstExpect[first_lane]);
            uart_puts("\n");
        }
    }

    uart_puts("GM_QEMUBENCH_BLEND: group_cases_tested=");
    uart_put_hex16((uint16_t)group_tested);
    uart_puts(" group_fail=");
    uart_put_hex16((uint16_t)group_fail);
    uart_puts("\n");

    if (pixel_fail == 0 && group_fail == 0) {
        uart_puts("GM_QEMUBENCH_PIE: PASS blendGroup8General bit-exact vs host reference\n");
    } else {
        uart_puts("GM_QEMUBENCH_PIE: FAIL blendGroup8General mismatches\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");

    for (;;) {
        /* spin */
    }
    return 0;
}
