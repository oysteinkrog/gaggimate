/* Cube kernel execution test, freestanding harness mode. The two noinline
 * kernels and their prefix/tail helper below are verbatim from AnimCube.cpp.
 * The independent C oracle uses closed-form planes for each pixel. It tests
 * every glow value 0..100, all legal alignment residues, zero/short/odd/full
 * spans, Q16 boundaries and large positive distances, signed step extrema,
 * all 256 palette indices, and guard words around every destination.
 * CPENABLE is set only here in the bare-metal main, never by a kernel.
 */
#include <stdint.h>
#define GM_ANIM_IRAM
#define UART0 (*(volatile uint32_t *)0x60000000u)
static void uart_puts(const char *s) { while (*s) UART0 = (uint8_t)*s++; }
static void uart_dec(uint32_t v) {
    char b[12]; int n=0;
    do { b[n++]=(char)('0'+v%10); v/=10; } while(v);
    while(n) UART0=(uint8_t)b[--n];
}
static inline void cubeFacePixelsRef(int32_t *out, int n, int32_t d[4], const int32_t *step, int amplitude, int limit) {
    int32_t d0 = d[0], d1 = d[1], d2 = d[2], d3 = d[3];
    const int32_t s0 = step[0], s1 = step[1], s2 = step[2], s3 = step[3];
    for (int x = 0; x < n; ++x) {
        int32_t m = d0 < d1 ? d0 : d1;
        if (d2 < m) m = d2;
        if (d3 < m) m = d3;
        int c = m >> 16;
        if (c > amplitude) c = amplitude;
        const int a = out[x] + c;
        out[x] = a > limit ? limit : a;
        d0 += s0;
        d1 += s1;
        d2 += s2;
        d3 += s3;
    }
    d[0] = d0;
    d[1] = d1;
    d[2] = d2;
    d[3] = d3;
}

GM_ANIM_IRAM __attribute__((noinline)) void cubeFaceSpanAsm(int32_t *out, int n, const int32_t *starts,
                                                          const int32_t *step, const int32_t *vectors) {
    int32_t d[4] = {starts[0], starts[1], starts[2], starts[3]};
    const int amplitude = vectors[16], limit = vectors[20];
    int prefix = (int)((16u - ((uintptr_t)out & 15u)) & 15u) / 4;
    if (prefix > n) prefix = n;
    cubeFacePixelsRef(out, prefix, d, step, amplitude, limit);
    out += prefix;
    n -= prefix;
    const int groups = n / 4;
    if (groups != 0) {
        int32_t lanes[16] __attribute__((aligned(16)));
        for (int e = 0; e < 4; ++e) {
            for (int j = 0; j < 4; ++j) lanes[e * 4 + j] = d[e] + step[e] * j;
        }
        const int32_t *lp = lanes;
        const int32_t *vp = vectors + 16;
        int32_t *op = out;
        // 18 instructions / four pixels = 4.5 per face pixel. Every load
        // has an independent instruction before consumption, across the
        // loop boundary too. The four broadcasts are reread sequentially
        // from the hot slab, freeing q4/q7 for the reduction and accumulator.
        asm volatile("ee.vld.128.ip q0, %[lp], 16\n"
                     "ee.vld.128.ip q1, %[lp], 16\n"
                     "ee.vld.128.ip q2, %[lp], 16\n"
                     "ee.vld.128.ip q3, %[lp], 16\n"
                     "ee.vld.128.ip q5, %[vp], 16\n"
                     "ee.vld.128.ip q6, %[vp], -16\n"
                     "ssai 16\n"
                     "loopnez %[n], 1f\n"
                     "ee.vmin.s32 q4, q0, q1\n"
                     "ee.vmin.s32 q4, q4, q2\n"
                     "ee.vmin.s32 q4, q4, q3\n"
                     "ee.vsr.32 q4, q4\n"
                     "ee.vmin.s32 q4, q4, q5\n"
                     "ee.vld.128.ip q7, %[out], 0\n"
                     "addi %[vp], %[vp], -64\n"
                     "ee.vadds.s32 q4, q4, q7\n"
                     "ee.vmin.s32 q4, q4, q6\n"
                     "ee.vst.128.ip q4, %[out], 16\n"
                     "ee.vld.128.ip q7, %[vp], 16\n"
                     "ee.vld.128.ip q4, %[vp], 16\n"
                     "ee.vadds.s32 q0, q0, q7\n"
                     "ee.vadds.s32 q1, q1, q4\n"
                     "ee.vld.128.ip q7, %[vp], 16\n"
                     "ee.vld.128.ip q4, %[vp], 16\n"
                     "ee.vadds.s32 q2, q2, q7\n"
                     "ee.vadds.s32 q3, q3, q4\n"
                     "1:\n"
                     : [out] "+&r"(op), [lp] "+&r"(lp), [vp] "+&r"(vp)
                     : [n] "r"(groups)
                     : "memory");
        out = op;
        const int advanced = groups * 4;
        for (int e = 0; e < 4; ++e) d[e] += step[e] * advanced;
        n -= advanced;
    }
    cubeFacePixelsRef(out, n, d, step, amplitude, limit);
}

// Eight palette indices are vector sums of the accumulator and the row's
// background plus Bayer offset. MOVI.32.A takes each index straight from
// its q register; ADDX2/L16UI gathers remain scalar because PIE has no
// vector gather. Four independent gathers hide all load-use gaps and
// S32I writes adjacent RGB565 pairs. out needs only the contract's 4-byte
// alignment; acc and the local base table are 16-byte aligned. Odd widths
// and all remainders use the same scalar formula after the last octet.
GM_ANIM_IRAM __attribute__((noinline)) void cubePaletteRowAsm(uint16_t *out, const int32_t *acc,
                                                            const int32_t *dith, int background,
                                                            const uint16_t *pal, int n) {
    int32_t base[8] __attribute__((aligned(16)));
    for (int i = 0; i < 8; ++i) base[i] = background + dith[i];
    const int groups = n / 8;
    if (groups != 0) {
        const int32_t *bp = base;
        int32_t t0, t1, t2, t3;
        // 41 instructions / eight pixels = 5.125 per output pixel, with
        // no immediate load-use dependency and no data-dependent branch.
        // The LOOPNEZ body is below the hardware's 256-byte limit.
        asm volatile("ee.vld.128.ip q2, %[bp], 16\n"
                     "ee.vld.128.ip q3, %[bp], 0\n"
                     "loopnez %[n], 1f\n"
                     "ee.vld.128.ip q0, %[acc], 16\n"
                     "ee.vld.128.ip q1, %[acc], 16\n"
                     "ee.vadds.s32 q0, q0, q2\n"
                     "ee.vadds.s32 q1, q1, q3\n"
                     "ee.movi.32.a q0, %[t0], 0\n"
                     "ee.movi.32.a q0, %[t1], 1\n"
                     "ee.movi.32.a q0, %[t2], 2\n"
                     "ee.movi.32.a q0, %[t3], 3\n"
                     "addx2 %[t0], %[t0], %[pal]\n"
                     "addx2 %[t1], %[t1], %[pal]\n"
                     "addx2 %[t2], %[t2], %[pal]\n"
                     "addx2 %[t3], %[t3], %[pal]\n"
                     "l16ui %[t0], %[t0], 0\n"
                     "l16ui %[t1], %[t1], 0\n"
                     "l16ui %[t2], %[t2], 0\n"
                     "l16ui %[t3], %[t3], 0\n"
                     "slli %[t1], %[t1], 16\n"
                     "slli %[t3], %[t3], 16\n"
                     "or %[t0], %[t0], %[t1]\n"
                     "or %[t2], %[t2], %[t3]\n"
                     "s32i %[t0], %[out], 0\n"
                     "s32i %[t2], %[out], 4\n"
                     "ee.movi.32.a q1, %[t0], 0\n"
                     "ee.movi.32.a q1, %[t1], 1\n"
                     "ee.movi.32.a q1, %[t2], 2\n"
                     "ee.movi.32.a q1, %[t3], 3\n"
                     "addx2 %[t0], %[t0], %[pal]\n"
                     "addx2 %[t1], %[t1], %[pal]\n"
                     "addx2 %[t2], %[t2], %[pal]\n"
                     "addx2 %[t3], %[t3], %[pal]\n"
                     "l16ui %[t0], %[t0], 0\n"
                     "l16ui %[t1], %[t1], 0\n"
                     "l16ui %[t2], %[t2], 0\n"
                     "l16ui %[t3], %[t3], 0\n"
                     "slli %[t1], %[t1], 16\n"
                     "slli %[t3], %[t3], 16\n"
                     "or %[t0], %[t0], %[t1]\n"
                     "or %[t2], %[t2], %[t3]\n"
                     "s32i %[t0], %[out], 8\n"
                     "s32i %[t2], %[out], 12\n"
                     "addi %[out], %[out], 16\n"
                     "1:\n"
                     : [out] "+&r"(out), [acc] "+&r"(acc), [bp] "+&r"(bp),
                       [t0] "=&r"(t0), [t1] "=&r"(t1), [t2] "=&r"(t2), [t3] "=&r"(t3)
                     : [n] "r"(groups), [pal] "r"(pal)
                     : "memory");
    }
    for (int i = 0; i < n % 8; ++i) out[i] = pal[acc[i] + base[i]];
}

#define STORAGE 512
static int32_t got[STORAGE] __attribute__((aligned(16)));
static int32_t want[STORAGE] __attribute__((aligned(16)));
static int32_t acc[STORAGE] __attribute__((aligned(16)));
static uint16_t rgb[STORAGE] __attribute__((aligned(16)));
static uint16_t rgbRef[STORAGE] __attribute__((aligned(16)));
static uint16_t pal[256] __attribute__((aligned(16)));
static int32_t vectors[24] __attribute__((aligned(16)));
static int32_t dith[8] __attribute__((aligned(16)));
static uint32_t seed=0x43c0beef;
static uint32_t rnd(void) { seed^=seed<<13; seed^=seed>>17; seed^=seed<<5; return seed; }
static int calls, mismatches, firstCall=-1, firstLane=-1;
static uint32_t firstGot, firstWant, facePixels, palettePixels;
static void mismatch(int lane, uint32_t a, uint32_t b) {
    if(a==b) return;
    if(!mismatches) { firstCall=calls; firstLane=lane; firstGot=a; firstWant=b; }
    ++mismatches;
}

static void faceOracle(int32_t *dst, int n, const int32_t *start, const int32_t *step, int amp, int limit) {
    for(int x=0;x<n;x++) {
        int m=INT32_MAX;
        for(int e=0;e<4;e++) {
            const int value=start[e]+step[e]*x;
            if(value<m) m=value;
        }
        int c=m/65536;
        if(c>amp) c=amp;
        int a=dst[x]+c;
        dst[x]=a>limit?limit:a;
    }
}

static void checkFace(int n, int offset, int amp, int limit, const int32_t *start, const int32_t *step) {
    ++calls;
    for(int i=0;i<STORAGE;i++) got[i]=want[i]=0x12345678;
    for(int i=0;i<n;i++) got[offset+i]=want[offset+i]=(int)(rnd()%(limit+1));
    for(int e=0;e<4;e++) {
        for(int j=0;j<4;j++) vectors[e*4+j]=step[e]*4;
        vectors[16+e]=amp; vectors[20+e]=limit;
    }
    cubeFaceSpanAsm(got+offset,n,start,step,vectors);
    faceOracle(want+offset,n,start,step,amp,limit);
    for(int i=0;i<STORAGE;i++) mismatch(i,(uint32_t)got[i],(uint32_t)want[i]);
    facePixels+=(uint32_t)n;
}

static void checkPalette(int n, int offset, int background) {
    ++calls;
    for(int i=0;i<STORAGE;i++) rgb[i]=rgbRef[i]=0xdead;
    for(int i=0;i<n;i++) rgbRef[offset+i]=pal[acc[i]+background+dith[i&7]];
    cubePaletteRowAsm(rgb+offset,acc,dith,background,pal,n);
    for(int i=0;i<STORAGE;i++) mismatch(i,rgb[i],rgbRef[i]);
    palettePixels+=(uint32_t)n;
}

int main(void) {
    uint32_t cp=8;
    __asm__ volatile("wsr %0, cpenable\nrsync" : : "r"(cp) : "memory");
    for(int i=0;i<256;i++) pal[i]=(uint16_t)((i*257)^0xa55a);
    static const int32_t slopes[]={-216773,-65536,-1,0,1,65536,216773};
    static const int widths[]={0,1,2,3,4,5,7,8,9,15,16,17,31,32,33,233,240,466,480};
    /* All glow settings and all widths/alignment residues. Slopes span the
     * maximum round(86/26*65536) and its negative. Starts stay nonnegative
     * through the last vector advance, including negative-slope cases.
     */
    for(int glow=0;glow<=100;glow++) {
        const int ampMax=52+(glow*34+50)/100;
        const int limit=(ampMax*145+50)/100;
        for(int wi=0;wi<(int)(sizeof(widths)/sizeof(widths[0]));wi++) {
            const int n=widths[wi];
            for(int residue=0;residue<4;residue++) {
                int32_t start[4],step[4];
                for(int e=0;e<4;e++) {
                    step[e]=slopes[(glow+wi+residue+e)%7];
                    start[e]=(int32_t)(rnd()%65537);
                    if(step[e]<0) start[e]-=step[e]*(n+4);
                }
                checkFace(n,4+residue,(glow+wi+residue)%(ampMax+1),limit,start,step);
                for(int i=0;i<n;i++) acc[i]=(int32_t)(rnd()%(limit+1));
                for(int i=0;i<8;i++) dith[i]=(i+glow+residue)%5-2;
                checkPalette(n,8+residue*2,56+glow%19);
            }
        }
    }
    /* Exact floor transitions, constant interiors, cap crossings and the
     * entire nonnegative int32 distance range. Actual face planes are
     * smaller; high values prove VSR.32 and the amplitude min cannot wrap.
     */
    static const int32_t boundaries[]={0,1,65535,65536,65537,86*65536-1,86*65536,
                                86*65536+1,0x3fffffff,INT32_MAX};
    for(int amp=0;amp<=86;amp++) {
        for(int b=0;b<(int)(sizeof(boundaries)/sizeof(boundaries[0]));b++) {
            int32_t start[4]={INT32_MAX,INT32_MAX,INT32_MAX,INT32_MAX};
            int32_t step[4]={0,0,0,0};
            start[b&3]=boundaries[b];
            checkFace(17,4+(b&3),amp,125,start,step);
        }
    }
    /* Full 256-entry gather coverage, then random long affine spans with
     * the limiting plane changing among all four edges inside a group.
     */
    for(int i=0;i<480;i++) acc[i]=i&255;
    for(int i=0;i<8;i++) dith[i]=0;
    for(int r=0;r<4;r++) checkPalette(480,8+r*2,0);
    for(int trial=0;trial<2048;trial++) {
        const int n=(int)(rnd()%481), amp=(int)(rnd()%87);
        int32_t start[4],step[4];
        for(int e=0;e<4;e++) {
            step[e]=(int32_t)(rnd()%433547)-216773;
            start[e]=(int32_t)(rnd()%8000000);
            if(step[e]<0) start[e]-=step[e]*(n+4);
        }
        checkFace(n,4+(trial&3),amp,75+trial%51,start,step);
    }
    uart_puts(mismatches?"GM_QEMUBENCH_PIE: FAIL cube":"GM_QEMUBENCH_PIE: PASS cube");
    uart_puts(" calls="); uart_dec(calls);
    uart_puts(" face_pixels="); uart_dec(facePixels);
    uart_puts(" palette_pixels="); uart_dec(palettePixels);
    uart_puts(" mismatches="); uart_dec(mismatches);
    if(mismatches) {
        uart_puts(" first_call=");uart_dec(firstCall);
        uart_puts(" lane=");uart_dec(firstLane);
        uart_puts(" got=");uart_dec(firstGot);
        uart_puts(" want=");uart_dec(firstWant);
    }
    uart_puts("\nGM_QEMUBENCH_PIE_DONE\n");
    for(;;) {}
}
