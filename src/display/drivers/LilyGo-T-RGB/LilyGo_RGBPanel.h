/**
 * @file      LilyGo_RGBPanel.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  Shenzhen Xin Yuan Electronic Technology Co., Ltd
 * @date      2024-01-22
 *
 */

#pragma once

#include <Arduino.h>

#ifndef BOARD_HAS_PSRAM
#error "Please turn on PSRAM to OPI !"
#endif

#include <SD_MMC.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_panel_vendor.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <display/drivers/common/Display.h>
#include <display/drivers/common/ext.h>

enum LilyGo_RGBPanel_Type {
    LILYGO_T_RGB_UNKNOWN,
    LILYGO_T_RGB_2_1_INCHES,
    LILYGO_T_RGB_2_8_INCHES,
};

enum LilyGo_RGBPanel_TouchType {
    LILYGO_T_RGB_TOUCH_UNKNOWN,
    LILYGO_T_RGB_TOUCH_FT3267,
    LILYGO_T_RGB_TOUCH_CST820,
    LILYGO_T_RGB_TOUCH_GT911,
};

enum LilyGo_RGBPanel_Color_Order {
    LILYGO_T_RGB_ORDER_RGB,
    LILYGO_T_RGB_ORDER_BGR,
};

enum LilyGo_RGBPanel_Wakeup_Method {
    LILYGO_T_RGB_WAKEUP_FORM_TOUCH,
    LILYGO_T_RGB_WAKEUP_FORM_BUTTON,
    LILYGO_T_RGB_WAKEUP_FORM_TIMER,
};

class LilyGo_RGBPanel : public Display {

  public:
    LilyGo_RGBPanel();

    ~LilyGo_RGBPanel();

    bool begin(LilyGo_RGBPanel_Color_Order order = LILYGO_T_RGB_ORDER_RGB);

    void initExtension();

    bool installSD();

    void uninstallSD();

    void setBrightness(uint8_t level);

    uint8_t getBrightness() const;

    LilyGo_RGBPanel_Type getModel();

    const char *getTouchModelName();

    void enableTouchWakeup();
    void enableButtonWakeup();
    void enableTimerWakeup(uint64_t time_in_us);

    void sleep();

    void wakeup();

    uint16_t width();

    uint16_t height();

    uint8_t getPoint(int16_t *x_array, int16_t *y_array, uint8_t get_point = 1);

    bool isPressed();

    uint16_t getBattVoltage(void);

    void pushColors(uint16_t x, uint16_t y, uint16_t xEnd, uint16_t yEnd, uint16_t *data) override;
    // Stops RGB scan-out until reboot (frees the esp_lcd panel). Used during
    // display OTA to keep the shared flash/PSRAM bus free for flash writes.
    void stopPanel();

    bool supportsDirectMode() { return false; }

    uint16_t *directFrameBuffer(int index = 0) override;
    int frameBufferCount() override;
    void presentFrameBuffer(int index, int dirtyY0, int dirtyY1) override;
    void lockFrameBuffer() override;
    void unlockFrameBuffer() override;
    void *frameBufferGate() override;
    void setDirectWriter(bool active) override;

    // The two ST7701S registers that set where inversion flicker nulls out:
    // VCOMS (BK1 0xB1) and INVSET's first byte (BK0 0xC2).
    //
    // Both are writable at runtime rather than only at init, because the fault
    // they address only shows up under conditions a build-and-flash loop cannot
    // hold still. VCOM's null moves with the temperature of the glass, so a
    // cold panel sits off it and shimmers on large mid-tone areas until it
    // warms; finding the value that is right cold means adjusting it while it
    // IS cold, which is a few seconds of sweeping, not a few minutes of
    // reflashing and waiting for the panel to cool down again.
    //
    // setVcom backs the panelVcom setting and is applied from DefaultUI on
    // every change, including the UI task's first pass after boot. setInversion
    // is not persisted and reverts to the init table on the next boot.
    //
    // Both hold _busLock for the whole bank-select sequence, so the touch
    // task's poll cannot land between two of its bit-banged transfers. Each
    // 9-bit transfer is about 29 read-modify-writes of the XL9555 over I2C,
    // so one call holds the bus for an estimated 100 ms and the touch task
    // misses its polls for that long (not measured).
    //
    // The control interface is the 9-bit SPI extender, entirely separate from
    // the RGB data path, so both are safe to write while scan-out is running.
    void setVcom(uint8_t vcoms);
    void setInversion(uint8_t invset0);

  private:
    // Two, so a frame can be composed off-screen and shown whole. Named rather
    // than spelled 2 in four places because it also sizes the panel config.
    static constexpr int FB_COUNT = 2;

    void resolveFrameBuffers();

    void writeData(const uint8_t *data, int len);

    void writeCommand(const uint8_t cmd);

    // False when esp_lcd cannot create or initialise the RGB panel.
    bool initBUS();

    // Marks the panel uninitialised and drops the cached framebuffer
    // pointers. Called on stopPanel(), sleep(), a failed begin() and in the
    // destructor.
    void clearPanelState();

    bool initTouch();

    uint8_t _brightness;

    esp_lcd_panel_handle_t _panelDrv;

    TouchDrvInterface *_touchDrv;

    LilyGo_RGBPanel_Color_Order _order;

    // True only between a successful begin() and stopPanel() or sleep().
    // setVcom and setInversion write only while it is set. Read and written
    // under _busLock.
    bool _has_init;
    bool _extension_initialized = false;

    LilyGo_RGBPanel_Wakeup_Method _wakeupMethod;

    uint64_t _sleepTimeUs;

    LilyGo_RGBPanel_TouchType _touchType;

    // Cached result of directFrameBuffer()'s one-time resolve, and the gate
    // that serialises a direct writer against pushColors. _fbResolved is
    // separate from a null entry because a failed resolve must not be retried
    // on every frame.
    //
    // Both of the panel's framebuffers. The animation writes whichever one
    // the scan-out is not reading and flips at the end of the frame, so no
    // pixel is ever written while it is being displayed.
    uint16_t *_fbDirect[FB_COUNT] = {};
    bool _fbResolved = false;
    SemaphoreHandle_t _fbGate = nullptr;
    // Serialises every user of the I2C bus behind this driver: the touch
    // controller reads (the touch task) and the XL9555 expander, through
    // which the ST7701 register writes are bit-banged (setVcom,
    // setInversion, on the UI task). Wire serialises single transactions,
    // but its receive buffer is shared and a register read is two
    // transactions, so two tasks reading through it at once hand each other
    // the wrong bytes: a GT911 poll during a VCOM write read the expander's
    // port byte back into the expander with the touch reset line in the
    // wrong state, and the controller NACKed every poll until the next
    // reboot (2026-09-08). Recursive: writeCommand nests under setVcom, and
    // the touch driver's expander callbacks nest under getPoint.
    SemaphoreHandle_t _busLock = nullptr;
    struct BusGuard {
        SemaphoreHandle_t m;
        explicit BusGuard(SemaphoreHandle_t mm) : m(mm) {
            if (m != nullptr) {
                xSemaphoreTakeRecursive(m, portMAX_DELAY);
            }
        }
        ~BusGuard() {
            if (m != nullptr) {
                xSemaphoreGiveRecursive(m);
            }
        }
    };
    int _fbCount = 0;
    // Which buffer the panel is scanning. pushColors invalidates against
    // this one, because that is where esp_lcd's copy path will land.
    int _fbCurrent = 0;
    bool _directWriter = false;

    ExtensionIOXL9555::ExtensionGPIO cs = ExtensionIOXL9555::IO3;
    ExtensionIOXL9555::ExtensionGPIO mosi = ExtensionIOXL9555::IO4;
    ExtensionIOXL9555::ExtensionGPIO sclk = ExtensionIOXL9555::IO5;
    ExtensionIOXL9555::ExtensionGPIO reset = ExtensionIOXL9555::IO6;
    ExtensionIOXL9555::ExtensionGPIO power_enable = ExtensionIOXL9555::IO2;
    ExtensionIOXL9555::ExtensionGPIO sdmmc_cs = ExtensionIOXL9555::IO7;
    ExtensionIOXL9555::ExtensionGPIO tp_reset = ExtensionIOXL9555::IO1;
};
