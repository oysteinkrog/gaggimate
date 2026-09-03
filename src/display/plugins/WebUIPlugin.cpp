#include "WebUIPlugin.h"

// Defined in AnimNebula.cpp; see nebulaLerpSelfTest there.
extern uint32_t nebula_lerp_self_test(uint32_t *firstBad);
#include <DNSServer.h>
#include <LittleFS.h>
#include <esp_cache.h>        // esp_cache_msync, so /api/debug/fb reads past the cache
#include <esp_timer.h>        // esp_timer_dump, for /api/debug/timers
#include <esp_memory_utils.h> // esp_ptr_external_ram, for the band-buffer placement report
#include <SD_MMC.h>
#include <algorithm>
#include <display/core/Controller.h>
#include <display/core/MemoryMonitor.h>
#include <display/core/ProfileManager.h>
#include <display/core/process/BrewProcess.h>
#include <display/core/process/GrindProcess.h>
#include <display/models/profile.h>
#include <display/plugins/BLEScalePlugin.h>
#include <display/plugins/ShotHistoryPlugin.h>
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
#ifndef GAGGIMATE_HEADLESS
#include <display/drivers/LilyGoDriver.h>
#endif
#include <display/drivers/common/PanelClock.h>
#include <display/ui/default/bganim/BgAnimCommon.h>
#include <display/util/PsramStlAllocator.h>
#include <display/util/PsramWsBuffer.h>
#include <display/webassets/web_ui_manifest.h>
#include <esp32-hal-psram.h>
#include <esp_core_dump.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
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
    if (!serverRunning) {
        return;
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

void WebUIPlugin::serveWebAsset(AsyncWebServerRequest *request) {
    String path = request->url();
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
    if (asset == nullptr) {
        request->send(404, "text/plain", "Not found");
        return;
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

// Counters exported by the patched esp_lcd RGB driver (scripts/patch_esp_lcd_rgb.py).
// restart is the one that matters: the driver restarts the transfer when it has
// lost count of the DMA EOFs, and every restart is one visible block of
// vertically displaced lines. catchup counts the coalesced EOFs the patch
// recovered from, which are the restarts that no longer happen.
extern "C" {
extern volatile uint32_t gm_rgb_restart_count;
extern volatile uint32_t gm_rgb_catchup_count;
extern volatile uint32_t gm_rgb_catchup_bufs;
extern volatile uint32_t gm_rgb_catchup_max;
extern volatile uint32_t gm_rgb_resync_count;
extern volatile uint32_t gm_rgb_resync_bufs;
extern volatile uint32_t gm_rgb_resync_max;
extern volatile uint32_t gm_rgb_over_count;
extern volatile uint32_t gm_rgb_over_bufs;
extern volatile uint32_t gm_rgb_flash_skip_bufs;
extern volatile uint32_t gm_rgb_eof_expect;

extern volatile uint32_t gm_rgb_eof_min;
extern volatile uint32_t gm_rgb_eof_max;
extern volatile uint32_t gm_rgb_busy_hist[];
extern volatile uint32_t gm_rgb_gap_hist[];
extern volatile uint32_t gm_rgb_busy_max;
extern volatile uint32_t gm_rgb_gap_max;
// Gap logger from the GM_RGB_GAPLOG_PATCH hunk of scripts/patch_esp_lcd_rgb.py:
// one event per long refill gap or slow refill copy, with what the two cores
// were doing. Mirrors the driver's struct.
struct gm_rgb_gap_ev_t {
    uint32_t t_ms;
    uint32_t gap_us;
    uint32_t prev_busy_us; // copy time of the previous callback (inside gap_us)
    uint32_t busy_us;      // copy time of this callback
    uint32_t fills;        // buffers this callback refilled
    uint32_t nest;
    uint32_t pos; // buffers into the frame at entry
    uint32_t pc;
    uint32_t ps;
    uint32_t stall_us; // longest 240 B chunk of this callback's copies
    uint32_t stall_at; // byte offset of that chunk in its bounce buffer
    const char *task;
    const char *other; // task on the other core at entry
};
extern volatile gm_rgb_gap_ev_t gm_rgb_gaplog[];
extern volatile uint32_t gm_rgb_gaplog_n;
extern volatile uint32_t gm_rgb_chunk_hist[];
extern volatile uint32_t gm_rgb_chunk_max_us;
static constexpr int GM_RGB_GAPLOG_N = 32;
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
    // Headroom in the pool that actually runs out. Internal DRAM is the
    // scarce one: ESPAsyncWebServer stages every response through a
    // 2,872-byte buffer (ASYNC_RESPONCE_BUFF_SIZE, CONFIG_LWIP_TCP_MSS * 2)
    // that it allocates and frees per send round, and with
    // CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL at 4096 that allocation can never
    // spill to PSRAM. When it fails, write_send_buffs() just breaks: no error,
    // no RST, the response simply stalls forever.
    //
    // int_min is the one to watch -- the low-water mark since boot, which is
    // what says how close the pool actually came to empty, rather than where it
    // happens to sit when polled. Built with a fixed stack buffer so the
    // endpoint still answers when the heap is too tight for a response stream.
    // Exposes no configuration and no secrets.
    // Every armed esp_timer, with its period. Added to name the source of a
    // periodic event that stalls the panel refill for most of a millisecond.
    // What is known about it: the period is wall clock rather than frame
    // locked, holding at 454 to 478 ms across pixel clocks while the same
    // period measured in frames tracks refresh exactly (27.7 frames at 60.8 Hz,
    // 23.9 at 50.7, 20.8 at 43.4); it survives sustained WiFi traffic, so it is
    // not a modem-sleep wake being deferred; and it is indifferent to the
    // animation's frame rate, so it is not the renderer. Nothing this firmware
    // schedules runs at roughly 2.1 Hz, which leaves the timers IDF and the
    // radio stacks arm for themselves.
    //
    // Without CONFIG_ESP_TIMER_PROFILING the dump carries no names, only the
    // handle address and the period, which is enough to identify a period and
    // then chase the address through the map file. It lists armed timers only.
    //
    // The dump goes to the serial console because esp_timer_dump takes a FILE*
    // and there is no in-memory stream here; the HTTP response only confirms it
    // ran. Exposes no configuration and no secrets.
    server.on("/api/debug/timers", [](AsyncWebServerRequest *request) {
        esp_timer_dump(stdout);
        fflush(stdout);
        request->send(200, "application/json", "{\"dumped\":true}");
    });

#if CONFIG_FREERTOS_USE_TRACE_FACILITY && CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    // /api/debug/tasks: every task with its accumulated runtime counter, so
    // two samples diffed over a wall-clock interval say exactly which tasks
    // own each core's time. Exists because the animation render task measures
    // ~11x more wall time than its bands' compute, and the split between
    // "preempted by which task" and "stalled on what bus" cannot be read from
    // stage timers alone: those are wall clock, and preemption lands inside
    // them. Runtime counters use esp_timer (us) per the sdkconfig, so
    // d(rt)/d(now_us) is that task's share of ONE core over the interval; the
    // IDLE0/IDLE1 rows give each core's headroom directly. ISR time is charged
    // to whichever task it interrupts, so a task's share here is an upper
    // bound on its own compute. Loadtest-only: the config flags are off in the
    // production sdkconfigs and this block compiles away with them.
    server.on("/api/debug/tasks", [](AsyncWebServerRequest *request) {
        // A few spare rows: tasks can be born between the count and the
        // snapshot, and a short array makes uxTaskGetSystemState return 0.
        const UBaseType_t cap = uxTaskGetNumberOfTasks() + 4;
        TaskStatus_t *st = static_cast<TaskStatus_t *>(
            heap_caps_malloc(sizeof(TaskStatus_t) * cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (st == nullptr) {
            request->send(500, "application/json", "{\"error\":\"alloc\"}");
            return;
        }
        configRUN_TIME_COUNTER_TYPE total = 0;
        const UBaseType_t got = uxTaskGetSystemState(st, cap, &total);
        JsonDocument doc;
        doc["now_us"] = esp_timer_get_time();
        doc["total_rt"] = total;
        JsonArray arr = doc["tasks"].to<JsonArray>();
        for (UBaseType_t i = 0; i < got; i++) {
            JsonObject o = arr.add<JsonObject>();
            o["n"] = st[i].pcTaskName;
            o["p"] = static_cast<int>(st[i].uxCurrentPriority);
#if CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
            o["c"] = st[i].xCoreID == tskNO_AFFINITY ? -1 : static_cast<int>(st[i].xCoreID);
#endif
            o["rt"] = st[i].ulRunTimeCounter;
            o["hwm"] = st[i].usStackHighWaterMark;
            o["s"] = static_cast<int>(st[i].eCurrentState);
        }
        free(st);
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });
#endif

    server.on("/api/debug/heap", [](AsyncWebServerRequest *request) {
    // anim_sram is the committed part of the animation budget and
    // anim_budget its ceiling. The gap between them is the important
    // figure: alloc() never frees, so every animation the user visits
    // converts more of that gap into permanently resident internal DRAM.
    // A comfortable int_min means nothing if the gap is larger than it.
    // Headless builds drop the whole ui/ tree from build_src_filter, so
    // BgAnimCommon.cpp -- which defines these two counters -- is never
    // compiled and the references would not link. The keys stay in the
    // payload either way so the web UI needs no build-specific branch;
    // zero is the true value when no animation can allocate.
#ifdef GAGGIMATE_HEADLESS
        const size_t animSram = 0;
        const size_t animPsram = 0;
#else
        const size_t animSram = bganim::g_allocSram;
        const size_t animPsram = bganim::g_allocPsram;
#endif
        // Scan-out health, from the panel's own interrupts. `slips` is the one
        // to watch: it counts frames whose bounce-buffer refill lost its race
        // against everything else on the shared MSPI bus, which is exactly what
        // shows on the panel as a displaced band. A run of minutes at a
        // constant value is the only real evidence the display is clean, since
        // the fault is far too rare to catch by looking at it.
        uint32_t scFrames = 0, scRefills = 0, scSlips = 0;
        panelclock::scanoutStats(&scFrames, &scRefills, &scSlips);
        char buf[420];
        snprintf(buf, sizeof(buf),
                 "{\"int_free\":%u,\"int_largest\":%u,\"int_min\":%u,\"psram_free\":%u,\"psram_largest\":%u,"
                 "\"anim_sram\":%u,\"anim_psram\":%u,\"anim_budget\":%u,"
                 "\"sc_frames\":%u,\"sc_refills\":%u,\"sc_slips\":%u}",
                 // heap_caps_get_largest_free_block walks every block in the heap,
                 // which costs about 1.3 ms across both regions and starves the
                 // RGB panel's bounce refill for the duration -- one displaced
                 // frame per call. That is an acceptable price for a debug
                 // endpoint somebody asked for, and an unacceptable one for
                 // anything polled on a timer, so do not fold these into a
                 // status poll. The free and min-free figures beside them are
                 // O(1) counters and cost nothing.
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)), static_cast<unsigned>(animSram),
                 static_cast<unsigned>(animPsram), static_cast<unsigned>(bganim::SRAM_TOTAL_BUDGET),
                 static_cast<unsigned>(scFrames), static_cast<unsigned>(scRefills), static_cast<unsigned>(scSlips));
        request->send(200, "application/json", buf);
    });
    // Which slips happened, and how long before each one the suspects last ran.
    // The rate alone does not identify the cause: several things can overrun the
    // refill's 162 us budget, and they are told apart by timing signature rather
    // than by magnitude. A once-per-second overlay snapshot leaves a small
    // overlay_us on most slips; a flash write leaves a tight burst of slips
    // sharing one flash_us, because the cache is off and the LCD interrupt
    // masked for the write's whole duration; radio coexistence leaves every
    // source stale and the slips scattered. Exposes no configuration and no
    // secrets.
    server.on("/api/debug/scanout", [](AsyncWebServerRequest *request) {
        // reset=1 zeroes the counters so two configurations can be compared as
        // rates rather than as totals accumulated since boot.
        if (request->hasArg("reset")) {
            panelclock::scanoutReset();
            gm_rgb_restart_count = 0;
            gm_rgb_catchup_count = 0;
            gm_rgb_catchup_bufs = 0;
            gm_rgb_catchup_max = 0;
            gm_rgb_resync_count = 0;
            gm_rgb_resync_bufs = 0;
            gm_rgb_resync_max = 0;
            gm_rgb_over_count = 0;
            gm_rgb_over_bufs = 0;
            gm_rgb_flash_skip_bufs = 0;
            gm_rgb_eof_min = 0xFFFFFFFFu;
            gm_rgb_eof_max = 0;
            gm_rgb_busy_max = 0;
            gm_rgb_gap_max = 0;
            gm_rgb_chunk_max_us = 0;
            for (int i = 0; i < 24; i++) {
                gm_rgb_busy_hist[i] = 0;
                gm_rgb_gap_hist[i] = 0;
                gm_rgb_chunk_hist[i] = 0;
            }
            gm_rgb_gaplog_n = 0;
        }
        // lagthresh=N also logs a correlation entry for every frame whose
        // refill headroom fell below N us. Set it one histogram bucket below
        // the healthy mode; 0 turns it off.
        if (request->hasArg("lagthresh")) {
            panelclock::setLagThresholdUs(
                static_cast<uint32_t>(request->arg("lagthresh").toInt()));
        }
        uint32_t frames = 0, refills = 0, slips = 0;
        panelclock::scanoutStats(&frames, &refills, &slips);
        uint32_t marginLast = 0, marginMin = 0, marginMax = 0;
        uint32_t marginBucket[panelclock::SCANOUT_MARGIN_BUCKETS] = {0};
        panelclock::scanoutMargin(&marginLast, &marginMin, &marginMax, marginBucket);
        panelclock::ScanoutSlip slipLog[24];
        const size_t n = panelclock::scanoutSlipLog(slipLog, 24);
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        response->printf("{\"frames\":%u,\"refills\":%u,\"slips\":%u,\"now_us\":%u,", static_cast<unsigned>(frames),
                         static_cast<unsigned>(refills), static_cast<unsigned>(slips),
                         static_cast<unsigned>(esp_timer_get_time()));
        // margin_us is the headroom the refill had on the last frame and
        // margin_min_us / margin_max_us the extremes since the last reset.
        // margin_hist is the raw distribution in 128 us buckets: healthy frames
        // form one mode and lagging ones fall in steps of a bounce-buffer time
        // below it, each step being BOUNCE_LINES lines of visible vertical
        // displacement. Read the histogram, not slips -- esp_lcd restarts the
        // transfer on a single late bounce buffer and such a frame never
        // registers as a slip.
        response->printf("\"margin_us\":%u,\"margin_min_us\":%u,\"margin_max_us\":%u,\"margin_bucket_us\":%u,"
                         "\"margin_hist\":[",
                         static_cast<unsigned>(marginLast), static_cast<unsigned>(marginMin),
                         static_cast<unsigned>(marginMax),
                         static_cast<unsigned>(panelclock::SCANOUT_MARGIN_BUCKET_US));
        for (size_t i = 0; i < panelclock::SCANOUT_MARGIN_BUCKETS; i++) {
            response->printf("%s%u", i ? "," : "", static_cast<unsigned>(marginBucket[i]));
        }
        // resyncs is the event that used to displace the picture: a frame that
        // counted fewer bounce buffers than a frame holds. The patched driver squares
        // it up against the beam instead of restarting the DMA, so it now costs one
        // band of stale pixels rather than a whole shifted frame. dma_restarts should
        // stay at zero: only an explicit panel restart reaches it.
        response->printf("],\"resyncs\":%u,\"resync_bufs\":%u,\"resync_max\":%u,\"over_count\":%u,\"over_bufs\":%u,\"phy_defer\":%u,"
                         "\"flash_skips\":%u,"
                         "\"dma_restarts\":%u,\"dma_catchups\":%u,\"dma_catchup_bufs\":%u,"
                         "\"dma_catchup_max\":%u,\"eof_expect\":%u,\"eof_min\":%u,\"eof_max\":%u,\"log\":[",
                         static_cast<unsigned>(gm_rgb_resync_count),
                         static_cast<unsigned>(gm_rgb_resync_bufs),
                         static_cast<unsigned>(gm_rgb_resync_max),
                         static_cast<unsigned>(gm_rgb_over_count), static_cast<unsigned>(gm_rgb_over_bufs),
                         static_cast<unsigned>(panelclock::phyTrackDeferred()),
                         static_cast<unsigned>(gm_rgb_flash_skip_bufs),
                         static_cast<unsigned>(gm_rgb_restart_count),
                         static_cast<unsigned>(gm_rgb_catchup_count),
                         static_cast<unsigned>(gm_rgb_catchup_bufs),
                         static_cast<unsigned>(gm_rgb_catchup_max),
                         static_cast<unsigned>(gm_rgb_eof_expect),
                         static_cast<unsigned>(gm_rgb_eof_min),
                         static_cast<unsigned>(gm_rgb_eof_max));
        for (size_t i = 0; i < n; i++) {
            response->printf(
                "%s{\"frame\":%u,\"t_us\":%u,\"margin_us\":%u,\"overlay_us\":%u,\"flash_us\":%u,\"band_us\":%u,"
                "\"present_us\":%u,\"phy_us\":%u}",
                i ? "," : "", static_cast<unsigned>(slipLog[i].frame), static_cast<unsigned>(slipLog[i].tUs),
                static_cast<unsigned>(slipLog[i].marginUs),
                static_cast<unsigned>(slipLog[i].sinceUs[panelclock::SCANOUT_ACT_OVERLAY]),
                static_cast<unsigned>(slipLog[i].sinceUs[panelclock::SCANOUT_ACT_FLASH]),
                static_cast<unsigned>(slipLog[i].sinceUs[panelclock::SCANOUT_ACT_BANDPUSH]),
                static_cast<unsigned>(slipLog[i].sinceUs[panelclock::SCANOUT_ACT_PRESENT]),
                static_cast<unsigned>(slipLog[i].sinceUs[panelclock::SCANOUT_ACT_PHY]));
        }
        // busy_hist is how long the refill handler spent copying, gap_hist how long it
        // waited between calls, both in 32 us buckets. They separate the two faults that
        // look identical from the frame counters: a refill that is slow because PSRAM is
        // contended piles up in busy_hist, one that is late because its interrupt was
        // masked piles up in gap_hist while busy_hist stays flat.
        response->printf("],\"hist_bucket_us\":32,\"busy_max_us\":%u,\"gap_max_us\":%u,\"busy_hist\":[",
                         static_cast<unsigned>(gm_rgb_busy_max), static_cast<unsigned>(gm_rgb_gap_max));
        for (int i = 0; i < 24; i++) {
            response->printf("%s%u", i ? "," : "", static_cast<unsigned>(gm_rgb_busy_hist[i]));
        }
        response->print("],\"gap_hist\":[");
        for (int i = 0; i < 24; i++) {
            response->printf("%s%u", i ? "," : "", static_cast<unsigned>(gm_rgb_gap_hist[i]));
        }
        // chunk_hist: every 240 B chunk of every refill copy, 16 us buckets. The
        // shape of a slow copy: one 400 us chunk is a bus freeze, many 7 us
        // chunks is a shared bus.
        response->printf("],\"chunk_bucket_us\":16,\"chunk_max_us\":%u,\"chunk_hist\":[", static_cast<unsigned>(gm_rgb_chunk_max_us));
        for (int i = 0; i < 24; i++) {
            response->printf("%s%u", i ? "," : "", static_cast<unsigned>(gm_rgb_chunk_hist[i]));
        }
        // gaplog: one event per refill gap over 400 us or refill copy over 250 us
        // (see the GM_RGB_GAPLOG_PATCH comment in scripts/patch_esp_lcd_rgb.py).
        // gap_us - prev_busy_us is the true interrupt latency; fills is how far
        // the DMA got ahead; stall_us over ~100 means a bus freeze rather than a
        // shared bus; other is the task on the other core, the bus competitor.
        // Run xtensa-esp32s3-elf-addr2line -e firmware.elf on the pcs. Newest
        // last; gaplog_total is the count since reset so a full ring is not
        // mistaken for exactly 32 events.
        const uint32_t gapTotal = gm_rgb_gaplog_n;
        const uint32_t gapN = gapTotal < static_cast<uint32_t>(GM_RGB_GAPLOG_N) ? gapTotal : GM_RGB_GAPLOG_N;
        const uint32_t gapStart = gapTotal > static_cast<uint32_t>(GM_RGB_GAPLOG_N) ? gapTotal - GM_RGB_GAPLOG_N : 0;
        response->printf("],\"gaplog_total\":%u,\"gaplog\":[", static_cast<unsigned>(gapTotal));
        for (uint32_t i = 0; i < gapN; i++) {
            const volatile gm_rgb_gap_ev_t &ev = gm_rgb_gaplog[(gapStart + i) % GM_RGB_GAPLOG_N];
            const char *task = ev.task ? ev.task : "?";
            const char *other = ev.other ? ev.other : "?";
            response->printf("%s{\"t_ms\":%u,\"gap_us\":%u,\"prev_busy_us\":%u,\"busy_us\":%u,\"fills\":%u,\"nest\":%u,\"pos\":%u,"
                             "\"stall_us\":%u,\"stall_at\":%u,\"pc\":\"0x%08x\",\"ps\":\"0x%08x\",\"task\":\"%s\",\"other\":\"%s\"}",
                             i ? "," : "", static_cast<unsigned>(ev.t_ms), static_cast<unsigned>(ev.gap_us),
                             static_cast<unsigned>(ev.prev_busy_us), static_cast<unsigned>(ev.busy_us),
                             static_cast<unsigned>(ev.fills), static_cast<unsigned>(ev.nest), static_cast<unsigned>(ev.pos),
                             static_cast<unsigned>(ev.stall_us), static_cast<unsigned>(ev.stall_at),
                             static_cast<unsigned>(ev.pc), static_cast<unsigned>(ev.ps), task, other);
        }
        response->print("]}");
        request->send(response);
    });
    // Times a full heap walk over each region, which is what a memory sample
    // costs. Deliberately its own endpoint: calling it perturbs the display, so
    // it must not be folded into a status poll something scrapes on a timer.
    server.on("/api/debug/heapwalk", [](AsyncWebServerRequest *request) {
        multi_heap_info_t info{};
        const int64_t a = esp_timer_get_time();
        heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        const int64_t b = esp_timer_get_time();
        heap_caps_get_info(&info, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        const int64_t c = esp_timer_get_time();
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"internal_us\":%u,\"psram_us\":%u,\"deadline_us\":162}", static_cast<unsigned>(b - a),
                 static_cast<unsigned>(c - b));
        request->send(200, "application/json", buf);
    });
#ifndef GAGGIMATE_HEADLESS
    // Live ST7701S inversion-mode tuning: /api/debug/panelreg?inv=49
    //
    // INVSET's first byte (BK0 0xC2) selects the inversion mode; the panel
    // ships with 0x31, 49 decimal. It is not persisted, so a reboot puts the
    // init table's value back and a bad sweep cannot leave the panel wrong.
    //
    // VCOM used to be swept here too and is now the panelVcom setting instead,
    // which is both persisted and applied live. Do not add it back: the setting
    // is only re-applied when its value changes, so a poke from here would sit
    // on top of it invisibly and the slider would appear to do nothing on the
    // way back to the value it already held.
    server.on("/api/debug/panelreg", [](AsyncWebServerRequest *request) {
        LilyGoDriver *drv = LilyGoDriver::peekInstance();
        if (drv == nullptr) {
            request->send(404, "application/json", "{\"error\":\"not a LilyGo panel\"}");
            return;
        }
        int inv = -1;
        if (request->hasArg("inv")) {
            inv = request->arg("inv").toInt();
            if (inv >= 0 && inv <= 255) {
                drv->setPanelInversion(static_cast<uint8_t>(inv));
            }
        }
        char buf[96];
        snprintf(buf, sizeof(buf), "{\"inv\":%d,\"shipped_inv\":49}", inv);
        request->send(200, "application/json", buf);
    });

    // /api/debug/flashchurn[?kb=64] writes that many kilobytes to a scratch
    // file on the internal-flash LittleFS partition in 4 KB flushed chunks,
    // then deletes it. Every flush programs flash with the cache disabled,
    // which is the same stall a production machine's shot recording produces
    // every ~42 s -- but a bench with an SD card logs shots to the card, so
    // its brews never take the flash cache down and the path goes untested.
    // Pair it with /api/debug/scanout: flash_skips climbing during the churn
    // while resyncs hold still is the LCD refill riding out the cache-down
    // window instead of being masked by it.
    server.on("/api/debug/flashchurn", [](AsyncWebServerRequest *request) {
        int kb = 64;
        if (request->hasArg("kb")) {
            kb = request->arg("kb").toInt();
        }
        kb = std::min(std::max(kb, 4), 512);
        // Static because this runs on the async_tcp task, whose stack is not
        // sized for a 4 KB buffer. The endpoint is a bench tool; one caller
        // at a time is its contract.
        static uint8_t chunk[4096];
        for (size_t i = 0; i < sizeof(chunk); i++) {
            chunk[i] = static_cast<uint8_t>(i * 31 + kb);
        }
        const char *path = "/gm_flashchurn.tmp";
        const int64_t t0 = esp_timer_get_time();
        File f = LittleFS.open(path, FILE_WRITE);
        if (!f) {
            request->send(500, "application/json", "{\"error\":\"littlefs open failed\"}");
            return;
        }
        size_t written = 0;
        for (int i = 0; i < kb / 4; i++) {
            // Same marker the shot recorder sets, so the slip attribution log
            // blames these windows on flash rather than on a bystander.
            panelclock::scanoutMark(panelclock::SCANOUT_ACT_FLASH);
            written += f.write(chunk, sizeof(chunk));
            f.flush();
        }
        f.close();
        LittleFS.remove(path);
        const int dtMs = static_cast<int>((esp_timer_get_time() - t0) / 1000);
        char buf[96];
        snprintf(buf, sizeof(buf), "{\"written\":%u,\"ms\":%d}", static_cast<unsigned>(written), dtMs);
        request->send(200, "application/json", buf);
    });

#ifdef GM_TOUCH_PROBE
    // Synthetic core-1 PSRAM load: the falsification test for the planned
    // core-1 render helper. The helper idea puts kernel work on core 1 at
    // priority 0 (under the UI task), where a task cannot delay the panel's
    // core-1 ISRs but CAN slow the bounce refill's copy through MSPI/dcache
    // contention -- the one risk code review cannot settle. This task
    // reproduces that bus pressure without any of the helper's machinery:
    // it streams 4 KB memcpys through two 64 KB PSRAM buffers (working set
    // 4x the 32 KB dcache, so the traffic stays real) whenever core 1 is
    // otherwise idle. Toggle it within one boot per the rig rules and watch
    // GM_SCANOUT slips/busy_max and GM_TOUCHLAT: if this alone moves them,
    // the helper is dead before it is written.
    static volatile bool s_c1LoadRun = false;
    static volatile uint32_t s_c1LoadIters = 0;
    static TaskHandle_t s_c1LoadTask = nullptr;
#endif

    // /api/debug/anim[?direct=0|1][&dma=0|1] reads and live-sets who owns the
    // panel's framebuffer pair while the background animation is running.
    //
    // Both settings produce a visible defect and neither moves the scan-out
    // slip counter, which is why this needs to be switchable with someone
    // watching the panel:
    //
    //   direct=1  the animation renders into the buffer the panel is NOT
    //             scanning and flips at the frame boundary. Correct by
    //             construction, provided the animation really is the pair's
    //             only writer.
    //   direct=0  the bands go out through pushColors, which is
    //             esp_lcd_panel_draw_bitmap into _fbDirect[_fbCurrent] -- the
    //             buffer being scanned right now. Every band write races the
    //             beam. The scan-out never starves, so slips stay near zero
    //             while the picture tears.
    //
    // Live and not persisted; the next boot goes back to the compiled default.
    server.on("/api/debug/anim", [](AsyncWebServerRequest *request) {
        SleepAnimation *a = sleep_animation_bench_instance();
        if (a == nullptr) {
            request->send(409, "application/json", "{\"error\":\"animation not running\"}");
            return;
        }
        if (request->hasArg("direct")) {
            a->setDirectPush(request->arg("direct").toInt() != 0);
        }
        if (request->hasArg("dma")) {
            a->setDmaWanted(request->arg("dma").toInt() != 0);
        }
        if (request->hasArg("half")) {
            a->setHalfRes(request->arg("half").toInt() != 0);
        }
        // Both land on setInterlaceForce(), not setInterlace() directly: the
        // latter is what DefaultUI::updateState() calls every UI pass with
        // the persisted setting, so a direct call here would be silently
        // reverted on the next pass (measured: an ilace=0/interlace=0 request
        // that "did nothing" was this, not a parsing bug). setInterlaceForce
        // pins the value against that periodic re-apply the same way
        // forcehalf already pins resolution against it; -1 releases the pin
        // and hands control back to the persisted setting, 0/1 hold it.
        if (request->hasArg("ilace")) {
            a->setInterlaceForce(static_cast<int8_t>(request->arg("ilace").toInt()));
        }
        // interlace=-1|0|1 is the same knob as ilace= above, both landing on
        // setInterlaceForce() -- it defaults false now that the direct-DMA
        // path (SleepAnimation.h's interlace/renderHalf comments) also
        // honours it, so the interlace lane's ladder has an explicit, named
        // opt-in to rung against rather than inheriting whichever value
        // ilace= last left behind under a name that predates this feature.
        if (request->hasArg("interlace")) {
            a->setInterlaceForce(static_cast<int8_t>(request->arg("interlace").toInt()));
        }
        // forcehalf pins the resolution: -1 auto, 0 full, 1 half. half= only
        // raises or drops the ceiling and autoResolution still gets the vote,
        // which is not enough to hold one variable still across a capture.
        if (request->hasArg("pattern")) {
            a->setDebugPattern(request->arg("pattern").toInt());
        }
        // capfps=N pins the animation's frame rate regardless of the stored
        // setting; 0 releases it. This is how much of the scan-out's refill
        // headroom the animation's PSRAM traffic is costing, as a curve.
        if (request->hasArg("capfps")) {
            const int f = request->arg("capfps").toInt();
            if (f >= 0 && f <= 60) {
                a->setFpsOverride(static_cast<uint8_t>(f));
            }
        }
        // testpattern=1 replaces the image with a decodable scan-out ramp. It is
        // the only way to judge the panel from a photograph rather than by eye.
        if (request->hasArg("testpattern")) {
            a->setTestPattern(request->arg("testpattern").toInt() != 0);
        }
        // rprio=N re-prioritises the render task live (clamped to [1,4], see
        // SleepAnimation::setRenderPrio). The band bracket is pure compute, so
        // fps against rprio is a direct read of how much of the render task's
        // wall time is core-0 preemption. Measurement knob, not a shipping
        // arrangement: 3+ delays the control loop.
        if (request->hasArg("rprio")) {
            a->setRenderPrio(request->arg("rprio").toInt());
        }
#ifdef GM_TOUCH_PROBE
        // c1load=0|1 starts/stops the synthetic core-1 PSRAM load declared
        // above. Off is asynchronous (the task frees its buffers and deletes
        // itself), so a fast off->on can see the old task still winding down
        // and skip the create; toggle off, snapshot until c1load reads false,
        // then toggle on. Bench knob, volatile across reboot like the rest.
        if (request->hasArg("c1load")) {
            const bool want = request->arg("c1load").toInt() != 0;
            if (want && s_c1LoadTask == nullptr) {
                s_c1LoadRun = true;
                s_c1LoadIters = 0;
                xTaskCreatePinnedToCore(
                    [](void *) {
                        constexpr size_t kBuf = 64 * 1024;
                        uint8_t *src = static_cast<uint8_t *>(heap_caps_malloc(kBuf, MALLOC_CAP_SPIRAM));
                        uint8_t *dst = static_cast<uint8_t *>(heap_caps_malloc(kBuf, MALLOC_CAP_SPIRAM));
                        size_t off = 0;
                        while (s_c1LoadRun && src != nullptr && dst != nullptr) {
                            memcpy(dst + off, src + off, 4096);
                            off = (off + 4096) % kBuf;
                            s_c1LoadIters = s_c1LoadIters + 1;
                        }
                        free(src);
                        free(dst);
                        s_c1LoadTask = nullptr;
                        vTaskDelete(nullptr);
                    },
                    "c1load", 3072, nullptr, 0, &s_c1LoadTask, 1);
            } else if (!want) {
                s_c1LoadRun = false;
            }
        }
#endif
        if (request->hasArg("forcehalf")) {
            a->setHalfForce(static_cast<int8_t>(request->arg("forcehalf").toInt()));
        }
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        // PSRAM-backed like every other JsonDocument in this file that
        // carries more than a couple of fields (lines 146/157/167/1148):
        // this handler is polled every few seconds for tens of minutes by
        // rig soaks, and the plain default allocator would keep growing and
        // freeing an internal-heap block on every poll instead.
        JsonDocument doc(&psramAllocator);
        doc["direct"] = a->directPush();
        doc["dma"] = a->dmaPathWanted();
        doc["rprio"] = a->renderPrioValue();
        doc["half"] = a->halfResOn();
        doc["forcehalf"] = a->halfForced();
        // Reports which way ilace=/interlace= is actually set, not which one
        // last asked -- see SleepAnimation::interlaceEnabled()'s comment. The
        // ladder rungs this against need to read this back to confirm the
        // rung landed, since a request that never arrives leaves the boot
        // default (false) standing rather than erroring visibly.
        doc["interlace"] = a->interlaceEnabled();
        // -1/0/1, mirroring forcehalf: whether a debug-endpoint pin is
        // currently holding interlace away from the persisted setting. A
        // rung that expects to see interlace flip on the NEXT settings
        // change (rather than staying pinned) should see -1 here first.
        doc["interlace_force"] = a->interlaceForced();
        doc["pattern"] = a->debugPatternOn();
        doc["msync_fail"] = a->msyncFailCount();
        doc["msync_ok"] = a->msyncOkCount();
        // tear_live over tear_checked is the tearing rate on the direct path.
        // tear_checked is reported alongside so a zero cannot be confused with
        // a check that never ran.
        doc["tear_live"] = a->liveWriteCount();
        doc["tear_checked"] = a->liveWriteCheckedCount();
        // Deliberately separate from tear_live: this is interlacing writing
        // the live buffer ON PURPOSE (see interlacedLiveWriteCount()'s own
        // comment), so it climbs continuously while interlace=1 is running
        // and a nonzero tear_live stays a bug report the whole time.
        doc["interlaced_live_writes"] = a->interlacedLiveWriteCount();
        doc["flip_timeouts"] = a->flipTimeoutCount();
        doc["frame_us"] = a->lastFrameUsValue();
        doc["work_us"] = a->lastWorkUsValue();
        doc["wait_us"] = a->lastWaitUsValue();
        doc["band_us"] = a->lastBandUsValue();
        doc["expand_us"] = a->lastExpandUsValue();
        doc["fill_us"] = a->lastFillUsValue();
        doc["copy_us"] = a->lastCopyUsValue();
        doc["half_psram"] = esp_ptr_external_ram(const_cast<void *>(a->halfBufAddr()));
        doc["blend_us"] = a->lastBlendUsValue();
        doc["msync_us"] = a->lastMsyncUsValue();
        doc["push_us"] = a->lastPushUsValue();
        for (int i = 0; i < 2; i++) {
            const void *bp = a->bandBufAddr(i);
            doc["band_psram"][i] = bp != nullptr && esp_ptr_external_ram(bp);
            doc["band_align"][i] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(bp) & 63u);
        }
        doc["dma_errors"] = a->dmaErrorCount();
        // A subset of dma_errors above, specifically submitRows() failures --
        // see dmaRowFallbackCount()'s own comment for why the two are watched
        // separately (a rising count here implicates the row-group mount
        // rather than the plain whole-band submit()).
        doc["dma_row_fallbacks"] = a->dmaRowFallbackCount();
        // Bands where interlace was requested but half resolution vetoed it
        // outright (SleepAnimation.cpp's bandInterlaced comment): forcehalf=1
        // with interlace/interlace_force on wedged the pipeline to ~0.2 fps
        // before this existed. A soak run at that combination should see this
        // climb by h/BAND_H every frame -- if it stays flat instead while
        // half=1 and interlace=1 both read true, the veto is not firing and
        // the wedge risk is back.
        doc["half_ilace_veto"] = a->halfInterlaceVetoCount();
        // Legitimate "this band's row-pair is the other phase's turn"
        // no-ops -- confirmed root cause of the half+interlace wedge and
        // its photographed corruption (see SleepAnimation.cpp's
        // nothingOwnedThisBand comment): these used to be misrouted through
        // dma_errors and a whole-band CPU pushColors fallback instead.
        // dma_row_fallbacks never counted them (it only counts a genuine
        // submitRows() failure), which is why it read 0 while dma_errors
        // climbed into the thousands during the rig soak that caught this.
        doc["interlace_band_skips"] = a->interlaceBandSkipCount();
        // Bands the regional overlay-update warmup forced full this run
        // (requestBandWarmup(), SleepAnimation.h's bandsForcedFullCount()
        // comment): near zero on a static screen, a handful (2-12, not 240)
        // right after one widget changes. Distinguishes "the regional
        // mechanism is doing its job" from "it never got a dirty rect to
        // work with", the same way half_ilace_veto and interlace_band_skips
        // above distinguish their own mechanisms from a dead knob.
        doc["bands_forced_full"] = a->bandsForcedFullCount();
        {
            // Band-DMA transfer durations. bdma_over512 climbing at the
            // refill's resync rate means a GDMA PSRAM access queues behind
            // the same bus stall the CPU memcpy does; staying at zero means
            // it dodges it. BandDma.h carries the full argument.
            uint32_t n = 0, sum = 0, mx = 0, o256 = 0, o512 = 0;
            a->dmaXferStats(&n, &sum, &mx, &o256, &o512);
            doc["bdma_n"] = n;
            doc["bdma_mean_us"] = n != 0 ? sum / n : 0;
            doc["bdma_max_us"] = mx;
            doc["bdma_over256"] = o256;
            doc["bdma_over512"] = o512;
        }
        // fb_mismatch over fb_checked is the rate at which a band's content
        // failed to reach the framebuffer row it was rendered for, which is the
        // fault the panel shows as a block of lines displaced vertically. This
        // is the only counter here that can see it: tear_live compares the
        // scan-out buffer against the render target, and the panel driver's
        // slip counters only see the scan, by which point the framebuffer is
        // already wrong. fb_delta is the displacement of the last mismatch in
        // bands, or 0 when no other band held the content either.
        doc["fb_checked"] = a->fbCheckedCount();
        doc["fb_mismatch"] = a->fbMismatchCount();
        doc["fb_band"] = a->fbLastBandIndex();
        doc["fb_source"] = a->fbLastSourceIndex();
        doc["fb_delta"] = a->fbLastDeltaBands();
        doc["inval_us"] = a->lastInvalidateUs();
        doc["capfps"] = a->fpsOverrideValue();
        doc["testpattern"] = a->testPatternOn();
        uint32_t frames = 0, refills = 0, slips = 0;
        panelclock::scanoutStats(&frames, &refills, &slips);
        doc["frames"] = frames;
        doc["refills"] = refills;
        doc["slips"] = slips;
#ifdef GM_TOUCH_PROBE
        doc["c1load"] = s_c1LoadTask != nullptr;
        doc["c1load_iters"] = s_c1LoadIters;
#endif
        serializeJson(doc, *response);
        request->send(response);
    });

    // /api/debug/radio?wifioff=SECS (bench only): take WiFi down for SECS
    // seconds, then bring it back with the STA watchdog's restart sequence.
    // The scan-out counters keep accumulating while the radio is off, so one
    // /api/debug/scanout?reset=1 before and one read after the window gives a
    // WiFi-off sample on exactly the same counters as a WiFi-on one, within one
    // boot. That is the within-boot A/B the old "noradio" build could never
    // give (it measured slips over serial, in a different memory layout).
    // WiFi goes down 1.5 s after the reply so the response gets out. SECS is
    // clamped to 20..180: below the STA watchdog's 20 s grace nothing new is
    // learned, above three minutes the reboot rung starts to matter.
    server.on("/api/debug/radio", [this](AsyncWebServerRequest *request) {
        long secs = 0;
        if (request->hasParam("wifioff")) {
            secs = request->getParam("wifioff")->value().toInt();
            if (secs < 20)
                secs = 20;
            if (secs > 180)
                secs = 180;
            const unsigned long tnow = millis();
            radioOffAtMs = tnow + 1500;
            radioOnAtMs = radioOffAtMs + static_cast<unsigned long>(secs) * 1000UL;
        }
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        response->printf("{\"scheduled_off_secs\":%ld,\"off_pending\":%d,\"on_pending\":%d,\"mode\":%d,\"status\":%d}", secs,
                         radioOffAtMs != 0, radioOnAtMs != 0, static_cast<int>(WiFi.getMode()), static_cast<int>(WiFi.status()));
        request->send(response);
    });
    // /api/debug/coex[?idlemin=N&idlemax=M] reads and live-sets the IDLE BLE
    // connection interval (1.25ms units: 24 == 30ms), then reports the state.
    //
    // This is a within-boot A/B knob for the scan-out. The resync rate
    // (displaced bands) is non-stationary -- it swings ~4x run-to-run -- so a
    // compile-time interval change cannot be told apart from noise across two
    // separate flashes. Toggle this live between intervals, resetting
    // /api/debug/scanout per phase, and both configs are sampled against the
    // SAME ambient conditions. Measured 2026-09-01: tight (7.5-10 ms) and wide
    // (200-300 ms) intervals both resync more than the 30-50 ms default, so the
    // default stays. idlemin=0 releases the override to it. The change only
    // re-issues the connection-param update while idle (not mid-shot) and
    // connected; it is volatile across boot.
    server.on("/api/debug/coex", [this](AsyncWebServerRequest *request) {
        GaggiMateClient *client = controller->getClientController();
        if (client == nullptr) {
            request->send(409, "application/json", "{\"error\":\"no client\"}");
            return;
        }
        if (request->hasArg("idlemin")) {
            long mn = request->arg("idlemin").toInt();
            // idlemax defaults to idlemin when omitted (a single fixed interval).
            long mx = request->hasArg("idlemax") ? request->arg("idlemax").toInt() : mn;
            if (mn < 0)
                mn = 0; // clamp; 0 clears the override
            if (mx < mn)
                mx = mn;
            // Guard against nonsense that would trip NimBLE's own validation:
            // interval units are 1.25ms and the spec caps at 0x0C80 (4000ms).
            if (mn > 3200)
                mn = 3200;
            if (mx > 3200)
                mx = 3200;
            client->setIdleInterval(static_cast<uint16_t>(mn), static_cast<uint16_t>(mx));
        }
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        JsonDocument doc(&psramAllocator);
        doc["connected"] = client->isConnected();
        // The interval the link uses while idle right now (override or default).
        doc["idle_min"] = client->idleMinInterval();
        doc["idle_max"] = client->idleMaxInterval();
        // Reported in ms for the operator; units above are 1.25ms.
        doc["idle_min_ms"] = client->idleMinInterval() * 1.25f;
        doc["idle_max_ms"] = client->idleMaxInterval() * 1.25f;
        if (client->hasLatency())
            doc["lat_ms"] = client->getLatencyMs();
        serializeJson(doc, *response);
        request->send(response);
    });

    // /api/debug/pclk[?div=n] reads and live-sets the RGB pixel
    // clock divider (pclk = 80 MHz / n) and reports the scan-out counters.
    //
    // The divider is the one lever on the bounce-refill deadline that costs no
    // memory. Deadline is lines * htotal / pclk, so a slower clock buys slack
    // per refill, where more bounce lines buy it out of the same DMA-capable
    // internal DRAM that WiFi's TX buffers come from. That tradeoff has already
    // been got wrong once in both directions, so it needs sweeping against real
    // load rather than reasoning about.
    //
    // Live and deliberately not persisted: this reverts to the stored setting
    // on the next boot, so a sweep that ends badly cannot leave the panel
    // wrong. The persisted control is the panelClockDiv setting.
    //
    // The counters are cumulative and there is no reset: a sweep takes the
    // difference between two reads of this endpoint, which keeps the reset
    // logic out of the ISR-side counters entirely.
    server.on("/api/debug/pclk", [](AsyncWebServerRequest *request) {
        if (request->hasArg("div")) {
            const int div = request->arg("div").toInt();
            if (div < 2 || div > 16) {
                request->send(400, "application/json", "{\"error\":\"div out of range 2..16\"}");
                return;
            }
            panelclock::setDiv(div);
        }
        uint32_t frames = 0, refills = 0, slips = 0;
        panelclock::scanoutStats(&frames, &refills, &slips);
        char buf[192];
        snprintf(buf, sizeof(buf), "{\"div\":%d,\"live\":%s,\"hz\":%u,\"frames\":%u,\"refills\":%u,\"slips\":%u}",
                 panelclock::currentDiv(), panelclock::hasLiveControl() ? "true" : "false",
                 static_cast<unsigned>(80000000UL / (panelclock::currentDiv() > 0 ? panelclock::currentDiv() : 1)),
                 static_cast<unsigned>(frames), static_cast<unsigned>(refills), static_cast<unsigned>(slips));
        request->send(200, "application/json", buf);
    });

    // /api/debug/fb?n=0|1[&step=2] streams one panel framebuffer as raw
    // RGB565, little-endian, row-major, step**2 decimated.
    //
    // This exists because the scan-out slip counter answers a different
    // question than "is the picture right". It counts bounce-buffer refills
    // that missed their deadline, and it reported 0.032% while every element
    // on the panel was visibly drawn twice, 25 px apart. A photograph proves
    // something is wrong but cannot say whether the duplicate is in the pixels
    // or only in the scan-out, and those two have opposite fixes. Reading the
    // buffers settles it: if the ghost is here, the compositor put it here.
    //
    // Both buffers are dumpable separately on purpose. With two framebuffers
    // alternating at 43 fps, content sitting at different offsets in each one
    // shows up on camera as a stable double image, which is exactly the
    // symptom, so comparing 0 against 1 is the first thing worth doing.
    server.on("/api/debug/fb", [](AsyncWebServerRequest *request) {
        LilyGoDriver *drv = LilyGoDriver::peekInstance();
        Display *disp = drv != nullptr ? drv->getDisplay() : nullptr;
        if (disp == nullptr) {
            request->send(404, "application/json", "{\"error\":\"not a LilyGo panel\"}");
            return;
        }
        const int idx = request->hasArg("n") ? request->arg("n").toInt() : 0;
        if (idx < 0 || idx >= disp->frameBufferCount()) {
            request->send(400, "application/json", "{\"error\":\"bad buffer index\"}");
            return;
        }
        const uint16_t *fb = disp->directFrameBuffer(idx);
        if (fb == nullptr) {
            request->send(404, "application/json", "{\"error\":\"no direct framebuffer\"}");
            return;
        }
        // Read past the data cache, or this endpoint reports what the CPU last
        // happened to hold rather than what is in the framebuffer. On the
        // direct path the bands arrive over GDMA straight into PSRAM, which
        // does not go through the cache, so any line still resident from an
        // earlier CPU write wins the read and the dump quietly shows old
        // pixels. That is the same hazard the animation's own present path
        // documents, in the same direction, and this instrument is used to
        // decide whether the display is correct, so it must not have it.
        //
        // Writeback first, then invalidate. A bare invalidate would be right
        // while the animation owns the pair (DMA is the only writer) and would
        // silently discard LVGL's dirty lines when it does not, which is a
        // corrupted panel rather than a bad measurement.
        if (const size_t fbBytes = static_cast<size_t>(disp->width()) * disp->height() * 2) {
            void *base = const_cast<uint16_t *>(fb);
            esp_cache_msync(base, fbBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
            esp_cache_msync(base, fbBytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
        }
        int step = request->hasArg("step") ? request->arg("step").toInt() : 1;
        if (step < 1 || step > 8)
            step = 1;
        const int w = disp->width();
        const int h = disp->height();
        const int ow = w / step;
        const int oh = h / step;
        // Chunked, because a full 480x480 buffer is 460,800 bytes and this
        // board has no business allocating that to answer a debug request. The
        // callback is handed a row budget and fills whole output rows only, so
        // it never has to carry a partial pixel across chunks.
        auto *state = new int(0);
        AsyncWebServerResponse *response = request->beginChunkedResponse(
            "application/octet-stream", [fb, w, step, ow, oh, state](uint8_t *out, size_t maxLen, size_t) -> size_t {
                const size_t rowBytes = static_cast<size_t>(ow) * 2;
                size_t written = 0;
                while (*state < oh && written + rowBytes <= maxLen) {
                    const uint16_t *src = fb + static_cast<size_t>(*state) * step * w;
                    uint16_t *dst = reinterpret_cast<uint16_t *>(out + written);
                    for (int x = 0; x < ow; x++)
                        dst[x] = src[x * step];
                    written += rowBytes;
                    (*state)++;
                }
                if (written == 0)
                    delete state;
                return written;
            });
        char disposition[64];
        snprintf(disposition, sizeof(disposition), "%dx%d", ow, oh);
        response->addHeader("X-FB-Size", disposition);
        request->send(response);
    });
#endif // GAGGIMATE_HEADLESS
    server.on("/api/status", [this](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        JsonDocument doc(&psramAllocator);
        doc["mode"] = controller->getMode();
        doc["tt"] = controller->getTargetTemp();
        doc["ct"] = controller->getCurrentTemp();
        serializeJson(doc, *response);
        request->send(response);
    });
#ifdef GM_ANIM_BENCH
    // Bench build only: raw memory-path throughput, to attribute the flat ~19 ms
    // push cost. esp_lcd_panel_draw_bitmap on an fb_in_psram panel is a CPU
    // memcpy into the PSRAM framebuffer plus a cache writeback, so push should
    // be bounded by whatever "memcpy SRAM -> PSRAM" measures here. The
    // interesting comparison is against the read direction: a write that costs
    // about twice a read is the signature of read-for-ownership, since the
    // 32-byte write-allocate cache line gets fetched from PSRAM before it is
    // overwritten. If that holds, a GDMA transfer -- which never passes through
    // the CPU cache -- moves the same bytes for half the traffic, which is why
    // esp_async_memcpy is measured alongside. Everything runs with the panel
    // scanning out, so the numbers include the contention push really sees.
    // What is actually programmed into the GDMA channels, read back from the
    // hardware rather than assumed. Two things worth knowing: which channel the
    // RGB panel driver took (it never exposes its handle, so the only way to
    // find it from outside is to scan the peripheral-select registers for
    // LCD_CAM's trigger ID), and what external-memory block size each channel
    // is running.
    //
    // That second one matters because the esp32s3 register field documents only
    // 16 and 32 bytes as valid -- gdma_struct.h:212, "0: 16 bytes 1: 32 bytes
    // 2/3:reserved" -- while the shared LL header still offers a 64B constant
    // that is legal only on other targets. Both the panel init and the async
    // memcpy config in this tree ask for 64.
    //
    // The poke arguments write the same fields at runtime so their effect can be
    // measured without a reflash: ?ch=N with bkin/bkout (0=16B, 1=32B, 2=64B)
    // and priin/priout (0-15).
    server.on("/api/gdma", [this](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        JsonDocument doc;
        if (request->hasArg("ch")) {
            const int ch = request->arg("ch").toInt();
            if (ch >= 0 && ch < 5) {
                if (request->hasArg("bkin")) {
                    GDMA.channel[ch].in.conf1.in_ext_mem_bk_size = request->arg("bkin").toInt() & 0x3;
                }
                if (request->hasArg("bkout")) {
                    GDMA.channel[ch].out.conf1.out_ext_mem_bk_size = request->arg("bkout").toInt() & 0x3;
                }
                if (request->hasArg("priin")) {
                    GDMA.channel[ch].in.pri.rx_pri = request->arg("priin").toInt() & 0xF;
                }
                if (request->hasArg("priout")) {
                    GDMA.channel[ch].out.pri.tx_pri = request->arg("priout").toInt() & 0xF;
                }
                doc["poked"] = ch;
            }
        }
        JsonArray chans = doc["channels"].to<JsonArray>();
        for (int ch = 0; ch < 5; ch++) {
            JsonObject o = chans.add<JsonObject>();
            o["ch"] = ch;
            o["in_sel"] = static_cast<uint32_t>(GDMA.channel[ch].in.peri_sel.sel);
            o["out_sel"] = static_cast<uint32_t>(GDMA.channel[ch].out.peri_sel.sel);
            o["mem_trans"] = static_cast<uint32_t>(GDMA.channel[ch].in.conf0.mem_trans_en);
            o["in_bk"] = static_cast<uint32_t>(GDMA.channel[ch].in.conf1.in_ext_mem_bk_size);
            o["out_bk"] = static_cast<uint32_t>(GDMA.channel[ch].out.conf1.out_ext_mem_bk_size);
            o["in_pri"] = static_cast<uint32_t>(GDMA.channel[ch].in.pri.rx_pri);
            o["out_pri"] = static_cast<uint32_t>(GDMA.channel[ch].out.pri.tx_pri);
            // The whole "M2M starves the LCD" theory predicts exactly one thing:
            // the LCD channel's transmit FIFO runs dry. These are the raw
            // interrupt status bits for that, sticky until cleared, so the
            // hypothesis stops being an inference. l1 is the per-channel FIFO,
            // l3 the shared one.
            o["outfifo_udf"] = static_cast<uint32_t>(GDMA.channel[ch].out.int_raw.outfifo_udf_l1) |
                               (static_cast<uint32_t>(GDMA.channel[ch].out.int_raw.outfifo_udf_l3) << 1);
            o["outfifo_ovf"] = static_cast<uint32_t>(GDMA.channel[ch].out.int_raw.outfifo_ovf_l1) |
                               (static_cast<uint32_t>(GDMA.channel[ch].out.int_raw.outfifo_ovf_l3) << 1);
            o["infifo_udf"] = static_cast<uint32_t>(GDMA.channel[ch].in.int_raw.infifo_udf_l1) |
                              (static_cast<uint32_t>(GDMA.channel[ch].in.int_raw.infifo_udf_l3) << 1);
            o["infifo_ovf"] = static_cast<uint32_t>(GDMA.channel[ch].in.int_raw.infifo_ovf_l1) |
                              (static_cast<uint32_t>(GDMA.channel[ch].in.int_raw.infifo_ovf_l3) << 1);
            if (request->hasArg("clr")) {
                GDMA.channel[ch].out.int_clr.val = 0xFFFFFFFF;
                GDMA.channel[ch].in.int_clr.val = 0xFFFFFFFF;
            }
        }
        doc["lcd_cam_trig_id"] = static_cast<uint32_t>(SOC_GDMA_TRIG_PERIPH_LCD0);
        doc["arb_pri_dis"] = static_cast<uint32_t>(GDMA.misc_conf.arb_pri_dis);
        serializeJson(doc, *response);
        request->send(response);
    });

    // Raw capture of what is actually on the panel, so a corruption claim can
    // be settled with bytes. ?src=fb (default) returns the RGB565 framebuffer,
    // ?src=ov the live overlay as RGB565+A8. Both are little-endian and
    // row-major; the geometry comes back in the headers because the overlay is
    // larger than the panel by the host object's ext draw size.
    server.on("/api/fbdump", [](AsyncWebServerRequest *request) {
        SleepAnimation *a = sleep_animation_bench_instance();
        if (a == nullptr) {
            request->send(503, "text/plain", "animation not running");
            return;
        }
        const bool wantOverlay = request->hasArg("src") && request->arg("src") == "ov";
        // 480x480 at 3 B/px covers both shapes with the ext draw margin.
        const size_t cap = 512u * 512u * 3u;
        uint8_t *buf = static_cast<uint8_t *>(heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (buf == nullptr) {
            request->send(507, "text/plain", "no psram for capture");
            return;
        }
        int w = 0, h = 0;
        const size_t bytes = wantOverlay ? a->benchCopyOverlay(buf, cap, &w, &h) : a->benchCopyFrameBuffer(buf, cap, &w, &h);
        if (bytes == 0) {
            free(buf);
            request->send(503, "text/plain", "capture unavailable");
            return;
        }
        // The response reads from this pointer lazily as it streams, so the
        // scratch has to outlive send() and is freed on disconnect instead.
        AsyncWebServerResponse *response = request->beginResponse(200, "application/octet-stream", buf, bytes);
        response->addHeader("X-Width", String(w));
        response->addHeader("X-Height", String(h));
        response->addHeader("X-Bpp", wantOverlay ? "3" : "2");
        request->onDisconnect([buf]() { free(buf); });
        request->send(response);
    });

    // Exhaustive check of the PIE scrim kernel against the scalar one, on the
    // device, over the whole 65,536 x 33 input space. ~135 ms, blocking.
    // Exhaustive check of nebula's vector lerp against its scalar form, over
    // all 256 x 256 x 256 inputs, on the device. ~1 s, blocking.
    server.on("/api/nebtest", HTTP_GET, [this](AsyncWebServerRequest *request) {
        uint32_t firstBad = 0;
        const uint32_t bad = nebula_lerp_self_test(&firstBad);
        char out[192];
        snprintf(out, sizeof(out),
                 "{\"triples\":%u,\"mismatches\":%u,\"first_a\":%u,\"first_b\":%u,"
                 "\"first_f\":%u,\"result\":\"%s\"}",
                 256u * 256u * 256u, bad, firstBad & 0xFFu, (firstBad >> 8) & 0xFFu, (firstBad >> 16) & 0xFFu,
                 bad == 0 ? "PASS" : "FAIL");
        request->send(200, "application/json", out);
    });

    server.on("/api/pietest", HTTP_GET, [this](AsyncWebServerRequest *request) {
        SleepAnimation *a = sleep_animation_bench_instance();
        if (a == nullptr) {
            request->send(503, "text/plain", "no animation instance");
            return;
        }
        uint32_t firstBad = 0;
        const uint32_t bad = a->benchPieSelfTest(&firstBad);
        char out[192];
        snprintf(out, sizeof(out), "{\"pairs\":%u,\"mismatches\":%u,\"first_colour\":%u,\"first_factor\":%u,\"result\":\"%s\"}",
                 33u * 65536u, bad, firstBad & 0xFFFFu, firstBad >> 16, bad == 0 ? "PASS" : "FAIL");
        request->send(200, "application/json", out);
    });

    server.on("/api/membench", [this](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        JsonDocument doc;
        // The PSRAM side must be much larger than the 32 KB data cache or the
        // benchmark measures the cache instead of the bus: a first version used
        // a 32 KB PSRAM buffer and reported 366 MB/s reads and 517 MB/s memsets,
        // which is cache bandwidth -- the working set never left L1. PSRAM
        // buffers are 512 KB and walked linearly so every access misses. The
        // SRAM side stays small (a real band buffer is 15 KB) and is reused.
        constexpr size_t SN = 16 * 1024;  // SRAM block, ~= one band buffer
        constexpr size_t PN = 512 * 1024; // PSRAM span, 16x the data cache
        constexpr int REPS = 8;           // full sweeps of PN
        constexpr int CHUNKS = PN / SN;
        // 64-byte aligned on both sides: esp_async_memcpy validates the pointers
        // against the configured trans_align and rejects the submit outright
        // otherwise (a plain heap_caps_malloc returned a pointer that failed
        // this and produced ESP_ERR_INVALID_ARG with no other diagnostic).
        uint8_t *sram = static_cast<uint8_t *>(heap_caps_aligned_alloc(64, SN, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        uint8_t *psram = static_cast<uint8_t *>(heap_caps_aligned_alloc(64, PN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        uint8_t *psram2 = static_cast<uint8_t *>(heap_caps_aligned_alloc(64, PN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (sram == nullptr || psram == nullptr || psram2 == nullptr) {
            doc["error"] = "alloc failed";
            doc["got_sram"] = sram != nullptr;
            doc["got_psram"] = psram != nullptr && psram2 != nullptr;
            doc["sram_free"] = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            doc["sram_largest"] = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        } else {
            memset(sram, 0x5A, SN);
            memset(psram, 0x5A, PN);
            memset(psram2, 0x5A, PN);
            auto mbps = [](uint32_t us) { return us ? static_cast<uint32_t>(static_cast<uint64_t>(PN) * REPS / us) : 0u; };

            // This is the push path: SRAM band buffer -> PSRAM framebuffer.
            uint32_t t0 = micros();
            for (int i = 0; i < REPS; i++) {
                for (int c = 0; c < CHUNKS; c++) {
                    memcpy(psram + c * SN, sram, SN);
                }
            }
            doc["w_sram_to_psram_mbps"] = mbps(micros() - t0);

            t0 = micros();
            for (int i = 0; i < REPS; i++) {
                for (int c = 0; c < CHUNKS; c++) {
                    memcpy(sram, psram + c * SN, SN);
                }
            }
            doc["r_psram_to_sram_mbps"] = mbps(micros() - t0);

            t0 = micros();
            for (int i = 0; i < REPS; i++) {
                memcpy(psram2, psram, PN);
            }
            doc["psram_to_psram_mbps"] = mbps(micros() - t0);

            // memset never reads the source, so if the write path really pays a
            // read-for-ownership on every 32-byte line this lands close to the
            // SRAM->PSRAM copy rather than well above it.
            t0 = micros();
            for (int i = 0; i < REPS; i++) {
                memset(psram, static_cast<uint8_t>(i), PN);
            }
            doc["memset_psram_mbps"] = mbps(micros() - t0);

            // GDMA path. If this clears the CPU memcpy by a wide margin, the
            // push task should hand its band to the DMA engine rather than copy
            // it, which also gives the byte movement back to hardware and frees
            // the core entirely.
            async_memcpy_config_t cfg = ASYNC_MEMCPY_DEFAULT_CONFIG();
            cfg.backlog = 8;
            cfg.dma_burst_size = 32;
            async_memcpy_t asmcp = nullptr;
            const esp_err_t inst = esp_async_memcpy_install(&cfg, &asmcp);
            doc["dma_install_err"] = esp_err_to_name(inst);
            if (inst == ESP_OK) {
                static volatile int s_done = 0;
                esp_err_t sub = ESP_OK;
                bool ok = true;
                // Serialised: every transfer is awaited, so this measures the
                // engine's throughput rather than queue depth.
                t0 = micros();
                for (int i = 0; i < REPS && ok; i++) {
                    for (int c = 0; c < CHUNKS && ok; c++) {
                        s_done = 0;
                        sub = esp_async_memcpy(
                            asmcp, psram + c * SN, sram, SN,
                            [](async_memcpy_t, async_memcpy_event_t *, void *) -> bool {
                                s_done = 1;
                                return false;
                            },
                            nullptr);
                        if (sub != ESP_OK) {
                            ok = false;
                            break;
                        }
                        uint32_t spin = 0;
                        while (s_done == 0 && spin < 4000000u) {
                            spin++;
                        }
                        if (s_done == 0) {
                            ok = false;
                        }
                    }
                }
                doc["dma_sram_to_psram_mbps"] = ok ? mbps(micros() - t0) : 0;
                doc["dma_ok"] = ok;
                doc["dma_submit_err"] = esp_err_to_name(sub);
                // Reverse direction too. If GDMA declines a PSRAM destination
                // but accepts a PSRAM source, the push cannot be handed to it
                // and the write-allocate cost has to be attacked another way.
                s_done = 0;
                const esp_err_t rev = esp_async_memcpy(
                    asmcp, sram, psram, SN,
                    [](async_memcpy_t, async_memcpy_event_t *, void *) -> bool {
                        s_done = 1;
                        return false;
                    },
                    nullptr);
                doc["dma_psram_src_err"] = esp_err_to_name(rev);
                if (rev == ESP_OK) {
                    uint32_t spin = 0;
                    while (s_done == 0 && spin < 4000000u) {
                        spin++;
                    }
                }
                esp_async_memcpy_uninstall(asmcp);
            } else {
                doc["dma_ok"] = false;
            }
        }
        heap_caps_free(sram);
        heap_caps_free(psram);
        heap_caps_free(psram2);
        doc["psram_span"] = PN;
        doc["reps"] = REPS;
        doc["sram_block"] = SN;
        serializeJson(doc, *response);
        request->send(response);
    });

    // Bench build only: the render task's own per-stage frame timings. Serial
    // is not a usable channel on this board (the IDF console goes to UART0,
    // not the USB CDC), so results come out over HTTP.
    server.on("/api/animbench", [this](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        JsonDocument doc;
        // ?div=n reprograms the RGB pixel clock (pclk = 80 MHz / n) and
        // restarts the sweep. Scan-out reads the PSRAM framebuffer
        // continuously, so the divider sets how much of the octal-PSRAM budget
        // is left for the render task's writes -- this is the knob that tests
        // whether the flat push cost is a bandwidth floor. Not persisted: it
        // reverts to the stored setting on the next boot.
        if (request->hasArg("fps")) {
            SleepAnimation *a = sleep_animation_bench_instance();
            const int f = request->arg("fps").toInt();
            if (a != nullptr && f >= 5 && f <= 60) {
                a->benchSetMaxFps(static_cast<uint8_t>(f));
                a->benchRequestReset();
            }
        }
        // ?dma=0/1 switches the framebuffer push between the CPU copy through
        // the push task and GDMA straight into the panel's buffer. Takes effect
        // at the next start(), because the engine is installed on the render
        // task; benchRequestReset restarts the sweep so the two are not
        // averaged together.
        if (request->hasArg("dma")) {
            SleepAnimation *a = sleep_animation_bench_instance();
            if (a != nullptr) {
                a->benchSetDma(request->arg("dma").toInt() != 0);
                a->benchRequestReset();
            }
        }
        // ?direct=0|1 -- GDMA straight into the framebuffer, or the ordinary
        // two-task CPU push. Takes effect on the next band, no restart needed.
        if (request->hasArg("direct")) {
            SleepAnimation *a = sleep_animation_bench_instance();
            if (a != nullptr) {
                a->benchSetDirectPush(request->arg("direct").toInt() != 0);
                a->benchRequestReset();
            }
        }
        // ?ilace=0|1 -- push every other row, alternating each frame. Halves the
        // push, which is the pipeline's ceiling; costs each row half the refresh
        // rate. Takes effect on the next band.
        if (request->hasArg("ilace")) {
            SleepAnimation *a = sleep_animation_bench_instance();
            if (a != nullptr) {
                a->benchSetInterlace(request->arg("ilace").toInt() != 0);
                a->benchRequestReset();
            }
        }
        // ?rhalf=0|1 -- with interlacing on at half resolution, render only the
        // source rows this frame will push, halving the animation's own cost.
        if (request->hasArg("rhalf")) {
            SleepAnimation *a = sleep_animation_bench_instance();
            if (a != nullptr) {
                a->benchSetRenderHalf(request->arg("rhalf").toInt() != 0);
                a->benchRequestReset();
            }
        }
        // ?flash=0|1 -- alternating solid frames, see benchSetFlash.
        if (request->hasArg("flash")) {
            SleepAnimation *a = sleep_animation_bench_instance();
            if (a != nullptr) {
                a->benchSetFlash(request->arg("flash").toInt());
                a->benchRequestReset();
            }
        }
        // ?pie=0|1 -- vector or scalar scrim, see benchSetPie.
        if (request->hasArg("pie")) {
            SleepAnimation *a = sleep_animation_bench_instance();
            if (a != nullptr) {
                a->benchSetPie(request->arg("pie").toInt() != 0);
                a->benchRequestReset();
            }
        }
        // ?bpie=0|1 -- vector or scalar composite (blendRowPie vs blendRow).
        if (request->hasArg("bpie")) {
            SleepAnimation *a = sleep_animation_bench_instance();
            if (a != nullptr) {
                a->benchSetBpie(request->arg("bpie").toInt() != 0);
                a->benchRequestReset();
            }
        }
        // ?pattern=0|1 -- deterministic framebuffer contents, see benchSetPattern.
        if (request->hasArg("pattern")) {
            SleepAnimation *a = sleep_animation_bench_instance();
            if (a != nullptr) {
                a->benchSetPattern(request->arg("pattern").toInt());
                a->benchRequestReset();
            }
        }
        // ?probe=0..3 -- blend-stage decomposition, see benchSetBlendProbe.
        if (request->hasArg("probe")) {
            SleepAnimation *a = sleep_animation_bench_instance();
            if (a != nullptr) {
                a->benchSetBlendProbe(request->arg("probe").toInt());
                a->benchRequestReset();
            }
        }
        if (request->hasArg("only")) {
            SleepAnimation *a = sleep_animation_bench_instance();
            if (a != nullptr) {
                a->benchSetOnly(request->arg("only").toInt());
                a->benchRequestReset();
            }
        }
        if (request->hasArg("half")) {
            SleepAnimation *a = sleep_animation_bench_instance();
            if (a != nullptr) {
                a->benchSetHalfRes(request->arg("half").toInt() != 0);
                a->benchRequestReset();
            }
        }
        if (request->hasArg("div")) {
            const int div = request->arg("div").toInt();
            if (div >= 2 && div <= 16) {
                panelclock::setDiv(div);
                SleepAnimation *a = sleep_animation_bench_instance();
                if (a != nullptr) {
                    a->benchRequestReset();
                }
            }
        }
        SleepAnimation *anim0 = sleep_animation_bench_instance();
        const BenchGateState &g = bench_gate_state();
        JsonObject gate = doc["gate"].to<JsonObject>();
        gate["ui_initialized"] = g.uiInitialized;
        gate["blocked"] = g.blocked;
        gate["want_animation"] = g.wantAnimation;
        gate["anim_active"] = g.animActive;
        gate["mode"] = g.mode;
        gate["screen"] = g.screen;
        gate["start_failed"] = g.startFailed;
        // Controller's own view: a populated hardware string is proof the
        // synthetic handshake reached onSystemInfo().
        gate["ctrl_hardware"] = controller->getSystemInfo().hardware;
        gate["ctrl_proto"] = controller->getSystemInfo().protocolVersion;
        gate["ctrl_mismatch"] = controller->getSystemInfo().protocolMismatch;
        gate["ctrl_initialized"] = controller->benchInitialized();
        gate["ctrl_screen_ready"] = controller->benchScreenReady();
        gate["uptime_ms"] = millis();
        // The pixel prescale only, NOT the whole clock path: pclk is the LCD
        // group clock (PLL160M divided by lcd_clkm_div_*) divided again by
        // this. Reporting a derived Hz here would be wrong, so report the
        // divider and let a caller compare relative values across settings.
        gate["pclk_div"] = panelclock::currentDiv();
        gate["pclk_boot_hz"] = panelclock::bootPclkHz();
        gate["half_res"] = anim0 != nullptr && anim0->benchHalfRes();
        gate["only"] = anim0 != nullptr ? anim0->benchGetOnly() : -1;
        gate["max_fps"] = anim0 != nullptr ? anim0->benchMaxFps() : 0;
        // Where the animations' lookup tables actually landed. alloc() sends
        // anything over SRAM_ALLOC_LIMIT to PSRAM on the assumption that big
        // tables are swept sequentially; a table indexed by a computed value
        // once per pixel is not, and pays a PSRAM round trip per miss.
        gate["lut_sram_b"] = static_cast<uint32_t>(bganim::g_allocSram);
        gate["lut_psram_b"] = static_cast<uint32_t>(bganim::g_allocPsram);
        // Direct-to-framebuffer push. wanted vs active is the difference
        // between asking and getting: the panel must hand over its framebuffer
        // and the engine must install. issued minus done is the liveness
        // check -- the render task cannot outrun the engine by more than
        // NUM_SLOTS, so a gap parked above that means transfers stopped
        // completing.
        gate["dma_wanted"] = anim0 != nullptr && anim0->benchDmaWanted();
        gate["dma_active"] = anim0 != nullptr && anim0->benchDmaActive();
        gate["direct_push"] = anim0 != nullptr && anim0->benchDirectPush();
        gate["pie_scrim"] = anim0 != nullptr && anim0->benchPie();
        gate["flash"] = anim0 != nullptr ? anim0->benchFlash() : 0;
        // 2 means the frame is composed off-screen and flipped at a frame
        // boundary, which is what makes the picture tear-free; 1 means the
        // writes race the scan-out.
        gate["fb_count"] = anim0 != nullptr ? anim0->benchFrameBufferCount() : 0;
        gate["interlace"] = anim0 != nullptr ? anim0->benchInterlace() : false;
        gate["render_half"] = anim0 != nullptr ? anim0->benchRenderHalf() : false;
        gate["bands_internal"] = anim0 != nullptr && anim0->benchBandsInternal();
        if (anim0 != nullptr) {
            JsonArray ba = gate["band_addr"].to<JsonArray>();
            for (int i = 0; i < 3; i++) {
                ba.add(anim0->benchBandAddr(i));
            }
        }
        gate["dma_issued"] = anim0 != nullptr ? anim0->benchDmaIssued() : 0;
        gate["dma_done"] = anim0 != nullptr ? anim0->benchDmaCompleted() : 0;
        gate["dma_err"] = anim0 != nullptr ? anim0->benchDmaErrors() : 0;
        gate["sram_limit"] = static_cast<uint32_t>(bganim::SRAM_ALLOC_LIMIT);
        gate["sram_budget"] = static_cast<uint32_t>(bganim::SRAM_TOTAL_BUDGET);
        gate["free_internal_b"] = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        // Largest contiguous block, not just the total. These diverge under
        // fragmentation, and the network stack needs whole blocks: lwIP drops
        // an incoming SYN silently when tcp_alloc() fails, so a fragmented pool
        // shows up as HTTP connect timeouts with ICMP still answering, which
        // reads as a wedged board rather than as memory pressure. Watching the
        // two figures together is what distinguishes exhaustion from
        // fragmentation.
        gate["largest_internal_b"] = static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        gate["min_free_internal_b"] = static_cast<uint32_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
        SleepAnimation *anim = sleep_animation_bench_instance();
        if (anim == nullptr) {
            doc["running"] = false;
            doc["error"] = "animation task not started";
        } else {
            doc["running"] = true;
            doc["passes"] = anim->benchPassCount();
            doc["current"] = bg_animation(anim->benchCurrentAnim()).id;
            JsonArray arr = doc["results"].to<JsonArray>();
            const SleepAnimation::BenchResult *res = anim->benchResults();
            for (int i = 0; i < bg_animation_count() && i < SleepAnimation::BENCH_MAX_ANIMS; i++) {
                if (!res[i].valid) {
                    continue;
                }
                JsonObject o = arr.add<JsonObject>();
                o["id"] = bg_animation(i).id;
                o["name"] = bg_animation(i).name;
                o["frames"] = res[i].frames;
                o["band_us"] = res[i].bandUs;
                o["blend_us"] = res[i].blendUs;
                o["push_us"] = res[i].pushUs;
                o["total_us"] = res[i].totalUs;
                o["max_us"] = res[i].maxTotalUs;
                o["wait_us"] = res[i].waitUs;
                o["pack_us"] = res[i].packUs;
                o["span_px"] = res[i].spanPx;
                o["scrim_px"] = res[i].scrimPx;
                o["fps"] = res[i].achievedFps / 100.0;
                // Per-row band cost with and without the scheduler suspended.
                // A gap between them is preemption being charged to the band
                // timer; parity means the band really is that expensive.
                o["band_ns_row"] = res[i].bandNsPerRow;
                o["band_locked_ns_row"] = res[i].bandLockedNsPerRow;
            }
        }
        serializeJson(doc, *response);
        request->send(response);
    });
#endif
    server.on("/api/scales/list", [this](AsyncWebServerRequest *request) { handleBLEScaleList(request); });
    server.on("/api/scales/connect", [this](AsyncWebServerRequest *request) { handleBLEScaleConnect(request); });
    server.on("/api/scales/scan", [this](AsyncWebServerRequest *request) { handleBLEScaleScan(request); });
    server.on("/api/scales/info", [this](AsyncWebServerRequest *request) { handleBLEScaleInfo(request); });
    server.on("/api/debug/heap/detail", [this](AsyncWebServerRequest *request) { handleDebugHeap(request); });
    FS *fs = &LittleFS;
    if (controller->isSDCard()) {
        fs = &SD_MMC;
    }
    server.serveStatic("/api/history/", *fs, "/h/").setCacheControl("no-store");
    server.on("/api/history/index.bin", HTTP_GET, [this, fs](AsyncWebServerRequest *request) {
        // Serve the binary index file directly
        if (fs->exists("/h/index.bin")) {
            request->send(*fs, "/h/index.bin", "application/octet-stream");
        } else {
            request->send(404, "text/plain", "Index not found");
        }
    });
    server.on("/api/history/recent.bin", HTTP_GET, [this](AsyncWebServerRequest *request) {
        // The most recent non-deleted shots, newest first, as a regular shot
        // index (SIDX header + entries) — same binary format as index.bin,
        // just truncated, so clients reuse the index.bin parser.
        constexpr long MAX_RECENT_LIMIT = 50;
        long limit = 8;
        if (request->hasArg("limit")) {
            limit = constrain(request->arg("limit").toInt(), 1L, MAX_RECENT_LIMIT);
        }

        auto *entries = static_cast<ShotIndexEntry *>(ps_malloc(limit * sizeof(ShotIndexEntry)));
        if (entries == nullptr) {
            request->send(500, "text/plain", "Out of memory");
            return;
        }
        size_t count = ShotHistory.readRecentEntries(entries, limit);

        ShotIndexHeader header{};
        header.magic = SHOT_INDEX_MAGIC;
        header.version = SHOT_INDEX_VERSION;
        header.entrySize = SHOT_INDEX_ENTRY_SIZE;
        header.entryCount = count;
        header.nextId = 0; // meaningless for a partial view

        AsyncResponseStream *response = request->beginResponseStream("application/octet-stream");
        response->addHeader("Cache-Control", "no-store");
        response->write(reinterpret_cast<const uint8_t *>(&header), sizeof(header));
        response->write(reinterpret_cast<const uint8_t *>(entries), count * sizeof(ShotIndexEntry));
        free(entries);
        request->send(response);
    });
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
                    JsonDocument resp(&psramAllocator);
                    ShotHistory.handleRequest(doc, resp);
                    client->text(toWsBuffer(resp));
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
        controller->getSettings().batchUpdate([request](Settings *settings) {
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
            if (request->hasArg("apPassword") && request->arg("apPassword").length() >= WIFI_AP_PASSWORD_MIN_LENGTH)
                settings->setWifiApPassword(request->arg("apPassword"));
            settings->setHomekit(request->hasArg("homekit"));
            settings->setBoilerFillActive(request->hasArg("boilerFillActive"));
            if (request->hasArg("startupFillTime"))
                settings->setStartupFillTime(request->arg("startupFillTime").toInt() * 1000);
            if (request->hasArg("steamFillTime"))
                settings->setSteamFillTime(request->arg("steamFillTime").toInt() * 1000);
            settings->setSmartGrindActive(request->hasArg("smartGrindActive"));
            settings->setScaleMenuButton(request->hasArg("scaleMenuButton"));
            if (request->hasArg("bgAnimId"))
                settings->setBgAnimId(request->arg("bgAnimId").toInt());
            if (request->hasArg("bgAnimParams"))
                settings->setBgAnimParams(request->arg("bgAnimParams"));
            if (request->hasArg("bgAnimTheme"))
                settings->setBgAnimTheme(request->arg("bgAnimTheme").toInt());
            if (request->hasArg("bgAnimFps"))
                settings->setBgAnimFps(request->arg("bgAnimFps").toInt());
            // Guarded on hasArg rather than read as a checkbox: a checkbox that
            // is off is simply not posted, so a partial submit would read as
            // "full resolution, no interlacing" and drop the panel to ~15 fps.
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
            // Checkbox: the form omits it entirely when unchecked (see
            // buildSubmitFormData's checkboxKeys), so presence IS the value.
            settings->setElementTintEnabled(request->hasArg("elementTintEnabled"));
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
            if (request->hasArg("bgAnimId") || request->hasArg("bgAnimParams"))
                settings->setBgAnimAllScreens(request->hasArg("bgAnimAllScreens"));
            if (request->hasArg("smartGrindIp"))
                settings->setSmartGrindIp(request->arg("smartGrindIp"));
            if (request->hasArg("smartGrindMode"))
                settings->setSmartGrindMode(request->arg("smartGrindMode").toInt());
            settings->setHomeAssistant(request->hasArg("homeAssistant"));
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
            settings->setMomentaryButtons(request->hasArg("momentaryButtons"));
            settings->setDelayAdjust(request->hasArg("delayAdjust"));
            if (request->hasArg("brewDelay"))
                settings->setBrewDelay(request->arg("brewDelay").toDouble());
            if (request->hasArg("grindDelay"))
                settings->setGrindDelay(request->arg("grindDelay").toDouble());
            if (request->hasArg("timezone"))
                settings->setTimezone(request->arg("timezone"));
            settings->setClockFormat(request->hasArg("clock24hFormat"));
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
            settings->setAutoWakeupEnabled(request->hasArg("autowakeupEnabled"));
            if (request->hasArg("autowakeupSchedules")) {
                // Handle schedule format with days
                String schedulesStr = request->arg("autowakeupSchedules");
                std::vector<AutoWakeupSchedule> schedules;

                if (schedulesStr.length() > 0) {
                    // Split semicolon-separated schedules
                    int start = 0;
                    int end = schedulesStr.indexOf(';');

                    while (end != -1 || start < schedulesStr.length()) {
                        String scheduleStr = (end != -1) ? schedulesStr.substring(start, end) : schedulesStr.substring(start);

                        int pipePos = scheduleStr.indexOf('|');
                        if (pipePos != -1) {
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

                if (schedules.empty()) {
                    schedules.push_back(AutoWakeupSchedule("07:00")); // Default fallback
                }
                settings->setAutoWakeupSchedules(schedules);
            }
            settings->save(true);
        });
        pluginManager->trigger("settings:changed");
        controller->setTargetTemp(controller->getTargetTemp());
        controller->setScaleFactors();
        controller->setPumpModelCoeffs();
    }

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    JsonDocument doc(&psramAllocator);
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
    doc["panelClockDiv"] = settings.getPanelClockDiv();
    doc["panelVcom"] = settings.getPanelVcom();
    // Read-only capability flag, not a setting: on ESP-IDF 4.4 there is no
    // esp_lcd_rgb_panel_set_pclk, so a new divider is only honoured when the
    // panel is next created. The form posts the whole document back and the
    // handler matches on explicit argument names, so echoing this is inert.
    doc["panelClockLive"] = panelclock::hasLiveControl();
    doc["bgAnimCustomTheme"] = settings.getBgAnimCustomTheme();
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
        snap = gaggimate::memmon::instance().sampleNow();
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
            snap = gaggimate::memmon::instance().sampleNow();
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
