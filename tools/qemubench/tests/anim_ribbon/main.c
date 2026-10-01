/* Harness mode, no libc. The production functions below are verbatim from
 * AnimRibbon.cpp. Separate probes for MOVI.32.A/Q and XORQ passed before
 * the kernel was written; they remain part of this regression run. */
#include <stdint.h>
#define GM_ANIM_IRAM
#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)
static void puts_uart(const char *s) {
    while (*s) { if (*s == '\n') UART0_FIFO = '\r'; UART0_FIFO = (uint8_t)*s++; }
}
static int probeMovesAndXor(void) {
    const uint32_t a = 0x7fff8000u, b = 0xffff0000u;
    uint32_t c, d;
    asm volatile("ee.movi.32.q q0, %[a], 0\n"
                 "ee.movi.32.q q0, %[b], 3\n"
                 "ee.movi.32.a q0, %[c], 0\n"
                 "ee.movi.32.a q0, %[d], 3\n"
                 : [c] "=&r"(c), [d] "=&r"(d)
                 : [a] "r"(a), [b] "r"(b) : "memory");
    if (c != a || d != b) return 1;
    asm volatile("ee.movi.32.q q1, %[a], 0\n"
                 "ee.movi.32.q q2, %[b], 0\n"
                 "ee.xorq q3, q1, q2\n"
                 "ee.movi.32.a q3, %[c], 0\n"
                 : [c] "=&r"(c) : [a] "r"(a), [b] "r"(b) : "memory");
    return c != (a ^ b);
}

// BEGIN VERBATIM PRODUCTION KERNELS
// Prefix and tail are scalar; only fully aligned 16-byte destinations reach
// VST, which silently masks address bits. The main loop is one instruction
// per eight background pixels, plus setup once per span.
GM_ANIM_IRAM __attribute__((noinline)) void ribbonFillAsm(uint16_t *out, int color, int n) {
    if (n <= 0) return;
    while (n > 0 && ((uintptr_t)out & 15u) != 0) {
        *out++ = (uint16_t)color;
        --n;
    }
    const int blocks = n >> 3;
    if (blocks != 0) {
        const uint32_t packed = (uint32_t)color | ((uint32_t)color << 16);
        asm volatile("ee.movi.32.q q0, %[color], 0\n"
                     "ee.movi.32.q q0, %[color], 1\n"
                     "ee.movi.32.q q0, %[color], 2\n"
                     "ee.movi.32.q q0, %[color], 3\n"
                     "loopnez %[n], 1f\n"
                     "ee.vst.128.ip q0, %[out], 16\n"
                     "1:\n"
                     : [out] "+&r"(out)
                     : [color] "r"(packed), [n] "r"(blocks)
                     : "memory");
    }
    for (int i = 0; i < (n & 7); i++) *out++ = (uint16_t)color;
}

// The distance is nonnegative Q8, stepped toward or away from the face.
// Caller trims buckets outside 0..288, exactly the page's no-dither case.
// slope is src-bg, -2..176; bg is 14..32; dr is -2..2. The result stays
// inside 12..192, so the page's clamp is redundant for every parameter set.
// 16 instructions per halo pixel, one MULL, no immediate load consumers.
// The independent bg add hides the profile load; distance stepping hides
// the palette load. The two serial gathers make this a scalar LOOPNEZ.
GM_ANIM_IRAM __attribute__((noinline)) void ribbonHaloAsm(uint16_t *out, const uint8_t *profile,
                                                       const uint16_t *pal, const int16_t *dr,
                                                       int x, int distance, int step, int slope, int bg, int n) {
    if (n <= 0) return;
    int t0, t1;
    asm volatile("loopnez %[n], 1f\n"
                 "extui %[t0], %[x], 0, 3\n"
                 "addx2 %[t0], %[t0], %[dr]\n"
                 "srai %[t1], %[d], 4\n"
                 "l16si %[t0], %[t0], 0\n"
                 "add %[t1], %[t1], %[gt]\n"
                 "l8ui %[t1], %[t1], 0\n"
                 "add %[t0], %[t0], %[bg]\n"
                 "mull %[t1], %[t1], %[slope]\n"
                 "addi %[x], %[x], 1\n"
                 "srai %[t1], %[t1], 8\n"
                 "add %[t0], %[t0], %[t1]\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "add %[d], %[d], %[step]\n"
                 "s16i %[t0], %[out], 0\n"
                 "addi %[out], %[out], 2\n"
                 "1:\n"
                 : [out] "+&r"(out), [x] "+&r"(x), [d] "+&r"(distance),
                   [t0] "=&r"(t0), [t1] "=&r"(t1)
                 : [gt] "r"(profile), [pal] "r"(pal), [dr] "r"(dr), [step] "r"(step),
                   [slope] "r"(slope), [bg] "r"(bg), [n] "r"(n)
                 : "memory");
}

// work is the 16-byte allocHot scratch, fully overwritten on every call.
// The scalar prefix aligns OUT to four bytes for paired S32I stores. The
// only VLD reads work, whose alignment band() proves, never the output or dither.
// Eight complete pixels are required before constructing any vector lanes.
//
// Loop body: XORQ, VADDS.S16, four ten-instruction gather/pack/store pairs,
// ADDI = 43 instructions per eight pixels, 5.375 per face pixel. Each pair
// loads both colors before consuming either, leaving no load-use interlock.
// The issued-op lower bound is 5.375 cycles/pixel, not a device measurement.
GM_ANIM_IRAM __attribute__((noinline)) void ribbonFaceAsm(uint16_t *out, const uint16_t *pal,
                                                       const int16_t *dr, int16_t *work,
                                                       int acc, int inc, int x, int n) {
    if (n <= 0) return;
    if (((uintptr_t)out & 2u) != 0) {
        *out++ = pal[(acc >> 8) + dr[x & 7]];
        acc += inc;
        ++x;
        --n;
    }
    const int blocks = n >> 3;
    if (blocks != 0) {
        for (int i = 0; i < 8; i++) {
            work[i] = (int16_t)(acc + i * inc + dr[(x + i) & 7] * 256 - 32768);
        }
        const uint32_t step8 = (uint32_t)(inc * 8);
        const uint32_t packedStep = step8 | (step8 << 16);
        const uint32_t sign = 0x80008000u; // undo the -32768 bias in each halfword
        uint32_t hi, lo;
        asm volatile("ee.movi.32.q q1, %[step], 0\n"
                     "ee.movi.32.q q1, %[step], 1\n"
                     "ee.movi.32.q q1, %[step], 2\n"
                     "ee.movi.32.q q1, %[step], 3\n"
                     "ee.movi.32.q q2, %[sign], 0\n"
                     "ee.movi.32.q q2, %[sign], 1\n"
                     "ee.movi.32.q q2, %[sign], 2\n"
                     "ee.movi.32.q q2, %[sign], 3\n"
                     "ee.vld.128.ip q0, %[work], 0\n"
                     "loopnez %[n], 1f\n"
                     "ee.xorq q3, q0, q2\n"
                     "ee.vadds.s16 q0, q0, q1\n"
                     "ee.movi.32.a q3, %[hi], 0\n"
                     "extui %[lo], %[hi], 8, 8\n"
                     "extui %[hi], %[hi], 24, 8\n"
                     "addx2 %[lo], %[lo], %[pal]\n"
                     "addx2 %[hi], %[hi], %[pal]\n"
                     "l16ui %[hi], %[hi], 0\n"
                     "l16ui %[lo], %[lo], 0\n"
                     "slli %[hi], %[hi], 16\n"
                     "or %[hi], %[hi], %[lo]\n"
                     "s32i %[hi], %[out], 0\n"
                     "ee.movi.32.a q3, %[hi], 1\n"
                     "extui %[lo], %[hi], 8, 8\n"
                     "extui %[hi], %[hi], 24, 8\n"
                     "addx2 %[lo], %[lo], %[pal]\n"
                     "addx2 %[hi], %[hi], %[pal]\n"
                     "l16ui %[hi], %[hi], 0\n"
                     "l16ui %[lo], %[lo], 0\n"
                     "slli %[hi], %[hi], 16\n"
                     "or %[hi], %[hi], %[lo]\n"
                     "s32i %[hi], %[out], 4\n"
                     "ee.movi.32.a q3, %[hi], 2\n"
                     "extui %[lo], %[hi], 8, 8\n"
                     "extui %[hi], %[hi], 24, 8\n"
                     "addx2 %[lo], %[lo], %[pal]\n"
                     "addx2 %[hi], %[hi], %[pal]\n"
                     "l16ui %[hi], %[hi], 0\n"
                     "l16ui %[lo], %[lo], 0\n"
                     "slli %[hi], %[hi], 16\n"
                     "or %[hi], %[hi], %[lo]\n"
                     "s32i %[hi], %[out], 8\n"
                     "ee.movi.32.a q3, %[hi], 3\n"
                     "extui %[lo], %[hi], 8, 8\n"
                     "extui %[hi], %[hi], 24, 8\n"
                     "addx2 %[lo], %[lo], %[pal]\n"
                     "addx2 %[hi], %[hi], %[pal]\n"
                     "l16ui %[hi], %[hi], 0\n"
                     "l16ui %[lo], %[lo], 0\n"
                     "slli %[hi], %[hi], 16\n"
                     "or %[hi], %[hi], %[lo]\n"
                     "s32i %[hi], %[out], 12\n"
                     "addi %[out], %[out], 16\n"
                     "1:\n"
                     : [out] "+&r"(out), [hi] "=&r"(hi), [lo] "=&r"(lo)
                     : [work] "r"(work), [pal] "r"(pal), [step] "r"(packedStep),
                       [sign] "r"(sign), [n] "r"(blocks)
                     : "memory");
        const int done = blocks * 8;
        acc += done * inc;
        x += done;
    }
    for (int i = 0; i < (n & 7); i++, x++, acc += inc) {
        *out++ = pal[(acc >> 8) + dr[x & 7]];
    }
}

// Plain arguments keep the exact row splitting executable in the QEMU test
// too. The page's accumulator starts at the outer halo edge, so the face
// must resume acc + (face0-x0)*inc, never restart at lo*256. That distinction
// preserves the small drift from truncating inc, which is part of the look.
GM_ANIM_IRAM __attribute__((noinline)) void ribbonRowAsm(uint16_t *out, const uint16_t *pal,
                                                      const uint8_t *profile, const int16_t *dr,
                                                      int16_t *work, int w, int step, int hw,
                                                      int x0, int x1, int lo, int bg, int acc, int inc) {
    const int center = 240 * 256; // preview coordinate, same as CX_Q8
    const int left = center - hw;
    const int right = center + hw;
    int face0 = (left + step - 1) / step;
    int face1 = right / step + 1;
    if (face0 > w) face0 = w;
    if (face1 > w) face1 = w;
    const int color = pal[bg];
    ribbonFillAsm(out, color, x0);
    ribbonFillAsm(out + x1 + 1, color, w - x1 - 1);
    int x = x0;
    while (x < face0 && ((left - x * step) >> 4) >= 289) {
        out[x++] = (uint16_t)color; // page's out-of-profile case has no dither
    }
    ribbonHaloAsm(out + x, profile, pal, dr, x, left - x * step, -step, lo - bg, bg, face0 - x);
    ribbonFaceAsm(out + face0, pal, dr, work, acc + (face0 - x0) * inc, inc, face0, face1 - face0);
    ribbonHaloAsm(out + face1, profile, pal, dr, face1, face1 * step - right, step,
                  lo + 64 - bg, bg, x1 - face1 + 1);
}
// END VERBATIM PRODUCTION KERNELS

/* A literal per-pixel reimplementation of bandRef. It deliberately retains
 * the page's cases and clamps, independent of the assembly span split. */
static void rowRef(uint16_t *out, const uint16_t *pal, const uint8_t *gt,
                   const int16_t *dr, int w, int step, int hw, int x0, int x1,
                   int lo, int bg, int acc, int inc) {
    for (int x = 0; x < w; x++) {
        if (x < x0 || x > x1) { out[x] = pal[bg]; continue; }
        int dq = x * step - 240 * 256;
        int aq = dq < 0 ? -dq : dq;
        int idx;
        if (aq <= hw) idx = acc >> 8;
        else {
            int d = (aq - hw) >> 4;
            if (d >= 289) { out[x] = pal[bg]; acc += inc; continue; }
            int src = dq < 0 ? lo : lo + 64;
            idx = bg + (((src - bg) * gt[d]) >> 8);
        }
        idx += dr[x & 7];
        idx = idx < 0 ? 0 : idx > 255 ? 255 : idx;
        out[x] = pal[idx];
        acc += inc;
    }
}
static uint16_t got[544] __attribute__((aligned(16)));
static uint16_t want[544] __attribute__((aligned(16)));
static int16_t scratch[24] __attribute__((aligned(16)));
static uint16_t pal[256] __attribute__((aligned(16)));
static uint8_t gt[289] __attribute__((aligned(16)));
static int16_t dr[8] __attribute__((aligned(16)));
static unsigned cases, pixels, bad, firstCase, firstLane, firstGot, firstWant;
static uint32_t seed = 0x34b17e91u;
static uint32_t rng(void) {
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return seed;
}
static void dec_uart(unsigned v) {
    char s[12]; int n = 0;
    do { s[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) UART0_FIFO = (uint8_t)s[--n];
}
static void mismatch(unsigned lane, unsigned actual, unsigned expected) {
    if (bad++ == 0) { firstCase=cases; firstLane=lane; firstGot=actual; firstWant=expected; }
}
static void prepare(int n) {
    for (int i=0; i<n+24; i++) got[i]=want[i]=0xa55a;
    for (int i=0; i<24; i++) scratch[i]=(int16_t)0xc33c;
}
static void check(int n) {
    for (int i=0; i<n+24; i++) if (got[i] != want[i]) mismatch((unsigned)i,got[i],want[i]);
    for (int i=0; i<24; i++) {
        if ((i<8 || i>=16) && scratch[i]!=(int16_t)0xc33c)
            mismatch((unsigned)(544+i),(uint16_t)scratch[i],0xc33c);
    }
    ++cases; pixels += (unsigned)n;
}
static void faceCase(int acc, int inc, int x, int n, int off) {
    prepare(n);
    uint16_t *a=got+8+off, *b=want+8+off;
    ribbonFaceAsm(a,pal,dr,scratch+8,acc,inc,x,n);
    for (int i=0; i<n; i++) b[i]=pal[((acc+i*inc)>>8)+dr[(x+i)&7]];
    check(n);
}
static void testFill(void) {
    /* Every RGB565 value, all halfword alignments, zero/small spans. */
    for (unsigned c=0; c<65536; c++) {
        int n=(int)(c%34), off=(int)(c&7);
        prepare(n);
        ribbonFillAsm(got+8+off,(int)c,n);
        for (int i=0; i<n; i++) want[8+off+i]=(uint16_t)c;
        check(n);
    }
    for (int n=0; n<=480; n++) for (int off=0; off<8; off++) {
        prepare(n);
        ribbonFillAsm(got+8+off,0x6a93,n);
        for (int i=0; i<n; i++) want[8+off+i]=0x6a93;
        check(n);
    }
}
static void testFace(void) {
    for (int i=0; i<8; i++) dr[i]=0;
    /* Every Q8 fraction/index, including both sides of signed index 128.
     * This also reaches both int16 accumulator extremes with zero step. */
    for (int acc=0; acc<65536; acc++) faceCase(acc,0,acc&7,8,acc&7);
    /* All positive eight-lane step encodings up to 32760, broader than
     * production's <=16384 face sweep. At least two vectors where valid. */
    for (int inc=0; inc<4096; inc++) {
        int n=inc<2048 ? 16 : 8;
        int limit=65536-(n-1)*inc;
        int acc=(int)(rng()%(unsigned)limit);
        faceCase(acc,inc,inc&7,n,(inc>>3)&7);
    }
    /* Every tail length and output alignment, all Bayer row phases, and
     * the full -2..2 dither range, with production-size face sweeps. */
    for (int phase=0; phase<8; phase++) {
        for (int i=0; i<8; i++) dr[i]=(int16_t)((i*3+phase)%5-2);
        for (int n=0; n<=480; n++) for (int off=0; off<8; off++) {
            int inc=n ? 16384/n : 0;
            faceCase(30*256+(int)(rng()&255),inc,phase,n,off);
        }
    }
}
static void testHalo(void) {
    /* Full profile byte range, every distance bucket and sub-bucket phase,
     * both directions, all row backgrounds and all possible src-bg values.
     * Signed negative products deliberately exercise JS's floor shift. */
    for (int bg=14; bg<=32; bg++) for (int slope=-2; slope<=176; slope++) {
        for (int dir=-1; dir<=1; dir+=2) {
            int x=(int)(rng()&7), off=(int)(rng()&7), frac=(slope+bg)&15;
            int distance=(dir<0 ? 288*16 : 0)+frac;
            int step=dir*16;
            for (int i=0; i<8; i++) dr[i]=(int16_t)((i+bg)%5-2);
            for (int i=0; i<289; i++) gt[i]=(uint8_t)(i*197+slope+bg);
            prepare(289);
            ribbonHaloAsm(got+8+off,gt,pal,dr,x,distance,step,slope,bg,289);
            for (int i=0; i<289; i++) {
                int d=(distance+i*step)>>4;
                want[8+off+i]=pal[bg+((slope*gt[d])>>8)+dr[(x+i)&7]];
            }
            check(289);
        }
    }
    prepare(0);
    ribbonHaloAsm(got+8,gt,pal,dr,0,0,256,-2,14,0);
    check(0);
}
static void rowCase(int w, int hw, int lo, int bg, int off) {
    const int step=480*256/w;
    int x0=(240*256-hw-18*256)/step;
    int x1=(240*256+hw+18*256)/step;
    if (x0<0) x0=0;
    if (x1>=w) x1=w-1;
    const int span=2*hw;
    const int inc=64*256*step/span;
    /* The page's total is positive at these widths. Split a signed
     * quotient and remainder to avoid needing a freestanding 64-bit divide. */
    const int delta=x0*step-240*256+hw;
    int num=delta*64*256;
    int acc=lo*256+num/span;
    if (num<0 && num%span) --acc;
    prepare(w);
    ribbonRowAsm(got+8+off,pal,gt,dr,scratch+8,w,step,hw,x0,x1,lo,bg,acc,inc);
    rowRef(want+8+off,pal,gt,dr,w,step,hw,x0,x1,lo,bg,acc,inc);
    check(w);
}
static void testRows(void) {
    const int widths[4]={480,240,466,233};
    /* All 16 sub-bucket fractions across the hw lattice at the waist,
     * the maximum width, clipped halo samples, narrow and broad faces.
     * lo/bg are deliberately decorrelated from width for wider coverage. */
    for (int hw=6656; hw<=44786; hw+=37) for (int wi=0; wi<4; wi++) {
        for (int i=0; i<8; i++) dr[i]=(int16_t)((i*3+hw)%5-2);
        for (int i=0; i<289; i++) gt[i]=(uint8_t)(rng()>>24);
        rowCase(widths[wi],hw,30+(int)(rng()%97),14+(int)(rng()%19),(hw+wi)&7);
    }
    /* Width/brightness 0 and 100 reach the same bounded kernel operands:
     * all sine phases can reach hw=6656, width 100 reaches hw=44785
     * (the sweep includes one extra Q8 unit of headroom),
     * cos reaches lo=30/126, bg=14/32. Palette words span all 16 bits. */
    for (int hi=0; hi<2; hi++) for (int lo=30; lo<=126; lo+=96)
        for (int bg=14; bg<=32; bg+=18) for (int wi=0; wi<4; wi++)
            for (int off=0; off<8; off++) rowCase(widths[wi],hi?44786:6656,lo,bg,off);
}
int main(void) {
    /* Only the bare-metal harness enables CP3. Production never does. */
    uint32_t cp=8;
    asm volatile("wsr %0, cpenable\nrsync\n" :: "r"(cp) : "memory");
    if (probeMovesAndXor()) mismatch(0,1,0);
    for (int i=0; i<256; i++) pal[i]=(uint16_t)((i*257)^0x5aa5);
    testFill(); testFace(); testHalo(); testRows();
    if (bad) {
        puts_uart("GM_QEMUBENCH_PIE: FAIL ribbon case=");dec_uart(firstCase);
        puts_uart(" lane=");dec_uart(firstLane);
        puts_uart(" got=");dec_uart(firstGot);puts_uart(" want=");dec_uart(firstWant);
        puts_uart(" mismatches=");dec_uart(bad);puts_uart("\n");
    } else {
        puts_uart("GM_QEMUBENCH_PIE: PASS ribbon cases=");dec_uart(cases);
        puts_uart(" pixels=");dec_uart(pixels);
        puts_uart(" mismatches=0 (fill, face, halo, row, guards, MOVI/XOR probes)\n");
    }
    puts_uart("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {}
}
