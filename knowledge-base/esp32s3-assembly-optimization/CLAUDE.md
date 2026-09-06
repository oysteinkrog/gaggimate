# knowledge-base/esp32s3-assembly-optimization/: agent harness

This file is the agent contract for everything under this directory.
Claude Code loads it when you cross into the subtree. Read it once per
session before adding, editing or citing anything here. The shared
conventions (anti-patterns, validation, retrieval) are in
[`../CLAUDE.md`](../CLAUDE.md) and are not repeated.

Durable content: [`MASTER-PLAN.md`](./MASTER-PLAN.md) (the locked plan),
[`README.md`](./README.md) (reader-facing intro), [`PROGRESS.md`](./PROGRESS.md)
(wave log), [`COMPENDIUM.md`](./COMPENDIUM.md) (the forty load-bearing
facts, one link each, the place to start reading), [`sources.md`](./sources.md)
(every external source the corpus cites, pinned by version or fetch date),
and [`FINAL-CONVERGENCE-REPORT.md`](./FINAL-CONVERGENCE-REPORT.md) (what
the build converged on and what it left open).

## 1. Purpose

Research KB for **making code fast on the ESP32-S3's Xtensa LX7 cores**:
hand-written and compiler-generated assembly, the PIE vector unit, the
memory hierarchy that bounds both, the GCC 14 toolchain that emits the
code, and how to measure a kernel so the number is the device's, not the
host's. The target is the chip this repo ships on: dual-core LX7 at
240 MHz, 16 KB icache, 32 KB dcache, octal PSRAM on the MSPI bus shared
with flash, toolchain `xtensa-esp-elf` GCC 14.2 (crosstool-NG
esp-14.2.0_20241119), ESP-IDF 5.x.

Two readers:

| Reader | Needs | Entry |
|---|---|---|
| An agent about to write or tune a kernel in this repo | The cost model, the instruction facts, the traps, the verification ladder | `qmd query -c esp32s3-assembly-optimization-kb`, then the bucket README, then the leaf |
| A person deciding whether assembly is worth it for a hot path | The decision flow, the measured wins and losses, the budget arithmetic | `10-synthesis/`, then `07-our-work/` |

Not in scope: the RGB panel scan-out and DMA (a backlog topic), WiFi and
DRAM budgeting (backlog), LVGL rendering (backlog), RISC-V ESP32 parts
except as a frontier comparison, and anything about what the animations
should look like.

## 2. Layout

```
00-foundations/            LX7 core: pipeline, register windows and the windowed ABI, exceptions, coprocessors and lazy context, the cycle cost model, the memory map
01-scalar-isa/             base ISA and configured options: LOOP, MUL32, MIN/MAX, SEXT, CLAMPS, NSA, funnel shifts, conditional moves, the FP option and what it lacks, load/store forms, branch costs
02-pie-vector/             the EE.* extension: q registers, SAR, load/store variants and address masking, multiply/add/saturate, zip/unzip, what is missing, context save rules
03-memory-hierarchy/       icache/dcache geometry, IRAM/DRAM/RTC/PSRAM, the shared MSPI bus, miss and load-use costs, alignment, placement attributes
04-toolchain-and-codegen/  GCC 14 Xtensa: flags, inline asm constraints and clobbers, attributes, when the zero-overhead loop is emitted, register pressure and spills, reading .S, LLVM status
05-measurement/            CCOUNT and esp_timer, min-of-n, preemption and interrupts, QEMU fidelity, bit-exact reference tests, fuzzing with sanitizers, host versus device
06-kernel-patterns/        fixed-point formats, LUT gathers and palettes, RGB565 arithmetic, incremental stepping, interleaving for load-use, pixel pairs, row-state builders, esp-dsp as reference kernels
07-our-work/               this repo: the animation kernel pass (13 kernels in 2026-09, 14 animations registered), the animbench ladder, kblob, qemubench, the hot slab, what the device reversed
08-frontiers/              ESP32-P4 and its vector extension, esp-dsp and toolchain roadmaps, LLVM Xtensa upstreaming, GCC 15, research on in-order DSP scheduling
09-adversarial/            myths, contradictions, the uncertainty log
10-synthesis/              the decision flow (assembly or not), the cycle budget framework, cross-bucket narrative, where the frontier is moving
bibliography/              consolidated sources
scripts/install-collection.sh
```

Placement rules: pick the most specific bucket, never duplicate across
buckets. `07-our-work/` is a hard boundary: anything that names this
repo's files, rigs, measurements or commits goes there and nowhere else,
so the other buckets stay a teaching corpus for any ESP32-S3 project.
Each bucket has a `README.md` index listing its leaves and their key
claims; update it when a leaf lands.

## 3. Citation contract (strict)

Every non-obvious claim carries a verifiable source. No citation, no
claim. Primary sources, in order of preference:

1. Cadence/Tensilica *Xtensa Instruction Set Architecture (ISA) Reference
   Manual* (cite the edition and section).
2. Espressif *ESP32-S3 Technical Reference Manual* (cite version and
   section or table) and *ESP32-S3 Datasheet* (version and table).
3. Source code: esp-idf, esp-dsp, GCC, the toolchain's `xtensa-config`
   headers, QEMU. Cite repository, path, and a tag or commit sha.
4. GCC 14 manual sections (Xtensa options, extended asm).
5. Espressif documentation pages and forum posts, and papers, when they
   are the only source; say so.

| Tag | When |
|---|---|
| `[Source Year][^n]` | All non-obvious factual claims |
| `[measured]` | A number taken on this repo's hardware or QEMU. Give the commit sha, the file or log, and the date. Only in `07-our-work/` unless the number is generic enough to state without repo names |
| `[experience]` | An empirical observation with no formal source |
| `[uncertain]` | Could not confirm against a primary source. Better than fabrication |
| `[obvious]` | Implicit; do not cite that a 32-bit register holds 32 bits |

Footnote: source, year, title or section, URL or repo path, edition or sha.
Instruction semantics need the ISA manual or an executed test; a forum post
about an instruction is `[uncertain]` until one of those confirms it.

## 4. Frontmatter (required on every leaf)

```yaml
---
title: <human-readable title>
id: <bucket>/<slug>
schema_version: 1
doc_type: reference | explanation | tutorial | how-to
status: draft | review | stable
last_reviewed: YYYY-MM-DD
tags: [esp32s3, xtensa, ...]
confidence: low | medium | high
---
```

Validate with `python3 ../scripts/validate-frontmatter.py .` from this
directory. Warnings are commit-blockers.

## 5. Anti-patterns (topic-specific)

Shared anti-patterns in `../CLAUDE.md` apply. Additions:

- **Instruction counts as timings.** An instruction count is evidence about
  code shape. A cycle claim needs a device or QEMU measurement, or the ISA
  manual's latency for that instruction, and says which.
- **Host benchmarks as device claims.** x86 hides load-use stalls, spills
  and cache misses. A host number may say where time goes; it never says
  how much.
- **PIE semantics from memory.** Every EE.* fact cites the TRM or a QEMU
  or device test that executed it. Undocumented behaviour (address-bit
  masking) is `[measured]` with the test named.
- **Unversioned toolchain claims.** "GCC emits a hardware loop here" is
  true of one GCC version and one flag set. Name both.
- **Cycle costs without the memory the operand lives in.** SRAM, IRAM,
  flash-cached and PSRAM-cached reads are different numbers. Say which.
- **Repo names outside `07-our-work/`.** Generic buckets do not mention
  animations, animbench, kblob or GaggiMate.

## 6. Quality gates

Before any commit in this subtree: frontmatter validates; every
non-`[obvious]`, non-`[experience]` claim has a footnote or an
`[uncertain]`; footnote URLs resolve (the verify pass checks; a broken
citation is a defect); no repo names outside `07-our-work/`; commit
message `kb(esp32s3-asm): <what>`.

## 7. qmd registration

```bash
./scripts/install-collection.sh
qmd query "<question>" -c esp32s3-assembly-optimization-kb --limit 10
```
