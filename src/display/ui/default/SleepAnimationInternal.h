#ifndef SLEEPANIMATION_INTERNAL_H
#define SLEEPANIMATION_INTERNAL_H
// What SleepAnimation.cpp shares with the bench and probe translation units
// next to it (SleepAnimationBench.cpp, SleepAnimationBlendProbe.cpp,
// SleepAnimationKBlob.cpp, SleepAnimationTouchProbe.cpp): the constants the
// band loop and the overlay run tables are built on, the scale565 inline the
// blend probe runs per pixel, and the one counter the band-DMA completion
// interrupt writes. Nothing else includes this.
#include <stdint.h>

// Rows rendered/pushed per chunk, and how many of those chunks are in flight.
// Push cost is per byte rather than per call, so band height does not change
// what a frame costs to push -- but it does set the handoff count, and 8 rows
// (60 cross-core semaphore round trips per frame instead of 30) cost ~1.3 ms
// and dropped plasma 43.0 -> 40.6 fps.
//
// 8 rows across two slots is chosen against internal SRAM, which is the
// binding constraint: internal SRAM is what WiFi/BLE/TLS draw from at runtime,
// and the web server needs a contiguous 2,872 B of it per send round. 8 x 2 is
// 15,360 B; the handoff cost above is the price of the row count, and the slot
// count is argued separately at NUM_SLOTS in the header.
//
// Note on the fps figure quoted above: it comes from a 16-vs-8 bench, which is
// where the 30-vs-60 handoff counts come from. The 12-vs-8 step this constant
// actually took has never been benched directly; scaling the measured number
// linearly puts it nearer 0.9 ms than 1.3 ms, so 40.6 fps is a conservative
// floor rather than a measurement of the current configuration.
// BAND_H=4 was tried on-device 2026-08-30 and reverted the same day, on two
// measurements. bdma_mean scaled exactly with the transfer (91 us at 1,920 B,
// 179 us at 3,840 B): the band DMA is bandwidth-bound at ~21.5 MB/s, so a
// bigger band buys no per-transfer overhead back. And the doubled internal
// slots (2 x 3,840 B, +3,840 B over BAND_H=2) tipped the DMA-capable pool
// with BLE up: NetWatchdog showed dma free 1.5-5 KB with min=256 B and the
// web server started timing out, which is the same WiFi-pool cliff the
// bounce-depth and 15,360 B experiments hit. The ~6.5 ms/frame of per-band
// loop overhead at 240 bands/frame is real but not worth that pool.
constexpr int BAND_H = 2;

// How many runs a single row's list can hold before emitRun starts merging.
// 24 is well past what the standby widgets produce; a row through the clock and
// both icons emits about eight.
constexpr int RUNS_PER_ROW = 24;

// The scrim factor is stored as 32nds of full brightness, so 32 means "leave
// this cell alone". Two smoothing passes leave a wide skirt of cells whose dim
// rounds away to nothing, and those are dropped from the runs entirely.
constexpr int SCRIM_INV_NONE = 32;

// Scrim grid resolution: 1 << 2 = one cell per 4x4 panel pixels.
constexpr int SCRIM_SHIFT = 2;

// Scale an RGB565 toward black. inv is 0..32, i.e. 32 keeps the pixel and 0
// blacks it out.
//
// Two multiplies rather than blend565's six, because red and blue share one
// lane. That packing is what the comment above blend565 warns is broken -- and
// it is, for an 8-bit alpha. With five bits it is exact: blue's product tops
// out at 31 * 32 = 992, ten bits, while red's starts at bit 11, so the two
// never touch. The scrim is a blurred halo, so the step from 256 levels to 32
// is not visible in it; the antialiasing on the glyph edges, where it would be,
// still goes through blend565.
__attribute__((always_inline)) inline uint16_t scale565(uint16_t c, uint32_t inv) {
    // The two products are left where the multiply puts them and the masks do
    // the shifting, so one shift serves both lanes instead of one each.
    // (c & 0xF81F) * inv leaves blue in bits 0..9 and red in bits 11..20 -- inv
    // is five bits, so they cannot reach each other -- and 0x1F03E0 picks the
    // top five of each. Green, five bits further up, comes out under 0xFC00.
    const uint32_t rb = (c & 0xF81Fu) * inv;
    const uint32_t g = (c & 0x07E0u) * inv;
    return static_cast<uint16_t>(((rb & 0x1F03E0u) | (g & 0xFC00u)) >> 5);
}

// Two neighbouring pixels at once. The band is 4-byte aligned and a scrim cell
// starts on an even pixel, so the pair is one aligned load and one aligned
// store where four half-word accesses stood before.
__attribute__((always_inline)) inline uint32_t scale565x2(uint32_t w, uint32_t inv) {
    return scale565(static_cast<uint16_t>(w), inv) | (static_cast<uint32_t>(scale565(w >> 16, inv)) << 16);
}

#ifdef GM_ANIM_BENCH
// Band transfers retired by the GDMA completion interrupt (SleepAnimation.cpp,
// sleepAnimBandRetire). Read by the drain loop in stop() and, in the bench
// build only, by benchDmaCompleted(); everywhere else the counter is static.
extern volatile uint32_t g_sleepAnimDmaDone;
#endif

#endif // SLEEPANIMATION_INTERNAL_H
