---
title: "09-adversarial: bucket index"
id: 09-adversarial/readme
schema_version: 1
doc_type: reference
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, adversarial, index]
---

# 09-adversarial: bucket index

The bucket that attacks the rest. It records where the other buckets could
be wrong, where two sources disagree and how each disagreement was settled,
what the emulator gets wrong, and what nobody has measured. Read it before
trusting a number from anywhere else in the topic.

## Leaves

| File | Topic | Key claims |
|---|---|---|
| [contradiction-log-sources-that-disagree.md](./contradiction-log-sources-that-disagree.md) | Every place two sources disagree, with the resolution and the evidence | The TRM contradicts itself four times: QACC memory moves (1.5.3 against 1.8.20 to 1.8.23 and 1.8.60 to 1.8.63), the `EE.ST.ACCX.IP` and `EE.VLDBC.32.IP` syntax lines against their descriptions (the assembler sides with the descriptions), `ST.QR` printed as `LD.QR`, and the 1.7.1 worked example labelling stage 2 as W. The datasheet's four-way data cache with a 16 or 32 byte block disagrees with the TRM's 64 byte block and ESP-IDF's eight-way default. Header against tool: CLAMPS, the unaligned-load macros, the FP seed bits, `-mcpu`, the `q` constraint. The eight corpus corrections of the verify pass ranked by error kind: unread source 3, generalising from another core 2, experiment not run 2, fabricated citation 1. |
| [myths-about-optimizing-for-this-core.md](./myths-about-optimizing-for-this-core.md) | Seventeen pieces of received optimisation advice tested against the sources and the corpus | Fewer instructions is not faster on an in-order core with a 2-cycle load-use gap. A taken branch costs 2 cycles and there is no predictor. `-funroll-loops` removed the hardware loop and grew a 14-instruction function to 84 `[measured]`. There is no multi-lane gather and no 8-bit lane shift. `div.s`, `sqrt.s`, `dpfr`, `dpfl` and `ipf` are unknown opcodes to the assembler. The PSRAM default is quad at 40 MHz and the 120 MHz octal option's own help text says accesses crash after a temperature swing. The FreeRTOS tick on this chip is the system timer, not CCOUNT. Hand-written assembly does not beat GCC on principle: the device reversed four of thirteen kernels in the one recorded pass. |
| [qemu-versus-silicon-known-and-suspected-divergences.md](./qemu-versus-silicon-known-and-suspected-divergences.md) | Every way Espressif's QEMU can disagree with the chip when validating a kernel | The six open PIE issues (161 to 166) are the complete set and no PIE translator commit has landed since 2026-03-27. Issue 154 (CPENABLE 0 at reset) has an open pull request with the root cause in `cpu.c`; silicon resets to 0xff. There is no timing model anywhere in the cache, machine or DMA source. The `sel2` operand of `EE.SRCMB.*` and `EE.SRS.ACCX` is decoded and never used, so QEMU cannot say whether it does anything on the chip. A suspected new divergence: the PIE 32-bit shifts and multiplies can reuse a stale shift-amount shadow left by `SSL` or `SSA8B` in the same basic block; not device-tested. The low-4-bit address masking is faithful. |
| [uncertainty-register-what-the-corpus-does-not-know.md](./uncertainty-register-what-the-corpus-does-not-know.md) | All 146 `[uncertain]` tags in buckets 00 to 08, grouped, each with the experiment or document that would settle it | Ten groups; the largest are scalar and float per-instruction latencies (40), PIE resource reservation and undocumented stages (20), vendor roadmap (19) and cache, PSRAM and MSPI costs (18). The ten that would change the most kernel decisions start with the scalar multiply and divide latencies, the back-to-back `EE.VMUL` issue rate, PIE and scalar load-store port sharing on a PSRAM miss, and the flash and PSRAM cache-miss cost. Most need a CCOUNT loop on the device; a few need a document Cadence does not publish. |

## Not yet covered

- A device run of the two experiments QEMU cannot answer: the `sel2` operand and the `SSL`-then-`EE.VSL.32` shadow case.
- The register's count will drift as verifiers resolve tags; it is reconciled at each commit, not continuously.
