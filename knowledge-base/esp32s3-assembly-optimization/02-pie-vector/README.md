---
title: "02-pie-vector: bucket index"
id: 02-pie-vector/readme
schema_version: 1
doc_type: reference
status: review
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, pie, simd, registers, alignment, load-store, fixed-point, saturation, shuffle, coprocessor]
---

# 02-pie-vector: bucket index

The `EE.*` extension: what state it adds, how its memory instructions treat
an address, what its compute instructions actually compute, and what it has
no instruction for. The whole extension is Chapter 1 of the ESP32-S3
Technical Reference Manual, version 1.8: Section 1.5 for the registers,
Section 1.6 for the instruction list by category, Section 1.7 for the
pipeline stages and hazards, and Section 1.8 for 220 numbered per-instruction
entries.

The base ISA these instructions sit beside is in
[`01-scalar-isa/`](../01-scalar-isa/), the coprocessor switching machinery in
[`00-foundations/`](../00-foundations/), the cache and PSRAM costs a vector
load pays in [`03-memory-hierarchy/`](../03-memory-hierarchy/), what GCC will
and will not accept around an `asm` block in
[`04-toolchain-and-codegen/`](../04-toolchain-and-codegen/), and the fixed-point
and lookup-table idioms these instructions serve in
[`06-kernel-patterns/`](../06-kernel-patterns/).

| File | Topic | Key claims |
|---|---|---|
| [pie-register-file-sar-and-context.md](./pie-register-file-sar-and-context.md) | The register-level model: coprocessor 3, the `q` registers, the accumulators, the implicit state registers, instruction widths, lazy context save, and what GCC knows | PIE is coprocessor 3, named `cop_ai`, with a 208-byte save area of 26 registers: eight 128-bit `q` registers plus 18 user registers. Its disabled exception is EXCCAUSE 35. The `q` registers have no fixed element type; the instruction picks 16 x 8-bit, 8 x 16-bit or 4 x 32-bit. `QACC_H` and `QACC_L` are 160 bits each, holding 16 lanes of 20 bits for 8-bit multiplies or 8 lanes of 40 bits for 16-bit multiplies, and `ACCX` is a single 40-bit horizontal accumulator. `SAR` is the base ISA's own 6-bit shift register, shared with scalar funnel shifts, and only `wsr.sar` reaches values above 31. `SAR_BYTE` is the unaligned-load path. Of the 217 `ee.` mnemonics the toolchain knows, 128 encode in 24 bits and 89 in 32; the split follows the fused memory access, not the operand count, and the count matches the manual's 217 `EE.*` entries exactly. ESP-IDF saves PIE state lazily on the disabled exception; a voluntary yield saves nothing, because every coprocessor 3 register is classified caller-saved, so `q` register contents never survive a call. Never write `CPENABLE` from a kernel. GCC 14.2 has no register class for `q0` to `q7`: `"q0"` in a clobber list is rejected, and `vector_size(16)` compiles to four scalar `add.n`. GCC has no model of `SAR` either, and re-emits `ssr` before every variable shift, so a `SAR` set inside one `asm` block does not survive to the next. |
| [pie-load-store-and-alignment.md](./pie-load-store-and-alignment.md) | Every load and store form, its immediate range, the silent address masking, the unaligned funnel-shift path, the fused forms, and the load-use pipeline gap | Every PIE access forces the low address bits to zero before the access: 4 bits for 128-bit, 3 for 64-bit, 2 for 32-bit, 1 for 16-bit, none for the 8-bit broadcast. This is stated in every TRM Section 1.8 load and store entry (Section 1.5.3 covers alignment and the split-access cost) and implemented in Espressif's QEMU as a mask with `0xfffffff0`. It never traps, so a misaligned pointer silently reads or overwrites the aligned block below it. The access address is masked but `as` is not, so a stream that starts three bytes off stays three bytes off. Immediates are signed fields left-shifted by the access width: `-2048..2032` step 16 for `ee.vld.128.ip`, `-1024..1016` step 8 for the 64-bit forms, `-128..112` step 16 for `ld.qr`, `st.qr` and the `f`-register 128-bit forms, and `-512..496` step 16 for the fused `ee.vmulas.*.ld.ip`. Two syntax lines in the manual are wrong and the assembler proves it: `EE.ST.ACCX.IP` is `-1024..1016`, not `-512..508`, and `EE.VLDBC.32.IP` is `-512..508`, not `-256..252`. The unaligned path is `ee.ld.128.usar.ip` to latch the byte offset into `SAR_BYTE`, then `ee.src.q` to funnel-shift two aligned blocks; esp-dsp's `memcpy` unrolls that rotation by three and keeps the destination aligned. There is no multi-lane gather: `EE.LDXQ.32` loads one 32-bit lane using one 16-bit lane of a `q` register, scaled by 4, as the index. In TRM Table 1.7-2 every 128-bit load defines its `qu` at stage 2 and the plain arithmetic uses `q` at stage 1, so a PIE loop must load one iteration ahead; the FFT store forms and `EE.VRELU` are the exceptions, see the hazards leaf. |
| [pie-hazards-latencies-and-issue-rules.md](./pie-hazards-latencies-and-issue-rules.md) | Turning TRM Table 1.7-2 into scheduling rules: which operands define late, the accumulator chain, resource and control hazards, and what the table omits | Section 1.7 is the only published timing model for the unit, and it gives no cycle counts at all. It gives five stages (I, R, E, M, W, numbered R=0 through W=3), the interlock rule `D = max(SA - SB + 1, 0)`, and a per-operand stage table. Table 1.7-2 uses only stages 1 and 2, so the whole model reduces to one question: does the producer define at 2 and the consumer use at 1? If so, one independent instruction has to sit between them. The stage 2 producers are every vector and float load, `EE.LDXQ.32`, every `EE.VMUL.*` and `EE.CMUL.*`, `EE.VPRELU.*`, `EE.VRELU.*` (which is in place, so it defines the register it reads), and the multiply accumulate instructions on their accumulator. Multiplies are therefore load-shaped, while adds, min, max, compares, bitwise ops, vector shifts, zips and `EE.SRC.Q` all define at stage 1 and chain freely. A run of `EE.VMULAS.*` uses and defines its accumulator at stage 2, so it issues one per cycle, but reading the accumulator out with `EE.SRCMB.*` or `EE.SRS.ACCX` uses it at stage 1 and needs the gap; clearing and preloading it define at stage 1 and cost nothing. Everything in the table is an interlock, not a correctness hazard, so a bad schedule is slow rather than wrong. Outside the table: the core has eight 16-bit multipliers and delays an instruction that cannot get one, without saying which instruction reserves what; a taken branch costs 2 cycles; issue width is one, with `XCHAL_HAVE_FLIX3` at 0; and base instructions, `LD.QR`, `ST.QR`, `MV.QR`, `RUR.*` and `WUR.*` are absent from Table 1.7-2 entirely. |
| [pie-arithmetic-multiply-saturate-and-shuffle.md](./pie-arithmetic-multiply-saturate-and-shuffle.md) | The compute half: multiplies, multiply accumulate, compares, bitwise, shifts, lane moves, zip and unzip, the activation pair, the FFT helpers, what is missing, and composition idioms | Saturation is the exception, not the rule: only instructions whose description mentions it clamp, and everything else wraps. `EE.VMUL.*` has no shift operand; the product is right-shifted by `SAR` inside the instruction, arithmetically for the signed forms and logically for the unsigned ones, and the low bits are written back to a lane as wide as the inputs. There is no 32-bit lane multiply and no widening multiply into a register pair, so the accumulators are the only route to a full product. Adds, subtracts, compares, min and max are signed only. Lane shifts exist for 32-bit lanes only; an 8-bit or 16-bit lane shift is a multiply by a power of two, which is exact. There is no select, no and-not, no vector divide, no float lane arithmetic, and no lane broadcast from a register. `EE.VZIP.8` against a zeroed register is the zero-extending widen every 8-bit kernel needs; there is no sign-extending widen. The manual carries four defects here: `EE.MOV.S16.QACC`'s operation section uses the wrong sign bits for the top two lanes, `EE.CMUL.S16`'s prose puts the real part in the wrong half against its own operation section and QEMU, the register overview says the 32-bit lane shifts read 5 bits of `SAR` while their own entries read 6, and `EE.SRCMB.*` and `EE.SRS.ACCX` take an undescribed third operand the toolchain calls `sel2`. |

## Not yet covered

- **Cycles, as against issue distances.** The hazards leaf works Table
  1.7-2 into scheduling rules, but the manual publishes no cycle count and no
  throughput figure for any PIE instruction, so nothing here says what a
  kernel costs. Two specific gaps it names are open: which instruction
  reserves how many of the eight 16-bit multipliers and for how long, and
  whether a fused load-and-op avoids a resource stall that the two separate
  instructions would hit. Both are measurable with
  [CCOUNT](../05-measurement/ccount-cycle-counter-and-timing-a-kernel.md) and
  neither has been measured.
- **Pipeline stages for the instructions Table 1.7-2 omits.** `LD.QR`,
  `ST.QR`, `MV.QR`, `RUR.*` and `WUR.*` all have Section 1.8 entries or base
  ISA definitions but no row in the stage table, so their def and use stages
  are `[uncertain]`.
- **The FFT family worked through.** `EE.FFT.R2BF.S16`, the two
  `EE.FFT.CMUL.S16` forms, the four `EE.FFT.AMS.S16` addressing modes,
  `EE.FFT.VST.R32.DECP` and `EE.BITREV` are named and their operands given,
  but no leaf walks a radix-2 stage from end to end, and the `UA_STATE`
  register that carries unaligned data between `EE.FFT.AMS.S16.LD.INCP.UAUP`
  calls is described only as a state register.
- **The fast GPIO port.** `EE.WR_MASK_GPIO_OUT`, `EE.SET_BIT_GPIO_OUT`,
  `EE.CLR_BIT_GPIO_OUT` and `EE.GET_GPIO_IN` reach eight processor pins
  through the GPIO matrix and are faster than a peripheral register write.
  The register leaf notes that `GPIO_OUT` is user register 12, the one
  register ESP-IDF's save list skips, so two tasks share it with no
  arbitration. Nothing covers how to route the port, what the latency
  actually is, or what the sharing hazard looks like in practice.
- **The unresolved `sel2` operand.** `EE.SRCMB.S16.QACC`,
  `EE.SRCMB.S8.QACC` and `EE.SRS.ACCX` each take a third operand of 0 or 1
  that the manual never describes, that has no field in its instruction-word
  diagram, and that QEMU ignores. Settling it needs a device test that runs
  both encodings over the same accumulator contents and compares.
- **What a PIE access costs against each memory.** Whether one 128-bit load
  beats four scalar loads on a PSRAM miss, and whether the PIE unit shares
  the scalar load-store port, are both open; the load-store leaf marks the
  second `[uncertain]`. The per-memory miss costs are in
  [caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md),
  but not the vector comparison.
- **Whether a 128-bit PIE access to an instruction-bus address works.** The
  instruction bus takes 4-byte aligned accesses only, and a 16-byte access
  satisfies that, but no source says the bus carries the 16-byte form. Marked
  `[uncertain]`; a device test would settle it.
- **A worked kernel from C reference to bit-exact vector loop.** The
  composition idioms are per-instruction. Nothing here assembles a prologue,
  an alignment guard, a main loop and a tail into one routine and proves it
  bit-exact against its portable twin. The verification method is in
  [`05-measurement/`](../05-measurement/) and the loop shapes in
  [`06-kernel-patterns/`](../06-kernel-patterns/); the PIE-specific worked
  example is missing.
- **The `.QUP` forms in practice.** Every `EE.VMULAS.*` fused-load form has a
  `.QUP` variant that applies the `SAR_BYTE` funnel shift to the data it
  loads, which is how a vector kernel reads an unaligned stream without a
  separate shift instruction. No leaf shows one in a loop.
- **`EE.VZIP` and `EE.VUNZIP` at 16 and 32 bits.** The 8-bit forms are given
  lane by lane and the wider ones are stated to follow the same pattern at
  their lane width, without their operation sections transcribed.
