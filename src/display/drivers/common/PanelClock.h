#ifndef PANELCLOCK_H
#define PANELCLOCK_H

// Runtime RGB pixel-clock control (ESP32-S3 LCD_CAM peripheral).
//
// The bundled IDF 4.4 esp_lcd RGB driver has no post-init pclk API, but the
// divider is a single register: pclk = 80 MHz / n (LCD group clock is the
// 160 MHz PLL pre-divided by 2; the driver floors requested frequencies to
// this grid, so e.g. a 14 MHz build flag actually runs 16 MHz). Poking the
// prescaler between frames lets the refresh rate be a live setting instead
// of an OTA-per-experiment build flag.
namespace panelclock {

// Sets the pclk divider n (pclk = 80 MHz / n). n == 0 restores the divider
// the panel booted with. Values are clamped to [2, 16]. Safe to call while
// scanning out — worst case is one glitched frame.
void setDiv(int n);

// Divider currently programmed in the peripheral (1..64).
int currentDiv();

} // namespace panelclock

#endif // PANELCLOCK_H
