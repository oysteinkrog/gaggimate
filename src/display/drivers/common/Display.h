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
    virtual uint16_t *directFrameBuffer() { return nullptr; }
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
