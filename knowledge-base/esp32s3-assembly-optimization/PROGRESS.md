---
title: "ESP32-S3 assembly optimization KB: progress"
id: progress
schema_version: 1
doc_type: reference
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, progress]
---

# Progress

| Date | Wave | What landed | New findings |
|---|---|---|---|
| 2026-09-06 | 0 | Harness ported from the monorepo; topic scaffolded (`CLAUDE.md`, `MASTER-PLAN.md`, buckets, install script); bead gm-08i | n/a |
| 2026-09-06 | 1 | Foundations and scalar ISA, 8 leaves in `00-foundations/` and `01-scalar-isa/` (commit 1cef2de8) | Windowed ABI leaves 14 usable registers in a leaf function; the hardware loop body limit is 256 bytes and GCC's refusal rules are in xtensa.cc; CPENABLE is SR 224 and the PIE save area is 208 bytes |
| 2026-09-06 | 2 | PIE, memory hierarchy, toolchain, measurement, kernel patterns, 16 leaves in `02` to `06` (commit 1cef2de8) | 128-bit PIE loads mask the low four address bits; EE.LDXQ.32 is a one-lane indexed load, there is no multi-lane gather; no 8- or 16-bit lane shifts, so a multiply stands in; GCC 14 has no PIE register class, so `"q"` is the stack pointer; memcpy on the S3 is a ROM routine (newlib's memcpy.S as built into the ROM, linked by an absolute symbol); QEMU's CCOUNT ticks at 40 MHz and has no timing model |
| 2026-09-06 | 3 | The our-work bucket, 9 leaves and an index in `07-our-work/` (commit 3008ba94) | The stray fuzz leaf was first written into the wrong repo and moved; the kernel pass covered 13 kernels while 14 animations are registered (Silk 2 came later) |
| 2026-09-06 | 4 | Verification pass over buckets 00 to 06 (in progress) and 10 frontier and gap leaves (in progress): P4, LLVM, GCC lineage, esp-nn, PIE hazard tables, branch costs, MAC16 and Boolean options, cache control, header-versus-tool contradictions, RTOS timing effects | Bucket 03 verifier fixed one unresolvable citation and wrote the bucket README |
