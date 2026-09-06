---
title: "ESP32-S3 assembly optimization knowledge base"
id: readme
schema_version: 1
doc_type: reference
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, assembly, pie]
---

# ESP32-S3 assembly optimization knowledge base

A citation-strict reference for making code fast on the ESP32-S3's Xtensa
LX7 cores: the instruction set and its PIE vector extension, the memory
hierarchy that bounds both, the GCC 14 toolchain, and how to measure so
the number is the device's.

Start with [`MASTER-PLAN.md`](./MASTER-PLAN.md) for scope and
[`PROGRESS.md`](./PROGRESS.md) for the build state. Query with
`qmd query "<question>" -c esp32s3-assembly-optimization-kb`.

Buckets, in reading order:

- [`00-foundations/`](./00-foundations/) the core, the ABI, the cost model, the memory map
- [`01-scalar-isa/`](./01-scalar-isa/) the base instructions and the configured options
- [`02-pie-vector/`](./02-pie-vector/) the EE.* vector extension
- [`03-memory-hierarchy/`](./03-memory-hierarchy/) caches, SRAM, PSRAM, the MSPI bus
- [`04-toolchain-and-codegen/`](./04-toolchain-and-codegen/) GCC 14 Xtensa, inline asm, reading the output
- [`05-measurement/`](./05-measurement/) cycle counters, QEMU, bit-exact tests
- [`06-kernel-patterns/`](./06-kernel-patterns/) fixed point, gathers, RGB565, loop shapes
- [`07-our-work/`](./07-our-work/) this repo's kernels and rigs
- [`08-frontiers/`](./08-frontiers/) ESP32-P4, LLVM Xtensa, roadmaps
- [`09-adversarial/`](./09-adversarial/) myths, contradictions, open questions
- [`10-synthesis/`](./10-synthesis/) the decision flow and the budget framework
