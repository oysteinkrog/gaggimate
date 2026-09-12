/* Isolated probes for VLDBC.16.IP, MOVI.32.A and VUNZIP.16, testing their
 * lane order and post-increment before using them in the hot kernel. */
static int pieProbe(void) {
    uint32_t input[8] __attribute__((aligned(16)));
    uint16_t output[8] __attribute__((aligned(16)));
    for (int i = 0; i < 8; ++i) input[i] = 0x72000000u + i * 0x10001u;
    const uint32_t *in = input;
    uint16_t *out = output;
    asm volatile("ee.vld.128.ip q0, %[in], 16\n"
                 "ee.vld.128.ip q1, %[in], 16\n"
                 "ee.vunzip.16 q0, q1\n"
                 "ee.vst.128.ip q0, %[out], 0\n"
                 : [in] "+&r"(in) : [out] "r"(out) : "memory");
    for (int i = 0; i < 8; ++i) if (output[i] != i) return 1;
    uint32_t c[2] = {0x12345678u, 0};
    const uint32_t *cp = c;
    uint32_t word;
    asm volatile("ee.vldbc.16.ip q2, %[cp], 4\n"
                 "ee.movi.32.a q2, %[word], 3\n"
                 : [cp] "+&r"(cp), [word] "=&r"(word) : : "memory");
    if (cp != c + 1 || word != 0x56785678u) return 2;
    return 0;
}
