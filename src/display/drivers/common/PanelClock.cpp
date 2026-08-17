#include "PanelClock.h"

// CONFIG_IDF_TARGET_* lives in sdkconfig.h, which Arduino sources don't get
// implicitly — pull it in before testing the target guard.
#if !defined(GAGGIMATE_SIM) && __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif

#if defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(GAGGIMATE_SIM)

#include <esp_lcd_panel_rgb.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// esp_lcd_rgb_panel_set_pclk arrived in ESP-IDF 5.0. On 4.4 there is no
// supported way to retime a running RGB panel, so the setting is applied at the
// next panel init instead of live (see header).
#if ESP_IDF_VERSION_MAJOR >= 5
#define PANELCLOCK_HAS_SET_PCLK 1
#else
#define PANELCLOCK_HAS_SET_PCLK 0
#endif

namespace panelclock {

namespace {
constexpr char LOG_TAG[] = "PanelClock";
// Nominal group clock, used only to turn the user-facing divider into a
// requested frequency. The hardware divider is not actually this simple (see
// setDiv), which is exactly why the request goes through the driver.
constexpr uint32_t GROUP_CLK_HZ = 80000000UL;

esp_lcd_panel_handle_t g_panel = nullptr;
uint32_t g_bootPclkHz = 0;
int g_bootDiv = 0;
int g_curDiv = 0;

// setDiv() runs on the async web-server task (WebUIPlugin) and the LVGL task,
// while attach()/detach() run on the display task around panel creation and
// deletion. Without serialisation, setDiv() can pass its null check on one core
// while stopPanel() deletes the panel on the other, and hand a freed handle to
// the driver. The mutex is held across the driver call so a deletion cannot
// begin until an in-flight retime has returned.
SemaphoreHandle_t lock() {
    static SemaphoreHandle_t m = xSemaphoreCreateMutex();
    return m;
}

struct Guard {
    SemaphoreHandle_t m;
    explicit Guard(SemaphoreHandle_t s) : m(s) {
        if (m != nullptr) {
            xSemaphoreTake(m, portMAX_DELAY);
        }
    }
    ~Guard() {
        if (m != nullptr) {
            xSemaphoreGive(m);
        }
    }
};

// Divider the user-facing setting means, as a frequency. Clamped to the same
// [2,16] window setDiv() accepts so a boot frequency at the edges cannot seed a
// divider setDiv() would refuse to reproduce.
int divForHz(uint32_t hz) {
    if (hz == 0) {
        return 0;
    }
    int n = static_cast<int>(GROUP_CLK_HZ / hz);
    if (n < 2) {
        n = 2;
    } else if (n > 16) {
        n = 16;
    }
    return n;
}

// Caller holds the lock.
void applyLocked(int n) {
#if PANELCLOCK_HAS_SET_PCLK
    if (g_panel == nullptr) {
        // Setting read before the panel came up, or while it is torn down for a
        // display OTA. attach() re-applies it.
        g_curDiv = n;
        return;
    }
    const uint32_t hz = GROUP_CLK_HZ / static_cast<uint32_t>(n);
    // Records the frequency and defers the register work to the next VSYNC.
    // No esp_lcd_rgb_panel_restart() is needed: the driver applies a pending
    // pclk update from the VSYNC handler regardless of its restart logic.
    const esp_err_t err = esp_lcd_rgb_panel_set_pclk(g_panel, hz);
    if (err != ESP_OK) {
        ESP_LOGW(LOG_TAG, "set_pclk(%u Hz) failed: %s", static_cast<unsigned>(hz), esp_err_to_name(err));
        return;
    }
    ESP_LOGI(LOG_TAG, "pclk divider %d -> %d (request %.1f MHz)", g_curDiv, n, GROUP_CLK_HZ / 1e6f / n);
    g_curDiv = n;
#else
    ESP_LOGI(LOG_TAG, "pclk divider %d -> %d, applied at next panel init", g_curDiv, n);
    g_curDiv = n;
#endif
}
} // namespace

void attach(void *panelHandle, uint32_t bootPclkHz) {
    Guard g(lock());
    g_panel = static_cast<esp_lcd_panel_handle_t>(panelHandle);
    g_bootPclkHz = bootPclkHz;
    g_bootDiv = divForHz(bootPclkHz);
    // A divider chosen before the panel existed (or before it was torn down for
    // a display OTA) is still the user's choice — re-apply it rather than
    // silently reverting to the boot rate.
    const int wanted = g_curDiv;
    g_curDiv = g_bootDiv;
    if (wanted != 0 && wanted != g_bootDiv) {
        applyLocked(wanted);
    }
}

void detach() {
    Guard g(lock());
    g_panel = nullptr;
}

uint32_t pclkHzForInit(uint32_t defaultHz) {
    Guard g(lock());
    if (g_curDiv < 2 || g_curDiv > 16) {
        return defaultHz;
    }
    return GROUP_CLK_HZ / static_cast<uint32_t>(g_curDiv);
}

int currentDiv() {
    Guard g(lock());
    return g_curDiv;
}

uint32_t bootPclkHz() {
    Guard g(lock());
    return g_bootPclkHz;
}

bool hasLiveControl() { return PANELCLOCK_HAS_SET_PCLK != 0; }

void setDiv(int n) {
    Guard g(lock());
    if (n == 0) {
        n = g_bootDiv;
    } else if (n < 2) {
        n = 2;
    } else if (n > 16) {
        n = 16;
    }
    if (n == g_curDiv) {
        return;
    }
    applyLocked(n);
}

} // namespace panelclock

#else

namespace panelclock {
void attach(void *, uint32_t) {}
void detach() {}
uint32_t pclkHzForInit(uint32_t defaultHz) { return defaultHz; }
int currentDiv() { return 0; }
uint32_t bootPclkHz() { return 0; }
bool hasLiveControl() { return false; }
void setDiv(int) {}
} // namespace panelclock

#endif
