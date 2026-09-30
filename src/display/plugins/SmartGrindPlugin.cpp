#include "SmartGrindPlugin.h"
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <display/core/Controller.h>
#include <display/core/Event.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>

namespace {
constexpr const char *TAG = "SmartGrind";

// A few jobs is plenty: one grind produces at most two. A post never waits
// for room; a job that does not fit is dropped and logged, so a plug that
// is unreachable for a string of grinds cannot back up into the logic task.
constexpr UBaseType_t kQueueDepth = 4;

// The stack holds HTTPClient, its WiFiClient, the URL String and lwIP's
// connect and receive path. Estimated at about 3 KB; 6 KB leaves room, and
// the stack lives in PSRAM so the margin costs no internal DRAM. The task
// does only HTTP and never runs with the flash cache disabled (CLAUDE.md,
// DRAM budget). Its measured high-water mark is logged after each request
// and listed by /api/debug/heapmap; size it from that.
constexpr uint32_t kStackBytes = 6144;

// HTTPClient waits up to 5 s for a connect by default. The worker is off the
// logic task, so this no longer delays control, but a short bound keeps an
// unreachable plug from holding later jobs back for long.
constexpr int32_t kConnectTimeoutMs = 2000;
constexpr uint16_t kReadTimeoutMs = 2000;
} // namespace

void SmartGrindPlugin::setup(Controller *controller, PluginManager *pluginManager) {
    this->controller = controller;
    queue = xQueueCreate(kQueueDepth, sizeof(Job));
    TaskHandle_t handle = nullptr;
    if (queue != nullptr) {
        // Core 0, priority 1: under Controller::loopLogic (3), and off core 1,
        // which the UI task has to itself. The task never deletes itself.
        xTaskCreatePinnedToCoreWithCaps(workerMain, "SmartGrind", kStackBytes, this, 1, &handle, 0,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (handle == nullptr) {
        ESP_LOGE(TAG, "worker not created; the smart plug will not be switched");
        if (queue != nullptr) {
            vQueueDelete(queue);
            queue = nullptr;
        }
        return;
    }
    pluginManager->on("controller:grind:start", [this](Event const &event) {
        if (this->controller->getSettings().getSmartGrindMode() == SG_MODE_ON_OFF) {
            post(Job::On);
        }
    });
    pluginManager->on("controller:grind:end", [this](Event const &event) {
        post(this->controller->getSettings().getSmartGrindMode() == SG_MODE_OFF_ON ? Job::OffThenOn : Job::Off);
    });
}

void SmartGrindPlugin::post(Job job) {
    if (xQueueSend(queue, &job, 0) != pdTRUE) {
        ESP_LOGW(TAG, "request queue full, dropped job %d", static_cast<int>(job));
    }
}

void SmartGrindPlugin::workerMain(void *arg) {
    auto *self = static_cast<SmartGrindPlugin *>(arg);
    Job job;
    for (;;) {
        if (xQueueReceive(self->queue, &job, portMAX_DELAY) == pdTRUE) {
            self->run(job);
            ESP_LOGD(TAG, "stack high-water mark %u B", static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
        }
    }
}

void SmartGrindPlugin::run(Job job) {
    switch (job) {
    case Job::On:
        controlRelay(COMMAND_ON);
        break;
    case Job::Off:
        controlRelay(COMMAND_OFF);
        break;
    case Job::OffThenOn:
        controlRelay(COMMAND_OFF);
        vTaskDelay(pdMS_TO_TICKS(500));
        controlRelay(COMMAND_ON);
        break;
    }
}

void SmartGrindPlugin::controlRelay(const String &command) {
    HTTPClient http;
    String serverPath = "http://" + controller->getSettings().getSmartGrindIp() + "/cm?cmnd=" + command;
    http.setConnectTimeout(kConnectTimeoutMs);
    http.setTimeout(kReadTimeoutMs);
    if (!http.begin(serverPath)) {
        ESP_LOGW(TAG, "bad plug address, relay not switched");
        return;
    }
    int responseCode = http.GET();
    if (responseCode != 200) {
        ESP_LOGW(TAG, "failed to switch relay (%d)", responseCode);
    }
    http.end();
}
