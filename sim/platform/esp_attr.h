// Host shim for ESP-IDF's esp_attr.h. On real hardware these place code/data in
// IRAM, DRAM, or PSRAM-backed BSS; the host build has none of those regions, so
// every attribute is a no-op here.
#pragma once

#define IRAM_ATTR
#define DRAM_ATTR
#define RTC_IRAM_ATTR
#define RTC_DATA_ATTR
#define RTC_RODATA_ATTR
#define RTC_FAST_ATTR
#define RTC_SLOW_ATTR
#define EXT_RAM_ATTR
#define EXT_RAM_BSS_ATTR
#define IRAM_ATTR_LOOP
