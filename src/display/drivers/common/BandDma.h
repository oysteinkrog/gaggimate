#pragma once

#ifndef GAGGIMATE_SIM

#include <esp_err.h>
#include <esp_private/gdma.h>
#include <esp_private/gdma_link.h>
#include <freertos/FreeRTOS.h>
#include <stddef.h>
#include <stdint.h>

// A memory-to-memory GDMA engine for one fixed transfer shape, with every
// descriptor allocated up front.
//
// IDF's own esp_async_memcpy cannot carry this rate. Read
// async_memcpy_gdma.c:mcp_gdma_memcpy: each call deletes both of its GDMA link
// lists, frees the stash buffer, allocates two fresh lists and re-splits the
// destination -- four frees and four aligned allocations per transfer. Measured
// on this part that is ~240 us of caller time per submission, so the 40 band
// pushes in a frame cost ~10 ms of render-task time: a third of the frame
// budget, spent on bookkeeping for work the DMA engine is supposed to be doing
// instead of the CPU.
//
// Here the link lists are built once per slot at install and only re-mounted
// per transfer, which is a handful of stores into memory that is already
// resident. Nothing is allocated, nothing is freed, and no cache maintenance is
// done on the caller's behalf -- the caller owns coherency, because it is
// already managing a framebuffer the LCD scans out continuously and knows far
// better than a general-purpose driver when a writeback is needed.
class BandDma {
  public:
    // Runs in the GDMA completion interrupt. Return true if it woke a
    // higher-priority task, exactly like a FreeRTOS ...FromISR caller would.
    using DoneFn = bool (*)(void *arg);

    // Descriptor pairs to preallocate. One transfer is running and the rest are
    // queued behind it; the animation pipeline only ever has NUM_SLOTS bands
    // outstanding, so four is slack rather than a limit.
    static constexpr int SLOTS = 4;

    // Largest row-group count submitRows() below will mount in one call.
    // install()'s own link-list sizing (maxTransferBytes / 2048 + 2) already
    // has enough spare items to cover BAND_H/2 single-row groups up to
    // BAND_H=64 -- the arithmetic is in install()'s comment -- so this is a
    // ceiling submitRows() enforces on its caller, not a size this class has
    // to grow its own lists for.
    static constexpr int MAX_ROW_GROUPS = 8;

    BandDma() = default;
    ~BandDma();
    BandDma(const BandDma &) = delete;
    BandDma &operator=(const BandDma &) = delete;

    // Allocates the channel pair and the descriptors. Call it from the core
    // that should service the completion interrupt: gdma_register_rx_event_
    // callbacks installs the handler on the calling core.
    //
    // maxTransferBytes sizes the descriptor lists; a submit larger than this is
    // rejected rather than silently truncated.
    esp_err_t install(size_t maxTransferBytes, size_t burstBytes, DoneFn onDone);
    void uninstall();
    bool ready() const { return _rxChan != nullptr; }

    // Queues one transfer and starts it if the engine is idle. `slot` picks
    // which preallocated descriptor pair to use; the caller must not reuse a
    // slot until that slot's completion callback has run. `arg` is handed back
    // to the completion callback untouched.
    //
    // Returns ESP_OK once the transfer is queued. It does NOT wait.
    esp_err_t submit(int slot, void *dst, const void *src, size_t bytes, void *arg);

    // Queues a STRIPED transfer: `n` disjoint row-groups within one band,
    // each `groupBytes` long, read from `srcBase + rowOffsets[i]` and written
    // to `dstBase + rowOffsets[i]` -- the same offset on both sides, because
    // the caller's SRAM band buffer and the destination framebuffer band
    // region share one row layout, only the base pointer differs.
    //
    // This exists for interlaced pushes, where only some rows of the band
    // hold this frame's content and the rest is a stale slot from NUM_SLOTS-1
    // bands back. submit() above mounts one contiguous run, so it would DMA
    // that stale content over destination rows the LAST frame correctly
    // wrote. Mounting `n` separate buffers instead means the gaps between
    // them never get a descriptor at all -- gdma_link_mount_buffers puts each
    // buffer in the config array on its own list item(s) rather than
    // coalescing them (see its own doc comment) -- so the skipped rows are
    // never read from srcBase and never written to dstBase, not merely
    // "skipped" by a policy this engine has to remember to apply.
    //
    // Same semantics as submit() otherwise: queues and returns without
    // waiting, `slot` must be free, `arg` reaches the completion callback
    // untouched. Fails with ESP_ERR_INVALID_SIZE if n is 0, exceeds
    // MAX_ROW_GROUPS, or the total would exceed the size install() sized the
    // descriptor lists for -- the caller already has a CPU-push fallback for
    // submit() failures and this reuses it rather than asserting.
    esp_err_t submitRows(int slot, void *dstBase, const void *srcBase, const uint32_t *rowOffsets, int n,
                         size_t groupBytes, void *arg);

    // Destination alignment this channel requires, valid after install(). The
    // caller needs it to decide whether its buffers can be used at all.
    size_t externalAlignment() const { return _extAlign; }
    size_t internalAlignment() const { return _intAlign; }

    // Per-transfer duration, hardware start to RX EOF, queue wait excluded.
    //
    // This exists to answer one question about the scan-out fault: when the
    // BLE scanner's window transition occupies the MSPI bus for ~900 us, does
    // a GDMA engine's PSRAM access queue behind it the way the refill ISR's
    // CPU memcpy does, or does it ride through? These transfers run ~440
    // times a second while the animation renders, so the bus events at
    // ~0.5/s cannot miss them for long. A clean band is ~70-150 us; if the
    // over-512 counter climbs at the same rate the refill's resyncs do, a
    // DMA-driven refill would inherit the stall rather than dodge it, and
    // the idea is dead. If it stays at zero, the stall is specific to CPU
    // cache traffic and moving the refill onto GDMA is the road to zero.
    //
    // Measured, 180 s at 43.4 Hz with BLE scanning at 80 ms/2000 ms: over-512
    // 7.05/s against resyncs 0.49/s, max transfer 2135 us. GDMA queues behind
    // the same MSPI stall the CPU does, only with a fatter tail. The idea is
    // dead; the counters stay because they are the cheapest live view of bus
    // contention this firmware has.
    void xferStats(uint32_t *count, uint32_t *sumUs, uint32_t *maxUs, uint32_t *over256, uint32_t *over512) const;
    void xferStatsReset();

  private:
    struct Pending {
        int slot;
        void *arg;
    };

    static bool rxEofTrampoline(gdma_channel_handle_t chan, gdma_event_data_t *ev, void *user);
    bool onRxEof();
    void startSlot(int slot); // must not be called while another transfer runs
    // Shared tail of submit()/submitRows(): both have already mounted their
    // buffers onto _txLink[slot]/_rxLink[slot] by the time they call this: it
    // only owns the pending-ring bookkeeping and kicking the engine if idle.
    esp_err_t queueSlot(int slot, void *arg);

    gdma_channel_handle_t _txChan = nullptr;
    gdma_channel_handle_t _rxChan = nullptr;
    gdma_link_list_handle_t _txLink[SLOTS] = {};
    gdma_link_list_handle_t _rxLink[SLOTS] = {};

    DoneFn _onDone = nullptr;
    size_t _intAlign = 4;
    size_t _extAlign = 4;
    size_t _maxBytes = 0;

    // Guards the ring below against the completion interrupt. A spinlock rather
    // than a semaphore because both sides are short and one of them is an ISR.
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    Pending _queue[SLOTS] = {};
    volatile uint8_t _head = 0; // next to start
    volatile uint8_t _tail = 0; // next free
    volatile bool _running = false;
    Pending _current = {};

    // Transfer-duration stats. Written from startSlot (task or ISR) and the
    // EOF ISR, read from the web task; 32-bit aligned loads and stores are
    // atomic on this core, and the readers only ever see a value one event
    // stale, which a rate measurement does not care about. The sum is in
    // microseconds and wraps after ~71 minutes; the reader takes deltas.
    volatile int64_t _xferStartUs = 0;
    volatile uint32_t _xferCount = 0;
    volatile uint32_t _xferSumUs = 0;
    volatile uint32_t _xferMaxUs = 0;
    volatile uint32_t _xferOver256 = 0;
    volatile uint32_t _xferOver512 = 0;
};

#endif // GAGGIMATE_SIM
