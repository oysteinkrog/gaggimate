/* Horizon real-Xtensa execution test, linked to the shared boot harness.
 * The kernel below is a verbatim copy of AnimHorizon.cpp, including its
 * scalar tail. The independent C loop evaluates the page's Q4 sum/gather.
 * Exhaust every nonnegative (ct, rt) pair with ct+rt <= 4095, so the
 * production operand range, every Q4 boundary and all parameter extremes
 * are covered. Separately exercise every width 0..480, all four legal
 * output alignments modulo 16, all 65536 palette words, and synthetic
 * frame tables for the 16 parameter corners at startup and uint32 wraps.
 * Canary words before/after every output catch prefix or tail writes.
 * No OS, libc, heap, libm, or global constructors. */
#include <stdint.h>
#define GM_ANIM_IRAM
#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)
static void puts_uart(const char *s) {
    while (*s) {
        if (*s == '\n') UART0_FIFO = '\r';
        UART0_FIFO = (uint8_t)*s++;
    }
}

/* Isolated EE.MOVI.32.A probe, run before using this instruction in the
 * production kernel. Each selector must preserve all 32 bits of its lane. */
static int probe_lane_moves(void) {
    uint32_t words[4] __attribute__((aligned(16))) = {0x01234567u, 0x89abcdefu, 0xfedcba98u, 0x76543210u};
    const uint32_t *p = words;
    uint32_t a, b, c, d;
    __asm__ volatile("ee.vld.128.ip q0, %[p], 0\n"
                     "ee.movi.32.a q0, %[a], 0\n"
                     "ee.movi.32.a q0, %[b], 1\n"
                     "ee.movi.32.a q0, %[c], 2\n"
                     "ee.movi.32.a q0, %[d], 3\n"
                     : [p] "+r"(p), [a] "=&r"(a), [b] "=&r"(b), [c] "=&r"(c), [d] "=&r"(d)
                     : : "memory");
    return a == words[0] && b == words[1] && c == words[2] && d == words[3];
}

/* Verbatim production kernel begins. */
GM_ANIM_IRAM __attribute__((noinline)) void horizonRowAsm(uint16_t *out, const int16_t *ct,
                                                        const uint16_t *pal, int rt, int w) {
    uint16_t *outp = out;
    const int16_t *ctp = ct;
    const uint32_t rowPair = (uint32_t)rt | ((uint32_t)rt << 16);
    const int groups = w >> 3;
    uint32_t packed, lo, hi;
    asm volatile("ee.movi.32.q q1, %[rt], 0\n"
                 "ee.movi.32.q q1, %[rt], 1\n"
                 "ee.movi.32.q q1, %[rt], 2\n"
                 "ee.movi.32.q q1, %[rt], 3\n"
                 "addi %[out], %[out], -16\n"
                 "loopnez %[n], 1f\n"
                 "ee.vld.128.ip q0, %[ct], 16\n"
                 "addi %[out], %[out], 16\n"
                 "ee.vadds.s16 q0, q0, q1\n"
                 "ee.movi.32.a q0, %[packed], 0\n"
                 "extui %[lo], %[packed], 4, 8\n"
                 "extui %[hi], %[packed], 20, 8\n"
                 "addx2 %[lo], %[lo], %[pal]\n"
                 "addx2 %[hi], %[hi], %[pal]\n"
                 "l16ui %[hi], %[hi], 0\n"
                 "l16ui %[lo], %[lo], 0\n"
                 "slli %[hi], %[hi], 16\n"
                 "or %[hi], %[hi], %[lo]\n"
                 "s32i %[hi], %[out], 0\n"
                 "ee.movi.32.a q0, %[packed], 1\n"
                 "extui %[lo], %[packed], 4, 8\n"
                 "extui %[hi], %[packed], 20, 8\n"
                 "addx2 %[lo], %[lo], %[pal]\n"
                 "addx2 %[hi], %[hi], %[pal]\n"
                 "l16ui %[hi], %[hi], 0\n"
                 "l16ui %[lo], %[lo], 0\n"
                 "slli %[hi], %[hi], 16\n"
                 "or %[hi], %[hi], %[lo]\n"
                 "s32i %[hi], %[out], 4\n"
                 "ee.movi.32.a q0, %[packed], 2\n"
                 "extui %[lo], %[packed], 4, 8\n"
                 "extui %[hi], %[packed], 20, 8\n"
                 "addx2 %[lo], %[lo], %[pal]\n"
                 "addx2 %[hi], %[hi], %[pal]\n"
                 "l16ui %[hi], %[hi], 0\n"
                 "l16ui %[lo], %[lo], 0\n"
                 "slli %[hi], %[hi], 16\n"
                 "or %[hi], %[hi], %[lo]\n"
                 "s32i %[hi], %[out], 8\n"
                 "ee.movi.32.a q0, %[packed], 3\n"
                 "extui %[lo], %[packed], 4, 8\n"
                 "extui %[hi], %[packed], 20, 8\n"
                 "addx2 %[lo], %[lo], %[pal]\n"
                 "addx2 %[hi], %[hi], %[pal]\n"
                 "l16ui %[hi], %[hi], 0\n"
                 "l16ui %[lo], %[lo], 0\n"
                 "slli %[hi], %[hi], 16\n"
                 "or %[hi], %[hi], %[lo]\n"
                 "s32i %[hi], %[out], 12\n"
                 "1:\n"
                 : [out] "+&r"(outp), [ct] "+&r"(ctp), [packed] "=&r"(packed), [lo] "=&r"(lo), [hi] "=&r"(hi)
                 : [rt] "r"(rowPair), [n] "r"(groups), [pal] "r"(pal)
                 : "memory");
    // At most seven remaining pixels, including the 233-wide odd tail.
    // Use the original pointers: the asm output cursor names its last group.
    for (int x = groups << 3; x < w; x++) {
        out[x] = pal[(ct[x] + rt) >> 4];
    }
}
/* Verbatim production kernel ends. */


static void dec_uart(uint32_t n) {
    char buf[10];
    int used = 0;
    do { buf[used++] = (char)('0' + n % 10); n /= 10; } while (n);
    while (used) UART0_FIFO = (uint8_t)buf[--used];
}

#define WIDTH 480
#define WORDS (WIDTH + 32)
static int16_t columns[WIDTH] __attribute__((aligned(16)));
static uint16_t palette[256] __attribute__((aligned(16)));
static uint16_t got[WORDS] __attribute__((aligned(16)));
static uint16_t want[WORDS] __attribute__((aligned(16)));
static uint32_t calls, pixels;

/* Independent reimplementation of bandRef's inner arithmetic. No packed
 * stores, vector extraction, loop rounding or shared helper with asm. */
static void horizonRowRef(uint16_t *out, const int16_t *ct, const uint16_t *pal, int rt, int w) {
    for (int x = 0; x < w; x++) out[x] = pal[(ct[x] + rt) >> 4];
}

static int check(int w, int rt, int alignment) {
    const int first = 8 + alignment * 2; /* 16-byte guard + 0/4/8/12 bytes */
    for (int i = 0; i < WORDS; i++) got[i] = want[i] = (uint16_t)(0x739bu ^ (i * 71));
    horizonRowAsm(got + first, columns, palette, rt, w);
    horizonRowRef(want + first, columns, palette, rt, w);
    ++calls;
    pixels += (uint32_t)w;
    for (int i = 0; i < WORDS; i++) {
        if (got[i] != want[i]) {
            puts_uart("GM_QEMUBENCH_PIE: FAIL horizon call="); dec_uart(calls);
            puts_uart(" buffer_word="); dec_uart((uint32_t)i);
            puts_uart(" width="); dec_uart((uint32_t)w);
            puts_uart(" rt="); dec_uart((uint32_t)rt);
            puts_uart(" alignment="); dec_uart((uint32_t)alignment * 4);
            puts_uart(" got="); dec_uart(got[i]);
            puts_uart(" want="); dec_uart(want[i]);
            puts_uart("\n");
            return 0;
        }
    }
    return 1;
}

static uint32_t rng = 0x12345678u;
static uint32_t random_word(void) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

static int exhaustive_operands(void) {
    /* Every index gets a unique 16-bit word. All 8,390,656 legal operand
     * pairs are evaluated, including ct=0, rt=0, and sum=4095. */
    for (int i = 0; i < 256; i++) palette[i] = (uint16_t)((i * 257) ^ 0xa55a);
    for (int rt = 0; rt <= 4095; rt++) {
        for (int first = 0; first <= 4095 - rt; first += WIDTH) {
            int n = 4096 - rt - first;
            if (n > WIDTH) n = WIDTH;
            for (int x = 0; x < n; x++) columns[x] = (int16_t)(first + x);
            if (!check(n, rt, (rt + first) & 3)) return 0;
        }
    }
    return 1;
}

static int widths_and_palettes(void) {
    /* Minimal/zero trips, every scalar remainder, and production widths
     * 480, 240, 466, 233. Random valid inputs vary all vector lanes. */
    for (int w = 0; w <= WIDTH; w++) {
        for (int alignment = 0; alignment < 4; alignment++) {
            int rt = (int)(random_word() & 4095);
            for (int i = 0; i < 256; i++) palette[i] = (uint16_t)random_word();
            for (int x = 0; x < w; x++) columns[x] = (int16_t)(random_word() % (4096u - (uint32_t)rt));
            if (!check(w, rt, alignment)) return 0;
        }
    }
    /* Every possible RGB565 value, with varied low Q4 bits under the same
     * palette index. Packing must preserve bit 15 and zero words exactly. */
    for (int page = 0; page < 256; page++) {
        for (int i = 0; i < 256; i++) {
            palette[i] = (uint16_t)(page * 256 + i);
            columns[i] = (int16_t)(i * 16 + ((i + page) & 15));
        }
        if (!check(256, 0, page & 3)) return 0;
    }
    return 1;
}

/* Synthetic sine-table values span the page's exact -512..512 range.
 * These are deliberately discontinuous to stress independent vector lanes;
 * the host goldens, not this synthetic table, verify the actual sine shape. */
static int synthetic_sine(uint32_t phase) {
    phase &= 1023;
    if (phase == 0) return -512;
    if (phase == 1023) return 512;
    return (int)((phase * 347u) % 1025u) - 512;
}

static int parameter_corners(void) {
    static const uint32_t times[] = {0, 33, 1990, 4960, 7930, 0x1fffffff, 0x7fffffff, 0xfffffff0u, 0xffffffffu};
    static const int widths[] = {480, 240, 466, 233};
    for (int corner = 0; corner < 16; corner++) {
        int speed = (corner & 1) ? 100 : 0, height = (corner & 2) ? 100 : 0;
        int curvature = (corner & 4) ? 100 : 0, softness = (corner & 8) ? 100 : 0;
        /* Same integer palette shape, with ramp positions encoded as
         * distinguishable full RGB565 words, independent of theme hue. */
        int soft = 22 + softness * 50 / 100, glow = 100 + softness * 64 / 100;
        for (int i = 0; i < 256; i++) {
            int d = i - 128;
            int v = d >= 0 ? 42 - d * 30 / 127 : 34 + d * 24 / 128;
            int c = d - soft / 6, ac = c < 0 ? -c : c;
            if (ac < soft) { int k = 256 - ac * 256 / soft, kk = (k * k) >> 8; v += (((kk * kk) >> 8) * glow) >> 8; }
            c = d + soft / 2; ac = c < 0 ? -c : c;
            if (d < 0 && ac < soft) { int k = 256 - ac * 256 / soft, kk = (k * k) >> 8; v += (((kk * kk) >> 8) * (glow / 3)) >> 8; }
            if (v < 0) v = 0; else if (v > 255) v = 255;
            palette[i] = (uint16_t)((v * 257) ^ 0x5aa5);
        }
        for (int ti = 0; ti < 9; ti++) {
            uint32_t base = times[ti] * (uint32_t)(4 + speed * 44 / 100);
            uint32_t phu = base >> 9, phu2 = (base * 3u) >> 10;
            int offset = 520 + height * 900 / 100 + ((synthetic_sine(base >> 7) * 240) >> 9) +
                         ((synthetic_sine((base * 5u) >> 10) * 110) >> 9);
            for (int wi = 0; wi < 4; wi++) {
                int w = widths[wi], cx = w / 2, k = (curvature - 50) * 420 / 50;
                int low = 32767, high = -32768;
                for (int x = 0; x < w; x++) {
                    int dx = x - cx, q = dx * dx * 256 / (cx * cx);
                    int v = ((k * q) >> 8) + ((synthetic_sine((uint32_t)((x * 21) >> 4) - phu) * 150) >> 9) +
                            ((synthetic_sine((uint32_t)((x * 55) >> 4) + phu2) * 72) >> 9);
                    columns[x] = (int16_t)v;
                    if (v < low) low = v;
                    if (v > high) high = v;
                }
                int cap = high - low + 40;
                for (int x = 0; x < w; x++) {
                    /* +/-115 Q4 units bounds the rounded page dither when
                     * ditherAmp reaches its cap of 16, times 0.45*16. */
                    int v = columns[x] - low + (x & 1 ? 115 : -115);
                    columns[x] = (int16_t)(v < 0 ? 0 : (v > cap ? cap : v));
                }
                for (int y = 0; y < w; y += w / 7) {
                    int rt = offset + (w - 1 - y) * 2200 / (w - 1);
                    if (rt < 0) rt = 0; else if (rt > 4095 - cap) rt = 4095 - cap;
                    if (!check(w, rt, (corner + y) & 3)) return 0;
                }
            }
        }
    }
    return 1;
}

int main(void) {
    /* Bare-metal startup only. Never copy this write into production. */
    uint32_t cp = 0xff;
    __asm__ volatile("wsr %0, cpenable\nrsync\n" : : "r"(cp) : "memory");
    int ok = probe_lane_moves();
    if (!ok) puts_uart("GM_QEMUBENCH_PIE: FAIL horizon EE.MOVI.32.A probe\n");
    if (ok) ok = exhaustive_operands();
    if (ok) ok = widths_and_palettes();
    if (ok) ok = parameter_corners();
    if (ok) {
        puts_uart("GM_QEMUBENCH_PIE: PASS horizon bit-exact calls="); dec_uart(calls);
        puts_uart(" pixels="); dec_uart(pixels);
        puts_uart(" mismatches=0 (all Q4 operands, widths 0..480, alignments, RGB565, 16 parameter corners)\n");
    }
    puts_uart("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
