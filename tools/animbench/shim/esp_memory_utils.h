// Host-build shim for tools/animbench: there is no external RAM on the host.
#pragma once

// shim/esp_heap_caps.h routes every allocation to malloc, so no pointer here
// lives in a PSRAM window and the SRAM/PSRAM split the bench prints degenerates
// to "all SRAM" -- which is exactly what it did when isPsram() compared against
// hardcoded ESP32-S3 addresses, so host numbers are unchanged by the switch.
static inline bool esp_ptr_external_ram(const void *) { return false; }
