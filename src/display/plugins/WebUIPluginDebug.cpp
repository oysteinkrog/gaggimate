// The debug, probe and bench routes of WebUIPlugin (gm-bzu.19).
//
// Everything registered here is read-only diagnostics or an experiment that a
// build flag gates (GM_TOUCH_PROBE, GM_KBLOB, GM_BLEND_PROBE, GM_ANIM_BENCH,
// GM_SYNTH_HANDSHAKE, GAGGIMATE_SIM). It was the middle 2,400 lines of
// WebUIPlugin.cpp's setupServer(), between the captive-portal routes and the
// scale, history and OTA routes, so a new experiment landed next to the code
// that serves the machine. The routes, their guards and their comments are
// unchanged; only the file moved. setupServer() calls setupDebugEndpoints()
// where the block used to be.
#include "WebUIPlugin.h"

#ifndef GAGGIMATE_HEADLESS
#include <display/ui/default/DrawProfile.h> // /api/debug/drawprof; the header pulls in lvgl.h
#endif
#ifndef GAGGIMATE_SIM
#include <esp_flash.h> // /api/debug/flashmode
#include <esp_image_format.h>
#include <esp_ota_ops.h>
#include <soc/spi_mem_reg.h>
#endif
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
#include <display/core/Controller.h>
#include <display/core/MemoryMonitor.h>
#include <display/core/ProfileManager.h>
#include <display/core/process/BrewProcess.h>
#include <display/core/process/GrindProcess.h>
#include <display/models/profile.h>
#include <display/plugins/BLEScalePlugin.h>
#include <display/plugins/ShotHistoryPlugin.h>
#include <esp_memory_utils.h> // esp_ptr_external_ram, for the band-buffer placement report
#include <esp_timer.h>        // esp_timer_dump, for /api/debug/timers
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

// Counters exported by the patched esp_lcd RGB driver (scripts/patch_esp_lcd_rgb.py),
// which only exists in the real ESP-IDF component tree; the sim has no LCD_CAM
// peripheral to patch and no /api/debug/scanout consumer of its own.
#ifndef GAGGIMATE_SIM
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
#endif // GAGGIMATE_SIM

void WebUIPlugin::setupDebugEndpoints() {
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
#ifndef GAGGIMATE_SIM // esp_timer_dump walks the IDF esp_timer subsystem's own list
    server.on("/api/debug/timers", [](AsyncWebServerRequest *request) {
        esp_timer_dump(stdout);
        fflush(stdout);
        request->send(200, "application/json", "{\"dumped\":true}");
    });
#endif

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
        TaskStatus_t *st =
            static_cast<TaskStatus_t *>(heap_caps_malloc(sizeof(TaskStatus_t) * cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
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

    server.on("/api/debug/heap", [this](AsyncWebServerRequest *request) {
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
        const size_t hotUsed = 0, hotShared = 0, hotPeak = 0, hotSlab = 0;
        const uint32_t hotFail = 0;
#else
        const size_t animSram = bganim::g_allocSram;
        const size_t animPsram = bganim::g_allocPsram;
        const size_t hotUsed = bganim::hotUsed(), hotShared = bganim::hotShared(), hotPeak = bganim::hotPeak(),
                     hotSlab = bganim::HOT_SLAB_BYTES;
        const uint32_t hotFail = bganim::hotFailCount();
#endif
        // Scan-out health, from the panel's own interrupts. `slips` is the one
        // to watch: it counts frames whose bounce-buffer refill lost its race
        // against everything else on the shared MSPI bus, which is exactly what
        // shows on the panel as a displaced band. A run of minutes at a
        // constant value is the only real evidence the display is clean, since
        // the fault is far too rare to catch by looking at it.
        uint32_t scFrames = 0, scRefills = 0, scSlips = 0;
        panelclock::scanoutStats(&scFrames, &scRefills, &scSlips);
        char buf[640];
        snprintf(buf, sizeof(buf),
                 "{\"int_free\":%u,\"int_largest\":%u,\"int_min\":%u,\"psram_free\":%u,\"psram_largest\":%u,\"psram_min\":%u,"
                 "\"anim_sram\":%u,\"anim_psram\":%u,\"hot_used\":%u,\"hot_shared\":%u,\"hot_peak\":%u,\"hot_fail\":%u,\"hot_"
                 "slab\":%u,"
                 "\"sc_frames\":%u,\"sc_refills\":%u,\"sc_slips\":%u,"
                 "\"dma_free\":%u,\"dma_min\":%u,\"asset_streams\":%u,\"asset_queue\":%u,"
                 "\"hist_served\":%u,\"hist_dropped\":%u,\"hist_queue_max\":%u,\"hist_open_us_max\":%u,"
                 "\"uptime_ms\":%lu}",
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
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)),
                 // psram_min is the PSRAM low-water mark since boot: the number a
                 // proposal that spends PSRAM (gm-2cl.20, code fetched from PSRAM)
                 // is measured against.
                 static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM)), static_cast<unsigned>(animSram),
                 static_cast<unsigned>(animPsram), static_cast<unsigned>(hotUsed), static_cast<unsigned>(hotShared),
                 static_cast<unsigned>(hotPeak), static_cast<unsigned>(hotFail), static_cast<unsigned>(hotSlab),
                 static_cast<unsigned>(scFrames), static_cast<unsigned>(scRefills), static_cast<unsigned>(scSlips),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DMA)),
                 static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_DMA)), static_cast<unsigned>(assetStreams),
                 static_cast<unsigned>(assetQueue.size()), static_cast<unsigned>(histServed), static_cast<unsigned>(histDropped),
                 static_cast<unsigned>(histQueueMax), static_cast<unsigned>(histOpenUsMax), static_cast<unsigned long>(millis()));
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
#ifndef GAGGIMATE_SIM // the patched esp_lcd RGB driver counters this reports have no sim equivalent
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
            panelclock::setLagThresholdUs(static_cast<uint32_t>(request->arg("lagthresh").toInt()));
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
                         static_cast<unsigned>(marginLast), static_cast<unsigned>(marginMin), static_cast<unsigned>(marginMax),
                         static_cast<unsigned>(panelclock::SCANOUT_MARGIN_BUCKET_US));
        for (size_t i = 0; i < panelclock::SCANOUT_MARGIN_BUCKETS; i++) {
            response->printf("%s%u", i ? "," : "", static_cast<unsigned>(marginBucket[i]));
        }
        // resyncs is the event that used to displace the picture: a frame that
        // counted fewer bounce buffers than a frame holds. The patched driver squares
        // it up against the beam instead of restarting the DMA, so it now costs one
        // band of stale pixels rather than a whole shifted frame. dma_restarts should
        // stay at zero: only an explicit panel restart reaches it.
        response->printf("],\"resyncs\":%u,\"resync_bufs\":%u,\"resync_max\":%u,\"over_count\":%u,\"over_bufs\":%u,"
                         "\"flash_skips\":%u,"
                         "\"dma_restarts\":%u,\"dma_catchups\":%u,\"dma_catchup_bufs\":%u,"
                         "\"dma_catchup_max\":%u,\"eof_expect\":%u,\"eof_min\":%u,\"eof_max\":%u,\"log\":[",
                         static_cast<unsigned>(gm_rgb_resync_count), static_cast<unsigned>(gm_rgb_resync_bufs),
                         static_cast<unsigned>(gm_rgb_resync_max), static_cast<unsigned>(gm_rgb_over_count),
                         static_cast<unsigned>(gm_rgb_over_bufs),
                         static_cast<unsigned>(gm_rgb_flash_skip_bufs), static_cast<unsigned>(gm_rgb_restart_count),
                         static_cast<unsigned>(gm_rgb_catchup_count), static_cast<unsigned>(gm_rgb_catchup_bufs),
                         static_cast<unsigned>(gm_rgb_catchup_max), static_cast<unsigned>(gm_rgb_eof_expect),
                         static_cast<unsigned>(gm_rgb_eof_min), static_cast<unsigned>(gm_rgb_eof_max));
        for (size_t i = 0; i < n; i++) {
            response->printf("%s{\"frame\":%u,\"t_us\":%u,\"margin_us\":%u,\"overlay_us\":%u,\"flash_us\":%u,\"band_us\":%u,"
                             "\"present_us\":%u}",
                             i ? "," : "", static_cast<unsigned>(slipLog[i].frame), static_cast<unsigned>(slipLog[i].tUs),
                             static_cast<unsigned>(slipLog[i].marginUs),
                             static_cast<unsigned>(slipLog[i].sinceUs[panelclock::SCANOUT_ACT_OVERLAY]),
                             static_cast<unsigned>(slipLog[i].sinceUs[panelclock::SCANOUT_ACT_FLASH]),
                             static_cast<unsigned>(slipLog[i].sinceUs[panelclock::SCANOUT_ACT_BANDPUSH]),
                             static_cast<unsigned>(slipLog[i].sinceUs[panelclock::SCANOUT_ACT_PRESENT]));
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
        response->printf("],\"chunk_bucket_us\":16,\"chunk_max_us\":%u,\"chunk_hist\":[",
                         static_cast<unsigned>(gm_rgb_chunk_max_us));
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
            response->printf(
                "%s{\"t_ms\":%u,\"gap_us\":%u,\"prev_busy_us\":%u,\"busy_us\":%u,\"fills\":%u,\"nest\":%u,\"pos\":%u,"
                "\"stall_us\":%u,\"stall_at\":%u,\"pc\":\"0x%08x\",\"ps\":\"0x%08x\",\"task\":\"%s\",\"other\":\"%s\"}",
                i ? "," : "", static_cast<unsigned>(ev.t_ms), static_cast<unsigned>(ev.gap_us),
                static_cast<unsigned>(ev.prev_busy_us), static_cast<unsigned>(ev.busy_us), static_cast<unsigned>(ev.fills),
                static_cast<unsigned>(ev.nest), static_cast<unsigned>(ev.pos), static_cast<unsigned>(ev.stall_us),
                static_cast<unsigned>(ev.stall_at), static_cast<unsigned>(ev.pc), static_cast<unsigned>(ev.ps), task, other);
        }
        response->print("]}");
        request->send(response);
    });
#endif // GAGGIMATE_SIM
    // What the internal heap is made of. The free/min counters say how much
    // is left, not where the rest went, and the WiFi TX path dies at ~8 KB
    // of DMA-capable free, so reclaiming memory needs the composition: per
    // region (the 8 KB RTC-fast region is internal but not DMA-capable,
    // which is why int_largest can sit at 7.6 KB while a 1.6 KB TX buffer
    // fails), a histogram of used block sizes (1600 B x16 is the static WiFi
    // RX pool, 4-8 KB blocks are task stacks), and, in a build with
    // CONFIG_HEAP_TASK_TRACKING, per-task totals. Walks every block under the
    // heap lock: same cost class as heapwalk, debug use only.
#ifndef GAGGIMATE_SIM // uxTaskGetSnapshotAll walks the real FreeRTOS scheduler's TCB list
    server.on("/api/debug/heapmap", [](AsyncWebServerRequest *request) {
        struct Region {
            intptr_t start, end;
            size_t used, free_, usedBlocks, freeBlocks, largestFree;
        };
        struct Bucket {
            size_t size, count;
        };
        struct Big {
            uintptr_t ptr;
            size_t size;
        };
        struct Walk {
            Region regions[8];
            int nRegions;
            Bucket buckets[192];
            int nBuckets;
            size_t overflowBytes, overflowBlocks;
            // Every used block of 1 KB or more, so task stacks can be
            // matched to their allocation afterwards.
            Big big[160];
            int nBig;
        };
        auto *w = static_cast<Walk *>(heap_caps_calloc(1, sizeof(Walk), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (w == nullptr) {
            request->send(500, "application/json", "{\"error\":\"alloc\"}");
            return;
        }
        // Runs with the heap locked: no allocation in here.
        heap_caps_walk(
            MALLOC_CAP_INTERNAL,
            [](walker_heap_into_t h, walker_block_info_t b, void *ud) -> bool {
                auto *w = static_cast<Walk *>(ud);
                Region *r = nullptr;
                for (int i = 0; i < w->nRegions; i++) {
                    if (w->regions[i].start == h.start) {
                        r = &w->regions[i];
                    }
                }
                if (r == nullptr && w->nRegions < 8) {
                    r = &w->regions[w->nRegions++];
                    r->start = h.start;
                    r->end = h.end;
                }
                if (r != nullptr) {
                    if (b.used) {
                        r->used += b.size;
                        r->usedBlocks++;
                    } else {
                        r->free_ += b.size;
                        r->freeBlocks++;
                        if (b.size > r->largestFree) {
                            r->largestFree = b.size;
                        }
                    }
                }
                if (b.used) {
                    if (b.size >= 1024 && w->nBig < 160) {
                        w->big[w->nBig++] = Big{reinterpret_cast<uintptr_t>(b.ptr), b.size};
                    }
                    for (int i = 0; i < w->nBuckets; i++) {
                        if (w->buckets[i].size == b.size) {
                            w->buckets[i].count++;
                            return true;
                        }
                    }
                    if (w->nBuckets < 192) {
                        w->buckets[w->nBuckets++] = Bucket{b.size, 1};
                    } else {
                        w->overflowBytes += b.size;
                        w->overflowBlocks++;
                    }
                }
                return true;
            },
            w);
        std::sort(w->buckets, w->buckets + w->nBuckets,
                  [](const Bucket &a, const Bucket &b) { return a.size * a.count > b.size * b.count; });
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        response->print("{\"regions\":[");
        for (int i = 0; i < w->nRegions; i++) {
            const Region &r = w->regions[i];
            response->printf("%s{\"start\":\"0x%08x\",\"end\":\"0x%08x\",\"size\":%u,\"used\":%u,\"free\":%u,\"largest_free\":%u,"
                             "\"used_blocks\":%u,\"free_blocks\":%u}",
                             i ? "," : "", static_cast<unsigned>(r.start), static_cast<unsigned>(r.end),
                             static_cast<unsigned>(r.end - r.start), static_cast<unsigned>(r.used),
                             static_cast<unsigned>(r.free_), static_cast<unsigned>(r.largestFree),
                             static_cast<unsigned>(r.usedBlocks), static_cast<unsigned>(r.freeBlocks));
        }
        response->printf("],\"overflow_bytes\":%u,\"overflow_blocks\":%u,\"sizes\":[", static_cast<unsigned>(w->overflowBytes),
                         static_cast<unsigned>(w->overflowBlocks));
        const int shown = std::min(w->nBuckets, 80);
        for (int i = 0; i < shown; i++) {
            response->printf("%s[%u,%u]", i ? "," : "", static_cast<unsigned>(w->buckets[i].size),
                             static_cast<unsigned>(w->buckets[i].count));
        }
        response->print("]");
        // Task stacks, matched to the heap block that holds them: the
        // snapshot walk gives each task's stack end (its highest address,
        // fixed for the task's life, unlike the saved stack pointer), and the
        // block containing it is the stack allocation, so that block's size is
        // the stack size. A stack outside the internal heap (loopLogic and the
        // animation tasks, in PSRAM) reports -1. uxTaskGetSystemState would
        // need the trace facility, which production builds leave off; the
        // panic handler's snapshot walk is always there but takes no locks, so
        // the scheduler is held off around it and everything that touches a
        // TCB (name, high-water mark) is read inside that window, into a
        // copy, so a task that exits afterwards cannot leave a dangling
        // handle in the loop that prints. vTaskSuspendAll holds off this core
        // only; the other core can still create or delete a task meanwhile.
        // Every task here is created at boot and lives as long as the
        // firmware, which is what makes that acceptable for a diagnostic
        // somebody fetches by hand. Do not poll it. (CONFIG_HEAP_TASK_TRACKING
        // was tried for per-task totals and deadlocks IDF 5.5 inside
        // vQueueDelete; do not retry.)
        struct TaskRow {
            char name[configMAX_TASK_NAME_LEN];
            uintptr_t stackEnd;
            uint32_t hwm;
        };
        const UBaseType_t cap = uxTaskGetNumberOfTasks() + 4;
        auto *st =
            static_cast<TaskSnapshot_t *>(heap_caps_malloc(sizeof(TaskSnapshot_t) * cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        auto *rows = static_cast<TaskRow *>(heap_caps_malloc(sizeof(TaskRow) * cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (st != nullptr && rows != nullptr) {
            vTaskSuspendAll();
            const UBaseType_t live = uxTaskGetSnapshotAll(st, cap, nullptr);
            for (UBaseType_t j = 0; j < live; j++) {
                auto handle = static_cast<TaskHandle_t>(st[j].pxTCB);
                strlcpy(rows[j].name, pcTaskGetName(handle), sizeof(rows[j].name));
                rows[j].stackEnd = reinterpret_cast<uintptr_t>(st[j].pxEndOfStack);
                rows[j].hwm = uxTaskGetStackHighWaterMark(handle);
            }
            xTaskResumeAll();
            response->print(",\"tasks\":[");
            for (UBaseType_t j = 0; j < live; j++) {
                // pxEndOfStack points at the last word of the stack, inside
                // the allocation.
                const uintptr_t top = rows[j].stackEnd;
                int stack = -1;
                for (int i = 0; i < w->nBig; i++) {
                    if (top >= w->big[i].ptr && top < w->big[i].ptr + w->big[i].size) {
                        stack = static_cast<int>(w->big[i].size);
                    }
                }
                response->printf("%s{\"n\":\"%s\",\"stack\":%d,\"hwm\":%u,\"psram\":%s}", j ? "," : "", rows[j].name, stack,
                                 static_cast<unsigned>(rows[j].hwm),
                                 esp_ptr_external_ram(reinterpret_cast<void *>(top)) ? "true" : "false");
            }
            response->print("]");
        }
        free(rows);
        free(st);
        response->print("}");
        free(w);
        request->send(response);
    });
#endif // GAGGIMATE_SIM
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
#if !defined(GAGGIMATE_HEADLESS) && !defined(GAGGIMATE_SIM) // real LilyGo panel, not the SDL stand-in
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
    // Kernel equivalence test (SleepAnimation::requestAnimTest). ?anim=N
    // [&frames=K] queues a run on the render task and returns at once; a
    // plain GET returns the last completed result, with `pending` true while a
    // run is still queued. bandRef is BgAnim.h's portable band(); an animation
    // without one reports has_ref=false and nothing else.
    server.on("/api/debug/animtest", [](AsyncWebServerRequest *request) {
        SleepAnimation *a = sleep_animation_bench_instance();
        if (a == nullptr) {
            request->send(409, "application/json", "{\"error\":\"animation not running\"}");
            return;
        }
        if (request->hasArg("anim")) {
            const int id = request->arg("anim").toInt();
            if (id < 0 || id >= bg_animation_count()) {
                request->send(400, "application/json", "{\"error\":\"bad anim\"}");
                return;
            }
            a->requestAnimTest(id, request->hasArg("frames") ? request->arg("frames").toInt() : 8);
        }
        const SleepAnimation::AnimTestResult r = a->animTestResult();
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "{\"pending\":%s,\"seq\":%u,\"anim\":%d,\"id\":\"%s\",\"frames\":%d,\"has_ref\":%s,\"init_failed\":%s,"
                 "\"bands\":%u,\"mismatch_px\":%u,\"first\":{\"frame\":%d,\"pset\":%d,\"x\":%d,\"y\":%d,\"got\":%u,\"want\":%u},"
                 "\"band_us\":%u,\"ref_us\":%u}",
                 a->animTestPending() ? "true" : "false", static_cast<unsigned>(r.seq), r.anim,
                 r.anim >= 0 ? bg_animation(r.anim).id : "", r.frames, r.hasRef ? "true" : "false",
                 r.initFailed ? "true" : "false", static_cast<unsigned>(r.bands), static_cast<unsigned>(r.mismatchPx),
                 r.firstFrame, r.firstPset, r.firstX, r.firstY, static_cast<unsigned>(r.firstGot),
                 static_cast<unsigned>(r.firstWant), static_cast<unsigned>(r.bandUs), static_cast<unsigned>(r.refUs));
        request->send(200, "application/json", buf);
    });
#ifdef GM_KBLOB
    // Hot-loaded kernel blob (bganim/KBlob.h, tools/kblob/kb.py).
    //
    //   GET  /api/debug/kblob   the two buffer addresses and capacities the
    //                           host links against, the running firmware's
    //                           ELF sha (the blob must be linked against the
    //                           same ELF), and what is installed.
    //   POST /api/debug/kblob   the container image as the raw body. The
    //                           render task is asked to let go of the current
    //                           blob first (SleepAnimation::kblobBeginInstall);
    //                           a blob that stays resident, because the
    //                           animation is stopped, refuses with 409.
    //
    // The body is staged in one PSRAM block hung on request->_tempObject and
    // installed from the request handler, which the server calls once the
    // last byte is parsed. A malloc'd block rather than an object because the
    // request destructor free()s _tempObject itself, which is what reclaims
    // the staging of an upload the client abandoned halfway.
    struct KBlobStage {
        uint32_t cap;
        uint32_t len;
        uint8_t data[];
    };
    server.on(
        "/api/debug/kblob", HTTP_GET | HTTP_POST,
        [](AsyncWebServerRequest *request) {
            bool ok = true;
            String err;
            int status = 200;
            if (request->method() == HTTP_POST) {
                auto *stage = static_cast<KBlobStage *>(request->_tempObject);
                request->_tempObject = nullptr;
                if (stage == nullptr || stage->len != stage->cap) {
                    ok = false;
                    err = stage == nullptr ? "no body (or over the 64 KB cap)" : "body shorter than announced";
                    status = 400;
                } else {
                    SleepAnimation *a = sleep_animation_bench_instance();
                    if (a != nullptr && !a->kblobBeginInstall(2000)) {
                        ok = false;
                        err = "blob still resident (animation stopped with it active?)";
                        status = 409;
                    } else {
                        if (!kblob::install(stage->data, stage->len)) {
                            ok = false;
                            err = kblob::info().err;
                            status = 422;
                        }
                        if (a != nullptr) {
                            a->kblobEndInstall();
                        }
                    }
                }
                heap_caps_free(stage);
            }
            const kblob::Info i = kblob::info();
            JsonDocument doc(&psramAllocator);
            doc["ok"] = ok;
            if (!ok) {
                doc["error"] = err;
            }
            doc["text_base"] = i.textBase;
            doc["text_cap"] = i.textCap;
            doc["data_base"] = i.dataBase;
            doc["data_cap"] = i.dataCap;
            doc["fw_sha"] = i.fwSha;
            doc["loaded"] = i.loaded;
            doc["gen"] = i.gen;
            doc["name"] = i.name;
            doc["text_size"] = i.textSize;
            doc["data_size"] = i.dataSize;
            doc["bss_size"] = i.bssSize;
            doc["last_err"] = i.err;
            SleepAnimation *a = sleep_animation_bench_instance();
            doc["resident"] = a != nullptr && a->kblobResident();
            doc["useblob"] = a != nullptr && a->useBlobOn();
            AsyncResponseStream *response = request->beginResponseStream("application/json");
            response->setCode(status);
            serializeJson(doc, *response);
            request->send(response);
        },
        nullptr,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            constexpr size_t kMaxImage = 64 * 1024;
            if (index == 0) {
                heap_caps_free(request->_tempObject);
                request->_tempObject = nullptr;
                if (total == 0 || total > kMaxImage) {
                    return;
                }
                auto *stage =
                    static_cast<KBlobStage *>(heap_caps_malloc(sizeof(KBlobStage) + total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
                if (stage == nullptr) {
                    return;
                }
                stage->cap = static_cast<uint32_t>(total);
                stage->len = 0;
                request->_tempObject = stage;
            }
            auto *stage = static_cast<KBlobStage *>(request->_tempObject);
            if (stage == nullptr || len > stage->cap - stage->len) {
                return;
            }
            memcpy(stage->data + stage->len, data, len);
            stage->len += static_cast<uint32_t>(len);
        });

    // Cycle-count microbench (SleepAnimation::requestKBench). ?anim=N
    // [&n=8][&frames=2][&which=15] queues a run and returns at once; a plain
    // GET returns the last result, `pending` true while one is queued. which
    // is a bit mask: 1 firmware band(), 2 firmware bandRef(), 4 blob band(),
    // 8 blob bandRef(). Cycles are at the CPU clock (240 MHz); min_cyc is the
    // sum over bands of the best of n runs, first_cyc the sum of the first
    // runs, mean_cyc the mean over all runs. mismatch_bands compares each
    // variant's output hash with the firmware band()'s when both ran.
    server.on("/api/debug/kbench", [](AsyncWebServerRequest *request) {
        SleepAnimation *a = sleep_animation_bench_instance();
        if (a == nullptr) {
            request->send(409, "application/json", "{\"error\":\"animation not running\"}");
            return;
        }
        if (request->hasArg("anim")) {
            const int id = request->arg("anim").toInt();
            if (id < 0 || id >= bg_animation_count()) {
                request->send(400, "application/json", "{\"error\":\"bad anim\"}");
                return;
            }
            a->requestKBench(id, request->hasArg("n") ? request->arg("n").toInt() : 8,
                             request->hasArg("frames") ? request->arg("frames").toInt() : 2,
                             request->hasArg("which") ? static_cast<uint32_t>(request->arg("which").toInt()) : 15u);
        }
        const SleepAnimation::KBenchResult r = a->kbenchResult();
        JsonDocument doc(&psramAllocator);
        doc["pending"] = a->kbenchPending();
        doc["seq"] = r.seq;
        doc["anim"] = r.anim;
        doc["id"] = r.anim >= 0 ? bg_animation(r.anim).id : "";
        doc["frames"] = r.frames;
        doc["n"] = r.n;
        doc["which"] = r.which;
        doc["cpu_mhz"] = getCpuFrequencyMhz();
        doc["hot_leak_before"] = r.hotLeakBefore;
        doc["hot_leak_fw"] = r.hotLeakFw;
        doc["hot_leak_blob"] = r.hotLeakBlob;
        static const char *const names[4] = {"band", "ref", "blob", "blobref"};
        for (int vi = 0; vi < 4; vi++) {
            const SleepAnimation::KBenchVariant &v = r.v[vi];
            JsonObject o = doc[names[vi]].to<JsonObject>();
            o["ran"] = v.ran;
            o["init_failed"] = v.initFailed;
            o["bands"] = v.bands;
            o["min_cyc"] = v.minCyc;
            o["first_cyc"] = v.firstCyc;
            o["mean_cyc"] = r.n > 0 ? v.sumCyc / static_cast<uint64_t>(r.n) : 0;
            o["mismatch_bands"] = v.mismatchBands;
            o["first_mismatch_frame"] = v.firstMismatchFrame;
            o["first_mismatch_y"] = v.firstMismatchY;
            o["mismatch_vs_blob"] = v.mismatchVsBlob;
        }
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        serializeJson(doc, *response);
        request->send(response);
    });
#endif // GM_KBLOB
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
        // ovmin=N: the overlay refresh spacing gate in microseconds (see
        // g_overlayMinRefreshUs); 0 lets the foreground refresh as fast as
        // the snapshot path can run, which is how that path's rate is
        // measured. Not persisted; the boot value is OVERLAY_MIN_REFRESH_US.
        if (request->hasArg("ovmin")) {
            const long v = request->arg("ovmin").toInt();
            if (v >= 0 && v <= 5000000) {
                g_overlayMinRefreshUs = v;
            }
        }
        // ovg=N: overlay gain, Q8, 0..256, applied from the next frame; the
        // measurement knob for the composite's fade cost (blend_us at 128
        // against 256). ovramp=ms starts a test ramp to the far end from the
        // current target (0 if it is above 128, else 256).
        if (request->hasArg("ovg")) {
            const long v = request->arg("ovg").toInt();
            if (v >= 0 && v <= 256) {
                a->setOverlayGain(static_cast<uint16_t>(v));
            }
        }
        if (request->hasArg("ovramp")) {
            const long v = request->arg("ovramp").toInt();
            if (v >= 0 && v <= 10000) {
                a->rampOverlayGain(a->overlayGainTarget() > 128 ? 0 : 256, static_cast<uint32_t>(v));
            }
        }
#ifdef GM_BLEND_PROBE
        // probe=0..7 -- blend-stage decomposition, see benchSetBlendProbe.
        if (request->hasArg("probe")) {
            a->setBlendProbe(request->arg("probe").toInt());
        }
        if (request->hasArg("probereps")) {
            a->setProbeReps(request->arg("probereps").toInt());
        }
        // bpie=0|1 -- scalar or vector composite kernel (blendRow vs blendRowPie).
        if (request->hasArg("bpie")) {
            a->setBpie(request->arg("bpie").toInt() != 0);
        }
#endif
        // scrim=0..100 overrides the text scrim strength until scrim=-1 or
        // a reboot; the stored setting is untouched. Measures the scrim pass
        // on blend_scrim_us.
        if (request->hasArg("scrim")) {
            a->setScrimOverride(request->arg("scrim").toInt());
        }
        // elemtest=1 shows a 120x120 test plate at the centre through element
        // slot 7 (the press highlight's cost, measured on elem_us); 0 clears.
        if (request->hasArg("elemtest")) {
            SleepAnimation::ElementDesc e;
            if (request->arg("elemtest").toInt() != 0) {
                e.type = SleepAnimation::ElementType::RoundRect;
                e.alpha = LV_OPA_40;
                e.color = 0;
                e.x = 180;
                e.y = 180;
                e.w = 120;
                e.h = 120;
                e.radius = 16;
            }
            a->setElement(SleepAnimation::MAX_ELEMENTS - 1, e);
        }
        // fps=N: temporary animation frame cap, 5..60, 0 restores the stored
        // setting. For the contention A/B (how much of a UI pass is the
        // render task's PSRAM traffic); DefaultUI applies it next pass.
        if (request->hasArg("fps")) {
            const long v = request->arg("fps").toInt();
            if (v == 0 || (v >= 5 && v <= 60)) {
                g_animFpsOverride = static_cast<uint8_t>(v);
            }
        }
        // uianim=0|1|2|3|4: the foreground motion test widget (LV_Helper.h's
        // g_uiAnimTestReq), created by the UI task; 1 and 2 move it through
        // LVGL, 3 through a layer, 4 parks it in a layer.
        if (request->hasArg("uianim")) {
            const int v = request->arg("uianim").toInt();
            if (v >= 0 && v <= 4) {
                g_uiAnimTestReq = v;
            }
        }
        // texts=0|1: live labels as Text elements (gm-2cl.5); textease=0|1:
        // the numeric easing of their values.
        // dials=0|1: the dial tick rings through the TickRing compositor
        // element (1, production) or through LVGL (0), for the framebuffer
        // compare and the refresh-count A/B (gm-2cl.6). DefaultUI applies it
        // on its next pass; elem_rings below reports how many rings the
        // render task is painting.
        if (request->hasArg("texts")) {
            g_textElementsReq = request->arg("texts").toInt() != 0 ? 1 : 0;
        }
        if (request->hasArg("icons")) {
            g_iconLayersReq = request->arg("icons").toInt() != 0 ? 1 : 0;
        }
        if (request->hasArg("animoff")) {
            g_animOffReq = request->arg("animoff").toInt() != 0 ? 1 : 0;
        }
        if (request->hasArg("marquees")) {
            g_marqueeLayersReq = request->arg("marquees").toInt() != 0 ? 1 : 0;
        }
        if (request->hasArg("drawprof")) {
            drawprof::g_req = request->arg("drawprof").toInt() != 0 ? 1 : 0;
        }
        if (request->hasArg("zoomfix")) {
            g_zoomFixReq = request->arg("zoomfix").toInt() != 0 ? 1 : 0;
        }
        if (request->hasArg("clipcorner")) {
            const int v = request->arg("clipcorner").toInt();
            g_clipCornerReq = v < 0 ? 0 : (v > 2 ? 2 : v);
        }
        if (request->hasArg("touchpoll")) {
            touchtask::setPollEnabled(request->arg("touchpoll").toInt() != 0);
        }
        if (request->hasArg("clrruns")) {
            g_clearByRunsReq = request->arg("clrruns").toInt() != 0 ? 1 : 0;
        }
        if (request->hasArg("textease")) {
            g_textEaseReq = request->arg("textease").toInt() != 0 ? 1 : 0;
        }
        if (request->hasArg("dials")) {
            g_dialElementsReq = request->arg("dials").toInt() != 0 ? 1 : 0;
        }
        // crop=0|1: the direct path's per-row chord crop (SleepAnimation's
        // dmaCrop). Off pushes full-width rows again, for an A/B of what the
        // corners of the square framebuffer cost the push stage.
        if (request->hasArg("crop")) {
            a->setDmaCrop(request->arg("crop").toInt() != 0);
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
        // useref=0|1 renders through each animation's portable bandRef()
        // instead of its hand-written band(): the production-conditions A/B
        // behind every asm kernel's claimed win (see SleepAnimation::setUseBandRef).
        if (request->hasArg("useref")) {
            a->setUseBandRef(request->arg("useref").toInt() != 0);
        }
#ifdef GM_KBLOB
        // useblob=0|1 renders through the hot-loaded blob's descriptor
        // (/api/debug/kblob) instead of the stored animation: the visual
        // check and the production A/B for a kernel that was never flashed.
        if (request->hasArg("useblob")) {
            a->setUseBlob(request->arg("useblob").toInt() != 0);
        }
#endif
        // reserve=N sets the internal-DRAM reserve the render task's band
        // buffers are gated on (bganim::internalHasRoomFor): 0 forces them
        // internal, anything above the pool forces them to PSRAM. Tables are
        // not affected; their placement is the hot slab (BgAnimCommon.h).
        if (request->hasArg("reserve")) {
            const long v = request->arg("reserve").toInt();
            bganim::setInternalReserve(v < 0 ? 0 : static_cast<size_t>(v));
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
        doc["useref"] = a->useBandRefOn();
#ifdef GM_TOUCH_PROBE
        // harmostamp=0|1|2: Harmonograph's stamp pass, portable, PIE, or
        // both with a compare (see BgAnimCommon.h).
        if (request->hasArg("harmostamp")) {
            bganim::g_harmoStampMode.store(request->arg("harmostamp").toInt());
        }
        doc["harmostamp"] = bganim::g_harmoStampMode.load();
        doc["harmostamp_checked"] = bganim::g_harmoStampChecked.load();
        doc["harmostamp_mismatch"] = bganim::g_harmoStampMismatch.load();
#endif
#ifdef GM_KBLOB
        doc["useblob"] = a->useBlobOn();
        doc["blob_resident"] = a->kblobResident();
#endif
        doc["reserve"] = static_cast<uint32_t>(bganim::internalReserve());
        doc["anim_sram"] = static_cast<uint32_t>(bganim::g_allocSram);
        doc["anim_psram"] = static_cast<uint32_t>(bganim::g_allocPsram);
        doc["hot_used"] = static_cast<uint32_t>(bganim::hotUsed());
        doc["hot_fail"] = bganim::hotFailCount();
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
        doc["anim_frames"] = a->animFrameCount();
        doc["fps_cap"] = a->maxFpsValue();
        doc["frame_us"] = a->lastFrameUsValue();
        doc["work_us"] = a->lastWorkUsValue();
        doc["wait_us"] = a->lastWaitUsValue();
        doc["slotwait_us"] = a->lastSlotWaitUsValue();
        doc["dma_crop"] = a->dmaCropOnValue();
        // Foreground: overlay refreshes (two reads over a window are the
        // widgets' refresh rate) and the last pass's snapshot/publish cost.
        doc["ov_refreshes"] = g_overlayStats.refreshes;
        doc["lv_flip_presents"] = g_lvFlipStats.presents;
        doc["lv_flip_free"] = g_lvFlipStats.free;
        doc["lv_flip_waits"] = g_lvFlipStats.waits;
        doc["lv_flip_wait_us_max"] = g_lvFlipStats.waitUsMax;
        doc["lv_flip_wait_us_total"] = g_lvFlipStats.waitUsTotal;
        doc["lv_flip_timeouts"] = g_lvFlipStats.timeouts;
        doc["ov_snap_us"] = g_overlayStats.lastSnapUs;
        doc["ov_whole_snap_us"] = g_overlayStats.lastWholeSnapUs;
        doc["ov_whole_pub_us"] = g_overlayStats.lastWholePubUs;
        doc["ov_whole_clear_us"] = g_overlayStats.lastWholeClearUs;
        doc["ov_whole_draw_us"] = g_overlayStats.lastWholeDrawUs;
        doc["ov_whole_scan_us"] = g_overlayStats.lastWholeScanUs;
        doc["ov_whole_scrim_us"] = g_overlayStats.lastWholeScrimUs;
        doc["ov_whole_at_ms"] = g_overlayStats.lastWholeAtMs;
        doc["ov_pub_scan_us"] = g_overlayStats.lastPubScanUs;
        doc["ov_pub_scrim_us"] = g_overlayStats.lastPubScrimUs;
        doc["ov_whole_clear_by_runs"] = g_overlayStats.lastWholeClearByRuns;
        {
            JsonArray st = doc["ov_scrim_stages_us"].to<JsonArray>();
            for (int i = 0; i < 4; i++) {
                st.add(g_overlayStats.scrimStageUs[i]);
            }
        }
        doc["ov_pub_us"] = g_overlayStats.lastPubUs;
        doc["ov_area_px"] = g_overlayStats.lastAreaPx;
        doc["ov_px"] = a->overlayPixels();
        doc["ov_px_rows"] = a->overlayPixelRows();
        doc["ov_clips"] = g_overlayStats.lastClips;
        doc["ov_min_us"] = static_cast<int64_t>(g_overlayMinRefreshUs);
        doc["fps_override"] = g_animFpsOverride;
        doc["ov_gain"] = a->overlayGain();
        doc["elem_us"] = a->lastElementUsValue();
        doc["elem_rings"] = a->ringElementCount();
        doc["dials"] = g_dialElementsReq;
        doc["texts"] = g_textElementsReq;
        doc["icons"] = g_iconLayersReq;
        doc["anim_off"] = g_animOffReq;
        doc["marquees"] = g_marqueeLayersReq;
        doc["zoomfix"] = g_zoomFixReq;
        doc["drawprof_req"] = drawprof::g_req;
        drawprof::report(doc, "drawprof");
        doc["clipcorner"] = g_clipCornerReq;
        doc["clrruns"] = g_clearByRunsReq;
        doc["touch_poll"] = touchtask::pollEnabled();
        doc["touch_task"] = touchtask::running();
        doc["touch_samples"] = touchtask::sampleCount();
        doc["touch_hwm"] = touchtask::stackHighWaterBytes();
        doc["hitmap_n"] = touchtask::hitMapCount();
        doc["hitmap_gen"] = touchtask::hitMapGeneration();
        doc["textease"] = g_textEaseReq;
        doc["elem_text"] = a->textElementCount();
        {
            JsonArray td = doc["text_dbg"].to<JsonArray>();
            for (int i = 0; i < 8; i++) {
                td.add(g_textDbg[i]);
            }
        }
        {
            // Newest last; age_ms is how long ago the rect was harvested.
            JsonArray dr = doc["dirty_recent"].to<JsonArray>();
            const uint32_t n = g_dirtyLogCount;
            const uint32_t nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000);
            const uint32_t from = n > static_cast<uint32_t>(DIRTYLOG_N) ? n - DIRTYLOG_N : 0;
            for (uint32_t i = from; i < n; i++) {
                const DirtyLogEntry &e = g_dirtyLog[i % DIRTYLOG_N];
                JsonArray r = dr.add<JsonArray>();
                r.add(e.x1);
                r.add(e.y1);
                r.add(e.x2);
                r.add(e.y2);
                r.add(nowMs - e.tMs);
            }
            doc["dirty_total"] = n;
        }
        {
            JsonArray te = doc["text_elems"].to<JsonArray>();
            for (int i = 0; i < 6; i++) {
                const TextElemDbg &d = g_textElemDbg[i];
                if (!d.owned) {
                    continue;
                }
                JsonObject o = te.add<JsonObject>();
                o["text"] = d.text;
                o["x"] = d.x;
                o["y"] = d.y;
                o["w"] = d.w;
                o["h"] = d.h;
                o["ver"] = d.ver;
            }
        }
        {
            JsonArray il = doc["icon_layers"].to<JsonArray>();
            for (int i = 0; i < 2; i++) {
                const IconLayerDbg &d = g_iconLayerDbg[i];
                if (!d.owned) {
                    continue;
                }
                JsonObject o = il.add<JsonObject>();
                o["x"] = d.x;
                o["y"] = d.y;
                o["w"] = d.w;
                o["h"] = d.h;
                o["shown"] = d.shown;
                o["toggles"] = d.toggles;
            }
        }
        {
            JsonArray ml = doc["marquee_layers"].to<JsonArray>();
            for (int i = 0; i < 2; i++) {
                const MarqueeDbg &d = g_marqueeDbg[i];
                if (!d.owned) {
                    continue;
                }
                JsonObject o = ml.add<JsonObject>();
                o["x"] = d.x;
                o["y"] = d.y;
                o["w"] = d.w;
                o["h"] = d.h;
                o["period"] = d.period;
                o["ms"] = d.ms;
            }
        }
        doc["atlas_bytes"] = glyphatlas::bytesUsed();
        doc["atlas_glyphs"] = glyphatlas::glyphCount();
        doc["band_internal"] = a->bandBufInternal();
#ifdef GM_BLEND_PROBE
        doc["probe"] = a->blendProbeLevel();
        doc["probe_px"] = a->probePixels();
        doc["probe_reps"] = a->probeRepsValue();
        doc["bpie"] = a->bpie();
        doc["probe_mismatch"] = a->probeMismatchValue();
#endif
        doc["ov_gain_target"] = a->overlayGainTarget();
        doc["tick_cache_bytes"] = meterticks::bytesAllocated();
        doc["uianim"] = g_uiAnimTestReq;
        doc["layer_us"] = a->lastLayerUsValue();
        {
            JsonArray ls = doc["layers"].to<JsonArray>();
            for (int i = 0; i < SleepAnimation::MAX_LAYERS; i++) {
                const SleepAnimation::LayerInfo li = a->layerInfo(i);
                if (!li.used) {
                    continue;
                }
                JsonObject o = ls.add<JsonObject>();
                o["id"] = i;
                o["visible"] = li.visible;
                o["animating"] = li.animating;
                o["x"] = li.x;
                o["y"] = li.y;
                o["w"] = li.w;
                o["h"] = li.h;
            }
        }
        doc["band_us"] = a->lastBandUsValue();
        doc["expand_us"] = a->lastExpandUsValue();
        doc["fill_us"] = a->lastFillUsValue();
        doc["copy_us"] = a->lastCopyUsValue();
        doc["half_psram"] = esp_ptr_external_ram(const_cast<void *>(a->halfBufAddr()));
        doc["blend_us"] = a->lastBlendUsValue();
        doc["blend_scrim_us"] = a->lastBlendScrimUsValue();
        doc["scrim_override"] = a->scrimOverrideValue();
        doc["anim_internal"] = a->objectInternal();
        doc["msync_us"] = a->lastMsyncUsValue();
        doc["push_us"] = a->lastPushUsValue();
        doc["framefn_us"] = a->lastFrameFnUsValue();
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
        // Which animation is configured (SleepAnimation::currentAnimId(),
        // not the GM_ANIM_BENCH-only benchCurrentAnim() below: this route
        // exists in every build, not just the bench one) and millis(), so a
        // touchmap dump and this poll can be lined up without a separate
        // round trip to some other endpoint.
        doc["anim_id"] = a->currentAnimId();
        doc["uptime_ms"] = millis();
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

    // /api/debug/touchlog: the last touch edges (LV_Helper.h, g_touchLog),
    // oldest first, each with the panel point and the object the press
    // landed on, named by its objects[] entry when it is a generated one.
    server.on("/api/debug/touchlog", [](AsyncWebServerRequest *request) {
        // PSRAM, per request: a static buffer this size would be internal
        // DRAM the WiFi stack needs more than a debug page does.
        constexpr size_t CAP = TOUCHLOG_N * 96 + 64;
        char *buf = static_cast<char *>(heap_caps_malloc(CAP, MALLOC_CAP_SPIRAM));
        if (buf == nullptr) {
            request->send(503, "application/json", "{\"error\":\"no memory\"}");
            return;
        }
        size_t len = 0;
        const uint32_t count = g_touchLogCount;
        const uint32_t first = count > static_cast<uint32_t>(TOUCHLOG_N) ? count - TOUCHLOG_N : 0;
        len += snprintf(buf + len, CAP - len, "{\"count\":%u,\"edges\":[", static_cast<unsigned>(count));
        lv_obj_t **arr = reinterpret_cast<lv_obj_t **>(&objects);
        const char **names = eez_flow_object_names();
        const size_t nObj = sizeof(objects) / sizeof(lv_obj_t *);
        for (uint32_t i = first; i < count && len + 96 < CAP; i++) {
            const TouchLogEntry en = g_touchLog[i % TOUCHLOG_N];
            const char *name = nullptr;
            if (en.hit != nullptr) {
                name = "(unnamed)";
                for (size_t k = 0; k < nObj; k++) {
                    if (arr[k] == en.hit) {
                        name = names != nullptr ? names[k] : "?";
                        break;
                    }
                }
            }
            len += snprintf(buf + len, CAP - len, "%s{\"t\":%u,\"press\":%d,\"syn\":%d,\"x\":%d,\"y\":%d,\"hit\":%s%s%s}",
                            i == first ? "" : ",", static_cast<unsigned>(en.tMs), en.press ? 1 : 0, en.syn ? 1 : 0, en.x, en.y,
                            name != nullptr ? "\"" : "", name != nullptr ? name : "null", name != nullptr ? "\"" : "");
        }
        len += snprintf(buf + len, CAP - len, "]}");
        request->send(200, "application/json", buf);
        heap_caps_free(buf);
    });

#ifdef GM_TOUCH_PROBE
    // /api/debug/gradfix[?on=0|1|2|3][&y0=&y1=][&xoff=0|1][&btone=&ktone=]
    // [&stops=<wire>[&anim=N]]: the gradient framebuffer fixture (gm-nov3.10).
    //
    // The question it answers is whether the framebuffer really holds the
    // colours web/src/config/gradientRamp.js says it will. tools/animbench's
    // ramp_parity.js already proves that module equal to the firmware's C++,
    // entry by entry, but a host-to-host proof cannot see the device: a
    // stored brightness, a stored rolloff, a gradient reference that resolves
    // to something else, or a compositor that drops bits would all pass it.
    //
    // What it cannot answer (gm-nov3.22). The reads go to framebuffer memory
    // in PSRAM, which is where the render task leaves its pixels, so the
    // result is evidence about the compositor and about nothing downstream of
    // it. It says nothing about RGB scan-out, the bounce buffers, the DMA or
    // the LCD_CAM peripheral, nothing about panel timing (the pixel-clock
    // divider is not in this path and does not qualify the result either
    // way), nothing about the ribbon or the wiring, nothing about the
    // controller board, and nothing about what the glass shows. A panel could
    // be dark, torn or miswired with every sample here still matching.
    //
    // Comparing an arbitrary animated frame is not an option. An animation
    // maps the palette through its own pattern and its own gain, so a moving
    // frame has no known relation to a ramp. So this paints a known one: rows
    // [y0, y1) of the framebuffer carry the active theme's 256-entry ramp,
    // column x holding index ((x + xoff) * 255) / (w - 1), written after every
    // compositing stage so no overlay, layer, element or scrim can reach it.
    // xoff is there because /api/debug/fb only delivers the framebuffer
    // subsampled, so a host reads even columns and would never see the index
    // on column 479; at xoff 1 that index lands on column 478 instead.
    //
    // The JSON is what makes the comparison possible rather than merely
    // repeatable: it reports the stops BEFORE tone, the interpolation mode,
    // both tone percentages with the integer values they converted to, the
    // palette gain and the exact sampled region. A host feeds those back into
    // the sampler and compares. Reading them here rather than from
    // /api/settings is deliberate: that endpoint returns the WiFi password in
    // clear text, and it would report what is stored rather than what the
    // render task actually used.
    //
    // Bench builds only, never persisted, cleared by a reboot.
    // tools/gradient_fb_check.py drives it.
    server.on("/api/debug/gradfix", [this](AsyncWebServerRequest *request) {
        SleepAnimation *a = sleep_animation_bench_instance();
        if (a == nullptr) {
            request->send(409, "application/json", "{\"error\":\"animation not running\"}");
            return;
        }
        LilyGoDriver *drv = LilyGoDriver::peekInstance();
        Display *disp = drv != nullptr ? drv->getDisplay() : nullptr;
        if (disp == nullptr) {
            request->send(404, "application/json", "{\"error\":\"not a LilyGo panel\"}");
            return;
        }
        if (request->hasArg("on")) {
            const int mode = request->arg("on").toInt();
            const int h = disp->height();
            int y0 = request->hasArg("y0") ? request->arg("y0").toInt() : a->rampFixtureY0();
            int y1 = request->hasArg("y1") ? request->arg("y1").toInt() : a->rampFixtureY1();
            y0 = y0 < 0 ? 0 : (y0 > h ? h : y0);
            y1 = y1 < 0 ? 0 : (y1 > h ? h : y1);
            const int xoff = request->hasArg("xoff") ? request->arg("xoff").toInt() : 0;
            a->setRampFixture(mode, y0, y1, xoff);
        }
        // stops=<wire>[&anim=N]: hold an arbitrary gradient on the panel through
        // the gradient editor's own live-preview path, which is what makes this
        // usable for a whole batch: every entry in data/gradients.json can be
        // put on the panel in turn without writing one stored setting. An empty
        // value ends the preview and the stored theme comes back on the next UI
        // pass. The preview lapses on its own after BGANIM_PREVIEW_HOLD_MS, so a
        // caller re-sends it per pass; the report below says which stops the
        // render task actually resolved, so a lapsed preview reads as the wrong
        // gradient rather than as a silent pass.
        if (request->hasArg("stops")) {
            const String wire = request->arg("stops");
            if (wire.length() == 0) {
                pluginManager->trigger("bganim:preview-end");
            } else {
                Event ev;
                ev.id = "bganim:preview";
                ev.setInt("anim", request->hasArg("anim") ? request->arg("anim").toInt() : a->currentAnimId());
                ev.setString("stops", wire);
                pluginManager->trigger(ev);
            }
        }
        // btone=/ktone=: the animation brightness and highlight rolloff in
        // percent, held against DefaultUI's per-pass re-apply of the stored
        // values until -1 releases them. This is how the fixture is run at a
        // tone other than the device's own without writing NVS, and without
        // POSTing /api/settings, which would need the WiFi password echoed
        // back at it. The tone the render task ended up with is in the report
        // below, so a caller checks the override took rather than assuming it.
        //
        // Both percentages go out in one atomic store (gm-nov3.22), so a
        // request that carries both can never be seen half applied by the UI
        // task. An argument that is absent keeps whatever is held now, which
        // is why the current pair is read back first. Every request runs on
        // the async_tcp task, so this is the only writer and the
        // read-modify-write needs no compare-exchange.
        if (request->hasArg("btone") || request->hasArg("ktone")) {
            int btone = -1, ktone = -1;
            gm_tone_unpack(g_animToneOverride.load(std::memory_order_relaxed), btone, ktone);
            if (request->hasArg("btone")) {
                btone = request->arg("btone").toInt();
            }
            if (request->hasArg("ktone")) {
                ktone = request->arg("ktone").toInt();
            }
            g_animToneOverride.store(gm_tone_pack(btone, ktone), std::memory_order_release);
        }
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        JsonDocument doc(&psramAllocator);
        doc["fw"] = BUILD_GIT_VERSION;
        doc["built"] = BUILD_TIMESTAMP;
        const int mode = a->rampFixtureMode();
        doc["armed"] = mode != 0;
        doc["mode"] = mode == 3 ? "wheel" : (mode == 2 ? "reversed" : (mode == 1 ? "ramp" : "off"));
        doc["frames"] = a->rampFixtureFrames();
        doc["y0"] = a->rampFixtureY0();
        doc["y1"] = a->rampFixtureY1();
        doc["x0"] = 0;
        doc["x1"] = disp->width();
        doc["w"] = disp->width();
        doc["h"] = disp->height();
        doc["xoff"] = a->rampFixtureXoff();
        // Spelled out so a reader of the report never has to find this file.
        doc["map"] = "index = ((x + xoff) * 255) / (w - 1), clamped to 255, every row of [y0,y1) alike";
        // The gain buildThemeRamp is called with. Fixed at 256 (none): an
        // animation's extra gain is a property of that animation, and the
        // fixture represents the theme.
        doc["gain256"] = 256;
        // The percentages the render task's tone should come from, read in one
        // atomic load, and read BEFORE the integers below. A UI pass applies
        // the pair a moment after the store lands, so integers read first
        // could be older than the percentages beside them and the host would
        // see a conversion that never happened. This way the integers are
        // never staler than the percentages, and a host that waits for the two
        // to agree is waiting for the render task to pick the tone up.
        int overrideB = -1, overrideK = -1;
        gm_tone_unpack(g_animToneOverride.load(std::memory_order_acquire), overrideB, overrideK);
        const int brightnessPct = overrideB >= 0 ? overrideB : controller->getSettings().getBgAnimBrightness();
        const int kneePct = overrideK >= 0 ? overrideK : controller->getSettings().getBgAnimHighlightKnee();
        int bright256 = 256, knee = 255;
        bganim::themeToneState(&bright256, &knee);
        doc["brightness256"] = bright256;
        doc["knee"] = knee;
        // The two settings the render task's tone came from, so the host can
        // run the same percent-to-integer conversion and check it landed on
        // the values above.
        doc["brightnessPct"] = brightnessPct;
        doc["kneePct"] = kneePct;
        doc["toneOverride"] = overrideB >= 0 || overrideK >= 0;
        uint8_t stops[BG_THEME_MAX_STOPS][3];
        uint8_t pos[BG_THEME_MAX_STOPS];
        bool uniform = true;
        const int n = bganim::themeRawStops(stops, pos, BG_THEME_MAX_STOPS, &uniform);
        doc["stopCount"] = n;
        doc["uniform"] = uniform;
        doc["themeGen"] = bganim::themeGen();
        // The wire format the firmware's own parser reads, so the host can
        // hand it straight to parseGradientWire without reassembling it: bare
        // hex for a uniform theme, hex@pos for a positional one.
        String wire;
        for (int i = 0; i < n; i++) {
            char part[16];
            if (uniform) {
                snprintf(part, sizeof(part), "%s%02x%02x%02x", i == 0 ? "" : ",", stops[i][0], stops[i][1], stops[i][2]);
            } else {
                snprintf(part, sizeof(part), "%s%02x%02x%02x@%u", i == 0 ? "" : ",", stops[i][0], stops[i][1], stops[i][2],
                         static_cast<unsigned>(pos[i]));
            }
            wire += part;
        }
        doc["stops"] = wire;
        serializeJson(doc, *response);
        request->send(response);
    });

#endif // GM_TOUCH_PROBE

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
        // board has no business allocating that to answer a debug request.
        //
        // Two rules this callback has to obey, both learned the hard way
        // (gm-6ivh). Returning 0 while rows remain ends the response: the
        // library reads 0 as "no more data", writes the terminating chunk and
        // closes, so the client gets a well formed but short dump rather than
        // an error. A row-at-a-time filler returns 0 exactly when the budget
        // is smaller than one row, and at step 1 a row is 960 bytes, which is
        // what the remaining TCP window always falls below on the first send
        // round: 2 rows, 2 rows, 1 row and then a 928 byte budget, so every
        // step=1 request delivered 4,800 of 460,800 bytes and said it was
        // complete. So fill whole pixels rather than whole rows, and say
        // RESPONSE_TRY_AGAIN when even two bytes do not fit, which parks the
        // response until the next ack instead of ending it.
        //
        // The position comes from index (the bytes already filled) rather than
        // from a heap cell the callback owns. The old cell was deleted only on
        // the final call, so a client that disconnected part way through never
        // freed it: 300 aborted requests cost 6,420 bytes of internal heap on
        // the bench board and none of it came back. Deriving the position
        // costs two divisions per call and cannot leak. The simulator branch
        // below already did it this way.
        AsyncWebServerResponse *response = request->beginChunkedResponse(
            "application/octet-stream", [fb, w, step, ow, oh](uint8_t *out, size_t maxLen, size_t index) -> size_t {
                const size_t rowBytes = static_cast<size_t>(ow) * 2;
                int oy = static_cast<int>(index / rowBytes);
                int ox = static_cast<int>((index % rowBytes) / 2);
                size_t written = 0;
                while (oy < oh && written + 2 <= maxLen) {
                    const uint16_t *src = fb + static_cast<size_t>(oy) * step * w;
                    int run = static_cast<int>((maxLen - written) / 2);
                    if (run > ow - ox)
                        run = ow - ox;
                    for (int i = 0; i < run; i++) {
                        // Byte stores, because where a chunk starts inside
                        // the library's send buffer is the library's business
                        // (six bytes in, to leave room for the chunk header,
                        // today) and a pixel no longer lands on a row
                        // boundary.
                        const uint16_t c = src[static_cast<size_t>(ox + i) * step];
                        out[written + static_cast<size_t>(i) * 2] = static_cast<uint8_t>(c);
                        out[written + static_cast<size_t>(i) * 2 + 1] = static_cast<uint8_t>(c >> 8);
                    }
                    written += static_cast<size_t>(run) * 2;
                    ox += run;
                    if (ox >= ow) {
                        ox = 0;
                        oy++;
                    }
                }
                if (written == 0 && oy < oh)
                    return RESPONSE_TRY_AGAIN;
                return written;
            });
        char disposition[64];
        snprintf(disposition, sizeof(disposition), "%dx%d", ow, oh);
        response->addHeader("X-FB-Size", disposition);
        request->send(response);
    });

    // /api/debug/ovl?step=1..8: the front overlay snapshot as RGB565 little
    // endian plus a coverage byte, 3 bytes a pixel (converted from the
    // planar buffer, so readers see the format they always did), subsampled
    // like the framebuffer dump. X-OV-Size carries the output size. The
    // instrument for what the composite is actually asked to blend:
    // coverage, flat stretches, the plates' real pixel values.
    server.on("/api/debug/ovl", [](AsyncWebServerRequest *request) {
        SleepAnimation *a = sleep_animation_bench_instance();
        int w = 0, h = 0;
        const uint16_t *buf = a != nullptr ? reinterpret_cast<const uint16_t *>(a->overlayFrontBuffer()) : nullptr;
        const uint32_t planePx = a != nullptr ? a->overlayPlanePixels() : 0;
        if (buf == nullptr || !a->overlayFrontSize(w, h) || w <= 0 || h <= 0) {
            request->send(404, "application/json", "{\"error\":\"no overlay published\"}");
            return;
        }
        int step = request->hasArg("step") ? request->arg("step").toInt() : 1;
        if (step < 1 || step > 8)
            step = 1;
        const int ow = w / step;
        const int oh = h / step;
        // Pixel granular, not row granular like the framebuffer dump: a
        // 3-byte row of 480 pixels is 1440 bytes, and a later chunk's budget
        // can be just under that, which would end the response after the
        // first chunk. The position comes from index (the bytes already
        // filled), so nothing is allocated for it and a client that
        // disconnects part way through leaves nothing behind, which is the
        // leak the framebuffer dump above had (gm-6ivh). Three bytes a pixel
        // and only whole pixels written, so index is always a multiple of 3.
        AsyncWebServerResponse *response = request->beginChunkedResponse(
            "application/octet-stream", [buf, planePx, w, step, ow, oh](uint8_t *out, size_t maxLen, size_t index) -> size_t {
                const int total = ow * oh;
                int pix = static_cast<int>(index / 3);
                size_t written = 0;
                while (pix < total && written + 3 <= maxLen) {
                    const int oy = pix / ow;
                    const int ox = pix - oy * ow;
                    const size_t at = static_cast<size_t>(oy) * step * w + static_cast<size_t>(ox) * step;
                    const uint16_t c = buf[at];
                    const uint16_t a16 = buf[planePx + at];
                    out[written] = static_cast<uint8_t>(c);
                    out[written + 1] = static_cast<uint8_t>(c >> 8);
                    out[written + 2] = a16 > 255 ? 255 : static_cast<uint8_t>(a16);
                    written += 3;
                    pix++;
                }
                if (written == 0 && pix < total)
                    return RESPONSE_TRY_AGAIN;
                return written;
            });
        char disposition[64];
        snprintf(disposition, sizeof(disposition), "%dx%d", ow, oh);
        response->addHeader("X-OV-Size", disposition);
        request->send(response);
    });
#endif // !GAGGIMATE_HEADLESS && !GAGGIMATE_SIM

#ifdef GAGGIMATE_SIM
    // /api/debug/fb?step=1..8: the simulator's equivalent of the device
    // branch above, same query semantics and X-FB-Size header. The device
    // reads a real panel framebuffer directly; the simulator has none, so
    // SdlDriver::copyFrameRGB565 renders the accumulated SDL texture (what
    // pumpAndRender() would show onscreen) into a full-frame RGB565 snapshot
    // and this subsamples and streams it the same way. n= is accepted only
    // as 0 (there is one "framebuffer") so a script written against the
    // device branch needs no venue-specific code.
    server.on("/api/debug/fb", [](AsyncWebServerRequest *request) {
        if (request->hasArg("n") && request->arg("n").toInt() != 0) {
            request->send(400, "application/json", "{\"error\":\"bad buffer index\"}");
            return;
        }
        SdlDriver *drv = SdlDriver::getInstance();
        const int w = drv->width();
        const int h = drv->height();
        auto *frame = new std::vector<uint16_t>(static_cast<size_t>(w) * static_cast<size_t>(h));
        if (!drv->copyFrameRGB565(frame->data(), frame->size() * sizeof(uint16_t))) {
            delete frame;
            request->send(503, "application/json", "{\"error\":\"no frame\"}");
            return;
        }
        int step = request->hasArg("step") ? request->arg("step").toInt() : 1;
        if (step < 1 || step > 8)
            step = 1;
        const int ow = w / step;
        const int oh = h / step;
        const size_t total = static_cast<size_t>(ow) * static_cast<size_t>(oh) * 2;
        // The sim shim (sim/web/ESPAsyncWebServer.h) has no beginChunkedResponse;
        // its beginResponse(contentType, len, filler) calls the filler exactly
        // once with maxLen == len, unlike the device's chunked callback above
        // (repeated calls, each bounded by the TCP send buffer). Deriving the
        // starting row from index rather than a separate row counter keeps this
        // correct either way, and frees frame once index+written reaches total
        // instead of waiting on a since-nonexistent extra all-zero call.
        AsyncWebServerResponse *response =
            request->beginResponse("application/octet-stream", total,
                                   [frame, w, step, ow, oh, total](uint8_t *out, size_t maxLen, size_t index) -> size_t {
                                       const size_t rowBytes = static_cast<size_t>(ow) * 2;
                                       size_t written = 0;
                                       int row = static_cast<int>(index / rowBytes);
                                       while (row < oh && written + rowBytes <= maxLen) {
                                           const uint16_t *src = frame->data() + static_cast<size_t>(row) * step * w;
                                           uint16_t *dst = reinterpret_cast<uint16_t *>(out + written);
                                           for (int x = 0; x < ow; x++)
                                               dst[x] = src[x * step];
                                           written += rowBytes;
                                           row++;
                                       }
                                       if (index + written >= total) {
                                           delete frame;
                                       }
                                       return written;
                                   });
        char disposition[64];
        snprintf(disposition, sizeof(disposition), "%dx%d", ow, oh);
        response->addHeader("X-FB-Size", disposition);
        request->send(response);
    });
#endif // GAGGIMATE_SIM

#ifndef GAGGIMATE_HEADLESS
    // /api/debug/touchmap[?screen=N[&load=1]]: with screen=, asks the UI
    // task to dump that screen's object tree (LV_Helper.h, g_touchMapReq).
    // screen=0 dumps the active screen (lv_scr_act()) without loading
    // anything, so load= is ignored there. Without arguments, returns the
    // last dump, or {"pending":true} while the UI task has not written it
    // yet: g_touchMapPending tracks that separately from g_touchMapReq now
    // that 0 is a real request rather than "nothing queued". Registered
    // outside the real-panel-only block above (like /api/debug/tap): the
    // simulator's UI task services this too (DefaultUI::serviceTouchMap).
    server.on("/api/debug/touchmap", [](AsyncWebServerRequest *request) {
        if (request->hasArg("screen")) {
            const int id = request->arg("screen").toInt();
            if (id < 0 || id > 11) {
                request->send(400, "application/json", "{\"error\":\"screen 0..11\"}");
                return;
            }
            g_touchMapLen = 0;
            g_touchMapLoad = id != 0 && request->hasArg("load") && request->arg("load").toInt() != 0;
            g_touchMapReq = id;
            g_touchMapPending = true;
            request->send(200, "application/json", "{\"queued\":true}");
            return;
        }
        if (g_touchMapPending || g_touchMapLen == 0 || g_touchMapBuf == nullptr) {
            request->send(200, "application/json", "{\"pending\":true}");
            return;
        }
        request->send(200, "application/json", g_touchMapBuf);
    });
#endif // !GAGGIMATE_HEADLESS

#if GM_TOUCH_INJECT
    // /api/debug/tap?x=<0..479>&y=<0..479>[&ms=<hold, default 80>][&x2=&y2=]:
    // queues one synthetic tap (TouchInject.h), or a drag from x,y to x2,y2
    // over the hold when both are given. touchpad_read (LV_Helper.cpp) and, on the
    // simulator, mouse_read (SdlDriver.cpp) poll it ahead of their own reads,
    // so a scripted request drives the same screen-change code a finger does.
    // Without x/y, returns the in-flight request's observed timing so a
    // script can wait out a hold before reading the result. Registered
    // outside the real-panel-only block above: the simulator needs this
    // route too, unlike most of the routes in that block.
    server.on("/api/debug/tap", [](AsyncWebServerRequest *request) {
        if (request->hasArg("x") || request->hasArg("y")) {
            if (!request->hasArg("x") || !request->hasArg("y")) {
                request->send(400, "application/json", "{\"error\":\"x and y both required\"}");
                return;
            }
            const int x = request->arg("x").toInt();
            const int y = request->arg("y").toInt();
            const int ms = request->hasArg("ms") ? request->arg("ms").toInt() : static_cast<int>(TOUCH_INJECT_DEFAULT_HOLD_MS);
            if (x < TOUCH_INJECT_MIN_COORD || x > TOUCH_INJECT_MAX_COORD || y < TOUCH_INJECT_MIN_COORD ||
                y > TOUCH_INJECT_MAX_COORD || ms < static_cast<int>(TOUCH_INJECT_MIN_HOLD_MS) ||
                ms > static_cast<int>(TOUCH_INJECT_MAX_HOLD_MS)) {
                request->send(400, "application/json", "{\"error\":\"x,y 0..479, ms 20..10000\"}");
                return;
            }
            int x2 = x, y2 = y;
            if (request->hasArg("x2") || request->hasArg("y2")) {
                if (!request->hasArg("x2") || !request->hasArg("y2")) {
                    request->send(400, "application/json", "{\"error\":\"x2 and y2 both required\"}");
                    return;
                }
                x2 = request->arg("x2").toInt();
                y2 = request->arg("y2").toInt();
                if (x2 < TOUCH_INJECT_MIN_COORD || x2 > TOUCH_INJECT_MAX_COORD || y2 < TOUCH_INJECT_MIN_COORD ||
                    y2 > TOUCH_INJECT_MAX_COORD) {
                    request->send(400, "application/json", "{\"error\":\"x2,y2 0..479\"}");
                    return;
                }
            }
            if (!touchInjectRequest(static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(x2),
                                    static_cast<int16_t>(y2), static_cast<uint32_t>(ms))) {
                bool active;
                uint32_t remainingMs, pressedAtMs, releasedAtMs;
                touchInjectState(active, remainingMs, pressedAtMs, releasedAtMs);
                char buf[64];
                snprintf(buf, sizeof(buf), "{\"error\":\"busy\",\"remaining_ms\":%u}", static_cast<unsigned>(remainingMs));
                request->send(409, "application/json", buf);
                return;
            }
            request->send(200, "application/json", "{\"queued\":true}");
            return;
        }
        bool active;
        uint32_t remainingMs, pressedAtMs, releasedAtMs;
        touchInjectState(active, remainingMs, pressedAtMs, releasedAtMs);
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"active\":%s,\"remaining_ms\":%u,\"pressed_at_ms\":%u,\"released_at_ms\":%u}",
                 active ? "true" : "false", static_cast<unsigned>(remainingMs), static_cast<unsigned>(pressedAtMs),
                 static_cast<unsigned>(releasedAtMs));
        request->send(200, "application/json", buf);
    });
#endif

#ifdef GM_SYNTH_HANDSHAKE
    // /api/debug/synth[?brew=0|1]: quiets or restores the load rig's
    // synthetic brew lifecycle (Controller.cpp, GM_SYNTH_HANDSHAKE), so a rig
    // soak can sit on the menu or settings screens instead of being dragged
    // back to the status screen every 45 s or forced to the brew screen after
    // 60 s idle. brew=0 while a synthetic brew is in progress ends it (one
    // controller:brew:end, so ShotHistoryPlugin's recording closes) before
    // quieting; the handshake itself and the temperature/pressure telemetry
    // ramp keep running either way. The request is queued here and consumed
    // by Controller::loop on its own thread, never applied from this task.
    server.on("/api/debug/synth", [this](AsyncWebServerRequest *request) {
        if (request->hasArg("brew")) {
            controller->synthBrewCycleRequest = request->arg("brew").toInt() != 0 ? 1 : 0;
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "{\"brew_cycle\":%s,\"brewing\":%s}", controller->synthBrewCycleOn ? "true" : "false",
                 controller->synthBrewingNow ? "true" : "false");
        request->send(200, "application/json", buf);
    });
#endif

#ifndef GAGGIMATE_SIM
    // The flash bus mode the chip is running, against what the two image
    // headers ask for. The board file says qio at 80 MHz; the IDF sdkconfig
    // said dio and nothing had checked which one the bootloader applied. An
    // instruction-cache miss is a flash line read, and the LVGL draw path
    // per object is longer than the 16 KB cache, so this bus is what every
    // widget draw waits on (gm-2cl.19 profile, 2026-09-08).
    server.on("/api/debug/flashmode", [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        JsonDocument doc(&psramAllocator);
        const uint32_t ctrl = REG_READ(SPI_MEM_CTRL_REG(0));
        const char *live = (ctrl & SPI_MEM_FREAD_QIO)    ? "qio"
                           : (ctrl & SPI_MEM_FREAD_QUAD) ? "qout"
                           : (ctrl & SPI_MEM_FREAD_DIO)  ? "dio"
                           : (ctrl & SPI_MEM_FREAD_DUAL) ? "dout"
                                                         : "slow";
        doc["live_mode"] = live;
        doc["spi0_ctrl"] = ctrl;
        // clk register: 0 with the sysclk bit means the full source clock.
        doc["spi0_clock_reg"] = REG_READ(SPI_MEM_CLOCK_REG(0));
        static const char *const modes[] = {"qio", "qout", "dio", "dout", "fast", "slow"};
        esp_image_header_t hdr;
        if (esp_flash_read(nullptr, &hdr, 0x0, sizeof(hdr)) == ESP_OK && hdr.magic == ESP_IMAGE_HEADER_MAGIC) {
            doc["bootloader_mode"] = hdr.spi_mode < 6 ? modes[hdr.spi_mode] : "?";
            doc["bootloader_speed"] = hdr.spi_speed;
        }
        const esp_partition_t *run = esp_ota_get_running_partition();
        if (run != nullptr && esp_partition_read(run, 0, &hdr, sizeof(hdr)) == ESP_OK && hdr.magic == ESP_IMAGE_HEADER_MAGIC) {
            doc["app_mode"] = hdr.spi_mode < 6 ? modes[hdr.spi_mode] : "?";
            doc["app_speed"] = hdr.spi_speed;
            doc["app_partition"] = run->label;
        }
        serializeJson(doc, *response);
        request->send(response);
    });
#endif
#ifdef GM_DRAW_PROFILE
    // Per-object draw profile of the active screen (DrawProfile.h). arm=1
    // attaches to the next screen DefaultUI tunes, arm=0 stops attaching;
    // the report is whatever the last attached screen has drawn since.
    server.on("/api/debug/drawprof", [](AsyncWebServerRequest *request) {
        if (request->hasArg("arm")) {
            drawprof::g_req = request->arg("arm").toInt() != 0 ? 1 : 0;
        }
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        JsonDocument doc(&psramAllocator);
        doc["armed"] = drawprof::g_req;
        drawprof::report(doc, "drawprof", 64);
        serializeJson(doc, *response);
        request->send(response);
    });
#endif
#if defined(GM_TOUCH_PROBE) || defined(GAGGIMATE_SIM)
    // /api/debug/settingsui[?open=1|close=1|cat=N|page=N|pop=1]: opens,
    // closes and navigates the on-display settings shell from a script and
    // reports where it is. Queued here (DefaultUI::queueSettingsUiCommand)
    // and executed on the UI task (DefaultUI::serviceSettingsUi), same
    // split as /api/debug/tap: LVGL is touched only from that task. One
    // command per request (400 for two at once, or an out-of-range cat/
    // negative page, before anything is queued); a request while a command
    // is already in flight (an Open can span several UI passes while it
    // waits for the menu screen) is 409, never silently dropped. Without
    // arguments, returns the shell's last published state plus the
    // Fixture counters (the bench/sim-only category the shared contract
    // exercises the lifecycle against before any real category exists)
    // and the seq of the last completed command.
    // String::toInt() turns "oops" and "" into 0 and accepts numeric
    // prefixes, so every argument is parsed as a whole decimal string here
    // before anything is queued (a malformed cat would otherwise pop and
    // commit the open category).
    auto parseWholeInt = [](const String &s, int &out) -> bool {
        const char *p = s.c_str();
        if (*p == '\0') {
            return false;
        }
        char *end = nullptr;
        const long v = strtol(p, &end, 10);
        if (end == p || *end != '\0' || v < INT32_MIN || v > INT32_MAX) {
            return false;
        }
        out = static_cast<int>(v);
        return true;
    };
    server.on("/api/debug/settingsui", [this, parseWholeInt](AsyncWebServerRequest *request) {
        const bool hasOpen = request->hasArg("open");
        const bool hasClose = request->hasArg("close");
        const bool hasCat = request->hasArg("cat");
        const bool hasPage = request->hasArg("page");
        const bool hasPop = request->hasArg("pop");
        const int argCount = hasOpen + hasClose + hasCat + hasPage + hasPop;
        if (argCount > 1) {
            request->send(400, "application/json", "{\"error\":\"one command per request\"}");
            return;
        }
        if (argCount == 1) {
            DefaultUI::SettingsUiCmd cmd;
            int arg = 0;
            if (hasOpen) {
                if (request->arg("open") != "1") {
                    request->send(400, "application/json", "{\"error\":\"open must be 1\"}");
                    return;
                }
                cmd = DefaultUI::SettingsUiCmd::Open;
            } else if (hasClose) {
                if (request->arg("close") != "1") {
                    request->send(400, "application/json", "{\"error\":\"close must be 1\"}");
                    return;
                }
                cmd = DefaultUI::SettingsUiCmd::Close;
            } else if (hasCat) {
                if (!parseWholeInt(request->arg("cat"), arg) || arg < 0 || arg >= DefaultUI::kSettingsUiCategoryCount) {
                    request->send(400, "application/json", "{\"error\":\"cat out of range\"}");
                    return;
                }
                cmd = DefaultUI::SettingsUiCmd::Cat;
            } else if (hasPage) {
                if (!parseWholeInt(request->arg("page"), arg) || arg < 0) {
                    request->send(400, "application/json", "{\"error\":\"page must be >= 0\"}");
                    return;
                }
                cmd = DefaultUI::SettingsUiCmd::Page;
            } else {
                if (request->arg("pop") != "1") {
                    request->send(400, "application/json", "{\"error\":\"pop must be 1\"}");
                    return;
                }
                cmd = DefaultUI::SettingsUiCmd::Pop;
            }
            uint32_t seq = 0;
            if (!controller->getUI()->queueSettingsUiCommand(cmd, arg, seq)) {
                request->send(409, "application/json", "{\"error\":\"busy\"}");
                return;
            }
            char buf[48];
            snprintf(buf, sizeof(buf), "{\"seq\":%u,\"accepted\":true}", static_cast<unsigned>(seq));
            request->send(200, "application/json", buf);
            return;
        }
        DefaultUI::SettingsUiState st;
        controller->getUI()->settingsUiState(st);
        // action/confirm/locked/repeats/fast_repeats added for the row-widget
        // bead (gm-flw.3): action and confirm are the Fixture category's
        // action/confirm row counters, locked is the locked row's current
        // state, and repeats/fast_repeats are the plain stepper row's
        // LV_EVENT_PRESSED + slow-repeat count and fast-repeat count
        // respectively (see SettingsFixture.cpp's stepRepeats/stepFastRepeats
        // comment for why a press and a slow repeat are counted together).
        char buf[320];
        snprintf(buf, sizeof(buf),
                 "{\"seq\":%u,\"open\":%s,\"depth\":%d,\"category\":%d,\"page\":%d,\"pages\":%d,\"title\":\"%s\","
                 "\"fixture\":{\"enter\":%d,\"commit\":%d,\"draft\":%d,\"action\":%d,\"confirm\":%d,\"locked\":%s,"
                 "\"repeats\":%d,\"fast_repeats\":%d}}",
                 static_cast<unsigned>(st.seq), st.open ? "true" : "false", st.depth, st.category, st.page, st.pages, st.title,
                 st.fixtureEnter, st.fixtureCommit, st.fixtureDraft, st.fixtureAction, st.fixtureConfirm,
                 st.fixtureLocked ? "true" : "false", st.fixtureRepeats, st.fixtureFastRepeats);
        request->send(200, "application/json", buf);
    });
#endif

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
        // Where the animations' lookup tables actually landed: allocHot() is
        // the fixed internal slab, alloc() is PSRAM (BgAnimCommon.h).
        gate["lut_sram_b"] = static_cast<uint32_t>(bganim::g_allocSram);
        gate["lut_psram_b"] = static_cast<uint32_t>(bganim::g_allocPsram);
        gate["hot_used_b"] = static_cast<uint32_t>(bganim::hotUsed());
        gate["hot_peak_b"] = static_cast<uint32_t>(bganim::hotPeak());
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
        gate["hot_slab_b"] = static_cast<uint32_t>(bganim::HOT_SLAB_BYTES);
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
}
