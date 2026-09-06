---
title: "04-toolchain-and-codegen: bucket index"
id: 04-toolchain-and-codegen/readme
schema_version: 1
doc_type: reference
status: review
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, gcc, toolchain, codegen, inline-asm, esp-idf, linker]
confidence: high
---

# 04-toolchain-and-codegen: bucket index

What the GCC 14.2 `xtensa-esp-elf` toolchain does to your code before the
chip ever sees it: the flags ESP-IDF 5.5.1 passes, the constraints and
clobbers that make inline asm safe, the four faults visible in a `.S`
file, and where a symbol physically lands. Everything here was rerun
against the local toolchain and the vendored framework on 2026-09-06.

| File | Topic | Key claims |
|---|---|---|
| [gcc14-xtensa-flags-and-what-they-cost.md](./gcc14-xtensa-flags-and-what-they-cost.md) | Every flag ESP-IDF adds, and what it costs in instructions | `-mlongcalls` is unconditional and expands in the assembler, but the linker relaxes most of it back to a direct `call8`. ESP-IDF passes neither literal-pool flag, so builds run on the compiler default, `-mno-text-section-literals`. `-fno-jump-tables` and `-fno-tree-switch-conversion` are separate transformations, both off, so every `switch` is a compare chain. On the ESP32-S3, `memcpy` and friends are ROM addresses from a linker fragment, not ESP-IDF code. Kconfig has no `-O3`. LTO is compiled in and then disabled with an unconditional `-fno-lto`. The ABI default comes from the core configuration and is windowed. |
| [gcc-extended-inline-asm-on-xtensa.md](./gcc-extended-inline-asm-on-xtensa.md) | Writing an `asm` block the compiler cannot break | The GCC manual's Xtensa constraint list is nine letters short of `constraints.md`. On Xtensa `q` is the stack pointer, not a vector register. `a0` and `a1` are fixed, so `r` and `a` behave alike. `SAR`, `SAR_BYTE`, the `LOOP` registers and the `q` registers have no clobber name, so a block that touches them owns them silently. A walking pointer needs `+r`; a temporary written before the last input read needs `=&r`; a store needs `"memory"`. A hand-written `LOOP` body over 256 bytes is relaxed into a sequence that destroys the count register. |
| [register-pressure-spills-and-reading-the-assembly.md](./register-pressure-spills-and-reading-the-assembly.md) | Reading a hot loop's `.S` before writing assembly | Four faults, all visible in the file: spills (`sp` references in the body), a lost hardware loop, a libcall in the body, and a load next to its consumer. A leaf gets `a2` to `a15`; anything making a `call8` keeps only `a2` to `a7`. Two separate GCC passes can refuse the hardware loop, and inline asm is caught only by the late one. Integer divide and single-precision add, multiply and convert are hardware; `float` divide, `sqrtf`, `fmodf` and all `double` work are calls. The backend's pipeline model is latency-only, with load 2 and `fmadd` 4, and knows nothing about memory. |
| [what-the-headers-say-versus-what-the-tools-do.md](./what-the-headers-say-versus-what-the-tools-do.md) | Five places a header and a tool disagree | The chip has `CLAMPS` and the assembler takes it, but GCC's own configuration says otherwise, so it emits `min` and `max` instead. `XCHAL_HAVE_FP_DIV` and `XCHAL_HAVE_FP_SQRT` mean the seed instructions exist, not `div.s` and `sqrt.s`. `q` is the stack-pointer constraint, and `q0` is not a register name. There is no `-mcpu=esp32s3`; the selector is `-mdynconfig=`. GCC emits no PIE instruction and has no Xtensa vector builtin. Every `[measured]` command in this leaf, plus its cited header line numbers and ISA manual pages, was rerun and confirmed on 2026-09-06; two counts were off (37, not 30, macro lines differ between drivers; 6, not 5, extra Espressif-only `--help=target` options) and both are now corrected. |
| [code-and-data-placement-in-esp-idf.md](./code-and-data-placement-in-esp-idf.md) | Putting code and data in a chosen memory | `IRAM_ATTR` and `DRAM_ATTR` are section attributes with a `__COUNTER__` suffix; `FORCE_IRAM_ATTR` adds `noinline`. Placement is really decided by `.lf` schemes and mappings, at archive, object or symbol granularity, which is why `-ffunction-sections -fdata-sections` are unconditional. Four `CONFIG_SPIRAM_*` options move data to PSRAM with no source change. `heap_caps_aligned_alloc(16, ...)` is how a vector table gets its alignment, because 128-bit accesses zero the low four address bits instead of faulting. The `.map` file is the only ground truth for where a symbol landed. |

## Not yet covered

- **LLVM and Clang on Xtensa** is covered in the frontier bucket:
  [LLVM and Clang for Xtensa](../08-frontiers/llvm-clang-for-xtensa-status-and-what-it-changes.md).
  What is still missing is a measured esp-clang against GCC 14 comparison
  on one kernel.
- **`-mextra-l32r-costs=n`.** A real Xtensa option that changes when GCC
  prefers a literal load over rematerialising a constant, which is exactly
  the tradeoff the constants section of the register-pressure leaf
  describes. Unmentioned, and its interaction with instruction RAM
  placement is unmeasured.
- **Function-attribute effects on the hardware loop.** `hot`, `cold` and
  per-function `optimize()` change the optimisation level for one
  function, and the loop leaf shows the level decides whether the hardware
  loop appears. Nobody has checked whether `optimize("O2")` on one
  function inside an `-Os` build restores it.
- **What `-fverbose-asm` and the RTL dumps cannot tell you.** The bucket
  documents reading the `.S` and the `loop2_doloop` dump. There is no
  guidance on the register-allocation dumps (`-fdump-rtl-ira`,
  `-fdump-rtl-reload`) that say which value lost and why.
- **The size cost of the flags.** Every claim here is about instruction
  selection or count in one function. Nothing measures what
  `-fno-jump-tables`, the stack protector or `-mlongcalls` cost across a
  whole image, which is the number that decides whether to change one.
- **Per-component flag overrides in practice.** The leaf lists the three
  mechanisms (`set_source_files_properties`, `target_compile_options`,
  `idf_component_set_property`) but does not say which survives a
  `reconfigure`, nor how to confirm the override reached the real command
  line other than by reading `compile_commands.json`.
- **The atomics workaround's actual hardware reason.** Left `[uncertain]`
  in the flags leaf after searching the vendored tree and the v5.5.1
  external RAM guide. An errata document would settle it.
