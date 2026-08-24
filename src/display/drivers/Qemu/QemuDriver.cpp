#ifdef GAGGIMATE_QEMU

#include "QemuDriver.h"

#include <display/drivers/common/LV_Helper.h>
#include <esp_log.h>
#include <esp_system.h>

static constexpr const char *LOG_TAG = "QemuDriver";

QemuDriver *QemuDriver::instance = nullptr;

void QemuDriver::init() {
    ESP_LOGI(LOG_TAG, "QemuDriver initializing");
    if (!panel.begin()) {
        // Nothing to retry against: if the synthetic device is not there, the
        // machine was started without it and no amount of waiting helps.
        ESP_LOGE(LOG_TAG, "QEMU RGB panel unavailable");
        esp_restart();
    }
    beginLvglHelper(panel);
}

#endif // GAGGIMATE_QEMU
