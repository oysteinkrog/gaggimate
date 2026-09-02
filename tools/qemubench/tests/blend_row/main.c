/* Real-Xtensa execution check for blendGroup8General -- the general
 * (no lane opaque) vector-arithmetic path of blendRow's PIE composite
 * kernel, tools/animbench/kernels-blend/blend_pie_kernel.cpp. That file's
 * own header explains why this exists: it is the piece a sibling agent
 * (tools/overlaybench/kernels/overlay_blend.cpp's blendRow_pie_asm)
 * explicitly declined to write in real asm, judging the hand-scheduled
 * EE.vadds.s16/EE.vmul.u16/ssai sequence "too likely to ship a silently-
 * wrong result... without a way to execute it." It has since been proven
 * three ways on the host (23.2M-case algorithmic proof, 50,000-case
 * group-dispatch proof, 1.6M-lane semantic-trace interpreter that parses
 * the assembled .S directly) -- but none of those is real execution.
 * This is: same instruction sequence, same operand registers, same ssai
 * immediates, transcribed by hand from blend_pie_kernel.cpp (not
 * regenerated or simplified), run under Espressif's qemu-system-xtensa
 * fork via this repo's tools/qemubench harness.
 *
 * Test vector: the "general_all_nonopaque" case from
 * tools/animbench/kernels-blend/blend_qemu_vectors.h (auto-generated
 * there from the host-proven scalar reference, fixed seed 0xB1e4d0) --
 * eight pixels, every lane non-opaque (a in [1,254]) and non-zero,
 * chosen to exercise every boundary of the per-channel arithmetic in one
 * group rather than eight separate single-lane checks. Values transcribed
 * by hand from that header; not #included directly because it's a C++
 * header (namespace/constexpr) and this test builds as freestanding C,
 * matching pie_smoke_full/main.c's precedent.
 *
 * No OS, no libc: prints over UART0 by writing its FIFO register directly
 * (ESP32-S3 UART0 base 0x60000000), same as pie_smoke_full/main.c.
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

/* Verbatim from tools/animbench/kernels-blend/blend_pie_kernel.cpp
 * (kBlendGroupConsts) -- order is load-bearing, must match the sequence
 * of ee.vld.128.ip qN, a7, 16 calls below exactly as it does there. */
static const uint16_t kBlendGroupConsts[48] __attribute__((aligned(16))) = {
    1,      1,      1,      1,      1,      1,      1,      1,
    0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800, 0xF800,
    2048,   2048,   2048,   2048,   2048,   2048,   2048,   2048,
    0x07E0, 0x07E0, 0x07E0, 0x07E0, 0x07E0, 0x07E0, 0x07E0, 0x07E0,
    32,     32,     32,     32,     32,     32,     32,     32,
    0x001F, 0x001F, 0x001F, 0x001F, 0x001F, 0x001F, 0x001F, 0x001F,
};

/* Verbatim instruction sequence from blendGroup8General in
 * blend_pie_kernel.cpp (mnemonics, operand registers, ssai immediates,
 * load/store order all unchanged) -- transcribed, not regenerated, so a
 * PASS here is direct evidence about the exact sequence that file
 * contains, not a lookalike written fresh against this harness. */
static void blendGroup8General(uint16_t *dst8, const uint16_t *colStage8, const uint16_t *aStage8,
                                const uint16_t *invStage8) {
    const uint16_t *rd = dst8;
    uint16_t *wr = dst8;
    const uint16_t *col = colStage8;
    const uint16_t *av = aStage8;
    const uint16_t *iv = invStage8;
    const uint16_t *ct = kBlendGroupConsts;
    __asm__ volatile("ee.vld.128.ip q7, %[ct], 16\n"
                      "ee.vld.128.ip q0, %[col], 16\n"
                      "ee.vld.128.ip q1, %[rd], 16\n"
                      "ee.vld.128.ip q2, %[av], 16\n"
                      "ee.vld.128.ip q3, %[iv], 16\n"
                      /* ---- R channel ---- */
                      "ee.vld.128.ip q4, %[ct], 16\n"
                      "ee.andq q5, q0, q4\n"
                      "ee.andq q4, q1, q4\n"
                      "ssai 11\n"
                      "ee.vmul.u16 q5, q5, q7\n"
                      "ee.vmul.u16 q4, q4, q7\n"
                      "ssai 0\n"
                      "ee.vmul.u16 q5, q5, q2\n"
                      "ee.vmul.u16 q4, q4, q3\n"
                      "ee.vadds.s16 q5, q5, q4\n"
                      "ssai 8\n"
                      "ee.vmul.u16 q5, q5, q7\n"
                      "ee.vld.128.ip q4, %[ct], 16\n"
                      "ssai 0\n"
                      "ee.vmul.u16 q6, q5, q4\n"
                      /* ---- G channel ---- */
                      "ee.vld.128.ip q4, %[ct], 16\n"
                      "ee.andq q5, q0, q4\n"
                      "ee.andq q4, q1, q4\n"
                      "ssai 5\n"
                      "ee.vmul.u16 q5, q5, q7\n"
                      "ee.vmul.u16 q4, q4, q7\n"
                      "ssai 0\n"
                      "ee.vmul.u16 q5, q5, q2\n"
                      "ee.vmul.u16 q4, q4, q3\n"
                      "ee.vadds.s16 q5, q5, q4\n"
                      "ssai 8\n"
                      "ee.vmul.u16 q5, q5, q7\n"
                      "ee.vld.128.ip q4, %[ct], 16\n"
                      "ssai 0\n"
                      "ee.vmul.u16 q5, q5, q4\n"
                      "ee.orq q6, q6, q5\n"
                      /* ---- B channel ---- */
                      "ee.vld.128.ip q4, %[ct], 16\n"
                      "ee.andq q5, q0, q4\n"
                      "ee.andq q4, q1, q4\n"
                      "ee.vmul.u16 q5, q5, q2\n"
                      "ee.vmul.u16 q4, q4, q3\n"
                      "ee.vadds.s16 q5, q5, q4\n"
                      "ssai 8\n"
                      "ee.vmul.u16 q5, q5, q7\n"
                      "ee.orq q6, q6, q5\n"
                      "ee.vst.128.ip q6, %[wr], 16\n"
                      : [col] "+r"(col), [rd] "+r"(rd), [wr] "+r"(wr), [av] "+r"(av), [iv] "+r"(iv), [ct] "+r"(ct)
                      :
                      : "memory");
}

int main(void) {
    /* "general_all_nonopaque" case, transcribed from
     * tools/animbench/kernels-blend/blend_qemu_vectors.h
     * (BLEND_GROUP_CASES[0], generated 2026-08-31, seed 0xB1e4d0). */
    static const uint16_t fg[8] __attribute__((aligned(16))) = {0x0000, 0xFFFF, 0xF800, 0x07E0,
                                                                 0x001F, 0xF81F, 0x8410, 0x2104};
    static const uint16_t alpha[8] = {1, 254, 127, 128, 2, 253, 64, 191};
    static const uint16_t dstBefore[8] __attribute__((aligned(16))) = {0xFFFF, 0x0000, 0x07E0, 0xF800,
                                                                        0xF81F, 0x001F, 0x2104, 0x8410};
    static const uint16_t dstExpect[8] = {0xF7DE, 0xF7DE, 0x7BE0, 0x7BE0, 0xF01F, 0xF01F, 0x39C7, 0x39C7};

    static uint16_t dst8[8] __attribute__((aligned(16)));
    static uint16_t aStage8[8] __attribute__((aligned(16)));
    static uint16_t invStage8[8] __attribute__((aligned(16)));

    for (int i = 0; i < 8; i++) {
        dst8[i] = dstBefore[i];
        aStage8[i] = alpha[i];
        invStage8[i] = (uint16_t)(256u - alpha[i]);
    }

    blendGroup8General(dst8, fg, aStage8, invStage8);

    uart_puts("GM_QEMUBENCH_PIE: got=");
    for (int i = 0; i < 8; i++) {
        uart_put_hex16(dst8[i]);
        uart_putc(' ');
    }
    uart_puts("expect=");
    for (int i = 0; i < 8; i++) {
        uart_put_hex16(dstExpect[i]);
        uart_putc(' ');
    }
    uart_puts("\n");

    int mismatches = 0;
    for (int i = 0; i < 8; i++) {
        if (dst8[i] != dstExpect[i]) {
            mismatches++;
        }
    }

    if (mismatches == 0) {
        uart_puts("GM_QEMUBENCH_PIE: PASS blendGroup8General bit-exact vs host-proven scalar reference "
                   "(8/8 lanes, general_all_nonopaque case)\n");
    } else {
        uart_puts("GM_QEMUBENCH_PIE: FAIL mismatches=");
        uart_putc((char)('0' + mismatches));
        uart_puts("\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");

    for (;;) {
        /* Spin so QEMU has a stable state; nothing to return to. */
    }
}
