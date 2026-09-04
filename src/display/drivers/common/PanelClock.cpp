#include "PanelClock.h"

// CONFIG_IDF_TARGET_* lives in sdkconfig.h, which Arduino sources don't get
// implicitly — pull it in before testing the target guard.
#if !defined(GAGGIMATE_SIM) && __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif

#if defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(GAGGIMATE_SIM)

#include <esp_lcd_panel_rgb.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

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
volatile uint32_t g_slips = 0;   // count of frames that completed no refill pass
volatile uint32_t g_refillsAtVsync = 0; // g_refills as of the previous VSYNC
volatile uint32_t g_maxDrift = 0;

// Refill headroom in microseconds: how long before VSYNC the refill pass
// finished copying the frame's last bounce buffer. See PanelClock.h for why
// this is timed rather than counted -- in short, esp_lcd restarts the transfer
// on a single late bounce buffer, and such a frame still completes its pass, so
// g_slips is blind to exactly the events that displace the picture.
volatile uint32_t g_fbcUs = 0; // esp_timer at the last on_frame_buf_complete
volatile uint32_t g_marginLastUs = 0;
volatile uint32_t g_marginMinUs = UINT32_MAX;
volatile uint32_t g_marginMaxUs = 0;
volatile uint32_t g_marginBucket[SCANOUT_MARGIN_BUCKETS] = {0};
volatile uint32_t g_lagThreshUs = 0;

// Last time each activity ran, in esp_timer microseconds truncated to 32 bits.
// Truncation wraps every ~71 minutes; only differences are ever taken, and
// unsigned subtraction gives the right answer across a wrap.
volatile uint32_t g_actUs[SCANOUT_ACT_COUNT] = {0};

// Ring of recent slips. Written only from the VSYNC ISR, read from a task.
constexpr size_t SLIP_LOG_N = 24;
ScanoutSlip g_slipLog[SLIP_LOG_N] = {};
volatile uint32_t g_slipWrite = 0;

// IRAM because these run from the panel's interrupts, which the driver keeps
// alive across a flash operation when its own ISR is IRAM-safe. Two increments
// each; the cost to the frame budget is nil, and putting them in flash would
// turn a diagnostic into a way to crash during an NVS write.
IRAM_ATTR bool onRefillDone(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *) {
    g_refills++;
    g_fbcUs = static_cast<uint32_t>(esp_timer_get_time());
    return false;
}

IRAM_ATTR void logEvent(uint32_t nowUs, uint32_t marginUs) {
    ScanoutSlip &e = g_slipLog[g_slipWrite % SLIP_LOG_N];
    e.frame = g_frames;
    e.tUs = nowUs;
    e.marginUs = marginUs;
    for (int i = 0; i < SCANOUT_ACT_COUNT; i++) {
        // Zero means the source has never run, which would otherwise read as
        // "ran at time zero" and look like a very stale hit.
        e.sinceUs[i] = (g_actUs[i] == 0) ? UINT32_MAX : (nowUs - g_actUs[i]);
    }
    g_slipWrite++;
}

} // namespace
} // namespace panelclock

// Called by the patched esp_lcd RGB driver every time it decides to restart the
// transmission (scripts/patch_esp_lcd_rgb.py). A restart is the moment the panel
// visibly shifts, so this is the event the correlation log exists for: logging
// on refill headroom instead was a proxy, and a proxy that fires on frames the
// driver went on to handle cleanly.
//
// `shortfall` is how many DMA end-of-frame interrupts the frame came up short,
// which converts to displaced scan-out time at one bounce buffer each. It is
// carried in the entry's marginUs field rather than a new one, because the two
// are never both meaningful: an entry logged here has no headroom reading.
extern "C" IRAM_ATTR void gm_rgb_restart_hook(uint32_t shortfall) {
    panelclock::logEvent(static_cast<uint32_t>(esp_timer_get_time()), shortfall);
}

// The RF PHY tracks its PLL against temperature drift once a second, and the
// tracking call's flash fetches occupy the MSPI bus for a ~0.5-1 ms burst:
// long enough to overrun the bounce pool's ~670 us of slack and displace one
// band, once a second, phase-locked (that was the flat 0.7/s resync train the
// slip log dated to x.26-x.33 of every second). The patched timer callback
// (scripts/patch_phy_track_defer.py) offers each tick here first; we park it
// until the next VSYNC and run it at the top of vertical blanking, where the
// scan-out consumes nothing for ~1.3 ms and the pool then still holds its
// full slack. Cadence stays one second, quantized to a 23 ms frame, which
// thermal drift cannot see.
//
// The heartbeat check makes the takeover self-disarming: if the panel is not
// scanning (standby teardown, pre-init, OTA rebuild), defer() sees a stale
// heartbeat, declines the tick, and the PHY timer runs it inline exactly as
// stock. PLL tracking is never starved by a stopped display.
namespace {
TaskHandle_t g_phyTrackTask = nullptr;
volatile bool g_phyTrackPending = false;
volatile uint32_t g_phyTrackHeartbeatUs = 0;
volatile uint32_t g_phyTrackDeferred = 0; // ticks run in the blanking window
constexpr uint32_t PHY_TRACK_HEARTBEAT_STALE_US = 100000;

extern "C" void gm_phy_track_pll_run(void);

void phyTrackTask(void *) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // Marked so the slip log can convict or acquit these ticks: a slip
        // with a tiny phy_us is a deferred run that still overran the
        // blanking window; a huge one rules this path out.
        panelclock::scanoutMark(panelclock::SCANOUT_ACT_PHY);
        gm_phy_track_pll_run();
        g_phyTrackDeferred = g_phyTrackDeferred + 1;
    }
}
} // namespace

extern "C" bool gm_phy_track_defer(void) {
    // Runs on the esp_timer task, once a second. Volatile reads only; the
    // pending flag is consumed in the VSYNC ISR.
    const uint32_t now = static_cast<uint32_t>(esp_timer_get_time());
    if (g_phyTrackTask == nullptr || now - g_phyTrackHeartbeatUs > PHY_TRACK_HEARTBEAT_STALE_US) {
        // A tick deferred just before the panel stopped has no VSYNC left to
        // release it. Drop it here rather than let it replay out of cadence
        // when the panel comes back; the inline run below covers this period.
        g_phyTrackPending = false;
        return false;
    }
    g_phyTrackPending = true;
    return true;
}

namespace panelclock {

uint32_t phyTrackDeferred() { return g_phyTrackDeferred; }

namespace {

IRAM_ATTR bool onVsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *) {
    g_frames++;
    // Count frames that completed no refill pass, not new high-water marks of
    // drift.
    //
    // This used to ratchet on g_maxDrift, which meant each drift level could
    // only ever be counted ONCE: miss a refill, recover, miss again, and the
    // second miss found drift == g_maxDrift and was not counted. The counter
    // saturated after the first few events and then read flat forever. It
    // reported 3 underruns in 160 s on a panel that a human watching it saw
    // displacing several times a second, and every "clean" measurement taken
    // against it was worthless.
    //
    // In steady state exactly one refill pass completes per displayed frame, so
    // a frame that completes none is an underrun, and it counts every time it
    // happens. g_maxDrift is kept because the correlation log is keyed on it
    // and a growing drift is still worth seeing.
    const uint32_t drift = g_frames - g_refills;
    if (drift > g_maxDrift) {
        g_maxDrift = drift;
    }
    const uint32_t refillsNow = g_refills;
    const uint32_t refillsThisFrame = refillsNow - g_refillsAtVsync;
    g_refillsAtVsync = refillsNow;
    const uint32_t nowUs = static_cast<uint32_t>(esp_timer_get_time());
    // Only meaningful when this frame's pass actually completed. On a frame
    // that completed none, g_fbcUs is left over from an earlier frame and the
    // difference would read as an enormous margin, which is the opposite of
    // the truth; g_slips is the counter for that case.
    if (g_frames > 4 && refillsThisFrame >= 1) {
        const uint32_t margin = nowUs - g_fbcUs;
        g_marginLastUs = margin;
        if (margin < g_marginMinUs) {
            g_marginMinUs = margin;
        }
        if (margin > g_marginMaxUs) {
            g_marginMaxUs = margin;
        }
        // 128 us buckets so the index is a shift, which keeps this ISR cheap.
        size_t b = margin >> 7;
        if (b >= SCANOUT_MARGIN_BUCKETS) {
            b = SCANOUT_MARGIN_BUCKETS - 1;
        }
        g_marginBucket[b]++;
        if (g_lagThreshUs != 0 && margin < g_lagThreshUs) {
            logEvent(nowUs, margin);
        }
    }
    // The first frames after init legitimately run ahead of the first completed
    // refill pass, which is a phase offset and not a fault.
    if (g_frames > 4 && refillsThisFrame == 0) {
        g_slips++;
        // marginUs 0 marks a total refill failure: no pass completed at all, so
        // there is no headroom to report.
        logEvent(nowUs, 0);
    }
    // PHY PLL-track deferral (see gm_phy_track_defer above): mark the panel
    // alive, and if a tick is parked, release the runner now. VSYNC_END puts
    // the beam at the start of the vertical back porch: ~450 us of no
    // consumption, then a freshly topped pool's ~670 us of slack, which is
    // the roomiest window this frame will ever offer a ~0.5 ms bus hold.
    g_phyTrackHeartbeatUs = nowUs;
    if (g_phyTrackPending && g_phyTrackTask != nullptr) {
        g_phyTrackPending = false;
        BaseType_t hpw = pdFALSE;
        vTaskNotifyGiveFromISR(g_phyTrackTask, &hpw);
        return hpw == pdTRUE;
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
    g_refillsAtVsync = 0;
    g_fbcUs = 0;
    g_marginLastUs = 0;
    g_marginMinUs = UINT32_MAX;
    g_marginMaxUs = 0;
    for (size_t i = 0; i < SCANOUT_MARGIN_BUCKETS; i++) {
        g_marginBucket[i] = 0;
    }
    // Field-by-field rather than a designated initialiser: on_frame_buf_complete
    // shares an anonymous union with a deprecated alias, and naming a union
    // member in a braced list makes the initialiser order-dependent in a way
    // g++ rejects outright.
    esp_lcd_rgb_panel_event_callbacks_t cbs = {};
    cbs.on_vsync = onVsync;
    cbs.on_frame_buf_complete = onRefillDone;
    esp_lcd_rgb_panel_register_event_callbacks(g_panel, &cbs, nullptr);
    // The PHY-track runner outlives panel rebuilds (a stopped panel just
    // stops feeding it); create it once. Core 0, where the esp_timer task
    // would have run the tick anyway; priority above the radio housekeeping
    // it replaces so the VSYNC release is not sat on.
    if (g_phyTrackTask == nullptr) {
        // 2.5 KB: the runner used ~1 KB of its previous 3 KB at the high-water mark.
        xTaskCreatePinnedToCore(phyTrackTask, "gm_phy_trk", 2560, nullptr, 19, &g_phyTrackTask, 0);
    }
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

void scanoutMark(int which) {
    if (which >= 0 && which < SCANOUT_ACT_COUNT) {
        // esp_timer_get_time is IRAM-safe, so this stays callable from the
        // paths that run with the cache disabled -- which are exactly the ones
        // most worth correlating against.
        g_actUs[which] = static_cast<uint32_t>(esp_timer_get_time());
    }
}

size_t scanoutSlipLog(ScanoutSlip *out, size_t max) {
    if (out == nullptr || max == 0) {
        return 0;
    }
    const uint32_t w = g_slipWrite;
    const size_t have = (w < SLIP_LOG_N) ? w : SLIP_LOG_N;
    const size_t n = (have < max) ? have : max;
    // Oldest first. A slip landing mid-copy can tear one entry; this is a
    // diagnostic read over seconds, not a synchronisation primitive, and a
    // critical section here would sit in the VSYNC ISR's path.
    for (size_t i = 0; i < n; i++) {
        out[i] = g_slipLog[(w - n + i) % SLIP_LOG_N];
    }
    return n;
}

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

void scanoutMargin(uint32_t *lastUs, uint32_t *minUs, uint32_t *maxUs, uint32_t *buckets) {
    if (lastUs != nullptr) {
        *lastUs = g_marginLastUs;
    }
    if (maxUs != nullptr) {
        *maxUs = g_marginMaxUs;
    }
    if (minUs != nullptr) {
        // Before the first measured frame this is the sentinel, which would
        // print as 4294967295 and read as an absurdly healthy panel.
        *minUs = (g_marginMinUs == UINT32_MAX) ? 0 : g_marginMinUs;
    }
    if (buckets != nullptr) {
        for (size_t i = 0; i < SCANOUT_MARGIN_BUCKETS; i++) {
            buckets[i] = g_marginBucket[i];
        }
    }
}

void setLagThresholdUs(uint32_t us) { g_lagThreshUs = us; }

void scanoutReset() {
    // Order matters a little: clear the derived counters before the phase
    // reference, so the VSYNC ISR cannot land between them and log a slip
    // against a frame count that has already been zeroed.
    g_slips = 0;
    g_maxDrift = 0;
    g_marginLastUs = 0;
    g_marginMinUs = UINT32_MAX;
    g_marginMaxUs = 0;
    for (size_t i = 0; i < SCANOUT_MARGIN_BUCKETS; i++) {
        g_marginBucket[i] = 0;
    }
    g_slipWrite = 0;
    g_frames = 0;
    g_refills = 0;
    g_refillsAtVsync = 0;
    g_fbcUs = 0;
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
    scanoutReset();
}

} // namespace panelclock

#else

namespace panelclock {
void attach(void *, uint32_t) {}
uint32_t phyTrackDeferred() { return 0; }
void detach() {}
uint32_t pclkHzForInit(uint32_t defaultHz) { return defaultHz; }
int currentDiv() { return 0; }
uint32_t bootPclkHz() { return 0; }
bool hasLiveControl() { return false; }
void setDiv(int) {}
void scanoutMark(int) {}
size_t scanoutSlipLog(ScanoutSlip *, size_t) { return 0; }
void scanoutReset() {}
void setLagThresholdUs(uint32_t) {}
void scanoutMargin(uint32_t *lastUs, uint32_t *minUs, uint32_t *maxUs, uint32_t *buckets) {
    if (lastUs != nullptr) {
        *lastUs = 0;
    }
    if (minUs != nullptr) {
        *minUs = 0;
    }
    if (maxUs != nullptr) {
        *maxUs = 0;
    }
    if (buckets != nullptr) {
        for (size_t i = 0; i < SCANOUT_MARGIN_BUCKETS; i++) {
            buckets[i] = 0;
        }
    }
}
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
