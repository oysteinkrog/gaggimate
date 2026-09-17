#ifndef WEBUIPLUGIN_H
#define WEBUIPLUGIN_H

#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1

#include <DNSServer.h>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>

#include "GitHubOTA.h"
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <display/core/Plugin.h>
#include <display/util/PsramAllocator.h>

// The dev-deploy endpoints (gm-thg): /api/ota/info and /api/ota/dev. On by
// default, including on the production build, because the machine they are
// for is plumbed in and its USB port is not reachable. -DGM_DEV_OTA=0
// compiles both out; see handleDevOta for what that trades away.
#ifndef GM_DEV_OTA
#define GM_DEV_OTA 1
#endif

constexpr size_t UPDATE_CHECK_INTERVAL = 30 * 60 * 1000;
constexpr size_t CLEANUP_PERIOD = 1000;
constexpr size_t STATUS_PERIOD = 500;
constexpr size_t HARDWARE_SCALE_DIAGNOSTIC_PERIOD = 200;
constexpr size_t DNS_PERIOD = 50;

const String LOCAL_URL = "http://4.4.4.1/";
const String RELEASE_URL = "https://github.com/oysteinkrog/gaggimate/releases/";

class ProfileManager;

class WebUIPlugin : public Plugin {
  public:
    WebUIPlugin();
    void setup(Controller *controller, PluginManager *pluginManager) override;
    void loop() override;
    // Called by the /api/history/ handler on the async_tcp task.
    void handleHistoryRequest(AsyncWebServerRequest *request);

  private:
    void setupServer();
    // The debug, probe and bench routes, in WebUIPluginDebug.cpp (gm-bzu.19).
    void setupDebugEndpoints();
    void start();
    void stop();

    // Websocket handlers
    void handleWebSocketData(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data,
                             size_t len);
    void handleOTASettings(uint32_t clientId, JsonDocument &request);
    void handleOTAStart(uint32_t clientId, JsonDocument &request);
    void handleAutotuneStart(uint32_t clientId, JsonDocument &request);
    void handleProfileRequest(uint32_t clientId, JsonDocument &request);
    void handleFlushStart(uint32_t clientId, JsonDocument &request);

    // HTTP handlers
    // Serves the web UI from the firmware-embedded, memory-mapped flash blob
    // (catch-all for any path not claimed by an explicit route). [GM-106]
    void serveWebAsset(AsyncWebServerRequest *request);
    void startAssetStream(AsyncWebServerRequest *request);
    void drainAssetQueue();
    bool assetSlotFree() const;
    void handleSettings(AsyncWebServerRequest *request) const;
    void handleBLEScaleList(AsyncWebServerRequest *request);
    void handleBLEScaleScan(AsyncWebServerRequest *request);
    void handleBLEScaleConnect(AsyncWebServerRequest *request);
    void handleBLEScaleInfo(AsyncWebServerRequest *request);
    void handleDebugHeap(AsyncWebServerRequest *request);
    void updateOTAStatus(const String &version);
    void updateOTAProgress(uint8_t phase, int progress);
    void sendAutotuneResult();
    void sendAutotuneFailed();

    void broadcastJson(JsonDocument &doc);

    // Core dump download
    void handleCoreDumpDownload(AsyncWebServerRequest *request);

    GitHubOTA *ota = nullptr;
    AsyncWebServer server;
    AsyncWebSocket ws;
    Controller *controller = nullptr;
    PluginManager *pluginManager = nullptr;
    DNSServer *dnsServer = nullptr;
    ProfileManager *profileManager = nullptr;

    // Large-asset admission: how many big embedded assets stream at once, and
    // the paused requests waiting for a slot (see serveWebAsset). Touched only
    // from the async_tcp task (handlers and disconnect callbacks).
    uint8_t assetStreams = 0;
    std::deque<AsyncWebServerRequestPtr> assetQueue;

    // Shot-history files (/api/history/*) are opened by a worker task, never
    // by the web server's own task: a FAT lookup in /h is a linear scan of
    // the directory, seconds on a card with thousands of shots, and the
    // 5 s task watchdog aborted async_tcp on a miss (see handleHistoryRequest
    // in WebUIPlugin.cpp). The queue is pushed from async_tcp and popped by
    // the worker, under the lock; the counters are read by /api/debug/heap.
    struct HistoryJob;
    std::deque<std::shared_ptr<HistoryJob>> historyQueue;
    std::mutex historyLock;
    fs::FS *historyFs = nullptr;
    uint32_t histServed = 0;
    uint32_t histDropped = 0;
    uint32_t histQueueMax = 0;
    uint32_t histOpenUsMax = 0;
    void serviceHistoryQueue();
    void completeHistoryJob(HistoryJob &job, AsyncWebServerRequest *request);
    static void historyTaskFn(void *param);
#ifndef GAGGIMATE_SIM
    static void historyPollCb(void *arg, AsyncClient *client);
#endif

    long lastUpdateCheck = 0;
    long lastStatus = 0;
    long lastHardwareScaleDiagnostic = 0;
    long lastCleanup = 0;
    long lastDns = 0;
    bool updating = false;
    bool apMode = false;
    bool serverRunning = false;
    // /api/debug/radio: scheduled WiFi off/on for the scan-out A/B (see the
    // endpoint comment). millis() deadlines, 0 == nothing scheduled.
    unsigned long radioOffAtMs = 0;
    unsigned long radioOnAtMs = 0;
    String updateComponent = "";
    // /api/ota/dev: the URL to pull the next display image from, handed from
    // the async_tcp task to loop() on the display task, which is where every
    // other OTA runs. A fixed buffer and a flag rather than a String, because
    // the two tasks would otherwise share one heap-allocated buffer with no
    // lock; one producer and one consumer make the flag enough. Written
    // before the flag is set and read before it is cleared.
    static constexpr size_t kDevOtaUrlCap = 200;
    char devOtaUrl[kDevOtaUrlCap] = {};
    std::atomic<bool> devOtaPending{false};
    void handleDevOta(AsyncWebServerRequest *request);
    void handleOtaInfo(AsyncWebServerRequest *request) const;
    float currentWeight = 0.0f;
    // Reused for every 500ms status broadcast. Allocating a fresh JsonDocument
    // each tick was a major contributor to internal-heap fragmentation
    // (device reports 33%+ fragmentation, causing AsyncTCP buffer allocs to
    // stall mid-asset-serve). Keeping one doc lets its underlying pool grow
    // once and stay put.
    JsonDocument statusDoc{&psramAllocator};
    // Small, independent 5-Hz stream used only by the calibration indicators;
    // it avoids raising the rate of the much larger general status document.
    JsonDocument hardwareScaleDiagnosticDoc{&psramAllocator};
};

#endif // WEBUIPLUGIN_H
