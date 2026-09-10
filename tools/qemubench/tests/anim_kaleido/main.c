/* Freestanding harness-mode test. The two production kernel functions below
 * are copied verbatim from AnimKaleido.cpp. CPENABLE is written only by main
 * in this bare-metal harness, never by either production kernel.
 *
 * Tests all 4096 map indices and 65536 colours, zero/minimal/production band lengths,
 * every halfword source alignment and word output alignment, in-place
 * palette resolution, every sum 0..1020 with every byte vignette 0..255,
 * mirrored angular endpoints, and full frames at parameter extremes.
 */
#include <stdint.h>
#define GM_ANIM_IRAM
#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)

static void puts_uart(const char *s) {
    while (*s) {
        if (*s == '\n') UART0_FIFO = '\r';
        UART0_FIFO = (uint8_t)*s++;
    }
}
static void dec_uart(uint32_t v) {
    char b[12]; int n = 0;
    do { b[n++] = '0' + v % 10; v /= 10; } while (v);
    while (n) UART0_FIFO = b[--n];
}

GM_ANIM_IRAM __attribute__((noinline)) void kaleidoGatherAsm(uint16_t *out, const uint16_t *in,
                                                           const uint16_t *cells, int nQuads) {
    uint32_t t0, t1, t2, t3;
    asm volatile("loopnez %[n], 1f\n"
                 "l16ui   %[t0], %[in], 0\n"
                 "l16ui   %[t1], %[in], 2\n"
                 "addx2   %[t0], %[t0], %[cells]\n"
                 "addx2   %[t1], %[t1], %[cells]\n"
                 "l16ui   %[t2], %[in], 4\n"
                 "l16ui   %[t3], %[in], 6\n"
                 "l16ui   %[t0], %[t0], 0\n"
                 "l16ui   %[t1], %[t1], 0\n"
                 "addx2   %[t2], %[t2], %[cells]\n"
                 "addx2   %[t3], %[t3], %[cells]\n"
                 "l16ui   %[t2], %[t2], 0\n"
                 "l16ui   %[t3], %[t3], 0\n"
                 "slli    %[t1], %[t1], 16\n"
                 "or      %[t0], %[t0], %[t1]\n"
                 "slli    %[t3], %[t3], 16\n"
                 "or      %[t2], %[t2], %[t3]\n"
                 "s32i    %[t0], %[out], 0\n"
                 "s32i    %[t2], %[out], 4\n"
                 "addi    %[in], %[in], 8\n"
                 "addi    %[out], %[out], 8\n"
                 "1:\n"
                 : [out] "+&r"(out), [in] "+&r"(in), [t0] "=&r"(t0), [t1] "=&r"(t1),
                   [t2] "=&r"(t2), [t3] "=&r"(t3)
                 : [cells] "r"(cells), [n] "r"(nQuads)
                 : "memory");
}

GM_ANIM_IRAM __attribute__((noinline)) void kaleidoSmoothAsm(uint16_t *out, const uint8_t *lo,
                                                           const uint8_t *mid, const uint8_t *hi,
                                                           const uint8_t *rim, int n16) {
    const uint32_t quarters = 0x00400040u; // two unsigned 16-bit copies of 64
    asm volatile("ee.movi.32.q q7, %[quarters], 0\n"
                 "ee.movi.32.q q7, %[quarters], 1\n"
                 "ee.movi.32.q q7, %[quarters], 2\n"
                 "ee.movi.32.q q7, %[quarters], 3\n"
                 "ssai 8\n"
                 "loopnez %[n], 1f\n"
                 "ee.vld.128.ip q0, %[lo], 16\n"
                 "ee.vld.128.ip q1, %[mid], 16\n"
                 "ee.vld.128.ip q2, %[hi], 16\n"
                 "ee.zero.q q3\n"
                 "ee.vzip.8 q0, q3\n"
                 "ee.zero.q q4\n"
                 "ee.vzip.8 q1, q4\n"
                 "ee.zero.q q5\n"
                 "ee.vzip.8 q2, q5\n"
                 "ee.vadds.s16 q1, q1, q1\n"
                 "ee.vadds.s16 q4, q4, q4\n"
                 "ee.vadds.s16 q0, q0, q2\n"
                 "ee.vadds.s16 q3, q3, q5\n"
                 "ee.vadds.s16 q0, q0, q1\n"
                 "ee.vadds.s16 q3, q3, q4\n"
                 "ee.vmul.u16 q0, q0, q7\n"
                 "ee.vmul.u16 q3, q3, q7\n"
                 "ee.vld.128.ip q1, %[rim], 16\n"
                 "ee.zero.q q2\n"
                 "ee.vzip.8 q1, q2\n"
                 "ee.vmul.u16 q0, q0, q1\n"
                 "ee.vmul.u16 q3, q3, q2\n"
                 "ee.vst.128.ip q0, %[out], 16\n"
                 "ee.vst.128.ip q3, %[out], 16\n"
                 "1:\n"
                 : [out] "+&r"(out), [lo] "+&r"(lo), [mid] "+&r"(mid), [hi] "+&r"(hi), [rim] "+&r"(rim)
                 : [quarters] "r"(quarters), [n] "r"(n16)
                 : "memory");
}

/* These are the portable pixel loop and cell arithmetic from bandRef/frame.
 * Keeping the reference independent of the quad and vector layouts detects
 * lane ordering and intermediate-rounding mistakes in the actual kernels. */
static void gatherRef(uint16_t *out, const uint16_t *in, const uint16_t *cells, int n) {
    for (int i = 0; i < n; i++) out[i] = cells[in[i]];
}
static void smoothRef(uint16_t *out, const uint8_t *lo, const uint8_t *mid,
                      const uint8_t *hi, const uint8_t *rim, int n) {
    for (int i = 0; i < n; i++) {
        int v = (lo[i] + 2 * mid[i] + hi[i]) >> 2;
        out[i] = (v * rim[i]) >> 8;
    }
}

#define CAP 1040
static uint16_t table[4096] __attribute__((aligned(16)));
static uint16_t indices[CAP] __attribute__((aligned(16)));
static uint16_t got[CAP] __attribute__((aligned(16)));
static uint16_t want[CAP] __attribute__((aligned(16)));
static uint8_t loBytes[64] __attribute__((aligned(16)));
static uint8_t midBytes[64] __attribute__((aligned(16)));
static uint8_t hiBytes[64] __attribute__((aligned(16)));
static uint8_t rimBytes[64] __attribute__((aligned(16)));
static uint8_t field[4096] __attribute__((aligned(16)));
static uint16_t texGot[4096] __attribute__((aligned(16)));
static uint16_t texWant[4096] __attribute__((aligned(16)));
static uint32_t calls, checked, bad, firstCall, firstLane, firstGot, firstWant;
static uint32_t rng = 0x12345678u;

static uint32_t rand32(void) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng;
}
static void check(const uint16_t *g, const uint16_t *w, int n) {
    calls++;
    for (int i = 0; i < n; i++) {
        checked++;
        if (g[i] != w[i]) {
            if (!bad) { firstCall = calls; firstLane = i; firstGot = g[i]; firstWant = w[i]; }
            bad++;
        }
    }
}
static void resetOut(void) {
    for (int i = 0; i < CAP; i++) got[i] = want[i] = 0xa55a;
}

/* One-instruction probe for MOVI.32.Q, before using it to build q7. Each
 * selector writes one word into an otherwise zero vector. The vld/vst and
 * zero instructions are already verified by the shared PIE smoke tests. */
#define PROBE_MOVI(SEL) do { \
    uint16_t *dst = got; \
    uint32_t word = 0x1234abcdu; \
    asm volatile("ee.zero.q q7\n" \
                 "ee.movi.32.q q7, %[v], " #SEL "\n" \
                 "ee.vst.128.ip q7, %[out], 0\n" \
                 : [out] "+&r"(dst) : [v] "r"(word) : "memory"); \
    for (int k = 0; k < 8; k++) want[k] = 0; \
    want[2 * (SEL)] = 0xabcd; want[2 * (SEL) + 1] = 0x1234; \
    check(got, want, 8); \
} while (0)

static void testGather(void) {
    /* A permutation makes every table index distinguishable, including all
     * 65536 RGB565 values. Production map indices are only 0..4095. */
    for (int base = 0; base < 65536; base += 1024) {
        for (int i = 0; i < 4096; i++) table[i] = (uint16_t)(((base & ~4095) + i) * 4051u + 17u);
        resetOut();
        for (int i = 0; i < 1024; i++) indices[i] = (base + i) & 4095;
        kaleidoGatherAsm(got, indices, table, 256);
        gatherRef(want, indices, table, 1024);
        check(got, want, CAP); /* includes guards beyond the last write */
    }
    static const int lengths[] = {0, 1, 2, 3, 4, 5, 7, 8, 15, 16, 31, 63, 64,
                           232, 233, 240, 466, 480, 932, 960};
    for (int align = 0; align < 8; align++) {
        for (unsigned j = 0; j < sizeof(lengths)/sizeof(lengths[0]); j++) {
            int n = lengths[j], off = (align & 3) * 2;
            resetOut();
            for (int i = 0; i < CAP; i++) indices[i] = rand32() & 4095;
            kaleidoGatherAsm(got + off, indices + align, table, n / 4);
            /* The identical tail used by production band(). */
            for (int i = n & ~3; i < n; i++) got[off+i] = table[indices[align+i]];
            gatherRef(want + off, indices + align, table, n);
            check(got, want, CAP);
        }
    }
    for (int j = 0; j < 32; j++) {
        resetOut();
        for (int i = 0; i < 64; i++) got[i] = want[i] = rand32() & 255;
        kaleidoGatherAsm(got, got, table, 16);
        gatherRef(want, want, table, 64);
        check(got, want, CAP);
    }
}

static void testSmooth(void) {
    /* For every sum, construct valid input bytes with that sum. Sweep all
     * rim factors for each sum, including the shift boundaries that would
     * fail if the >>2 and >>8 were incorrectly combined. */
    for (int sum = 0; sum <= 1020; sum++) {
        int l = sum > 255 ? 255 : sum;
        int m = (sum-l)/2; if (m > 255) m = 255;
        int h = sum-l-2*m;
        for (int block = 0; block < 4; block++) {
            resetOut();
            for (int i = 0; i < 64; i++) {
                loBytes[i] = l; midBytes[i] = m; hiBytes[i] = h;
                rimBytes[i] = block*64+i;
            }
            kaleidoSmoothAsm(got, loBytes, midBytes, hiBytes, rimBytes, 4);
            smoothRef(want, loBytes, midBytes, hiBytes, rimBytes, 64);
            check(got, want, 72); /* eight untouched lanes after output */
        }
    }
    for (int n16 = 0; n16 <= 4; n16++) {
        resetOut();
        for (int i = 0; i < 64; i++) {
            loBytes[i] = rand32(); midBytes[i] = rand32(); hiBytes[i] = rand32(); rimBytes[i] = rand32();
        }
        kaleidoSmoothAsm(got, loBytes, midBytes, hiBytes, rimBytes, n16);
        smoothRef(want, loBytes, midBytes, hiBytes, rimBytes, n16*16);
        check(got, want, 72);
    }
}

static void testFrames(void) {
    /* Scale steps cover every value produced by scale 0..100. Brightness
     * 0, 62, 100 gives gains 80, 189, 256. The synthetic source is fully
     * byte-valued, and wide phases exercise time/speed extremes and wrap. */
    static const uint32_t phases[] = {0u, 1u, 255u, 256u, 0x00543210u, 0x057fffffu, 0xffffffffu};
    static const int brightness[] = {0, 62, 100};
    for (int scale = 0; scale <= 100; scale++) {
        uint32_t scA = 2+(scale*2+50)/100, scR = 4+(scale*4+50)/100;
        for (unsigned ph = 0; ph < sizeof(phases)/sizeof(phases[0]); ph++) {
            uint32_t ox = phases[ph], oy = phases[ph]*2;
            int bright = 80+(brightness[ph%3]*176+50)/100;
            for (int i = 0; i < 256; i++) table[i] = (uint16_t)((i*bright/256)*257u);
            for (int r = 0; r < 64; r++) rimBytes[r] = 104 + (r*37)%133;
            for (int a = 0; a < 64; a++) {
                uint32_t aSh = (a+ox)*scA;
                for (int r = 0; r < 64; r++) {
                    uint32_t sa = ((aSh*(r+5u))>>9)&255;
                    uint32_t sr = (((r*scR)>>3)+oy)&255;
                    int sample = (sa*73u+sr*151u)&255;
                    field[a*64+r] = 44+((sample*132)>>8);
                }
            }
            for (int a = 0; a < 64; a++) {
                int c = a*64, l = (a==0 ? 1:a-1)*64, h = (a==63 ? 62:a+1)*64;
                kaleidoSmoothAsm(texGot+c, field+l, field+c, field+h, rimBytes, 4);
                kaleidoGatherAsm(texGot+c, texGot+c, table, 16);
                smoothRef(texWant+c, field+l, field+c, field+h, rimBytes, 64);
                gatherRef(texWant+c, texWant+c, table, 64);
            }
            check(texGot, texWant, 4096);
        }
    }
}

int main(void) {
    uint32_t cp = 8; /* CP3 only; bare metal has no lazy-enable handler. */
    asm volatile("wsr %0, cpenable\nrsync\n" :: "r"(cp) : "memory");
    PROBE_MOVI(0); PROBE_MOVI(1); PROBE_MOVI(2); PROBE_MOVI(3);
    if (!bad) { testGather(); testSmooth(); testFrames(); }
    if (!bad) {
        puts_uart("GM_QEMUBENCH_PIE: PASS kaleido gather+smooth calls="); dec_uart(calls);
        puts_uart(" checked="); dec_uart(checked); puts_uart(" mismatches=0\n");
    } else {
        puts_uart("GM_QEMUBENCH_PIE: FAIL kaleido call="); dec_uart(firstCall);
        puts_uart(" lane="); dec_uart(firstLane);
        puts_uart(" got="); dec_uart(firstGot); puts_uart(" want="); dec_uart(firstWant);
        puts_uart(" mismatches="); dec_uart(bad); puts_uart("\n");
    }
    puts_uart("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
