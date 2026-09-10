/* Harness mode, freestanding, no libc. The three functions between the
 * VERBATIM markers are copied from AnimDimples.cpp without a change, which is
 * why this driver is C++ and not C: the production copies use C++ casts.
 * A probe for EE.VLD/VST.128.IP runs first, then the kernels run against an
 * independent per pixel reference over the whole operand range the animation
 * can reach: texel indices 32..150, Bayer offsets -2..2, every dither phase,
 * every destination alignment the band contract allows and the panel widths
 * 480, 466, 240 and 233 plus the short widths a partial cell can produce. */
#include <stdint.h>
#define GM_ANIM_IRAM
#define TS 64
#define TPX 2
#define TILE_PX (TS << TPX)
#define UART0_FIFO (*(volatile uint32_t *)0x60000000u)

/* GCC turns one of the driver's copy loops into a memcpy call even at -O1
 * with -fno-builtin, and this link has no libc, so the test supplies one.
 * The attribute keeps the pass from rewriting the body into a call to
 * itself. */
extern "C" __attribute__((optimize("no-tree-loop-distribute-patterns"))) void *
memcpy(void *d, const void *s, __SIZE_TYPE__ n) {
    uint8_t *dd = (uint8_t *)d;
    const uint8_t *ss = (const uint8_t *)s;
    for (__SIZE_TYPE__ i = 0; i < n; i++) dd[i] = ss[i];
    return d;
}

static void puts_uart(const char *s) {
    while (*s) {
        if (*s == '\n') UART0_FIFO = '\r';
        UART0_FIFO = (uint8_t)*s++;
    }
}
static void dec_uart(unsigned v) {
    char s[12];
    int n = 0;
    do {
        s[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n) UART0_FIFO = (uint8_t)s[--n];
}

/* VLD/VST move all 16 bytes and post increment by the immediate. The kernel
 * below is the only PIE this animation uses, so this is the only probe it
 * needs. An unimplemented instruction shows up here as garbage, never as a
 * quiet pass. */
static uint16_t probeSrc[16] __attribute__((aligned(16)));
static uint16_t probeDst[16] __attribute__((aligned(16)));
static int probeVectorMove(void) {
    for (int i = 0; i < 16; i++) {
        probeSrc[i] = (uint16_t)(0x1234u + i * 0x1111u);
        probeDst[i] = 0;
    }
    const uint16_t *s = probeSrc;
    uint16_t *d = probeDst;
    asm volatile("ee.vld.128.ip q0, %[s], 16\n"
                 "ee.vst.128.ip q0, %[d], 16\n"
                 "ee.vld.128.ip q0, %[s], 16\n"
                 "ee.vst.128.ip q0, %[d], 16\n"
                 : [s] "+&r"(s), [d] "+&r"(d)
                 :
                 : "memory");
    if (s != probeSrc + 16 || d != probeDst + 16) return 1;
    for (int i = 0; i < 16; i++) {
        if (probeDst[i] != probeSrc[i]) return 1;
    }
    return 0;
}

// BEGIN VERBATIM PRODUCTION KERNELS
// One cell per iteration: 17 instructions for four pixels. Every load has two
// independent instructions between it and its consumer, so no load-use
// interlock is left, and every address is computed at least three
// instructions ahead of the load that uses it. b0..b3 are byte addresses of
// pal biased by the cell's four dither offsets, so a texel value indexes them
// directly. out advances eight pixels because the next cell of this parity is
// eight pixels along, and tp advances two texels for the same reason.
// out must be 4-byte aligned, which is BgAnim.h's precondition on dst.
GM_ANIM_IRAM __attribute__((noinline)) void dimplesCellsAsm(uint16_t *out, const uint8_t *tp,
                                                            int b0, int b1, int b2, int b3, int cells) {
    if (cells <= 0) return;
    int t, v0, v1, v2, v3;
    asm volatile("loopnez %[cells], 1f\n"
                 "l8ui   %[t], %[tp], 0\n"
                 "addi   %[tp], %[tp], 2\n"
                 "addx2  %[v0], %[t], %[b0]\n"
                 "addx2  %[v1], %[t], %[b1]\n"
                 "addx2  %[v2], %[t], %[b2]\n"
                 "addx2  %[v3], %[t], %[b3]\n"
                 "l16ui  %[v0], %[v0], 0\n"
                 "l16ui  %[v1], %[v1], 0\n"
                 "l16ui  %[v2], %[v2], 0\n"
                 "l16ui  %[v3], %[v3], 0\n"
                 "slli   %[v1], %[v1], 16\n"
                 "or     %[v0], %[v0], %[v1]\n"
                 "slli   %[v3], %[v3], 16\n"
                 "s32i   %[v0], %[out], 0\n"
                 "or     %[v2], %[v2], %[v3]\n"
                 "s32i   %[v2], %[out], 4\n"
                 "addi   %[out], %[out], 16\n"
                 "1:\n"
                 : [out] "+&r"(out), [tp] "+&r"(tp), [t] "=&r"(t), [v0] "=&r"(v0), [v1] "=&r"(v1),
                   [v2] "=&r"(v2), [v3] "=&r"(v3)
                 : [b0] "r"(b0), [b1] "r"(b1), [b2] "r"(b2), [b3] "r"(b3), [cells] "r"(cells)
                 : "memory");
}

// The periodic extension of a row: dst is src plus one whole tile of pixels.
// A scalar prefix takes dst to a 16-byte boundary, and src moves with it, so
// both are aligned once the vector loop starts. VLD and VST mask the low four
// address bits instead of trapping, so the prefix is what keeps them off a
// neighbouring row. The 512-byte gap between src and dst is wider than the
// 16-byte block, so a forward copy never reads a byte this call has just
// written, which is what makes one call correct even past two tiles.
GM_ANIM_IRAM __attribute__((noinline)) void dimplesCopyAsm(uint16_t *dst, const uint16_t *src, int n) {
    if (n <= 0) return;
    if (((reinterpret_cast<uintptr_t>(dst) ^ reinterpret_cast<uintptr_t>(src)) & 15u) != 0) {
        for (int i = 0; i < n; i++) dst[i] = src[i];
        return;
    }
    while (n > 0 && (reinterpret_cast<uintptr_t>(dst) & 15u) != 0) {
        *dst++ = *src++;
        --n;
    }
    const int blocks = n >> 3;
    if (blocks != 0) {
        asm volatile("loopnez %[n], 1f\n"
                     "ee.vld.128.ip q0, %[src], 16\n"
                     "ee.vst.128.ip q0, %[dst], 16\n"
                     "1:\n"
                     : [dst] "+&r"(dst), [src] "+&r"(src)
                     : [n] "r"(blocks)
                     : "memory");
    }
    for (int i = 0; i < (n & 7); i++) dst[i] = src[i];
}

// Plain arguments so the whole row split runs unchanged in the QEMU test.
// The tail loop covers a width that is not a whole number of cells, which is
// the 233 px half-resolution row of the 466 px panel.
GM_ANIM_IRAM __attribute__((noinline)) void dimplesRowAsm(uint16_t *out, const uint8_t *trow,
                                                          const uint16_t *pal, const int16_t *dr, int w) {
    const int n0 = w < TILE_PX ? w : TILE_PX;
    const int cells = n0 >> TPX;
    int base[8];
    // Byte addresses, not uint16_t pointers: a negative offset would put a
    // pointer before the palette, and the kernel only ever adds a texel value
    // of 32 or more to it.
    for (int j = 0; j < 8; j++) {
        base[j] = static_cast<int>(reinterpret_cast<uintptr_t>(pal)) + 2 * dr[j];
    }
    dimplesCellsAsm(out, trow, base[0], base[1], base[2], base[3], (cells + 1) >> 1);
    if (cells > 1) {
        dimplesCellsAsm(out + 4, trow + 1, base[4], base[5], base[6], base[7], cells >> 1);
    }
    for (int x = cells << TPX; x < n0; x++) {
        out[x] = pal[trow[(x >> TPX) & (TS - 1)] + dr[x & 7]];
    }
    if (w > TILE_PX) dimplesCopyAsm(out + TILE_PX, out, w - TILE_PX);
}
// END VERBATIM PRODUCTION KERNELS

/* A literal per pixel reimplementation of bandRef, clamp included, with no
 * knowledge of the kernel's cells, parities or period. */
static void rowRef(uint16_t *out, const uint8_t *trow, const uint16_t *pal, const int16_t *dr, int w) {
    for (int x = 0; x < w; x++) {
        int i = trow[(x >> TPX) & (TS - 1)] + dr[x & 7];
        i = i < 0 ? 0 : i > 255 ? 255 : i;
        out[x] = (uint16_t)pal[i];
    }
}

#define GUARD 16
static uint16_t got[512 + 2 * GUARD] __attribute__((aligned(16)));
static uint16_t want[512 + 2 * GUARD] __attribute__((aligned(16)));
static uint16_t pal[256] __attribute__((aligned(16)));
static uint8_t trow[TS] __attribute__((aligned(16)));
static int16_t dr[8] __attribute__((aligned(16)));
static uint16_t csrc[512 + 2 * GUARD] __attribute__((aligned(16)));
static uint16_t cdst[512 + 2 * GUARD] __attribute__((aligned(16)));
static unsigned cases, pixels, bad, firstCase, firstLane, firstGot, firstWant;
static uint32_t seed = 0x51ab37cdu;
static uint32_t rng(void) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}
static void mismatch(unsigned lane, unsigned actual, unsigned expected) {
    if (bad++ == 0) {
        firstCase = cases;
        firstLane = lane;
        firstGot = actual;
        firstWant = expected;
    }
}
static void prepare(int n) {
    for (int i = 0; i < n + 2 * GUARD; i++) got[i] = want[i] = 0xa55a;
}
static void check(int n) {
    for (int i = 0; i < n + 2 * GUARD; i++) {
        if (got[i] != want[i]) mismatch((unsigned)i, got[i], want[i]);
    }
    ++cases;
    pixels += (unsigned)n;
}

/* One whole row through the kernel split against the reference. off is in
 * halfwords and stays even, because band()'s destination is 4-byte aligned
 * and a row of even width keeps every later row so. */
static void rowCase(int w, int off) {
    prepare(w + off);
    dimplesRowAsm(got + GUARD + off, trow, pal, dr, w);
    rowRef(want + GUARD + off, trow, pal, dr, w);
    check(w + off);
}

static void fillRow(int k, int s) {
    for (int i = 0; i < TS; i++) trow[i] = (uint8_t)(32 + ((i * k + s) % 119));
}
static void fillDither(int phase) {
    for (int i = 0; i < 8; i++) dr[i] = (int16_t)((i * 3 + phase) % 5 - 2);
}

static void testRows(void) {
    const int widths[10] = {480, 466, 240, 233, 256, 255, 257, 8, 5, 1};
    /* Every dither phase pattern against every width and every legal
     * destination alignment, with the texel row stepping so that all 119
     * reachable indices land in both cell parities. */
    for (int phase = 0; phase < 8; phase++) {
        fillDither(phase);
        for (int wi = 0; wi < 10; wi++) {
            for (int off = 0; off < 8; off += 2) {
                fillRow(1 + phase, wi * 7 + off);
                rowCase(widths[wi], off);
            }
        }
    }
    /* The index extremes on their own: the whole row at 32, the whole row at
     * 150, and the two alternating, with the dither at both ends of -2..2. */
    for (int lo = 0; lo < 2; lo++) {
        for (int i = 0; i < 8; i++) dr[i] = (int16_t)(lo ? -2 : 2);
        for (int mode = 0; mode < 3; mode++) {
            for (int i = 0; i < TS; i++) {
                trow[i] = (uint8_t)(mode == 0 ? 32 : mode == 1 ? 150 : ((i & 1) ? 150 : 32));
            }
            for (int wi = 0; wi < 10; wi++) rowCase(widths[wi], (wi & 3) * 2);
        }
    }
    /* Random rows, random offsets inside the same ranges. */
    for (int r = 0; r < 400; r++) {
        for (int i = 0; i < TS; i++) trow[i] = (uint8_t)(32 + (rng() % 119));
        for (int i = 0; i < 8; i++) dr[i] = (int16_t)((int)(rng() % 5) - 2);
        rowCase(widths[rng() % 10], (int)(rng() % 4) * 2);
    }
    /* Every width from 0 to 300 once, so every partial cell and every count
     * of copied pixels is covered. */
    fillDither(3);
    for (int w = 0; w <= 300; w++) {
        fillRow(5, w);
        rowCase(w, (w & 3) * 2);
    }
}

/* The copy on its own. skew 0 puts source and destination on the same
 * alignment, which is what the row driver's 512-byte gap gives and what
 * reaches the vector loop; skew 1 is the unequal alignment the kernel's own
 * guard sends to the scalar loop instead of to VLD, which masks address bits
 * rather than trapping. Every case starts at a fresh fill so a byte written
 * outside the range shows up. */
static void copyCase(int n, int off, int skew) {
    for (int i = 0; i < 512 + 2 * GUARD; i++) {
        csrc[i] = (uint16_t)(0x3c00u + i);
        cdst[i] = 0xa55a;
    }
    uint16_t *d = cdst + GUARD + off;
    const uint16_t *s = csrc + GUARD + off + skew;
    dimplesCopyAsm(d, s, n);
    for (int i = 0; i < 512 + 2 * GUARD; i++) {
        const int rel = i - (GUARD + off);
        const uint16_t expect = (rel >= 0 && rel < n) ? csrc[GUARD + off + skew + rel] : (uint16_t)0xa55a;
        if (cdst[i] != expect) mismatch((unsigned)i, cdst[i], expect);
    }
    ++cases;
    pixels += (unsigned)(n < 0 ? 0 : n);
}

/* The periodic extension in place, which is how the row driver calls it: one
 * buffer, the destination one whole tile of pixels past the source. */
static void copyPeriodCase(int n, int off) {
    for (int i = 0; i < 512 + 2 * GUARD; i++) cdst[i] = (uint16_t)(0x2100u + i * 7);
    uint16_t *d = cdst + GUARD + off + TILE_PX;
    const uint16_t *s = cdst + GUARD + off;
    for (int i = 0; i < n; i++) csrc[i] = s[i];
    dimplesCopyAsm(d, s, n);
    for (int i = 0; i < n; i++) {
        if (d[i] != csrc[i]) mismatch((unsigned)i, d[i], csrc[i]);
    }
    ++cases;
    pixels += (unsigned)n;
}

static void testCopy(void) {
    for (int off = 0; off < 8; off++) {
        for (int n = 0; n <= 260; n++) copyCase(n, off, 0);
    }
    for (int off = 0; off < 8; off++) {
        for (int n = 0; n <= 40; n++) copyCase(n, off, 1);
    }
    for (int off = 0; off < 8; off++) {
        for (int n = 0; n <= 224; n++) copyPeriodCase(n, off);
    }
}

/* The cell pass on its own, so a fault inside it cannot hide behind the row
 * driver's tail or copy. */
static void cellsCase(int cellsA, int off) {
    prepare(8 * cellsA + off + 8);
    int base[8];
    for (int j = 0; j < 8; j++) base[j] = (int)(uintptr_t)pal + 2 * dr[j];
    dimplesCellsAsm(got + GUARD + off, trow, base[0], base[1], base[2], base[3], cellsA);
    for (int c = 0; c < cellsA; c++) {
        for (int j = 0; j < 4; j++) {
            want[GUARD + off + 8 * c + j] = pal[trow[2 * c] + dr[j]];
        }
    }
    check(8 * cellsA + off + 8);
}

static void testCells(void) {
    for (int phase = 0; phase < 8; phase++) {
        fillDither(phase);
        fillRow(3, phase);
        for (int c = 0; c <= 32; c++) {
            for (int off = 0; off < 8; off += 2) cellsCase(c, off);
        }
    }
}

int main(void) {
    /* Only the bare-metal harness enables CP3. Production never does. */
    uint32_t cp = 8;
    asm volatile("wsr %0, cpenable\nrsync\n" ::"r"(cp) : "memory");
    if (probeVectorMove()) mismatch(0, 1, 0);
    for (int i = 0; i < 256; i++) pal[i] = (uint16_t)((i * 2571u) ^ 0x5aa5u);
    testCells();
    testCopy();
    testRows();
    if (bad) {
        puts_uart("GM_QEMUBENCH_PIE: FAIL dimples case=");
        dec_uart(firstCase);
        puts_uart(" lane=");
        dec_uart(firstLane);
        puts_uart(" got=");
        dec_uart(firstGot);
        puts_uart(" want=");
        dec_uart(firstWant);
        puts_uart(" mismatches=");
        dec_uart(bad);
        puts_uart("\n");
    } else {
        puts_uart("GM_QEMUBENCH_PIE: PASS dimples cases=");
        dec_uart(cases);
        puts_uart(" pixels=");
        dec_uart(pixels);
        puts_uart(" mismatches=0 (cells, copy, rows, guards, VLD/VST probe)\n");
    }
    puts_uart("GM_QEMUBENCH_PIE_DONE\n");
    for (;;) {
    }
}
