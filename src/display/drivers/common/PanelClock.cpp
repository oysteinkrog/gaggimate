#include "PanelClock.h"

// CONFIG_IDF_TARGET_* lives in sdkconfig.h, which Arduino sources don't get
// implicitly — pull it in before testing the target guard.
#if !defined(GAGGIMATE_SIM) && __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif

#if defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(GAGGIMATE_SIM)

#include <esp_log.h>
#include <hal/lcd_ll.h>
#include <soc/lcd_cam_struct.h>

namespace panelclock {

namespace {
constexpr char LOG_TAG[] = "PanelClock";
int bootDiv = 0; // captured on first setDiv() call
} // namespace

int currentDiv() {
    if (LCD_CAM.lcd_clock.lcd_clk_equ_sysclk) {
        return 1;
    }
    return static_cast<int>(LCD_CAM.lcd_clock.lcd_clkcnt_n) + 1;
}

void setDiv(int n) {
    if (bootDiv == 0) {
        bootDiv = currentDiv();
    }
    if (n == 0) {
        // Restore the boot value verbatim — it may legitimately be 1
        // (lcd_clk_equ_sysclk), which the explicit-value clamp below forbids.
        n = bootDiv;
    } else if (n < 2) {
        n = 2;
    } else if (n > 16) {
        n = 16;
    }
    if (n == currentDiv()) {
        return;
    }
    ESP_LOGI(LOG_TAG, "pclk divider %d -> %d (%.1f MHz)", currentDiv(), n, 80.0f / n);
    lcd_ll_set_pixel_clock_prescale(&LCD_CAM, static_cast<uint32_t>(n));
}

} // namespace panelclock

#else

namespace panelclock {
int currentDiv() { return 0; }
void setDiv(int) {}
} // namespace panelclock

#endif
