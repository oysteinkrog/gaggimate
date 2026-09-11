#ifndef GAGGIMATE_SIM

// "Harmonograph": one luminous Lissajous thread over a drifting, vignetted
// wash, entry 35 of tools/animbench/web/anim_bench.html. Every frame walks
// 2,048 samples. Four Q6 sine phases and the rotation interpolate the shared
// 1,024-entry, +/-512 sine table. Q2 positions select one of sixteen quarter
// pixel 11x11 stamps: radius 1.8 px flat core, cosine skirt to radius 5.4 px,
// raised to 1.5. Max coverage preserves every crossing. A smooth, nonzero
// comet envelope completes one lap per 8 seconds at speed 50.
//
// Eight parameters, all read in frame(): speed, figure size, thread glow and
// brightness, then lobe count (the two secondary curve frequencies), turn
// rate (the whole figure's rotation), trail length (the comet envelope's
// exponent) and vignette (the ground's quadratic rim falloff). Each of the
// four added on 2026-09-10 (gm-3vj.38) is exactly the constant this file used
// to hard-code at its default, so the default frame is unchanged. None of
// them reaches the pixel loop: they act on the curve samples, the comet table
// and the row and column terms, and band(), bandRef() and harmonographRowAsm
// read the same tables they always did. Vignette does widen how far down
// rowTerm and ct can reach, so the saturation proof below and the sweep in
// tools/qemubench/tests/anim_harmonograph quote the wider domain.
//
// The page's wash is deliberately asymmetric here: rowTerm is multiplied
// by 32, colTerm is not. Bayer offsets are multiplied by 32 before the sum
// is shifted by 10. Making the axes symmetric changes the approved look.
// The page fixes its geometry at 480px, centred on (240,240), even for
// smaller dimensions. Keep those literal coordinates, including clipping.

#include "BgAnim.h"
#include "BgAnimCommon.h"
#include <math.h>
#include <string.h>

#ifndef GM_BGANIM_HARMONOGRAPH_ASM
#define GM_BGANIM_HARMONOGRAPH_ASM 1
#endif
// The PIE stamp pass in frame() (harmonographStampAsm). Off renders the
// portable stamp loop, which is also what the host and the simulator run.
#ifndef GM_BGANIM_HARMONOGRAPH_STAMP_ASM
#define GM_BGANIM_HARMONOGRAPH_STAMP_ASM 1
#endif
#if GM_BGANIM_HARMONOGRAPH_STAMP_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
#define HARMO_STAMP_ASM 1
#else
#define HARMO_STAMP_ASM 0
#endif

namespace {
using namespace bganim;
constexpr int NS = 2048;
constexpr int SW = 11, SH = 11;
constexpr int STAMP_BYTES = 16 * SW * SH;
// Per stamp and stamp row, the first and one-past-last column with a
// nonzero byte (i0, i1): the skirt ends at radius 5.4 px inside an 11 px
// box, so the corners are zero and the stamp pass skips them. About 20
// percent of the bytes, and a zero can never raise a max.
constexpr int EXT_BYTES = 16 * SH * 2;
// The same sixteen stamps with every row padded to 16 bytes (bytes 11 to
// 15 zero), 16-byte aligned, for the PIE stamp pass: one aligned vector
// load per stamp row. STAMP16_BYTES carries 15 bytes of alignment slack.
constexpr int STAMP16_ROW = 16;
constexpr int STAMP16_BYTES = 16 * SH * STAMP16_ROW + 15;
// Slack around the coverage buffer: the PIE pass reads and writes a
// 32-byte aligned window around each stamp row, so its first window can
// start 16 bytes before the row and its last can end 16 bytes after the
// buffer. The bytes it touches outside the stamp are written back as
// read (a max against zero), but they must be ours.
constexpr int BUF_SLACK = 16 + 15 + 16;
constexpr float PI = 3.14159265358979323846f;

// At 480x480: ctPh 7,680 + rowTerm 960 + palette 512 + scratch 32 +
// ones 16 = 9,200 B of the 9,216 B slab. ctPh rows are padded to eight int16s,
// including at 466/233 wide, so vector loads start on 16-byte boundaries.
// The coverage image is a bulk sequential sweep in PSRAM, with 16-byte
// padded rows and an aligned alias. bufStorage owns that allocation.
// Frame-only PSRAM tables: colTerm 960, tail 2,048, dith 128, stamp 1,936,
// bufStorage 230,415 B at 480x480 (including 15 B alignment reserve).
int16_t *ctPh = nullptr, *rowTerm = nullptr, *colTerm = nullptr;
uint16_t *palette = nullptr, *scratch = nullptr, *ones = nullptr;
uint8_t *bufStorage = nullptr, *buf = nullptr, *tail = nullptr, *stamp = nullptr, *stampExt = nullptr;
uint8_t *stamp16Storage = nullptr, *stamp16 = nullptr;
#if defined(GM_TOUCH_PROBE) && defined(ESP_PLATFORM)
// Self-check scratch (mode 2 of bganim::g_harmoStampMode): a second
// coverage buffer the portable pass writes so the two can be compared.
uint8_t *chkStorage = nullptr, *chk = nullptr;
#endif
int16_t *dith = nullptr;
// Per-sample stamp positions in Q2 (posX, posY, NS each), the samples in
// stamp-row order (order, NS) and the counting sort's row cursors (rowCur,
// h entries). All PSRAM, all swept sequentially. Why the sort: in curve
// order each of the 2,048 samples lands on eleven coverage rows that the
// curve last touched a lap ago, so every stamp row is a PSRAM line miss;
// in row order the working set is eleven rows of the buffer and each line
// is fetched once a frame. Max compositing does not care about the order,
// so the picture is the same to the bit. Bench board, 2026-09-11, frame()
// timed on its own (framefn_us on /api/debug/anim): 25.9 ms in curve
// order, 19.7 sorted, 17 with the stamp extents and the branchless max
// below; of that, the stamp pass is about 11.5 ms, the union clear 3 to
// 5, the position pass 1.5 to 2, the row and column terms 0.6 and the
// sort 0.2. The scalar stamp pass was 9 instructions per byte in a
// zero-overhead loop (the compiler keeps a branch for the max);
// harmonographStampAsm below does a stamp row in 17 PIE instructions
// and took frame() to 11 to 13 ms (mode 0 against mode 1 through
// `harmostamp=` on the loadtest build, 19.2 and 21.3 ms against 11.8
// and 11.1; 71 frames compared byte for byte in mode 2, 0 mismatches;
// tools/qemubench/tests/anim_harmonograph_stamp is the QEMU proof).
int16_t *posX = nullptr, *posY = nullptr;
uint16_t *order = nullptr, *rowCur = nullptr;
const int16_t *sl = nullptr; // borrowed shared slab table, never released here
int allocW = 0, allocH = 0, ctStride = 0, bufStride = 0;
uint32_t lastThemeGen = 0xFFFFFFFF;
int lastTailKey = -1, lastBright = -1, top = 0;
// The box the previous frame stamped into buf, half open in buf
// coordinates, empty when x1 <= x0. frame() clears the union of this box
// and the frame's own box instead of the whole buffer: at 480x480 the
// buffer is 230 KB of PSRAM and a default-size figure covers about half of
// it. On the bench board the whole-buffer memset was not where frame()'s
// time went (2026-09-11: 25.9 ms before, 25.9 after this change alone; the
// stamp order above was), so this is the smaller saving of the two. Rows
// and columns outside the union hold the zeros init() wrote once.
int prevX0 = 0, prevX1 = 0, prevY0 = 0, prevY1 = 0;

void release();

bool init(int w, int h) {
    // Reject dimensions above the firmware's largest surface before a hot
    // allocation could silently spill out of the reserved slab.
    if (w < 1 || h < 1 || w > 480 || h > 480) {
        release();
        return false;
    }
    if (palette != nullptr && allocW == w && allocH == h) return true;
    release();
    allocW = w;
    allocH = h;
    ctStride = (w + 7) & ~7;
    bufStride = (w + 15) & ~15;
    sl = sinLut();
    if (sl == nullptr) {
        release();
        return false;
    }
    ctPh = static_cast<int16_t *>(allocHot(8 * ctStride * sizeof(int16_t)));
    rowTerm = static_cast<int16_t *>(allocHot(h * sizeof(int16_t)));
    palette = static_cast<uint16_t *>(allocHot(256 * sizeof(uint16_t)));
    scratch = static_cast<uint16_t *>(allocHot(16 * sizeof(uint16_t)));
    ones = static_cast<uint16_t *>(allocHot(8 * sizeof(uint16_t)));
    colTerm = static_cast<int16_t *>(alloc(w * sizeof(int16_t)));
    tail = static_cast<uint8_t *>(alloc(NS));
    posX = static_cast<int16_t *>(alloc(NS * sizeof(int16_t)));
    posY = static_cast<int16_t *>(alloc(NS * sizeof(int16_t)));
    order = static_cast<uint16_t *>(alloc(NS * sizeof(uint16_t)));
    rowCur = static_cast<uint16_t *>(alloc(static_cast<size_t>(h) * sizeof(uint16_t)));
    dith = static_cast<int16_t *>(alloc(64 * sizeof(int16_t)));
    stamp = static_cast<uint8_t *>(alloc(STAMP_BYTES));
    stampExt = static_cast<uint8_t *>(alloc(EXT_BYTES));
    stamp16Storage = static_cast<uint8_t *>(alloc(STAMP16_BYTES));
    bufStorage = static_cast<uint8_t *>(alloc(static_cast<size_t>(bufStride) * h + BUF_SLACK));
    if (!ctPh || !rowTerm || !palette || !scratch || !ones || !colTerm || !tail || !dith || !stamp || !stampExt || !stamp16Storage || !bufStorage || !posX ||
        !posY || !order || !rowCur) {
        // Every successful allocation is released, including those after
        // an earlier failure. A retry sees exactly the initial state.
        release();
        return false;
    }
    // 16 bytes of slack before buf and at least 16 after (see BUF_SLACK).
    buf = reinterpret_cast<uint8_t *>((reinterpret_cast<uintptr_t>(bufStorage) + 16 + 15) & ~uintptr_t(15));
    stamp16 = reinterpret_cast<uint8_t *>((reinterpret_cast<uintptr_t>(stamp16Storage) + 15) & ~uintptr_t(15));
    // Once, here: frame() only clears where the thread was and is. The
    // slack is zeroed too so the PIE pass's window reads defined bytes.
    memset(bufStorage, 0, static_cast<size_t>(bufStride) * h + BUF_SLACK);
    prevX0 = prevX1 = prevY0 = prevY1 = 0;
    for (int k = 0; k < 8; ++k) ones[k] = 1;
    for (int fy = 0; fy < 4; ++fy) {
        for (int fx = 0; fx < 4; ++fx) {
            const int o = (fy * 4 + fx) * SW * SH;
            for (int j = 0; j < SH; ++j) {
                for (int i = 0; i < SW; ++i) {
                    const float dx = i - 5 - fx * 0.25f, dy = j - 5 - fy * 0.25f;
                    const float r = sqrtf(dx * dx + dy * dy);
                    const float prof = r <= 1.8f ? 1.0f : r >= 5.4f ? 0.0f :
                        powf(0.5f * (1.0f + cosf(PI * (r - 1.8f) / (5.4f - 1.8f))), 1.5f);
                    stamp[o + j * SW + i] = static_cast<uint8_t>(lroundf(255.0f * prof));
                }
            }
        }
    }
    for (int k = 0; k < 16; ++k) {
        for (int j = 0; j < SH; ++j) {
            const uint8_t *row = stamp + k * SW * SH + j * SW;
            int i0 = 0, i1 = SW;
            while (i0 < SW && row[i0] == 0) ++i0;
            while (i1 > i0 && row[i1 - 1] == 0) --i1;
            stampExt[(k * SH + j) * 2] = static_cast<uint8_t>(i0);
            stampExt[(k * SH + j) * 2 + 1] = static_cast<uint8_t>(i1);
        }
    }
    memset(stamp16, 0, 16 * SH * STAMP16_ROW);
    for (int k = 0; k < 16; ++k)
        for (int j = 0; j < SH; ++j)
            memcpy(stamp16 + (k * SH + j) * STAMP16_ROW, stamp + k * SW * SH + j * SW, SW);
    // pcDither(..., 2.6), whole palette indices. No half-integer ties occur,
    // so lroundf agrees with JavaScript Math.round for negative values too.
    for (int k = 0; k < 64; ++k)
        dith[k] = static_cast<int16_t>(lroundf((BAYER8[k] - 31.5f) / 31.5f * 2.6f));
    return true;
}

// Positive float | 0 in JS truncates then wraps modulo 2^32. A uint64_t
// bridge avoids undefined conversions at the largest tMs and speed 100.
// All phase lookups mask away the high bits, including the sign bit.
// frame uses float on both firmware and host, while JS uses double. At
// long uptimes float loses fine phase bits; it still uses the page's direct
// tMs clock, with no extra accumulator drift or different cycle period.
uint32_t phase(float tt, float rate) {
    return static_cast<uint32_t>(static_cast<uint64_t>(tt * rate));
}

BGANIM_INLINE int sineQ6(uint32_t q) {
    const int k = (q >> 6) & 1023;
    return sl[k] + (((sl[(k + 1) & 1023] - sl[k]) * static_cast<int>(q & 63)) >> 6);
}


#if HARMO_STAMP_ASM
// One stamp, eleven rows, through the PIE: per row one aligned load of the
// padded stamp row, one EE.VMUL.U8 against the broadcast comet level with
// SAR 8 (the exact (s * lvl) >> 8 of the portable loop, at most 254), two
// funnel shifts that place the 16 bytes inside a 32-byte aligned window,
// and a max against the two coverage vectors of that window. The window
// starts at the last 16-byte boundary strictly before the stamp's left
// edge, so the stamp sits at offset a in 1..16 and the funnel shift is
// k = 16 - a in 0..15: L = {S, 0} >> k lands S at offset a, and H =
// {0, S} >> k carries what spills past byte 16. SAR_BYTE is latched from
// the low four bits of the address handed to EE.LD.128.USAR.IP, and the
// packed rows are 16-byte aligned, so pointing it at rows16 + k loads the
// row and sets the shift in one instruction. The PIE has no unsigned max,
// so both sides are biased by 0x80 for EE.VMAX.S8 and the result is
// biased back; every value is 0 to 254, so the bias is a bijection on the
// range. Bytes of the window outside the stamp meet a zero lane and are
// written back as read, which is why the coverage buffer carries 16 bytes
// of slack on both sides. q0 to q7 are all used; SAR and PIE context
// belong to FreeRTOS and CPENABLE is never written here.
GM_ANIM_IRAM __attribute__((noinline)) void harmonographStampAsm(uint8_t *base, int stride, const uint8_t *rows16, int k,
                                                                    const uint8_t *lvlPtr, int nrows) {
    const uint32_t bias = 0x80808080u;
    const uint8_t *usar = rows16 + k;
    asm volatile("ee.movi.32.q q7, %[bias], 0\n"
                 "ee.movi.32.q q7, %[bias], 1\n"
                 "ee.movi.32.q q7, %[bias], 2\n"
                 "ee.movi.32.q q7, %[bias], 3\n"
                 "ee.vldbc.8 q6, %[lvl]\n"
                 "ee.zero.q q1\n"
                 "ssai 8\n"
                 "loop %[n], 1f\n"
                 "ee.ld.128.usar.ip q0, %[usar], 16\n"
                 "ee.vmul.u8 q0, q0, q6\n"
                 "ee.src.q q2, q1, q0\n"
                 "ee.src.q q3, q0, q1\n"
                 "ee.vld.128.ip q4, %[out], 16\n"
                 "ee.vld.128.ip q5, %[out], -16\n"
                 "ee.xorq q2, q2, q7\n"
                 "ee.xorq q3, q3, q7\n"
                 "ee.xorq q4, q4, q7\n"
                 "ee.xorq q5, q5, q7\n"
                 "ee.vmax.s8 q4, q4, q2\n"
                 "ee.vmax.s8 q5, q5, q3\n"
                 "ee.xorq q4, q4, q7\n"
                 "ee.xorq q5, q5, q7\n"
                 "ee.vst.128.ip q4, %[out], 16\n"
                 "ee.vst.128.ip q5, %[out], -16\n"
                 "add %[out], %[out], %[stride]\n"
                 "1:\n"
                 : [out] "+&r"(base), [usar] "+&r"(usar)
                 : [n] "r"(nrows), [stride] "r"(stride), [lvl] "r"(lvlPtr), [bias] "r"(bias)
                 : "memory");
}
#endif

// The portable stamp: eleven rows of at most eleven bytes, max composited.
// An 11-byte stamp row cannot contain an aligned 16-byte PIE span, so this
// keeps to scalar byte stores and skips each row's zero corners through the
// extents. Branchless max: the S3 has MAX, and the data-dependent branch
// this replaces mispredicted on about every other byte.
inline void stampScalar(uint8_t *dst, int dstStride, int x0, int y0, int sub, int lvl) {
    const uint8_t *sb = stamp + sub * SW * SH;
    const uint8_t *ext = stampExt + sub * SH * 2;
    for (int j = 0; j < SH; ++j) {
        uint8_t *out = dst + static_cast<size_t>(y0 + j) * dstStride + x0;
        const uint8_t *sr = sb + j * SW;
        const int i0 = ext[j * 2], i1 = ext[j * 2 + 1];
        for (int i = i0; i < i1; ++i) {
            const int g = (sr[i] * lvl) >> 8;
            const int o = out[i];
            out[i] = static_cast<uint8_t>(o > g ? o : g);
        }
    }
}

// Every stamp of the frame into dst, in the sorted order. usePie selects the
// PIE pass where it is compiled in; otherwise the portable one.
void stampAll(uint8_t *dst, int total, int head, bool usePie) {
    for (int k = 0; k < total; ++k) {
        const int s = order[k];
        const int pxq = posX[s], pyq = posY[s];
        const int x0 = (pxq >> 2) - 5, y0 = (pyq >> 2) - 5;
        const int sub = ((pyq & 3) * 4) + (pxq & 3);
        const uint8_t *lvlPtr = tail + ((s - head) & (NS - 1));
#if HARMO_STAMP_ASM
        if (usePie) {
            const int base = (x0 - 1) & ~15;
            harmonographStampAsm(dst + static_cast<size_t>(y0) * bufStride + base, bufStride,
                                 stamp16 + sub * SH * STAMP16_ROW, 16 - (x0 - base), lvlPtr, SH);
            continue;
        }
#else
        (void)usePie;
#endif
        stampScalar(dst, bufStride, x0, y0, sub, *lvlPtr);
    }
}

void frame(uint32_t tMs, int w, int h, const uint8_t p[BG_ANIM_PARAMS]) {
    const uint32_t gen = themeGen();
    if (lastBright != p[3] || lastThemeGen != gen) {
        // The page applies 176..256 Q8 brightness after themeRGB, using
        // round(p[3]*0.8). Keep this explicit animation parameter mapping.
        buildThemeRamp(palette, 176 + (static_cast<int>(p[3]) * 8 + 5) / 10);
        lastBright = p[3];
        lastThemeGen = gen;
    }
    // The comet table depends on thread glow and on trail length, so both
    // gate its rebuild. Trail length reshapes the envelope: the exponent is
    // 9.0 at slider 0, where the thread sits at its floor except for a short
    // bright head, exactly 2.6 at 50, and 0.4 at 100, where the whole thread
    // glows. It is carried in thousandths so 50 divides to the old literal
    // exactly. The table's own range, floorLvl to 255, never moves with it.
    const int tailKey = static_cast<int>(p[2]) | (static_cast<int>(p[6]) << 8);
    if (lastTailKey != tailKey) {
        const float floorLvl = 78.0f + p[2] * 0.35f;
        const int texpM = p[6] <= 50 ? 2600 + (50 - static_cast<int>(p[6])) * 128 : 2600 - (static_cast<int>(p[6]) - 50) * 44;
        const float texp = static_cast<float>(texpM) / 1000.0f;
        for (int k = 0; k < NS; ++k) {
            const float u = 0.5f * (1.0f + cosf(2.0f * PI * k / NS));
            tail[k] = static_cast<uint8_t>(lroundf(floorLvl + (255.0f - floorLvl) * powf(u, texp)));
        }
        top = 196 + (static_cast<int>(p[2]) * 55 + 50) / 100;
        lastTailKey = tailKey;
    }
    const float tt = static_cast<float>(tMs) * speedMul(p[0]);
    const uint32_t g1 = phase(tt, 0.0170f), g2 = phase(tt, 0.0119f);
    const uint32_t g3 = phase(tt, 0.0098f), g4 = phase(tt, 0.0145f);
    // Vignette scales the half quadratic each axis subtracts, which is what
    // darkens the rim: 0 at slider 0, so the ground is a flat drifting wash
    // out to the panel edge, the original 1560 at 50, and 2340 at 100, where
    // the frame closes down to a small bright centre. The scale is a whole
    // number and is 1560 at 50, so the subtracted term is the old float
    // product there. This is the
    // one added parameter that widens an operand domain: rowTerm and colTerm
    // reach -2550 rather than -1770, and ct -2646 rather than -1866. The
    // proof under the kernel and the QEMU sweep both quote the wider range.
    const int vign = p[7] <= 50 ? (1560 * static_cast<int>(p[7])) / 50 : 1560 + ((static_cast<int>(p[7]) - 50) * 780) / 50;
    for (int x = 0; x < w; ++x) {
        const float q = (x - 240) / 240.0f;
        colTerm[x] = ((sl[(x * 3 + g1) & 1023] * 135) >> 9) +
                     ((sl[(x * 7 - g2) & 1023] * 75) >> 9) - static_cast<int>(vign * q * q);
    }
    for (int y = 0; y < h; ++y) {
        const float q = (y - 240) / 240.0f;
        rowTerm[y] = ((sl[(y * 4 + g3) & 1023] * 135) >> 9) +
                     ((sl[(y * 5 - g4) & 1023] * 75) >> 9) - static_cast<int>(vign * q * q);
    }
    for (int ph = 0; ph < 8; ++ph) {
        for (int x = 0; x < w; ++x)
            ctPh[ph * ctStride + x] = colTerm[x] + dith[ph * 8 + (x & 7)] * 32;
    }
    const float at = 60.0f + p[1] * 0.95f;
    const int a1 = static_cast<int>(at * 0.69f), a2 = static_cast<int>(at * 0.31f);
    const uint32_t q1 = phase(tt, 0.704f), q2 = phase(tt, 0.4544f);
    const uint32_t q3 = phase(tt, 0.3136f), q4 = phase(tt, 0.5952f);
    // Lobe count shifts both secondary sample strides by whole steps of 32 in
    // Q6, which is half a sine table entry. The shift is 0 at slider 50, so
    // the strides are the original 320 and 256 there; at 0 they are 128 and
    // 64 and at 100 they are 512 and 448. A stride is 32 per cycle over the
    // 2048 samples, so the second frequency runs 4 to 16 cycles on the x axis
    // and 2 to 14 on the y, against the first pair's fixed 6 and 4, and the
    // figure gains or loses lobes with it. Every stride stays a multiple of 32,
    // so 2048 samples is a whole number of table laps and the thread still
    // closes on itself rather than leaving a gap.
    const int lobeOff = ((static_cast<int>(p[4]) - 50) * 6) / 50 * 32;
    const uint32_t sx2 = static_cast<uint32_t>(320 + lobeOff);
    const uint32_t sy2 = static_cast<uint32_t>(256 + lobeOff);
    // Turn rate scales the whole figure's rotation, from held still at slider
    // 0 through the original rate at 50 to three times it at 100. The
    // multiplier is carried as a percentage, so 50 divides to exactly 1.0f
    // and the phase is the old one there. ca and sa stay within +/-512
    // whatever the rate.
    const int turnPct = p[5] <= 50 ? static_cast<int>(p[5]) * 2 : 100 + (static_cast<int>(p[5]) - 50) * 4;
    const float turnMul = static_cast<float>(turnPct) / 100.0f;
    const uint32_t rot = phase(tt, 0.18176f * turnMul); // Q6, 360 degrees per ~360.56 s at 50
    const int ca = sineQ6(rot + 256 * 64), sa = sineQ6(rot);
    const int head = phase(tt, NS / 8000.0f) & (NS - 1);
    // Q6 strides 192 and 128 are exactly 3 and 2 table entries; the two
    // secondary strides are 320 and 256 at lobe count 50 and any other
    // multiple of 32 otherwise, where the sine table's own interpolation
    // carries the half entry. The weighted coordinates use Q9 sine, and
    // the rotation shifts by 7 to Q2. Neither the strides nor the turn
    // rate change how far a sample can land from the centre. The stamp
    // corner comes out in whole pixels, the sub-pixel phase in Q2.
    const auto samplePos = [&](int s, int &pxq, int &pyq) {
        const int ux = (a1 * sineQ6(s * 192u + q1) + a2 * sineQ6(s * sx2 + q2)) >> 9;
        const int uy = (a1 * sineQ6(s * 128u + q3) + a2 * sineQ6(s * sy2 + q4)) >> 9;
        pxq = 240 * 4 + ((ux * ca - uy * sa) >> 7);
        pyq = 240 * 4 + ((ux * sa + uy * ca) >> 7);
    };
    // First pass: every sample's position, the box this frame's stamps will
    // cover, and how many stamps start on each row. A sample the clip drops
    // gets posY -1 and is skipped by the passes below; the clip is the one
    // the stamp pass has always used.
    int curX0 = w, curX1 = 0, curY0 = h, curY1 = 0;
    memset(rowCur, 0, static_cast<size_t>(h) * sizeof(uint16_t));
    for (int s = 0; s < NS; ++s) {
        int pxq, pyq;
        samplePos(s, pxq, pyq);
        const int x0 = (pxq >> 2) - 5, y0 = (pyq >> 2) - 5;
        if (x0 < 0 || y0 < 0 || x0 + SW > w || y0 + SH > h) {
            posY[s] = -1;
            continue;
        }
        posX[s] = static_cast<int16_t>(pxq);
        posY[s] = static_cast<int16_t>(pyq);
        rowCur[y0]++;
        if (x0 < curX0) curX0 = x0;
        if (x0 + SW > curX1) curX1 = x0 + SW;
        if (y0 < curY0) curY0 = y0;
        if (y0 + SH > curY1) curY1 = y0 + SH;
    }
    // Counting sort by stamp row: turn the counts into start cursors, then
    // scatter the sample indices. The index, not the position, is what the
    // stamp pass needs, since the comet level is looked up by sample.
    int total = 0;
    for (int y = 0; y < h; ++y) {
        const int c = rowCur[y];
        rowCur[y] = static_cast<uint16_t>(total);
        total += c;
    }
    for (int s = 0; s < NS; ++s) {
        if (posY[s] < 0) continue;
        order[rowCur[(posY[s] >> 2) - 5]++] = static_cast<uint16_t>(s);
    }
    // Clear the union of the previous frame's box and this one, row by row,
    // and nothing else: everything outside it is still the zero init() wrote.
    {
        const bool curEmpty = curX1 <= curX0 || curY1 <= curY0;
        const bool prevEmpty = prevX1 <= prevX0 || prevY1 <= prevY0;
        int ux0 = curEmpty ? prevX0 : curX0, ux1 = curEmpty ? prevX1 : curX1;
        int uy0 = curEmpty ? prevY0 : curY0, uy1 = curEmpty ? prevY1 : curY1;
        if (!prevEmpty && !curEmpty) {
            ux0 = prevX0 < ux0 ? prevX0 : ux0;
            ux1 = prevX1 > ux1 ? prevX1 : ux1;
            uy0 = prevY0 < uy0 ? prevY0 : uy0;
            uy1 = prevY1 > uy1 ? prevY1 : uy1;
        }
        if (ux1 > ux0 && uy1 > uy0) {
            const size_t n = static_cast<size_t>(ux1 - ux0);
            for (int y = uy0; y < uy1; ++y) memset(buf + static_cast<size_t>(y) * bufStride + ux0, 0, n);
        }
        prevX0 = curEmpty ? 0 : curX0;
        prevX1 = curEmpty ? 0 : curX1;
        prevY0 = curEmpty ? 0 : curY0;
        prevY1 = curEmpty ? 0 : curY1;
    }
    // Stamp pass, in row order (see posX above for why).
#if defined(GM_TOUCH_PROBE) && defined(ESP_PLATFORM)
    const int mode = HARMO_STAMP_ASM ? bganim::g_harmoStampMode.load() : 0;
    if (mode == 2) {
        // Both passes on identical input, then a byte compare of the whole
        // buffer including the slack: the PIE pass must leave the bytes
        // beside a stamp exactly as it found them.
        const size_t bytes = static_cast<size_t>(bufStride) * h + BUF_SLACK;
        if (chkStorage == nullptr) {
            chkStorage = static_cast<uint8_t *>(alloc(bytes));
            chk = chkStorage != nullptr ? chkStorage + (buf - bufStorage) : nullptr;
        }
        if (chk != nullptr) {
            memcpy(chkStorage, bufStorage, bytes);
            stampAll(buf, total, head, true);
            stampAll(chk, total, head, false);
            uint32_t bad = 0;
            for (size_t i = 0; i < bytes; ++i) bad += bufStorage[i] != chkStorage[i];
            bganim::g_harmoStampChecked.fetch_add(1);
            if (bad != 0) bganim::g_harmoStampMismatch.fetch_add(bad);
            return;
        }
    }
    stampAll(buf, total, head, mode != 0);
#else
    stampAll(buf, total, head, HARMO_STAMP_ASM != 0);
#endif
}

GM_ANIM_IRAM void bandRef(uint16_t *__restrict dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int r = 0; r < rows; ++r) {
        const int y = y0 + r;
        const int rt = rowTerm[y] * 32; // shifting a negative signed value would be UB
        const int16_t *__restrict ct = ctPh + (y & 7) * ctStride;
        const uint8_t *__restrict coverage = buf + y * bufStride;
        uint16_t *__restrict out = dst + static_cast<size_t>(r) * w;
        for (int x = 0; x < w; ++x) {
            int i = 90 + ((ct[x] + rt) >> 10);
            const int g = coverage[x];
            if (g != 0) i += ((top - i) * g) >> 8;
            out[x] = palette[i < 0 ? 0 : i > 255 ? 255 : i];
        }
    }
}

#if GM_BGANIM_HARMONOGRAPH_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
// GCC 14's original .L8 hardware loop is 20 instructions/pixel when g!=0,
// 18 when g==0, including a branch and a multiply even for zero coverage.
// Its schedule is l16si/l8ui/add/srai/addi/sub/mull/mov/beqz/[srai/add]/
// min/extui/addx2/movltz/l16ui/addi/s16i/addi/addi. The loads already have
// independent instructions before use. The edge here is lane arithmetic,
// removal of the redundant branch/clamp, and paired stores, not unrolling
// that already sound scalar schedule into a larger register window.
//
// Split rt=32*rowTerm into 1024*(rowTerm>>5) + 32*(rowTerm&31).
// Thus i=(90+(rowTerm>>5)) + ((ct+32*(rowTerm&31))>>10), exactly,
// including negative rows. rowTerm is -2550..210 and ct is -2646..306,
// both at vignette 100; at the default they are -1770..210 and -1866..306.
// The lane sum is -2646..1298, i is 7..97, top is 196..251, and
// coverage is 0..254 (QEMU additionally tests 255). The composite lies
// in 7..250, so signed lane adds/subtracts never saturate and the palette
// clamp cannot fire. Unsigned coverage widened against zero stays positive.
//
// Row constants are broadcast from scalars, without a stack table. one is
// eight uint16 ones in the hot slab: SAR=10 turns its multiply into a shift.
// ct and coverage are aligned by padded allocation, indices is 32 bytes
// in the hot slab, and output uses only scalar word stores. No vector load
// or store is ever given the caller's merely 4-byte-aligned destination.
// All named register results are early clobbers. GCC does not allocate q
// registers and provides no q clobber syntax; this block owns q0..q7.
// SAR and PIE context belong to FreeRTOS. Never write CPENABLE here.
//
// Two eight-lane groups share one 16-byte coverage load. The 11-instruction
// scalar pair loop hides every load-use gap and writes one s32i per pair.
// The surrounding 16-pixel loop uses a branch so hardware loops never nest.
// The slab scratch keeps the vector-to-gather handoff off the PSRAM stack.
// Its body executes 114 instructions per 16 pixels (7.125/pixel), including
// the eight scalar pairs and loop control. At one issue per cycle plus the
// outer taken branch's two cycles, the hot-cache estimate is 7.25 cycles
// per pixel, excluding row setup, stores waiting on the bus, and preemption.
// Device timing still decides whether this beats GCC under panel traffic;
// host timing cannot measure PIE, load interlocks, or PSRAM contention.
GM_ANIM_IRAM __attribute__((noinline)) void harmonographRowAsm(
    uint16_t *out, const int16_t *ct, const uint8_t *coverage, const uint16_t *pal,
    uint16_t *indices, const uint16_t *one, int rowValue, int topValue, int n16) {
    // Multiplication by 65537 replicates a nonnegative halfword into both
    // halves of a scalar word. Four moves then broadcast all eight lanes.
    const uint32_t rem = (uint32_t)((rowValue & 31) * 32) * 65537u;
    const uint32_t base = (uint32_t)(90 + (rowValue >> 5)) * 65537u;
    const uint32_t crest = (uint32_t)topValue * 65537u;
    asm volatile("ee.movi.32.q q4, %[rem], 0\n"
                 "ee.movi.32.q q4, %[rem], 1\n"
                 "ee.movi.32.q q4, %[rem], 2\n"
                 "ee.movi.32.q q4, %[rem], 3\n"
                 "ee.movi.32.q q5, %[base], 0\n"
                 "ee.movi.32.q q5, %[base], 1\n"
                 "ee.movi.32.q q5, %[base], 2\n"
                 "ee.movi.32.q q5, %[base], 3\n"
                 "ee.movi.32.q q7, %[crest], 0\n"
                 "ee.movi.32.q q7, %[crest], 1\n"
                 "ee.movi.32.q q7, %[crest], 2\n"
                 "ee.movi.32.q q7, %[crest], 3\n"
                 : : [rem] "r"(rem), [base] "r"(base), [crest] "r"(crest) : "memory");
    // Both blocks are in one noinline leaf. GCC cannot insert PIE work or
    // calls between them; its scalar register setup leaves q4/q5/q7 intact.
    uint32_t t0, t1;
    uint16_t *walk;
    const int pairs = 8;
    asm volatile("beqz %[n], 3f\n"
                 "1:\n"
                 "ee.vld.128.ip q0, %[g], 16\n"
                 "ee.vld.128.ip q2, %[ct], 16\n"
                 "ee.vld.128.ip q3, %[ct], 16\n"
                 "ee.zero.q q1\n"
                 "ee.vld.128.ip q6, %[one], 0\n"
                 "ee.vzip.8 q0, q1\n"
                 "ee.vadds.s16 q2, q2, q4\n"
                 "ee.vadds.s16 q3, q3, q4\n"
                 "ssai 10\n"
                 "ee.vmul.s16 q2, q2, q6\n"
                 "ee.vmul.s16 q3, q3, q6\n"
                 "ee.vadds.s16 q2, q2, q5\n"
                 "ee.vadds.s16 q3, q3, q5\n"
                 "ssai 8\n"
                 "ee.vsubs.s16 q6, q7, q2\n"
                 "ee.vmul.s16 q0, q6, q0\n"
                 "ee.vsubs.s16 q6, q7, q3\n"
                 "ee.vmul.s16 q1, q6, q1\n"
                 "ee.vadds.s16 q2, q2, q0\n"
                 "ee.vadds.s16 q3, q3, q1\n"
                 "mov %[walk], %[indices]\n"
                 "ee.vst.128.ip q2, %[walk], 16\n"
                 "ee.vst.128.ip q3, %[walk], -16\n"
                 "loop %[pairs], 2f\n"
                 "l16ui %[t0], %[walk], 0\n"
                 "l16ui %[t1], %[walk], 2\n"
                 "addx2 %[t0], %[t0], %[pal]\n"
                 "addx2 %[t1], %[t1], %[pal]\n"
                 "l16ui %[t0], %[t0], 0\n"
                 "l16ui %[t1], %[t1], 0\n"
                 "addi %[walk], %[walk], 4\n"
                 "slli %[t1], %[t1], 16\n"
                 "or %[t0], %[t0], %[t1]\n"
                 "s32i %[t0], %[out], 0\n"
                 "addi %[out], %[out], 4\n"
                 "2:\n"
                 "addi %[n], %[n], -1\n"
                 "bnez %[n], 1b\n"
                 "3:\n"
                 : [out] "+&r"(out), [ct] "+&r"(ct), [g] "+&r"(coverage),
                   [n] "+&r"(n16), [walk] "=&r"(walk), [t0] "=&r"(t0), [t1] "=&r"(t1)
                 : [pal] "r"(pal), [indices] "r"(indices), [one] "r"(one), [pairs] "r"(pairs)
                 : "memory");
}

GM_ANIM_IRAM void band(uint16_t *dst, int y0, int rows, int w, uint32_t, const uint8_t *) {
    for (int r = 0; r < rows; ++r) {
        const int y = y0 + r;
        const int rt = rowTerm[y];
        const int16_t *ct = ctPh + (y & 7) * ctStride;
        const uint8_t *coverage = buf + y * bufStride;
        uint16_t *out = dst + static_cast<size_t>(r) * w;
        harmonographRowAsm(out, ct, coverage, palette, scratch, ones, rt, top, w >> 4);
        // Scalar tail for 466/233 and other widths. Never read padded
        // columns or depend on another row being in this band call.
        for (int x = w & ~15; x < w; ++x) {
            int i = 90 + ((ct[x] + rt * 32) >> 10);
            i += ((top - i) * coverage[x]) >> 8;
            out[x] = palette[i];
        }
    }
}
#else
void band(uint16_t *dst, int y0, int rows, int w, uint32_t tMs, const uint8_t *p) {
    bandRef(dst, y0, rows, w, tMs, p);
}
#endif

void release() {
    releaseTable(ctPh, 8 * static_cast<size_t>(ctStride) * sizeof(int16_t));
    releaseTable(rowTerm, static_cast<size_t>(allocH) * sizeof(int16_t));
    releaseTable(palette, 256 * sizeof(uint16_t));
    releaseTable(scratch, 16 * sizeof(uint16_t));
    releaseTable(ones, 8 * sizeof(uint16_t));
    releaseTable(colTerm, static_cast<size_t>(allocW) * sizeof(int16_t));
    releaseTable(tail, NS);
    releaseTable(posX, NS * sizeof(int16_t));
    releaseTable(posY, NS * sizeof(int16_t));
    releaseTable(order, NS * sizeof(uint16_t));
    releaseTable(rowCur, static_cast<size_t>(allocH) * sizeof(uint16_t));
    releaseTable(dith, 64 * sizeof(int16_t));
    releaseTable(stamp, STAMP_BYTES);
    releaseTable(stampExt, EXT_BYTES);
    releaseTable(stamp16Storage, STAMP16_BYTES);
    releaseTable(bufStorage, static_cast<size_t>(bufStride) * allocH + BUF_SLACK);
#if defined(GM_TOUCH_PROBE) && defined(ESP_PLATFORM)
    releaseTable(chkStorage, static_cast<size_t>(bufStride) * allocH + BUF_SLACK);
    chk = nullptr;
#endif
    buf = nullptr;
    stamp16 = nullptr;
    sl = nullptr;
    allocW = allocH = ctStride = bufStride = 0;
    lastThemeGen = 0xFFFFFFFF;
    lastTailKey = lastBright = -1;
    top = 0;
}

} // namespace

extern const BgAnimation bg_anim_harmonograph;
const BgAnimation bg_anim_harmonograph = {
    "harmonograph",
    "Harmonograph",
    {{"speed", "Speed", 50},
     {"size", "Figure size", 68},
     {"glow", "Thread glow", 60},
     {"bright", "Brightness", 60},
     {"lobes", "Lobe count", 50},
     {"turn", "Turn rate", 50},
     {"trail", "Trail length", 50},
     {"vign", "Vignette", 50}},
    init,
    frame,
    band,
    release,
    bandRef,
};

#endif // GAGGIMATE_SIM
