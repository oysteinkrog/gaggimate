#pragma once

#include <stdint.h>

class Display {
  public:
    virtual ~Display() = default;

    Display() : _rotation(0) {};
    virtual void pushColors(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t *data) = 0;
    virtual uint16_t width() = 0;
    virtual uint16_t height() = 0;
    virtual uint8_t getPoint(int16_t *x, int16_t *y, uint8_t get_point) = 0;
    virtual bool supportsDirectMode() = 0;

    // The panel's own framebuffer, when it has one the caller may write to
    // directly, or nullptr when it does not. A CPU write to PSRAM costs twice a
    // read on this part -- the 32-byte write-allocate line is fetched before it
    // is overwritten -- so pushColors moves two frames of bus traffic to deliver
    // one: 24 MB/s measured, against 48 for the same bytes over GDMA, which
    // never passes through the cache. A caller that can drive its own DMA wants
    // the destination address, not a copy routine.
    //
    // `index` selects which of frameBufferCount() buffers to write. Index 0 is
    // whatever the panel scans by default; a panel with two lets the caller
    // write the one that is NOT being scanned and then flip, which is the only
    // way to make tearing structurally impossible rather than merely unlikely.
    virtual uint16_t *directFrameBuffer(int index = 0) { return nullptr; }
    // How many framebuffers directFrameBuffer() can hand out. 0 means the panel
    // has none the caller may write; 1 means writes race the scan-out.
    virtual int frameBufferCount() { return 0; }
    // Make `index` the buffer the panel scans from. The switch happens when the
    // scan-out DMA reaches the end of the current buffer, so it lands on a
    // frame boundary and never mid-picture.
    //
    // dirtyY0/dirtyY1 bound the rows the CPU wrote *through the cache* and that
    // therefore still need a writeback. A DMA writer has already put its bytes
    // in memory and passes an empty range, which is not an optimisation but the
    // truth: there is nothing dirty to write back, and asking for 480 rows of
    // it every frame would be pure cost.
    virtual void presentFrameBuffer(int index, int dirtyY0, int dirtyY1) {}
    // Serialise a direct writer against this panel's own pushColors, which ends
    // with a cache writeback over the region it touched.
    virtual void lockFrameBuffer() {}
    virtual void unlockFrameBuffer() {}
    // The gate lockFrameBuffer() takes, as a raw FreeRTOS handle, or nullptr on
    // a panel that has none. Handed out raw rather than wrapped in an
    // unlockFrameBufferFromISR() because the only caller is a DMA completion
    // interrupt that may run with the flash cache disabled: a virtual call
    // would land in flash, while xSemaphoreGiveFromISR is in IRAM.
    virtual void *frameBufferGate() { return nullptr; }
    // Tell the panel that someone is writing its framebuffer behind the cache,
    // so pushColors can stop trusting its own cached view of that memory. See
    // the implementation for what goes wrong without it.
    virtual void setDirectWriter(bool) {}

  protected:
    uint8_t _rotation;
};
