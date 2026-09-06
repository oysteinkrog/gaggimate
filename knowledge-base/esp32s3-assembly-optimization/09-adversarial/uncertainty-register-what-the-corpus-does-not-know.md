---
title: Uncertainty register - what the corpus does not know
id: 09-adversarial/uncertainty-register-what-the-corpus-does-not-know
schema_version: 1
doc_type: reference
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, adversarial, uncertainty, gaps]
confidence: medium
---

# Uncertainty register: what the corpus does not know

Every other bucket in this knowledge base marks a claim `[uncertain]` when
it could not confirm that claim against a primary source. This leaf
collects every one of those tags into one place, groups them by theme, and
names the one thing that would settle each. Read the corpus first: it
takes each `[uncertain]` seriously rather than filling it with a guess,
and this register is what that discipline produces when you count it up.

A search of the corpus (buckets `00` through `08`, about 55 leaf and
`README.md` files) found 146 `[uncertain]` tags on an early pass on
2026-09-06, excluding the two mentions inside this topic's own
`CLAUDE.md` and `MASTER-PLAN.md`, which describe the tag convention
rather than tag a claim. This register was reconciled once against a
later state of the same corpus, once the rest of the topic's build had
settled for the day: the same search found 138 tags across 39 files.
Ten of the first 146 rows had been resolved by then (a claim settled
with a citation or a device fact, so the tag came off); three new rows
appeared, two of them in leaves that did not exist at the first count.
Every row below traces to one of the 138 tags live at reconciliation.
Several rows repeat the same underlying question because a bucket
`README.md` restates a leaf's open question in its own summary, or
because two leaves hit the same gap from different angles (a PIE hazard
and a fixed-point format table, for instance). Repetition is left
visible rather than collapsed, because it shows which gaps the corpus
keeps bumping into.

This bucket and `10-synthesis/` are excluded from the count by design,
the same way `09-adversarial/` excludes its own tags from itself: this
leaf catalogues buckets `00` through `08` only. Counted the same way at
reconciliation, `09-adversarial/` carries 20 `[uncertain]` tags of its
own and `10-synthesis/` carries 16; neither set is a row below.[^2]

## How to read this table

Each row names the leaf, states the claim as the corpus tagged it, and
names one settling method:

- **device measurement**: a specific instrument this corpus already
  documents, most often [CCOUNT](../05-measurement/ccount-cycle-counter-and-timing-a-kernel.md)
  (a device or QEMU test that runs the operation in a timed loop), a
  [bit-exact or fuzz test](../05-measurement/bit-exact-reference-tests-and-fuzzing.md),
  or [QEMU execution](../05-measurement/qemu-esp32s3-what-it-proves-and-what-it-cannot.md).
- **toolchain test**: a compiler or assembler invocation that answers the
  question without touching hardware; the command is given.
- **document not yet read**: a specific named document, issue thread or
  source file that this corpus did not open, could not reach, or read
  only through a secondary summary.
- **unknowable here**: settling it needs data this corpus has no path to,
  typically the unpublished Cadence LX7 processor data book, an
  Espressif internal errata document, or hardware this repository does
  not have (ESP32-P4 silicon).

## 1. Scalar and floating-point per-instruction latencies (41 tags)

The Xtensa ISA manual gives correctness and pipeline structure, not
per-instruction cycle counts, and says so directly: it defers timing to
"a specific Xtensa processor data book," which is not openly published
for the LX7. Every scalar or floating-point latency this corpus could not
get from a table therefore carries this tag, in one framing statement and
40 specific instructions or table rows.

| Leaf | Claim as tagged | Settling |
|---|---|---|
| [scalar arithmetic, shifts and bit tricks](../01-scalar-isa/scalar-arithmetic-shifts-and-bit-tricks.md) | Cycle costs are out of scope in general: Cadence publishes no per-instruction Xtensa latencies | unknowable here (LX7 data book) |
| [scalar arithmetic, shifts and bit tricks](../01-scalar-isa/scalar-arithmetic-shifts-and-bit-tricks.md) | `mull`/`quos` are not fixed-cost; no per-core LX7 figure published | device measurement, CCOUNT loop |
| [zero-overhead loops](../01-scalar-isa/zero-overhead-loops.md) | The loop-back cost itself, distinct from the taken-branch cost the loop replaces (now cited at 2 cycles, TRM 1.7.3), is still unstated anywhere | device measurement, CCOUNT loop |
| [kernel patterns README](../06-kernel-patterns/README.md) | Every scalar cycle count in the fixed-point leaf is uncertain because the manual publishes none | device measurement, CCOUNT loop |
| [kernel patterns README](../06-kernel-patterns/README.md) | Whether `MUL16S` and `MULL` share a latency on this part | device measurement, CCOUNT loop |
| [kernel patterns README](../06-kernel-patterns/README.md) | The tie rule for `ROUND.S` (manual says "nearest," does not define its own `rounds()` helper) | device measurement, CCOUNT loop on a tie-value input |
| [LX7 core pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md) | The LX7 data book is unpublished (HTTP 403 on fetch), so every non-structural latency below is uncertain | unknowable here (LX7 data book) |
| [LX7 core pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md) | The actual LX7 issue-to-issue cost of `QUOS` | device measurement, CCOUNT loop |
| [LX7 core pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md) | Framing: every cost-table row is either structural and cited, or marked uncertain | unknowable here (LX7 data book) |
| [LX7 core pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md) | `MULL`, `MULSH`, `MULUH` result latency | device measurement, CCOUNT loop |
| [LX7 core pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md) | `QUOS`, `QUOU`, `REMS`, `REMU` latency | device measurement, CCOUNT loop |
| [LX7 core pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md) | `ADD.S`, `SUB.S`, `MUL.S`, `MADD.S` result latency | device measurement, CCOUNT loop |
| [LX7 core pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md) | Float compare (`OLT.S` and friends) latency into a Boolean register | device measurement, CCOUNT loop |
| [LX7 core pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md) | `FLOAT.S`, `TRUNC.S` conversion latency | device measurement, CCOUNT loop |
| [LX7 core pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md) | Float divide total cycle cost (a windowed call into a 30-instruction routine) | device measurement, CCOUNT loop |
| [LX7 core pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md) | `sqrtf` total cycle cost (a C library call) | device measurement, CCOUNT loop |
| [LX7 core pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md) | Framing: every uncertain row above is measurable on the device | device measurement, CCOUNT loop |
| [floating-point option on LX7](../01-scalar-isa/floating-point-option-on-lx7.md) | No per-instruction cycle-latency table for `add.s`, `mul.s`, `madd.s` or a compare | device measurement, CCOUNT loop |
| [MAC16, Boolean and other configured options](../01-scalar-isa/mac16-boolean-and-other-configured-options.md) | No per-core LX7 latency for `MULL` or `QUOS` published anywhere consulted | device measurement, CCOUNT loop |
| [MAC16, Boolean and other configured options](../01-scalar-isa/mac16-boolean-and-other-configured-options.md) | No LX7 cycle count for `RSIL` | device measurement, CCOUNT loop (disable/enable pair back to back) |
| [ESP32-S3 memory map and address spaces](../00-foundations/esp32s3-memory-map-and-address-spaces.md) | Cycle cost of the software handler for an IRAM byte access on the S3 (only the ESP32-original figure, 167 cycles, is published) | device measurement, CCOUNT loop around the handler |
| [branches, jumps and control-flow costs](../01-scalar-isa/branches-jumps-and-control-flow-costs.md) | Whether any other microarchitectural mechanism (instruction prefetch buffering) softens the fixed 2-cycle taken-branch cost in practice | device measurement, CCOUNT loop across branch densities |
| [fixed-point arithmetic on LX7](../06-kernel-patterns/fixed-point-arithmetic-on-lx7.md) | The tie rule for `ROUND.S` (repeats the kernel-patterns README claim) | device measurement, CCOUNT loop |
| [fixed-point arithmetic on LX7](../06-kernel-patterns/fixed-point-arithmetic-on-lx7.md) | Framing: the manual publishes no scalar latencies and calls the multiply algorithm implementation-dependent, so every scalar cycle count in the table below is uncertain | unknowable here (LX7 data book) |
| [fixed-point arithmetic on LX7](../06-kernel-patterns/fixed-point-arithmetic-on-lx7.md) | Scalar cycles, Q15 x Q15 truncating (`mul16s`, `srai 15`) | device measurement, CCOUNT loop |
| [fixed-point arithmetic on LX7](../06-kernel-patterns/fixed-point-arithmetic-on-lx7.md) | Scalar cycles, Q15 x Q15 round half up | device measurement, CCOUNT loop |
| [fixed-point arithmetic on LX7](../06-kernel-patterns/fixed-point-arithmetic-on-lx7.md) | Scalar cycles, Q16.16 x Q16.16 general (`mull`, `mulsh`, `ssai 16`, `src`) | device measurement, CCOUNT loop |
| [fixed-point arithmetic on LX7](../06-kernel-patterns/fixed-point-arithmetic-on-lx7.md) | Scalar cycles, Q16.16 x 16-bit constant | device measurement, CCOUNT loop |
| [fixed-point arithmetic on LX7](../06-kernel-patterns/fixed-point-arithmetic-on-lx7.md) | Scalar cycles, Q1.31 x Q1.31 | device measurement, CCOUNT loop |
| [fixed-point arithmetic on LX7](../06-kernel-patterns/fixed-point-arithmetic-on-lx7.md) | Scalar cycles, unsigned high multiply (`muluh`) | device measurement, CCOUNT loop |
| [fixed-point arithmetic on LX7](../06-kernel-patterns/fixed-point-arithmetic-on-lx7.md) | Scalar cycles, clamp to signed 16 bits (`clamps`, inline asm) | device measurement, CCOUNT loop |
| [fixed-point arithmetic on LX7](../06-kernel-patterns/fixed-point-arithmetic-on-lx7.md) | Scalar cycles, clamp to arbitrary bounds (`min`, `max`) | device measurement, CCOUNT loop |
| [fixed-point arithmetic on LX7](../06-kernel-patterns/fixed-point-arithmetic-on-lx7.md) | Scalar cycles, sign-extend a packed field (`sext`) | device measurement, CCOUNT loop |
| [fixed-point arithmetic on LX7](../06-kernel-patterns/fixed-point-arithmetic-on-lx7.md) | Scalar cycles, lerp (`sub`, `mul16s`, `srai`, `add`) | device measurement, CCOUNT loop |
| [fixed-point arithmetic on LX7](../06-kernel-patterns/fixed-point-arithmetic-on-lx7.md) | Scalar cycles, normalise for reciprocal (`nsau`) | device measurement, CCOUNT loop |
| [fixed-point arithmetic on LX7](../06-kernel-patterns/fixed-point-arithmetic-on-lx7.md) | Scalar cycles, float to Q15 (`trunc.s`) | device measurement, CCOUNT loop |
| [fixed-point arithmetic on LX7](../06-kernel-patterns/fixed-point-arithmetic-on-lx7.md) | Scalar cycles, Q15 to float (`float.s`) | device measurement, CCOUNT loop |
| [foundations README](../00-foundations/README.md) | Key claim: every latency the manual does not give is uncertain, with the CCOUNT method to settle it (LX7 pipeline leaf summary) | device measurement, CCOUNT loop |
| [foundations README](../00-foundations/README.md) | Open question: closing the cost-table gaps needs a measurement campaign, not another document | device measurement, CCOUNT loop |
| [our-work README](../07-our-work/README.md) | Measured cycle costs for the scalar multiply, divide and FP instructions on this board are what the generic buckets above leave uncertain | device measurement, CCOUNT loop (tracked as its own leaf in `07-our-work/`) |
| [host versus device reversals measured here](../07-our-work/host-versus-device-reversals-measured-here.md) | Whether the hand-written kernel currently shipped for one of the two cases that won on paper and lost on the chip is the same version that produced the measured ratio, or a later rewrite; not recorded in the commit history read for this leaf | document not yet read, a closer read of the commit history around the file this leaf names |

## 2. PIE vector unit: resource reservation and undocumented stages (20 tags)

TRM Table 1.7-2 gives pipeline stages for most `EE.*` instructions but
omits several groups entirely, and never states the issue gap two
instructions competing for one execution unit need. That gap is the
single largest undocumented area in the PIE bucket.

| Leaf | Claim as tagged | Settling |
|---|---|---|
| [kernel patterns README](../06-kernel-patterns/README.md) | The write stage of a scalar `WSR.SAR` relative to the first `EE.VMUL.S16` that reads it | device measurement, CCOUNT loop varying the distance |
| [kernel patterns README](../06-kernel-patterns/README.md) | Whether `EE.LDXQ.32` treats its index lane as signed or unsigned | device measurement, one executed test with a negative index |
| [LUT gathers, palettes and RGB565](../06-kernel-patterns/lut-gathers-palettes-and-rgb565.md) | Whether the `EE.LDXQ.32` index is signed or unsigned (repeats the README claim) | device measurement, one executed test |
| [PIE hazards, latencies and issue rules](../02-pie-vector/pie-hazards-latencies-and-issue-rules.md) | Def stage of `WSR.SAR`, `SSL`, `SSR`, `SSAI`, `SSA8B`, `SSA8L`, none of which Table 1.7-2 covers | device measurement, CCOUNT loop |
| [PIE hazards, latencies and issue rules](../02-pie-vector/pie-hazards-latencies-and-issue-rules.md) | Stages of `RUR.*` and `WUR.*`, also absent from the table | device measurement, CCOUNT loop |
| [PIE hazards, latencies and issue rules](../02-pie-vector/pie-hazards-latencies-and-issue-rules.md) | Whether two `EE.VMUL.S16` instructions can issue on consecutive cycles | device measurement, CCOUNT loop, dense chain vs. filler chain |
| [PIE hazards, latencies and issue rules](../02-pie-vector/pie-hazards-latencies-and-issue-rules.md) | Whether a fused form (`EE.VADDS.S16.LD.INCP`) avoids a resource stall two separate instructions would hit | device measurement, CCOUNT loop, fused vs. split |
| [PIE hazards, latencies and issue rules](../02-pie-vector/pie-hazards-latencies-and-issue-rules.md) | Open question restating the above: back-to-back throughput of `EE.VMUL.*` and `EE.VMULAS.*` | device measurement, CCOUNT loop |
| [PIE hazards, latencies and issue rules](../02-pie-vector/pie-hazards-latencies-and-issue-rules.md) | `LD.QR`, `ST.QR`, `MV.QR` have no row in Table 1.7-2; their stages | device measurement, CCOUNT loop against `EE.VLD.128.IP` |
| [PIE hazards, latencies and issue rules](../02-pie-vector/pie-hazards-latencies-and-issue-rules.md) | Open question restating `RUR.*`/`WUR.*` stages | device measurement, CCOUNT loop |
| [PIE hazards, latencies and issue rules](../02-pie-vector/pie-hazards-latencies-and-issue-rules.md) | Issue distance from `SSAI` or `WSR.SAR` to a following `EE.VMUL.S16` | device measurement, CCOUNT loop varying the distance |
| [PIE arithmetic: multiply, saturate and shuffle](../02-pie-vector/pie-arithmetic-multiply-saturate-and-shuffle.md) | What the undocumented `sel` bit does on silicon for `EE.SRCMB.*`/`EE.SRS.ACCX` (manual, QEMU and esp-dsp disagree or stay silent) | device measurement, execute both bit values and diff |
| [PIE load, store and alignment](../02-pie-vector/pie-load-store-and-alignment.md) | Whether the PIE unit has a separate port or bypass for PSRAM access, or shares the scalar load-store path | device measurement, CCOUNT loop, PIE load vs. scalar load under a PSRAM miss |
| [PIE load, store and alignment](../02-pie-vector/pie-load-store-and-alignment.md) | Whether a 128-bit PIE access to an instruction-bus address is serviced correctly | device measurement, one executed test against an IRAM address |
| [PIE vector README](../02-pie-vector/README.md) | Pipeline stages for `LD.QR`, `ST.QR`, `MV.QR`, `RUR.*`, `WUR.*` (repeats the hazards-leaf claim) | device measurement, CCOUNT loop |
| [PIE vector README](../02-pie-vector/README.md) | Whether a 128-bit PSRAM load beats four scalar loads, and whether PIE shares the scalar load-store port | device measurement, CCOUNT loop |
| [PIE vector README](../02-pie-vector/README.md) | Whether a 128-bit PIE access to an instruction-bus address works (repeats the load-store leaf claim) | device measurement, one executed test |
| [PIE register file, SAR and context](../02-pie-vector/pie-register-file-sar-and-context.md) | Reset values of the PIE state registers (given as 0 for the save area, not stated for PIE itself) | device measurement, read cold after reset |
| [PIE register file, SAR and context](../02-pie-vector/pie-register-file-sar-and-context.md) | Same claim, restated in the leaf's closing note | device measurement, read cold after reset |
| [fixed-point arithmetic on LX7](../06-kernel-patterns/fixed-point-arithmetic-on-lx7.md) | The write side of the `SAR` hand-off to `EE.VMUL.S16` (Table 1.7-2 gives only the read side) | device measurement, CCOUNT loop |

## 3. Cache, PSRAM bandwidth and MSPI bus costs (18 tags)

The TRM states the cache and bus topology but not a sustained-throughput
or miss-cost figure, and documents preload and lock as existing without
a usage contract.

| Leaf | Claim as tagged | Settling |
|---|---|---|
| [kernel patterns README](../06-kernel-patterns/README.md) | Whether an unaligned `S32I` behaves as a plain 32-bit store in every memory region, cached external memory in particular | device measurement, unaligned store sweep across SRAM, cached flash, cached PSRAM |
| [LUT gathers, palettes and RGB565](../06-kernel-patterns/lut-gathers-palettes-and-rgb565.md) | Same unaligned-`S32I`-in-cached-external-memory claim | device measurement, unaligned store sweep |
| [cache control: preload, lock and writeback](../03-memory-hierarchy/cache-control-preload-lock-and-writeback.md) | A preload racing a concurrent writeback or invalidate of the same line is not addressed by the header | device measurement, deliberately racing preload against a writeback |
| [cache control: preload, lock and writeback](../03-memory-hierarchy/cache-control-preload-lock-and-writeback.md) | No published cycle number for preload throughput versus an ordinary demand miss | device measurement, CCOUNT loop, preload vs. demand-miss |
| [cache control: preload, lock and writeback](../03-memory-hierarchy/cache-control-preload-lock-and-writeback.md) | No Kconfig option or register field names the auto-preload threshold or window size | document not yet read, TRM auto-preload register fields not enumerated here |
| [cache control: preload, lock and writeback](../03-memory-hierarchy/cache-control-preload-lock-and-writeback.md) | The lock convention: whether it needs an interrupt disabled, is safe against a concurrent miss on the same set, and what happens on overflow | document not yet read, an Espressif application note on cache locking, if one exists |
| [cache control: preload, lock and writeback](../03-memory-hierarchy/cache-control-preload-lock-and-writeback.md) | Whether re-occupying cache ways from application code (not just at boot) is supported | document not yet read, an errata or app note; IDF itself only calls it once at startup |
| [memory hierarchy README](../03-memory-hierarchy/README.md) | Whether the DCache has a write-through mode | document not yet read, no register bit or TRM statement found so far |
| [code and data placement in ESP-IDF](../04-toolchain-and-codegen/code-and-data-placement-in-esp-idf.md) | The precise trigger list for cache-disable windows beyond flash erase/write and `ESP_INTR_FLAG_IRAM` handlers | document not yet read, the v5.5.1 external-RAM guide names only those two |
| [caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md) | No sustained (not peak theoretical) PSRAM bandwidth figure published | document not yet read, an Espressif PSRAM app note; otherwise device measurement |
| [caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md) | No published cycle range for a flash-cache miss on this part | device measurement, CCOUNT loop with the `EXTMEM_*` miss counters the TRM documents |
| [caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md) | No write-through mode bit found in the register header or TRM chapter (repeats the memory-hierarchy README claim) | document not yet read |
| [caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md) | Flash-code cache miss: full cost unpublished (table row) | device measurement, CCOUNT loop with miss counters |
| [caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md) | PSRAM cache miss: full cost unpublished (table row) | device measurement, CCOUNT loop with miss counters |
| [caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md) | Closing note: the two unpublished-cost rows above are the ones worth measuring on a specific board | device measurement, CCOUNT loop with miss counters |
| [ESP32-S3 memory map and address spaces](../00-foundations/esp32s3-memory-map-and-address-spaces.md) | Framing: the address-range table marks any figure not confirmed against a primary source | document not yet read (varies per row) |
| [LX7 core pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md) | The magnitude of a cache miss in core cycles depends on what the other core and any bus master are doing | device measurement, CCOUNT loop under deliberate cross-core contention |
| [LX7 core pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md) | Load or store, cache miss to flash or PSRAM (cost-table row, repeats the caches-leaf claim) | device measurement, CCOUNT loop with miss counters |

## 4. Toolchain and codegen internals (15 tags)

These are questions about GCC 14.2 and the vendored dynamic configuration
(`dynconfig`) that a compiler invocation, not a device run, would answer.

| Leaf | Claim as tagged | Settling |
|---|---|---|
| [zero-overhead loops](../01-scalar-isa/zero-overhead-loops.md) | What the 256-byte `XCHAL_LOOP_BUFFER_SIZE` does for instruction fetch on this part | document not yet read, confirming against a primary source beyond the header's own gloss |
| [zero-overhead loops](../01-scalar-isa/zero-overhead-loops.md) | No interaction found between `-mlongcalls` and hardware loops | toolchain test: `xtensa-esp32s3-elf-gcc -mlongcalls -O2 -S` on a loop with an out-of-range call target, diff against without the flag |
| [toolchain and codegen README](../04-toolchain-and-codegen/README.md) | The atomics workaround's actual hardware reason | document not yet read, an Espressif/Cadence errata document for `S32C1I` against PSRAM addresses |
| [GCC 14 Xtensa flags and what they cost](../04-toolchain-and-codegen/gcc14-xtensa-flags-and-what-they-cost.md) | The function or file size at which `-mauto-litpools` starts to matter for correctness | toolchain test: compile a function large enough to exhaust `L32R`'s range with and without the flag, check for an assembler error |
| [GCC 14 Xtensa flags and what they cost](../04-toolchain-and-codegen/gcc14-xtensa-flags-and-what-they-cost.md) | The exact hardware defect `-mno-disable-hardware-atomics`'s scoped exception works around | document not yet read, same errata document as above |
| [GCC 14 Xtensa flags and what they cost](../04-toolchain-and-codegen/gcc14-xtensa-flags-and-what-they-cost.md) | Whether a project can opt back into LTO per-target with a matching linker override | toolchain test: a two-component CMake build overriding `link_options` locally |
| [MAC16, Boolean and other configured options](../01-scalar-isa/mac16-boolean-and-other-configured-options.md) | Why GCC's `-O2` MAC16 shape hoists `wsr.acclo`/`rsr.acclo` outside the loop while `-Os` does not | toolchain test: `-O2 -S` versus `-Os -S` on the same MAC16 loop, diff the RTL dump (`-fdump-rtl-all`) |
| [code and data placement in ESP-IDF](../04-toolchain-and-codegen/code-and-data-placement-in-esp-idf.md) | How much scheduling freedom `restrict` is worth on this target (it buys no vectorization) | toolchain test: `-O2 -S` with and without `restrict` on a representative loop, compare instruction count and schedule |
| [register windows and the windowed ABI](../00-foundations/register-windows-and-windowed-abi.md) | No single number for how many scalars GCC will keep live before spilling around an `asm` block | toolchain test: vary live scalar count around a fixed `asm` block, read the generated assembly for a spill |
| [GCC/Xtensa lineage and what newer releases bring](../08-frontiers/gcc-xtensa-lineage-and-what-newer-releases-bring.md) | Whether the earlier reload-based register allocator handled the windowed ABI's constraints differently enough to matter | toolchain test: build the same kernel with an older GCC (reload-based) and current GCC 14 (LRA), diff the assembly |
| [GCC/Xtensa lineage and what newer releases bring](../08-frontiers/gcc-xtensa-lineage-and-what-newer-releases-bring.md) | Whether interprocedural optimization (`-fipa-*`) ever behaves differently on Xtensa (a code search found no on-topic patch, which is not proof of absence) | document not yet read, a closer read of `gcc-mirror/gcc`'s Xtensa backend for IPA hooks |
| [what the headers say versus what the tools do](../04-toolchain-and-codegen/what-the-headers-say-versus-what-the-tools-do.md) | Whether Espressif treats `__XCHAL_HAVE_CLAMPS=0` in the dynconfig as a defect or a deliberate choice | document not yet read, an issue or commit in the toolchain's own repository |
| [what the headers say versus what the tools do](../04-toolchain-and-codegen/what-the-headers-say-versus-what-the-tools-do.md) | Whether the silicon implements `CLAMPS` at all, at the strength a header-and-emulator agreement can reach | device measurement, run the disabled-instruction test on real hardware and check for an illegal-instruction exception |
| [what the headers say versus what the tools do](../04-toolchain-and-codegen/what-the-headers-say-versus-what-the-tools-do.md) | Whether `-mno-strict-align` is safe for a whole ESP-IDF build, unaligned access in cached external memory specifically | device measurement, unaligned load/store sweep across SRAM, cached flash, cached PSRAM |
| [what the headers say versus what the tools do](../04-toolchain-and-codegen/what-the-headers-say-versus-what-the-tools-do.md) | Why the dynconfig leaves four related macros undefined rather than setting them | document not yet read, a field-by-field read of the dynconfig structure |

## 5. Measurement methodology gaps (7 tags)

Even the instrument this corpus recommends above has its own unmeasured cost.

| Leaf | Claim as tagged | Settling |
|---|---|---|
| [PIE hazards, latencies and issue rules](../02-pie-vector/pie-hazards-latencies-and-issue-rules.md) | Whether `RSR`'s "sometimes more than one cycle" warning applies to reading `CCOUNT` on this core | device measurement, back-to-back `rsr.ccount` timing |
| [RTOS and runtime effects on kernel timing](../08-frontiers/rtos-and-runtime-effects-on-kernel-timing.md) | The cross-core contention a `portENTER_CRITICAL` interval is exposed to, deferred from the CCOUNT leaf | device measurement, CCOUNT loop under deliberate cross-core bus load |
| [CCOUNT, cycle counter and timing a kernel](../05-measurement/ccount-cycle-counter-and-timing-a-kernel.md) | The two cores' `CCOUNT` values are not synchronised after start-up, and no primary source states an alignment guarantee either way | document not yet read, or a device test comparing both cores' counters through an inter-processor call |
| [CCOUNT, cycle counter and timing a kernel](../05-measurement/ccount-cycle-counter-and-timing-a-kernel.md) | The exact cache and bus arbitration behind cross-core contention (deferred to the memory-hierarchy bucket) | device measurement, CCOUNT loop under deliberate cross-core bus load |
| [CCOUNT, cycle counter and timing a kernel](../05-measurement/ccount-cycle-counter-and-timing-a-kernel.md) | `rsr.ccount`'s own issue latency; no primary source gives one | device measurement, measure the empty interval and subtract |
| [CCOUNT, cycle counter and timing a kernel](../05-measurement/ccount-cycle-counter-and-timing-a-kernel.md) | The size of the effect an attached debugger (OpenOCD, breakpoints, single-stepping) has on a timing run | device measurement, same kernel timed with the debugger attached and detached |
| [measurement README](../05-measurement/README.md) | A published per-instruction issue latency for `rsr.ccount` (repeats the CCOUNT-leaf claim) | device measurement, measure the empty interval |

## 6. QEMU fidelity (2 tags)

The issue-154 question is resolved in both leaves that raised it: QEMU's
reset hook sets `CPENABLE` to `0xff` only in its user-mode build, never
in the system emulation this corpus runs, confirmed against a silicon
reading and an open, unmerged pull request fixing it. What remains is the
`-icount` scaling nobody has run.

| Leaf | Claim as tagged | Settling |
|---|---|---|
| [QEMU ESP32-S3: what it proves and what it cannot](../05-measurement/qemu-esp32s3-what-it-proves-and-what-it-cannot.md) | With `-icount`, a `CCOUNT` delta becomes a linear function of instructions retired, but the exact scaling was not run to confirm | toolchain/QEMU test: run `-icount` mode over a fixed instruction count, read the `CCOUNT` delta |
| [measurement README](../05-measurement/README.md) | QEMU's `-icount` combined with a `CCOUNT` delta was not run to confirm the scaling (repeats the QEMU-leaf claim) | toolchain/QEMU test, as above |

## 7. RTOS, exceptions and coprocessor handoff costs (13 tags)

Window overflow/underflow and the coprocessor-disabled exception are
structural (the ISA requires them), but nobody publishes what one costs
in cycles, and the RTOS comparison table carries several unconfirmed
cells for Zephyr and NuttX because this corpus did not read those
kernels' Xtensa ports as closely as it read ESP-IDF's.

| Leaf | Claim as tagged | Settling |
|---|---|---|
| [RTOS and runtime effects on kernel timing](../08-frontiers/rtos-and-runtime-effects-on-kernel-timing.md) | The per-exception cost of a window overflow/underflow handler, unpublished in either primary source checked | device measurement, trigger one in isolation under CCOUNT, subtract the calibrated empty interval |
| [RTOS and runtime effects on kernel timing](../08-frontiers/rtos-and-runtime-effects-on-kernel-timing.md) | Whether two Zephyr AMP images on the two cores can cooperate more tightly than message passing; no citable statement found | document not yet read, Zephyr's own multicore documentation |
| [RTOS and runtime effects on kernel timing](../08-frontiers/rtos-and-runtime-effects-on-kernel-timing.md) | Whether Zephyr on the ESP32-S3 saves or restores PIE state at all | document not yet read, Zephyr's Xtensa coprocessor-save source |
| [RTOS and runtime effects on kernel timing](../08-frontiers/rtos-and-runtime-effects-on-kernel-timing.md) | Whether interrupt levels above `XCHAL_EXCM_LEVEL`=3 still preempt regardless of task priority (comparison table) | document not yet read, confirmed only for ESP-IDF FreeRTOS here |
| [RTOS and runtime effects on kernel timing](../08-frontiers/rtos-and-runtime-effects-on-kernel-timing.md) | Whether Zephyr models PIE as a coprocessor at all for lazy save/restore on a contended handoff | document not yet read, Zephyr's Xtensa coprocessor source |
| [RTOS and runtime effects on kernel timing](../08-frontiers/rtos-and-runtime-effects-on-kernel-timing.md) | Whether a flash-cache-down freeze during a flash write reaches the other core under Zephyr's or NuttX's own flash driver | document not yet read, each kernel's flash driver source |
| [RTOS and runtime effects on kernel timing](../08-frontiers/rtos-and-runtime-effects-on-kernel-timing.md) | Open question restating the window/coprocessor exception cost above | device measurement, as above |
| [RTOS and runtime effects on kernel timing](../08-frontiers/rtos-and-runtime-effects-on-kernel-timing.md) | Footnote cross-reference to the register-windows leaf's own uncertain per-exception cost | device measurement, as above |
| [coprocessors, CPENABLE and lazy context switching](../00-foundations/coprocessors-cpenable-and-lazy-context.md) | Whether using PIE instructions inside an ISR is a supported configuration (Espressif documents this only for the FPU) | document not yet read, an Espressif statement scoping `CONFIG_FREERTOS_FPU_IN_ISR`-equivalent PIE support |
| [LX7 core pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md) | Register window overflow/underflow cost (cost-table row, repeats the RTOS-leaf and register-windows-leaf claim) | device measurement, as above |
| [register windows and the windowed ABI](../00-foundations/register-windows-and-windowed-abi.md) | The cost of one window overflow/underflow, not published by the architecture manual | device measurement, as above |
| [foundations README](../00-foundations/README.md) | Key claim: the cost of one window overflow/underflow is not published and is uncertain (register-windows leaf summary) | device measurement, as above |
| [frontiers bucket index](../08-frontiers/README.md) | Open question restating the per-exception cost of a window overflow/underflow handler and of a coprocessor-disabled handler, shared across this bucket and `00-foundations/` | device measurement, as above |

## 8. Vendor roadmap: ESP32-P4, esp-dsp/esp-nn/esp-dl, ESP32-S31, and the LLVM/Clang fork (12 tags)

Everything in this group is about hardware this repository does not
target (the ESP32-P4, the announced ESP32-S31) or a toolchain fork this
repository does not build with. Several rows need documents this corpus
could not fetch, including pages that returned HTTP 403.

| Leaf | Claim as tagged | Settling |
|---|---|---|
| [esp-dsp, esp-nn and vendor kernel libraries](../08-frontiers/esp-dsp-esp-nn-and-vendor-kernel-libraries.md) | Whether the ESP32-P4/S31 RISC-V PIE extension and the ESP32-S3 Xtensa PIE are the same microarchitectural design retargeted, or independent designs | document not yet read, an Espressif architecture document naming both explicitly |
| [ESP32-P4 and where the vector work moves](../08-frontiers/esp32-p4-and-where-the-vector-work-moves.md) | The P4's clock ceiling: 400 MHz per the datasheet, 360 MHz per the TRM | document not yet read, whichever Espressif document resolves the discrepancy by chip revision |
| [ESP32-P4 and where the vector work moves](../08-frontiers/esp32-p4-and-where-the-vector-work-moves.md) | The clock figures disagree the same way, probably by chip revision, but neither document says so | unknowable here (needs P4 hardware or an Espressif revision table) |
| [ESP32-P4 and where the vector work moves](../08-frontiers/esp32-p4-and-where-the-vector-work-moves.md) | The semantics of the P4's unaligned-access enable bit are unpublished | document not yet read, a P4 register reference beyond the TRM excerpt read here |
| [ESP32-P4 and where the vector work moves](../08-frontiers/esp32-p4-and-where-the-vector-work-moves.md) | Whether the P4's 128-bit `UA_STATE` is in fact unaligned-access state (the name is suggestive, no source confirms it) | document not yet read, a P4 architecture document naming `UA_STATE` explicitly |
| [ESP32-P4 and where the vector work moves](../08-frontiers/esp32-p4-and-where-the-vector-work-moves.md) | Whether the P4 lacks `ee.src.q` and the `.qup` register-rotating MAC forms, or simply has no esp-dsp kernel needing them | document not yet read, a P4 ISA chapter enumerating its full vector instruction set |
| [ESP32-P4 and where the vector work moves](../08-frontiers/esp32-p4-and-where-the-vector-work-moves.md) | Whether the P4's PIE has any floating-point vector capability at all | unknowable here (needs P4 hardware or the missing ISA chapter) |
| [ESP32-P4 and where the vector work moves](../08-frontiers/esp32-p4-and-where-the-vector-work-moves.md) | Whether GCC's auto-vectoriser can target `xesppie` at all | toolchain test: `-O3 -fopt-info-vec-all` on a vectorisable loop with the P4 `-march` string |
| [LLVM/Clang for Xtensa: status and what it changes](../08-frontiers/llvm-clang-for-xtensa-status-and-what-it-changes.md) | Whether the fork's ability to call PIE operations as ordinary function calls changes code quality, or is usable for this repo's pointer-incrementing idioms | toolchain test: build a representative kernel with the fork's Clang, compare generated code to the GCC 14 baseline |
| [LLVM/Clang for Xtensa: status and what it changes](../08-frontiers/llvm-clang-for-xtensa-status-and-what-it-changes.md) | Which loop shapes the fork's zero-overhead-loop pass recognises, versus GCC's doloop pass; no side-by-side comparison run | toolchain test: compile the same set of loop shapes under both compilers, diff the emitted loop instructions |
| [LLVM/Clang for Xtensa: status and what it changes](../08-frontiers/llvm-clang-for-xtensa-status-and-what-it-changes.md) | How the fork's inline-asm constraints (`a`, `f`, no memory or paired-register constraints) compare to GCC 14's full Xtensa constraint set | toolchain test: compile the same inline-asm block under both compilers |
| [LLVM/Clang for Xtensa: status and what it changes](../08-frontiers/llvm-clang-for-xtensa-status-and-what-it-changes.md) | Whether this LLVM build path is meant for production ESP32-S3 firmware or is still experimental within ESP-IDF | document not yet read, an ESP-IDF roadmap or release note stating its intended status |

## 9. Manual, documentation and citation gaps (8 tags)

These are not chip questions; they are places a source this corpus
wanted to read was unreachable, recorded rather than papered over.

| Leaf | Claim as tagged | Settling |
|---|---|---|
| [LUT gathers, palettes and RGB565](../06-kernel-patterns/lut-gathers-palettes-and-rgb565.md) | Whether the TRM's `EE.LDXQ.32` operation listing (`vaddr3 = as + qs[63:47] * 4`, breaking the 16-bit lane pattern the other seven lines follow) is a typographical error | document not yet read, a later TRM revision or an Espressif errata list |
| [LUT gathers, palettes and RGB565](../06-kernel-patterns/lut-gathers-palettes-and-rgb565.md) | Bayer's 1973 paper was cited only through a secondary source, not read directly | document not yet read, the original *IEEE International Conference on Communications* proceedings |
| [floating-point option on LX7](../01-scalar-isa/floating-point-option-on-lx7.md) | Exact wording of `RECIP0.S`/`RSQRT0.S`'s definitions, pending a direct read (the Cadence PDF returned HTTP 403) | document not yet read, Cadence's *Xtensa ISA Summary* PDF |
| [floating-point option on LX7](../01-scalar-isa/floating-point-option-on-lx7.md) | Which ISA revision added the `div0.s`/`recip0.s`/`rsqrt0.s` seed instructions (absent from the 2010 manual, present on this target) | document not yet read, a later Cadence ISA revision document |
| [LX7 core pipeline and cost model](../00-foundations/lx7-core-pipeline-and-cost-model.md) | The exact semantics of the float-divide helper instructions (`div0.s`, `nexp01.s`, `maddn.s` and the rest), absent from the 2010 manual | document not yet read, a Cadence or Espressif document describing the LX7's floating-point microcode |
| [bit-exact reference tests and fuzzing](../05-measurement/bit-exact-reference-tests-and-fuzzing.md) | McKeeman's 1998 "Differential Testing for Software" page range is confirmed only through a secondary summary (Wikipedia), not the original journal issue | document not yet read, *Digital Technical Journal* vol. 10 no. 1, 1998 |
| [measurement README](../05-measurement/README.md) | Same McKeeman 1998 citation gap, restated | document not yet read, as above |
| [host benchmarks versus the device](../05-measurement/host-benchmarks-versus-the-device.md) | Cadence's *ISA Summary for All Xtensa LX Processors* PDF returned a bot-detection page on fetch; only its section title is confirmed via a search index | document not yet read, the Cadence PDF once reachable without bot detection |

## 10. The esp-dsp float-versus-fixed-point FFT gap (2 tags)

A narrow, standalone question worth keeping separate because it names an
unexplained performance ratio rather than an unpublished timing or
register fact.

| Leaf | Claim as tagged | Settling |
|---|---|---|
| [kernel patterns README](../06-kernel-patterns/README.md) | Why esp-dsp's float FFT wins only 1.8x on the ESP32-S3 where its fixed-point FFT wins 13.1x; this corpus's proposed cause (no float kernel there is PIE-vectorised) is its own inference | document not yet read, an esp-dsp maintainer statement or profiling breakdown of the float FFT's own time |
| [esp-dsp as a reference kernel library](../06-kernel-patterns/esp-dsp-as-a-reference-kernel-library.md) | Same FFT-gap claim, in the leaf the README summarises | document not yet read, as above |

## Ranked: the ten uncertainties that would change the most kernel decisions

1. **Scalar `MULL`/`MULSH`/`MULUH`/`QUOS` result latency.** One CCOUNT run
   would fill 13 rows of [fixed-point arithmetic on LX7](../06-kernel-patterns/fixed-point-arithmetic-on-lx7.md)
   at once, every one a format decision made without a scalar cycle count.
2. **`EE.VMUL.*`/`EE.VMULAS.*` back-to-back issue rate.** Decides whether
   a PIE kernel is bound by lane count or a hidden multiplier reservation,
   and so how aggressively to unroll a PIE multiply chain.
3. **Whether the PIE unit shares the scalar load-store port on a PSRAM
   miss.** If not, a 128-bit PIE load beats four scalar loads on every
   miss; if it does, the load-store leaf's alignment advice needs a cost
   caveat attached.
4. **Cache-miss cycle cost for flash and PSRAM.** The floor every other
   latency in this corpus is compared against; nothing else here becomes
   a wall-clock estimate without it.
5. **Register window overflow/underflow handler cost.** Call depth is a
   named tuning knob in [register windows and the windowed ABI](../00-foundations/register-windows-and-windowed-abi.md),
   but CALL0 versus windowed ABI for a hot path has no number behind it.
6. **`WSR.SAR`-to-`EE.VMUL.S16` issue distance.** Any kernel that scales
   through `SAR` right before a PIE multiply may pay a stall this corpus
   cannot currently warn about.
7. **The write side of `S32I` in cached external memory.** The standing
   advice ("align the rows; it is free") assumes an unaligned scalar
   store never gets slower or wrong there; nobody has run the sweep.
8. **Whether `-mno-strict-align` is safe project-wide.** A build flag
   decision, currently advised against for exactly this reason.
9. **Whether PIE instructions are supported inside an ISR.** A
   correctness question: a kernel trusting lazy coprocessor save here may
   be silently wrong.
10. **Reset values of the PIE state registers.** One register dump after
    reset, on device or in QEMU, away from confirmation or a bug report.

## Open questions

- Whether one CCOUNT campaign could settle several group-1 or group-2 rows
  at once was not evaluated; this register lists the gaps, not a plan.
- Whether any "document not yet read" row in groups 8 and 9 has since
  become reachable (the HTTP 403s and bot-detection pages were all hit on
  2026-09-06) was not re-checked at reconciliation.
- This register has now been reconciled once against a later corpus
  state (see the intro); a leaf edited after that pass makes a row here
  stale again until the next audit.

## Sources

Every claim here restates a tag already present in the cited leaf; no
new primary-source citation is introduced beyond the counting method.

[^1]: Count method: `grep -rnF '[uncertain]' --include=*.md knowledge-base/esp32s3-assembly-optimization/` (the `-F` is load-bearing: an unescaped `[uncertain]` is a bracket-expression character class, not the literal string, and undercounts) restricted to buckets `00` through `08`, excluding `09-adversarial/`, `10-synthesis/`, `CLAUDE.md` and `MASTER-PLAN.md`. First run 2026-09-06: 146 matching lines across 36 files. Reconciliation run, same day, once the topic's build had settled: 138 matching lines across 39 files. `[measured]`.
[^2]: The two buckets this register excludes by design carry their own `[uncertain]` tags: `09-adversarial/` (this bucket) has 20, `10-synthesis/` has 16, counted the same way on the reconciliation date. Neither count is folded into the 138 above; this register catalogues buckets `00` through `08` only. `[measured]`.
