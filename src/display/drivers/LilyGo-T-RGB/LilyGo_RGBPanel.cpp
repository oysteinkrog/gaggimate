/**
 * @file      LilyGo_RGBPanel.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  Shenzhen Xin Yuan Electronic Technology Co.,
 * Ltd
 * @date      2024-01-22
 *
 */
#include "LilyGo_RGBPanel.h"
#include "utilities.h"
#include <display/drivers/common/PanelClock.h>
#include <display/drivers/common/RGBPanelInit.h>
#include <esp_cache.h>                    // esp_cache_msync, for the direct-writer path in pushColors
#include <esp_memory_utils.h>              // esp_ptr_external_ram
#include <esp_private/esp_cache_private.h> // esp_cache_get_alignment

// Panel timing porches, overridable from build flags (see [env:display] in
// platformio.ini). Defaults are the stock LilyGo values.
#ifndef GM_LCD_HSYNC_PW
#define GM_LCD_HSYNC_PW 1
#define GM_LCD_HSYNC_BP 30
#define GM_LCD_HSYNC_FP 50
#define GM_LCD_VSYNC_PW 1
#define GM_LCD_VSYNC_BP 30
#define GM_LCD_VSYNC_FP 20
#endif

static void TouchDrvDigitalWrite(uint32_t gpio, uint8_t level);
static int TouchDrvDigitalRead(uint32_t gpio);
static void TouchDrvPinMode(uint32_t gpio, uint8_t mode);
static ExtensionIOXL9555 extension;
static const lcd_init_cmd_t *_init_cmd = nullptr;

LilyGo_RGBPanel::LilyGo_RGBPanel(/* args */)
    : _brightness(0), _panelDrv(nullptr), _touchDrv(nullptr), _order(LILYGO_T_RGB_ORDER_RGB), _has_init(false),
      _wakeupMethod(LILYGO_T_RGB_WAKEUP_FORM_BUTTON), _sleepTimeUs(0), _touchType(LILYGO_T_RGB_TOUCH_UNKNOWN) {}

LilyGo_RGBPanel::~LilyGo_RGBPanel() {
    if (_panelDrv) {
        panelclock::detach();
        esp_lcd_panel_del(_panelDrv);
        _panelDrv = nullptr;
    }
    if (_touchDrv) {
        delete _touchDrv;
        _touchDrv = nullptr;
    }
}

bool LilyGo_RGBPanel::begin(LilyGo_RGBPanel_Color_Order order) {
    if (_panelDrv) {
        return true;
    }

    _order = order;

    pinMode(BOARD_TFT_BL, OUTPUT);
    digitalWrite(BOARD_TFT_BL, LOW);

    initExtension();

    if (!initTouch()) {
        Serial.println(F("Touch chip not found."));
    }

    initBUS();

    getModel();

    return true;
}

void LilyGo_RGBPanel::initExtension() {
    if (_extension_initialized) {
        return;
    }
    // Initialize the XL9555 expansion chip
    if (!extension.init(Wire, BOARD_I2C_SDA, BOARD_I2C_SCL)) {
        Serial.println(F("External GPIO expansion chip does not exist."));
        assert(false);
    }

    /**
     * * The power enable is connected to the XL9555 expansion chip GPIO.
     * * It must be turned on and can only be started when using a battery.
     */
    extension.pinMode(power_enable, OUTPUT);
    extension.digitalWrite(power_enable, HIGH);
    _extension_initialized = true;
}

bool LilyGo_RGBPanel::installSD() {
    initExtension();
    extension.pinMode(sdmmc_cs, OUTPUT);
    extension.digitalWrite(sdmmc_cs, HIGH);

    SD_MMC.setPins(BOARD_SDMMC_SCK, BOARD_SDMMC_CMD, BOARD_SDMMC_DAT);

    // maxOpenFiles 5 -> 10: shot history lives on SD, and the web server serves
    // many .slog files concurrently alongside live shot logging + index access;
    // the default of 5 exhausts and throws "too many open files". [GM-90]
    if (SD_MMC.begin("/sdcard", true, false, BOARD_MAX_SDMMC_FREQ, 10)) {
        uint8_t cardType = SD_MMC.cardType();
        if (cardType != CARD_NONE) {
            Serial.print(F("SD Card Type: "));
            if (cardType == CARD_MMC)
                Serial.println(F("MMC"));
            else if (cardType == CARD_SD)
                Serial.println(F("SDSC"));
            else if (cardType == CARD_SDHC)
                Serial.println(F("SDHC"));
            else
                Serial.println(F("UNKNOWN"));
            uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
            Serial.printf("SD Card Size: %lluMB\n", cardSize);
        }
        return true;
    }
    return false;
}

void LilyGo_RGBPanel::uninstallSD() {
    SD_MMC.end();
    extension.digitalWrite(sdmmc_cs, LOW);
    extension.pinMode(sdmmc_cs, INPUT);
}

void LilyGo_RGBPanel::setBrightness(uint8_t value) {
    static uint8_t steps = 16;

    if (_brightness == value) {
        return;
    }

    if (value > 16) {
        value = 16;
    }
    if (value == 0) {
        digitalWrite(BOARD_TFT_BL, 0);
        delay(3);
        _brightness = 0;
        return;
    }
    if (_brightness == 0) {
        digitalWrite(BOARD_TFT_BL, 1);
        _brightness = steps;
        delayMicroseconds(30);
    }
    int from = steps - _brightness;
    int to = steps - value;
    int num = (steps + to - from) % steps;
    for (int i = 0; i < num; i++) {
        digitalWrite(BOARD_TFT_BL, 0);
        digitalWrite(BOARD_TFT_BL, 1);
    }
    _brightness = value;
}

uint8_t LilyGo_RGBPanel::getBrightness() const { return _brightness; }

LilyGo_RGBPanel_Type LilyGo_RGBPanel::getModel() {
    if (_touchDrv) {
        const char *model = _touchDrv->getModelName();
        if (model == NULL)
            return LILYGO_T_RGB_UNKNOWN;
        if (strlen(model) == 0)
            return LILYGO_T_RGB_UNKNOWN;
        if (strcmp(model, "FT3267") == 0) {
            _touchType = LILYGO_T_RGB_TOUCH_FT3267;
            return LILYGO_T_RGB_2_1_INCHES;
        } else if (strcmp(model, "CST820") == 0) {
            _touchType = LILYGO_T_RGB_TOUCH_CST820;
            return LILYGO_T_RGB_2_1_INCHES;
        } else if (strcmp(model, "GT911") == 0) {
            _touchType = LILYGO_T_RGB_TOUCH_GT911;
            return LILYGO_T_RGB_2_8_INCHES;
        }
    }
    return LILYGO_T_RGB_UNKNOWN;
}

const char *LilyGo_RGBPanel::getTouchModelName() {
    if (_touchDrv) {
        return _touchDrv->getModelName();
    }
    return "UNKNOWN";
}

void LilyGo_RGBPanel::enableTouchWakeup() { _wakeupMethod = LILYGO_T_RGB_WAKEUP_FORM_TOUCH; }

void LilyGo_RGBPanel::enableButtonWakeup() { _wakeupMethod = LILYGO_T_RGB_WAKEUP_FORM_BUTTON; }

void LilyGo_RGBPanel::enableTimerWakeup(uint64_t time_in_us) {
    _wakeupMethod = LILYGO_T_RGB_WAKEUP_FORM_TIMER;
    _sleepTimeUs = time_in_us;
}

// The sleep method tested CST820 and GT911, and the FTxxxx series should also
// be usable.
void LilyGo_RGBPanel::sleep() {
    // turn off blacklight
    for (int i = _brightness; i >= 0; --i) {
        setBrightness(i);
        delay(30);
    }

    if (LILYGO_T_RGB_WAKEUP_FORM_TOUCH != _wakeupMethod) {
        if (_touchDrv) {
            if (getModel() == LILYGO_T_RGB_2_8_INCHES) {
                pinMode(BOARD_TOUCH_IRQ, OUTPUT);
                digitalWrite(BOARD_TOUCH_IRQ,
                             LOW); // Before touch to set sleep, it is necessary
                                   // to set INT to LOW
            }
            _touchDrv->sleep();
        }
    }

    switch (_wakeupMethod) {
    case LILYGO_T_RGB_WAKEUP_FORM_TOUCH: {
        int16_t x_array[1];
        int16_t y_array[1];
        uint8_t get_point = 1;
        pinMode(BOARD_TOUCH_IRQ, INPUT);
        // Wait for your finger to be lifted from the screen
        while (!digitalRead(BOARD_TOUCH_IRQ)) {
            delay(100);
            // Clear touch buffer
            getPoint(x_array, y_array, get_point);
        }
        // Wait for the interrupt level to stabilize
        delay(2000);
        // Set touch irq wakeup
        esp_sleep_enable_ext1_wakeup(_BV(BOARD_TOUCH_IRQ), ESP_EXT1_WAKEUP_ANY_LOW);
    } break;
    case LILYGO_T_RGB_WAKEUP_FORM_BUTTON:
        esp_sleep_enable_ext1_wakeup(_BV(0), ESP_EXT1_WAKEUP_ANY_LOW);
        break;
    case LILYGO_T_RGB_WAKEUP_FORM_TIMER:
        esp_sleep_enable_timer_wakeup(_sleepTimeUs);
        break;
    default:
        // Default GPIO0 Wakeup
        esp_sleep_enable_ext1_wakeup(_BV(0), ESP_EXT1_WAKEUP_ANY_LOW);
        break;
    }

    if (_panelDrv) {
        panelclock::detach();
        esp_lcd_panel_disp_on_off(_panelDrv, false);
        esp_lcd_panel_del(_panelDrv);
    }

    Wire.end();

    pinMode(BOARD_I2C_SDA, OPEN_DRAIN);
    pinMode(BOARD_I2C_SCL, OPEN_DRAIN);

    Serial.end();

    // If the SD card is initialized, it needs to be unmounted.
    if (SD_MMC.cardSize()) {
        SD_MMC.end();
    }

    // Enter sleep
    esp_deep_sleep_start();
}

void LilyGo_RGBPanel::wakeup() {}

uint16_t LilyGo_RGBPanel::width() { return BOARD_TFT_WIDTH; }

uint16_t LilyGo_RGBPanel::height() { return BOARD_TFT_HEIGHT; }

uint8_t LilyGo_RGBPanel::getPoint(int16_t *x_array, int16_t *y_array, uint8_t get_point) {
    if (_touchDrv) {

        // The FT3267 type touch reading INT level is to read the coordinates
        // after pressing The CST820 interrupt level is not continuous, so the
        // register must be read all the time to obtain continuous coordinates.
        if (_touchType == LILYGO_T_RGB_TOUCH_FT3267) {
            if (!_touchDrv->isPressed()) {
                return 0;
            }
        }
        uint8_t touched = _touchDrv->getPoint(x_array, y_array, get_point);
        return touched;
    }
    return 0;
}

bool LilyGo_RGBPanel::isPressed() {
    if (_touchDrv) {
        return _touchDrv->isPressed();
    }
    return 0;
}

uint16_t LilyGo_RGBPanel::getBattVoltage() {
    const int number_of_samples = 20;
    uint32_t sum = 0;
    for (int i = 0; i < number_of_samples; i++) {
        sum += analogReadMilliVolts(BOARD_ADC_DET);
        delay(2);
    }
    return (sum / number_of_samples) * 2;
}

void LilyGo_RGBPanel::initBUS() {
    assert(_init_cmd);

    if (_panelDrv) {
        return;
    }

    extension.pinMode(reset, OUTPUT);
    extension.digitalWrite(reset, LOW);
    delay(20);
    extension.digitalWrite(reset, HIGH);
    delay(10);

    Wire.setClock(1000000UL);
    // uint32_t start = millis();

    extension.beginSPI(mosi, -1, sclk, cs);

    int i = 0;
    while (_init_cmd[i].databytes != 0xff) {
        writeCommand(_init_cmd[i].cmd);
        writeData(_init_cmd[i].data, _init_cmd[i].databytes & 0x1F);
        if (_init_cmd[i].databytes & 0x80) {
            delay(100);
        }
        i++;
    }

    // uint32_t end = millis();

    // Serial.printf("Initialization took %u milliseconds\n", end - start);

    // Uses 400k I2C speed : Initialization took about 6229 milliseconds
    // Uses 1M   I2C speed : Initialization took about 1833 milliseconds

    // Reduce to standard speed, touch does not support access greater than
    // 400KHZ speed
    Wire.setClock(400000UL);

    const int bus_rbg_order[SOC_LCD_RGB_DATA_WIDTH] = {
        // BOARD_TFT_DATA12,    //LSB
        BOARD_TFT_DATA13,
        BOARD_TFT_DATA14,
        BOARD_TFT_DATA15,
        BOARD_TFT_DATA16,
        BOARD_TFT_DATA17,

        BOARD_TFT_DATA0,
        BOARD_TFT_DATA1,
        BOARD_TFT_DATA2,
        BOARD_TFT_DATA3,
        BOARD_TFT_DATA4,
        BOARD_TFT_DATA5,

        // BOARD_TFT_DATA6,     //LSB
        BOARD_TFT_DATA7,
        BOARD_TFT_DATA8,
        BOARD_TFT_DATA9,
        BOARD_TFT_DATA10,
        BOARD_TFT_DATA11,
    };

    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_PLL160M,
        .timings =
            {
                // Build default, unless a refresh-rate setting was seeded from
                // NVS before setupPanel() — see PanelClock.h. Honouring it here
                // rather than only live means the setting survives a reboot on
                // IDF 5 and is the only thing that applies it at all on 4.4.
                .pclk_hz = panelclock::pclkHzForInit(RGB_MAX_PIXEL_CLOCK_HZ),
                .h_res = BOARD_TFT_WIDTH,
                .v_res = BOARD_TFT_HEIGHT,
                // The following parameters should refer to LCD spec.
                // Overridable from build flags; stock values give 561x531
                // total timing (23.5 Hz at 7 MHz pclk).
                .hsync_pulse_width = GM_LCD_HSYNC_PW,
                .hsync_back_porch = GM_LCD_HSYNC_BP,
                .hsync_front_porch = GM_LCD_HSYNC_FP,
                .vsync_pulse_width = GM_LCD_VSYNC_PW,
                .vsync_back_porch = GM_LCD_VSYNC_BP,
                .vsync_front_porch = GM_LCD_VSYNC_FP,
                .flags =
                    {
                        .pclk_active_neg = 1,
                    },
            },
        .data_width = 16, // RGB565 in parallel mode, thus 16bit in width
        // Two framebuffers, so a frame can be composed in the one the panel is
        // not scanning and then shown whole. esp_lcd switches which buffer the
        // scan-out DMA reads by re-linking its descriptor lists, so the change
        // lands when the DMA reaches the end of the current buffer -- a frame
        // boundary. With one buffer there is nothing to synchronise against and
        // every write races the beam.
        .num_fbs = FB_COUNT,
        // Bounce buffers. Without them the scan-out DMA reads every pixel
        // straight out of PSRAM in real time, so anything else touching PSRAM
        // -- the render task's writes, WiFi, a cache miss on the shared SPI0
        // controller -- can starve it. The RGB peripheral does not stall when
        // that happens: it keeps generating HSYNC/VSYNC off its own counters
        // and clocks out whatever is in the FIFO, so the pixel stream slips
        // against the sync signals. On screen that is a line displaced
        // sideways, wrapping around the edge, and because RGB565's green field
        // straddles the byte boundary a one-byte slip also recolours white to
        // green. Measured directly: at 13.3 MHz pclk the panel is unreadable
        // with the renderer at 60 fps and mostly clean at 5 fps, and dropping
        // to 6.7 MHz cleans it up at 60 fps. Both are the same bandwidth
        // cliff.
        //
        // With bounce buffers the DMA reads internal SRAM, which cannot be
        // starved, and an EOF interrupt refills it from the framebuffer. PSRAM
        // traffic is unchanged; what changes is that it no longer has to be
        // punctual. The refill may run late by most of a bounce period before
        // anything shows.
        //
        // Sized in whole scanlines so the copy stays a single contiguous run,
        // and it must divide the framebuffer exactly (the driver rejects it
        // otherwise). Internal RAM is the scarce resource here -- two buffers of
        // ten lines cost 2 * 480 * 10 * 2 = 19200 bytes -- but the slack a
        // bounce buffer buys is one buffer period, so it scales with the size,
        // and ten lines also halves the EOF interrupt rate to 48 per frame.
        //
        // The other half of the fix is CONFIG_ESP32S3_DATA_CACHE_LINE_64B. The
        // refill reads the framebuffer front to back, which is exactly the
        // pattern a long cache line serves best; at 32 bytes a 9600-byte refill
        // is 300 separate PSRAM transactions, at 64 it is 150. Measured on this
        // panel, that one setting is the difference between a visible vertical
        // shake at 16 MHz and thirty consecutive clean frames.
        .bounce_buffer_size_px = GM_LCD_BOUNCE_LINES * BOARD_TFT_WIDTH,
        .dma_burst_size = 64, // union alias of the deprecated psram_trans_align under IDF 5.5
        .hsync_gpio_num = BOARD_TFT_HSYNC,
        .vsync_gpio_num = BOARD_TFT_VSYNC,
        .de_gpio_num = BOARD_TFT_DE,
        .pclk_gpio_num = BOARD_TFT_PCLK,
        .disp_gpio_num = GPIO_NUM_NC,
        .data_gpio_nums =
            {
                // BOARD_TFT_DATA0,
                BOARD_TFT_DATA13,
                BOARD_TFT_DATA14,
                BOARD_TFT_DATA15,
                BOARD_TFT_DATA16,
                BOARD_TFT_DATA17,

                BOARD_TFT_DATA6,
                BOARD_TFT_DATA7,
                BOARD_TFT_DATA8,
                BOARD_TFT_DATA9,
                BOARD_TFT_DATA10,
                BOARD_TFT_DATA11,
                // BOARD_TFT_DATA12,

                BOARD_TFT_DATA1,
                BOARD_TFT_DATA2,
                BOARD_TFT_DATA3,
                BOARD_TFT_DATA4,
                BOARD_TFT_DATA5,
            },
        .flags =
            {
                .fb_in_psram = 1, // allocate frame buffer in PSRAM
            },
    };

    if (_order == LILYGO_T_RGB_ORDER_BGR) {

        // Swap color order

        uint8_t data = 0x00;
        writeCommand(0x36);
        writeData(&data, 1);

        memcpy(panel_config.data_gpio_nums, bus_rbg_order, sizeof(panel_config.data_gpio_nums));
    }

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &_panelDrv));
    ESP_ERROR_CHECK(esp_lcd_panel_init(_panelDrv));
    // Live refresh-rate control needs the driver handle: retiming has to go
    // through esp_lcd so its cached timings stay truthful (see PanelClock.h).
    panelclock::attach(_panelDrv, panel_config.timings.pclk_hz);
}

bool LilyGo_RGBPanel::initTouch() {
    const uint8_t touch_reset_pin = tp_reset | 0x80;
    const uint8_t touch_irq_pin = BOARD_TOUCH_IRQ;
    bool result = false;

    log_i("=================initTouch====================");
    _touchDrv = new TouchDrvCSTXXX();
    _touchDrv->setGpioCallback(TouchDrvPinMode, TouchDrvDigitalWrite, TouchDrvDigitalRead);
    _touchDrv->setPins(touch_reset_pin, touch_irq_pin);
    result = _touchDrv->begin(Wire, CST816_SLAVE_ADDRESS, BOARD_I2C_SDA, BOARD_I2C_SCL);
    if (result) {
        // Keep CST816/CST820 awake: it auto-sleeps when idle and then NACKs every
        // poll, which IDF 5.x's i2c.master driver logs at ERROR level. No-op on
        // non-CST816 chips.
        static_cast<TouchDrvCSTXXX *>(_touchDrv)->disableAutoSleep();

        _init_cmd = st7701_2_1_inches;

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
        const char *model = _touchDrv->getModelName();
        log_i("Successfully initialized %s, using %s Driver!\n", model, model);
#endif
        return true;
    }
    delete _touchDrv;

    _touchDrv = new TouchDrvGT911();
    _touchDrv->setGpioCallback(TouchDrvPinMode, TouchDrvDigitalWrite, TouchDrvDigitalRead);
    _touchDrv->setPins(touch_reset_pin, touch_irq_pin);
    result = _touchDrv->begin(Wire, GT911_SLAVE_ADDRESS_L, BOARD_I2C_SDA, BOARD_I2C_SCL);
    if (result) {
        TouchDrvGT911 *tmp = static_cast<TouchDrvGT911 *>(_touchDrv);
        tmp->setInterruptMode(FALLING);

        _init_cmd = st7701_2_8_inches;
        log_i("Successfully initialized GT911, using GT911 Driver!");
        return true;
    }
    delete _touchDrv;

    _touchDrv = new TouchDrvFT6X36();
    _touchDrv->setGpioCallback(TouchDrvPinMode, TouchDrvDigitalWrite, TouchDrvDigitalRead);
    _touchDrv->setPins(touch_reset_pin, touch_irq_pin);
    result = _touchDrv->begin(Wire, FT3267_SLAVE_ADDRESS, BOARD_I2C_SDA, BOARD_I2C_SCL);
    if (result) {

        _init_cmd = st7701_2_1_inches;

        TouchDrvFT6X36 *tmp = static_cast<TouchDrvFT6X36 *>(_touchDrv);
        tmp->interruptTrigger();

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
        const char *model = _touchDrv->getModelName();
        log_i("Successfully initialized %s, using %s Driver!\n", model, model);
#endif

        return true;
    }
    delete _touchDrv;

    log_e("Unable to find touch device.");

    _touchDrv = NULL;

    // If touch does not exist, add a default initialization sequence
    _init_cmd = st7701_2_1_inches;

    return false;
}

void LilyGo_RGBPanel::writeCommand(const uint8_t cmd) {
    uint16_t data = cmd;
    extension.transfer9(data);
}

void LilyGo_RGBPanel::writeData(const uint8_t *data, int len) {
    uint32_t i = 0;
    if (len > 0) {
        do {
            // The ninth bit of data, 1, represents data, 0 represents command
            uint16_t pdat = (*(data + i)) | 1 << 8;
            extension.transfer9(pdat);
            i++;
        } while (len--);
    }
}

void LilyGo_RGBPanel::pushColors(uint16_t x, uint16_t y, uint16_t width, uint16_t hight, uint16_t *data) {
    if (_panelDrv == nullptr) { // panel stopped for display OTA
        return;
    }
    lockFrameBuffer();
    if (_directWriter && _fbDirect[_fbCurrent] != nullptr && hight > y) {
        // esp_lcd's rgb_panel_draw_bitmap ends with a Cache_WriteBack_Addr over
        // whole SCANLINES -- disassembled from this build: it computes
        // (y_end - y_start) * bytes_per_line from fb + y_start * bytes_per_line
        // and writes that whole span back, not the rectangle it was asked to
        // draw. So a repaint of a small clock area flushes every cache line
        // across the full width of those rows.
        //
        // With something else writing the same framebuffer over DMA, those
        // lines hold a stale view, and the writeback smears it across
        // full-width bands on top of the DMA's pixels. That is a persistent
        // visible garble, not a transient one, because the stale data wins.
        //
        // Dropping the lines first makes the copy below read-allocate from
        // PSRAM, which is where the DMA's pixels actually are, so the driver's
        // writeback carries fresh content plus whatever LVGL just drew.
        // Discarding is safe rather than lossy: the only writer that dirties
        // this region is this function, and the driver already wrote those
        // lines back on the way out last time.
        //
        // The span is line-aligned by construction -- 480 px x 2 B = 960 B per
        // line, a multiple of the 32-byte cache line.
        const uint32_t stride = static_cast<uint32_t>(this->width()) * 2;
        esp_cache_msync(reinterpret_cast<uint8_t *>(_fbDirect[_fbCurrent]) + static_cast<size_t>(y) * stride,
                        static_cast<size_t>(hight - y) * stride, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    }
    esp_lcd_panel_draw_bitmap(_panelDrv, x, y, width, hight, data);
    unlockFrameBuffer();
}

void LilyGo_RGBPanel::setDirectWriter(bool active) { _directWriter = active; }

uint16_t *LilyGo_RGBPanel::directFrameBuffer(int index) {
    if (!_fbResolved) {
        resolveFrameBuffers();
    }
    return (index >= 0 && index < _fbCount) ? _fbDirect[index] : nullptr;
}

int LilyGo_RGBPanel::frameBufferCount() {
    if (!_fbResolved) {
        resolveFrameBuffers();
    }
    return _fbCount;
}

void LilyGo_RGBPanel::resolveFrameBuffers() {
    _fbResolved = true;
    _fbCount = 0;
    if (_panelDrv == nullptr) {
        return;
    }
    // esp_lcd's public accessor. This used to index the driver's private
    // esp_rgb_panel_t at byte offsets recovered by disassembling an IDF 4.4.7
    // build, guarded by a fingerprint of the neighbouring fields so a layout
    // change would fail closed rather than hand out a pointer into the middle
    // of a live driver object. It did fail closed, silently: IDF 5.5.1 moved
    // the framebuffer array from offset 72 to 108, so every resolve returned
    // null and the direct push path was unreachable on this branch. There is
    // no layout dependency left to break.
    void *fbs[FB_COUNT] = {};
    const esp_err_t err = esp_lcd_rgb_panel_get_frame_buffer(_panelDrv, FB_COUNT, &fbs[0], &fbs[1]);
    if (err != ESP_OK) {
        log_w("LilyGo_RGBPanel: no direct framebuffer (%s)", esp_err_to_name(err));
        return;
    }

    // Preconditions the direct push path actually relies on, not a layout
    // check: a framebuffer outside PSRAM means the panel was built with
    // fb_in_psram off and none of the cache reasoning below applies, and one
    // that is not cache-line aligned makes every esp_cache_msync over a band
    // fail its own alignment check.
    //
    // Asked for rather than hardcoded. An earlier version of this required 64
    // bytes, which held under IDF 4.4 because psram_trans_align was a real
    // alignment request; under 5.5 that field is the DMA burst size and the
    // driver aligns the framebuffer to the data cache line instead -- 32 bytes
    // on this part -- so a fixed 64 rejected a framebuffer that was perfectly
    // usable.
    size_t cacheAlign = 0;
    if (esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &cacheAlign) != ESP_OK || cacheAlign == 0) {
        cacheAlign = 64; // unknown: fall back to the stricter requirement
    }
    // Every band lands at fb + row * stride, so the stride has to preserve
    // whatever alignment the base has. 480 px x 2 B = 960 B, which is a
    // multiple of 32 but not of 64 -- worth asserting rather than assuming.
    if (((static_cast<size_t>(width()) * 2) % cacheAlign) != 0) {
        log_w("LilyGo_RGBPanel: stride %u not a multiple of the %u B cache line; no direct writes",
              static_cast<unsigned>(width()) * 2, static_cast<unsigned>(cacheAlign));
        return;
    }
    for (int i = 0; i < FB_COUNT; i++) {
        uint16_t *const fb = static_cast<uint16_t *>(fbs[i]);
        const bool inPsram = fb != nullptr && esp_ptr_external_ram(fb);
        const bool aligned = fb != nullptr && (reinterpret_cast<uintptr_t>(fb) & (cacheAlign - 1)) == 0;
        if (!inPsram || !aligned) {
            // Take the buffers that pass, in order, and stop at the first that
            // does not: a caller flipping between 0 and 1 needs them both, and
            // one usable buffer is still worth having for a direct writer that
            // accepts the tearing.
            log_w("LilyGo_RGBPanel: framebuffer %d at %p unusable for direct writes (psram=%d aligned=%d, need %u B)", i,
                  fb, static_cast<int>(inPsram), static_cast<int>(aligned), static_cast<unsigned>(cacheAlign));
            break;
        }
        _fbDirect[i] = fb;
        _fbCount = i + 1;
    }
    if (_fbCount == 0) {
        return;
    }
    if (_fbGate == nullptr) {
        // A binary semaphore rather than a mutex: the direct writer holds this
        // across an asynchronous GDMA transfer and releases it from the
        // completion interrupt, and xSemaphoreGiveFromISR cannot act on a
        // mutex. The priority inheritance a mutex would add buys nothing here
        // -- the only two contenders, the LVGL task and the animation render
        // task, run at the same priority on the same core.
        _fbGate = xSemaphoreCreateBinary();
        if (_fbGate == nullptr) {
            _fbCount = 0; // without the gate a direct writer would race pushColors
            return;
        }
        xSemaphoreGive(_fbGate); // created empty; start it unheld
    }
    log_i("LilyGo_RGBPanel: %d direct framebuffers at %p/%p (%ux%u)", _fbCount, _fbDirect[0], _fbDirect[1],
          static_cast<unsigned>(width()), static_cast<unsigned>(height()));
}

void LilyGo_RGBPanel::presentFrameBuffer(int index, int dirtyY0, int dirtyY1) {
    if (_panelDrv == nullptr || index < 0 || index >= _fbCount || _fbDirect[index] == nullptr) {
        return;
    }
    // Handing esp_lcd a pointer that IS one of its own framebuffers takes the
    // branch in rgb_panel_draw_bitmap that skips the copy entirely: it records
    // the new index and re-concatenates the scan-out DMA's descriptor lists so
    // the next traversal reads this buffer. No pixel moves.
    //
    // It also runs a cache writeback over rows [y_start, y_end), which is why
    // the caller passes the range it dirtied through the cache rather than the
    // range it drew. A DMA writer dirties nothing, so it passes an empty range
    // and pays for one row instead of 480. The row still has to be non-empty:
    // the driver would clip an inverted range to nothing but the arithmetic is
    // not worth relying on.
    if (dirtyY1 <= dirtyY0) {
        dirtyY0 = 0;
        dirtyY1 = 1;
    }
    esp_lcd_panel_draw_bitmap(_panelDrv, 0, dirtyY0, width(), dirtyY1, _fbDirect[index]);
    _fbCurrent = index;
}

void LilyGo_RGBPanel::lockFrameBuffer() {
    if (_fbGate != nullptr) {
        xSemaphoreTake(_fbGate, portMAX_DELAY);
    }
}

void LilyGo_RGBPanel::unlockFrameBuffer() {
    if (_fbGate != nullptr) {
        xSemaphoreGive(_fbGate);
    }
}

void *LilyGo_RGBPanel::frameBufferGate() { return _fbGate; }

void LilyGo_RGBPanel::stopPanel() {
    setBrightness(0);
    if (_panelDrv != nullptr) {
        esp_lcd_panel_handle_t handle = _panelDrv;
        _panelDrv = nullptr;
        panelclock::detach();
        esp_lcd_panel_del(handle);
    }
}

static void TouchDrvDigitalWrite(uint32_t gpio, uint8_t level) {
    if (gpio & 0x80) {
        extension.digitalWrite(gpio & 0x7F, level);
    } else {
        digitalWrite(gpio, level);
    }
}

static int TouchDrvDigitalRead(uint32_t gpio) {
    if (gpio & 0x80) {
        return extension.digitalRead(gpio & 0x7F);
    } else {
        return digitalRead(gpio);
    }
}

static void TouchDrvPinMode(uint32_t gpio, uint8_t mode) {
    if (gpio & 0x80) {
        extension.pinMode(gpio & 0x7F, mode);
    } else {
        pinMode(gpio, mode);
    }
}
