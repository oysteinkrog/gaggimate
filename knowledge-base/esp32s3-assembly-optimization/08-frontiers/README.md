---
title: "08-frontiers: bucket index"
id: 08-frontiers/readme
schema_version: 1
doc_type: reference
status: review
last_reviewed: 2026-09-06
tags: [esp32s3, frontiers, index]
---

# 08-frontiers: bucket index

Where the ground under the other buckets is moving: newer compilers, other
chips, vendor libraries and runtimes. Every leaf here carries a fetch date,
because most of its claims will age. Nothing in this bucket names this
repo's files or numbers.

## Leaves

| File | Topic | Key claims |
|---|---|---|
| [llvm-clang-for-xtensa-status-and-what-it-changes.md](./llvm-clang-for-xtensa-status-and-what-it-changes.md) | Xtensa in upstream LLVM and in Espressif's esp-clang fork, as of 2026-09-06 | Upstream Xtensa is an experimental target with no ESP32-S3 processor definition, no PIE, empty codegen for `LOOP` and one inline-asm constraint letter. Espressif's fork at the tag ESP-IDF pins (esp-21.1.3) has an esp32s3 definition, the full PIE instruction set, 252 `__builtin_xtensa_*` builtins (213 of them `ee_*`) and a hardware-loop pass. GNU ld still links Xtensa; an lld RFC from 2026-08 covers static linking only and had not merged as of 2026-09-06. Nobody has measured esp-clang against GCC 14 on a kernel. Every cited file, tag, commit sha and RFC thread was re-fetched and confirmed on 2026-09-06. |
| [gcc-xtensa-lineage-and-what-newer-releases-bring.md](./gcc-xtensa-lineage-and-what-newer-releases-bring.md) | Which GCC each ESP-IDF release ships, what Espressif patches, and what GCC 15 and 16 change | ESP-IDF 5.5.1 ships GCC 14.2.0; ESP-IDF 6.0 and 6.1 ship 15.2.0 and master 16.1.0. `-mdynconfig` is an Espressif-only switch on upstream loader machinery. The register allocator moved from Reload to LRA between GCC 13 and 14. `-mstrict-align` arrived in 14; `-mforce-l32` is on trunk and in no released Espressif branch. LTO is supported by the toolchain and disabled by ESP-IDF, unchanged from 13 to 16. |
| [esp-dsp-esp-nn-and-vendor-kernel-libraries.md](./esp-dsp-esp-nn-and-vendor-kernel-libraries.md) | esp-nn, esp-dl and esp-dsp as bodies of vendor PIE assembly and as a roadmap signal | Espressif's own docs call the ESP32-S3 unit "PIE V1" and the ESP32-P4 and ESP32-S31 unit "PIE V2" (different rounding). esp-dl carries 58 hand-written ESP32-S3 assembly files, the largest vendor body of S3 PIE code. esp-nn's dot product accumulates in `ACCX` while esp-dl's convolution uses `QACC`; both are right, for different output shapes. esp-nn issue 21 was a real unaligned-buffer bug on the S3, fixed with a scalar head loop. Two transferable rules from esp-dl's authoring guide: never place a kernel in `.iram1`, always ship aligned and unaligned entry points. |
| [esp32-p4-and-where-the-vector-work-moves.md](./esp32-p4-and-where-the-vector-work-moves.md) | What changes for kernel work on the ESP32-P4 (RISC-V) and what carries over from the S3 | The P4 unit is also called PIE: eight 128-bit integer vector registers, a 512-bit vector accumulator (256-bit halves against the S3's 160) and a 40-bit scalar one. Its instruction reference is a TRM chapter still marked to be added, so the manual rung of the ladder does not exist yet. The hardware loop is a separate extension with a six-instruction minimum body, no branches inside, two nesting levels. PIE is lazily switched like `CPENABLE` and ESP-IDF aborts on use from an ISR. GCC assembles the mnemonics under the confirmed `-march=rv32imafc_zicsr_zifencei_xesppie` string; Clang does not. ESP-IDF 5.5.1's own PIE context-save code drops one vector register (`q3`) from the save and restore list, a real bug fixed upstream but not backported to the branch this repository builds against. Flash and PSRAM have separate MSPI controllers, so the S3's shared-bus contention has no obvious P4 twin. esp-dsp's own table has the P4 winning on fixed point and losing on float, because its float kernels are scalar. |
| [rtos-and-runtime-effects-on-kernel-timing.md](./rtos-and-runtime-effects-on-kernel-timing.md) | What FreeRTOS, Zephyr, NuttX and bare metal each do to a kernel's measured time | The ESP-IDF tick is 100 Hz from SYSTIMER at interrupt level 1, staggered by half a period between cores. Task priority and interrupt level are independent axes; `portDISABLE_INTERRUPTS` masks up to level 3 only. Window and coprocessor exceptions are ISA-level and never deferred. A flash write stalls the other core and disables both caches, so any flash-cached code or PSRAM access on either core waits. Zephyr runs the S3 as two single-core images and does not document PIE state saving; NuttX has real SMP and an explicit lazy-coprocessor option. |

## Not yet covered

- A measured GCC 14 versus esp-clang comparison on one PIE kernel and one scalar kernel.
- A GCC 15 or 16 rebuild of a known kernel set, to see whether the compiler's baseline schedule moved.
- Whether the P4 and S31 "PIE V2" instruction set is a retarget of the S3 unit or a new design; Espressif's public comments do not settle it.
- Whether ESP-IDF 5.5.1's Xtensa-side PIE context save (the S3's own coprocessor-disabled handler) has an analogous missing-register gap to the P4/RISC-V `q3` bug this bucket found; not checked here, and worth a direct read of the S3 save routine in `00-foundations/coprocessors-cpenable-and-lazy-context.md`'s source material.
- Whether Zephyr's Xtensa port saves or restores CP3 (PIE) state on the ESP32-S3 at all; unconfirmed against the Zephyr SoC layer source.
- The per-exception cost, in cycles, of a window overflow/underflow handler and of a coprocessor-disabled handler; would settle two `[uncertain]` tags shared across this bucket and `00-foundations/`.
- The HP core's real clock ceiling on the ESP32-P4 (400 MHz datasheet versus 360 MHz TRM) and whether it varies by chip revision.
- Whether the P4 PIE unit has any floating-point vector capability; every published float kernel in `esp-dsp` is scalar RV32F, and the TRM calls the vector registers integer registers.
