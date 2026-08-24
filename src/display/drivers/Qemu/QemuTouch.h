#ifndef QEMUTOUCH_H
#define QEMUTOUCH_H

#ifdef GAGGIMATE_QEMU

#include <cstdint>

// Host-driven touch injection for the QEMU build.
//
// No touch controller is emulated -- the panel device is a bare framebuffer and
// the GT911/FT3267 sit on an I2C bus QEMU does not model -- and the fork carries
// no input device of its own, so QEMU's own mouse handling has nowhere to
// deliver events. The only host-to-guest channel a stock build offers is a
// chardev, so touch arrives as records on the console UART:
//
//     ESC 'T' <x> ',' <y> ',' <1|0> '\n'
//
// scripts/qemu-touch.py writes those from the host's mouse position over the
// SDL window. The console has no other reader in this firmware, and the ESC
// prefix keeps the records out of the way of anything a human might type.
namespace qemutouch {

// Installs the UART0 receive driver and starts the parser task. Safe to call
// more than once.
void begin();

// Latest injected point. Returns false when nothing is pressed, which includes
// the case where the host bridge went away mid-press.
bool read(int16_t *x, int16_t *y);

} // namespace qemutouch

#endif // GAGGIMATE_QEMU
#endif // QEMUTOUCH_H
