#ifndef GAGGIMATE_SIM

#include "BgAnimCommon.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <math.h>

namespace bganim {

const int16_t *sinLut() {
    static int16_t *lut = nullptr;
    if (lut == nullptr) {
        lut = static_cast<int16_t *>(alloc(SIN_N * sizeof(int16_t)));
        if (lut != nullptr) {
            for (int i = 0; i < SIN_N; i++) {
                lut[i] = static_cast<int16_t>(lroundf(sinf(i * (2.0f * static_cast<float>(M_PI) / SIN_N)) * SIN_AMP));
            }
        }
    }
    return lut;
}

void *alloc(size_t size) {
    void *p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (p == nullptr) {
        p = ps_malloc(size);
        if (p != nullptr) {
            log_w("bganim: %u B in PSRAM (internal SRAM full)", static_cast<unsigned>(size));
        }
    }
    return p;
}

void buildPalette(uint16_t *out, const uint8_t (*keys)[3], int nKeys, uint16_t brightness256) {
    for (int i = 0; i < 256; i++) {
        const float pos = i * (static_cast<float>(nKeys) / 256.0f);
        const int k0 = static_cast<int>(pos) % nKeys;
        const int k1 = (k0 + 1) % nKeys;
        const float f = pos - floorf(pos);
        const uint32_t r = static_cast<uint32_t>((keys[k0][0] + (keys[k1][0] - keys[k0][0]) * f)) * brightness256 >> 8;
        const uint32_t g = static_cast<uint32_t>((keys[k0][1] + (keys[k1][1] - keys[k0][1]) * f)) * brightness256 >> 8;
        const uint32_t b = static_cast<uint32_t>((keys[k0][2] + (keys[k1][2] - keys[k0][2]) * f)) * brightness256 >> 8;
        out[i] = rgb565(static_cast<uint8_t>(r > 255 ? 255 : r), static_cast<uint8_t>(g > 255 ? 255 : g),
                        static_cast<uint8_t>(b > 255 ? 255 : b));
    }
}

} // namespace bganim

#endif // GAGGIMATE_SIM
