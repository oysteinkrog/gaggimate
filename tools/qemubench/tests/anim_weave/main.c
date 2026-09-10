/* Freestanding, harness-mode parity test for AnimWeave.cpp. The kernel is
 * copied verbatim, including its signature, attributes and constraints.
 * The reference below independently implements the page's Q16.16 lookup.
 * No libc, allocation, floating point or constructors. Only main enables
 * CP3; production leaves CPENABLE to FreeRTOS's lazy context handling.
 *
 * Coverage: every RGB565 value, every 64x64 cell, every 16-bit fractional
 * value, every integer du/dv in [-40141,40141], all quad counts 0..128,
 * uint32 phase wraps, zero and signed steps, extreme simultaneous steps,
 * 233/240/466/480 widths, 0..3-pixel tails, 512-pixel chunk boundaries,
 * all four legal output alignments modulo 16, a 2-byte-aligned texture,
 * canaries and absolute-row call shapes. The synthetic square step range
 * includes all speed/scale 0 and 100 cases as well as impossible diagonal
 * combinations beyond the production circle. Brightness 0..100 is covered
 * by all 65536 texture values; it never enters the cursor arithmetic. */
#include <stdint.h>
#define GM_ANIM_IRAM
#define asm __asm__
#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)

static void puts_uart(const char *s) {
    while (*s) {
        if (*s == '\n') UART0_FIFO = '\r';
        UART0_FIFO = (uint8_t)*s++;
    }
}

static void hex_uart(uint32_t v) {
    const char *hex = "0123456789abcdef";
    for (int s = 28; s >= 0; s -= 4) UART0_FIFO = hex[(v >> s) & 15];
    UART0_FIFO = ' ';
}

static int probe(void) {
    int32_t a[4] __attribute__((aligned(16))) = {-2147483647 - 1, -65537, 65535, 2147483647};
    int32_t b[4] __attribute__((aligned(16))) = {-1, 1, 1, 1};
    int32_t sums[4] __attribute__((aligned(16)));
    int32_t shifts[4] __attribute__((aligned(16)));
    const int32_t *pa = a, *pb = b;
    int32_t *ps = sums, *pt = shifts;
    int32_t l0, l1, l2, l3;
    asm volatile("ee.vld.128.ip q0, %[a], 0\n"
                 "ee.vld.128.ip q1, %[b], 0\n"
                 "ssai 16\n"
                 "ee.vadds.s32 q2, q0, q1\n"
                 "ee.vst.128.ip q2, %[s], 0\n"
                 "ee.vsr.32 q3, q0\n"
                 "ee.vst.128.ip q3, %[t], 0\n"
                 "ee.movi.32.a q0, %[l0], 0\n"
                 "ee.movi.32.a q0, %[l1], 1\n"
                 "ee.movi.32.a q0, %[l2], 2\n"
                 "ee.movi.32.a q0, %[l3], 3\n"
                 : [a] "+&r"(pa), [b] "+&r"(pb), [s] "+&r"(ps), [t] "+&r"(pt),
                   [l0] "=&r"(l0), [l1] "=&r"(l1), [l2] "=&r"(l2), [l3] "=&r"(l3)
                 : : "memory");
    /* QEMU clips negative saturation at -INT32_MAX. The production cursor
     * proof stays below 1.6e9 in magnitude and never relies on saturation. */
    return sums[0] == -2147483647 && sums[1] == -65536 && sums[2] == 65536 && sums[3] == a[3] &&
           shifts[0] == -32768 && shifts[1] == -2 && shifts[2] == 0 && shifts[3] == 32767 &&
           l0 == a[0] && l1 == a[1] && l2 == a[2] && l3 == a[3];
}

/* BEGIN verbatim production kernel. */
GM_ANIM_IRAM __attribute__((noinline)) void weaveQuadAsm(uint16_t *out, const uint16_t *texture,
                                                        const int32_t *data, uint32_t u, uint32_t v, int nQuads) {
    u &= 0x003fffffu;
    v = (v & 0x003fffffu) << 6;
    int32_t t0, t1, t2, t3;
    asm volatile("ee.movi.32.q q0, %[u], 0\n"
                 "ee.movi.32.q q0, %[u], 1\n"
                 "ee.movi.32.q q0, %[u], 2\n"
                 "ee.movi.32.q q0, %[u], 3\n"
                 "ee.movi.32.q q1, %[v], 0\n"
                 "ee.movi.32.q q1, %[v], 1\n"
                 "ee.movi.32.q q1, %[v], 2\n"
                 "ee.movi.32.q q1, %[v], 3\n"
                 "ee.vld.128.ip q6, %[data], 16\n"
                 "ee.vld.128.ip q7, %[data], 16\n"
                 "ee.vadds.s32 q0, q0, q6\n"
                 "ee.vadds.s32 q1, q1, q7\n"
                 "ee.vld.128.ip q2, %[data], 16\n"
                 "ee.vld.128.ip q3, %[data], 16\n"
                 "ee.vld.128.ip q4, %[data], 16\n"
                 "ee.vld.128.ip q5, %[data], 16\n"
                 "ssai 16\n"
                 "loopnez %[n], 1f\n"
                 "ee.vsr.32 q6, q0\n"
                 "ee.vsr.32 q7, q1\n"
                 "ee.andq q6, q6, q4\n"
                 "ee.andq q7, q7, q5\n"
                 "ee.orq q6, q6, q7\n"
                 "ee.movi.32.a q6, %[t0], 0\n"
                 "ee.movi.32.a q6, %[t1], 1\n"
                 "addx2 %[t0], %[t0], %[tex]\n"
                 "addx2 %[t1], %[t1], %[tex]\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "ee.movi.32.a q6, %[t2], 2\n"
                 "slli %[t1], %[t1], 16\n"
                 "ee.movi.32.a q6, %[t3], 3\n"
                 "or %[t0], %[t0], %[t1]\n"
                 "s32i %[t0], %[out], 0\n"
                 "addx2 %[t2], %[t2], %[tex]\n"
                 "addx2 %[t3], %[t3], %[tex]\n"
                 "l16ui %[t2], %[t2], 0\n"
                 "l16ui %[t3], %[t3], 0\n"
                 "ee.vadds.s32 q0, q0, q2\n"
                 "slli %[t3], %[t3], 16\n"
                 "ee.vadds.s32 q1, q1, q3\n"
                 "or %[t2], %[t2], %[t3]\n"
                 "s32i %[t2], %[out], 4\n"
                 "addi %[out], %[out], 8\n"
                 "1:\n"
                 : [out] "+&r"(out), [data] "+&r"(data), [t0] "=&r"(t0), [t1] "=&r"(t1),
                   [t2] "=&r"(t2), [t3] "=&r"(t3)
                 : [u] "r"(u), [v] "r"(v), [tex] "r"(texture), [n] "r"(nQuads)
                 : "memory");
}

/* END verbatim production kernel. */

static uint32_t random_state = 0x57454156u;
static uint32_t random32(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

#define MAX_W 1040
static uint16_t texture_store[4097] __attribute__((aligned(16)));
static int32_t phase_data[24] __attribute__((aligned(16)));
static uint16_t output[MAX_W + 16] __attribute__((aligned(16)));
static uint16_t expected[MAX_W];
static uint32_t calls, pixels, mismatches;

static void dec_uart(uint32_t n) {
    char b[12];
    int k = 0;
    do { b[k++] = '0' + n % 10; n /= 10; } while (n);
    while (k) UART0_FIFO = b[--k];
}

static void fill_data(int du, int dv) {
    for (int i = 0; i < 4; i++) {
        phase_data[i] = i * du;
        phase_data[4+i] = i * dv * 64;
        phase_data[8+i] = 4 * du;
        phase_data[12+i] = 4 * dv * 64;
        phase_data[16+i] = 63;
        phase_data[20+i] = 0x0fc0;
    }
}

/* Independent C reference: separate integer texel coordinates, then index
 * row*64+column. Unsigned DDA additions reproduce JS's signed |0 wrap. */
static void row_ref(uint16_t *dst, const uint16_t *tex, uint32_t u, uint32_t v,
                    int du, int dv, int width) {
    for (int x = 0; x < width; x++) {
        uint32_t column = (u >> 16) % 64;
        uint32_t row = (v >> 16) % 64;
        dst[x] = tex[row * 64 + column];
        u += (uint32_t)du;
        v += (uint32_t)dv;
    }
}

/* Same chunk and tail glue as production band(), with synthetic row state. */
static void row_asm(uint16_t *dst, const uint16_t *tex, uint32_t u, uint32_t v,
                    int du, int dv, int width) {
    int left = width;
    while (left >= 4) {
        const int quads = left >= 512 ? 128 : left / 4;
        weaveQuadAsm(dst, tex, phase_data, u, v, quads);
        const int n = 4 * quads;
        u += (uint32_t)n * (uint32_t)du;
        v += (uint32_t)n * (uint32_t)dv;
        dst += n;
        left -= n;
    }
    while (left-- > 0) {
        *dst++ = tex[((v >> 10) & 0x0fc0u) | ((u >> 16) & 63u)];
        u += (uint32_t)du;
        v += (uint32_t)dv;
    }
}

static void bad(int lane, uint32_t got, uint32_t want) {
    if (mismatches++ == 0) {
        puts_uart("GM_QEMUBENCH_PIE: FAIL weaveQuadAsm call="); dec_uart(calls);
        puts_uart(" lane="); dec_uart((uint32_t)lane);
        puts_uart(" got="); hex_uart(got); puts_uart("want="); hex_uart(want);
        puts_uart("\n");
    }
}

static void check(uint32_t u, uint32_t v, int du, int dv, int width, int align, int tex_align) {
    uint16_t *out = output + 4 + align * 2;
    const uint16_t *tex = texture_store + tex_align;
    calls++;
    fill_data(du, dv);
    out[-2] = 0x719b; out[-1] = 0xc56a;
    for (int i = 0; i < width; i++) out[i] = 0xbaad;
    out[width] = 0x286e; out[width+1] = 0x5ad3;
    row_ref(expected, tex, u, v, du, dv, width);
    if (width == 0) weaveQuadAsm(out, tex, phase_data, u, v, 0);
    row_asm(out, tex, u, v, du, dv, width);
    for (int i = 0; i < width; i++) {
        if (out[i] != expected[i]) bad(i, out[i], expected[i]);
    }
    if (out[-2] != 0x719b) bad(-2, out[-2], 0x719b);
    if (out[-1] != 0xc56a) bad(-1, out[-1], 0xc56a);
    if (out[width] != 0x286e) bad(width, out[width], 0x286e);
    if (out[width+1] != 0x5ad3) bad(width+1, out[width+1], 0x5ad3);
    pixels += (uint32_t)width;
}

int main(void) {
    uint32_t cp = 8;
    asm volatile("wsr %0, cpenable\nrsync\n" : : "r"(cp));
    if (!probe()) {
        puts_uart("GM_QEMUBENCH_PIE: FAIL weave PIE instruction probes\nGM_QEMUBENCH_PIE_DONE\n");
        for (;;) {}
    }
    puts_uart("GM_QEMUBENCH_PIE: weave PIE instruction probes OK\n");

    /* Every RGB565 value and every gather address, including the last cell.
     * A one-halfword texture offset proves it is only read with scalar loads. */
    for (int block = 0; block < 16; block++) {
        for (int i = 0; i <= 4096; i++) texture_store[i] = (uint16_t)(i + block * 4096 - (block & 1));
        for (int i = 0; i < 4096; i++) {
            check((uint32_t)(i & 63) << 16, (uint32_t)(i >> 6) << 16,
                  0, 0, 4, i & 3, block & 1);
        }
    }
    /* All tile entries are distinct, so a wrong coordinate cannot hide. */
    for (int i = 0; i <= 4096; i++) texture_store[i] = (uint16_t)(i * 13 + 7);

    for (uint32_t f = 0; f < 65536; f++) {
        check(((f & 63) << 16) | f, (((f >> 6) & 63) << 16) | (65535-f),
              (f & 1) ? 40141 : -40141, (f & 2) ? 40141 : -40141, 4, f & 3, f & 1);
    }
    for (int step = -40141; step <= 40141; step++) {
        check(random32(), random32(), step, -step, 4, step & 3, step & 1);
    }
    for (int q = 0; q <= 128; q++) {
        for (int a = 0; a < 4; a++) {
            check(0xffffffffu, 0xffc00000u, 40141, -40141, q * 4, a, 0);
            check(0x80000000u, 0x7fffffffu, -40141, 40141, q * 4, a, 1);
        }
    }
    static const int widths[] = {0,1,2,3,4,5,7,8,9,233,240,466,480,511,512,513,1025};
    static const int steps[] = {-40141,-32768,-1,0,1,32768,40141};
    for (int wi = 0; wi < (int)(sizeof(widths)/sizeof(widths[0])); wi++) {
        for (int i = 0; i < 7; i++) {
            for (int j = 0; j < 7; j++) {
                for (int a = 0; a < 4; a++) {
                    check(random32(), random32(), steps[i], steps[j], widths[wi], a, a & 1);
                }
            }
        }
    }
    /* Absolute-row state for contiguous, single-row and parity-skipping
     * sequences, including odd y0. Repeated y must reproduce the same row. */
    for (int parity = 0; parity < 2; parity++) {
        for (int y = parity; y < 480; y += 2) {
            const uint32_t u = 0xfff00001u - (uint32_t)y * (uint32_t)-23457;
            const uint32_t v = 0x7ffffffeu + (uint32_t)y * 32768u;
            check(u, v, 32768, -23457, 480, (y >> 1) & 3, y & 1);
            check(u, v, 32768, -23457, 233, (y >> 1) & 3, y & 1);
        }
    }
    if (mismatches == 0) {
        puts_uart("GM_QEMUBENCH_PIE: PASS weaveQuadAsm calls="); dec_uart(calls);
        puts_uart(" pixels="); dec_uart(pixels);
        puts_uart(" mismatches=0 guards=OK probes=OK\n");
    }
    puts_uart("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
