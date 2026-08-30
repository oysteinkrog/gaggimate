// Host-build shim for tools/animbench: bganim::radiosSettled() polls the WiFi
// driver to know when the radios have finished claiming internal DRAM, which
// has no meaning on the host. Report "initialised" so the allocator behaves
// like the settled device.
#pragma once

typedef int wifi_mode_t;
#ifndef ESP_OK
#define ESP_OK 0
#endif

static inline int esp_wifi_get_mode(wifi_mode_t *mode) {
    if (mode != nullptr) {
        *mode = 0;
    }
    return ESP_OK;
}
