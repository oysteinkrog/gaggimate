/* Step-1 smoke test for team-lead's QEMU-PIE-harness task
 * (data/qemu-snapshot-profile.md lane): does Espressif's qemu-system-xtensa
 * fork implement the PIE (EE.*) vector extension at all, for the specific
 * ops the blend kernel work needs -- EE.VLD.128.IP (128-bit vector load,
 * post-increment addressing) and EE.VADDS.S8 (signed 8-bit lane-wise
 * saturating add)? Prior evidence in this repo (GM_PIETEST,
 * data/qemu-snapshot-profile.md) already confirmed ee.zero.q and
 * ee.vst.128.ip execute correctly under this exact QEMU fork -- but that's
 * a zero/store pair, not a load+saturating-add pair, and a different
 * execution unit inside the PIE block could easily be unimplemented or
 * buggy even if others work. This test does not assume the answer by
 * analogy; it checks the specific ops.
 *
 * No OS, no drivers, no libc startup: this prints over UART0 by writing
 * its FIFO register directly (ESP32-S3 UART0 base 0x60000000, FIFO at
 * offset 0x0 -- stable across the ESP32 family, and independent of
 * whatever baud/clock state QEMU's UART model starts in, since QEMU does
 * not model UART timing -- it moves bytes through immediately regardless
 * of baud configuration).
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

static void uart_put_hex_byte(uint8_t b) {
    static const char hex[] = "0123456789abcdef";
    uart_putc(hex[(b >> 4) & 0xF]);
    uart_putc(hex[b & 0xF]);
}

/* Scalar reference for EE.VADDS.S8's per-lane operation: signed 8-bit
 * saturating add. This is the ground truth the vector result is checked
 * against -- not hand-computed expected constants, which would risk the
 * same kind of arithmetic slip this test exists to catch. */
static int8_t sat_add_s8(int8_t x, int8_t y) {
    int16_t sum = (int16_t)x + (int16_t)y;
    if (sum > 127) {
        sum = 127;
    }
    if (sum < -128) {
        sum = -128;
    }
    return (int8_t)sum;
}

int main(void) {
    /* 16-byte-aligned: EE.VLD.128.IP's 128-bit load has the same alignment
     * requirement this session's own GM_PIETEST hit on device earlier
     * (an unaligned poison buffer produced nondeterministic
     * ee.vst.128.ip failures there) -- a known-real constraint, not
     * defensive overkill.
     *
     * Test data deliberately spans three cases in one 16-lane vector:
     * lanes 0-7 exercise a plain signed add with no saturation (small
     * magnitudes, mixed sign); lanes 8-10 stay just under the positive
     * saturation boundary; lanes 11-15 deliberately cross it, so a
     * mis-clamped or unclamped implementation shows up as a mismatch
     * rather than being masked by every lane saturating the same way. */
    static uint8_t a[16] __attribute__((aligned(16)));
    static uint8_t b[16] __attribute__((aligned(16)));
    static uint8_t result[16] __attribute__((aligned(16)));

    for (int i = 0; i < 16; i++) {
        a[i] = (uint8_t)(i * 8 - 60); /* signed: -60, -52, ..., 60 */
        b[i] = (uint8_t)((i < 8) ? 5 : 100);
    }

    uint8_t *pa = a;
    uint8_t *pb = b;
    uint8_t *pr = result;

    __asm__ volatile(
        "ee.vld.128.ip q0, %[pa], 0\n"
        "ee.vld.128.ip q1, %[pb], 0\n"
        "ee.vadds.s8 q2, q0, q1\n"
        "ee.vst.128.ip q2, %[pr], 0\n"
        : [pa] "+r"(pa), [pb] "+r"(pb), [pr] "+r"(pr)
        :
        : "memory");

    int mismatches = 0;
    for (int i = 0; i < 16; i++) {
        int8_t expected = sat_add_s8((int8_t)a[i], (int8_t)b[i]);
        if ((int8_t)result[i] != expected) {
            mismatches++;
        }
    }

    uart_puts("GM_QEMUBENCH_PIE: a=");
    for (int i = 0; i < 16; i++) {
        uart_put_hex_byte(a[i]);
    }
    uart_puts(" b=");
    for (int i = 0; i < 16; i++) {
        uart_put_hex_byte(b[i]);
    }
    uart_puts(" result=");
    for (int i = 0; i < 16; i++) {
        uart_put_hex_byte(result[i]);
    }
    uart_puts(" expected=");
    for (int i = 0; i < 16; i++) {
        uart_put_hex_byte((uint8_t)sat_add_s8((int8_t)a[i], (int8_t)b[i]));
    }
    uart_puts("\n");

    if (mismatches == 0) {
        uart_puts("GM_QEMUBENCH_PIE: PASS ee.vld.128.ip+ee.vadds.s8 bit-exact vs scalar reference\n");
    } else {
        uart_puts("GM_QEMUBENCH_PIE: FAIL mismatches=");
        uart_put_hex_byte((uint8_t)mismatches);
        uart_puts("\n");
    }
    uart_puts("GM_QEMUBENCH_PIE_DONE\n");

    for (;;) {
        /* Nothing to return to: spin so QEMU has a stable state to
         * screendump/inspect if needed, instead of running off the end of
         * mapped IRAM into undefined behavior. */
    }
}
