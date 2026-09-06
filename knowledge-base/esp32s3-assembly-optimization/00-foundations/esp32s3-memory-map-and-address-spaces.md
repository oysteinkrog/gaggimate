---
title: "ESP32-S3 memory map and address spaces"
id: 00-foundations/esp32s3-memory-map-and-address-spaces
schema_version: 1
doc_type: reference
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, memory-map, iram, dram, psram, heap-caps]
confidence: high
---

# ESP32-S3 memory map and address spaces

This page describes the memory map as an optimizer needs to see it: which
address a pointer must hold to land in a given kind of memory, which bus
reaches it, what access widths are legal there, and which ESP-IDF macro or
heap-capability flag asks for it. Cache behaviour (hit and miss cost,
associativity, prefetch) is not repeated here; see
`03-memory-hierarchy/` for that.

## 1. Two buses, one CPU, one shared boundary

The ESP32-S3 has two Xtensa LX7 cores, each a Harvard-architecture CPU with
a separate instruction bus and data bus. Addresses below `0x4000_0000` go
over the data bus. Addresses from `0x4000_0000` to `0x4FFF_FFFF` go over
the instruction bus. Addresses at or above `0x5000_0000` (peripherals, RTC
memory) are reached by both buses [Espressif2026a][^1].

The data bus supports single-byte, double-byte, 4-byte and 16-byte aligned
accesses. The instruction bus supports only 4-byte-aligned accesses; a
non-aligned access over the instruction bus raises a CPU exception
[Espressif2026a][^1]. Some memory is wired to only one bus; some is wired
to both, at two different addresses, described below.

## 2. Internal ROM: 384 KB, two blocks

Total internal ROM is 384 KB, split into ROM 0 (256 KB) and ROM 1 (128 KB)
[Espressif2026a][^1]:

| Block | Size | Bus | Address range |
|---|---|---|---|
| ROM 0 | 256 KB | Instruction bus only | `0x4000_0000`-`0x4003_FFFF` |
| ROM 1 | 128 KB | Instruction bus | `0x4004_0000`-`0x4005_FFFF` |
| ROM 1 (same words) | 128 KB | Data bus | `0x3FF0_0000`-`0x3FF1_FFFF` |

ROM 1 is one physical 128 KB block reachable at two addresses: word N on
the instruction-bus address maps to the same word N on the data-bus
address (`0x4004_0000` and `0x3FF0_0000` are the same word, and so on)
[Espressif2026a][^1]. ROM holds early boot code and read-only tables for
low-level system software; it is not writable and not a target for
anything an application places itself.

## 3. Internal SRAM: 512 KB in three blocks

Internal SRAM totals 512 KB and splits into three blocks that differ in
which bus reaches them [Espressif2026a][^1]:

| Block | Size | Bus | Address range | Notes |
|---|---|---|---|---|
| SRAM 0 | 32 KB | Instruction bus only | `0x4037_0000`-`0x4037_7FFF` | 16 KB or all 32 KB can be repurposed as ICache; the part used as cache is then invisible to direct CPU access |
| SRAM 1 | 416 KB | Instruction bus and data bus (same words, two addresses) | Instr: `0x4037_8000`-`0x403D_FFFF`; Data: `0x3FC8_8000`-`0x3FCE_FFFF` | Built from 8 KB and 16 KB sub-blocks; one up-to-16-KB sub-block can be repurposed as Trace Memory |
| SRAM 2 | 64 KB | Data bus only | `0x3FCF_0000`-`0x3FCF_FFFF` | 32 KB or all 64 KB can be repurposed as DCache |

SRAM 1 is the block application code calls "IRAM/DRAM aliasing": the same
416 KB of physical SRAM answers to an instruction-bus address and a
data-bus address, word for word, with a fixed offset between the two
views. ESP-IDF computes that offset as
`SOC_I_D_OFFSET = SOC_DIRAM_IRAM_LOW - SOC_DIRAM_DRAM_LOW`, which is
`0x4037_8000 - 0x3FC8_8000 = 0x6F_0000` on the S3, and exposes it as
`MAP_DRAM_TO_IRAM()` / `MAP_IRAM_TO_DRAM()` [ESPIDF2026a][^2]. A buffer
placed in this block can be read as data through one address and executed
as code through the other; this is how a linker section written to at
runtime (a `.data` blob, or code patched in after load) can also run.
SRAM 0 and SRAM 2 have no such alias: SRAM 0 is instruction-side only,
SRAM 2 is data-side only.

Total internal SRAM after subtracting any cache-repurposed portion is what
ESP-IDF's linker script and heap allocator actually have to place things
in; the icache/dcache size trade-off (16 vs 32 KB, 32 vs 64 KB) is a
`03-memory-hierarchy/` topic, not repeated here.

## 4. RTC memory: 16 KB, two independent blocks

RTC memory is 16 KB total, split into two 8 KB SRAM blocks that survive
deep sleep [Espressif2026a][^1]:

| Block | Size | Bus | Address range | Reachable by ULP? |
|---|---|---|---|---|
| RTC FAST | 8 KB | Data and instruction bus (shared address) | `0x600F_E000`-`0x600F_FFFF` | No |
| RTC SLOW | 8 KB | Data and instruction bus (shared address) | `0x5000_0000`-`0x5000_1FFF` | Yes |

RTC FAST is CPU-only: used for instructions and data that must persist
across deep sleep and never need the ULP co-processor to see them. RTC
SLOW is reachable by both the main CPUs and the ULP co-processor, so it is
the block used to hand data back and forth with the ULP
[Espressif2026a][^1]. RTC SLOW also has a second, peripheral-style address
(`0x6002_1000`-`0x6002_2FFF`) in the module/peripheral address map; that
is the same 8 KB, not a second copy [Espressif2026a][^1].

## 5. External memory: flash and PSRAM through the cache

Flash and PSRAM are not wired directly to either bus. The CPU reaches them
only through the cache, which maps a CPU-side virtual address to a
physical flash or PSRAM address using tables the MMU holds
[Espressif2026a][^1]:

| Window | Bus | Address range | Size | Cache behind it |
|---|---|---|---|---|
| Instruction-bus external window | Instruction bus | `0x4200_0000`-`0x43FF_FFFF` | 32 MB | ICache |
| Data-bus external window | Data bus | `0x3C00_0000`-`0x3DFF_FFFF` | 32 MB | DCache |

The instruction-bus window supports 4-byte-aligned reads and instruction
fetches only. The data-bus window supports single-byte, double-byte,
4-byte and 16-byte aligned reads and writes, and can itself be mapped to
flash for read-only access as well as to PSRAM for read-write access
[Espressif2026a][^1]. Flash is normally reached only through the
instruction-bus window (code) and the data-bus window in read-only mode
(rodata); PSRAM is the only external memory the data-bus window maps for
both reads and writes.

Each window maps external memory in fixed 64 KB blocks: "up to 32 MB
instruction bus address space can be mapped ... as individual 64 KB
blocks via the ICache," and the same for the data-bus window and DCache
[Espressif2026a][^1]. That 64 KB figure is the cache's MMU page size on
this chip's usual configuration. ESP-IDF's flash MMU uses the same
`SPI_FLASH_MMU_PAGE_SIZE`, which Kconfig sets from the flash size: 64 KB
above 2 MB of flash, 32 KB for 2 MB flash, 16 KB for 1 MB flash, with an
8 KB option gated behind per-chip support [ESPIDF2026b][^3]. The chip
this repo targets ships with flash well above 2 MB, so 64 KB is the page
size in practice; `03-memory-hierarchy/` covers what a page fault and a
cache miss cost, not this page.

Up to 1 GB of external flash and up to 1 GB of external RAM are
addressable through the MMU's remapping, far more than the 32 MB windows
above can hold mapped at once [Espressif2026a][^1]. Swapping which 64 KB
pages are mapped where is an MMU operation, covered in
`03-memory-hierarchy/`.

## 6. Access-width rules, and what breaks them

The instruction bus is 4-byte-access only, everywhere it reaches: internal
ROM and SRAM over that bus, and the external instruction-bus cache window
[Espressif2026a][^1]. The data bus tolerates byte, halfword, word and
16-byte accesses on internal SRAM and on the PSRAM side of the data-bus
cache window; ROM over the data bus and flash over the data-bus window are
read-only regardless of width.

A CPU that issues an 8-bit or 16-bit load or store against memory wired
for 32-bit-only access, or issues an unaligned load or store anywhere
alignment is required, takes an Xtensa exception rather than silently
succeeding or corrupting data. ESP-IDF's exception-cause table names the
two relevant causes at their EXCCAUSE numbers [ESPIDF2026c][^4]:

| EXCCAUSE | Name | Trigger |
|---|---|---|
| 3 | LoadStoreError | An 8-bit or 16-bit load/store issued against a 32-bit-only addressable region [ESPIDF2026d][^5] |
| 9 | LoadStoreAlignment | A load/store issued at an address not aligned to its own width [ESPIDF2026d][^5] |

ESP-IDF ships a software handler for both causes
(`xtensa_loadstore_handler.S`) that decodes the faulting instruction's
opcode fields and completes the access in software, at a cost of up to 167
CPU cycles per faulted access on the one chip family that documents the
number (the original ESP32, gated behind
`CONFIG_ESP32_IRAM_AS_8BIT_ACCESSIBLE_MEMORY`) [ESPIDF2026e][^6]. ESP-IDF
does not offer that opt-in workaround on the S3
(`IRAM_8BIT_ACCESSIBLE` is defined as false whenever the target is not the
original ESP32 [ESPIDF2026f][^7]), so on this chip the rule is not "byte
access to IRAM is slow," it is "byte access to IRAM is not offered as a
supported path" and code should not depend on it. [uncertain] whether an
unhandled fault of either cause simply is not installed for other
regions on the S3, or whether the general panic handler always catches
it; the S3-specific Kconfig text for this option was not found and only
the ESP32-original Kconfig entry documents the cycle cost.

## 7. DMA-capable memory

GDMA (the chip's general DMA engine) reaches internal SRAM 1 and SRAM 2 at
the same addresses the CPU's data bus uses (`0x3FC8_8000`-`0x3FCE_FFFF` and
`0x3FCF_0000`-`0x3FCF_FFFF`), and reaches external RAM at the same address
the data-bus cache window uses (`0x3C00_0000`-`0x3DFF_FFFF`)
[Espressif2026a][^1]. GDMA cannot reach whatever portion of SRAM 0 or
SRAM 2 is currently repurposed as cache. ESP-IDF's own DMA-capable test,
`esp_ptr_dma_capable()`, checks a pointer against `SOC_DMA_LOW` /
`SOC_DMA_HIGH`, which on the S3 are `0x3FC8_8000` and `0x3FD0_0000`: the
SRAM 1 plus SRAM 2 span, matching the TRM's GDMA description exactly
[ESPIDFsrc2026a][^8]. Whether a PSRAM pointer is separately DMA-capable is
a second, PSRAM-specific check, `esp_ptr_dma_ext_capable()`, gated on
`SOC_PSRAM_DMA_CAPABLE` and the PSRAM driver's own address-range test
[ESPIDFsrc2026a][^8].

## 8. ESP-IDF linker attributes

ESP-IDF's `esp_attr.h` gives source-level control over which of the
address ranges above a symbol lands in, by steering it into a linker
section the link script maps to that range [ESPIDFsrc2026b][^9]:

| Attribute | Effect |
|---|---|
| `IRAM_ATTR` | Places a function into IRAM (`.iram1`) instead of flash, so it executes without a flash-cache fetch |
| `DRAM_ATTR` | Forces a variable into DRAM (`.dram1`) instead of flash-mapped rodata |
| `DMA_ATTR` | `DRAM_ATTR` plus 4-byte alignment, for a buffer a DMA engine will touch |
| `RTC_FAST_ATTR` | Places a variable into RTC FAST memory, retained across deep sleep, CPU-only |
| `RTC_SLOW_ATTR` | Places a variable into RTC SLOW memory, retained across deep sleep, CPU- and ULP-reachable |
| `RTC_IRAM_ATTR` | Places a function into RTC FAST memory as code, for a deep-sleep wake stub |
| `EXT_RAM_BSS_ATTR` | Forces a `.bss` variable into external PSRAM instead of internal SRAM, when `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` is set |

These attributes choose a placement at link time. `heap_caps_malloc()` and
its `MALLOC_CAP_*` flags choose a placement at allocation time from the
heap, which is the only option for anything sized or requested at runtime
[ESPIDFsrc2026c][^10]:

| Flag | Requires |
|---|---|
| `MALLOC_CAP_EXEC` | Memory able to hold and run executable code (IRAM) |
| `MALLOC_CAP_DMA` | Memory a DMA engine can reach |
| `MALLOC_CAP_SPIRAM` | Memory in external PSRAM |
| `MALLOC_CAP_INTERNAL` | Memory that does not disappear when the flash or PSRAM cache is switched off, i.e. internal SRAM, not PSRAM |
| `MALLOC_CAP_8BIT` | Memory that allows 8/16-bit as well as 32-bit accesses (excludes IRAM-only regions) |
| `MALLOC_CAP_32BIT` | Memory that allows aligned 32-bit accesses (includes IRAM) |

A request combining flags that no single region satisfies (for example
`MALLOC_CAP_EXEC | MALLOC_CAP_SPIRAM` on a chip where PSRAM is not
execute-mapped) fails the allocation rather than silently degrading.
Which flag combination lands where, and which of these regions a given
FreeRTOS task's stack should use, depends on what else the system needs
from the same pool at the same time; that is a cost-and-contention
question for a different bucket, not an address-space fact.

## 9. Full address-range table

Every row below cites the table or source it came from; `[uncertain]`
marks a figure not confirmed against a primary source.

| Region | Bus | Low address | High address | Size | Source |
|---|---|---|---|---|---|
| ROM 0 | Instruction | `0x4000_0000` | `0x4003_FFFF` | 256 KB | TRM Table 4.3-1 [^1] |
| ROM 1 | Instruction | `0x4004_0000` | `0x4005_FFFF` | 128 KB | TRM Table 4.3-1 [^1] |
| ROM 1 (alias) | Data | `0x3FF0_0000` | `0x3FF1_FFFF` | 128 KB | TRM Table 4.3-1 [^1] |
| SRAM 0 | Instruction | `0x4037_0000` | `0x4037_7FFF` | 32 KB | TRM Table 4.3-1 [^1] |
| SRAM 1 | Instruction | `0x4037_8000` | `0x403D_FFFF` | 416 KB | TRM Table 4.3-1 [^1] |
| SRAM 1 (alias) | Data | `0x3FC8_8000` | `0x3FCE_FFFF` | 416 KB | TRM Table 4.3-1 [^1] |
| SRAM 2 | Data | `0x3FCF_0000` | `0x3FCF_FFFF` | 64 KB | TRM Table 4.3-1 [^1] |
| RTC FAST | Data/Instruction | `0x600F_E000` | `0x600F_FFFF` | 8 KB | TRM Table 4.3-1 [^1] |
| RTC SLOW | Data/Instruction | `0x5000_0000` | `0x5000_1FFF` | 8 KB | TRM Table 4.3-1 [^1] |
| RTC SLOW (peripheral alias) | Data/Instruction | `0x6002_1000` | `0x6002_2FFF` | 8 KB | TRM Table 4.3-3 [^1] |
| External flash/PSRAM window | Instruction (ICache) | `0x4200_0000` | `0x43FF_FFFF` | 32 MB | TRM Table 4.3-2 [^1] |
| External PSRAM (or read-only flash) window | Data (DCache) | `0x3C00_0000` | `0x3DFF_FFFF` | 32 MB | TRM Table 4.3-2 [^1] |
| Module/peripheral space | Data/Instruction | `0x6000_0000` | `0x600D_0FFF` | ~836 KB | TRM Section 4.2, Table 4.3-3 [^1] |

## Footnotes

[^1]: Espressif Systems, *ESP32-S3 Technical Reference Manual*, Version 1.8, Chapter 4 "System and Memory," Sections 4.2, 4.3.1-4.3.5, Tables 4.3-1, 4.3-2, 4.3-3. `https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf` (fetched 2026-09-06). Datasheet size figures cross-checked against Espressif Systems, *ESP32-S3 Datasheet*, Version 2.2, p.6 ("ROM: 384 KB," "SRAM: 512 KB," "SRAM in RTC: 16 KB"), `https://documentation.espressif.com/esp32-s3_datasheet_en.pdf` (fetched 2026-09-06).

[^2]: ESP-IDF 5.5.1, `components/soc/esp32s3/include/soc/soc.h`, lines 167-200 (`SOC_IRAM_LOW`, `SOC_DRAM_LOW`, `SOC_DIRAM_IRAM_LOW`, `SOC_DIRAM_DRAM_LOW`, `SOC_I_D_OFFSET`, `MAP_DRAM_TO_IRAM`, `MAP_IRAM_TO_DRAM`); local path `~/.platformio/packages/framework-espidf/components/soc/esp32s3/include/soc/soc.h`.

[^3]: ESP-IDF 5.5.1, `components/soc/Kconfig`, `MMU_PAGE_SIZE_*` and `MMU_PAGE_SIZE` options (page size chosen from flash size: `0x10000` for `MMU_PAGE_SIZE_64KB`, the default once no smaller size is forced); `components/soc/esp32s3/include/soc/ext_mem_defs.h` for `SOC_IRAM0_CACHE_ADDRESS_*` / `SOC_DRAM0_CACHE_ADDRESS_*`; `components/spi_flash/include/spi_flash_mmap.h` for `SPI_FLASH_MMU_PAGE_SIZE`.

[^4]: ESP-IDF 5.5.1, `components/esp_system/port/arch/xtensa/panic_arch.c`, the `reason[]` array in `panic_arch_fill_info()`, giving the zero-indexed EXCCAUSE-to-name table used by the panic handler.

[^5]: ESP-IDF 5.5.1, `components/xtensa/xtensa_loadstore_handler.S`, header comment ("LoadStoreErrorCause: Occurs when trying to access 32 bit addressable memory region as 8 bit or 16 bit; LoadStoreAlignmentCause: Occurs when trying to access in an unaligned manner") and the per-opcode table below it.

[^6]: ESP-IDF 5.5.1, `components/esp_system/port/soc/esp32/Kconfig.memory`, the `ESP32_IRAM_AS_8BIT_ACCESSIBLE_MEMORY` option help text ("Each unaligned read/write access will incur a penalty of maximum of 167 CPU cycles"). This option and its Kconfig file are scoped to the original ESP32 port, not the S3.

[^7]: ESP-IDF 5.5.1, `components/esp_common/include/esp_attr.h`, `IRAM_8BIT_ACCESSIBLE` macro: `(CONFIG_IDF_TARGET_ESP32 && CONFIG_ESP32_IRAM_AS_8BIT_ACCESSIBLE_MEMORY)`, false whenever the target is not the original ESP32.

[^8]: ESP-IDF 5.5.1, `components/esp_hw_support/include/esp_memory_utils.h` (inline `esp_ptr_dma_capable()` against `SOC_DMA_LOW`/`SOC_DMA_HIGH`) and `components/esp_hw_support/esp_memory_utils.c` (`esp_ptr_dma_ext_capable()`, `esp_ptr_byte_accessible()`, `esp_ptr_executable()`, `esp_ptr_external_ram()`); `SOC_DMA_LOW` / `SOC_DMA_HIGH` defined as `0x3FC8_8000` / `0x3FD0_0000` in `components/soc/esp32s3/include/soc/soc.h`.

[^9]: ESP-IDF 5.5.1, `components/esp_common/include/esp_attr.h`, definitions of `IRAM_ATTR`, `DRAM_ATTR`, `DMA_ATTR`, `RTC_FAST_ATTR`, `RTC_SLOW_ATTR`, `RTC_IRAM_ATTR`, `EXT_RAM_BSS_ATTR` and the `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` guard on the last one.

[^10]: ESP-IDF 5.5.1, `components/heap/include/esp_heap_caps.h`, `MALLOC_CAP_*` flag definitions (`MALLOC_CAP_EXEC`, `MALLOC_CAP_32BIT`, `MALLOC_CAP_8BIT`, `MALLOC_CAP_DMA`, `MALLOC_CAP_SPIRAM`, `MALLOC_CAP_INTERNAL`, and related flags not covered here).
