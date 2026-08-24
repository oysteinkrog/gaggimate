#ifndef QEMUPANEL_H
#define QEMUPANEL_H

#ifdef GAGGIMATE_QEMU

#include <display/drivers/common/Display.h>

#include <cstdint>

// Panel implementation for Espressif's QEMU fork, which provides a synthetic
// RGB framebuffer device (hw/display/esp_rgb.c) instead of a real LCD
// controller. Nothing about this file describes real hardware; it exists so the
// unmodified display firmware can be run under instruction-accurate emulation
// for debugging and profiling.
//
// The device has two windows in the guest address space:
//
//   0x20000000  VRAM, ESP_RGB_MAX_VRAM_SIZE (800*600*4) bytes of plain RAM
//   0x21000000  control registers, 32-bit accesses only
//
// A blit is requested by writing a source rectangle and then setting the ENA
// bit of UPDATE_STATUS. QEMU performs the copy on its next gfx_update tick
// (~30 Hz) and clears ENA, so a rectangle written while a previous one is still
// pending is silently dropped.
//
// This driver therefore does not blit per LVGL flush. It keeps a full
// 480x480 RGB565 framebuffer at VRAM offset 0, has pushColors() copy each dirty
// rectangle into it, and lets a separate task issue one whole-frame update at
// panel rate. That models the real RGB panel's continuous scan-out: no
// rectangle can be lost, and the LVGL task never waits on the host.
class QemuPanel : public Display {
  public:
    static constexpr uint16_t PANEL_WIDTH = 480;
    static constexpr uint16_t PANEL_HEIGHT = 480;

    bool begin();

    void pushColors(uint16_t x, uint16_t y, uint16_t x_end, uint16_t y_end, uint16_t *data) override;
    uint16_t width() override { return PANEL_WIDTH; }
    uint16_t height() override { return PANEL_HEIGHT; }
    uint8_t getPoint(int16_t *x, int16_t *y, uint8_t get_point) override;

    // LVGL keeps its own draw buffer in PSRAM and we copy out of it. Direct mode
    // would hand LVGL the framebuffer itself, which is legal here, but it also
    // makes LVGL responsible for the partial-render bookkeeping that the RGB
    // driver needs on real hardware. Copying keeps the QEMU path closer to the
    // Waveshare/LilyGo behaviour the rest of the UI is written against.
    bool supportsDirectMode() override { return false; }
    // Still exposed, because the standby animation writes the panel directly
    // rather than through LVGL.
    uint16_t *directFrameBuffer() override { return framebuffer(); }

    static uint16_t *framebuffer();

    // Publishes the current framebuffer contents to the QEMU console. Called
    // from the scan-out task; safe to call from anywhere.
    static void present();

  private:
    static void scanoutTask(void *arg);
    bool started = false;
};

#endif // GAGGIMATE_QEMU
#endif // QEMUPANEL_H
