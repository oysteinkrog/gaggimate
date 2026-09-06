---
title: "Cache control from software: preload, lock and writeback"
id: 03-memory-hierarchy/cache-control-preload-lock-and-writeback
schema_version: 1
doc_type: reference
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, cache, preload, cache-lock, writeback, esp-idf]
confidence: medium
---

# Cache control from software: preload, lock and writeback

This file answers one question: what can a kernel author actually do to the
ESP32-S3 caches from code, beyond letting them work automatically? There are
two separate answers, and mixing them up is the most common mistake. The
Xtensa instruction set architecture (ISA) defines a family of cache-control
instructions; the ESP32-S3 chip defines a separate cache-management
peripheral with its own preload, lock and writeback operations, reached
through ROM and ESP-IDF functions, not through those instructions. The two
caches' geometry, and what a miss costs, is covered in
[caches-sram-psram-and-the-mspi-bus.md](./caches-sram-psram-and-the-mspi-bus.md);
read that first.

## 1. The Xtensa ISA defines cache-control instructions ESP32-S3 does not have

The Xtensa ISA Reference Manual groups cache instructions under configurable
options, each of which a core either includes or omits:[^isa-general]

- **Prefetch** (`4.5.1.3 Cache Prefetch`): `IPF` moves a line toward the
  instruction cache; `DPFR`, `DPFRO`, `DPFW` and `DPFWO` move a line toward
  the data cache, with the letter indicating whether a following write is
  expected. All are defined so that "any exception that might be raised
  causes the instruction to become a NOP rather than actually raising an
  exception", specifically so they are safe to use speculatively on
  addresses that might not be legal.[^isa-prefetch]
- **Writeback and invalidate**, Instruction Cache Option and Data Cache
  Option (`4.5.2`, `4.5.5`): `IHI` invalidates one line, `III` invalidates by
  index. `DHI` invalidates one data line; `DII` invalidates by index; `DHWB`
  writes back a dirty line and clears its dirty bit, and is defined as a
  no-op "if not IsWriteback"; `DHWBI` writes back then invalidates; `DIWB`
  and `DIWBI` are the index-addressed forms of the same two
  operations.[^isa-dcache]
- **Prefetch and lock**, Instruction/Data Cache Index Lock Option (`4.5.4`,
  `4.5.7`): `IPFL` and `DPFL` fetch a line and mark it non-replaceable;
  `IHU`/`DHU` unlock by hit address, `IIU`/`DIU` unlock by index. The manual
  states plainly that these two instructions "raise an illegal instruction
  exception on implementations that do not support" the corresponding lock
  option.[^isa-lock]

Every one of these instructions carries a "Required Configuration Option" in
its formal definition.[^isa-dhi] They exist only on a core configured with
the matching Cache Option, Prefetch feature, or Cache Index Lock Option at
core-generation time. A core built without the option does not necessarily
execute the mnemonic as a silent no-op; only the plain prefetch instructions
are guaranteed that. The lock forms explicitly raise an illegal instruction
exception, and the plain invalidate/writeback forms document no fallback
behaviour at all when the owning option is absent.

The ESP-IDF Xtensa core configuration header for this target reports the
Data Cache Option itself as absent, not merely one feature under it:

```
#define XCHAL_ICACHE_SIZE           0   /* I-cache size in bytes or 0 */
#define XCHAL_DCACHE_SIZE           0   /* D-cache size in bytes or 0 */
#define XCHAL_DCACHE_IS_WRITEBACK   0   /* writeback feature */
#define XCHAL_HAVE_PREFETCH         0   /* PREFCTL register */
```

[^core-isa] (`XCHAL_HAVE_CACHE_BLOCKOPS`, `XCHAL_ICACHE_LINE_LOCKABLE` and
`XCHAL_DCACHE_LINE_LOCKABLE` are all 0 too.) The zero size is the tell: this
is the core's own view of whether it has an architectural Data Cache Option
or Instruction Cache Option at all, and on the LX7 configuration ESP32-S3
uses, it does not. The 16 KB ICache and 32 KB DCache
[the sibling file](./caches-sram-psram-and-the-mspi-bus.md) describes are
real, and the CPU does miss on them, but they are not the architectural
Xtensa cache these instructions control.

**[measured]** The consequence carries all the way to the assembler. Every
one of the fourteen instructions named above (`dhwb`, `dhwbi`, `dhi`, `dii`,
`diwb`, `diwbi`, `dpfr`, `dpfw`, `dpfro`, `dpfwo`, `dpfl`, `dhu`, `diu`,
`ipfl`, `ihu`, `iiu`, `ihi`, `iii`) fails to assemble for this target with
"Error: unknown opcode or format name", tested one at a time against
`xtensa-esp32s3-elf-gcc` (crosstool-NG esp-14.2.0_20241119, GCC 14.2.0) with
`-mlongcalls`, the flag set the firmware build uses. This is a target
configuration fact, not a runtime trap: the toolchain that ships for
ESP32-S3 was built without these mnemonics in its instruction table, so a
kernel cannot reach them even in hand-written assembly. They are absent from
the source, not merely disabled in hardware.

One instruction outside this list still touches the cache indirectly:
`S32C1I`, the atomic compare-and-swap. On an implementation with the RCW
Transaction path and a configured Data Cache Option, "any corresponding
cache line will be flushed out of the cache by the S32C1I instruction using
the equivalent of a DHWBI instruction before the RCW transaction is
sent".[^isa-s32c1i] Since no Data Cache Option is configured here, this path
does not apply to this chip either; `S32C1I` on ESP32-S3 is documented and
usable, but its cache interaction described in the ISA manual is written
for a different core configuration than the one shipped.

## 2. What ESP32-S3 actually has instead: a chip-level cache peripheral

The TRM describes the real ICache and DCache as a peripheral sitting between
the CPU buses and external memory, with its own operation set: write-back,
clean, invalidate, and preload and lock, each split into a manual and an
automatic form.[^trm-cacheops] These are chip-specific register-level
operations, documented in TRM chapter 4.3.3 and reached from software
through ROM functions and ESP-IDF wrappers, never through the ISA
instructions above. The full text of each operation, and the "16 KB DCache
is 32 KB with `Cache_Occupy_Addr`" fact this section expands on, are in
[the sibling file](./caches-sram-psram-and-the-mspi-bus.md#12-what-the-sizes-cost-in-heap).

ESP-IDF 5.5.1 exposes this peripheral at three layers, from lowest to
highest:

1. **ROM functions**, declared in
   `components/esp_rom/esp32s3/include/esp32s3/rom/cache.h`: the oldest and
   widest surface. `Cache_Invalidate_Addr`, `Cache_Clean_Addr` and
   `Cache_WriteBack_Addr` (lines 473, 484, 501) cover the three non-lock
   operations; `Cache_Start_ICache_Preload` / `Cache_Start_DCache_Preload`
   (lines 616, 650) cover manual preload, each paired with a
   `..._Preload_Done` poll and a `Cache_End_..._Preload` that hands
   auto-preload back; `Cache_Lock_Addr` / `Cache_Unlock_Addr` (lines 862,
   875) cover tag-memory locking; `Cache_Occupy_Addr` (line 1019) pins a
   region outright; `Cache_Suspend_ICache`/`Cache_Suspend_DCache` with
   matching `Resume` calls take a core off the cache
   entirely.[^romcache] Nearly every one carries the same header comment,
   "Please do not call this function in your SDK application": mechanism,
   not supported application interface.
2. **HAL functions**, `components/hal/esp32s3/include/hal/cache_ll.h`, such
   as `cache_ll_writeback_addr` and `cache_ll_invalidate_addr`: thin,
   direct register-poking wrappers with no locking or alignment checking of
   their own, written once against a target-independent HAL rather than one
   ROM header per chip.[^cachell]
3. **`esp_cache_msync`**, `components/esp_mm/include/esp_cache.h`, the
   supported application-facing entry point. This is the function the
   sibling file already covers for cache-to-memory writeback before a DMA
   read and memory-to-cache invalidate after a DMA write.[^espcache] Its
   header states it is "cache-safe and thread-safe", the only one of the
   three layers to make that claim explicitly.

Preload and lock have no equivalent public wrapper. An application that
wants either one calls the ROM function directly, with the two caveats the
header names, and without the alignment- and direction-checking
`esp_cache_msync` does for writeback and invalidate.

## 3. Preload: what it buys, what it costs, and where it is safe

Manual preload starts a hardware fetch of a region into the cache ahead of
the CPU asking for it, and is meant to hide the miss latency
[caches-sram-psram-and-the-mspi-bus.md §3-4](./caches-sram-psram-and-the-mspi-bus.md)
measures, by overlapping the fetch with unrelated work. `Cache_Start_DCache_Preload(addr, size, order)`
suspends auto-preload, issues the fetch, and returns whether auto-preload
was running before the call, which the caller is expected to feed back into
`Cache_End_DCache_Preload` once done, or poll with
`Cache_DCache_Preload_Done`.[^romcache] The size argument "should not exceed
the size of DCache", so a preload request larger than the cache cannot be
satisfied in one call.

**Where it is safe to call.** ESP-IDF's own RGB LCD panel driver calls
`Cache_Start_DCache_Preload` from inside an `IRAM_ATTR` function that also
runs from interrupt context, to preload the next chunk of a frame buffer
while a DMA engine drains the current one.[^lcdrgb] That is direct evidence
that Espressif considers the call interrupt-safe: it is a ROM function, so
it executes independently of whether the flash-backed ICache currently
holds valid application code, and it does not require the calling code to
take any lock the rest of the system also takes. What it does not do is
synchronise with anything else touching the same address range; a preload
racing a concurrent writeback or invalidate of the same line is not
addressed by the header and was not exercised by this search. [uncertain]

**What it costs.** A preload occupies the cache's fetch path exactly like an
ordinary miss does: it contends with the CPU's own miss traffic and with
GDMA on the shared MSPI bus, the arbitration
[the sibling file §2](./caches-sram-psram-and-the-mspi-bus.md#2-what-the-caches-front-and-the-bus-they-share)
describes. Preloading a working set larger than the DCache does not help;
whichever part does not fit will be evicted before it is used, at the cost
of the bus time spent fetching it. No published cycle number for preload
throughput versus an ordinary demand miss was found. [uncertain] The
hardware's alternative, auto-preload, fetches ahead of a hit or miss stream
on its own; the TRM states its existence but this file found no Kconfig
option or register field naming the threshold or window size ESP32-S3
uses. [uncertain]

## 4. Lock: what it buys, what it costs, and what is undocumented

Lock keeps a line resident after it has been fetched, so ordinary misses
elsewhere in the cache cannot evict it: "the cache checks the data that is
already in the cache memory and locks the data only if it falls in the
specified area", and while any way remains unlocked the cache replaces there
first.[^trm-cacheops] The TRM's own caveat is important for a kernel that
also uses writeback or clean: those two operations, plus manual invalidate,
"will only work on the unlocked data", so a locked line must be unlocked
before it can be written back or invalidated on purpose.[^trm-cacheops]

`Cache_Lock_Addr(addr, size)` and `Cache_Unlock_Addr(addr, size)` are the ROM
entry points, both declared with "operation will be done CACHE_LINE_SIZE
aligned" and both carrying the same "do not call in your SDK application"
comment as the rest of the ROM surface.[^romcache] This search found no
caller of either function anywhere in the ESP-IDF 5.5.1 component tree, so
there is no worked example, in the framework itself, of a safe calling
convention for locking from application code: whether it needs an interrupt
disabled, whether it is safe against a concurrent miss on the same set, and
what happens if the requested region does not fit in the ways left unlocked
by an earlier call are all undocumented. [uncertain] Treat lock as a
mechanism the chip has and the SDK deliberately does not surface, rather
than as a supported optimisation technique, until it has been exercised and
measured.

`Cache_Occupy_Addr(addr, size)` is the function with a real, checked-in
caller: ESP-IDF calls it once, at boot, only when
`CONFIG_ESP32S3_DATA_CACHE_16KB` is set, as
`Cache_Occupy_Addr(SOC_DROM_LOW, 0x4000)`.[^cpustart] `Cache_Occupy_Items`'s
own header comment calls this "occupy the
cache items for DCache", separately from `Cache_Lock_Addr`'s tag-memory
lock; the effect this boot-time call produces is exactly the "16 KB DCache
is 32 KB" fact
[the sibling file](./caches-sram-psram-and-the-mspi-bus.md#12-what-the-sizes-cost-in-heap)
states: 16 KB of the 32 KB hardware DCache is pinned to a fixed 16 KB window
of external address space, and that window is then handed to the heap as
ordinary DMA-capable DRAM rather than left as general-purpose cache. This is
a one-time boot decision, not a technique available to a running kernel;
IDF does not call `Cache_Occupy_Addr` again after startup, and this file
found no guidance on whether doing so from application code is
supported. [uncertain]

## 5. Writeback and invalidate: the one operation with a safe, documented API

Unlike preload and lock, writeback and invalidate have exactly the public,
checked API the sibling file already covers:
`esp_cache_msync(addr, size, flags)`, cache-to-memory direction by default,
memory-to-cache with `ESP_CACHE_MSYNC_FLAG_DIR_M2C`.[^espcache] Two details
worth restating in the context of this file's ROM-versus-application
distinction:

- The alignment check lives in `esp_cache_msync.c` itself, not the ROM or
  HAL layers below it: without `ESP_CACHE_MSYNC_FLAG_UNALIGNED`, the
  function rejects an address or size that is not a multiple of the data
  cache's line size, and that flag is refused outright on the
  memory-to-cache direction.[^msync-src] Query the real line size with
  `esp_cache_get_alignment()`, which resolves to
  `cache_hal_get_cache_line_size(cache_level, CACHE_TYPE_DATA)` and falls
  back to 4 only if that HAL call reports zero.[^msync-src]
- `esp_cache_msync` is the one layer in this file's hierarchy documented as
  callable without special care about concurrency: its header calls it
  "cache-safe and thread-safe" outright.[^espcache] The ROM functions one
  layer down make no such claim either way.

The note this file's sibling already raised still applies here from the
other direction: do not call `esp_cache_msync` during a flash operation,
unless execute-in-place from PSRAM is enabled, because the same
cache-disabled window that removes flash and PSRAM from the address space
(TRM 4.3.3.3's operations assume the cache is enabled) also removes the
cache this function manages.[^espcache]

## 6. Summary: which tool for which job

| Need | Tool | Safe from a task | Safe from an ISR | Evidence |
|---|---|---|---|---|
| Make a DMA-written buffer visible to the CPU, or a CPU-written buffer visible to DMA | `esp_cache_msync` | Yes, documented | Not stated either way | [^espcache] |
| Hide a coming miss by fetching ahead | `Cache_Start_ICache_Preload` / `Cache_Start_DCache_Preload` | Undocumented; the only working example found is from an ISR | Yes, by example | [^lcdrgb][^romcache] |
| Keep a region resident against eviction | `Cache_Lock_Addr` / `Cache_Unlock_Addr` | Undocumented, no example found | Undocumented, no example found | [^romcache] |
| Permanently trade cache capacity for a fixed internal-speed window | `Cache_Occupy_Addr`, boot only | N/A, this is a startup-time decision | N/A | [^cpustart] |
| Any Xtensa `DPF*`/`DH*`/`DI*`/`IH*`/`I*` cache-control instruction from the ISA manual | None; not assembled for this target | N/A | N/A | [measured], §1 |

The pattern across every row: the chip's cache peripheral is real and does
have preload and lock capability, but ESP-IDF 5.5.1 turns only writeback and
invalidate into a documented, checked, application-facing function. Preload
has one working example in the framework's own driver code and no written
contract. Lock has neither.

## Open questions

- What does `Cache_Start_DCache_Preload` or `Cache_Lock_Addr` do when called
  concurrently with a DMA writeback or an ordinary CPU miss touching the
  same cache set? Not documented in the ROM header or the TRM chapter;
  would need a targeted device test.
- Does any Espressif component outside ESP-IDF itself (esp-dsp, esp-nn, a
  BSP) call `Cache_Lock_Addr`? Not checked here.
- Is `Cache_Occupy_Addr` safe to call after boot, once FreeRTOS scheduling
  and the second core are both running? IDF's own only call site is before
  either starts.

## Sources

[^isa-general]: Cadence/Tensilica, *Xtensa Instruction Set Architecture (ISA) Reference Manual*, RC-2010.1 (April 2010), Section 4.5.1 "General Cache Option Features" and 4.5.1.2 "Cache Tag Format", pages 111 to 112.
[^isa-prefetch]: Cadence/Tensilica, *Xtensa ISA Reference Manual*, RC-2010.1, Section 4.5.1.3 "Cache Prefetch", page 112.
[^isa-dcache]: Cadence/Tensilica, *Xtensa ISA Reference Manual*, RC-2010.1, Section 4.5.2 "Instruction Cache Option" (page 114) and Section 4.5.5 "Data Cache Option" (page 118), and the DHI, DII, DHWB, DHWBI, DIWB, DIWBI instruction descriptions in Chapter 6, pages 312 to 320.
[^isa-lock]: Cadence/Tensilica, *Xtensa ISA Reference Manual*, RC-2010.1, Section 4.5.4 "Instruction Cache Index Lock Option" (page 117, Table 4-82) and Section 4.5.7 "Data Cache Index Lock Option" (page 121, Table 4-86).
[^isa-dhi]: Cadence/Tensilica, *Xtensa ISA Reference Manual*, RC-2010.1, "Data Cache Hit Invalidate (DHI)" instruction description, "Required Configuration Option: Data Cache Option", page 312.
[^isa-s32c1i]: Cadence/Tensilica, *Xtensa ISA Reference Manual*, RC-2010.1, Section 4.3.13 discussion of the S32C1I instruction's RCW Transaction path and its DHWBI-equivalent cache flush, page 78.
[^core-isa]: ESP-IDF 5.5.1, `components/xtensa/esp32s3/include/xtensa/config/core-isa.h`, lines 246 to 298 (`XCHAL_ICACHE_SIZE`, `XCHAL_DCACHE_SIZE`, `XCHAL_DCACHE_IS_WRITEBACK`, `XCHAL_HAVE_PREFETCH`, `XCHAL_HAVE_CACHE_BLOCKOPS`, `XCHAL_ICACHE_LINE_LOCKABLE`, `XCHAL_DCACHE_LINE_LOCKABLE`), local install path `~/.platformio/packages/framework-espidf/`.
[^trm-cacheops]: Espressif, *ESP32-S3 Technical Reference Manual*, Version 1.8, Section 4.3.3.3 "Cache Operations", pages 405 to 406. https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf
[^romcache]: ESP-IDF 5.5.1, `components/esp_rom/esp32s3/include/esp32s3/rom/cache.h`: `Cache_Invalidate_Addr` (line 473), `Cache_Clean_Addr` (line 484), `Cache_WriteBack_Addr` (line 501), `Cache_Start_ICache_Preload` (line 616), `Cache_Start_DCache_Preload` (line 650), `Cache_Lock_Addr` (line 862), `Cache_Unlock_Addr` (line 875), `Cache_Occupy_Addr` (line 1019).
[^cachell]: ESP-IDF 5.5.1, `components/hal/esp32s3/include/hal/cache_ll.h`, `cache_ll_invalidate_addr` (line 358) and `cache_ll_writeback_addr` (line 401).
[^espcache]: ESP-IDF 5.5.1, `components/esp_mm/include/esp_cache.h`, `esp_cache_msync()` and the `ESP_CACHE_MSYNC_FLAG_*` definitions, including the "cache-safe and thread-safe" statement and the flash-operation caveat in the function's doc comment.
[^msync-src]: ESP-IDF 5.5.1, `components/esp_mm/esp_cache_msync.c`: the unaligned-address check against `cache_line_size` (around line 110 to 112), the memory-to-cache rejection of `ESP_CACHE_MSYNC_FLAG_UNALIGNED` (line 120), and `esp_cache_get_alignment()` (lines 269 to 282).
[^cpustart]: ESP-IDF 5.5.1, `components/esp_system/port/cpu_start.c`, lines 715 and 717, the `CONFIG_ESP32S3_DATA_CACHE_16KB` block calling `Cache_Occupy_Addr(SOC_DROM_LOW, 0x4000)`.
[^lcdrgb]: ESP-IDF 5.5.1, `components/esp_lcd/rgb/esp_lcd_panel_rgb.c`, the `IRAM_ATTR` bounce-buffer refill path calling `Cache_Start_DCache_Preload` under `CONFIG_IDF_TARGET_ESP32S3`, around line 1045.
