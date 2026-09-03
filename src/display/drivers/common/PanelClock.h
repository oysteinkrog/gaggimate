#ifndef PANELCLOCK_H
#define PANELCLOCK_H

// Runtime RGB pixel-clock control (ESP32-S3 LCD_CAM peripheral).
//
// The web UI exposes the panel refresh rate as a divider n, meaning "run the
// pixel clock at about 80 MHz / n". That is a request, not a register value:
// the hardware divider is two coupled parts, a group divider off the 160 MHz
// PLL (fractional on IDF 5, with its own numerator/denominator) and a separate
// integer prescale. esp_lcd programs both together from a target frequency and
// caches the result.
//
// This is why retiming must go through the driver rather than the clock
// register. The previous implementation called lcd_ll_set_pixel_clock_prescale
// directly, which writes only the prescale half and leaves the group divider at
// whatever the boot frequency computed — so the resulting clock was not
// 80 MHz / n at all, and the driver's own view of the timing no longer matched
// the hardware. Reported symptom was the top of the framebuffer stretched over
// the whole screen on every setting that actually reached the register.
// esp_lcd_rgb_panel_set_pclk recomputes both halves and defers the register
// work to the next VSYNC, which is the only point at which it is safe. No
// accompanying esp_lcd_rgb_panel_restart is needed; the driver applies a
// pending pclk update from the VSYNC handler on its own.
//
// Where the driver offers no such API (ESP-IDF 4.4), there is nothing safe to
// do at runtime: setDiv() records the request and the panel picks it up at its
// next init, which is what pclkHzForInit() is for. hasLiveControl() reports
// which of the two behaviours is in effect so the UI can say whether a restart
// is needed.

#include <stddef.h>
#include <stdint.h>

namespace panelclock {

// Hands the RGB panel handle to this module. Called once from the display
// driver after esp_lcd_panel_init succeeds. bootPclkHz is the frequency the
// panel was configured with, used to resolve setDiv(0).
// The handle is typed void* so this header stays includable from translation
// units that do not pull in esp_lcd.
void attach(void *panelHandle, uint32_t bootPclkHz);

// Drops the handle before the panel is deleted (display OTA stops the panel).
// The selected divider is remembered, so a later attach() re-applies it.
void detach();

// Requests a pixel clock of about 80 MHz / n. n == 0 restores the frequency the
// panel booted with. Values are clamped to [2, 16]. Safe to call from any task.
void setDiv(int n);

// Fastest pixel clock the stored setting may select. The bounce refill copies
// the PSRAM framebuffer over the MSPI bus it shares with flash; whenever core 0
// runs flash-resident code (the BLE host on every controller message, WiFi)
// that bus is shared and the copy runs at ~28 MB/s. At n=5 (16 MHz, 61 Hz) the
// panel consumes 28 MB/s, so the refill falls behind by a few buffers, stays
// behind for milliseconds, and laps the 8-buffer pool: one band of the picture
// displaced per lap (measured 2026-09-03 on the bench machine with the
// controller connected, ~18 slow copies/s and 0.03 laps/s in standby; the
// user's stored setting was 5). At n=6 (13.3 MHz, 51 Hz) consumption is
// 24 MB/s and the same soak showed no copy over 190 us against 632 us of pool
// slack, zero laps. A stored divider below this is applied as this; the debug
// endpoint is not clamped so the fast clock stays available for measurement.
constexpr int MIN_USER_DIV = 6;
inline int clampUserDiv(int n) { return (n != 0 && n < MIN_USER_DIV) ? MIN_USER_DIV : n; }

// Frequency the panel should be created with: the requested divider as a
// frequency when one has been chosen, otherwise defaultHz unchanged. The panel
// driver calls this instead of using its build-time constant directly, so a
// stored setting is honoured from the first frame. This is the only path that
// applies the setting at all on ESP-IDF 4.4, and it needs the divider to be
// seeded (setDiv) before the panel is created — Controller::setup does that
// from NVS, which is loaded before setupPanel().
uint32_t pclkHzForInit(uint32_t defaultHz);

// The divider last requested — not a register read-back, and after detach() not
// something any hardware is currently doing.
int currentDiv();

// Frequency the panel was created with, 0 before attach().
uint32_t bootPclkHz();

// True when setDiv takes effect without a restart.
bool hasLiveControl();

// Scan-out counters.
//
// The RGB peripheral generates HSYNC and VSYNC from its own counters and does
// not stall when its FIFO runs dry, so losing the race to refill a bounce
// buffer does not stop the scan. esp_lcd notices at the next VSYNC and restarts
// the transfer, and because that restart resets the FIFO and the DMA but not
// the peripheral's line and pixel counters, the stream resumes from the top of
// the buffer while the beam is already partway down the frame. Everything below
// that point is displaced downward by however far the beam had travelled: whole
// blocks of lines, no colour corruption. The driver's own author documents it
// at esp_lcd_panel_rgb.c:1093 as "the display will shift".
//
// `slips` counts frames that completed NO refill pass. Read it as a count of
// total refill failures, not as the underrun rate, because the restart trigger
// is far weaker than that: esp_lcd restarts whenever bb_eof_count <
// expect_eof_count at VSYNC, so ONE late bounce buffer out of sixty is enough.
// Such a frame still finishes its pass and still fires on_frame_buf_complete,
// so a completion counter cannot see it. Measured against a panel visibly
// shifting several times a second, `slips` read 91 in 33 minutes.
//
// Any argument may be null. All three are zero before attach().
void scanoutStats(uint32_t *frames, uint32_t *refills, uint32_t *slips);

// PHY PLL-track ticks run in the vertical blanking window instead of at the
// timer's own moment (see PanelClock.cpp). Zero on a live radio means the
// deferral is not engaging and the once-a-second displaced band is back.
uint32_t phyTrackDeferred();

// Refill headroom, which is the measurement `slips` cannot make.
//
// There is no public per-EOF callback to replicate esp_lcd's own condition with
// -- on_bounce_empty exists but the driver only calls it when it owns no frame
// buffer of its own (esp_lcd_panel_rgb.c:880). So headroom is timed instead of
// counted. on_frame_buf_complete fires when the refill has copied the frame's
// last bounce buffer, at which point the DMA still has two bounce buffers plus
// the vertical front porch left to transmit; the gap from there to VSYNC is
// how much slack the refill had. A healthy frame reports a few hundred
// microseconds. As contention grows the margin collapses toward zero, and an
// underrun is the moment it crosses -- so the bucket histogram shows the fault
// approaching, at a resolution a binary counter never had.
//
// The histogram is the raw margin distribution: 32 buckets of 128 us, the last
// one holding everything at or above 3968 us. Deliberately absolute and
// deliberately not normalised on-device.
//
// Two earlier shapes were both wrong. Fixed edges in microseconds chosen at one
// pixel clock say nothing at another, because the healthy margin is two bounce
// buffers plus the front porch and so scales with the clock. Normalising
// against the running maximum then failed for a subtler reason: margin grows
// when the VSYNC ISR is late just as it shrinks when the refill is late, so the
// maximum is an outlier statistic that ratchets up and eventually marks every
// healthy frame as lagging. It drifted 1257 -> 1845 us mid-measurement and
// turned a 20 percent reading into 89 percent.
//
// The distribution answers it without a baseline having to be chosen in
// advance: healthy frames pile up in one mode, and lagging frames fall in
// discrete steps of one bounce-buffer time below it, because the refill can
// only ever be a whole number of bounce buffers behind. Read the mode, count
// the tail below it. `slips` counts only total refill failure and will read
// near zero throughout.
//
// Any argument may be null.
enum { SCANOUT_MARGIN_BUCKETS = 32, SCANOUT_MARGIN_BUCKET_US = 128 };
void scanoutMargin(uint32_t *lastUs, uint32_t *minUs, uint32_t *maxUs, uint32_t *buckets);

// Zeroes every scan-out counter, including the margin calibration.
//
// Comparing two configurations means comparing rates, and counters that have
// been accumulating since boot bury a change in whatever came before it.
// setDiv() calls this on its own: the healthy margin scales with the pixel
// clock, so a maximum established at one clock is the wrong yardstick at the
// next, and at a faster clock it would mark every frame as lagging.
void scanoutReset();

// Correlation log for those slips.
//
// Knowing the rate is not enough to fix it: several plausible causes all steal
// more than the refill's 162 us budget, and they are told apart by WHEN they
// do it, not by how much. A once-per-second overlay snapshot leaves slips
// clustered near a 1 s cadence; a flash write leaves a tight burst, because the
// cache is off for the whole write and the ISR is masked throughout; radio
// coexistence arbitration leaves them scattered. So each slip records how long
// it had been since each suspect last ran, and one dump separates them.
//
// Sources call mark() as they run. The cost is one timestamp store.
enum ScanoutActivity {
    SCANOUT_ACT_OVERLAY = 0,  // LVGL widget snapshot into the overlay buffer
    SCANOUT_ACT_FLASH = 1,    // NVS / LittleFS write, which masks the LCD ISR
    SCANOUT_ACT_BANDPUSH = 2, // animation band pushed into the framebuffer
    SCANOUT_ACT_PRESENT = 3,  // whole-framebuffer cache invalidate before a flip
    SCANOUT_ACT_PHY = 4,      // deferred RF PLL-track tick run at VSYNC (PanelClock.cpp)
    SCANOUT_ACT_COUNT = 5,
};

void scanoutMark(int which);

struct ScanoutSlip {
    uint32_t frame;                         // frame counter when it happened
    uint32_t tUs;                           // esp_timer microseconds, low 32 bits
    uint32_t marginUs;                      // refill headroom, 0 for a total failure
    uint32_t sinceUs[SCANOUT_ACT_COUNT];    // since each source last marked
};

// Also log a correlation entry for every frame whose refill headroom fell below
// `us`, not just for total refill failures. 0 disables it.
//
// The log was keyed on `slips` alone, and slips are rare -- single digits over
// runs where the panel displaced thousands of times -- so it almost never had a
// sample of the event actually being chased. Lagging frames are common enough
// (10 to 14 percent at 13.3 MHz) that a few seconds of logging says which
// source was running when the refill fell behind. Pick the threshold from the
// margin histogram: one bucket below its healthy mode.
void setLagThresholdUs(uint32_t us);

// Copies out up to `max` of the most recent slips, oldest first, and returns
// how many were written.
size_t scanoutSlipLog(ScanoutSlip *out, size_t max);

} // namespace panelclock

#endif // PANELCLOCK_H
