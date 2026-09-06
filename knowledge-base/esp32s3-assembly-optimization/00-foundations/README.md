---
title: "00-foundations: bucket index"
id: 00-foundations/readme
schema_version: 1
doc_type: reference
status: review
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, lx7, pipeline, register-windows, coprocessor, memory-map]
---

# 00-foundations: bucket index

The LX7 core itself: how a cycle is spent, how a call moves the register
window, how the FPU and the vector unit are switched between tasks, and
which address reaches which memory. The instructions are catalogued in
`01-scalar-isa/` and `02-pie-vector/`, the cache and PSRAM numbers in
`03-memory-hierarchy/`, and the measurement method in `05-measurement/`.

| File | Topic | Key claims |
|---|---|---|
| [lx7-core-pipeline-and-cost-model.md](./lx7-core-pipeline-and-cost-model.md) | The five-stage pipeline, the interlock rule, load-use, branch cost, integer and floating-point divide, and a cost table | The ESP32-S3 runs the 5-stage I/R/E/M/W pipeline, on three sources: the datasheet says five stages, TRM Table 1.7-1 names them and numbers R/E/M/W as 0/1/2/3, and `XCHAL_DATA_PIPE_DELAY` is 1, glossed in the header as 5-stage, against 2 on the original ESP32. At most one instruction issues per cycle. The cost model is not per-instruction latency: an instruction using a value in stage SB issues no earlier than max(SA - SB + 1, 0) cycles after the one defining it in SA. A load defines in M and is used in E, so a dependent instruction stalls one cycle unless one independent instruction is placed between them. A taken branch removes the instructions in R and E, so 2 cycles, and the target begins three cycles after the branch instead of two if it crosses a fetch boundary. Integer divide is in hardware (`QUOS`, `QUOU`, `REMS`, `REMU`) and GCC 14.2 emits it directly, but the ISA manual warns some hardware divide implementations are slower than software for some operands, and gives no cycle count. Float divide is a call into a 30-instruction `__divsf3` built from the Newton-Raphson helpers, unchanged by `-ffast-math`; `sqrtf` is a library call; every `double` is a library call. Every latency the manual does not give is `[uncertain]` with the CCOUNT method to settle it. |
| [register-windows-and-windowed-abi.md](./register-windows-and-windowed-abi.md) | The 64-register file, `WindowBase` and `WindowStart`, CALL4/8/12, `ENTRY` and `RETW`, overflow and underflow, the stack frame, the CALL0 ABI, register pressure | 64 physical address registers, 16 visible. `WindowBase` is special register 72 (width log2(NAREG/4)), `WindowStart` is 73 (width NAREG/4); `PS.WOE`, `PS.CALLINC` and `PS.OWB` live in special register 230. `CALLn` sets `PS.CALLINC`; the callee's `ENTRY` is what adds it to `WindowBase`. The caller's `a_(n+k)` is the callee's `a_k`, so a `call8`'s arguments go out in `a10`-`a15` and its return value comes back in `a10`-`a13`, not `a2`-`a5`. Rotation saves nothing: the spill happens later, as a Window Overflow exception on the first reference to a stale group, handled by three depth-specific handlers that ESP-IDF ships matching the manual instruction for instruction. The cost of one is not published and is `[uncertain]`. GCC 14.2 fixes `a0` and `a1`, leaving 14 allocatable, and allocates `a8`-`a15` before `a7` down to `a2`. CALL0 has no windows: `a1` and `a12`-`a15` callee-saved, everything else caller-saved. |
| [coprocessors-cpenable-and-lazy-context.md](./coprocessors-cpenable-and-lazy-context.md) | `CPENABLE`, the coprocessor-disabled exception, ESP-IDF's lazy save, which coprocessor is which, core pinning, `SAR` versus `SAR_BYTE`, and what GCC knows about `q` registers | `CPENABLE` is special register 224, 8 bits, so 8 coprocessors at most; the ESP32-S3 has 2, CP0 (the FPU, 72-byte save area) and CP3 (the PIE vector unit, 208 bytes, of which 128 are `q0`-`q7`). Coprocessor`n`Disabled is EXCCAUSE 32+n. ESP-IDF saves coprocessor state only on that exception, tracking one owner per coprocessor per core under a spinlock. A voluntary yield saves nothing: `_xt_coproc_savecs` asks for the callee-saved subset and this core classifies every FPU and PIE register as caller-saved. Never write `CPENABLE` from application code, because the owner array would then disagree with the hardware. Coprocessor instructions are thread-only unless `CONFIG_FREERTOS_FPU_IN_ISR` is set, which is off by default and endorsed only for the FPU. A task is pinned to a core the first time it uses a coprocessor. `SAR` is base-ISA state saved on every switch; `SAR_BYTE` is PIE state saved lazily. GCC 14.2 has no register class or name for `q0`-`q7`, so they cannot be named as operands or clobbers and do not need to be. |
| [esp32s3-memory-map-and-address-spaces.md](./esp32s3-memory-map-and-address-spaces.md) | The data and instruction bus split, ROM, the three SRAM blocks, RTC memory, the external cache windows, access-width faults, DMA reach, and the ESP-IDF placement attributes | Addresses below `0x4000_0000` go over the data bus, `0x4000_0000` to `0x4FFF_FFFF` over the instruction bus, and `0x5000_0000` upward over both. The instruction bus takes 4-byte aligned accesses only. Internal SRAM is 512 KB in three blocks: SRAM 0 (32 KB, instruction side, ICache candidate), SRAM 1 (416 KB, both buses at `0x4037_8000` and `0x3FC8_8000`, offset `0x6F_0000`), SRAM 2 (64 KB, data side, DCache candidate). External flash and PSRAM are reached only through two 32 MB cache windows, `0x4200_0000` on the instruction side and `0x3C00_0000` on the data side, mapped in 64 KB pages out of up to 1 GB each. An 8- or 16-bit access to 32-bit-only memory raises LoadStoreError (EXCCAUSE 3), a misaligned one LoadStoreAlignment (EXCCAUSE 9); the software workaround for the first is an ESP32-only option, so on the S3 byte access to IRAM is not a supported path. GDMA reaches SRAM 1 and SRAM 2 (`SOC_DMA_LOW` `0x3FC8_8000` to `SOC_DMA_HIGH` `0x3FD0_0000`) and external RAM at the data-bus window. |

## Not yet covered

- **Exceptions and interrupts as a bucket topic.** The bucket description
  names exceptions, and the leaves here touch only the ones they need
  (window overflow, coprocessor disabled, load/store faults). Nothing
  covers the exception architecture as a whole: XEA2, the vector table
  and its entry points, `PS.EXCM` and `PS.INTLEVEL`, the seven interrupt
  levels and which of them run with the flash cache usable, `EPC[n]` and
  `EPS[n]`, and what worst-case interrupt latency does to a kernel's
  timing.
  [Configured ISA options](../01-scalar-isa/core-isa-and-configured-options.md)
  lists the
  interrupt configuration but does not work the mechanism through.
- **Atomics, ordering and the two cores.** `S32C1I` and `SCOMPARE1`,
  `MEMW` and `EXTW`, what "shared by both cores" means for a table one
  core writes and the other reads, and whether a kernel needs a barrier.
  Mentioned in passing in `04-toolchain-and-codegen/` as the reason
  ESP-IDF writes `SCOMPARE1` from inline asm; not established anywhere.
- **The MAC16 option.** `XCHAL_HAVE_MAC16` is 1 on this chip and the
  cost-model table lists it, but no leaf says what the 40-bit accumulator
  and the `MUL.AA`/`MULA.DD` family can do, whether GCC 14.2 ever emits
  them, or how they compare with the PIE unit for a small filter.
- **Boot-time state.** `WindowBase`, `WindowStart`, `LBEG` and `LEND` are
  undefined after reset and something must initialise them, and the cache
  size and mode are chosen at boot. Nothing here says what the
  second-stage bootloader and ESP-IDF's startup code have already done by
  the time a kernel runs.
- **Per-instruction latency, at all.** The LX7 processor data book is the
  document the ISA manual defers to for latency tables, and it is not
  openly published; a fetch of the Cadence product pages returned HTTP 403
  on 2026-09-06. Every latency row in the cost table is therefore
  `[uncertain]` with a measurement recipe. Closing this needs a
  measurement campaign, not another document.
- **The cost of a window overflow, in cycles.** Same reason. It is the one
  number that would let a reader decide whether a call inside a hot loop
  matters, and this bucket can only say "measure it".
- **Permission Control (PMS).** TRM Table 4.3-1 notes that every internal
  memory is gated by the Permission Control module. No leaf says what that
  can deny or how a fault presents.
