// Host shim for esp_timer.h.
#pragma once

#include <stdint.h>

// arduino_shim.cpp defines this inside its extern "C" block, matching the real
// esp_timer.h, so the declaration has to carry C linkage too.
extern "C" int64_t esp_timer_get_time(void); // microseconds since start
