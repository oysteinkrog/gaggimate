/* Rung 2 baseline, transcribed before the Floor kernels were written.
 * GCC 14.2, xtensa-asm14.sh AnimFloor, bandRef's .L11/.L11_LEND:
 * 13 instructions per pixel, hardware loop, no adjacent load consumers.
 * Original bandRef: 115 instructions, est_weight=116, 2 hardware loops.
 * The production kernel retains this index/gather schedule but pairs pixels,
 * rotates register-held dither and stores both pixels with one s32i.
 */
static __attribute__((noinline)) uint32_t floorGccRowAsm(
    uint16_t *out, const int16_t *row, const int16_t *dither,
    const uint16_t *pal, uint32_t u, uint32_t du, int n) {
    int x = 0;
    int t0, t1;
    asm volatile("loopnez %[n], 1f\n"
                 "extui %[t0], %[u], 16, 6\n"
                 "extui %[t1], %[x], 0, 3\n"
                 "addx2 %[t0], %[t0], %[row]\n"
                 "addx2 %[t1], %[t1], %[dither]\n"
                 "l16si %[t0], %[t0], 0\n"
                 "l16si %[t1], %[t1], 0\n"
                 "add %[u], %[u], %[du]\n"
                 "add %[t0], %[t0], %[t1]\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "l16ui %[t0], %[t0], 4\n"
                 "addi %[x], %[x], 1\n"
                 "s16i %[t0], %[out], 0\n"
                 "addi %[out], %[out], 2\n"
                 "1:\n"
                 : [out] "+&r"(out), [u] "+&r"(u), [x] "+&r"(x),
                   [t0] "=&r"(t0), [t1] "=&r"(t1)
                 : [row] "r"(row), [dither] "r"(dither), [pal] "r"(pal), [du] "r"(du), [n] "r"(n)
                 : "memory");
    return u;
}
