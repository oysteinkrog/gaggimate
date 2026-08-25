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

// Scan-out underrun counters. Written only from the panel's ISRs and read from
// tasks, so plain volatile is enough: each is a single naturally aligned 32-bit
// store on this core, and a reader that catches a stale value is off by one
// frame on a number whose whole purpose is to be watched over seconds.
volatile uint32_t g_frames = 0;  // on_vsync, one per displayed frame
volatile uint32_t g_refills = 0; // on_frame_buf_complete, one per refill pass
volatile uint32_t g_slips = 0;   // ratcheted count of underrun frames
volatile uint32_t g_maxDrift = 0;

// IRAM because these run from the panel's interrupts, which the driver keeps
// alive across a flash operation when its own ISR is IRAM-safe. Two increments
// each; the cost to the frame budget is nil, and putting them in flash would
// turn a diagnostic into a way to crash during an NVS write.
IRAM_ATTR bool onRefillDone(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *) {
    g_refills++;
    return false;
}

IRAM_ATTR bool onVsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *) {
    g_frames++;
    // Ratchet rather than report the raw difference. The two callbacks fire at
    // different points in the frame, so their difference sits at 0 or 1 even
    // when the scan-out is perfect; only a refill pass that never completed
    // pushes it permanently higher, and each new high-water mark is one
    // underrun frame.
    const uint32_t drift = g_frames - g_refills;
    if (drift > g_maxDrift) {
        g_maxDrift = drift;
        // The first frames after init legitimately run ahead of the first
        // completed refill pass, which is a phase offset and not a fault.
        if (g_frames > 4) {
            g_slips++;
        }
    }
    return false;
}

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
    // Counters restart with the panel: after a display OTA tears it down and
    // rebuilds it, a total carried over from the previous instance would be
    // attributed to the new timing.
    g_frames = g_refills = g_slips = g_maxDrift = 0;
    // Field-by-field rather than a designated initialiser: on_frame_buf_complete
    // shares an anonymous union with a deprecated alias, and naming a union
    // member in a braced list makes the initialiser order-dependent in a way
    // g++ rejects outright.
    esp_lcd_rgb_panel_event_callbacks_t cbs = {};
    cbs.on_vsync = onVsync;
    cbs.on_frame_buf_complete = onRefillDone;
    esp_lcd_rgb_panel_register_event_callbacks(g_panel, &cbs, nullptr);
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

void scanoutStats(uint32_t *frames, uint32_t *refills, uint32_t *slips) {
    // Deliberately not taking the lock: these are ISR-written counters, and the
    // mutex here guards the panel handle against deletion, which is unrelated.
    if (frames != nullptr) {
        *frames = g_frames;
    }
    if (refills != nullptr) {
        *refills = g_refills;
    }
    if (slips != nullptr) {
        *slips = g_slips;
    }
}

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
void scanoutStats(uint32_t *frames, uint32_t *refills, uint32_t *slips) {
    if (frames != nullptr) {
        *frames = 0;
    }
    if (refills != nullptr) {
        *refills = 0;
    }
    if (slips != nullptr) {
        *slips = 0;
    }
}
} // namespace panelclock

#endif
