---
title: "03-memory-hierarchy: bucket index"
id: 03-memory-hierarchy/readme
schema_version: 1
doc_type: reference
status: review
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, cache, psram, mspi, memory-hierarchy]
---

# 03-memory-hierarchy: bucket index

Cache geometry, internal SRAM, PSRAM, the shared MSPI bus, miss cost and
alignment for the ESP32-S3. The cycle cost model for the core itself lives
in `00-foundations/`; the decision rules built on this bucket's facts live
in `10-synthesis/`.

| File | Topic | Key claims |
|---|---|---|
| [caches-sram-psram-and-the-mspi-bus.md](./caches-sram-psram-and-the-mspi-bus.md) | ICache/DCache geometry, internal SRAM, PSRAM speed, the shared MSPI bus, miss cost, cache coherence with DMA, alignment, the table-budget rule | One ICache and one DCache, shared by both cores, carved from internal SRAM (16 or 32 KB ICache from a 32 KB block; 32 or 64 KB DCache from a 64 KB block); default is 16 KB ICache, 32 KB DCache, 8 ways, 32 B lines. The 16 KB DCache Kconfig setting is still a 32 KB hardware cache with 16 KB pinned by `Cache_Occupy_Addr` and added to the heap. Flash and PSRAM share SPI0 (cache and GDMA) and SPI1 (CPU) and, in octal mode, every pin except chip select. Peak PSRAM pin bandwidth by mode and clock is derived arithmetic, and gives a line-fill floor (about 24 to 96 cycles per line at 240 MHz), not a miss cost; no miss-cycle number is published and none is invented here. The DCache is write-back; GDMA does not see it without `esp_cache_msync`, which enforces line-size alignment. Flash writes disable both caches and PSRAM on both cores. GCC 14.2 models a load with 2-cycle latency against 1 for most arithmetic. The data cache is the working-set budget for a kernel's tables. |
| [cache-control-preload-lock-and-writeback.md](./cache-control-preload-lock-and-writeback.md) | What software can do to the caches: the Xtensa cache instructions this core does not have, and the chip's own cache peripheral (status: draft, wave 4, not yet verified) | The ESP32-S3 configures no Xtensa Cache Option: `XCHAL_DCACHE_SIZE`, `XCHAL_ICACHE_SIZE` and `XCHAL_HAVE_PREFETCH` are 0, and all eighteen cache instructions (`dhwb`, `dpfr`, `dpfl`, `ihi` and the rest) fail to assemble. Preload, lock, writeback and invalidate exist only in the cache peripheral (TRM 4.3.3.3) reached through ROM functions in `rom/cache.h` and `esp_cache_msync`. ESP-IDF 5.5.1 calls `Cache_Start_DCache_Preload` from one driver and `Cache_Lock_Addr` from nowhere. `Cache_Occupy_Addr` at boot is what turns the 32 KB data cache into 16 KB plus 16 KB of reserved SRAM. |

## Not yet covered

- A sustained (not peak theoretical) PSRAM bandwidth figure: Espressif has not published one in the sources checked (datasheet v2.2, the external-RAM and flash/PSRAM-config guides). Worth a `07-our-work/` measurement.
- A published cache-miss cycle cost (flash or PSRAM side): not found in the TRM or the IDF speed guide. `00-foundations/` or a future `07-our-work/` measurement is the place to establish one with the CCOUNT method and the `EXTMEM_*` miss counters this leaf documents.
- Whether the DCache has a write-through mode: no register bit or TRM statement found; flagged `[uncertain]` in the leaf rather than assumed absent.
- Cache preload and lock operations (TRM 4.3.3.3) are named but not worked through as a technique; a kernel-patterns leaf could cover manual preload/lock for a hot table.
- The permission-control boundaries on external RAM (TRM 3.4.10, GDMA access regions) are out of scope here; relevant to a DMA-focused leaf, not this one.
