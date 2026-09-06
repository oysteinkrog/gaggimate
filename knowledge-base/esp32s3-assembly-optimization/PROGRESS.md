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
| 2026-09-06 | 4 (commit) | Verify pass over buckets 00 to 06 committed (4621722f); wave 4 committed (ba3abe9c): 10 leaves in `01`, `02`, `03`, `04` and `08-frontiers` plus the frontier index | memcpy on the S3 is a ROM routine linked by an absolute symbol; this core has no Xtensa cache instructions, the cache is a peripheral; GCC's own `__XCHAL_HAVE_CLAMPS` is 0 while the header says 1; esp-clang exposes PIE as 252 builtins and has a hardware-loop pass, GCC 14 has neither; ESP-IDF 6.x ships GCC 15.2 and master 16.1 |
| 2026-09-06 | 5 | The adversarial bucket, 4 leaves and an index in `09-adversarial/` (eb9d7f2b) | `-funroll-loops` removed the hardware loop and grew a 14 instruction function to 84; QEMU issue 154 has an open pull request with the root cause in `cpu.c`; the TRM contradicts itself four times; 146 open uncertainty tags grouped, 40 of them per-instruction latencies |
| 2026-09-06 | 6 | Wave 4 leaves verified (b7085b61 and the final commit); `10-synthesis/` (3 leaves and index), `COMPENDIUM.md` (40 facts), `sources.md` (745 footnotes, 60 external sources), `FINAL-CONVERGENCE-REPORT.md`; uncertainty register reconciled; registry set to converged | esp-clang has 252 PIE builtins, not 269; about 35 page cites in the branches leaf were off by one; the datasheet and TRM disagree on data cache geometry; -funroll-loops removes the hardware loop |
