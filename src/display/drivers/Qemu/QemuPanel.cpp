#ifdef GAGGIMATE_QEMU

#include "QemuPanel.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

static constexpr const char *LOG_TAG = "QemuPanel";

// Espressif QEMU synthetic RGB framebuffer device (hw/display/esp_rgb.c).
static constexpr uintptr_t QEMU_RGB_VRAM_BASE = 0x20000000u;
static constexpr uintptr_t QEMU_RGB_REG_BASE = 0x21000000u;

static constexpr uintptr_t REG_VERSION = QEMU_RGB_REG_BASE + 0x00;
static constexpr uintptr_t REG_WIN_SIZE = QEMU_RGB_REG_BASE + 0x04;
static constexpr uintptr_t REG_UPDATE_FROM = QEMU_RGB_REG_BASE + 0x08;
static constexpr uintptr_t REG_UPDATE_TO = QEMU_RGB_REG_BASE + 0x0c;
static constexpr uintptr_t REG_UPDATE_CONTENT = QEMU_RGB_REG_BASE + 0x10;
static constexpr uintptr_t REG_UPDATE_STATUS = QEMU_RGB_REG_BASE + 0x14;
static constexpr uintptr_t REG_BPP_VALUE = QEMU_RGB_REG_BASE + 0x18;

static constexpr uint32_t UPDATE_STATUS_ENA = 1u << 0;

// The device rejects any access that is not 32 bits wide, so every register
// touch goes through these two.
static inline void regWrite(uintptr_t addr, uint32_t value) { *reinterpret_cast<volatile uint32_t *>(addr) = value; }
static inline uint32_t regRead(uintptr_t addr) { return *reinterpret_cast<volatile uint32_t *>(addr); }

uint16_t *QemuPanel::framebuffer() { return reinterpret_cast<uint16_t *>(QEMU_RGB_VRAM_BASE); }

bool QemuPanel::begin() {
    if (started) {
        return true;
    }

    const uint32_t version = regRead(REG_VERSION);
    ESP_LOGI(LOG_TAG, "QEMU RGB device version %u.%u", (unsigned)((version >> 16) & 0xffff), (unsigned)(version & 0xffff));

    // 16 bpp gives PIXMAN_r5g6b5 on the host side, which is byte-for-byte what
    // LVGL produces here (LV_COLOR_DEPTH 16, LV_COLOR_16_SWAP 0) on a
    // little-endian guest running on a little-endian host.
    regWrite(REG_BPP_VALUE, 16);
    regWrite(REG_WIN_SIZE, (static_cast<uint32_t>(PANEL_WIDTH) << 16) | PANEL_HEIGHT);

    std::memset(framebuffer(), 0, static_cast<size_t>(PANEL_WIDTH) * PANEL_HEIGHT * sizeof(uint16_t));

    // Priority 2: below the LVGL/UI task, above idle. Pinned to core 1 so the
    // register writes never contend with the logic task on core 0.
    BaseType_t ok = xTaskCreatePinnedToCore(scanoutTask, "QemuPanel::scanout", 2048, this, 2, nullptr, 1);
    if (ok != pdPASS) {
        ESP_LOGE(LOG_TAG, "Failed to start scan-out task");
        return false;
    }

    started = true;
    return true;
}

// x_end / y_end are exclusive, matching esp_lcd_panel_draw_bitmap() and what
// LV_Helper's disp_flush passes (area->x2 + 1, area->y2 + 1).
void QemuPanel::pushColors(uint16_t x, uint16_t y, uint16_t x_end, uint16_t y_end, uint16_t *data) {
    if (data == nullptr || x_end <= x || y_end <= y) {
        return;
    }
    if (x >= PANEL_WIDTH || y >= PANEL_HEIGHT) {
        return;
    }
    if (x_end > PANEL_WIDTH) {
        x_end = PANEL_WIDTH;
    }
    if (y_end > PANEL_HEIGHT) {
        y_end = PANEL_HEIGHT;
    }

    const size_t rowBytes = static_cast<size_t>(x_end - x) * sizeof(uint16_t);
    uint16_t *dst = framebuffer() + static_cast<size_t>(y) * PANEL_WIDTH + x;
    const uint16_t *src = data;
    for (uint16_t row = y; row < y_end; row++) {
        std::memcpy(dst, src, rowBytes);
        dst += PANEL_WIDTH;
        src += (x_end - x);
    }
}

void QemuPanel::present() {
    // A previous update that QEMU has not consumed yet: leave it alone, the
    // next tick will pick up whatever the framebuffer holds by then.
    if ((regRead(REG_UPDATE_STATUS) & UPDATE_STATUS_ENA) != 0) {
        return;
    }
    regWrite(REG_UPDATE_FROM, 0);
    regWrite(REG_UPDATE_TO, (static_cast<uint32_t>(PANEL_WIDTH) << 16) | PANEL_HEIGHT);
    // A CPU physical address, despite the device reading it through a private
    // AddressSpace rooted at the VRAM MemoryRegion. The SoC maps that same
    // region into system memory before the device is realized, so QEMU's
    // render_memory_region() offsets the root by its address within sys_mem and
    // the private space ends up valid at 0x20000000 rather than at 0. The same
    // holds for the device's other source, internal DRAM at 0x3FC80000, so
    // "pass the address the guest itself uses" is the rule in both cases.
    // Passing a VRAM-relative 0 fails address_space_access_valid() and the
    // device drops the frame with "Invalid color content address or length".
    regWrite(REG_UPDATE_CONTENT, QEMU_RGB_VRAM_BASE);
    regWrite(REG_UPDATE_STATUS, UPDATE_STATUS_ENA);
}

void QemuPanel::scanoutTask(void *arg) {
    (void)arg;
    // QEMU refreshes its console at roughly 30 Hz; asking for more just burns
    // guest cycles that the profiler would then attribute to us.
    const TickType_t period = pdMS_TO_TICKS(33) > 0 ? pdMS_TO_TICKS(33) : 1;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        present();
        vTaskDelayUntil(&last, period);
    }
}

uint8_t QemuPanel::getPoint(int16_t *x, int16_t *y, uint8_t get_point) {
    (void)x;
    (void)y;
    (void)get_point;
    // No touch controller is emulated. Injecting synthetic touches needs a host
    // side channel, which is a separate piece of work.
    return 0;
}

#endif // GAGGIMATE_QEMU
