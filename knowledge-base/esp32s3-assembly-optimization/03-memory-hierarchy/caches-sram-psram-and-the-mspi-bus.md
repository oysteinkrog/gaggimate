---
title: Caches, internal SRAM, PSRAM and the shared MSPI bus
id: 03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus
schema_version: 1
doc_type: reference
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, cache, psram, mspi, memory-hierarchy, dma]
confidence: medium
---

# Caches, internal SRAM, PSRAM and the shared MSPI bus

A hot loop on the ESP32-S3 is bounded far more often by where its operands
live than by how many instructions it runs. This file states the geometry
of the two caches, what they front, how the external memory bus is shared,
and what each memory class costs. The decision rules built on it live in
`10-synthesis/`, and the cycle model for the core itself lives in
`00-foundations/`.

## 1. The two caches

The ESP32-S3 has one instruction cache and one data cache. Both are shared
by the two CPU cores.[^trm-cache] There is no second level.

Both caches are carved out of the same 512 KB of internal SRAM that the
application otherwise uses for code and data. The TRM describes three
internal SRAM blocks: Internal SRAM 0 is 32 KB on the instruction bus and
supplies the ICache, Internal SRAM 1 is 416 KB, and Internal SRAM 2 is
64 KB on the data bus and supplies the DCache.[^trm-sram] Memory taken by a
cache cannot be addressed by the CPU, and the TRM says so directly: "The
space used as DCache cannot be accessed by the CPU, while the remaining
space can still be accessed by the CPU."[^trm-sram] A bigger cache is
literally paid for in heap.

### 1.1 Configuration table

Sizes, ways and line sizes are set at startup from Kconfig. The hardware
limits come from the TRM; the option names and defaults come from the IDF
Kconfig file for the target.[^trm-cache][^kconfig-cache]

| Property | Hardware choices | Kconfig option | IDF 5.5.1 default |
|---|---|---|---|
| ICache size | 16 KB or 32 KB | `CONFIG_ESP32S3_INSTRUCTION_CACHE_SIZE` (via `..._16KB` / `..._32KB`) | 16 KB |
| ICache ways | 4 or 8 | `CONFIG_ESP32S3_ICACHE_ASSOCIATED_WAYS` | 8 |
| ICache line | 16 B or 32 B; 16 B not allowed with a 32 KB ICache | `CONFIG_ESP32S3_INSTRUCTION_CACHE_LINE_SIZE` | 32 B |
| DCache size | 32 KB or 64 KB in hardware; IDF also offers a 16 KB setting (see 1.2) | `CONFIG_ESP32S3_DATA_CACHE_SIZE` | 32 KB |
| DCache ways | 4 or 8 | `CONFIG_ESP32S3_DCACHE_ASSOCIATED_WAYS` | 8 |
| DCache line | 16 B, 32 B or 64 B; 16 B not allowed with a 64 KB DCache | `CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE` | 32 B |

The TRM states the two exclusions in one sentence each: "When an ICache is
configured to 32 KB, its block cannot be 16 B" and "When a DCache is
configured to 64 KB, its block cannot be 16 B."[^trm-cache]

At the default geometry, 8 ways and a 32 B line, a 32 KB DCache holds 1024
lines in 128 sets. Two addresses 4 KB apart therefore land in the same set,
and nine such addresses in one loop will thrash it. [obvious] from the
geometry above.

### 1.2 What the sizes cost in heap

The IDF heap layout for this target makes the trade concrete.[^memlayout]

- With a 16 KB ICache, the other 16 KB of Internal SRAM 0 at 0x40374000 is
  added to the heap as IRAM, but only when memory protection is
  disabled.[^memlayout]
- With a 32 KB DCache, the other 32 KB of Internal SRAM 2 at 0x3FCF0000 is
  added to the heap as DMA-capable DRAM. With a 64 KB DCache it is
  not.[^memlayout]
- The 16 KB DCache setting is not a hardware size. IDF still configures a
  32 KB cache and then calls `Cache_Occupy_Addr(SOC_DROM_LOW, 0x4000)` at
  startup, which pins 16 KB of cache lines to a 16 KB window of external
  address space, and adds that window at 0x3C000000 to the heap as
  DMA-capable DRAM.[^cpustart][^memlayout] The effect is 16 KB of
  cache-resident, internal-speed RAM in exchange for half the data cache.

## 2. What the caches front, and the bus they share

The CPU reaches external flash and external RAM only through the caches.
The MMU maps a bus address to a physical address in the external
part.[^trm-extmap] Up to 32 MB of instruction bus space is mapped through
the ICache at 0x4200_0000, and up to 32 MB of data bus space through the
DCache at 0x3C00_0000.[^trm-extmap] Constant data compiled to flash and
code executed from flash both arrive through this path. So does every read
and write of PSRAM.

Flash and PSRAM are not separate ports. The datasheet assigns SPI0 to the
cache and the GDMA controller, and SPI1 to the CPU, and says both are for
"in-package or off-package flash/PSRAM".[^ds-spi] The pin table settles the
rest: in octal mode, flash and PSRAM share CLK, DQ0 through DQ7 and
DQS/DM, and are separated only by chip select, CS0 for flash and CS1 for
PSRAM.[^ds-pins] The IDF flash and PSRAM configuration guide adds that
"Flash and PSRAM share the same internal clock", which is why the two are
constrained to compatible speed groups.[^idf-fpcfg]

Two consequences for a hot loop:

1. **A flash miss and a PSRAM access contend.** The TRM says that "When
   ICache and DCache initiate requests on the external memory
   simultaneously, the arbiter determines which gets the access to the
   external memory first."[^trm-cache] Code that runs from flash therefore
   steals bus time from data that lives in PSRAM, and the other way round.
   Moving a hot function to IRAM removes its instruction fetches from the
   bus entirely, which helps the data side as well as the code side.
2. **The second core contends too.** Both cores share both caches and the
   arbiter behind them.[^trm-cache] A measurement taken with the other core
   idle is not the number the loop will see in production.

The IDF external RAM guide states the sharing in its own words: "External
RAM uses the same cache region as the external flash", and warns that "The
bandwidth that DMA accesses external RAM is very limited, especially when
the core is trying to access the external RAM at the same time."[^idf-extram]

## 3. PSRAM speed and the bandwidth that follows

PSRAM is either quad or octal, selected by `CONFIG_SPIRAM_MODE_QUAD` or
`CONFIG_SPIRAM_MODE_OCT`, and clocked at 40, 80 or 120 MHz through
`CONFIG_SPIRAM_SPEED`. The IDF default is quad at 40 MHz.[^kconfig-psram]
Octal PSRAM runs DDR in the supported combination tables; quad rows in
those tables carry no DDR label.[^idf-fpcfg] The datasheet lists 120 MHz as
the maximum for 8-line SDR and DDR modes,[^ds-spi] while Table 5-12 gives
80 MHz as the maximum clock for the in-package PSRAM itself.[^ds-psram] IDF
marks octal 120 MHz experimental and says accesses "will crash randomly"
after a temperature swing of roughly 20 degrees Celsius.[^kconfig-psram]

Peak data-pin bandwidth is `lines / 8 x clock x edges` bytes per second.
With MB meaning 10^6 bytes:

| Mode | Lines | Edges per clock | 40 MHz | 80 MHz | 120 MHz |
|---|---|---|---|---|---|
| Quad, SDR | 4 | 1 | 20 MB/s | 40 MB/s | 60 MB/s |
| Octal, DDR | 8 | 2 | 80 MB/s | 160 MB/s | 240 MB/s |

These are ceilings on the data phase only. Every burst also pays a command,
an address and the read latency the PSRAM device requires, none of which
appear in the arithmetic, so sustained throughput is lower. Espressif
publishes no sustained figure that this file could verify. [uncertain]

The same arithmetic gives a floor for one cache line fill, which is the
smallest unit a miss can cost. At 240 MHz the core cycle is 4.167 ns.

| Line | Octal DDR 80 MHz (160 MB/s) | Quad SDR 80 MHz (40 MB/s) |
|---|---|---|
| 16 B | 100 ns, about 24 cycles | 400 ns, about 96 cycles |
| 32 B | 200 ns, about 48 cycles | 800 ns, about 192 cycles |
| 64 B | 400 ns, about 96 cycles | 1600 ns, about 384 cycles |

Read these as lower bounds on the data phase, not as miss costs. They are
still enough to settle design questions: at the default 32 B line, a
gather that misses on every pixel cannot beat about 48 cycles per pixel on
octal PSRAM, whatever the loop body does.

## 4. Miss cost, and how to measure it

Espressif documents the existence of the cost, not its size. The IDF speed
optimization guide says only that executing from flash causes "the CPU to
have to wait on a 'cache miss' while the next instructions are loaded from
flash", and that code copied into IRAM "always execute[s] at full
speed".[^idf-speed] No published cycle range for a miss on this part was
found. [uncertain]

Two instruments give the number on the device.

**Hardware counters.** The external memory block has counters for accesses
and misses, and it splits the data side by target. The register header
names `EXTMEM_IBUS_ACS_CNT_REG` and `EXTMEM_IBUS_ACS_MISS_CNT_REG` for the
instruction bus, and `EXTMEM_DBUS_ACS_CNT_REG` with
`EXTMEM_DBUS_ACS_FLASH_MISS_CNT_REG` and
`EXTMEM_DBUS_ACS_SPIRAM_MISS_CNT_REG` for the data bus. The pair is cleared
through `EXTMEM_CACHE_ACS_CNT_CLR_REG`, and counter overflow can raise an
interrupt.[^extmem] Because flash misses and PSRAM misses are counted
separately, these registers answer "which side of the bus is the loop
paying for" directly.

**CCOUNT.** The core's cycle counter is read with `RSR CCOUNT`, wrapped by
IDF as `esp_cpu_get_cycle_count()`.[^ccount][^espcpu] The method is
differential: run the same loop over a working set that fits the cache and
over one that does not, take the minimum of n repetitions each to remove
preemption, and divide the difference by the miss count the counters
report. The loop body must be identical in both halves, or the compiler's
different schedule is what you measured. See `05-measurement/` for the
timing discipline this depends on.

## 5. The data cache is write-back, and DMA does not see it

The TRM describes the DCache as tracking dirty blocks with dirty bits, and
lists a write-back operation that clears them and pushes new data to
external memory. Clean, invalidate, preload and lock are the other
operations. Write-back and clean exist only on the DCache.[^trm-cacheops]
That is a write-back cache. No write-through mode bit was found in the
register header, and the TRM chapter does not mention one. [uncertain]

The consequence is a correctness rule, not a speed rule. GDMA reaches
external RAM through the same address range the DCache uses, and the TRM
states plainly: "When DCache and GDMA access the external memory
simultaneously, the software needs to make sure the data is
consistent."[^trm-gdma] GDMA also cannot touch internal memory that a cache
occupies.[^trm-gdma]

In IDF 5.x the synchronisation API is `esp_cache_msync(addr, size, flags)`.
Cache to memory, the default direction, writes dirty lines back before a
DMA engine reads the buffer. Memory to cache invalidates so the CPU sees
what DMA wrote.[^espcache] Two constraints matter in practice:

- Address and size must both be multiples of the cache line size unless
  `ESP_CACHE_MSYNC_FLAG_UNALIGNED` is set, and that flag is rejected in the
  memory-to-cache direction.[^msync-src] Query the required alignment with
  `esp_cache_get_alignment()` rather than assuming 32.[^msync-src]
- The header says not to call it during flash operations, unless execute in
  place from PSRAM is enabled.[^espcache]

The ROM also exports `Cache_Invalidate_Addr` and `rom_Cache_WriteBack_Addr`
directly.[^romld] Prefer `esp_cache_msync`, which the header documents as
cache safe and thread safe.[^espcache]

## 6. The cache-disabled window during flash writes

Writing or erasing flash takes the caches away from both cores. IDF states
that with `CONFIG_SPI_FLASH_AUTO_SUSPEND` disabled, "the caches must be
disabled while reading/writing/erasing operations", because "the SPI0/1 bus
is shared between the instruction & data cache (for firmware execution) and
the SPI1 peripheral".[^idf-concurrency] During that window "all CPUs should
always execute code and access data from internal RAM", the other core
spins in a busy loop, and interrupt handlers not registered with
`ESP_INTR_FLAG_IRAM` do not run.[^idf-concurrency]

PSRAM goes with it. The external RAM guide says that "when flash cache is
disabled (for example, if the flash is being written to), the external RAM
also becomes inaccessible", and that touching it then raises "an illegal
cache access exception".[^idf-extram]

For a kernel this means: anything that can run inside the window must be
`IRAM_ATTR` code reading `DRAM_ATTR` data, and any table it touches must be
internal. A kernel whose tables live in PSRAM is not callable from an
IRAM-safe interrupt handler, no matter how the code is placed.

## 7. Internal SRAM, IRAM, and the load-use interlock

Internal SRAM is not cached and not on the MSPI bus. The core reaches it
directly over the data or instruction bus.[^trm-buses] There is no miss to
pay and no arbitration with flash.

There is still a pipeline cost on a dependent use. GCC 14.2's Xtensa
scheduler models a memory load with a latency of two cycles, against one
for most arithmetic:

```
(define_insn_reservation "xtensa_memory" 2
			 (eq_attr "type" "load,fload")
			 "nothing")
```

so an instruction that consumes a load result in the very next slot stalls
one cycle in the model.[^gccmd] That is the compiler's model of the
pipeline, versioned to GCC 14.2, not a TRM statement about the LX7
pipeline. The instruction scheduling consequence, which is to separate a
load from its use, belongs to `00-foundations/` and `06-kernel-patterns/`.

Placement follows from section 2. Code in IRAM is fetched from internal
SRAM and never misses; code in flash is fetched through the ICache and
competes for the bus with every PSRAM access.[^idf-speed] IRAM is the
scarcer resource, so the placement decision is per function, not per file.

## 8. Alignment

Three separate alignment rules apply, and they are easy to confuse.

- **CPU data access.** The TRM says the CPU can access data on the data bus
  "using single-byte, double-byte, 4-byte and 16-byte alignment", and on
  the instruction bus "only in 4-byte aligned manner; non-aligned data
  access will cause a CPU exception."[^trm-buses] The 16-byte form is the
  128-bit vector access. A 16-byte load whose address is not 16-byte
  aligned is not a slow access on this part, it is a different access; see
  `02-pie-vector/` for what the vector load and store instructions do with
  the low address bits.
- **Cache lines.** A 16-byte access that straddles two 32-byte lines can
  miss twice instead of once. Aligning a table to the line size, and
  padding a per-row stride to a multiple of it, converts a two-miss access
  into a one-miss access. [obvious] from the geometry in section 1.
- **DMA descriptors.** For internal RAM with burst mode on, size and buffer
  address in receive descriptors must be word aligned; without burst mode
  nothing needs alignment.[^trm-gdma-align] For external RAM, GDMA sends
  only in burst mode, the block size is 16, 32 or 64 bytes, and size and
  buffer address in receive descriptors must be aligned to that block
  size.[^trm-gdma-ext]

## 9. The data cache is the table budget

The practical rule that falls out of all of the above: the data cache is
the working-set budget for a kernel's tables, and a table in PSRAM behaves
like a table in internal SRAM only while the whole working set fits.

At the IDF default the DCache is 32 KB with 32-byte lines and 8 ways. A
kernel that sweeps a 16 KB lookup table once per row, with nothing else
competing, will mostly hit. The same kernel with two such tables, or with a
second task touching PSRAM between rows, exceeds the cache and pays the
fill cost from section 3 on a large fraction of accesses. The cache does
not degrade gracefully across that boundary, because the cost of a hit and
the cost of a miss differ by roughly two orders of magnitude.

So table placement is a source-level decision. Small tables read once per
output element belong in internal SRAM, where the cost is fixed. Large,
sequentially swept data belongs in PSRAM, where the line fill is amortised
over 16, 32 or 64 bytes of useful data.

## 10. Memory classes and what they cost

| Class | Where it lives | Reached how | Expected access cost | Source |
|---|---|---|---|---|
| Registers | Core | Directly | 0 extra cycles | [obvious] |
| Internal SRAM, data (DRAM) | Internal SRAM 1 and 2 | Data bus, uncached | One access, with a one-cycle interlock if the next instruction uses the result | [^gccmd] for the model, [^trm-buses] for the path |
| Internal SRAM, code (IRAM) | Internal SRAM 0 and 1 | Instruction bus, uncached | No fetch miss | [^idf-speed] |
| Flash code (IROM), cached hit | External flash | ICache | Near IRAM | [^idf-speed] |
| Flash code, miss | External flash | ICache fill over MSPI | Line fill, floor in section 3; full cost unpublished | [uncertain], [^idf-speed] |
| Flash const data (DROM) | External flash | DCache, read only | Same as PSRAM read, but read only | [^trm-extmap] |
| PSRAM, cached hit | External RAM | DCache | Near internal SRAM | [^trm-cache] |
| PSRAM, miss | External RAM | DCache fill over MSPI | Line fill, floor in section 3; contends with flash fetches and GDMA | [uncertain], [^trm-cache], [^idf-extram] |
| RTC FAST and RTC SLOW | 8 KB each | Data and instruction bus | Not a hot-loop class; sized for deep sleep | [^trm-sram] |
| Any class, cache disabled | Flash and PSRAM both | Unreachable | Exception on access | [^idf-extram], [^idf-concurrency] |

The two [uncertain] rows are the ones worth measuring on the specific
board, with the counters and the CCOUNT method in section 4, because they
depend on the PSRAM mode, the clock, the line size and how busy the other
core is.

## Footnotes

[^trm-cache]: Espressif, *ESP32-S3 Technical Reference Manual*, Version 1.8, Section 4.3.3.2 "Cache", pages 405 to 406. https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf
[^trm-sram]: Espressif, *ESP32-S3 Technical Reference Manual*, Version 1.8, Section 4.3.2 "Internal Memory", entries 3 to 7, pages 403 to 404.
[^trm-extmap]: Espressif, *ESP32-S3 Technical Reference Manual*, Version 1.8, Section 4.3.3.1 "External Memory Address Mapping" and Table 4.3-2, pages 404 to 405.
[^trm-cacheops]: Espressif, *ESP32-S3 Technical Reference Manual*, Version 1.8, Section 4.3.3.3 "Cache Operations", pages 405 to 406.
[^trm-gdma]: Espressif, *ESP32-S3 Technical Reference Manual*, Version 1.8, Section 4.3.4 "GDMA Address Space", pages 406 to 407.
[^trm-gdma-align]: Espressif, *ESP32-S3 Technical Reference Manual*, Version 1.8, Table 3.4-2 "Descriptor Field Alignment Requirements for Accessing Internal RAM", page 363.
[^trm-gdma-ext]: Espressif, *ESP32-S3 Technical Reference Manual*, Version 1.8, Section 3.4.9 "Accessing External RAM", Tables 3.4-3 and 3.4-4, pages 363 to 364.
[^trm-buses]: Espressif, *ESP32-S3 Technical Reference Manual*, Version 1.8, Section 4.2 "Address Mapping", page 401.
[^ds-spi]: Espressif, *ESP32-S3 Series Datasheet*, Version 2.2, Section 4.2.1.5 "Serial Peripheral Interface (SPI)". https://documentation.espressif.com/esp32-s3_datasheet_en.pdf
[^ds-pins]: Espressif, *ESP32-S3 Series Datasheet*, Version 2.2, Section 2.6 and Table 2-14 "Pin Mapping Between Chip and Flash or PSRAM", page 31.
[^ds-psram]: Espressif, *ESP32-S3 Series Datasheet*, Version 2.2, Table 5-12 "PSRAM Specifications", page 69.
[^kconfig-cache]: ESP-IDF 5.5.1, `components/esp_system/port/soc/esp32s3/Kconfig.cache` (local install path `~/.platformio/packages/framework-espidf/`, version.txt reports 5.5.1).
[^kconfig-psram]: ESP-IDF 5.5.1, `components/esp_psram/esp32s3/Kconfig.spiram`, the `SPIRAM_MODE` and `SPIRAM_SPEED` choices.
[^memlayout]: ESP-IDF 5.5.1, `components/heap/port/esp32s3/memory_layout.c`, `soc_memory_regions[]`.
[^cpustart]: ESP-IDF 5.5.1, `components/esp_system/port/cpu_start.c`, the `CONFIG_ESP32S3_DATA_CACHE_16KB` block calling `Cache_Occupy_Addr(SOC_DROM_LOW, 0x4000)`.
[^extmem]: ESP-IDF 5.5.1, `components/soc/esp32s3/register/soc/extmem_reg.h`, registers at `DR_REG_EXTMEM_BASE` offsets 0xC4 through 0xD8.
[^ccount]: ESP-IDF 5.5.1, `components/xtensa/include/xt_utils.h`, `xt_utils_get_cycle_count()`.
[^espcpu]: ESP-IDF 5.5.1, `components/esp_hw_support/include/esp_cpu.h`, `esp_cpu_get_cycle_count()`.
[^espcache]: ESP-IDF 5.5.1, `components/esp_mm/include/esp_cache.h`, `esp_cache_msync()` and the `ESP_CACHE_MSYNC_FLAG_*` definitions.
[^msync-src]: ESP-IDF 5.5.1, `components/esp_mm/esp_cache_msync.c`, the line-size alignment check and `esp_cache_get_alignment()`.
[^romld]: ESP-IDF 5.5.1, `components/esp_rom/esp32s3/ld/esp32s3.rom.ld`, `Cache_Invalidate_Addr` and `rom_Cache_WriteBack_Addr`.
[^idf-extram]: Espressif, ESP-IDF v5.5.1 Programming Guide, "Support for External RAM" (esp32s3). https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-guides/external-ram.html
[^idf-fpcfg]: Espressif, ESP-IDF v5.5.1 Programming Guide, "All Supported Flash and PSRAM Modes and Speeds" (esp32s3). https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-guides/flash_psram_config.html
[^idf-speed]: Espressif, ESP-IDF v5.5.1 Programming Guide, "Speed Optimization" (esp32s3). https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-guides/performance/speed.html
[^idf-concurrency]: Espressif, ESP-IDF v5.5.1 Programming Guide, "SPI Flash API, Concurrency Constraints" (esp32s3). https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/peripherals/spi_flash/spi_flash_concurrency.html
[^gccmd]: GCC 14.2.0, `gcc/config/xtensa/xtensa.md`, `define_insn_reservation "xtensa_memory"`. https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-14.2.0/gcc/config/xtensa/xtensa.md
