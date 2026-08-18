#include "WebUIPlugin.h"
#include <DNSServer.h>
#include <LittleFS.h>
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
#include <esp32s3/rom/cache.h> // Cache_WriteBack_Addr / Cache_Invalidate_Addr for /api/dmatest
#include <esp_async_memcpy.h>
#include <soc/gdma_channel.h> // SOC_GDMA_TRIG_PERIPH_LCD0 for /api/gdma
#include <soc/gdma_struct.h>  // direct GDMA register access for /api/gdma
#include <esp_heap_caps.h>
#endif
// Not bench-only: /api/debug/heap reports the animation SRAM budget, and
// /api/settings echoes panelclock::hasLiveControl() so the form can tell the
// user whether a new divider applies now or at the next boot.
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
        statusDoc["bc"] = bleConnected;                                    // bluetooth scale connected status
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
        char buf[320];
        snprintf(buf, sizeof(buf),
                 "{\"int_free\":%u,\"int_largest\":%u,\"int_min\":%u,\"psram_free\":%u,\"psram_largest\":%u,"
                 "\"anim_sram\":%u,\"anim_psram\":%u,\"anim_budget\":%u}",
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)),
                 static_cast<unsigned>(animSram), static_cast<unsigned>(animPsram),
                 static_cast<unsigned>(bganim::SRAM_TOTAL_BUDGET));
        request->send(200, "application/json", buf);
    });
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
    // Does GDMA actually deliver what it was handed? The animation's direct
    // framebuffer push renders a sheared image while a byte-identical CPU copy
    // to the same address renders correctly, which leaves two possibilities:
    // the transfer is unfaithful, or it is faithful and the artefact is the
    // panel scanning the region while it is written. Asking the panel cannot
    // separate those. This can: same source alignment, same size, same engine
    // config as the animation uses, but into scratch PSRAM nobody is scanning,
    // then read back and compared.
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

    server.on("/api/dmatest", [this](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        JsonDocument doc;
        const size_t rows = request->hasArg("rows") ? request->arg("rows").toInt() : 12;
        const size_t w = 480;
        const size_t bytes = rows * w * 2;
        uint16_t *src = static_cast<uint16_t *>(heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
        uint16_t *dst = static_cast<uint16_t *>(heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        async_memcpy_t h = nullptr;
        async_memcpy_config_t cfg = ASYNC_MEMCPY_DEFAULT_CONFIG();
        cfg.backlog = 64;
        cfg.sram_trans_align = 4;
        cfg.psram_trans_align = 64;
        esp_err_t installErr = ESP_OK;
        if (src != nullptr && dst != nullptr) {
            installErr = esp_async_memcpy_install(&cfg, &h);
        }
        doc["src"] = reinterpret_cast<uint32_t>(src);
        doc["dst"] = reinterpret_cast<uint32_t>(dst);
        doc["bytes"] = static_cast<uint32_t>(bytes);
        doc["install_err"] = static_cast<int>(installErr);
        if (src == nullptr || dst == nullptr || h == nullptr) {
            doc["ok"] = false;
            doc["why"] = "alloc/install failed";
        } else {
            // A pattern where every 16-bit word encodes its own index, so a
            // mismatch reports not just THAT it moved but WHERE it came from --
            // a constant delta means a displaced transfer, noise means garbage.
            const size_t words = bytes / 2;
            for (size_t i = 0; i < words; i++) {
                src[i] = static_cast<uint16_t>(i * 7 + 1);
            }
            memset(dst, 0xA5, bytes);
            Cache_WriteBack_Addr(reinterpret_cast<uint32_t>(dst), static_cast<uint32_t>(bytes));
            SemaphoreHandle_t done = xSemaphoreCreateBinary();
            const int64_t t0 = esp_timer_get_time();
            const esp_err_t err = esp_async_memcpy(
                h, dst, src, bytes,
                [](async_memcpy_t, async_memcpy_event_t *, void *arg) -> bool {
                    BaseType_t woken = pdFALSE;
                    xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(arg), &woken);
                    return woken == pdTRUE;
                },
                done);
            const bool completed = err == ESP_OK && xSemaphoreTake(done, pdMS_TO_TICKS(500)) == pdTRUE;
            const uint32_t elapsedUs = static_cast<uint32_t>(esp_timer_get_time() - t0);
            // The CPU's cached view of dst predates the transfer, so it must be
            // dropped before the comparison or this would verify the cache.
            Cache_Invalidate_Addr(reinterpret_cast<uint32_t>(dst), static_cast<uint32_t>(bytes));
            uint32_t mismatches = 0;
            int32_t firstIdx = -1;
            uint32_t firstExp = 0, firstGot = 0;
            int32_t deltaWords = 0;
            for (size_t i = 0; i < words; i++) {
                const uint16_t exp = static_cast<uint16_t>(i * 7 + 1);
                if (dst[i] != exp) {
                    if (firstIdx < 0) {
                        firstIdx = static_cast<int32_t>(i);
                        firstExp = exp;
                        firstGot = dst[i];
                        // If the value present is itself a valid pattern word,
                        // report which index it belongs to: that difference is
                        // the displacement, in words.
                        if (firstGot >= 1 && ((firstGot - 1) % 7) == 0) {
                            deltaWords = static_cast<int32_t>((firstGot - 1) / 7) - firstIdx;
                        }
                    }
                    mismatches++;
                }
            }
            doc["submit_err"] = static_cast<int>(err);
            doc["completed"] = completed;
            doc["elapsed_us"] = elapsedUs;
            doc["mismatches"] = mismatches;
            doc["words"] = static_cast<uint32_t>(words);
            doc["first_bad_idx"] = firstIdx;
            doc["first_expected"] = firstExp;
            doc["first_got"] = firstGot;
            doc["displacement_words"] = deltaWords;
            doc["ok"] = completed && mismatches == 0;
            vSemaphoreDelete(done);
        }
        if (h != nullptr) {
            esp_async_memcpy_uninstall(h);
        }
        heap_caps_free(src);
        heap_caps_free(dst);
        serializeJson(doc, *response);
        request->send(response);
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
        constexpr size_t SN = 16 * 1024;   // SRAM block, ~= one band buffer
        constexpr size_t PN = 512 * 1024;  // PSRAM span, 16x the data cache
        constexpr int REPS = 8;            // full sweeps of PN
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
            auto mbps = [](uint32_t us) {
                return us ? static_cast<uint32_t>(static_cast<uint64_t>(PN) * REPS / us) : 0u;
            };

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
            cfg.psram_trans_align = 64;
            cfg.sram_trans_align = 4;
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
        // ?dmamode=0|1|2|3|4 -- see benchSetDmaMode. Bisect control, takes effect
        // on the next band, no restart needed.
        if (request->hasArg("dmamode")) {
            SleepAnimation *a = sleep_animation_bench_instance();
            if (a != nullptr) {
                a->benchSetDmaMode(request->arg("dmamode").toInt());
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
        gate["dma_mode"] = anim0 != nullptr ? anim0->benchDmaMode() : 0;
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
        gate["largest_internal_b"] =
            static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        gate["min_free_internal_b"] =
            static_cast<uint32_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
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
                o["blend_px"] = res[i].blendPx;
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
                // 4-12 window (6.7-20 MHz pclk) could leave the panel
                // unreadable, so reject them to default rather than persist.
                int div = request->arg("panelClockDiv").toInt();
                if (div != 0 && (div < 4 || div > 12))
                    div = 0;
                settings->setPanelClockDiv(div);
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
    doc["bgAnimBrightness"] = settings.getBgAnimBrightness();
    doc["bgAnimHighlightKnee"] = settings.getBgAnimHighlightKnee();
    doc["bgAnimScrim"] = settings.getBgAnimScrim();
    doc["panelClockDiv"] = settings.getPanelClockDiv();
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
    doc["latestVersion"] = ota->getCurrentVersion();
    doc["tp"] = "res:ota-settings";
    doc["displayUpdateAvailable"] = ota->isUpdateAvailable(false);
    doc["controllerUpdateAvailable"] = ota->isUpdateAvailable(true);
    doc["displayVersion"] = BUILD_GIT_VERSION;
    doc["controllerVersion"] = controller->getSystemInfo().version;
    doc["hardware"] = controller->getSystemInfo().hardware;
    doc["latestVersion"] = ota->getCurrentVersion();
    doc["channel"] = settings.getOTAChannel();
    doc["updating"] = updating;
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
        doc["heapFree"] = static_cast<uint32_t>(ri ? ri->freeBytes
                                                   : heap_caps_get_free_size(MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL));
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
