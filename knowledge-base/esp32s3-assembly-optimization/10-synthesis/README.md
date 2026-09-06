---
title: "10-synthesis: bucket index"
id: 10-synthesis/readme
schema_version: 1
doc_type: reference
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, synthesis, index]
---

# 10-synthesis: bucket index

Three files that add no new facts. Each one arranges claims the other
buckets already carry, with a link on every claim, into a shape a
practitioner can act on: a decision procedure, a budget, and a watch list.

## Leaves

| File | Topic | Key claims |
|---|---|---|
| [should-this-kernel-be-hand-written.md](./should-this-kernel-be-hand-written.md) | Six gates with a stop or go at each, then a before-writing and a before-shipping checklist | Measure before assuming, since the per-call setup is paid 240 times a frame and a soft-float divide is 30 instructions. Fix table placement before scheduling: SRAM against PSRAM is 1.3x to 2x on the same kernel. Read the compiler's assembly for the four visible faults (lost hardware loop, unscheduled load-use, spills, libcalls) and try source-level fixes first. Check the vector unit can express the idea: no multi-lane gather, no 8-bit lane shift, 16-byte alignment prefix, stage-2 producers. Price the portable twin, the host goldens, the emulator check and the device A/B, knowing the device reversed four of thirteen kernels in the one recorded pass. Six things a kernel must never do, starting with writing CPENABLE. |
| [cycle-budget-framework-for-a-pixel-loop.md](./cycle-budget-framework-for-a-pixel-loop.md) | From a frame-rate target to cycles per pixel, a cost table with every row marked cited or uncertain, and a worked palette-lookup example | 240 MHz over 480x320 at 60 fps is 26 cycles a pixel, 13 for the kernel once the passes around it take their half. Cited rows: issue width one, load-use 2, taken branch 2, loop back edge free, PIE loads and multiplies define at stage 2, MAC chains free. Uncertain rows: MUL32 and DIV32 latency, window overflow cost, cache-miss cost. The palette loop measures 5 to 6 instructions a pixel against a 5-cycle issue floor plus about 1.5 cycles of amortised line fill. A quantised frame rate cannot be inverted into a kernel time; min-of-n lies when a row cache is involved. |
| [what-would-change-the-answers.md](./what-would-change-the-answers.md) | Seven external events that would shift the corpus, ranked by breadth times likelihood, each with a trigger to watch and what to re-run | Highest: a GCC 15 or 16 bump in this ESP-IDF line, since ESP-IDF 6.x already ships 15.2 and touches every GCC 14 specific (hardware-loop rules, LRA, the CLAMPS macro, no PIE emission). Then vendor kernels landing in esp-dl or esp-nn, esp-clang becoming supported, QEMU fixes for issues 154 and 161 to 166, a TRM revision, a move to the P4 or S31 (methodology and ladder survive, every Xtensa encoding leaf dies), and Cadence or Espressif publishing latencies (closes 60 of the open tags, least likely). Ends with a re-verify table. |

## Not yet covered

- A worked PIE kernel end to end (prologue, alignment guard, main loop, tail) as a template; the corpus gives semantics and idioms but no complete generic kernel.
- The cycle cost of a window overflow, which decides whether a call inside a hot loop matters.
- How two of the seven watch-list triggers landing together would compound.
