#include "WebUIPlugin.h"
#ifndef GAGGIMATE_HEADLESS // the headless build has no LVGL and no UI tree (src/CMakeLists.txt)
#include <display/ui/default/GlyphAtlas.h>
#include <display/ui/default/TouchTask.h>
#endif

// Defined in AnimNebula.cpp; see nebulaLerpSelfTest there.
extern uint32_t nebula_lerp_self_test(uint32_t *firstBad);
#include <DNSServer.h>
#include <LittleFS.h>
#ifndef GAGGIMATE_SIM
#include <esp_cache.h> // esp_cache_msync, so /api/debug/fb reads past the cache
#endif
#include <SD_MMC.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <display/core/Controller.h>
#include <display/core/MemoryMonitor.h>
#include <display/core/ProfileManager.h>
#include <display/core/process/BrewProcess.h>
#include <display/core/process/GrindProcess.h>
#include <display/models/profile.h>
#include <display/plugins/BLEScalePlugin.h>
#include <display/plugins/ShotHistoryPlugin.h>
#include <cstring>
#include <esp_memory_utils.h> // esp_ptr_external_ram, for the band-buffer placement report
#include <esp_timer.h>        // esp_timer_dump, for /api/debug/timers
#ifndef GAGGIMATE_SIM
#include <esp_app_desc.h> // esp_app_get_description / _elf_sha256, for /api/ota/info
#include <esp_ota_ops.h>  // the running and next app slots, same
#endif
#include <sys/stat.h>
#ifdef GM_ANIM_BENCH
#include <display/ui/default/SleepAnimation.h>
#include <display/ui/default/bganim/BgAnim.h>
#include <display/ui/default/bganim/BgAnimCommon.h>
#include <esp_async_memcpy.h>
#include <esp_heap_caps.h>
#include <soc/gdma_channel.h> // SOC_GDMA_TRIG_PERIPH_LCD0 for /api/gdma
#include <soc/gdma_struct.h>  // direct GDMA register access for /api/gdma
#endif
// Not bench-only: /api/debug/heap reports the animation SRAM budget, and
// /api/settings echoes panelclock::hasLiveControl() so the form can tell the
// user whether a new divider applies now or at the next boot.
#include <display/core/utils.h>
#if !defined(GAGGIMATE_HEADLESS) && !defined(GAGGIMATE_SIM) // real LilyGo panel, not the SDL stand-in
#include <display/drivers/LilyGoDriver.h>
#endif
#ifdef GAGGIMATE_SIM
#include <SdlDriver.h> // /api/debug/fb's sim frame source
#endif
#include <display/core/TouchInject.h> // /api/debug/tap
#ifndef GAGGIMATE_HEADLESS
#include <display/drivers/common/LV_Helper.h>      // g_overlayStats / g_overlayMinRefreshUs for /api/debug/anim
#include <display/ui/default/eez/MeterTickCache.h> // tick_cache_bytes on /api/debug/anim
#include <display/ui/default/eez/eez-flow.h>       // eez_flow_object_names
#include <display/ui/default/eez/screens.h>        // objects, for /api/debug/touchlog
#endif
#include <display/drivers/common/PanelClock.h>
#include <display/ui/default/bganim/BgAnim.h> // bg_library_valid / bg_map_valid for the settings writer; headless too
#ifndef GAGGIMATE_HEADLESS
#include <display/ui/default/bganim/BgAnimCommon.h>
#endif
#ifdef GM_KBLOB
#include <display/ui/default/bganim/KBlob.h>
#endif
#include <display/util/PsramStlAllocator.h>
#include <display/util/PsramWsBuffer.h>
#include <display/webassets/web_ui_manifest.h>
#include <esp32-hal-psram.h>
#include <esp_core_dump.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#ifndef GAGGIMATE_SIM
#include <esp_private/freertos_debug.h> // uxTaskGetSnapshotAll, for /api/debug/heapmap
#endif
#include <esp_partition.h>
#include <esp_timer.h>
#include <mbedtls/platform.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <version.h>

// Incoming WebSocket payloads (profile uploads reserve up to 64 KB) are
// reassembled here. Back the character storage with PSRAM so these large,
// transient buffers don't spike the scarce internal SRAM. The map nodes
// themselves stay on the default heap (tiny: an id + a string handle).
using PsramString = std::basic_string<char, std::char_traits<char>, PsramStlAllocator<char>>;
static std::unordered_map<uint32_t, PsramString> rxBuffers;
static WebUIPlugin *g_webUIPlugin = nullptr;
// HistoryJob::deliverWs, set by setupServer (the job type is defined below loop()).
static void (*g_historyWsDeliver)(WebUIPlugin *) = nullptr;

// Serialize a JsonDocument straight into a PSRAM-backed WebSocket message
// buffer — one exact-sized allocation, off the internal heap. [GM-139]
static AsyncWebSocketSharedBuffer toWsBuffer(JsonDocument &doc) {
    const size_t len = measureJson(doc);
    auto buffer = makePsramWsBuffer(len);
    serializeJson(doc, buffer->data(), len);
    return buffer;
}

// Route mbedTLS allocations to PSRAM.
static void *mbedtlsPsramCalloc(size_t n, size_t size) { // NOSONAR
    void *p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == nullptr) {
        p = heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return p;
}
static void mbedtlsPsramFree(void *p) { heap_caps_free(p); } // NOSONAR

WebUIPlugin::WebUIPlugin() : server(80), ws("/ws") { g_webUIPlugin = this; }

void WebUIPlugin::setup(Controller *_controller, PluginManager *_pluginManager) {
    // Redirect mbedTLS allocations to PSRAM before any TLS (OTA) handshake runs, so the
    // ~32 KB handshake buffers don't exhaust the scarce internal-DRAM pool. See mbedtlsPsramCalloc.
    (void)mbedtls_platform_set_calloc_free(mbedtlsPsramCalloc, mbedtlsPsramFree);
    this->controller = _controller;
    this->profileManager = _controller->getProfileManager();
    this->pluginManager = _pluginManager;
    this->ota = new GitHubOTA(
        BUILD_GIT_VERSION, controller->getSystemInfo().version,
        RELEASE_URL + (controller->getSettings().getOTAChannel() == "latest" ? "latest" : "tag/nightly"),
        [this](uint8_t phase) {
            pluginManager->trigger("ota:update:phase", "phase", phase);
            updateOTAProgress(phase, 0);
        },
        [this](uint8_t phase, int progress) {
            pluginManager->trigger("ota:update:progress", "progress", progress);
            updateOTAProgress(phase, progress);
        },
        "display-firmware.bin", "display-filesystem.bin", "board-firmware.bin");
    pluginManager->on("controller:wifi:connect", [this](Event const &event) {
        const bool nowAp = event.getInt("AP") != 0;
        // Since the AP-fallback retry landed, this event can fire a SECOND
        // time with AP=0: the watchdog reconnects STA from AP fallback and
        // Controller::loop() drops the config AP with WiFi.mode(WIFI_STA).
        // start() early-returns once serverRunning is set, so without this the
        // captive-portal DNS server created for AP mode is never torn down and
        // loop() keeps polling it against an interface that no longer exists.
        // stop() cannot be used here: it would also close the web server,
        // which deliberately survives a reconnect (see start()).
        if (apMode && !nowAp && dnsServer != nullptr) {
            dnsServer->stop();
            delete dnsServer;
            dnsServer = nullptr;
            ESP_LOGI("WebUIPlugin", "Stopped catchall DNS (STA recovered from AP fallback)");
        }
        apMode = nowAp;
        start();
    });
    // Intentionally do NOT stop the server on a WiFi disconnect: the listen
    // socket survives a reconnect, and tearing it down only to rebind moments
    // later races AsyncTCP's async close (bind: -8) and churns sockets in the
    // recovery path. The server keeps listening; clients reconnect on their own.
    pluginManager->on("controller:wifi:disconnect", [this](Event const &) {
        ws.cleanupClients(); // drop dead websocket clients; keep the listener up
    });
    pluginManager->on("controller:ready", [this](Event const &) {
        ota->setControllerVersion(controller->getSystemInfo().version);
        // getClient() is null until the BLE transport has actually connected.
        // ControllerOTA::init() calls getService() straight through the pointer
        // (NimBLEClient.cpp:639), so passing null panics on a null this. The
        // controller-OTA path needs a live link anyway, so skip it and let the
        // next controller:ready set it up.
        NimBLEClient *bleClient = controller->getClientController()->getClient();
        if (bleClient == nullptr) {
            ESP_LOGW("WebUIPlugin", "controller:ready with no BLE client; skipping controller OTA init");
            return;
        }
        ota->init(bleClient);
    });
    pluginManager->on("controller:autotune:result", [this](Event const &event) { sendAutotuneResult(); });
    pluginManager->on("controller:autotune:failed", [this](Event const &) { sendAutotuneFailed(); });

    // Forward shot history rebuild progress events to WebSocket clients
    pluginManager->on("evt:history-rebuild-progress", [this](Event const &event) {
        JsonDocument doc(&psramAllocator);
        doc["tp"] = "evt:history-rebuild-progress";
        doc["total"] = event.getInt("total");
        doc["current"] = event.getInt("current");
        doc["status"] = event.getString("status");
        broadcastJson(doc);
    });

    // Forward "shot saved to history" events to WebSocket clients, so the
    // dashboard can refetch the recent-shots buffer at the right time.
    pluginManager->on("evt:history-shot-saved", [this](Event const &event) {
        JsonDocument doc(&psramAllocator);
        doc["tp"] = "evt:history-shot-saved";
        doc["id"] = event.getInt("id");
        broadcastJson(doc);
    });

    // Forward live shot-finished stats (pressure/flow) to WebSocket clients, so
    // the dashboard's finished card can show them without waiting for the
    // history file write.
    pluginManager->on("evt:shot-finished-stats", [this](Event const &event) {
        JsonDocument doc(&psramAllocator);
        doc["tp"] = "evt:shot-finished-stats";
        doc["maxPressure"] = event.getFloat("maxPressure");
        doc["avgFlow"] = event.getFloat("avgFlow");
        broadcastJson(doc);
    });

    // Subscribe to volumetric measurement updates (hardware, bluetooth, or estimation)
    pluginManager->on("controller:volumetric-measurement:active:change",
                      [this](Event const &event) { this->currentWeight = event.getFloat("value"); });

    setupServer();
}

void WebUIPlugin::loop() {
    // Scheduled radio window from /api/debug/radio. Runs here, on loopTask, for
    // the same reason the STA watchdog does its restarts from a task: WiFi mode
    // changes are too heavy for the timer or web-server tasks. The off/on pair
    // is the watchdog's own restartDriver() rung, so the recovery path is the
    // field-tested one. Ahead of the serverRunning check so the "on" deadline
    // can never be skipped.
    {
        const unsigned long tnow = millis();
        if (radioOffAtMs != 0 && static_cast<long>(tnow - radioOffAtMs) >= 0) {
            radioOffAtMs = 0;
            ESP_LOGW("WebUIPlugin", "debug/radio: WiFi OFF for %lu ms", radioOnAtMs - tnow);
            WiFi.mode(WIFI_OFF);
        }
        if (radioOnAtMs != 0 && static_cast<long>(tnow - radioOnAtMs) >= 0) {
            radioOnAtMs = 0;
            ESP_LOGW("WebUIPlugin", "debug/radio: WiFi back ON");
            WiFi.mode(WIFI_STA);
            WiFi.begin(controller->getSettings().getWifiSsid(), controller->getSettings().getWifiPassword());
        }
    }
    if (updating) {
        // Pass which component is being flashed: a controller update streams the
        // firmware over BLE (wants a low-latency link), a display update is over
        // Wi-Fi (wants BLE to stay out of the radio's way). "" = both.
        pluginManager->trigger("ota:update:start", "component", updateComponent);
        ota->update(updateComponent != "display", updateComponent != "controller");
        pluginManager->trigger("ota:update:end");
        updating = false;
    }
    if (devOtaPending.load()) {
        // Copied out before the flag is cleared, so the producer cannot
        // overwrite the buffer under the download.
        String url(devOtaUrl);
        devOtaPending.store(false);
        // The same event the release path fires, and "display" is what makes
        // DefaultUI stop the panel: the RGB peripheral streaming a framebuffer
        // out of PSRAM and a 6 MB download into flash contend on the same bus,
        // and the download is the one that aborts.
        pluginManager->trigger("ota:update:start", "component", String("display"));
        ota->updateFromUrl(url);
        // Only reached when it failed; a good image restarts inside the call.
        pluginManager->trigger("ota:update:end");
    }
    if (!serverRunning) {
        return;
    }
#ifdef GAGGIMATE_SIM
    serviceHistoryQueue(); // the simulator spawns no tasks
#endif
    // HistoryJob is defined further down, so the websocket history answers
    // are sent through a pointer that setupServer sets.
    if (g_historyWsDeliver != nullptr) {
        g_historyWsDeliver(this);
    }
    const unsigned long now = millis();
    // Skip the (blocking, TLS) update check while a process is active: a brew/steam/grind
    // must not have the control loop stalled for the duration of the handshake, nor compete
    // with it for memory. isActive() is the reliable "a process is running" signal. Subtraction
    // (not now > last + interval) keeps the interval check millis()-rollover-safe.
    if (!controller->isActive() && (lastUpdateCheck == 0 || now - lastUpdateCheck > UPDATE_CHECK_INTERVAL)) {
        // And not in AP mode: no route to GitHub, so the mbedtls handshake would
        // spend ~30 KB of internal heap to fail. That spike alone is enough to
        // make a later BLE_INIT allocation fail.
        if (!apMode) {
            ota->checkForUpdates();
            pluginManager->trigger("ota:update:status", "value", ota->isUpdateAvailable());
            updateOTAStatus(ota->getCurrentVersion());
        }
        lastUpdateCheck = now;
    }
    if (now > lastHardwareScaleDiagnostic + HARDWARE_SCALE_DIAGNOSTIC_PERIOD && !ws.getClients().empty() &&
        controller->getSystemInfo().capabilities.hwScale) {
        lastHardwareScaleDiagnostic = now;
        hardwareScaleDiagnosticDoc.clear();
        hardwareScaleDiagnosticDoc["tp"] = "evt:hardware-scale";
        hardwareScaleDiagnosticDoc["c1"] = controller->getHardwareScaleCell1Weight();
        hardwareScaleDiagnosticDoc["c2"] = controller->getHardwareScaleCell2Weight();
        hardwareScaleDiagnosticDoc["c1v"] = controller->isHardwareScaleCell1Valid();
        hardwareScaleDiagnosticDoc["c2v"] = controller->isHardwareScaleCell2Valid();
        // The factors these readings were produced with: the stored values are
        // what setScaleFactors() sent the controller. The web calibration
        // divides the reading by this pair, never by its unsaved draft
        // (gm-bzu.10).
        {
            const Settings &s = controller->getSettings();
            hardwareScaleDiagnosticDoc["sf1"] = s.getScaleFactor1();
            hardwareScaleDiagnosticDoc["sf2"] = s.getScaleFactor2();
        }
        broadcastJson(hardwareScaleDiagnosticDoc);
    }
    if (now > lastStatus + STATUS_PERIOD && !ws.getClients().empty()) {
        lastStatus = now;
        statusDoc.clear();
        statusDoc["tp"] = "evt:status";
        statusDoc["ct"] = controller->getCurrentTemp();
        statusDoc["tt"] = controller->getTargetTemp();
        statusDoc["pr"] = controller->getCurrentPressure();
        statusDoc["fl"] = controller->getCurrentPumpFlow();
        statusDoc["pt"] = controller->getTargetPressure();
        statusDoc["m"] = controller->getMode();
        statusDoc["p"] = controller->getProfileManager()->getSelectedProfile().label;
        statusDoc["puid"] = controller->getProfileManager()->getSelectedProfile().id;
        statusDoc["cp"] = controller->getSystemInfo().capabilities.pressure;
        statusDoc["cd"] = controller->getSystemInfo().capabilities.dimming;
        statusDoc["gp"] = controller->getSystemInfo().capabilities.hasAddon(7);
        statusDoc["hs"] = controller->getSystemInfo().capabilities.hwScale;
        statusDoc["scaleSource"] = controller->getActiveScaleSourceName();
        statusDoc["tw"] = profileManager->getSelectedProfile().getTotalVolume(); // total target weight for the process
        statusDoc["bta"] = controller->isVolumetricAvailable() ? 1 : 0;
        statusDoc["bt"] =
            controller->isVolumetricAvailable() && controller->getProfileManager()->getSelectedProfile().isVolumetric() ? 1 : 0;
        statusDoc["btd"] = profileManager->getSelectedProfile().getTotalDuration();
        statusDoc["led"] = controller->getSystemInfo().capabilities.ledControl;
        statusDoc["gtd"] = controller->getTargetGrindDuration();
        statusDoc["gtv"] = controller->getSettings().getTargetGrindVolume();
        statusDoc["gt"] = controller->isVolumetricAvailable() && controller->getSettings().isVolumetricTarget() ? 1 : 0;
        statusDoc["gact"] = controller->isGrindActive() ? 1 : 0;
        statusDoc["wl"] = controller->getWaterLevel();
        statusDoc["tof"] = controller->getTofDistance();
        statusDoc["rssi"] = 0;
        statusDoc["lat"] = -1; // BLE round-trip latency (ms); -1 = not yet measured
        statusDoc["pw"] = controller->getCurrentPumpPower();
        statusDoc["hp"] = controller->getCurrentHeaterPower();

        // Null until the BLE transport has built a client; this status frame is
        // broadcast on a timer and can easily precede that (a browser attached
        // before the controller link comes up, or a build with BLE disabled).
        NimBLEClient *bleClient = controller->getClientController()->getClient();
        if (bleClient != nullptr && bleClient->isConnected()) {
            statusDoc["rssi"] = bleClient->getRssi();
        }
        if (controller->getClientController()->hasLatency()) {
            statusDoc["lat"] = controller->getClientController()->getLatencyMs();
        }

        bool bleConnected = BLEScales.isConnected();
        // Add Bluetooth scale weight information
        statusDoc["bw"] = this->currentWeight;
        statusDoc["cw"] = this->currentWeight;
        statusDoc["bc"] = bleConnected; // bluetooth scale connected status
        // Scale battery — only surfaced when the driver reports one and the
        // value isn't the UNKNOWN sentinel (255). UI omits the battery pill
        // entirely when `sbat` is absent, so disconnected/unknown scales don't
        // render a stale stub.
        if (bleConnected && BLEScales.hasBatteryLevel()) {
            const uint8_t pct = BLEScales.getBatteryLevel();
            if (pct != REMOTE_SCALES_BATTERY_UNKNOWN) {
                statusDoc["sbat"] = pct;
            }
        }

        // Deref under the process lock — other tasks delete the process at any time (GM-147).
        // Released before broadcastJson so the ws send never runs under the lock.
        std::unique_lock<std::recursive_mutex> processGuard(controller->getProcessLock());
        Process *process = controller->getProcess();
        if (process == nullptr) {
            process = controller->getLastProcess();
        }
        if (process != nullptr) {
            auto pObj = statusDoc["process"].to<JsonObject>();
            pObj["a"] = controller->isActive() ? 1 : 0;
            statusDoc["pkr"] = controller->getCurrentPuckResistance();
            statusDoc["pf"] = controller->getCurrentPuckFlow();
            statusDoc["tf"] = controller->getTargetFlow();
            if (process->getType() == MODE_BREW) {
                auto *brew = static_cast<BrewProcess *>(process);
                unsigned long ts = brew->isActive() && controller->isActive() ? millis() : brew->finished;
                pObj["s"] = brew->currentPhase.phase == PhaseType::PHASE_TYPE_BREW ? "brew" : "infusion";
                pObj["l"] = brew->isActive() ? brew->currentPhase.name.c_str() : "Finished";
                pObj["e"] = ts - brew->processStarted;
                const bool isVolumetric = brew->target == ProcessTarget::VOLUMETRIC && brew->currentPhase.hasVolumetricTarget() &&
                                          controller->isVolumetricAvailable();
                pObj["tt"] = isVolumetric ? "volumetric" : "time";
                if (isVolumetric) {
                    Target t = brew->currentPhase.getVolumetricTarget();
                    pObj["pt"] = t.value;
                    pObj["pp"] = brew->currentVolume;
                } else {
                    pObj["pt"] = brew->getPhaseDuration();
                    pObj["pp"] = ts - brew->currentPhaseStarted;
                }
            } else if (process->getType() == MODE_GRIND) {
                auto *grind = static_cast<GrindProcess *>(process);
                unsigned long ts = grind->isActive() && controller->isActive() ? millis() : grind->finished;
                pObj["s"] = "grind";
                pObj["l"] = grind->isActive() ? "Grinding" : "Finished";
                pObj["e"] = ts - grind->started;
                const bool isVolumetric = grind->target == ProcessTarget::VOLUMETRIC && controller->isVolumetricAvailable();
                pObj["tt"] = isVolumetric ? "volumetric" : "time";
                if (isVolumetric) {
                    pObj["pt"] = grind->grindVolume;
                    pObj["pp"] = grind->currentVolume;
                } else {
                    pObj["pt"] = grind->time;
                    pObj["pp"] = ts - grind->started;
                }
            }
        }
        processGuard.unlock();

        broadcastJson(statusDoc);
    }
    if (now > lastCleanup + CLEANUP_PERIOD) {
        lastCleanup = now;
        ws.cleanupClients();
    }
    if (now > lastDns + DNS_PERIOD && dnsServer != nullptr) {
        lastDns = now;
        dnsServer->processNextRequest();
    }
}

// Linear lookup over the embedded asset table (~60 entries) — a couple of
// strcmps per request, negligible next to the network round-trip.
static const WebAsset *findWebAsset(const String &path) {
    for (size_t i = 0; i < WEB_ASSETS_COUNT; i++) {
        if (path == WEB_ASSETS[i].path) {
            return &WEB_ASSETS[i];
        }
    }
    return nullptr;
}

// Maps a request URL to an embedded asset, filling `path` with the path that
// was actually resolved. Returns nullptr for a genuine 404.
static const WebAsset *resolveWebAsset(const String &url, String &path) {
    path = url;
    if (path.isEmpty() || path == "/") {
        path = WEB_UI_INDEX_PATH;
    }
    const WebAsset *asset = findWebAsset(path);
    if (asset == nullptr && !path.startsWith("/assets/")) {
        // SPA client-side routes (e.g. /settings, /profiles) aren't real files —
        // fall back to index.html. A miss under /assets/ is a genuine 404, not a
        // route, so it is not rewritten.
        asset = findWebAsset(WEB_UI_INDEX_PATH);
    }
    return asset;
}

// Every segment lwIP hands the WiFi driver becomes a ~1630-byte copy in
// DMA-capable internal DRAM until it is on the air (the pbufs live in PSRAM
// and the MAC cannot DMA from there), and a connection keeps up to
// TCP_SND_BUF/MSS of them in flight. Big assets are the only responses that
// stay in flight for long, so with N browsers cold-loading at once the
// in-flight copies scale as N x (JS + CSS + logo) x 4 segments: three tabs
// drained the 50 kB pool to under 400 bytes and logged 51 failed WiFi
// allocations (2026-09-04). Capping how many big assets stream at once
// bounds that term regardless of tab count; the rest of the traffic is
// short JSON and websocket frames that finish within a round trip. Extra
// requests are parked with request continuation and resumed as slots free
// up, so nothing is refused. Everything here runs on the async_tcp task.
//
// The count is a ceiling, not the whole gate: the pool it protects is also
// what the animation's hot slab and the radios' own bursts come out of, so
// three streams that fit on an idle boot do not fit on every boot. A second
// or third stream is admitted only while the DMA-capable pool is above
// kAssetGateDmaFloor; the first is always admitted so a parked request can
// never wait on a pool that nothing is draining. The floor is one stream's
// in-flight copies (four segments, ~6.5 kB) on top of the 15-18 kB at which
// the WiFi driver started failing its own allocations before the gate.
static constexpr uint8_t kMaxAssetStreams = 3;
static constexpr uint32_t kAssetGateMinBytes = 8192;
static constexpr uint32_t kAssetGateDmaFloor = 20 * 1024;

bool WebUIPlugin::assetSlotFree() const {
    if (assetStreams == 0) {
        return true;
    }
    if (assetStreams >= kMaxAssetStreams) {
        return false;
    }
    return heap_caps_get_free_size(MALLOC_CAP_DMA) >= kAssetGateDmaFloor;
}

void WebUIPlugin::serveWebAsset(AsyncWebServerRequest *request) {
    String path;
    const WebAsset *asset = resolveWebAsset(request->url(), path);
    if (asset == nullptr) {
        request->send(404, "text/plain", "Not found");
        return;
    }
    if (asset->length >= kAssetGateMinBytes && !assetSlotFree()) {
        assetQueue.push_back(request->pause());
        return;
    }
    startAssetStream(request);
}

void WebUIPlugin::startAssetStream(AsyncWebServerRequest *request) {
    String path;
    const WebAsset *asset = resolveWebAsset(request->url(), path);
    if (asset == nullptr) {
        request->send(404, "text/plain", "Not found");
        return;
    }
    if (asset->length >= kAssetGateMinBytes) {
        assetStreams++;
        // Fires on normal completion too: the server closes the client once a
        // response has been acked in full (AsyncWebServerRequest::_onAck), and
        // AsyncTCP runs the disconnect callback on errors as well as closes,
        // so every slot taken here is given back.
        request->onDisconnect([this]() {
            if (assetStreams > 0) {
                assetStreams--;
            }
            drainAssetQueue();
        });
    }

    // Serve straight from the memory-mapped flash blob — no copy into RAM, no
    // filesystem read. AsyncProgmemResponse streams from the pointer in chunks.
    AsyncWebServerResponse *response =
        request->beginResponse(200, asset->contentType, gWebUiBlobStart + asset->offset, asset->length);
    if (asset->gzip) {
        response->addHeader("Content-Encoding", "gzip");
    }
    // Content-hashed build assets (/assets/<hash>.js) never change for a given URL — cache them forever. index.html and
    // other unhashed files must revalidate so a new build is picked up after an update. [GM-83]
    if (path.startsWith("/assets/")) {
        response->addHeader("Cache-Control", "public, max-age=31536000, immutable");
    } else {
        response->addHeader("Cache-Control", "no-cache");
    }
    request->send(response);
}

void WebUIPlugin::drainAssetQueue() {
    while (assetSlotFree() && !assetQueue.empty()) {
        auto waiting = assetQueue.front().lock();
        assetQueue.pop_front();
        if (!waiting) {
            continue; // the browser gave up while parked; the request is already gone
        }
        startAssetStream(waiting.get());
    }
}

// ---- /api/history/* -------------------------------------------------------
//
// The shot history lives as one file per shot in /h on the SD card (LittleFS
// without a card). FAT has no directory index: every open, stat or exists()
// walks /h entry by entry, one 512 B sector read per step, and with about
// 3,000 shots on the bench card a walk is about 2 s and FS::open (a stat,
// then the open) 4 to 5 s. The AsyncStaticWebHandler that used to serve this
// prefix probed for "<name>.gz", then "<name>", then opened the file, on the
// web server's own task, so a request for a name that does not exist
// (recent.bin, which is computed, not stored) was two full walks and the 5 s
// task watchdog aborted async_tcp: the board rebooted on the first Home page
// load of the web UI with a big history (2026-09-09).
//
// Here the handler validates the name, parks the request and queues a job.
// The worker task does the filesystem work and marks the job done; it never
// touches the request. AsyncTCP polls the parked client every 500 ms on the
// async_tcp task, and that poll is where the result is picked up and the
// response sent, so the response runs on the one task the library expects.
// A first version sent from the worker, the way the library's
// RequestContinuation example does, and under a burst of five requests the
// ack path finished the response underneath the worker, deleted the client
// and faulted in write_send_buffs (LoadProhibited, 2026-09-09). One file at
// a time, so a burst queues rather than piling walks on the card.
//
// Nothing that touches the card runs on async_tcp, because the card's volume
// lock is held by whichever task is inside a walk: a read or a close on
// async_tcp waits for the worker's walk to finish. So the worker also reads
// the file, in 16 KB chunks into a PSRAM buffer of two slots (the next chunk
// is read while the filler copies out of the current one), and closes it
// (a handle released on another task is handed back to the worker). The
// filler only copies. The websocket's history requests (notes load, notes
// save, shot delete) are jobs on the same queue; their answers are sent from
// the plugin loop, the task that sends the other websocket messages.

namespace {

// True for "HH:MM" with a 24-hour hour and a two-digit minute in range,
// the only form the schedule editor and the wakeup tick understand.
bool isScheduleTime(const String &t) {
    if (t.length() != 5 || t.charAt(2) != ':') {
        return false;
    }
    for (int i : {0, 1, 3, 4}) {
        if (t.charAt(i) < '0' || t.charAt(i) > '9') {
            return false;
        }
    }
    const int hour = (t.charAt(0) - '0') * 10 + (t.charAt(1) - '0');
    const int minute = (t.charAt(3) - '0') * 10 + (t.charAt(4) - '0');
    return hour <= 23 && minute <= 59;
}

enum HistoryKind : uint8_t { kHistFile = 0, kHistRecent = 1, kHistWs = 2 };
constexpr size_t kHistoryQueueCap = 8;
constexpr size_t kHistoryChunk = 16 * 1024;
constexpr long kRecentLimitMax = 50;

// index.bin, recent.bin, or up to 12 digits followed by .slog or .json.
bool historyName(const String &name, HistoryKind &kind) {
    if (name == "index.bin") {
        kind = kHistFile;
        return true;
    }
    if (name == "recent.bin") {
        kind = kHistRecent;
        return true;
    }
    int dot = name.indexOf('.');
    if (dot < 1 || dot > 12) {
        return false;
    }
    for (int i = 0; i < dot; i++) {
        if (name[i] < '0' || name[i] > '9') {
            return false;
        }
    }
    const String ext = name.substring(dot);
    if (ext != ".slog" && ext != ".json") {
        return false;
    }
    kind = kHistFile;
    return true;
}

const char *historyContentType(const String &name) {
    return name.endsWith(".json") ? "application/json" : "application/octet-stream";
}

class HistoryHandler : public AsyncWebHandler {
  public:
    explicit HistoryHandler(WebUIPlugin *plugin) : plugin(plugin) {}
    bool canHandle(AsyncWebServerRequest *request) const override {
        return request->method() == HTTP_GET && request->url().startsWith("/api/history/");
    }
    void handleRequest(AsyncWebServerRequest *request) override { plugin->handleHistoryRequest(request); }

  private:
    WebUIPlugin *plugin;
};

} // namespace

struct WebUIPlugin::HistoryJob {
    WebUIPlugin *plugin = nullptr;
    AsyncWebServerRequestPtr request;
    String name;
    uint8_t kind = kHistFile;
    uint8_t limit = 0;
    // Set by the worker when the result below is complete; read on async_tcp.
    std::atomic<bool> done{false};
    bool sent = false;
    File file;                         // kHistFile on the simulator: the opened file, or none
    FILE *fp = nullptr;                // kHistFile on the device: the opened file, or null
    size_t size = 0;                   // its length
    ShotIndexEntry *entries = nullptr; // kHistRecent: ps_malloc'd, `count` valid
    size_t count = 0;

    // kHistFile on the device: the file in chunks of kHistoryChunk, read by the
    // worker into two PSRAM slots. Chunk k lives in slot k & 1. `chunk` is the
    // chunk a slot holds (-1: none), written by the worker only; `want` is the
    // chunk the filler asked for, written by the filler only. The filler reads
    // a slot only while `chunk` says it holds the chunk it needs, and asks for
    // the next chunk into the other slot, so the worker never writes the slot
    // being copied from (the library's filler index only moves forward).
    struct Slot {
        std::atomic<int32_t> chunk{-1};
        std::atomic<int32_t> want{-1};
        size_t len = 0;
    };
    uint8_t *chunkBuf = nullptr;
    Slot slots[2];
    bool opened = false;       // the first read is done; a queued entry is a refill
    bool oom = false;          // no PSRAM for the chunk buffer
    bool refillQueued = false; // under historyLock

    // kHistWs: the websocket request and its answer.
    uint32_t wsClient = 0;
    JsonDocument wsRequest{&psramAllocator};
    JsonDocument wsResponse{&psramAllocator};

    // Worker state shared by every job. The plugin is a singleton.
    static std::atomic<bool> workerUp;
    static TaskHandle_t worker;
    static std::mutex auxLock;                             // guards the two lists below
    static std::deque<FILE *> toClose;                     // handles to close on the worker
    static std::deque<std::shared_ptr<HistoryJob>> wsDone; // answers for the plugin loop

    static void wakeWorker() {
#ifndef GAGGIMATE_SIM
        if (worker != nullptr) {
            xTaskNotifyGive(worker);
        }
#endif
    }
    static void closeDeferred() {
        for (;;) {
            FILE *f = nullptr;
            {
                std::lock_guard<std::mutex> guard(auxLock);
                if (toClose.empty()) {
                    return;
                }
                f = toClose.front();
                toClose.pop_front();
            }
            fclose(f);
        }
    }

    // Reads chunk `k` into its slot. Worker only.
    void readChunk(int32_t k) {
        Slot &slot = slots[k & 1];
        const size_t off = static_cast<size_t>(k) * kHistoryChunk;
        size_t len = 0;
        if (fp != nullptr && off < size && fseek(fp, static_cast<long>(off), SEEK_SET) == 0) {
            const size_t want = std::min(kHistoryChunk, size - off);
            len = fread(chunkBuf + (k & 1) * kHistoryChunk, 1, want, fp);
        }
        slot.len = len;
        slot.chunk.store(k, std::memory_order_release);
    }

    // Filler side (async_tcp): ask the worker for chunk `k`.
    void requestChunk(int32_t k) {
        if (static_cast<size_t>(k) * kHistoryChunk >= size) {
            return;
        }
        Slot &slot = slots[k & 1];
        if (slot.chunk.load(std::memory_order_acquire) == k || slot.want.load(std::memory_order_relaxed) == k) {
            return;
        }
        slot.want.store(k, std::memory_order_release);
        std::shared_ptr<HistoryJob> self = selfRef.lock();
        if (!self) {
            return;
        }
        {
            std::lock_guard<std::mutex> guard(plugin->historyLock);
            if (refillQueued) {
                return;
            }
            refillQueued = true;
            // A refill goes ahead of the new requests: the handle is open, so
            // it is sector reads, not a directory walk.
            plugin->historyQueue.push_front(std::move(self));
        }
        wakeWorker();
    }
    std::weak_ptr<HistoryJob> selfRef;

    // A websocket history request (notes load, notes save, shot delete). Each
    // is one or more walks of /h, seconds on a big card, so it goes to the
    // worker like a file request. False when it could not be queued, with the
    // answer to send at once in `errorResponse`. Runs on async_tcp.
    static bool enqueueWs(WebUIPlugin *plugin, uint32_t clientId, JsonDocument &request, JsonDocument &errorResponse);
    // Sends the finished websocket answers. Runs on the plugin loop.
    static void deliverWs(WebUIPlugin *plugin);

    ~HistoryJob() {
        if (file) {
            file.close();
        }
        if (fp != nullptr) {
#ifdef GAGGIMATE_SIM
            fclose(fp);
#else
            if (xTaskGetCurrentTaskHandle() == worker || !workerUp.load()) {
                fclose(fp);
            } else {
                // Released on async_tcp (the request's disconnect closure):
                // the close waits on the volume lock, so the worker does it.
                std::lock_guard<std::mutex> guard(auxLock);
                toClose.push_back(fp);
            }
#endif
            fp = nullptr;
            wakeWorker();
        }
        if (chunkBuf != nullptr) {
            free(chunkBuf);
        }
        if (entries != nullptr) {
            free(entries);
        }
    }
};

std::atomic<bool> WebUIPlugin::HistoryJob::workerUp{false};
TaskHandle_t WebUIPlugin::HistoryJob::worker = nullptr;
std::mutex WebUIPlugin::HistoryJob::auxLock;
std::deque<FILE *> WebUIPlugin::HistoryJob::toClose;
std::deque<std::shared_ptr<WebUIPlugin::HistoryJob>> WebUIPlugin::HistoryJob::wsDone;

namespace {
// Counts the queued jobs that are new requests; refills do not take a place.
template <typename Q> size_t newJobCount(const Q &queue) {
    size_t n = 0;
    for (const auto &j : queue) {
        if (!j->opened) {
            n++;
        }
    }
    return n;
}

// The answer to a websocket history request that never reached the worker.
void historyWsError(JsonDocument &request, JsonDocument &response, const char *error) {
    const String type = request["tp"].as<String>();
    response["tp"] = String("res:") + type.substring(4);
    response["rid"] = request["rid"].as<String>();
    response["error"] = error;
}
} // namespace

void WebUIPlugin::handleHistoryRequest(AsyncWebServerRequest *request) {
    const String name = request->url().substring(strlen("/api/history/"));
    HistoryKind kind = kHistFile;
    if (!historyName(name, kind)) {
        request->send(404, "text/plain", "Not found");
        return;
    }
    long limit = 8;
    if (kind == kHistRecent && request->hasArg("limit")) {
        limit = constrain(request->arg("limit").toInt(), 1L, kRecentLimitMax);
    }
    if (!HistoryJob::workerUp.load()) {
        // No worker was created (see setupServer): nothing would ever take the job.
        request->send(503, "text/plain", "History unavailable");
        return;
    }
    std::lock_guard<std::mutex> guard(historyLock);
    if (newJobCount(historyQueue) >= kHistoryQueueCap) {
        histDropped++;
        request->send(503, "text/plain", "History busy, retry");
        return;
    }
    auto job = std::make_shared<HistoryJob>();
    job->selfRef = job;
    job->plugin = this;
    job->name = name;
    job->kind = kind;
    job->limit = static_cast<uint8_t>(limit);
    job->request = request->pause();
#ifndef GAGGIMATE_SIM
    // Replaces the request's own poll handler for this client, which does
    // nothing while the request is paused; historyPollCb stands in for it
    // once the response is on its way. The closure keeps the job alive for
    // as long as the request; the queue holds the other reference.
    request->client()->onPoll(historyPollCb, job.get());
    request->onDisconnect([job]() {});
#endif
    historyQueue.push_back(std::move(job));
    if (historyQueue.size() > histQueueMax) {
        histQueueMax = historyQueue.size();
    }
    HistoryJob::wakeWorker();
}

// Worker side: the filesystem work, and nothing that touches the request.
// On the simulator this runs from the plugin loop (it has no tasks) and the
// same thread also serves the web, so the response is sent right here.
void WebUIPlugin::serviceHistoryQueue() {
    for (;;) {
        std::shared_ptr<HistoryJob> job;
        {
            std::lock_guard<std::mutex> guard(historyLock);
            if (historyQueue.empty()) {
                return;
            }
            job = std::move(historyQueue.front());
            historyQueue.pop_front();
        }
        const unsigned long t0 = micros();
        if (job->kind == kHistWs) {
            ShotHistory.handleRequest(job->wsRequest, job->wsResponse);
            job->wsRequest.clear();
        } else if (job->opened) {
#ifndef GAGGIMATE_SIM
            // A refill: read every chunk the filler has asked for. Checked
            // again under the lock before the flag drops, so a request made
            // while the reads ran is not lost (requestChunk sets `want` first,
            // then looks at the flag under the same lock).
            for (;;) {
                const bool live = static_cast<bool>(job->request.lock());
                if (live) {
                    for (auto &slot : job->slots) {
                        const int32_t want = slot.want.load(std::memory_order_acquire);
                        if (want >= 0 && want != slot.chunk.load(std::memory_order_relaxed)) {
                            job->readChunk(want);
                        }
                    }
                }
                std::lock_guard<std::mutex> guard(historyLock);
                bool settled = true;
                if (live) {
                    for (auto &slot : job->slots) {
                        const int32_t want = slot.want.load(std::memory_order_acquire);
                        if (want >= 0 && want != slot.chunk.load(std::memory_order_relaxed)) {
                            settled = false;
                        }
                    }
                }
                if (settled) {
                    job->refillQueued = false;
                    break;
                }
            }
#endif
            continue; // a refill is not a request: no timing, no second completion
        } else if (historyFs != nullptr && job->request.lock()) {
            if (job->kind == kHistRecent) {
                job->entries = static_cast<ShotIndexEntry *>(ps_malloc(job->limit * sizeof(ShotIndexEntry)));
                if (job->entries != nullptr) {
                    job->count = ShotHistory.readRecentEntries(job->entries, job->limit);
                }
            } else {
#ifdef GAGGIMATE_SIM
                job->file = historyFs->open("/h/" + job->name, "r");
#else
                // The slow part: the directory walk inside the open. Through
                // the POSIX layer it is one walk; FS::open does a stat first
                // and so walks twice (4.8 s against 2.5 s for a shot file on
                // the bench card, 2026-09-09). The size comes from the open
                // handle, not from a stat.
                const char *mount = historyFs->mountpoint();
                if (mount != nullptr) {
                    const String full = String(mount) + "/h/" + job->name;
                    job->fp = fopen(full.c_str(), "r");
                    if (job->fp != nullptr) {
                        struct stat st {};
                        if (fstat(fileno(job->fp), &st) == 0 && S_ISREG(st.st_mode)) {
                            job->size = static_cast<size_t>(st.st_size);
                        } else {
                            fclose(job->fp);
                            job->fp = nullptr;
                        }
                    }
                    // The first two chunks, so the response starts from memory
                    // and the filler never reads the card. A file of one chunk
                    // or less gets a buffer of its own size.
                    if (job->fp != nullptr) {
                        const size_t bufLen = std::min(job->size, 2 * kHistoryChunk);
                        job->chunkBuf = static_cast<uint8_t *>(ps_malloc(bufLen > 0 ? bufLen : 1));
                        if (job->chunkBuf == nullptr) {
                            fclose(job->fp);
                            job->fp = nullptr;
                            job->size = 0;
                            job->oom = true;
                        } else {
                            job->readChunk(0);
                            if (job->size > kHistoryChunk) {
                                job->readChunk(1);
                            }
                        }
                    }
                    job->opened = true;
                }
#endif
            }
        }
        const unsigned long us = micros() - t0;
        if (us > histOpenUsMax) {
            histOpenUsMax = us;
        }
        job->done.store(true, std::memory_order_release);
        if (job->kind == kHistWs) {
            std::lock_guard<std::mutex> guard(HistoryJob::auxLock);
            HistoryJob::wsDone.push_back(std::move(job));
            continue;
        }
#ifdef GAGGIMATE_SIM
        if (auto req = job->request.lock()) {
            job->sent = true;
            completeHistoryJob(*job, req.get());
        }
#endif
    }
}

bool WebUIPlugin::HistoryJob::enqueueWs(WebUIPlugin *plugin, uint32_t clientId, JsonDocument &request,
                                        JsonDocument &errorResponse) {
    if (!workerUp.load()) {
        historyWsError(request, errorResponse, "History unavailable");
        return false;
    }
    auto job = std::make_shared<HistoryJob>();
    job->selfRef = job;
    job->plugin = plugin;
    job->kind = kHistWs;
    job->wsClient = clientId;
    job->wsRequest.set(request);
    {
        std::lock_guard<std::mutex> guard(plugin->historyLock);
        if (newJobCount(plugin->historyQueue) >= kHistoryQueueCap) {
            plugin->histDropped++;
            historyWsError(request, errorResponse, "History busy, retry");
            return false;
        }
        plugin->historyQueue.push_back(std::move(job));
        if (plugin->historyQueue.size() > plugin->histQueueMax) {
            plugin->histQueueMax = plugin->historyQueue.size();
        }
    }
    wakeWorker();
    return true;
}

void WebUIPlugin::HistoryJob::deliverWs(WebUIPlugin *plugin) {
    for (;;) {
        std::shared_ptr<HistoryJob> job;
        {
            std::lock_guard<std::mutex> guard(auxLock);
            if (wsDone.empty()) {
                return;
            }
            job = std::move(wsDone.front());
            wsDone.pop_front();
        }
        // A client that left while its job ran is simply not found.
        plugin->ws.text(job->wsClient, toWsBuffer(job->wsResponse));
        plugin->histServed++;
    }
}

// Sends the finished job's response. Runs on the web server's task (the
// client poll on the device, the plugin loop on the simulator).
void WebUIPlugin::completeHistoryJob(HistoryJob &job, AsyncWebServerRequest *request) {
    if (historyFs == nullptr) {
        request->send(503, "text/plain", "History unavailable");
        return;
    }
    if (job.kind == kHistRecent) {
        if (job.entries == nullptr) {
            request->send(500, "text/plain", "Out of memory");
            return;
        }
        // The most recent non-deleted shots, newest first, as a regular shot
        // index (SIDX header + entries): the same binary format as index.bin,
        // truncated, so clients reuse the index.bin parser.
        ShotIndexHeader header{};
        header.magic = SHOT_INDEX_MAGIC;
        header.version = SHOT_INDEX_VERSION;
        header.entrySize = SHOT_INDEX_ENTRY_SIZE;
        header.entryCount = job.count;
        header.nextId = 0; // meaningless for a partial view
        AsyncResponseStream *response = request->beginResponseStream("application/octet-stream");
        response->addHeader("Cache-Control", "no-store");
        response->write(reinterpret_cast<const uint8_t *>(&header), sizeof(header));
        response->write(reinterpret_cast<const uint8_t *>(job.entries), job.count * sizeof(ShotIndexEntry));
        request->send(response);
        histServed++;
        return;
    }
#ifdef GAGGIMATE_SIM
    if (!job.file || job.file.isDirectory()) {
        request->send(404, "text/plain", "Not found");
        return;
    }
    const String path = "/h/" + job.name;
    AsyncWebServerResponse *response = request->beginResponse(job.file, path, historyContentType(job.name));
    response->addHeader("Cache-Control", "no-store");
    // The response holds the file now; drop this handle without closing it
    // (File is shared, close() would close it for the response too).
    job.file = File();
#else
    if (job.fp == nullptr) {
        request->send(404, "text/plain", "Not found");
        return;
    }
    if (job.oom) {
        request->send(500, "text/plain", "Out of memory");
        return;
    }
    // The filler runs on async_tcp as the client acks and only copies out of
    // the chunk slots the worker filled; it never touches the card. When the
    // chunk it needs is not in yet it asks for it and returns TRY_AGAIN; the
    // library tries again on the next ack or on historyPollCb. The job
    // outlives the response: the request's disconnect closure holds it, and
    // its destructor hands the handle to the worker to close.
    HistoryJob *jobp = &job;
    AsyncWebServerResponse *response = request->beginResponse(
        historyContentType(job.name), job.size, [jobp](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
            if (index >= jobp->size) {
                return 0;
            }
            const int32_t k = static_cast<int32_t>(index / kHistoryChunk);
            HistoryJob::Slot &slot = jobp->slots[k & 1];
            if (slot.chunk.load(std::memory_order_acquire) != k) {
                jobp->requestChunk(k);
                return RESPONSE_TRY_AGAIN;
            }
            // Read ahead: the next chunk goes into the slot this one's
            // predecessor used, which the filler has finished with.
            jobp->requestChunk(k + 1);
            const size_t inChunk = index - static_cast<size_t>(k) * kHistoryChunk;
            if (inChunk >= slot.len) {
                return 0; // the read came up short: end the body here
            }
            const size_t got = std::min(maxLen, slot.len - inChunk);
            memcpy(buffer, jobp->chunkBuf + (k & 1) * kHistoryChunk + inChunk, got);
            return got;
        });
    response->addHeader("Cache-Control", "no-store");
#endif
    request->send(response);
    histServed++;
}

#ifndef GAGGIMATE_SIM
void WebUIPlugin::historyPollCb(void *arg, AsyncClient *client) {
    auto *job = static_cast<HistoryJob *>(arg);
    if (!job->done.load(std::memory_order_acquire)) {
        return;
    }
    auto request = job->request.lock();
    if (!request) {
        return;
    }
    if (!job->sent) {
        job->sent = true;
        job->plugin->completeHistoryJob(*job, request.get());
        return;
    }
    // What AsyncWebServerRequest::_onPoll does: nudge a response whose acks
    // have stopped while the client can take more.
    AsyncWebServerResponse *response = request->getResponse();
    if (response != nullptr && client->canSend()) {
        response->_ack(request.get(), 0, 0);
    }
}
#endif

void WebUIPlugin::historyTaskFn(void *param) {
    auto *self = static_cast<WebUIPlugin *>(param);
    for (;;) {
        HistoryJob::closeDeferred();
        self->serviceHistoryQueue();
#ifdef GAGGIMATE_SIM
        vTaskDelay(pdMS_TO_TICKS(10));
#else
        // Woken at once by a new job, a refill request or a handle to close;
        // the timeout is only a backstop.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
#endif
    }
}

void WebUIPlugin::setupServer() {
    server.on("/connecttest.txt", [](AsyncWebServerRequest *request) {
        request->redirect("http://logout.net");
    }); // windows 11 captive portal workaround
    server.on("/wpad.dat", [](AsyncWebServerRequest *request) {
        request->send(404);
    }); // Honestly don't understand what this is but a 404 stops win 10 keep calling this repeatedly and panicking the esp32
        // :)
    server.on("/generate_204",
              [](AsyncWebServerRequest *request) { request->redirect(LOCAL_URL); }); // android captive portal redirect
    server.on("/redirect", [](AsyncWebServerRequest *request) { request->redirect(LOCAL_URL); });            // microsoft redirect
    server.on("/hotspot-detect.html", [](AsyncWebServerRequest *request) { request->redirect(LOCAL_URL); }); // apple call home
    server.on("/canonical.html",
              [](AsyncWebServerRequest *request) { request->redirect(LOCAL_URL); });       // firefox captive portal call home
    server.on("/success.txt", [](AsyncWebServerRequest *request) { request->send(200); }); // firefox captive portal call home
    server.on("/ncsi.txt", [](AsyncWebServerRequest *request) { request->redirect(LOCAL_URL); }); // windows call home
    server.on("/api/settings", [this](AsyncWebServerRequest *request) { handleSettings(request); });
    server.on("/api/ota/info", HTTP_GET, [this](AsyncWebServerRequest *request) { handleOtaInfo(request); });
    server.on("/api/ota/dev", HTTP_POST, [this](AsyncWebServerRequest *request) { handleDevOta(request); });
    setupDebugEndpoints();

    server.on("/api/status", [this](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        JsonDocument doc(&psramAllocator);
        doc["mode"] = controller->getMode();
        doc["tt"] = controller->getTargetTemp();
        doc["ct"] = controller->getCurrentTemp();
        serializeJson(doc, *response);
        request->send(response);
    });
    server.on("/api/scales/list", [this](AsyncWebServerRequest *request) { handleBLEScaleList(request); });
    server.on("/api/scales/connect", [this](AsyncWebServerRequest *request) { handleBLEScaleConnect(request); });
    server.on("/api/scales/scan", [this](AsyncWebServerRequest *request) { handleBLEScaleScan(request); });
    server.on("/api/scales/info", [this](AsyncWebServerRequest *request) { handleBLEScaleInfo(request); });
    server.on("/api/debug/heap/detail", [this](AsyncWebServerRequest *request) { handleDebugHeap(request); });
    FS *fs = &LittleFS;
    if (controller->isSDCard()) {
        fs = &SD_MMC;
    }
    // /api/history/index.bin, recent.bin and <id>.slog/.json. Every open of a
    // file in /h is queued for the history worker (handleHistoryRequest); the
    // static handler this replaces probed the directory up to four times per
    // request on the async_tcp task and tripped the task watchdog.
    historyFs = fs;
    server.addHandler(new HistoryHandler(this));
    g_historyWsDeliver = &HistoryJob::deliverWs;
#ifndef GAGGIMATE_SIM
    {
        // The worker only reads through the filesystem. On an SD card that
        // never disables the flash cache, so its stack can live in PSRAM (the
        // internal pool is what the web UI dies of, see CLAUDE.md); on
        // LittleFS a read is a flash operation, which runs with the cache
        // off and needs an internal stack. The task never deletes itself.
        // 6 KB: besides the opens, the worker runs the notes save (JSON
        // serialisation, the .tmp write and the rename) and the index updates.
        constexpr uint32_t kHistoryStack = 6144;
        TaskHandle_t handle = nullptr;
        if (controller->isSDCard()) {
            xTaskCreatePinnedToCoreWithCaps(historyTaskFn, "HistServe", kHistoryStack, this, 1, &handle, 0,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (handle == nullptr) {
            xTaskCreatePinnedToCore(historyTaskFn, "HistServe", kHistoryStack, this, 1, &handle, 0);
        }
        if (handle == nullptr) {
            ESP_LOGE("WebUIPlugin", "history worker not created; /api/history/* and the websocket history requests will "
                                    "answer 503 (History unavailable)");
        } else {
            HistoryJob::worker = handle;
            HistoryJob::workerUp.store(true);
        }
    }
#else
    HistoryJob::workerUp.store(true); // the simulator's plugin loop is the worker
#endif
    server.on("/api/core-dump", HTTP_GET, [this](AsyncWebServerRequest *request) { handleCoreDumpDownload(request); });
    // The web UI is embedded in firmware flash and served from the memory-mapped blob (see serveWebAsset). It is no
    // longer in LittleFS, so OTA never touches the partition holding profiles/shots. The catch-all onNotFound handles
    // every path not claimed by an explicit server.on()/api route above. [GM-106]
    server.onNotFound([this](AsyncWebServerRequest *request) { serveWebAsset(request); });
    ws.onEvent(
        [this](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
            if (type == WS_EVT_CONNECT) {
                // Close (and let the browser reconnect) a client whose send
                // queue backs up, instead of keeping it open. With it kept open
                // (false), a client that stalls under load — e.g. while the UI
                // is fetching many shot files for statistics — never has its
                // queued frames / AsyncTCP buffers reclaimed, so they accumulate
                // in internal DRAM until the whole IP stack starves (web + ICMP
                // die, no recovery). Reclaiming via close is the safer failure
                // mode. (Was the v1.8.1 behaviour.)
                client->setCloseClientOnQueueFull(true);
                ESP_LOGI("WebUIPlugin", "WebSocket client connected (%d open connections)", server->getClients().size());
            } else if (type == WS_EVT_DISCONNECT) {
                ESP_LOGI("WebUIPlugin", "WebSocket client disconnected (%d open connections)", server->getClients().size());
                rxBuffers.erase(client->id());
            } else if (type == WS_EVT_DATA) {
                handleWebSocketData(server, client, type, arg, data, len);
            }
        });
    server.addHandler(&ws);
}

void WebUIPlugin::start() {
    if (serverRunning) {
        // Already listening. The 0.0.0.0:80 listen socket survives a WiFi
        // reconnect, so re-running end()+begin() only races AsyncTCP's async
        // socket close and fails to rebind ("bind: -8, port in use"). A transient
        // STA reconnect needs nothing done here.
        return;
    }
    server.begin();
    ESP_LOGI("WebUIPlugin", "Started webserver");
    if (apMode) {
        dnsServer = new DNSServer();
        dnsServer->setTTL(3600);
        dnsServer->start(53, "*", WIFI_AP_IP);
        ESP_LOGI("WebUIPlugin", "Started catchall DNS for captive portal");
    }
    lastUpdateCheck = millis();
    serverRunning = true;
}

void WebUIPlugin::stop() {
    if (!serverRunning)
        return;
    ws.closeAll();
    server.end();
    if (dnsServer != nullptr) {
        dnsServer->stop();
        delete dnsServer;
        dnsServer = nullptr;
    }
    serverRunning = false;
    ESP_LOGI("WebUIPlugin", "WebUIPlugin stopped (wifi disconnected)");
}

void WebUIPlugin::handleWebSocketData(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg,
                                      uint8_t *data, size_t len) {

    auto *info = static_cast<AwsFrameInfo *>(arg);
    const uint32_t cid = client->id();

    if (info->index == 0) {
        auto &buf = rxBuffers[cid];
        buf.clear();
        if (info->len <= 64 * 1024) {
            buf.reserve(info->len);
        }
    }

    auto &buf = rxBuffers[cid];
    buf.append(reinterpret_cast<const char *>(data), len);
    const bool isFinal = info->final && (info->index + len) == info->len;

    // If this is the final frame of the message, process and clear
    if (isFinal) {
        if (info->opcode == WS_TEXT) {
            ESP_LOGV("WebUIPlugin", "Received request: %.*s", (int)buf.size(), buf.c_str());
            JsonDocument doc(&psramAllocator);
            DeserializationError err = deserializeJson(doc, buf.c_str());
            if (!err) {
                String msgType = doc["tp"].as<String>();
                if (msgType.startsWith("req:profiles:")) {
                    handleProfileRequest(client->id(), doc);
                } else if (msgType == "req:ota-settings") {
                    handleOTASettings(client->id(), doc);
                } else if (msgType == "req:ota-start") {
                    handleOTAStart(client->id(), doc);
                } else if (msgType == "req:autotune-start") {
                    handleAutotuneStart(client->id(), doc);
                } else if (msgType == "req:process:activate") {
                    controller->activate();
                } else if (msgType == "req:process:deactivate") {
                    controller->deactivate();
                    controller->clear();
                } else if (msgType == "req:process:clear") {
                    controller->clear();
                } else if (msgType == "req:grind:activate") {
                    controller->activateGrind();
                } else if (msgType == "req:grind:deactivate") {
                    controller->deactivateGrind();
                } else if (msgType == "req:change-grind-target") {
                    if (doc["target"].is<uint8_t>()) {
                        auto target = doc["target"].as<uint8_t>();
                        controller->getSettings().setVolumetricTarget(target);
                    }
                } else if (msgType == "req:raise-temp") {
                    controller->raiseTemp();
                } else if (msgType == "req:lower-temp") {
                    controller->lowerTemp();
                } else if (msgType == "req:raise-grind-target") {
                    controller->raiseGrindTarget();
                } else if (msgType == "req:lower-grind-target") {
                    controller->lowerGrindTarget();
                } else if (msgType == "req:raise-brew-target") {
                    controller->raiseBrewTarget();
                } else if (msgType == "req:lower-brew-target") {
                    controller->lowerBrewTarget();
                } else if (msgType == "req:change-mode") {
                    if (doc["mode"].is<uint8_t>()) {
                        auto mode = doc["mode"].as<uint8_t>();
                        controller->deactivate();
                        controller->clear();
                        controller->setMode(mode);
                    }
                } else if (msgType == "req:change-brew-target") {
                    if (doc["target"].is<uint8_t>()) {
                        auto target = doc["target"].as<uint8_t>();
                        controller->getSettings().setVolumetricTarget(target);
                    }
                } else if (msgType == "req:bganim:preview") {
                    // Gradient editor live preview: show {anim} drawn with the
                    // {stops} gradient string on the panel without saving
                    // anything. The UI task owns the theme state, so this only
                    // hands the request over (see DefaultUI::init).
                    if (doc["anim"].is<int>() && doc["stops"].is<const char *>()) {
                        Event ev;
                        ev.id = "bganim:preview";
                        ev.setInt("anim", doc["anim"].as<int>());
                        ev.setString("stops", doc["stops"].as<String>());
                        pluginManager->trigger(ev);
                    }
                } else if (msgType == "req:bganim:preview-end") {
                    pluginManager->trigger("bganim:preview-end");
                } else if (msgType == "req:history:rebuild") {
                    // Handle rebuild asynchronously - send immediate ack, progress comes via events
                    JsonDocument resp(&psramAllocator);
                    resp["tp"] = "res:history:rebuild";
                    if (doc["rid"].is<const char *>()) {
                        resp["rid"] = doc["rid"];
                    }
                    resp["msg"] = "Rebuild started";
                    client->text(toWsBuffer(resp));
                    ShotHistory.startAsyncRebuild();
                } else if (msgType.startsWith("req:history")) {
                    // Answered from the plugin loop once the worker has done
                    // the filesystem work (HistoryJob::enqueueWs).
                    JsonDocument resp(&psramAllocator);
                    if (!HistoryJob::enqueueWs(this, client->id(), doc, resp)) {
                        client->text(toWsBuffer(resp));
                    }
                } else if (msgType == "req:flush:start") {
                    handleFlushStart(client->id(), doc);
                } else if (msgType == "req:scale:tare") {
                    controller->getClientController()->tare();
                }
            }
        }
        // Done with this message
        rxBuffers.erase(cid);
    }
}

void WebUIPlugin::handleOTASettings(uint32_t clientId, JsonDocument &request) {
    if (request["update"].as<bool>()) {
        if (!request["channel"].isNull()) {
            controller->getSettings().setOTAChannel(request["channel"].as<String>() == "latest" ? "latest" : "nightly");
            ota->setReleaseUrl(RELEASE_URL + (controller->getSettings().getOTAChannel() == "latest" ? "latest" : "tag/nightly"));
            lastUpdateCheck = 0;
        }
    }
    updateOTAStatus("Checking...");
}

void WebUIPlugin::handleOTAStart(uint32_t clientId, JsonDocument &request) {
    updating = true;
    if (request["cp"].is<String>()) {
        updateComponent = request["cp"].as<String>();
    } else {
        updateComponent = "";
    }
}

// GET /api/ota/info: what is running, and whether this build can be updated
// over the air at all. Two fields carry the work (gm-thg). "sha" is
// esp_app_get_elf_sha256, which changes on every link even when nothing in the
// source did, so it is the only field that can tell a dev deploy apart from
// the image it replaced; BUILD_GIT_VERSION does not move between two builds of
// the same dirty tree. "slot" names the partition being executed, which is how
// you see the image land in the other slot. tools/ota_dev.py reads both.
void WebUIPlugin::handleOtaInfo(AsyncWebServerRequest *request) const {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    JsonDocument doc(&psramAllocator);
    doc["version"] = BUILD_GIT_VERSION;
    doc["dev_ota"] = GM_DEV_OTA != 0;
#ifndef GAGGIMATE_SIM
    char sha[17] = {};
    esp_app_get_elf_sha256(sha, sizeof(sha));
    doc["sha"] = sha;
    const esp_app_desc_t *desc = esp_app_get_description();
    if (desc != nullptr) {
        doc["built"] = String(desc->date) + " " + desc->time;
        doc["idf"] = desc->idf_ver;
    }
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
    doc["slot"] = running != nullptr ? running->label : "?";
    doc["next"] = next != nullptr ? next->label : "?";
    doc["ota"] = next != nullptr && next != running;
#else
    doc["sha"] = "";
    doc["slot"] = "sim";
    doc["ota"] = false;
#endif
    doc["uptime_ms"] = millis();
    doc["busy"] = controller->isActive() || updating || devOtaPending.load();
    serializeJson(doc, *response);
    request->send(response);
}

// POST /api/ota/dev?url=<http url>: flash the display from an image the
// developer is serving, with no GitHub release and no version check (gm-thg).
// It is the only way to change the firmware on a machine that is plumbed in,
// where the USB port is not reachable.
//
// It only records the URL. The download runs on the display task in loop(),
// where every other OTA already runs, because the flash writes block for tens
// of seconds and this handler is on async_tcp: writing from here would stall
// the web server it is answering on, and an erase every few kilobytes would
// sit under the task watchdog.
//
// Pull, not push. A POST carrying the 6 MB image would have to write flash
// from the async_tcp callback for that same reason, and the pull path is the
// one the release update already uses, down to the event that stops the panel.
// The cost is that the board has to reach the developer's host, which
// tools/ota_dev.py checks before it asks for anything.
//
// Not authenticated, and present on the production build. That is deliberate,
// and it is the trust boundary the rest of this server already assumes:
// /api/settings returns the WiFi password in cleartext over plain http to
// anyone on the LAN. Build with -DGM_DEV_OTA=0 to compile it out.
void WebUIPlugin::handleDevOta(AsyncWebServerRequest *request) {
#if GM_DEV_OTA
    if (!request->hasArg("url")) {
        request->send(400, "application/json", "{\"error\":\"url required\"}");
        return;
    }
    const String url = request->arg("url");
    // http:// or https:// only. Anything else reaches HTTPUpdate as a host it
    // cannot parse, and the failure comes back as a generic update error
    // seconds later with the panel already stopped.
    if (!url.startsWith("http://") && !url.startsWith("https://")) {
        request->send(400, "application/json", "{\"error\":\"url must be http:// or https://\"}");
        return;
    }
    if (url.length() >= kDevOtaUrlCap) {
        request->send(400, "application/json", "{\"error\":\"url too long\"}");
        return;
    }
    // A shot, a steam or a grind is running. The panel stops for the whole
    // download and the display task does nothing else while it runs, so this
    // would take the UI away mid-brew.
    if (controller->isActive()) {
        request->send(409, "application/json", "{\"error\":\"a process is running\"}");
        return;
    }
    // Test then set, with no atomic claim between them, because there is only
    // one producer: every request handler runs on the async_tcp task, so two
    // POSTs cannot be inside this function at once. The consumer is loop() on
    // the display task, and it reads the buffer only after seeing the flag,
    // which is why the flag is stored last.
    if (updating || devOtaPending.load()) {
        request->send(409, "application/json", "{\"error\":\"an update is already pending\"}");
        return;
    }
    std::strncpy(devOtaUrl, url.c_str(), kDevOtaUrlCap - 1);
    devOtaUrl[kDevOtaUrlCap - 1] = '\0';
    devOtaPending.store(true);
    ESP_LOGW("WebUIPlugin", "dev OTA queued: %s", devOtaUrl);
    request->send(200, "application/json", "{\"ok\":true}");
#else
    request->send(404, "application/json", "{\"error\":\"dev OTA not built in\"}");
#endif
}

void WebUIPlugin::handleAutotuneStart(uint32_t clientId, JsonDocument &request) {
    int testTime = request["time"].as<int>();
    int samples = request["samples"].as<int>();
    // Heater wattage drives combinedKff = TUNER_OUTPUT_SPAN / wattage on the
    // controller. 0 = "skip combinedKff derivation" — happens when older Web
    // UI builds omit the field. WebUI form default is 680 W (Gaggia Classic
    // Pro 2019 / E24, 230 V boiler).
    int heaterWattage = request["wattage"] | 0;
    controller->autotune(testTime, samples, heaterWattage);
}

void WebUIPlugin::handleProfileRequest(uint32_t clientId, JsonDocument &request) {
    // Allocate the response node pool from PSRAM — list responses can be tens
    // of KB and would otherwise fragment the ~300 KB internal heap.
    JsonDocument response(&psramAllocator);
    auto type = request["tp"].as<String>();
    ESP_LOGI("WebUIPlugin", "Handling request: %s", type.c_str());
    response["tp"] = String("res:") + type.substring(4);
    response["rid"] = request["rid"].as<String>();

    if (type == "req:profiles:list") {
        auto arr = response["profiles"].to<JsonArray>();
        for (auto const &id : profileManager->listProfiles()) {
            Profile profile{};
            // Skip entries whose JSON couldn't be opened or failed validation
            // (parseProfile returns false for missing label/type/phases). Without
            // this, corrupt or partial profile files surface as blank cards in
            // the UI — the user reported "blank Simple cards" originating here.
            if (!profileManager->loadProfile(id, profile)) {
                ESP_LOGW("WebUIPlugin", "Skipping unreadable profile %s in list response", id.c_str());
                continue;
            }
            auto p = arr.add<JsonObject>();
            if (request["minimal"].as<bool>()) {
                p["id"] = profile.id;
                p["label"] = profile.label;
            } else {
                writeProfile(p, profile);
            }
        }
    } else if (type == "req:profiles:load") {
        auto id = request["id"].as<String>();
        Profile profile;
        if (profileManager->loadProfile(id, profile)) {
            auto obj = response["profile"].to<JsonObject>();
            writeProfile(obj, profile);
        } else {
            response["error"] = F("Profile not found");
        }
    } else if (type == "req:profiles:save") {
        auto obj = request["profile"].as<JsonObject>();
        Profile profile;
        parseProfile(obj, profile);
        if (!profileManager->saveProfile(profile)) {
            response["error"] = F("Save failed");
        }
        auto respObj = response["profile"].to<JsonObject>();
        writeProfile(respObj, profile);
    } else if (type == "req:profiles:delete") {
        auto id = request["id"].as<String>();
        if (!profileManager->deleteProfile(id)) {
            response["error"] = F("Delete failed");
        }
    } else if (type == "req:profiles:select") {
        auto id = request["id"].as<String>();
        profileManager->selectProfile(id);
    } else if (type == "req:profiles:favorite") {
        auto id = request["id"].as<String>();
        profileManager->addFavoritedProfile(id);
    } else if (type == "req:profiles:unfavorite") {
        auto id = request["id"].as<String>();
        profileManager->removeFavoritedProfile(id);
    } else if (type == "req:profiles:reorder") {
        // Expect an array of profile IDs in desired order
        if (request["order"].is<JsonArray>()) {
            std::vector<String> order;
            for (JsonVariant v : request["order"].as<JsonArray>()) {
                if (v.is<String>()) {
                    String id = v.as<String>();
                    if (!id.isEmpty() && std::find(order.begin(), order.end(), id) == order.end()) {
                        order.emplace_back(std::move(id));
                    }
                }
            }
            controller->getSettings().setProfileOrder(order);
        }
    }

    ws.text(clientId, toWsBuffer(response));
}

void WebUIPlugin::handleSettings(AsyncWebServerRequest *request) const {
    if (request->method() == HTTP_POST) {
        bool persisted = true;
        // Names of posted fields that failed validation and were not stored.
        // Every other field in the same request is still applied; the
        // response lists these instead of answering success (gm-nov3.26).
        std::vector<const char *> rejected;
        controller->getSettings().batchUpdate([request, &persisted, &rejected](Settings *settings) {
            // A checkbox is posted as 0 or 1 and read only when present. The
            // form sends every checkbox; a partial POST (the pump calibration's
            // postCoefficients sends one field) leaves every flag as it was.
            // Reading presence as the value used to clear all of them.
            auto flagArg = [request](const char *name, auto &&set) {
                if (request->hasArg(name))
                    set(request->arg(name).toInt() != 0);
            };
            if (request->hasArg("startupMode"))
                settings->setStartupMode(request->arg("startupMode") == "brew" ? MODE_BREW : MODE_STANDBY);
            if (request->hasArg("startupProfile"))
                settings->setStartupProfile(request->arg("startupProfile"));
            if (request->hasArg("targetSteamTemp"))
                settings->setTargetSteamTemp(request->arg("targetSteamTemp").toInt());
            if (request->hasArg("targetWaterTemp"))
                settings->setTargetWaterTemp(request->arg("targetWaterTemp").toInt());
            if (request->hasArg("temperatureOffset"))
                settings->setTemperatureOffset(request->arg("temperatureOffset").toInt());
            if (request->hasArg("pressureScaling"))
                settings->setPressureScaling(request->arg("pressureScaling").toFloat());
            if (request->hasArg("scaleFactor1") || request->hasArg("scaleFactor2")) {
                float sf1 = settings->getScaleFactor1();
                float sf2 = settings->getScaleFactor2();
                if (request->hasArg("scaleFactor1")) {
                    sf1 = request->arg("scaleFactor1").toFloat();
                }
                if (request->hasArg("scaleFactor2")) {
                    sf2 = request->arg("scaleFactor2").toFloat();
                }
                settings->setScaleFactors(sf1, sf2);
            }
            if (request->hasArg("hardwareScaleSampleRateSps") || request->hasArg("hardwareScaleIdleAlpha") ||
                request->hasArg("hardwareScaleActiveAlpha")) {
                const uint16_t sampleRate = request->hasArg("hardwareScaleSampleRateSps")
                                                ? static_cast<uint16_t>(request->arg("hardwareScaleSampleRateSps").toInt())
                                                : settings->getHardwareScaleSampleRateSps();
                const float idleAlpha = request->hasArg("hardwareScaleIdleAlpha")
                                            ? request->arg("hardwareScaleIdleAlpha").toFloat()
                                            : settings->getHardwareScaleIdleAlpha();
                const float activeAlpha = request->hasArg("hardwareScaleActiveAlpha")
                                              ? request->arg("hardwareScaleActiveAlpha").toFloat()
                                              : settings->getHardwareScaleActiveAlpha();
                settings->setHardwareScaleConfiguration(sampleRate, idleAlpha, activeAlpha);
            }
            if (request->hasArg("preferredScaleSource"))
                settings->setPreferredScaleSource(request->arg("preferredScaleSource"));
            if (request->hasArg("pid"))
                settings->setPid(request->arg("pid"));
            if (request->hasArg("pumpModelCoeffs"))
                settings->setPumpModelCoeffs(request->arg("pumpModelCoeffs"));
            if (request->hasArg("pumpSlipCoeffs"))
                settings->setPumpSlipCoeffs(request->arg("pumpSlipCoeffs"));
            if (request->hasArg("wifiSsid"))
                settings->setWifiSsid(request->arg("wifiSsid"));
            if (request->hasArg("mdnsName"))
                settings->setMdnsName(request->arg("mdnsName"));
            if (request->hasArg("wifiPassword") && request->arg("wifiPassword") != "---unchanged---")
                settings->setWifiPassword(request->arg("wifiPassword"));
            if (request->hasArg("apPassword") && request->arg("apPassword").length() > 0) {
                // WPA2 needs at least 8 characters; a shorter one is refused
                // and reported, not stored. Empty is "not set": the form
                // echoes the stored value, and the simulator stores none.
                if (request->arg("apPassword").length() >= WIFI_AP_PASSWORD_MIN_LENGTH)
                    settings->setWifiApPassword(request->arg("apPassword"));
                else
                    rejected.push_back("apPassword");
            }
            flagArg("homekit", [settings](bool v) { settings->setHomekit(v); });
            flagArg("boilerFillActive", [settings](bool v) { settings->setBoilerFillActive(v); });
            if (request->hasArg("startupFillTime"))
                settings->setStartupFillTime(request->arg("startupFillTime").toInt() * 1000);
            if (request->hasArg("steamFillTime"))
                settings->setSteamFillTime(request->arg("steamFillTime").toInt() * 1000);
            flagArg("smartGrindActive", [settings](bool v) { settings->setSmartGrindActive(v); });
            flagArg("scaleMenuButton", [settings](bool v) { settings->setScaleMenuButton(v); });
            if (request->hasArg("bgAnimId"))
                settings->setBgAnimId(request->arg("bgAnimId").toInt());
            // -1 means "the standby screen plays the main animation". The
            // form sends that value like any other, so nothing here has to
            // special-case it; DefaultUI reads a negative or out of range id
            // as "same as the main one".
            if (request->hasArg("bgAnimStandbyId"))
                settings->setBgAnimStandbyId(request->arg("bgAnimStandbyId").toInt());
            if (request->hasArg("bgAnimParams"))
                settings->setBgAnimParams(request->arg("bgAnimParams"));
            if (request->hasArg("bgAnimTheme")) {
                // The legacy integer's namespace is frozen (BgAnim.h): 0 to
                // 17 are the original built-ins and 18 is the pre-library
                // custom gradient. Anything outside it is a stale form or a
                // hand-made request, and is stored as 0 rather than kept to
                // start meaning an appended built-in later.
                const int theme = request->arg("bgAnimTheme").toInt();
                settings->setBgAnimTheme(theme >= 0 && theme <= BG_THEME_LEGACY_CUSTOM ? theme : 0);
            }
            if (request->hasArg("bgAnimFps"))
                settings->setBgAnimFps(request->arg("bgAnimFps").toInt());
            // Guarded on hasArg like the flags above: a partial submit that
            // read absence as off would drop to "full resolution, no
            // interlacing" and the panel to ~15 fps.
            if (request->hasArg("bgAnimHalfRes"))
                settings->setBgAnimHalfRes(request->arg("bgAnimHalfRes").toInt() != 0 ? 1 : 0);
            if (request->hasArg("bgAnimInterlace"))
                settings->setBgAnimInterlace(request->arg("bgAnimInterlace").toInt() != 0 ? 1 : 0);
            if (request->hasArg("bgAnimClearPlates")) {
                // 0 keep, 1 hide, 2 custom colour+opacity; anything else is a
                // stale or hand-made request, so fall back to the default.
                const int plateMode = request->arg("bgAnimClearPlates").toInt();
                settings->setBgAnimClearPlates(plateMode >= 0 && plateMode <= 2 ? plateMode : 1);
            }
            if (request->hasArg("bgAnimPlateColor")) {
                // Accepts "#rrggbb" (what <input type=color> submits) as well
                // as a plain decimal, which is what a scripted client sends.
                String c = request->arg("bgAnimPlateColor");
                c.trim();
                long parsed = 0;
                if (c.startsWith("#")) {
                    parsed = strtol(c.c_str() + 1, nullptr, 16);
                } else {
                    parsed = strtol(c.c_str(), nullptr, 10);
                }
                settings->setBgAnimPlateColor(static_cast<int>(parsed));
            }
            if (request->hasArg("bgAnimPlateOpacity"))
                settings->setBgAnimPlateOpacity(request->arg("bgAnimPlateOpacity").toInt());
            flagArg("elementTintEnabled", [settings](bool v) { settings->setElementTintEnabled(v); });
            if (request->hasArg("elementTintColor")) {
                // Same accepted forms as bgAnimPlateColor: "#rrggbb" from
                // <input type=color>, plain decimal from scripted clients.
                String c = request->arg("elementTintColor");
                c.trim();
                settings->setElementTintColor(
                    static_cast<int>(c.startsWith("#") ? strtol(c.c_str() + 1, nullptr, 16) : strtol(c.c_str(), nullptr, 10)));
            }
            if (request->hasArg("touchDimColor")) {
                String c = request->arg("touchDimColor");
                c.trim();
                settings->setTouchDimColor(
                    static_cast<int>(c.startsWith("#") ? strtol(c.c_str() + 1, nullptr, 16) : strtol(c.c_str(), nullptr, 10)));
            }
            // Tone controls. All three are percentages and all three clamp in
            // the setter, so a stale or hand-made request cannot push a value
            // into the Q8 conversions that drive the render path.
            if (request->hasArg("bgAnimBrightness"))
                settings->setBgAnimBrightness(request->arg("bgAnimBrightness").toInt());
            if (request->hasArg("bgAnimHighlightKnee"))
                settings->setBgAnimHighlightKnee(request->arg("bgAnimHighlightKnee").toInt());
            if (request->hasArg("bgAnimScrim"))
                settings->setBgAnimScrim(request->arg("bgAnimScrim").toInt());
            if (request->hasArg("bgFadeOutMs"))
                settings->setBgFadeOutMs(request->arg("bgFadeOutMs").toInt());
            if (request->hasArg("bgFadeInMs"))
                settings->setBgFadeInMs(request->arg("bgFadeInMs").toInt());
            if (request->hasArg("bgFadeCurve"))
                settings->setBgFadeCurve(request->arg("bgFadeCurve").toInt());
            if (request->hasArg("panelClockDiv")) {
                // 0 = firmware default; explicit dividers outside the sane
                // window (MIN_USER_DIV..12, 6.7-13.3 MHz pclk) could leave the
                // panel unreadable or, below MIN_USER_DIV, garbling (see
                // PanelClock.h), so reject them to default rather than persist.
                int div = request->arg("panelClockDiv").toInt();
                if (div != 0 && (div < panelclock::MIN_USER_DIV || div > 12))
                    div = 0;
                settings->setPanelClockDiv(div);
            }
            if (request->hasArg("panelVcom")) {
                // Clamped in the setter to 0-127. The register is 8-bit, but
                // the top half is far past any VCOM this glass wants, and a
                // wildly wrong VCOM is visible as a washed-out or flickering
                // panel rather than as anything that fails safely.
                settings->setPanelVcom(request->arg("panelVcom").toInt());
            }
            if (request->hasArg("bgAnimCustomTheme"))
                settings->setBgAnimCustomTheme(request->arg("bgAnimCustomTheme"));
            // Structurally validated rather than trusted: a malformed library
            // would make every animation that references it fall back to the
            // global theme, and the strings come straight from the form.
            // A field that fails is refused and reported, and the stored
            // value stays as it was.
            if (request->hasArg("bgAnimGradients")) {
                if (bg_library_valid(request->arg("bgAnimGradients").c_str()))
                    settings->setBgAnimGradients(request->arg("bgAnimGradients"));
                else
                    rejected.push_back("bgAnimGradients");
            }
            if (request->hasArg("bgAnimThemeMap")) {
                if (bg_map_valid(request->arg("bgAnimThemeMap").c_str()))
                    settings->setBgAnimThemeMap(request->arg("bgAnimThemeMap"));
                else
                    rejected.push_back("bgAnimThemeMap");
            }
            // The global gradient, in the same grammar as one map slot. A
            // built-in written here is mirrored into bgAnimTheme so the two
            // agree: bgAnimTheme is the last fallback and the on-display
            // "Global (<name>)" label reads it. bg_legacy_mirror_for_ref
            // (BgAnim.h) is the one mirror policy, shared with the display's
            // two writers and mirrored by the web form.
            if (request->hasArg("bgAnimGradientRef")) {
                const String ref = request->arg("bgAnimGradientRef");
                if (bg_ref_valid(ref.c_str())) {
                    settings->setBgAnimGradientRef(ref);
                    const int mirror = bg_legacy_mirror_for_ref(ref.c_str(), bg_theme_count());
                    if (mirror >= 0)
                        settings->setBgAnimTheme(mirror);
                } else {
                    rejected.push_back("bgAnimGradientRef");
                }
            }
            flagArg("bgAnimAllScreens", [settings](bool v) { settings->setBgAnimAllScreens(v); });
            if (request->hasArg("smartGrindIp"))
                settings->setSmartGrindIp(request->arg("smartGrindIp"));
            if (request->hasArg("smartGrindMode"))
                settings->setSmartGrindMode(request->arg("smartGrindMode").toInt());
            flagArg("homeAssistant", [settings](bool v) { settings->setHomeAssistant(v); });
            if (request->hasArg("haUser"))
                settings->setHomeAssistantUser(request->arg("haUser"));
            if (request->hasArg("haPassword"))
                settings->setHomeAssistantPassword(request->arg("haPassword"));
            if (request->hasArg("haIP"))
                settings->setHomeAssistantIP(request->arg("haIP"));
            if (request->hasArg("haPort"))
                settings->setHomeAssistantPort(request->arg("haPort").toInt());
            if (request->hasArg("haTopic"))
                settings->setHomeAssistantTopic(request->arg("haTopic"));
            flagArg("momentaryButtons", [settings](bool v) { settings->setMomentaryButtons(v); });
            flagArg("delayAdjust", [settings](bool v) { settings->setDelayAdjust(v); });
            if (request->hasArg("brewDelay"))
                settings->setBrewDelay(request->arg("brewDelay").toDouble());
            if (request->hasArg("grindDelay"))
                settings->setGrindDelay(request->arg("grindDelay").toDouble());
            if (request->hasArg("timezone"))
                settings->setTimezone(request->arg("timezone"));
            flagArg("clock24hFormat", [settings](bool v) { settings->setClockFormat(v); });
            if (request->hasArg("standbyTimeout"))
                settings->setStandbyTimeout(request->arg("standbyTimeout").toInt() * 1000);
            if (request->hasArg("mainBrightness"))
                settings->setMainBrightness(request->arg("mainBrightness").toInt());
            if (request->hasArg("standbyBrightness"))
                settings->setStandbyBrightness(request->arg("standbyBrightness").toInt());
            if (request->hasArg("standbyBrightnessTimeout"))
                settings->setStandbyBrightnessTimeout(request->arg("standbyBrightnessTimeout").toInt() * 1000);
            if (request->hasArg("steamPumpPercentage"))
                settings->setSteamPumpPercentage(request->arg("steamPumpPercentage").toFloat());
            if (request->hasArg("steamPumpCutoff"))
                settings->setSteamPumpCutoff(request->arg("steamPumpCutoff").toFloat());
            if (request->hasArg("themeMode"))
                settings->setThemeMode(request->arg("themeMode").toInt());
            if (request->hasArg("sunriseIdle"))
                settings->setSunriseIdle(request->arg("sunriseIdle"));
            if (request->hasArg("sunriseActive"))
                settings->setSunriseActive(request->arg("sunriseActive"));
            if (request->hasArg("sunriseFinished"))
                settings->setSunriseFinished(request->arg("sunriseFinished"));
            if (request->hasArg("sunriseError"))
                settings->setSunriseError(request->arg("sunriseError"));
            if (request->hasArg("sunriseExtBrightness"))
                settings->setSunriseExtBrightness(request->arg("sunriseExtBrightness").toInt());
            if (request->hasArg("emptyTankDistance"))
                settings->setEmptyTankDistance(request->arg("emptyTankDistance").toInt());
            if (request->hasArg("fullTankDistance"))
                settings->setFullTankDistance(request->arg("fullTankDistance").toInt());
            if (request->hasArg("altRelayFunction"))
                settings->setAltRelayFunction(request->arg("altRelayFunction").toInt());
            if (request->hasArg("buttonBehavior"))
                settings->setButtonBehaviorList(explode(request->arg("buttonBehavior"), ','));
            if (request->hasArg("commutationGain"))
                settings->setCommutationGain(request->arg("commutationGain").toFloat());
            if (request->hasArg("convergenceGain"))
                settings->setConvergenceGain(request->arg("convergenceGain").toFloat());
            if (request->hasArg("integralGain"))
                settings->setIntegralGain(request->arg("integralGain").toFloat());
            if (request->hasArg("maxPumpPower"))
                settings->setMaxPumpPower(request->arg("maxPumpPower").toFloat());
            if (request->hasArg("savedScale"))
                settings->setSavedScale(request->arg("savedScale"));
            flagArg("autowakeupEnabled", [settings](bool v) { settings->setAutoWakeupEnabled(v); });
            if (request->hasArg("autowakeupSchedules")) {
                // Handle schedule format with days
                String schedulesStr = request->arg("autowakeupSchedules");
                std::vector<AutoWakeupSchedule> schedules;
                bool schedulesValid = true;

                if (schedulesStr.length() > 0) {
                    // Split semicolon-separated schedules
                    int start = 0;
                    int end = schedulesStr.indexOf(';');

                    while (end != -1 || start < schedulesStr.length()) {
                        String scheduleStr = (end != -1) ? schedulesStr.substring(start, end) : schedulesStr.substring(start);

                        int pipePos = scheduleStr.indexOf('|');
                        // A time that is not HH:MM in range is never stored:
                        // the on-display editor parses the stored string and
                        // showed 00:00 for anything else, and the wakeup tick
                        // would never match it. One such entry refuses the
                        // whole field below, so the stored list stays as it was.
                        if (pipePos == -1 || !isScheduleTime(scheduleStr.substring(0, pipePos))) {
                            schedulesValid = false;
                        } else {
                            String timeStr = scheduleStr.substring(0, pipePos);
                            String daysStr = scheduleStr.substring(pipePos + 1);

                            AutoWakeupSchedule schedule;
                            schedule.time = timeStr;

                            if (daysStr.length() == 7) {
                                for (int i = 0; i < 7; i++) {
                                    schedule.days[i] = (daysStr.charAt(i) == '1');
                                }
                            }

                            schedules.push_back(schedule);
                        }

                        if (end == -1)
                            break;
                        start = end + 1;
                        end = schedulesStr.indexOf(';', start);
                    }
                }

                if (!schedulesValid) {
                    rejected.push_back("autowakeupSchedules");
                } else {
                    if (schedules.empty()) {
                        schedules.push_back(AutoWakeupSchedule("07:00")); // Default fallback
                    }
                    settings->setAutoWakeupSchedules(schedules);
                }
            }
            // flushNow() reports whether every change reached NVS. save(true)
            // did not, so a failed write answered success and the browser
            // showed values that were gone after the next reboot.
            persisted = settings->flushNow();
        });
        pluginManager->trigger("settings:changed");
        // A save supersedes the editor's live preview. The preview holds the
        // panel for 15 s per message, so a preview the editor sent just before
        // Save (or one that lost the race with a dropped socket) would
        // otherwise keep showing a gradient that is not the one just saved;
        // seen once in 80 scripted edit-and-save rounds. The editor re-sends
        // its state every 5 s while it is open, so an unsaved edit still being
        // previewed comes back on its own.
        if (request->hasArg("bgAnimThemeMap") || request->hasArg("bgAnimGradients") || request->hasArg("bgAnimTheme") ||
            request->hasArg("bgAnimGradientRef") || request->hasArg("bgAnimId")) {
            pluginManager->trigger("bganim:preview-end");
        }
        controller->setTargetTemp(controller->getTargetTemp());
        controller->setScaleFactors();
        controller->setPumpModelCoeffs();

        // The new values are live in memory and still marked dirty, so the
        // periodic flush keeps retrying, but they are not on flash yet. Tell
        // the browser so it keeps its edits and offers a retry, and do not
        // honour a restart: rebooting now would drop them. The body is
        // {"error": <text>, "code": <id>}, the shape a rejected field should
        // use too (gm-nov3.26).
        if (!persisted) {
            ESP_LOGE("WebUIPlugin", "settings save: NVS write failed, answering 500");
            AsyncResponseStream *response = request->beginResponseStream("application/json");
            response->setCode(500);
            JsonDocument doc(&psramAllocator);
            doc["error"] = "The settings could not be written to flash. They are in use now, "
                           "but a restart before a successful save loses them.";
            doc["code"] = "persist_failed";
            serializeJson(doc, *response);
            request->send(response);
            return;
        }
        // The valid fields are stored and on flash; the refused ones kept
        // their old values. Say which, and do not honour a restart, so the
        // browser keeps its edits and the user can correct and retry.
        if (!rejected.empty()) {
            ESP_LOGW("WebUIPlugin", "settings save: %u field(s) refused, answering 422", (unsigned)rejected.size());
            AsyncResponseStream *response = request->beginResponseStream("application/json");
            response->setCode(422);
            JsonDocument doc(&psramAllocator);
            doc["error"] = "invalid fields";
            doc["code"] = "invalid_fields";
            JsonArray fields = doc["fields"].to<JsonArray>();
            for (const char *name : rejected)
                fields.add(name);
            serializeJson(doc, *response);
            request->send(response);
            return;
        }
    }

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    JsonDocument doc(&psramAllocator);
    // The on-display settings write the container-typed properties (theme
    // map, timezone, schedules) from the UI task; Property::set replaces the
    // String or vector, and a read here that overlaps it copies from a freed
    // buffer. Hold the settings transaction lock while the document is
    // built from them (the UI task's writes are single sets and commits).
    Settings::Guard settingsGuard(controller->getSettings());
    Settings const &settings = controller->getSettings();
    doc["startupMode"] = settings.getStartupMode() == MODE_BREW ? "brew" : "standby";
    doc["startupProfile"] = settings.getStartupProfile();
    doc["targetSteamTemp"] = settings.getTargetSteamTemp();
    doc["targetWaterTemp"] = settings.getTargetWaterTemp();
    doc["homekit"] = settings.isHomekit();
    doc["homeAssistant"] = settings.isHomeAssistant();
    doc["haUser"] = settings.getHomeAssistantUser();
    doc["haPassword"] = settings.getHomeAssistantPassword();
    doc["haIP"] = settings.getHomeAssistantIP();
    doc["haPort"] = settings.getHomeAssistantPort();
    doc["haTopic"] = settings.getHomeAssistantTopic();
    doc["pid"] = settings.getPid();
    doc["pumpModelCoeffs"] = settings.getPumpModelCoeffs();
    doc["pumpSlipCoeffs"] = settings.getPumpSlipCoeffs();
    doc["wifiSsid"] = settings.getWifiSsid();
    doc["wifiPassword"] = apMode ? "---unchanged---" : settings.getWifiPassword();
    doc["apPassword"] = settings.getWifiApPassword();
    doc["mdnsName"] = settings.getMdnsName();
    doc["temperatureOffset"] = String(settings.getTemperatureOffset());
    doc["pressureScaling"] = String(settings.getPressureScaling());
    doc["scaleFactor1"] = settings.getScaleFactor1();
    doc["scaleFactor2"] = settings.getScaleFactor2();
    doc["hardwareScaleSampleRateSps"] = settings.getHardwareScaleSampleRateSps();
    doc["hardwareScaleIdleAlpha"] = settings.getHardwareScaleIdleAlpha();
    doc["hardwareScaleActiveAlpha"] = settings.getHardwareScaleActiveAlpha();
    doc["preferredScaleSource"] = settings.getPreferredScaleSource();
    doc["boilerFillActive"] = settings.isBoilerFillActive();
    doc["startupFillTime"] = settings.getStartupFillTime() / 1000;
    doc["steamFillTime"] = settings.getSteamFillTime() / 1000;
    doc["smartGrindActive"] = settings.isSmartGrindActive();
    doc["scaleMenuButton"] = settings.isScaleMenuButton();
    doc["bgAnimId"] = settings.getBgAnimId();
    doc["bgAnimStandbyId"] = settings.getBgAnimStandbyId();
    doc["bgAnimParams"] = settings.getBgAnimParams();
    doc["bgAnimAllScreens"] = settings.isBgAnimAllScreens();
    doc["bgAnimTheme"] = settings.getBgAnimTheme();
    doc["bgAnimFps"] = settings.getBgAnimFps();
    doc["bgAnimHalfRes"] = settings.getBgAnimHalfRes();
    doc["bgAnimInterlace"] = settings.getBgAnimInterlace();
    doc["bgAnimClearPlates"] = settings.getBgAnimClearPlates();
    {
        // Emitted as #rrggbb so the form can bind it straight to <input type=color>.
        char hex[8];
        snprintf(hex, sizeof(hex), "#%06X", static_cast<unsigned>(settings.getBgAnimPlateColor()) & 0xFFFFFFu);
        doc["bgAnimPlateColor"] = hex;
    }
    doc["bgAnimPlateOpacity"] = settings.getBgAnimPlateOpacity();
    doc["elementTintEnabled"] = settings.getElementTintEnabled();
    {
        char hex[8];
        snprintf(hex, sizeof(hex), "#%06X", static_cast<unsigned>(settings.getElementTintColor()) & 0xFFFFFFu);
        doc["elementTintColor"] = hex;
        snprintf(hex, sizeof(hex), "#%06X", static_cast<unsigned>(settings.getTouchDimColor()) & 0xFFFFFFu);
        doc["touchDimColor"] = hex;
    }
    doc["bgAnimBrightness"] = settings.getBgAnimBrightness();
    doc["bgAnimHighlightKnee"] = settings.getBgAnimHighlightKnee();
    doc["bgAnimScrim"] = settings.getBgAnimScrim();
    doc["bgFadeOutMs"] = settings.getBgFadeOutMs();
    doc["bgFadeInMs"] = settings.getBgFadeInMs();
    doc["bgFadeCurve"] = settings.getBgFadeCurve();
    doc["panelClockDiv"] = settings.getPanelClockDiv();
    doc["panelVcom"] = settings.getPanelVcom();
    // Read-only capability flag, not a setting: on ESP-IDF 4.4 there is no
    // esp_lcd_rgb_panel_set_pclk, so a new divider is only honoured when the
    // panel is next created. The form posts the whole document back and the
    // handler matches on explicit argument names, so echoing this is inert.
    doc["panelClockLive"] = panelclock::hasLiveControl();
    doc["bgAnimCustomTheme"] = settings.getBgAnimCustomTheme();
    doc["bgAnimGradients"] = settings.getBgAnimGradients();
    doc["bgAnimThemeMap"] = settings.getBgAnimThemeMap();
    doc["bgAnimGradientRef"] = settings.getBgAnimGradientRef();
    doc["smartGrindIp"] = settings.getSmartGrindIp();
    doc["smartGrindMode"] = settings.getSmartGrindMode();
    doc["momentaryButtons"] = settings.isMomentaryButtons();
    doc["brewDelay"] = settings.getBrewDelay();
    doc["grindDelay"] = settings.getGrindDelay();
    doc["delayAdjust"] = settings.isDelayAdjust();
    doc["timezone"] = settings.getTimezone();
    doc["clock24hFormat"] = settings.isClock24hFormat();
    doc["standbyTimeout"] = settings.getStandbyTimeout() / 1000;
    doc["mainBrightness"] = settings.getMainBrightness();
    doc["standbyBrightness"] = settings.getStandbyBrightness();
    doc["standbyBrightnessTimeout"] = settings.getStandbyBrightnessTimeout() / 1000;
    doc["steamPumpPercentage"] = settings.getSteamPumpPercentage();
    doc["steamPumpCutoff"] = settings.getSteamPumpCutoff();
    doc["themeMode"] = settings.getThemeMode();
    doc["sunriseIdle"] = settings.getSunriseIdle();
    doc["sunriseActive"] = settings.getSunriseActive();
    doc["sunriseFinished"] = settings.getSunriseFinished();
    doc["sunriseError"] = settings.getSunriseError();
    doc["sunriseExtBrightness"] = settings.getSunriseExtBrightness();
    doc["emptyTankDistance"] = settings.getEmptyTankDistance();
    doc["fullTankDistance"] = settings.getFullTankDistance();
    doc["altRelayFunction"] = settings.getAltRelayFunction();
    // Add auto-wakeup settings to response
    doc["autowakeupEnabled"] = settings.isAutoWakeupEnabled();
    doc["buttonBehavior"] = implode(settings.getButtonBehaviorList(), ",");
    doc["commutationGain"] = settings.getCommutationGain();
    doc["convergenceGain"] = settings.getConvergenceGain();
    doc["integralGain"] = settings.getIntegralGain();
    doc["maxPumpPower"] = settings.getMaxPumpPower();
    doc["savedScale"] = settings.getSavedScale();

    // Add schedule format with days
    std::vector<AutoWakeupSchedule> autowakeupSchedules = settings.getAutoWakeupSchedules();
    String schedulesStr = "";
    for (size_t i = 0; i < autowakeupSchedules.size(); i++) {
        if (i > 0)
            schedulesStr += ";";
        schedulesStr += autowakeupSchedules[i].time + "|";

        // Convert days array to 7-bit string
        for (int j = 0; j < 7; j++) {
            schedulesStr += autowakeupSchedules[i].days[j] ? "1" : "0";
        }
    }
    doc["autowakeupSchedules"] = schedulesStr;
    serializeJson(doc, *response);
    request->send(response);

    if (request->method() == HTTP_POST && request->hasArg("restart"))
        ESP.restart();
}

void WebUIPlugin::handleBLEScaleList(AsyncWebServerRequest *request) {
    JsonDocument doc(&psramAllocator);
    JsonArray scalesArray = doc.to<JsonArray>();
    std::vector<DiscoveredDevice> devices = BLEScales.getDiscoveredScales();
    for (const DiscoveredDevice &device : BLEScales.getDiscoveredScales()) {
        JsonDocument scale(&psramAllocator);
        scale["uuid"] = device.getAddress().toString();
        scale["name"] = device.getName();
        scale["rssi"] = device.getRSSI();
        scalesArray.add(scale);
    }
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
}

void WebUIPlugin::handleBLEScaleScan(AsyncWebServerRequest *request) {
    if (request->method() != HTTP_POST) {
        request->send(404);
        return;
    }
    BLEScales.scan();
    JsonDocument doc(&psramAllocator);
    doc["success"] = true;
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
}

void WebUIPlugin::handleBLEScaleConnect(AsyncWebServerRequest *request) {
    if (request->method() != HTTP_POST) {
        request->send(404);
        return;
    }
    BLEScales.connect(request->arg("uuid").c_str());
    JsonDocument doc(&psramAllocator);
    doc["success"] = true;
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
}

void WebUIPlugin::handleBLEScaleInfo(AsyncWebServerRequest *request) {
    JsonDocument doc(&psramAllocator);
    doc["connected"] = BLEScales.isConnected();
    doc["name"] = BLEScales.getName();
    doc["uuid"] = BLEScales.getUUID();
    doc["rssi"] = BLEScales.getRSSI();
    doc["hasBattery"] = BLEScales.hasBatteryLevel();
    // Only surface the numeric when the scale reports one — a 255 sentinel
    // (REMOTE_SCALES_BATTERY_UNKNOWN) would otherwise render as a fake "255%".
    if (BLEScales.hasBatteryLevel()) {
        const uint8_t pct = BLEScales.getBatteryLevel();
        if (pct != REMOTE_SCALES_BATTERY_UNKNOWN) {
            doc["battery"] = pct;
        }
    }
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    serializeJson(doc, *response);
    request->send(response);
}

void WebUIPlugin::handleDebugHeap(AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    JsonDocument doc;
    MemorySnapshot snap;
    if (gaggimate::memmon::isReady()) {
        snap = gaggimate::memmon::instance().snapshotNow();
    }
    const RegionStats *ri = nullptr;
    const RegionStats *rp = nullptr;
    for (const auto &rs : snap.regions) {
        if (rs.region == MemoryRegion::Internal)
            ri = &rs;
        else if (rs.region == MemoryRegion::Psram)
            rp = &rs;
    }
    JsonObject internalObj = doc["internal"].to<JsonObject>();
    constexpr uint32_t kInternalCaps = MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL;
    internalObj["free"] = ri ? ri->freeBytes : heap_caps_get_free_size(kInternalCaps);
    internalObj["largest"] = ri ? ri->largestFreeBlock : heap_caps_get_largest_free_block(kInternalCaps);
    internalObj["total"] = heap_caps_get_total_size(kInternalCaps);
    internalObj["minimum_free"] = ri ? ri->minimumFreeBytes : heap_caps_get_minimum_free_size(kInternalCaps);
    JsonObject psObj = doc["psram"].to<JsonObject>();
    psObj["free"] = rp ? rp->freeBytes : heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    psObj["largest"] = rp ? rp->largestFreeBlock : heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    psObj["total"] = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    psObj["minimum_free"] = rp ? rp->minimumFreeBytes : heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    doc["fragmentation_internal"] = ri ? ri->fragmentation : 0.0f;
    doc["fragmentation_psram"] = rp ? rp->fragmentation : 0.0f;
    if (ri) {
        internalObj["slope"] = ri->freeBytesSlope;
        internalObj["seconds_to_warn"] = ri->secondsToWarn;
        internalObj["seconds_to_critical"] = ri->secondsToCritical;
    }
    serializeJson(doc, *response);
    request->send(response);
}

void WebUIPlugin::updateOTAStatus(const String &version) {
    if (ws.getClients().empty()) {
        return;
    }
    Settings const &settings = controller->getSettings();
    JsonDocument doc(&psramAllocator);
    doc["tp"] = "res:ota-settings";
    doc["displayUpdateAvailable"] = ota->isUpdateAvailable(false);
    doc["controllerUpdateAvailable"] = ota->isUpdateAvailable(true);
    doc["displayVersion"] = BUILD_GIT_VERSION;
    doc["controllerVersion"] = controller->getSystemInfo().version;
    doc["hardware"] = controller->getSystemInfo().hardware;
    doc["latestVersion"] = ota->getCurrentVersion();
    doc["channel"] = settings.getOTAChannel();
    doc["updating"] = updating;
    // Carried here rather than in a new endpoint because SystemTab's support
    // bundle already serializes this whole document as `versions`. Without them
    // a bundle says a dump is attached but not what crashed, and the reset
    // reason is the first thing you want when the dump turns out to be stale.
    doc["resetReason"] = boot_reset_reason();
    doc["coreDumpSize"] = static_cast<uint32_t>(boot_coredump_size());
    // LittleFS usage metrics
    {
        size_t total = LittleFS.totalBytes();
        size_t used = LittleFS.usedBytes();
        size_t freeBytes = total > used ? (total - used) : 0;
        doc["spiffsTotal"] = static_cast<uint32_t>(total);
        doc["spiffsUsed"] = static_cast<uint32_t>(used);
        doc["spiffsFree"] = static_cast<uint32_t>(freeBytes);
        if (total > 0) {
            doc["spiffsUsedPct"] = static_cast<uint8_t>((used * 100) / total);
        }
    }
    // Memory usage metrics — sourced from ESPMemoryMonitor so the settings UI
    // shares a single source of truth with /api/debug/heap and the 60 s sampler
    // task. Falls back to heap_caps_* during the boot window before init().
    {
        const RegionStats *ri = nullptr;
        MemorySnapshot snap;
        if (gaggimate::memmon::isReady()) {
            snap = gaggimate::memmon::instance().snapshotNow();
            for (const auto &rs : snap.regions) {
                if (rs.region == MemoryRegion::Internal) {
                    ri = &rs;
                    break;
                }
            }
        }
        const size_t total = heap_caps_get_total_size(MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL);
        doc["heapFree"] =
            static_cast<uint32_t>(ri ? ri->freeBytes : heap_caps_get_free_size(MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL));
        doc["heapLargest"] = static_cast<uint32_t>(
            ri ? ri->largestFreeBlock : heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL));
        doc["heapTotal"] = static_cast<uint32_t>(total);
        doc["heapMinimum"] = static_cast<uint32_t>(
            ri ? ri->minimumFreeBytes : heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL));
    }
    doc["controllerTaskHealth"] = controller->isTaskHealthy();
#ifndef GAGGIMATE_HEADLESS
    doc["uiTaskHealth"] = controller->getUI()->isTaskHealthy();
#endif
    if (controller->isSDCard()) {
        const uint64_t total = SD_MMC.cardSize();
        const uint64_t used = SD_MMC.usedBytes();
        const uint64_t freeBytes = total > used ? (total - used) : 0;
        doc["sdTotal"] = total;
        doc["sdUsed"] = used;
        doc["sdFree"] = freeBytes;
        if (total > 0) {
            // Provide integer percentage to avoid float JSON
            doc["sdUsedPct"] = static_cast<uint8_t>((used * 100) / total);
        }
    }
    broadcastJson(doc);
}

void WebUIPlugin::updateOTAProgress(uint8_t phase, int progress) {
    if (ws.getClients().empty()) {
        return;
    }
    JsonDocument doc(&psramAllocator);
    doc["tp"] = "evt:ota-progress";
    doc["phase"] = phase;
    doc["progress"] = progress;
    broadcastJson(doc);
}

void WebUIPlugin::broadcastJson(JsonDocument &doc) {
    if (ws.getClients().empty()) {
        return;
    }
    ws.textAll(toWsBuffer(doc));
}

void WebUIPlugin::sendAutotuneResult() {
    JsonDocument doc(&psramAllocator);
    doc["tp"] = "evt:autotune-result";
    doc["pid"] = controller->getSettings().getPid();
    broadcastJson(doc);
}

void WebUIPlugin::sendAutotuneFailed() {
    // Distinct WS event — Autotune page renders "timed out" error card
    // instead of stuck spinner. Fires on ERROR_CODE_AUTOTUNE_TIMEOUT.
    JsonDocument doc(&psramAllocator);
    doc["tp"] = "evt:autotune-failed";
    broadcastJson(doc);
}

void WebUIPlugin::handleFlushStart(uint32_t clientId, JsonDocument &request) {
    controller->onFlush();

    JsonDocument response(&psramAllocator);
    response["tp"] = "res:flush:start";
    response["rid"] = request["rid"];
    response["success"] = true;
    ws.text(clientId, toWsBuffer(response));
}

void WebUIPlugin::handleCoreDumpDownload(AsyncWebServerRequest *request) {
    // Check if core dump is available
    size_t coreAddr, coreSize;
    if (esp_core_dump_image_get(&coreAddr, &coreSize) != ESP_OK || coreSize == 0) {
        request->send(404, "text/plain", "No core dump available");
        return;
    }

    // Find the coredump partition
    const esp_partition_t *coredump_partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
    if (coredump_partition == NULL) {
        request->send(500, "text/plain", "Core dump partition not found");
        return;
    }

    ESP_LOGI("WebUIPlugin", "Streaming core dump: %d bytes from 0x%x", coreSize, coreAddr);

    // Create a streaming response
    AsyncWebServerResponse *response =
        request->beginResponse("application/octet-stream", coreSize,
                               [coredump_partition, coreSize](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
                                   // Calculate how much to read
                                   size_t remaining = coreSize - index;
                                   size_t toRead = (remaining < maxLen) ? remaining : maxLen;

                                   if (toRead == 0)
                                       return 0;

                                   // Read from partition
                                   esp_err_t err = esp_partition_read(coredump_partition, index, buffer, toRead);
                                   if (err != ESP_OK) {
                                       ESP_LOGE("WebUIPlugin", "Failed to read core dump: %s", esp_err_to_name(err));
                                       return 0;
                                   }

                                   return toRead;
                               });

    // Set appropriate headers
    response->addHeader("Content-Disposition", "attachment; filename=\"coredump.bin\"");
    response->addHeader("Cache-Control", "no-cache");

    request->send(response);
}
