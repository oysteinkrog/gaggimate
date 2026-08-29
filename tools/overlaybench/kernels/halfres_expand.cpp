#include "halfres_expand.h"

#include <cstdint>
#include <cstring>

namespace ovb {

// Byte-identical to the fill+copy loop pair in SleepAnimation::renderFrame's
// half-resolution expansion.
void expandRow_ref(const uint16_t *__restrict src, uint16_t *__restrict row0, uint16_t *__restrict row1, int rw) {
    uint32_t *__restrict d0 = reinterpret_cast<uint32_t *>(row0);
    uint32_t *__restrict d1 = reinterpret_cast<uint32_t *>(row1);
    for (int i = 0; i < rw; i++) {
        const uint32_t v = src[i];
        d0[i] = v | (v << 16);
    }
    for (int i = 0; i < rw; i++) {
        const uint32_t v = src[i];
        d1[i] = v | (v << 16);
    }
}

// ---------------------------------------------------------------------------
// Sanity-check variant (task 1): validate the pairing arithmetic for a wider
// store before attempting one in real PIE asm. Two adjacent `v | (v<<16)`
// words (four source pixels) are concatenated into one uint64_t and written
// via memcpy -- memcpy rather than a `uint64_t*` reinterpret_cast so this
// stays free of alignment/strict-aliasing UB regardless of how `row0`/`row1`
// happen to be allocated by whatever calls it (GCC turns a small constant-
// size memcpy into a single wide store when the target and alignment allow
// it, exactly as if it had been written directly).
//
// Values and order are identical to expandRow_ref: this only changes how
// many pixels are flushed to memory together, never a computed value.
//
// Expected outcome, reasoned (not measured -- this is a host-only sanity
// check): x86-64 already issues natural 8-byte stores, so this may or may
// not move the host number. It is not expected to help the real device:
// Xtensa (a 32-bit ISA) has no native 64-bit integer store, so GCC lowers a
// uint64_t store back into two 32-bit stores (s32i x2) -- confirmed by
// reading xtensa-asm/halfres_expand.S after running ./asm.sh halfres_expand,
// not assumed. At best this is a wash against expandRow_ref; the extra
// shift/OR needed to assemble the 64-bit temporary can make it slightly
// worse. Kept here because it establishes, before writing a single line of
// PIE asm, that the four-pixels-at-a-time grouping produces the exact same
// bytes as processing one pixel at a time -- the thing expandRow_pie_model
// and expandRow_pie_asm both also rely on.
void expandRow_wide64(const uint16_t *__restrict src, uint16_t *__restrict row0, uint16_t *__restrict row1, int rw) {
    uint32_t *__restrict d0 = reinterpret_cast<uint32_t *>(row0);
    uint32_t *__restrict d1 = reinterpret_cast<uint32_t *>(row1);

    int i = 0;
    for (; i + 2 <= rw; i += 2) {
        const uint32_t v0 = src[i];
        const uint32_t v1 = src[i + 1];
        const uint32_t w0 = v0 | (v0 << 16);
        const uint32_t w1 = v1 | (v1 << 16);
        const uint64_t packed = static_cast<uint64_t>(w0) | (static_cast<uint64_t>(w1) << 32);
        std::memcpy(&d0[i], &packed, sizeof(packed));
    }
    for (; i < rw; i++) {
        const uint32_t v = src[i];
        d0[i] = v | (v << 16);
    }

    // Independent re-derivation from `src`, exactly like expandRow_ref --
    // NOT a copy of row0 (see the file header and SleepAnimation.cpp's own
    // comment on why that was reverted).
    i = 0;
    for (; i + 2 <= rw; i += 2) {
        const uint32_t v0 = src[i];
        const uint32_t v1 = src[i + 1];
        const uint32_t w0 = v0 | (v0 << 16);
        const uint32_t w1 = v1 | (v1 << 16);
        const uint64_t packed = static_cast<uint64_t>(w0) | (static_cast<uint64_t>(w1) << 32);
        std::memcpy(&d1[i], &packed, sizeof(packed));
    }
    for (; i < rw; i++) {
        const uint32_t v = src[i];
        d1[i] = v | (v << 16);
    }
}

// ---------------------------------------------------------------------------
// Pure-C model of expandRow_pie_asm's grouping (task 2, correctness gate).
// Same values as expandRow_ref, grouped four pixels at a time -- the same
// grouping the real asm flushes with one EE.VST.128.IP -- so this function
// carries the "four independent widened words, then one flush" shape the
// asm's derivation is checked against, per the span_scan pie_model
// convention (see the block comment above scanRowPieModelT in
// span_scan.cpp for the precedent this follows).
void expandRow_pie_model(const uint16_t *__restrict src, uint16_t *__restrict row0, uint16_t *__restrict row1,
                          int rw) {
    uint16_t *const rows[2] = {row0, row1};
    for (int pass = 0; pass < 2; pass++) {
        uint32_t *__restrict d = reinterpret_cast<uint32_t *>(rows[pass]);
        int i = 0;
        for (; i + 4 <= rw; i += 4) {
            const uint32_t v0 = src[i];
            const uint32_t v1 = src[i + 1];
            const uint32_t v2 = src[i + 2];
            const uint32_t v3 = src[i + 3];
            const uint32_t w0 = v0 | (v0 << 16);
            const uint32_t w1 = v1 | (v1 << 16);
            const uint32_t w2 = v2 | (v2 << 16);
            const uint32_t w3 = v3 | (v3 << 16);
            // The "quad flush": expandStoreQuad's contract is exactly these
            // four stores issued together as one 16-byte write.
            d[i] = w0;
            d[i + 1] = w1;
            d[i + 2] = w2;
            d[i + 3] = w3;
        }
        for (; i < rw; i++) {
            const uint32_t v = src[i];
            d[i] = v | (v << 16);
        }
    }
}

#if defined(__XTENSA__)
// ---------------------------------------------------------------------------
// Real ESP32-S3 PIE implementation of the wider-store hypothesis (task 2).
// Xtensa-only inline asm: cannot build, and has never been run, on this x86
// host -- see piePending on this variant's kExpandRowVariants entry.
//
// Why this does NOT follow scale565Oct's shape (vector load -> vector ALU ->
// vector store): that shape needs a PIE instruction that replicates one
// 16-bit source lane into two adjacent 16-bit output lanes, which is the
// actual operation this kernel needs (`v` doubled into `(v,v)`). No such
// instruction turned up anywhere in the one real, compiled artifact
// available to check against: disassembling the vendored
// espressif__esp-dsp static library for this exact target
// (~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/lib/
// libespressif__esp-dsp.a, checked in this session) surfaces zip/unzip only
// as `ee.vzip.32`/`ee.vunzip.32` (32-bit lanes, used once, in
// dsps_fft2r_sc16_aes3_, to interleave/deinterleave two DIFFERENT input
// streams -- not to replicate one lane within itself), never a 16-bit-lane
// variant. Inventing a `ee.vzip.16` opcode with the semantics this kernel
// would need, without a datasheet or a compiled use site to confirm it
// exists, would mean shipping unverified opcode semantics in real inline
// asm -- exactly the trap this whole exercise is supposed to avoid.
//
// What the same disassembly DOES confirm, and what this function uses
// instead:
//   - `ee.movi.32.q qX, aY, imm` (move a 32-bit GPR value into lane `imm`
//     (0..3) of qX) is real: dsps_fft2r_sc16_aes3_ uses it four times in a
//     row (imm 0,1,2,3) to fill one q register from four GPRs.
//   - The exact idiom this function follows -- compute scalar words in
//     GPRs, assemble them into a q register with movi.32.q, flush with one
//     EE.VST.128 -- is not a novel construction: it is how
//     dsps_memset_aes3 (Espressif's own vectorized memset for this chip)
//     builds its wide stores. It computes one repeated fill word in a GPR,
//     writes it into all four lanes with four `ee.movi.32.q` calls, then
//     stores 16 B at a time with EE.VST.128. That is real, shipping code
//     for this exact target, not a guess -- this function is the same
//     idiom with four DIFFERENT computed words instead of one repeated one.
//
// This also means the *read* side is deliberately left alone: `halfBuf`
// (this kernel's `src`) is requested with MALLOC_CAP_INTERNAL in
// SleepAnimation.cpp (allocPreferInternal), i.e. it prefers SRAM, not
// PSRAM -- so the source read was never the bandwidth problem BASELINE-
// OVERLAY.md and the source comment describe; only `band[]` (row0/row1
// here) is PSRAM. Vectorizing the read too would add EE.VLD.128 traffic on
// a buffer that was never the bottleneck, for no benefit this hypothesis is
// actually about.
//
// So: four independently computed 32-bit words -- same arithmetic, same
// order as expandRow_ref -- are assembled into one q register and flushed
// with a single EE.VST.128.IP, turning four 4-byte stores into one 16-byte
// store without changing a single computed value.
//
// Alignment: EE.VST.128 forces the low four address bits to zero rather
// than trapping (same hazard scale565Oct's and scanRow_pie_asm's comments
// document), so the vector path is only taken when the row pointer is
// 16-byte aligned; an unaligned row falls back to scanline-scalar in full,
// identical to expandRow_ref, so this can never be less correct, only
// faster on the aligned common case. Real band[] rows are always aligned:
// bandBuf is `heap_caps_aligned_alloc(64, bandBytes, ...)` and the row
// stride (PANEL_W*2 = 960 B) is itself a multiple of 64, so every row0/row1
// pointer this kernel is actually called with on device is 64-byte aligned,
// hence certainly 16-byte aligned -- the runtime check exists for this
// function's general ExpandRowFn contract, not because the real caller is
// expected to trip it.
__attribute__((always_inline)) static inline void expandStoreQuad(uint32_t *&dst, uint32_t w0, uint32_t w1,
                                                                    uint32_t w2, uint32_t w3) {
    asm volatile("ee.movi.32.q q0, %[w0], 0\n"
                 "ee.movi.32.q q0, %[w1], 1\n"
                 "ee.movi.32.q q0, %[w2], 2\n"
                 "ee.movi.32.q q0, %[w3], 3\n"
                 "ee.vst.128.ip q0, %[d], 16\n"
                 : [d] "+r"(dst)
                 : [w0] "r"(w0), [w1] "r"(w1), [w2] "r"(w2), [w3] "r"(w3)
                 : "memory");
}

// One row's worth. `d` is a walking pointer advanced 16 B per quad by
// expandStoreQuad's EE.VST.128.IP; the ragged tail (rw not a multiple of 4)
// walks the SAME pointer with `*d++`, not `d[i]` off the original base --
// indexing off the base after the vector loop already moved it would double
// -advance and write past the end of the row.
static void expandOneRowPie(const uint16_t *__restrict src, uint16_t *__restrict rowOut, int rw) {
    if ((reinterpret_cast<uintptr_t>(rowOut) & 0xF) != 0) {
        uint32_t *__restrict d = reinterpret_cast<uint32_t *>(rowOut);
        for (int i = 0; i < rw; i++) {
            const uint32_t v = src[i];
            d[i] = v | (v << 16);
        }
        return;
    }
    uint32_t *d = reinterpret_cast<uint32_t *>(rowOut);
    int i = 0;
    for (; i + 4 <= rw; i += 4) {
        const uint32_t v0 = src[i];
        const uint32_t v1 = src[i + 1];
        const uint32_t v2 = src[i + 2];
        const uint32_t v3 = src[i + 3];
        expandStoreQuad(d, v0 | (v0 << 16), v1 | (v1 << 16), v2 | (v2 << 16), v3 | (v3 << 16));
    }
    for (; i < rw; i++) {
        const uint32_t v = src[i];
        *d++ = v | (v << 16);
    }
}

void expandRow_pie_asm(const uint16_t *__restrict src, uint16_t *__restrict row0, uint16_t *__restrict row1,
                       int rw) {
    expandOneRowPie(src, row0, rw);
    expandOneRowPie(src, row1, rw);
}
#endif // defined(__XTENSA__)

const ExpandRowVariant kExpandRowVariants[] = {
    {"ref_scalar", &expandRow_ref, true, false},
    {"wide64", &expandRow_wide64, true, false},
    {"pie_model", &expandRow_pie_model, true, false},
#if defined(__XTENSA__)
    {"pie_asm", &expandRow_pie_asm, false, true},
#endif
};
const int kExpandRowVariantCount = sizeof(kExpandRowVariants) / sizeof(kExpandRowVariants[0]);

} // namespace ovb
