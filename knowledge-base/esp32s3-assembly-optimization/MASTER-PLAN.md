---
title: "ESP32-S3 assembly optimization KB: master plan"
id: master-plan
schema_version: 1
doc_type: reference
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, assembly, planning]
---

# ESP32-S3 assembly optimization KB: master plan

Goal: a citation-strict KB on making code fast on the ESP32-S3's Xtensa
LX7 cores, deep enough that an agent writing a kernel in this repo finds
the cost model, the instruction facts, the traps and the verification
ladder in one query instead of re-learning them on the device.

## Locked decisions (2026-09-06)

The user asked for the KB to be built without an interview, so these are
the coordinator's decisions, recorded so they can be changed deliberately.

| # | Decision | Value |
|---|---|---|
| 1 | KB root | `knowledge-base/esp32s3-assembly-optimization/` |
| 2 | qmd collection | `esp32s3-assembly-optimization-kb` |
| 3 | Bead | `gm-08i` |
| 4 | Scope | Xtensa LX7 scalar and PIE assembly, the memory hierarchy, GCC 14 codegen, measurement, kernel patterns. Panel DMA, WiFi/DRAM, LVGL are separate backlog topics |
| 5 | Slug prefix | `esp32s3-`: the load-bearing claims change on any other chip |
| 6 | Citation contract | Strict. ISA manual, TRM, source code with sha, GCC manual. `[measured]` for this repo's numbers, `[uncertain]` over fabrication |
| 7 | Company content | Segregated in `07-our-work/` |
| 8 | Frontier stance | A frontier bucket (`08-frontiers/`) for ESP32-P4, LLVM Xtensa, toolchain roadmaps. No far-future bucket: this is mature engineering |
| 9 | Downstream outputs | None pre-scaffolded. Likely later: a `/xtensa-kernel` skill that fronts the KB for kernel work |
| 10 | Execution mode | Autonomous: waves proceed on the coordinator's judgement; the user is not gating |
| 11 | Sizing | Narrow, single-discipline topic: 3 to 6 waves, 30 to 60 leaves, then adversarial and synthesis |
| 12 | Models | Sonnet default for leaves and mechanical verify lenses; Opus for the PIE semantics, cost-model, codegen and synthesis leaves and for fidelity verification |

## Taxonomy

See the layout table in `CLAUDE.md`. Eleven buckets plus bibliography.

## Wave plan

| Wave | Type | Buckets | Leaves |
|---|---|---|---|
| 1 | foundations and ISA | 00, 01, 02, 03 | 12 |
| 2 | toolchain, measurement, patterns | 04, 05, 06 | 12 |
| 3 | our-work | 07 | 8 |
| 4 | frontier and gaps from waves 1 to 3 | 08 plus reserve | 8 |
| 5 | adversarial survey | 09 | 3 |
| 6 | synthesis and consolidation | 10, bibliography, COMPENDIUM | 4 |

Each wave: write, then verify (resolvability and coherence on Sonnet,
fidelity on Opus for the load-bearing leaves), then fix, then a
leader-commit. Convergence, not a quota, decides whether a reserve wave
runs.
