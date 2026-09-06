---
title: Coprocessors, CPENABLE, and lazy context switching on the ESP32-S3
id: 00-foundations/coprocessors-cpenable-and-lazy-context
schema_version: 1
doc_type: explanation
status: review
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, coprocessor, cpenable, freertos, fpu, pie, context-switch]
confidence: high
---

# Coprocessors, CPENABLE, and lazy context switching on the ESP32-S3

The Xtensa LX7 core treats the floating-point unit and the vector unit
as "coprocessors": optional execution units whose state the operating
system does not have to save on every context switch. This document
explains the hardware mechanism that makes that possible, how ESP-IDF's
FreeRTOS port uses it, and the rules that follow for a task or an
inline assembly block that touches either unit.

## The Coprocessor Option and CPENABLE

Xtensa's Coprocessor Option lets a core designer attach extra execution
units, each with its own register state, and gate each one behind a bit
in a single special register, `CPENABLE`[^1]. The architecture makes
`CPENABLE` 8 bits wide at special register number 224, one enable bit per
coprocessor, so the ceiling is 8 coprocessors, CP0 through CP7[^1].
ESP-IDF's headers for this chip carry the same number, 224[^2], and the
same architectural ceiling of 8[^3]. What the ESP32-S3 actually has is 2
coprocessors with a highest ID of 3, `XCHAL_CP_NUM` 2 and `XCHAL_CP_MAX` 4,
present as the bitmask `XCHAL_CP_MASK` `0x09`, that is CP0 and CP3[^6].

When code executes an instruction that belongs to coprocessor `n` while
bit `n` of `CPENABLE` is clear, the core raises a **Coprocessor `n`
Disabled** exception instead of executing the instruction. The exception
cause codes are contiguous: Coprocessor0Disabled is EXCCAUSE 32 and
Coprocessor`n`Disabled is `32 + n`, up to Coprocessor7Disabled at 39[^1].
ESP-IDF's header spells the same table out as `EXCCAUSE_CP_DISABLED(n)`
= `32+(n)` with the eight individual defines[^4]. ESP-IDF's exception
dispatcher relies on the codes being contiguous: it routes any cause
number at or above `EXCCAUSE_CP0_DISABLED` to the coprocessor exception
handler, with a single range check[^5].

This is the hook the operating system uses to avoid saving coprocessor
state it does not need to save. A task that never executes a
coprocessor instruction never takes this exception and never touches
that coprocessor's state, so nothing needs to be saved or restored for
it.

## Which coprocessor is which, on this chip

Coprocessor numbering is not architected; it is a property of how a
given core was configured. On the ESP32-S3, the toolchain's generated
core-configuration header assigns:

| CP number | Name in the header | Save-area size | What it holds |
|---|---|---|---|
| CP0 | `FPU` | 72 bytes | The single-precision floating-point registers `f0`-`f15` and the FP control/status registers `FCR`/`FSR`[^6] |
| CP1, CP2 | (unused) | 0 bytes | Not present on this core[^6] |
| CP3 | `cop_ai` (internal name) | 208 bytes | The vector/DSP state: the eight 128-bit vector registers `q0`-`q7` (128 bytes of the 208), plus the accumulator registers (`ACCX`), the Q-accumulators (`QACC_H`/`QACC_L`), a byte-granular shift-amount register, an FFT bit-width register, and four "unaligned load" state registers[^7] |
| CP4-CP7 | (unused) | 0 bytes | Not present on this core[^6] |

CP3's register list is the tell: `q0` through `q7`, `accx_0`, `accx_1`,
`qacc_h_0..4`, `qacc_l_0..4`, `sar_byte`, `fft_bit_width` and
`ua_state_0..3` are the architectural names for the PIE (vector) unit's
own state[^7]. The generated header calls this coprocessor `cop_ai`
internally, but its register list identifies it as the PIE unit beyond
doubt; nothing else on this chip owns a `q` or `QACC` register.

The 208 bytes add up: eight `q` registers at 16 bytes each is 128, the
eighteen 4-byte user registers are 72, and the save area is padded to its
16-byte alignment[^7]. So the q registers are coprocessor state and are
covered by the lazy scheme below, which is the same statement as
[the PIE register file leaf](../02-pie-vector/pie-register-file-sar-and-context.md)
makes from the vector side.

Only two coprocessors exist on the ESP32-S3, then: the scalar FPU at
CP0, and the PIE vector unit at CP3. Every mention of "the coprocessor
disabled exception" for CP0 in this document is about the FPU; every
mention of it for CP3 is about the PIE unit.

## ESP-IDF's lazy save and restore

The mechanism lives in `components/xtensa/xtensa_vectors.S` (the
exception handler) and `components/xtensa/xtensa_context.S` (the save
and restore routines), and it works the same way for CP0 and CP3.

Each coprocessor's state has one **owner**: the task whose values
currently sit in that coprocessor's physical registers. ESP-IDF tracks
ownership per coprocessor per core in an array, `_xt_coproc_owner_sa`,
indexed by coprocessor number, holding a pointer to the owning task's
save area (or zero for "no owner")[^8]. On a dual-core build this array
holds one entry per coprocessor per core, because coprocessor state is
physical register file content on one specific core; the array's size
is declared as `(XCHAL_CP_MAX * portNUM_PROCESSORS) << 2` bytes for
exactly this reason[^9].

The sequence when a task executes a coprocessor instruction:

1. If the task's own `CPENABLE` bit for that coprocessor is already
   set, the instruction just executes; nothing else happens.
2. If the bit is clear, the core raises the Coprocessor `n` Disabled
   exception, entering `_xt_coproc_exc`[^10].
3. The handler sets the trapping task's `CPENABLE` bit for coprocessor
   `n`, so the instruction can now be retried and will succeed.
4. It reads `_xt_coproc_owner_sa[n]` for the current core to find the
   **previous owner** (if any), and immediately overwrites the entry
   with the trapping task, making it the new owner. On a dual-core build
   that read-and-write pair is taken under a spinlock,
   `_xt_coproc_owner_sa_lock`, because both cores can reach the same
   array[^8].
5. If there was a previous owner, and that owner's own saved
   `CPENABLE` still shows the coprocessor bit set (meaning it has live,
   unsaved state in the physical registers), the handler saves that
   owner's coprocessor register file into that owner's save area, and
   marks the owner's `CPENABLE` bit as clear so a future switch back to
   it knows to reload before use[^11].
6. If the new owner has previously-saved state for this coprocessor
   (its `CPSTORED` bit is set), the handler restores it into the
   physical registers[^12].
7. The interrupted instruction resumes.

There is a second path, and on this chip it does nothing.
`_xt_coproc_savecs` and `_xt_coproc_restorecs` (in `xtensa_context.S`)
run on FreeRTOS's solicited context switch, the voluntary yield, and drive
the same `xchal_cpN_store`/`xchal_cpN_load` HAL macros through the
save-area offset table `_xt_coproc_sa_offset`. But they pass
`select=XTHAL_SAS_TIE|XTHAL_SAS_NOCC|XTHAL_SAS_CALE`, which asks for the
callee-saved subset only[^13]. In this core's generated `tie-asm.h`, every
CP0 and CP3 register sits in a caller-saved (`XTHAL_SAS_CALR`) block and
no block is emitted for `XTHAL_SAS_CALE` at all[^20]. So a voluntary yield
saves no floating-point register and no `q` register: the ABI already
treats them as dead across a call. Only the disabled-exception path above
ever moves this state. The same conclusion, reached from the vector side,
is in
[the PIE register file leaf](../02-pie-vector/pie-register-file-sar-and-context.md).

What this buys: a full save of both coprocessors' state is roughly
280 bytes of register file (72 for the FPU, 208 for PIE) plus the
associated bookkeeping. Saving and restoring that on every context
switch, for every task, whether or not the task ever touches either
unit, would tax every switch with a cost most tasks never need. Instead
the cost is paid only when two different tasks actually contend for the
same coprocessor: a task that has the FPU or PIE unit to itself never
triggers a save on switches away from it, and the very first
coprocessor instruction any task ever executes always takes the disabled
exception once, then keeps the bit set until something else claims the
unit.

## Never write CPENABLE from application code

`CPENABLE` must never be written directly by C code, a hand-written
kernel, or inline assembly running under FreeRTOS. The owner-tracking
scheme above depends on the value of each task's `CPENABLE` bit being
exactly what the exception handler last left it as: set when that task
is the live owner with unsaved state, clear otherwise. A direct write
that sets a bit without going through `_xt_coproc_exc` makes the core
believe that coprocessor's instructions are usable, while
`_xt_coproc_owner_sa` still points at whatever task last triggered the
exception. The next context switch or exception that consults the
owner array now disagrees with the hardware about who owns the
registers, and the save/restore logic can save the wrong task's values
over another task's still-live state, or skip a save it should have
made. The only place `CPENABLE` should be written under this port is
inside the exception handler and the RTOS-provided save/restore
routines that maintain the owner array alongside it.

## Coprocessors cannot be used in an interrupt, by default

Xtensa's comment in the vector table is explicit about the general
rule: "these exceptions are generated by co-processor instructions,
which are only allowed in thread code (not in interrupts or kernel
code). This restriction is deliberately imposed to reduce the burden of
state-save/restore in interrupts."[^14] The handler enforces it: if a
coprocessor-disabled exception is taken with no current thread context
(that is, from inside an interrupt), it is treated as an unrecoverable
error, unless a specific escape hatch is compiled in[^15].

That escape hatch is `CONFIG_FREERTOS_FPU_IN_ISR`. Its help text: "When
enabled, the usage of float type is allowed inside Level 1 ISRs. Note
that usage of float types in higher level interrupts is still not
permitted."[^16] It is gated on `SOC_CPU_HAS_FPU` and is specifically
about the FPU (CP0), and it is off by default[^16]. The assembly gate
that this option controls is shared code that also runs for a CP3 (PIE)
disabled exception, so the same bypass would mechanically apply if a
PIE instruction executed inside a Level-1 ISR while the option is
enabled. Espressif's documentation only describes and endorses this for
the FPU. **[uncertain]**: whether using PIE instructions inside an ISR is
a supported configuration is not confirmed by any source this document
could verify; treat PIE in an ISR as unsupported.

## Where SAR lives, and why it is not "lazy"

The Xtensa shift-amount register, `SAR`, is architectural state used by
ordinary scalar shift instructions (`SLL`, `SRL`, `SRC`, and others),
not coprocessor state. ESP-IDF's context-save routine reads it with
`rsr a3, SAR` and stores it into the ordinary interrupt/task stack
frame, at a fixed offset (`XT_STK_SAR`), on every full register
save, unconditionally[^18]. It is restored the same way on every
context restore. `SAR` therefore survives every task switch as part of
the base thread context, with no `CPENABLE` gating and no lazy-save
logic: it is saved every time, not only when contended.

This is a different register from the PIE-specific `sar_byte` entry
inside CP3's own save area (`XCHAL_CP3_SA_LIST`, register `ur,13`)[^7].
That one is PIE's own byte-granular addressing state, used by some
`EE.*` instructions, and it is saved and restored lazily along with the
rest of CP3's register file by the disabled-exception mechanism described
above.
[The PIE register file leaf](../02-pie-vector/pie-register-file-sar-and-context.md)
covers what each of the two shift-amount registers is for. A kernel that
reasons about "does my shift amount survive a context switch" needs to
know which of the two it is using.

## Core affinity

Coprocessor ownership is tracked per physical core, because it is
physical register-file content on that core: the owner array's size is
scaled by `portNUM_PROCESSORS`, giving each core its own set of
per-coprocessor owner entries[^9]. A task is not required to be pinned
to a core before it first uses a coprocessor. Instead, ESP-IDF pins it
automatically the first time it does: the coprocessor exception hook
checks whether the trapping task is running under the scheduler and not
already inside an interrupt, and if so writes the current core ID into
the task's TCB core-affinity field, with the comment "CP operations are
incompatible with unpinned tasks. Thus we pin the task to the current
running core."[^19] From that point on, the task can no longer migrate
to the other core, because its live and saved coprocessor register
content exists only on the core it was pinned to.

## Consequence for inline assembly and the PIE's Q registers

The PIE unit's 128-bit vector registers, `q0` through `q7`, are named
directly by `EE.*` instruction mnemonics in assembly, and the assembler
knows them. GCC 14.2.0 does not. Its Xtensa backend declares ten register
classes, `NO_REGS`, `BR_REGS`, `FP_REGS`, `ACC_REG`, `SP_REG`, `ISC_REGS`,
`RL_REGS`, `GR_REGS`, `AR_REGS` and `ALL_REGS`, none of them a vector
class, and `REGISTER_NAMES` lists only the sixteen address registers, the
frame and argument pointers, `b0`, `f0`-`f15` and `acc`: no `q`[^21]. The
letter `q` is taken, and not by a vector class: in the Xtensa
`constraints.md` it is an internal register constraint for the stack
pointer, register `a1`[^22]. The shipping compiler agrees. Listing `"q0"`
in a clobber list is rejected with `error: unknown register name 'q0' in
'asm'`, and asking for a `"=q"` output on a 32-bit value is rejected with
`inconsistent operand constraints in an 'asm'`[^23].

Two consequences, which
[GCC extended inline asm on Xtensa](../04-toolchain-and-codegen/gcc-extended-inline-asm-on-xtensa.md)
covers in full:

- You cannot name a `q` register as an operand or a clobber. A value
  enters and leaves an `asm` block through memory or through an address
  register the block loads itself.
- You do not need to. The allocator has no class containing `q0`-`q7`, so
  it can never place a C-level value there and can never need one live
  across the block. A comment at the top of the block is the only way to
  record that the block owns them, and it is worth writing, because the
  guarantee is a property of GCC 14.2 and not of the ISA.

Within one task, the `q` registers are yours between the points where the
scheduler can intervene. Across a call or a yield they are not: the ABI
classifies every coprocessor 3 register as caller-saved, which is why
`_xt_coproc_savecs` saves none of them (above).

## Footnotes

[^1]: Cadence/Tensilica, *Xtensa Instruction Set Architecture (ISA) Reference Manual*, issue 4/2010, release RC-2010.1, Section 4.3.9 "Coprocessor Option" and Section 4.3.9.1, pp. 63-64: Table 4-41 "Coprocessor Option Exception Additions" (Coprocessor0Disabled through Coprocessor7Disabled, EXCCAUSE 32 to 39) and Table 4-42 "Coprocessor Option Processor-State Additions" (`CPENABLE`, quantity 1, width 8 bits, special register number 224). Copy consulted: https://0x04.net/~mwk/doc/xtensa.pdf
[^2]: ESP-IDF 5.5.1, `components/xtensa/esp32s3/include/xtensa/config/specreg.h`: `#define CPENABLE 224`.
[^3]: ESP-IDF 5.5.1, `components/xtensa/esp32s3/include/xtensa/config/core-isa.h`: `XCHAL_HAVE_CP` (1, "CPENABLE reg (coprocessor)"), `XCHAL_CP_MAXCFG` (8, "max allowed cp id plus one").
[^4]: ESP-IDF 5.5.1, `components/xtensa/include/xtensa/corebits.h`: `EXCCAUSE_CP_DISABLED(n)` = `32+(n)`, and the individual `EXCCAUSE_CP0_DISABLED` (32) through `EXCCAUSE_CP7_DISABLED` (39) defines.
[^5]: ESP-IDF 5.5.1, `components/xtensa/xtensa_vectors.S`, comment: "Handle any coprocessor exceptions. Rely on the fact that exception numbers above EXCCAUSE_CP0_DISABLED all relate to the coprocessors", and the `bgeui a0, EXCCAUSE_CP0_DISABLED, _xt_to_coproc_exc` dispatch.
[^6]: ESP-IDF 5.5.1, `components/xtensa/esp32s3/include/xtensa/config/tie.h`: `XCHAL_CP_NUM` 2, `XCHAL_CP_MAX` 4, `XCHAL_CP_MASK` `0x09`; `XCHAL_CP0_NAME` "FPU", `XCHAL_CP0_SA_SIZE` 72, `XCHAL_CP0_SA_ALIGN` 4; `XCHAL_CP1_SA_SIZE` 0, `XCHAL_CP2_SA_SIZE` 0, `XCHAL_CP4_SA_SIZE`..`XCHAL_CP7_SA_SIZE` 0.
[^7]: ESP-IDF 5.5.1, `components/xtensa/esp32s3/include/xtensa/config/tie.h`: `XCHAL_CP3_NAME` "cop_ai", `XCHAL_CP3_SA_SIZE` 208, `XCHAL_CP3_SA_ALIGN` 16, and the full `XCHAL_CP3_SA_LIST`: user registers `accx_0`, `accx_1`, `qacc_h_0`..`qacc_h_4`, `qacc_l_0`..`qacc_l_4`, `sar_byte`, `fft_bit_width`, `ua_state_0`..`ua_state_3` (18 entries, `ur,0`..`ur,11` and `ur,13`..`ur,18`, 4 bytes each), then `q0`..`q7` (16 bytes each, 16-byte aligned).
[^8]: ESP-IDF 5.5.1, `components/xtensa/xtensa_vectors.S`: `_xt_coproc_owner_sa` array declaration, comment "Owner thread of CP n, identified by thread's CP save area (0 = unowned)".
[^9]: ESP-IDF 5.5.1, `components/xtensa/xtensa_vectors.S`: `_xt_coproc_owner_sa: .space (XCHAL_CP_MAX * portNUM_PROCESSORS) << 2`.
[^10]: ESP-IDF 5.5.1, `components/xtensa/xtensa_vectors.S`, `_xt_coproc_exc` label and the `addi a5, a0, -EXCCAUSE_CP0_DISABLED` computation of the coprocessor index from `EXCCAUSE`.
[^11]: ESP-IDF 5.5.1, `components/xtensa/xtensa_vectors.S`, `_xt_coproc_exc`: the `.L_save_old` block, gated by `bnone a4, a0, .L_check_new` ("old owner not using CP") on the old owner's saved `CPENABLE`.
[^12]: ESP-IDF 5.5.1, `components/xtensa/xtensa_vectors.S`, `_xt_coproc_exc`: the `.L_check_new` block, gated on the new owner's `CPSTORED` bits, calling `xchal_cpi_load_funcbody`.
[^13]: ESP-IDF 5.5.1, `components/xtensa/xtensa_context.S`: `_xt_coproc_savecs` and `_xt_coproc_restorecs`, both driven by the `_xt_coproc_sa_offset` table and the per-coprocessor `xchal_cpN_store`/`xchal_cpN_load` macros, guarded per-CP by `#if XCHAL_CPn_SA_SIZE`.
[^14]: ESP-IDF 5.5.1, `components/xtensa/xtensa_vectors.S`, comment above the coprocessor exception handler section.
[^15]: ESP-IDF 5.5.1, `components/xtensa/xtensa_vectors.S`, `_xt_coproc_exc`: `#if !CONFIG_FREERTOS_FPU_IN_ISR` / `beqz a15, .L_goto_invalid` ("not in a thread (invalid)").
[^16]: ESP-IDF 5.5.1, `components/freertos/Kconfig`: `config FREERTOS_FPU_IN_ISR`, `depends on SOC_CPU_HAS_FPU && (IDF_TARGET_ESP32 || IDF_TARGET_ESP32S3)`, `default n`, help text quoted verbatim.
[^18]: ESP-IDF 5.5.1, `components/xtensa/xtensa_context.S`, `_xt_context_save`: unconditional `rsr a3, SAR` / `s32i a3, sp, XT_STK_SAR`; and `components/xtensa/include/xtensa_context.h`: `STRUCT_FIELD (long, 4, XT_STK_SAR, sar)` in the base register-save-area layout.
[^19]: ESP-IDF 5.5.1, `components/freertos/FreeRTOS-Kernel/portable/xtensa/portasm.S`, `_frxt_coproc_exc_hook`, comment "CP operations are incompatible with unpinned tasks. Thus we pin the task to the current running core", writing the core ID through `offset_xCoreID`; and `components/freertos/FreeRTOS-Kernel/portable/xtensa/port.c`, comment above `offset_xCoreID`: "Used to pin unpinned tasks that use the FPU."
[^20]: ESP-IDF 5.5.1, `components/xtensa/esp32s3/include/xtensa/config/tie-asm.h`. `XTHAL_SAS_CALR` is `0x0010` ("caller-saved") and `XTHAL_SAS_CALE` is `0x0020` ("callee-saved"). Inside `xchal_cp0_store`, `xchal_cp0_load`, `xchal_cp3_store` and `xchal_cp3_load`, every guarded block tests a select mask built from `XTHAL_SAS_CALR`; the string `CALE` appears nowhere in the file except its own `#define`.
[^21]: GCC 14.2.0 source, `gcc/config/xtensa/xtensa.h`: `enum reg_class` (lines 341-354) and `REGISTER_NAMES` (lines 652-660). https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-14.2.0/gcc/config/xtensa/xtensa.h
[^22]: GCC 14.2.0 source, `gcc/config/xtensa/constraints.md`: `(define_register_constraint "q" "TARGET_WINDOWED_ABI ? SP_REG : NO_REGS" "@internal The stack pointer (register @code{a1}).")`. https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-14.2.0/gcc/config/xtensa/constraints.md
[^23]: Toolchain observation, `xtensa-esp32s3-elf-gcc` 14.2.0 (crosstool-NG `esp-14.2.0_20241119`), 2026-09-06. `void f(void){ __asm__ volatile("" ::: "q0"); }` fails with `error: unknown register name 'q0' in 'asm'`. An `asm` with a `"=q"` output on an `int` fails with `error: inconsistent operand constraints in an 'asm'`.
