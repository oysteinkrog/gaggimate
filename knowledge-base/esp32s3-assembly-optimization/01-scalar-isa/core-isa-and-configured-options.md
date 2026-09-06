---
title: Xtensa LX7 configured ISA options on the ESP32-S3
id: 01-scalar-isa/core-isa-and-configured-options
schema_version: 1
doc_type: reference
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, lx7, lx6, core-isa, coprocessor, cache, isa-options]
confidence: medium
---

# Xtensa LX7 configured ISA options on the ESP32-S3

Tensilica's Xtensa core is a template, not a fixed chip. Espressif picked a set
of options when they configured the ESP32-S3's dual-core LX7, and that choice
is baked into one C header that ships with the toolchain:
`xtensa/config/core-isa.h`. Every macro in it has the form `XCHAL_HAVE_<NAME>`
and is either 0 (not built into this core) or 1 (built in). A macro at 0 is
not a missing library or a runtime flag: the instruction it guards does not
exist in this silicon, and the assembler will refuse the mnemonic outright.

This page reads that header for the ESP32-S3 (LX7) and, next to it, for the
original ESP32 (LX6), so a reader can see which options are new on the S3 and
which have been there since the first chip. Both headers come from the same
ESP-IDF package on this machine: ESP-IDF 5.5.1, packaged toolchain
`xtensa-esp-elf`.[^1][^2]

## The configured options

| Option | Macro(s) | ESP32-S3 (LX7) | ESP32 (LX6) | What it enables | Why an optimizer cares |
|---|---|---|---|---|---|
| 16-bit narrow encodings (Density) | `XCHAL_HAVE_DENSITY` | 1 | 1 | `.n` 2-byte forms of common instructions (`MOV.N`, `ADD.N`, `L32I.N`, `RET.N`, …) alongside the normal 3-byte forms | The compiler picks the narrow form whenever it can, shrinking code size and instruction-fetch traffic; a hand-written asm routine written entirely in wide-form mnemonics is leaving code density on the table for no benefit.[^1][^2] |
| Zero-overhead loops | `XCHAL_HAVE_LOOPS`, `XCHAL_LOOP_BUFFER_SIZE` | 1, 256 bytes | 1, 256 bytes | `LOOP`, `LOOPNEZ`, `LOOPGTZ` plus the `LBEG`/`LEND`/`LCOUNT` registers | GCC turns a simple countable loop into one of these, removing the branch-and-decrement from the loop body entirely. The loop body must fit in the 256-byte buffer or GCC quietly falls back to an ordinary branch; there is no error, only a missed optimization.[^1][^2] |
| NSA/NSAU (Miscellaneous Operations Option) | `XCHAL_HAVE_NSA` | 1 | 1 | `NSA`, `NSAU`: count leading sign or zero bits | Backs `__builtin_clz`/`__builtin_clrsb` and one-instruction fixed-point normalization instead of a shift-and-test loop.[^1][^2] |
| MIN/MAX (Miscellaneous Operations Option) | `XCHAL_HAVE_MINMAX` | 1 | 1 | `MIN`, `MAX`, `MINU`, `MAXU` | Collapses a compare-and-branch or a `?:` between two register values into one instruction; GCC emits it for plain integer `min`/`max` idioms on its own.[^1][^2] |
| Sign extend | `XCHAL_HAVE_SEXT` | 1 | 1 | `SEXT`: sign-extend from an arbitrary bit position | One instruction to sign-extend a packed sub-word field, instead of a shift-left/arithmetic-shift-right pair.[^1][^2] |
| Saturating clamp | `XCHAL_HAVE_CLAMPS` | 1 | 1 | `CLAMPS`: signed clamp to a bit width | Saturating fixed-point arithmetic in one instruction; GCC does not generate it from ordinary saturating-add C idioms, so reaching it needs inline asm or a builtin.[^1][^2] |
| 16x16 multiply | `XCHAL_HAVE_MUL16` | 1 | 1 | `MUL16S`, `MUL16U` | A narrower multiply than `MUL32` below; on this core plain C code has no reason to prefer it over `MUL32`.[^1][^2] |
| 32x32→32 multiply | `XCHAL_HAVE_MUL32` | 1 | 1 | `MULL` | GCC's default lowering for `int * int`. Without this option, integer multiply would go through the MAC16 package or a software routine.[^1][^2] |
| 32x32→64 multiply, high half | `XCHAL_HAVE_MUL32_HIGH` | 1 | 1 | `MULUH`, `MULSH` | Gives the upper 32 bits of a 64-bit product in one instruction; used for Q-format fixed-point multiplies and widening-multiply builtins without a full 64-bit multiply routine.[^1][^2] |
| 32-bit divide | `XCHAL_HAVE_DIV32` | 1 | 1 | `QUOS`, `QUOU`, `REMS`, `REMU` | Hardware integer divide and remainder. Without it, `/` and `%` on 32-bit integers call a software divide routine that is far slower and larger.[^1][^2] |
| Windowed registers | `XCHAL_HAVE_WINDOWED`, `XCHAL_NUM_AREGS` | 1, 64 physical `a` registers | 1, 64 | A 16-register window rotated through the 64 physical registers by `ENTRY`/`RETW` and the `CALL`n family | This is the ABI the whole toolchain assumes (`-mabi=windowed`, GCC 14's default). It makes an ordinary function call cheap, since callee-saved registers do not need explicit spilling, but it makes interrupt entry expensive: a window overflow can force a spill to memory inside the exception path.[^1][^2][^6] |
| Call-window variants | `XCHAL_HAVE_CALL4AND12` | 1 (the header itself marks this "(obsolete option)") | 1 | `CALL4`, `CALL8`, `CALL12` and the matching `ENTRY` sizes | GCC's own code generation only ever emits `CALL4` (a 4-register window); `CALL8`/`CALL12` exist for compatibility, not because the compiler chooses them.[^1][^2] |
| Boolean registers | `XCHAL_HAVE_BOOLEANS` | 1 | 1 | 16 one-bit registers `b0`-`b15`, written by floating-point compares | Every FP comparison result lands in one of these, not in an integer register; plain integer code never touches them.[^1][^2] |
| Single-precision FP | `XCHAL_HAVE_FP`, `XCHAL_HAVE_FP_DIV`, `XCHAL_HAVE_FP_RECIP`, `XCHAL_HAVE_FP_SQRT`, `XCHAL_HAVE_FP_RSQRT` | all 1 | all 1 | `ADD.S`, `MUL.S`, `MADD.S`, `DIV.S`, `SQRT.S`, `RSQRT0.S`, FP compares into a boolean register | Both chips carry a hardware single-precision FPU, including hardware divide and square root. This is worth stating plainly because "the ESP32 has no FPU" is a common claim and it is wrong at the ISA-option level: what the ESP32 (and the S3) actually lack is double precision, next row.[^1][^2] |
| Double-precision FP | `XCHAL_HAVE_DFP` | 0 | 0 | none | `double` is always software-emulated on both chips. A hot path that can tolerate single precision should be written in `float`, not `double`, to reach the hardware unit at all.[^1][^2] |
| MAC16 package | `XCHAL_HAVE_MAC16` | 1 | 1 | `MUL.AA.*`, `MUL.AD.*`, `MUL.DA.*`, `MUL.DD.*` and accumulate forms into a 40-bit `ACCLO`/`ACCHI` pair | An older 16x16 multiply-accumulate unit that predates `MUL32`. Modern GCC does not generate it from ordinary C; reaching it needs inline asm or a compiler builtin.[^1][^2] |
| Atomic compare-and-set | `XCHAL_HAVE_S32C1I` (vs. `XCHAL_HAVE_EXCLUSIVE`) | `S32C1I`=1, `EXCLUSIVE`=0 | `S32C1I`=1 (no `EXCLUSIVE` macro defined at all in this header) | `S32C1I`: store a value into `SCOMPARE1`, then conditionally store to memory if it still matches | The only hardware read-modify-write primitive on this core. There is no load-linked/store-conditional pair (`L32EX`/`S32EX`); a single-word atomic compiles to `S32C1I`, anything wider needs a lock.[^1][^2] |
| Thread pointer | `XCHAL_HAVE_THREADPTR` | 1 | 1 | `RUR.THREADPTR` / `WUR.THREADPTR` | Gives thread-local storage a dedicated register reached by one instruction instead of a table lookup.[^1][^2] |
| Literal loads | `XCHAL_HAVE_L32R` | 1 | 1 | `L32R`: PC-relative load from a literal pool | The only way this core loads a 32-bit constant that does not fit in an immediate field. The literal pool must be within `L32R`'s PC-relative reach, which is why GCC's `-mlongcalls`, `-mtext-section-literals` and `-mauto-litpools` options exist.[^1][^2][^6] |
| Absolute literals | `XCHAL_HAVE_ABSOLUTE_LITERALS` | 0 | 0 | none | Literal pools are always PC-relative here; a literal placed out of reach needs the assembler/linker to relax the reference, there is no absolute-address fallback.[^1][^2] |
| CONST16 | `XCHAL_HAVE_CONST16` | 0 | 0 | none | An alternative way to build a 32-bit constant from two 16-bit immediates without touching a literal pool. Absent on both chips, so every non-trivial constant costs a literal-pool entry, and GCC exposes `-mconst16`/`-mno-const16` purely for targets that do have it.[^1][^2][^6] |
| Scaled-address adds | `XCHAL_HAVE_ADDX` | 1 | 1 | `ADDX2/4/8`, `SUBX2/4/8`: add with a built-in shift | Folds `base + (index << 1\|2\|3)` array-index arithmetic into one instruction instead of a separate shift and add.[^1][^2] |
| Absolute value | `XCHAL_HAVE_ABS` | 1 | 1 | `ABS` | One instruction for integer absolute value.[^1][^2] |
| Wide branches | `XCHAL_HAVE_WIDE_BRANCHES` | 0 | 0 | none | Branch instructions only reach a narrow signed displacement on this core. A far branch needs an indirect jump through a register (`JX`) or an assembler-inserted long-branch sequence, not a wider branch encoding.[^1][^2] |
| Predicted branches | `XCHAL_HAVE_PREDICTED_BRANCHES` | 0 | 0 | none | No branch-hint encoding exists; whatever prediction the pipeline does is not something the compiler can influence through these instructions.[^1][^2] |
| Release/acquire ordered access | `XCHAL_HAVE_RELEASE_SYNC` | 1 | 1 | `L32AI`, `S32RI` | Acquire/release-ordered load and store, one step short of a full load-linked/store-conditional pair.[^1][^2] |
| Load/store datapath width | `XCHAL_DATA_WIDTH` | 16 bytes | 4 bytes | Width of the core's own datapath to the cache | The S3's datapath is four times wider than the original ESP32's. This is a property of the CPU pipeline itself, separate from the PIE vector coprocessor described below; it is why a plain scalar copy loop can already move more bytes per cycle on the S3.[^1][^2] |
| D-cache pipeline depth | `XCHAL_DATA_PIPE_DELAY` | 1 (5-stage) | 2 (7-stage) | Number of d-side pipeline stages | A lower number means a shorter load-use penalty: the S3's core resolves a load sooner, all else equal, than the original ESP32's longer pipeline.[^1][^2] |
| Max instruction length | `XCHAL_MAX_INSTRUCTION_SIZE` | 4 bytes | 3 bytes | Widest single instruction the decoder accepts | The S3 core was generated to decode a 4-byte instruction, which the vector coprocessor's wider opcodes need; the original ESP32 tops out at 3 bytes.[^1][^2] |

## Interrupts, timers and the debug module

Both chips report the same interrupt and debug configuration in
`core-isa.h`:[^1][^2]

- `XCHAL_NUM_INTLEVELS` is 6, plus a separate NMI "level" of 7
  (`XCHAL_NMILEVEL`). `XCHAL_EXCM_LEVEL` is 3: levels 1 through 3 are masked
  together by `PS.EXCM`, and an interrupt handler written in C can only run
  at one of those levels. A handler assigned to level 4 or above (a
  "high-priority interrupt" in Xtensa's terms) must be written in assembly,
  because the C runtime assumes `EXCM` is set and cannot be safely entered
  from a level where it isn't.
- `XCHAL_NUM_TIMERS` is 3: three `CCOMPARE`n registers compared against the
  free-running `CCOUNT`. Every hardware timer a program wants — an RTOS
  tick, a high-resolution timer, anything else — competes for one of these
  three per core.
- `XCHAL_NUM_INTERRUPTS` is 32 (`XCHAL_NUM_EXTINTERRUPTS` = 26 of those are
  external).
- The debug option (`XCHAL_HAVE_DEBUG`) is on, with on-chip debug (OCD), 2
  `IBREAK` and 2 `DBREAK` registers, JTAG access, and an in-core TRAX trace
  buffer of 16,384 bytes (`XCHAL_TRAX_MEM_SIZE`).
- `XCHAL_NUM_PERF_COUNTERS` is 2. Only two hardware performance-counter
  events can be captured at once; a profiling pass that wants more than two
  simultaneous counts has to run more than once.

## Coprocessors: the FPU, and where the vector unit actually is

`core-isa.h` on both chips sets `XCHAL_HAVE_CP` (the coprocessor option) to 1
and `XCHAL_CP_MAXCFG` to 8, meaning up to 8 coprocessor IDs could exist.[^1][^2]
Which ones actually do is not in `core-isa.h` at all — it is in the
neighboring `tie.h`:[^3][^4]

- **ESP32 (LX6):** `XCHAL_CP_NUM` is 1. The only coprocessor is id 0, named
  `"FPU"`, with a 72-byte state-save area.[^4]
- **ESP32-S3 (LX7):** `XCHAL_CP_NUM` is 2. Coprocessor 0 is still `"FPU"`
  (72-byte save area). Coprocessor 3 is named `"cop_ai"` in the header, with
  a 208-byte save area aligned to 16 bytes.[^3]

Coprocessor 3, `cop_ai`, is what this repository and Espressif's own
documentation call the PIE vector/SIMD extension. **The string "PIE" does
not appear anywhere in `core-isa.h` or `tie.h`.** It is Espressif's public
name for the unit; the Xtensa configuration tooling only knows it as a
custom TIE coprocessor with HAL identifier `cop_ai` and coprocessor id 3.
A reader who greps the vendored headers for "PIE" and finds nothing has not
found a documentation gap — they are looking in the wrong file. The
coprocessor's context is saved and restored through the same generic
mechanism as the FPU: the `CPENABLE` register and FreeRTOS's lazy
coprocessor-context switch, gated by the coprocessor-disabled exception, not
by anything specific to `cop_ai`.[^3] This is also the reason a hand-written
kernel must never write `CPENABLE` directly — doing so bypasses the OS's
lazy-save bookkeeping for whichever task's FPU or vector state was live.

## Cache: what the core header says, and where the real numbers live

This is the one area where reading `core-isa.h` alone is actively
misleading. On **both** chips, the header reports:[^1][^2]

- `XCHAL_ICACHE_SIZE` = 0, `XCHAL_DCACHE_SIZE` = 0 ("I-cache size in bytes
  or 0" — the comment in the header itself flags 0 as a real, meaningful
  value, not a placeholder)
- `XCHAL_ICACHE_WAYS` = 1, `XCHAL_DCACHE_WAYS` = 1, `XCHAL_ICACHE_SETWIDTH`
  = 0, `XCHAL_DCACHE_SETWIDTH` = 0
- `XCHAL_ICACHE_LINESIZE` = 4 bytes on both chips; `XCHAL_DCACHE_LINESIZE`
  is 16 bytes on the S3 versus 4 bytes on the original ESP32

A cache reported as zero-size, one-way, is not a core with no cache. It
means the Xtensa core generator was configured with the cache's capacity and
associativity left external to the CPU macro itself — on these Espressif
parts, the real instruction and data caches are a separate, software
-configurable block controlled by ESP-IDF at boot, not a fixed property of
the Xtensa core IP. The actual sizes live in the SoC build configuration,
not in `core-isa.h`:[^5]

- Instruction cache: selectable 16 KB or 32 KB (default 16 KB), 4-way or
  8-way associative (default 8-way), 16-byte or 32-byte line (default 32
  bytes; 16-byte lines are only available when the 16 KB size is chosen).
  Choosing the 16 KB size still leaves the full 32 KB of physical cache SRAM
  in place — the unused half is handed to the heap allocator instead of the
  cache, per the Kconfig help text.[^5]
- Data cache: selectable 16 KB, 32 KB or 64 KB (default 32 KB), 4-way or
  8-way (default 8-way), 16, 32 or 64-byte line (default 32 bytes). The 16
  KB and 32 KB choices both program the same 0x8000 register value; the
  16 KB choice reserves the other half of a 32 KB physical block for the
  heap, the same pattern as the instruction cache.[^5]

The practical rule: for anything cache-geometry related on an Espressif
Xtensa part, `core-isa.h`'s cache fields are not the source of truth. Check
the SoC's own cache Kconfig (or the equivalent runtime cache-size API) for
the number that is actually built into the running firmware, because it is
a build choice, not a silicon constant.

## How to read a `core-isa.h` and why check it before using an instruction

1. **Find the macro, not the mnemonic, first.** Every instruction group in
   the Xtensa ISA reference is gated by an `XCHAL_HAVE_*` name (an "option"
   in Tensilica's terms). Cross-referencing the manual's option name against
   the header is more reliable than trusting a blog post or a training
   memory of "the ESP32 has instruction X," because the option is what the
   assembler actually checks.[^7]
2. **Read the header the active toolchain will include, not a copy from
   memory or a different chip's package.** The ESP32 (LX6), the ESP32-S2
   (LX7, single core), and the ESP32-S3 (LX7, dual core) each ship their own
   `core-isa.h` under a chip-named subdirectory, and, as the table above
   shows, they do not agree on data width, pipeline depth, cache line size,
   or coprocessor count even when both chips are nominally "the same LX7 or
   LX6 core." Find the include path the compiler actually uses (for
   example, ask the compiler driver to report its search paths) rather than
   assuming which header applies.
3. **A macro at 0 fails at assemble time, which is a gift.** Because the
   assembler statically knows which coprocessor and TIE options were
   configured in, an instruction that is not present fails to assemble,
   with a clear diagnostic, rather than trapping as an illegal instruction
   at runtime the way a missing CPU feature typically would on a general
   -purpose ISA. Treat an assembler error on an unfamiliar mnemonic as a
   signal to check the option, not to look for a typo first.
4. **Coprocessor instructions need `tie.h`, not `core-isa.h`.** As the
   comparison above shows, `XCHAL_HAVE_CP` being 1 only says the core
   supports having coprocessors; which ones exist, their IDs, and their
   state-save sizes are enumerated in `tie.h`. A custom or vendor-specific
   coprocessor, like the S3's `cop_ai`, will not show up under any
   familiar-sounding macro name in `core-isa.h` at all.
5. **Treat the header as the build's ground truth, ahead of any
   general-purpose knowledge of "what the ESP32 has."** The single-precision
   FPU on the ESP32, above, is the clearest example: it is a documented
   `XCHAL_HAVE_FP` = 1 in the configuration header, yet it is common to see
   the chip described as having no floating-point hardware at all. Read the
   macro before repeating the claim either way.

## Footnotes

[^1]: Espressif, ESP-IDF 5.5.1 packaged Xtensa toolchain, `components/xtensa/esp32s3/include/xtensa/config/core-isa.h` (and, for coprocessors, the neighboring `tie.h`), as installed at `~/.platformio/packages/framework-espidf/components/xtensa/esp32s3/include/xtensa/config/core-isa.h` on this machine. Header customer/build stamp: "Customer ID=15128; Build=0x90f1f; Copyright (c) 1999-2021 Tensilica Inc."; `XCHAL_HW_VERSION_NAME` = "LX7.0.12".

[^2]: The same package's ESP32 (original, LX6) variant, `components/xtensa/esp32/include/xtensa/config/core-isa.h`, as installed at `~/.platformio/packages/framework-espidf/components/xtensa/esp32/include/xtensa/config/core-isa.h`. Header stamp: "Customer ID=11657; Build=0x5fe96; Copyright (c) 1999-2016 Tensilica Inc."; `XCHAL_HW_VERSION_NAME` = "LX6.0.3".

[^3]: `components/xtensa/esp32s3/include/xtensa/config/tie.h`, same package, lines defining `XCHAL_CP_NUM`, `XCHAL_CP0_NAME`/`XCHAL_CP3_NAME`, and each coprocessor's `_SA_SIZE`/`_SA_ALIGN`.

[^4]: `components/xtensa/esp32/include/xtensa/config/tie.h`, same package, same fields, showing only coprocessor 0 defined.

[^5]: `components/esp_system/port/soc/esp32s3/Kconfig.cache`, same ESP-IDF package, the `ESP32S3_INSTRUCTION_CACHE_SIZE`/`ESP32S3_ICACHE_ASSOCIATED_WAYS`/`ESP32S3_INSTRUCTION_CACHE_LINE_SIZE` and `ESP32S3_DATA_CACHE_SIZE`/`ESP32S3_DCACHE_ASSOCIATED_WAYS`/`ESP32S3_DATA_CACHE_LINE_SIZE` Kconfig choices and their defaults and help text.

[^6]: GNU Project, *Using the GNU Compiler Collection (GCC)*, version 14.2.0, "Xtensa Options" section (`gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Xtensa-Options.html`), for `-mconst16`/`-mno-const16`, `-mabi=call0|windowed`, `-mlongcalls`, `-mtext-section-literals`/`-mauto-litpools`, and `-mfused-madd`, fetched 2026-09-06.

[^7]: Cadence/Tensilica, *Xtensa Instruction Set Architecture (ISA) Reference Manual*. [uncertain] This reference names the manual as the authority for per-option instruction semantics (Loop Option, Boolean Option, Miscellaneous Operations Option, Coprocessor Option, and so on), but this leaf does not cite specific page or section numbers from it: the copies available to fetch during this session were either too large to retrieve in full or returned an access error, so no edition or section number could be verified firsthand. Every option-to-instruction mapping stated above is instead cross-checked directly against the vendored header comments and, where noted, the GCC 14.2 manual. Do not add a section number to this manual for these options without opening a verified copy first.
