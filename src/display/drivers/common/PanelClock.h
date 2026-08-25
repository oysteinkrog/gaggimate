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

// Scan-out underrun counters, the direct measure of the fault that shows up on
// the panel as a band of displaced pixels.
//
// The RGB peripheral generates HSYNC and VSYNC from its own counters and does
// not stall when its FIFO runs dry, so losing the race to refill a bounce
// buffer does not stop the scan -- it slides the pixel stream against the sync
// signals, which is what tears the picture. The driver detects this in its
// VSYNC ISR and restarts the transfer, so the damage is bounded to a frame or
// two, but nothing counts how often it happens.
//
// These two callbacks bracket it exactly. on_vsync fires once per displayed
// frame. on_frame_buf_complete fires once per full pass of the bounce refill
// over the framebuffer. In steady state they run 1:1; a frame whose refill fell
// behind never completes its pass, so the counts diverge by one and stay
// diverged. `slips` ratchets on each such divergence, which makes it a count of
// underrun frames since boot rather than an instantaneous phase difference --
// the two callbacks fire at slightly different points in the frame, so their
// raw difference oscillates by one even when nothing is wrong.
//
// Any argument may be null. All three are zero before attach().
void scanoutStats(uint32_t *frames, uint32_t *refills, uint32_t *slips);

} // namespace panelclock

#endif // PANELCLOCK_H
