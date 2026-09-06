---
title: Code and Data Placement in ESP-IDF
id: 04-toolchain-and-codegen/code-and-data-placement-in-esp-idf
schema_version: 1
doc_type: how-to
status: stable
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, esp-idf, linker, iram, dram, psram, heap-caps]
confidence: high
---

# Code and Data Placement in ESP-IDF

A kernel that is fast on paper still runs slow if its code sits in flash
behind a cache miss, or its lookup table sits in PSRAM behind a slower bus.
This page covers how to control where code and data physically end up:
attributes, the linker fragment (`.lf`) system, PSRAM configuration, and
the allocator flags that pick a memory type at run time. It targets
ESP-IDF 5.5.1 on the ESP32-S3, checked against the vendored framework
sources (`esp_attr.h`, `esp_system/ld/`, the `.lf` files, and
`esp_heap_caps.h` in the `framework-espidf` package) and the ESP-IDF v5.5.1
online documentation.[^1]

What each memory type costs in cycles once code or data lands there is
covered in `03-memory-hierarchy/`, not here: this page is only about how
to put something in a given place.

## IRAM_ATTR: code in instruction RAM

By default the linker puts compiled code in flash and lets the instruction
cache serve it. `IRAM_ATTR` overrides that for one function, placing it in
internal instruction RAM (IRAM) so it runs with no cache-miss risk and stays
reachable while the flash cache is disabled (see below). The macro expands
to a linker section attribute, `__attribute__((section(".iram1")))` under
the hood, defined as:

```c
// components/esp_common/include/esp_attr.h
#define IRAM_ATTR _SECTION_ATTR_IMPL(".iram1", __COUNTER__)
```

IRAM on the ESP32-S3 is small and shared: the same physical SRAM also backs
part of the heap, so every function pulled into IRAM is bytes the heap
does not get. `IRAM_ATTR` is for code that must not stall on a
flash-cache miss or must keep running while the cache is off, not a
general "make it faster" switch. Measure the actual IRAM budget with
`idf.py size`, which prints "a summary of the statically-allocated memory
for different memory types in the firmware binary" broken out by IRAM,
DRAM, and flash code and data; `idf.py size-components` and `idf.py
size-files` break the same totals down per archive and per source file,
both sorted by contribution and both accepting `--diff` to compare two
builds.[^2]

A related macro, `FORCE_IRAM_ATTR`, is the same placement but bypasses the
usual "is this ever called" pruning; ESP-IDF uses it for a handful of
functions the linker would otherwise discard.

## DRAM_ATTR: data that must stay internal

`DRAM_ATTR` forces a variable into internal data RAM instead of the
flash-mapped rodata region the compiler would otherwise pick for a
`const` table:

```c
// components/esp_common/include/esp_attr.h
#define DRAM_ATTR _SECTION_ATTR_IMPL(".dram1", __COUNTER__)
```

Two attributes build on it. `DMA_ATTR` is `WORD_ALIGNED_ATTR DRAM_ATTR`
combined, for a buffer that must be internal and 4-byte aligned so a DMA
engine can address it; `DRAM_DMA_ALIGNED_ATTR` aligns to the cache line
size (`CONFIG_CACHE_L1_CACHE_LINE_SIZE`) instead, so a DMA transfer never
straddles a cache line it does not own.[^1] `DRAM_STR` wraps a string
literal so it can be handed to code that runs with the flash cache
disabled, since a plain literal would otherwise live in flash rodata.

## The linker fragment system: schemes and mappings

Flash placement is not decided function by function in source. ESP-IDF's
build generates the final linker script from `.lf` (linker fragment) files
that each component ships, using two kinds of section in every `.lf` file:
a `[scheme:NAME]` block that says, for a category of input section (`text`,
`data`, `rodata`, `bss`, and the placement-specific categories `iram`,
`dram`, `rtc_text`, `rtc_data`, `extram_bss`, and others), which output
memory region it goes to; and a `[mapping:NAME]` block that says which
object files or symbols use which scheme.[^3]

The built-in schemes, defined in `components/esp_system/app.lf`, are:

| Scheme | What it does |
|---|---|
| `default` | Catch-all: code and constants go to flash, variables go to internal RAM.[^4] |
| `noflash` | Places code and read-only data into IRAM/DRAM instead of flash.[^4] |
| `noflash_text` | Code only, into IRAM; leaves rodata in flash. |
| `noflash_data` | Read-only data only, into DRAM; leaves code in flash. |
| `rtc` | Places code, data, and read-only data into RTC fast/slow memory, for deep-sleep-surviving code.[^4] |

`noflash_text` and `noflash_data` are the split halves of `noflash` and are
useful when only the code or only the constants of a translation unit are
hot, not both.[^1] The scheme block source:

```
# components/esp_system/app.lf
[scheme:noflash]
entries:
    text -> iram0_text
    rodata -> dram0_data

[scheme:noflash_data]
entries:
    rodata -> dram0_data

[scheme:noflash_text]
entries:
    text -> iram0_text
```

A `[mapping]` block applies a scheme at one of three granularities: every
object in an archive, one named object file, or one named symbol inside an
object file. From `components/esp_ringbuf/linker.lf`, an archive-wide
mapping with per-symbol overrides:

```
[mapping:esp_ringbuf]
archive: libesp_ringbuf.a
entries:
    * (noflash_text)
    if RINGBUF_PLACE_FUNCTIONS_INTO_FLASH = y:
        ringbuf: prvGetCurMaxSizeNoSplit (default)
        ringbuf: xRingbufferCreate (default)
```

Here `*` maps every object in the archive to `noflash_text` (IRAM), and
the `if` block sends specific functions in the `ringbuf` object file back
to `default` (flash) when a Kconfig option asks for it. `object:symbol
(scheme)` is the per-symbol form; `object (scheme)` alone (no colon)
applies to every symbol in that one object file, as used throughout
`components/freertos/linker_common.lf`. A project's own `.lf` files use
the same three-level syntax and register through
`idf_component_register(... LDFRAGMENTS "linker.lf")`.

The `extram_bss` category routes zero-initialized variables to PSRAM, only
when `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` is set; otherwise
`extram_bss -> dram0_bss` and the variables stay internal.[^4]

## CONFIG_SPIRAM_* options: what moves to PSRAM and when

These Kconfig options change where a `static` table or a `malloc()`
allocation lands without touching a single attribute in source. Read from
`components/esp_psram/Kconfig.spiram.common`:

| Option | Default | Effect |
|---|---|---|
| `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` | off | "If enabled, variables with `EXT_RAM_BSS_ATTR` attribute will be placed in SPIRAM instead of internal DRAM." Also moves the BSS of `lwip`, `net80211`, `pp`, and `bt` automatically, and enables the `extram_bss` linker scheme for other objects. Such variables are always zero-initialized.[^5] |
| `CONFIG_SPIRAM_USE_MALLOC` | on (when PSRAM is enabled) | Adds external memory to both the capability allocator and the plain `malloc()` pool, so ordinary `malloc()` calls can return a PSRAM pointer without any source change.[^6] |
| `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` | 16384 bytes | The size threshold `malloc()` uses to choose a preferred region: "allocations less than this size in internal memory, while allocations larger than this will be done from external RAM," falling back to the other region if the preferred one is full.[^7] |
| `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` | off | "Try to allocate memories of WiFi and LWIP in SPIRAM firstly. If failed, try to allocate internal memory then."[^7] |

A `static` array with no attribute at all is unaffected by any of these:
it lives wherever the `default` scheme's `bss`/`data`/`rodata` mapping
sends it, which is internal RAM unless a `.lf` mapping or
`EXT_RAM_BSS_ATTR` says otherwise.[^5]

## heap_caps_malloc: choosing memory by capability at run time

`heap_caps_malloc(size, caps)` and `heap_caps_aligned_alloc(alignment,
size, caps)`, declared in `esp_heap_caps.h`, take a bitmask of required
capabilities instead of assuming one region:[^8]

| Flag | Value | Meaning |
|---|---|---|
| `MALLOC_CAP_INTERNAL` | `1<<11` | "Memory must be internal; specifically it should not disappear when flash/spiram cache is switched off." |
| `MALLOC_CAP_DMA` | `1<<3` | "Memory must be able to [be] accessed by DMA." |
| `MALLOC_CAP_SPIRAM` | `1<<10` | "Memory must be in SPI RAM." |
| `MALLOC_CAP_32BIT` | `1<<1` | "Memory must allow for aligned 32-bit data accesses." |

Flags combine with bitwise OR; `heap_caps_malloc(n, MALLOC_CAP_INTERNAL |
MALLOC_CAP_DMA)` asks for internal, DMA-capable memory in one call. For a
table read by a PIE (vector) load or store instruction, use
`heap_caps_aligned_alloc(16, size, MALLOC_CAP_INTERNAL)` so the pointer is
16-byte aligned up front: the PIE aligned load/store forms mask low
address bits rather than faulting on misalignment, so an unaligned
pointer silently reads the wrong bytes instead of erroring.[^9]

## GCC attributes for kernel code

Attributes that matter when hand-tuning a kernel, from the GCC 14 manual's
"Common Function Attributes" and the variable-attribute equivalents:[^10]

- **`aligned(16)`**: on a function, pins its first instruction to a
  16-byte boundary; on a variable, pins its start address. Useful for a
  routine entered from an indexed jump table, or a table read by an
  aligned SIMD load.
- **`section("name")`**: overrides where the compiler emits a function's
  code or a variable's data, the mechanism `IRAM_ATTR`/`DRAM_ATTR` build
  on. "Normally, the compiler places the code it generates in the text
  section" and this attribute moves it.[^10]
- **`noinline`**: stops a function being folded into its callers, to keep
  a hot loop's body out of a cold caller and keep disassembly readable.
- **`always_inline`**: forces inlining and turns a failure to inline into
  a compile error, for a small helper that must never carry call overhead.
- **`hot`**: "the function is optimized more aggressively and on many
  targets it is placed into a special subsection," grouping hot code for
  locality.[^10]
- **`cold`**: the size-optimized counterpart, for error paths and rare
  branches, grouped into their own subsection.
- **`optimize("O2")`** (or another level/flag string): overrides the
  command-line optimization for one function, arguments "behave as if
  appended to the command-line."[^10] Useful to force `-O2` on one hot
  function inside a project built at `-Os`.
- **`used`**: keeps a function or variable in the output even with no
  apparent reference in the translation unit, for code reached only from
  assembly or a linker-fragment entry point: "code must be emitted for the
  function even if it appears that the function is not referenced."[^10]
- **`__restrict`**: a C99/C++ type qualifier, not a GCC attribute, but the
  same toolbox: it tells the compiler two pointers do not alias, unlocking
  vectorization aliasing rules would otherwise forbid. Apply it only when
  the caller actually guarantees non-aliasing; a false promise is
  undefined behavior. [uncertain] whether GCC 14's Xtensa backend acts on
  it more aggressively than a generic target; not confirmed here.

## -ffunction-sections and -fdata-sections

ESP-IDF's build passes `-ffunction-sections -fdata-sections` to every
component compile, confirmed at `tools/cmake/build.cmake`.[^11] Without
these flags, GCC emits one `.text` and one `.data` section per
translation unit, so a linker fragment mapping that names one function in
a multi-function source file has nothing to grab: the whole file moves
together or not at all. With the flags, each function gets its own
`.text.<name>` section and each variable its own `.data.<name>` or
`.bss.<name>` section, which is what makes the `object:symbol (scheme)`
per-symbol mapping form possible. The cost is link time and more sections
for the linker to process, not usually visible at this project's size.

## Code that runs with the flash cache disabled

While a component is erasing or writing SPI flash, or inside certain
interrupt contexts, the flash cache goes offline: nothing mapped from
flash is readable on the core that owns the operation. ESP-IDF's docs
state that "interrupt handlers must be placed into IRAM if
`ESP_INTR_FLAG_IRAM` is used when registering the interrupt handler," and
that "strings or constants inside an `IRAM_ATTR` function may not be
placed in RAM automatically," so a literal reachable from such a function
needs its own `DRAM_ATTR` (or `DRAM_STR` for a string).[^12] In practice:
a function marked `IRAM_ATTR` for this reason must not call a function
that is not itself IRAM-resident, and must not read `const` data that is
not `DRAM_ATTR`: either could be a flash read that returns garbage or
faults while the cache is down. [uncertain] the precise trigger list for
cache-disable windows beyond flash erase/write and `ESP_INTR_FLAG_IRAM`
ISRs; not confirmed against the concurrency-control source for this page.

## Finding where a symbol actually landed

A `section()` attribute or a `.lf` mapping is a request; verifying the
linker honored it is a separate step. Three tools, cheapest first:

1. **`idf.py size` / `size-components` / `size-files`**, described above,
   give aggregate IRAM/DRAM/flash totals, then per-archive, then
   per-source-file: fast, and enough to catch "this whole archive is in
   IRAM when it shouldn't be."[^2]
2. **`xtensa-esp32s3-elf-nm --size-sort --print-size <elf>`** lists every
   symbol with its address and size; the address range says which region
   it landed in, once you know the project's region map from the linker
   script (`xtensa-esp32s3-elf-nm <elf> | grep <symbol>` for one symbol).
   The binary ships in the `toolchain-xtensa-esp32s3` package and is on
   the build's `PATH` during a `pio run`.
3. **The `.map` file** the linker writes alongside the ELF is ground
   truth: every input section, which object file it came from, which
   output section it landed in, and the exact address. It is the only one
   of the three that shows *why* a symbol landed where it did (which
   mapping rule matched), at the cost of size and a text search instead of
   one command.

## Trade-offs

| Choice | Faster / more predictable | Costs |
|---|---|---|
| Code in IRAM (`IRAM_ATTR`, `noflash`/`noflash_text` scheme) | No flash-cache-miss risk; safe to run while cache is disabled | IRAM bytes, which also back part of the heap; budget is small and shared |
| Code left in flash (`default` scheme) | No IRAM cost | Subject to icache misses; unsafe to run while the cache is disabled |
| Table in internal SRAM (`DRAM_ATTR`, `heap_caps_malloc(MALLOC_CAP_INTERNAL)`) | Lowest and most consistent per-access latency | Internal SRAM is the scarcest, most contended memory on the chip |
| Table in PSRAM (`EXT_RAM_BSS_ATTR`, `MALLOC_CAP_SPIRAM`, or plain `malloc()` above the `ALWAYSINTERNAL` threshold) | Effectively unlimited capacity | Higher and less consistent per-access latency; shares a bus with flash |

The cycle-level cost of each row, and when the difference is worth
fighting the linker for, is the subject of `03-memory-hierarchy/`: this
page only covers making the placement happen once that analysis decides.

## Footnotes

[^1]: Espressif Systems, *ESP-IDF Programming Guide* v5.5.1, "Linker Script Generation" (api-guides/linker-script-generation.html); source `components/esp_common/include/esp_attr.h`, `framework-espidf` package under `~/.platformio/packages/`, read 2026-09-06.
[^2]: Espressif Systems, *ESP-IDF Programming Guide* v5.5.1, "idf.py Size (idf-size.py)" (api-guides/tools/idf-size.html), read 2026-09-06.
[^3]: Espressif Systems, *ESP-IDF Programming Guide* v5.5.1, "Linker Script Generation" (api-guides/linker-script-generation.html), read 2026-09-06.
[^4]: Source: `components/esp_system/app.lf`, `framework-espidf` package, tag matching ESP-IDF 5.5.1.
[^5]: Source: `components/esp_psram/Kconfig.spiram.common`, config `SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`, `framework-espidf` package.
[^6]: Espressif Systems, *ESP-IDF Programming Guide* v5.5.1, "Support for External RAM" (api-guides/external-ram.html), config `CONFIG_SPIRAM_USE_MALLOC`, read 2026-09-06.
[^7]: Source: `components/esp_psram/Kconfig.spiram.common`, configs `SPIRAM_MALLOC_ALWAYSINTERNAL` and `SPIRAM_TRY_ALLOCATE_WIFI_LWIP`, `framework-espidf` package.
[^8]: Source: `components/heap/include/esp_heap_caps.h`, macros `MALLOC_CAP_INTERNAL`, `MALLOC_CAP_DMA`, `MALLOC_CAP_SPIRAM`, `MALLOC_CAP_32BIT`, and `heap_caps_malloc`/`heap_caps_aligned_alloc`, `framework-espidf` package.
[^9]: [uncertain] PIE address-masking on unaligned `EE.VLD`/`EE.VST.128.IP` forms is stated from the ISA reference and a device/QEMU test recorded in `02-pie-vector/`; not re-verified against the manual on this page.
[^10]: Free Software Foundation, *Using the GNU Compiler Collection (GCC)* v14.2.0, "Common Function Attributes" (gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Common-Function-Attributes.html), read 2026-09-06.
[^11]: Source: `tools/cmake/build.cmake`, `framework-espidf` package, compile option list including `-ffunction-sections` and `-fdata-sections`.
[^12]: Espressif Systems, *ESP-IDF Programming Guide* v5.5.1, "Memory Types," sections "When to Place Code in IRAM" and "How to Place Code in IRAM" (api-guides/memory-types.html), read 2026-09-06.
