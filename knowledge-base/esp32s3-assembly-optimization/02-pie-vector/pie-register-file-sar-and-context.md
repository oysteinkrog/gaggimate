---
title: PIE register file, state registers and context save
id: 02-pie-vector/pie-register-file-sar-and-context
schema_version: 1
doc_type: reference
status: review
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, pie, simd, registers, coprocessor, context-switch, inline-asm, toolchain]
confidence: high
---

# PIE register file, state registers and context save

The ESP32-S3 adds a SIMD extension called PIE, for Processor Instruction
Extensions. It is built with the Tensilica Instruction Extension (TIE)
language and is documented as Chapter 1 of the ESP32-S3 Technical Reference
Manual.[^trm] This file is the register-level model: what state the unit
holds, which registers are implicit operands, who saves that state across a
context switch, and what the compiler does and does not know about any of
it.

Instruction semantics per mnemonic are not here. TRM Section 1.6 lists the
instructions by category and Section 1.8 gives the per-instruction
description.[^trm] The load and store forms are in
[PIE load and store instructions and their alignment rules](./pie-load-store-and-alignment.md);
the compute forms are in
[PIE compute instructions](./pie-arithmetic-multiply-saturate-and-shuffle.md).

## 1. PIE is coprocessor 3

PIE state is not part of the base core state. The toolchain's configuration
header for the ESP32-S3 declares two coprocessors: coprocessor 0 is the FPU,
and coprocessor 3 is the PIE unit, named `cop_ai`.[^tie] The header sets
`XCHAL_CP_MASK` to `0x09`, which is bits 0 and 3.

| Fact | Value | Source |
|---|---|---|
| Coprocessor ID | 3 (`XCHAL_CP_ID_COP_AI`) | `tie.h`[^tie] |
| Internal name | `cop_ai` (`XCHAL_CP3_NAME`) | `tie.h`[^tie] |
| State save area size | 208 bytes (`XCHAL_CP3_SA_SIZE`) | `tie.h`[^tie] |
| Save area alignment | 16 bytes (`XCHAL_CP3_SA_ALIGN`) | `tie.h`[^tie] |
| Registers in the save list | 26 (`XCHAL_CP3_SA_NUM`) | `tie.h`[^tie] |
| Exception when disabled | EXCCAUSE 35, `EXCCAUSE_CP3_DISABLED` | `corebits.h`[^corebits] |

The 208 bytes are 8 vector registers at 16 bytes each, plus 18 user
registers at 4 bytes each, padded to a 16-byte boundary between the two
groups.[^tie]

Being a coprocessor is the reason the state is saved lazily and the reason a
PIE instruction outside a task context is fatal. Section 6 covers both.

## 2. The vector register file

| Register | Count | Width | Access | Type | Source |
|---|---|---|---|---|---|
| `q0` to `q7` (QR) | 8 | 128 bits | R/W | Custom general purpose | TRM Table 1.5-1[^trm] |
| `a0` to `a15` (AR) | 16 visible of 64 | 32 bits | R/W | Base general purpose | TRM Table 1.5-1[^trm] |
| `f0` to `f15` (FR) | 16 | 32 bits | R/W | FPU general purpose | TRM Table 1.5-1[^trm] |

The q registers are the only general purpose state PIE adds. The manual's
stated reason for adding them is bandwidth: the AR registers are 32 bits
wide, so a 128-bit memory access can only use a quarter of the data bus if
it lands in an AR register.[^trm]

A q register has no fixed element type. The same 128 bits are read as one of
three lane views, and the instruction decides which:[^trm]

| Lane view | Lanes | Lane width | Example mnemonic suffix |
|---|---|---|---|
| 8-bit | 16 | 8 bits | `.S8`, `.U8` |
| 16-bit | 8 | 16 bits | `.S16`, `.U16` |
| 32-bit | 4 | 32 bits | `.32` |

So `ee.vadds.s8 q2, q0, q1` and `ee.vadds.s16 q2, q0, q1` read the same bits
of `q0` and `q1` and produce different results. Nothing in the register file
records which view was last used.

The FR registers get one PIE benefit without being PIE state. PIE adds
128-bit load and store forms for them, such as `ee.ldf.128.ip`, which the
manual describes as four times more efficient than the base 32-bit
floating-point load and store.[^trm]

## 3. Accumulators

The multiply and accumulate instructions do not write a q register. They
write one of two accumulators, and a separate instruction shifts the result
back into a q register or into memory.[^trm]

| Register | Width | Access | Read by | Source |
|---|---|---|---|---|
| `QACC_H` | 160 bits | R/W | `EE.VMULAS.*.QACC*`, `EE.SRCMB.*.QACC` | TRM Table 1.5-1, Section 1.5.1.2[^trm] |
| `QACC_L` | 160 bits | R/W | same | TRM Table 1.5-1, Section 1.5.1.2[^trm] |
| `ACCX` | 40 bits | R/W | `EE.VMULAS.*.ACCX*`, `EE.SRS.ACCX` | TRM Table 1.5-1, Section 1.5.1.2[^trm] |

QACC is one 320-bit accumulator split into a high half and a low half. Its
lane structure follows the multiply width: for 8-bit multiplies it is 16
accumulators of 20 bits, and for 16-bit multiplies it is 8 accumulators of
40 bits.[^trm] ACCX is the other shape. It accumulates every lane product of
a whole vector multiply down to one 40-bit value, which is what a dot
product needs.[^trm]

No single instruction moves a whole 160-bit QACC half to or from memory.
Section 1.5.3 says the transfer goes through five AR registers or two QR
registers.[^trm-153] The register-numbered `rur`/`wur` forms exist for
exactly that: `QACC_H_0` through `QACC_H_4` and `QACC_L_0` through
`QACC_L_4` are the five 32-bit windows onto each 160-bit half, and
`ACCX_0` and `ACCX_1` are the low 32 bits and high 8 bits of ACCX.[^tie]
The `_n` suffix convention for registers wider than 32 bits is stated in
TRM Section 1.6.10.[^trm]

Section 1.5.3's "no direct way" reads as out of date against Section 1.8,
which does give memory instructions for these registers, two per half:
`EE.LD.QACC_H.L.128.IP` and `EE.ST.QACC_H.L.128.IP` move the low 128 bits
and the `.H.32.IP` pair moves the top 32, with the same pair for
`QACC_L`.[^trm-qaccmem] `EE.LDQA.*` is the widening load that fills both
halves from packed 8-bit or 16-bit memory. All of them are listed in
[PIE load and store instructions and their alignment rules](./pie-load-store-and-alignment.md).

## 4. State registers

These are implicit operands. The manual is explicit that you cannot name one
in an instruction, and that you do not need to.[^trm] The consequence for a
kernel author is that a state register is a hidden dependency between
instructions that look unrelated.

| Register | Width | Set by | Read by | Source |
|---|---|---|---|---|
| `SAR` | 6 bits | `wsr.sar` writes all six bits; `ssr`, `ssl` and `ssai` reach only 0 to 31 (base ISA) | `EE.VSR.32` and `EE.VSL.32` read `SAR[5:0]` as the shift; `EE.VMUL.*`, `EE.CMUL.*`, `EE.FFT.AMS.*`, `EE.FFT.CMUL.*` use it as the right shift applied to the intermediate product | TRM Table 1.5-1, Sections 1.8.186 and 1.8.191[^trm][^trm-vsx]; ISA RM `SSR` and `SSAI`[^isa-ssr] |
| `SAR_BYTE` | 4 bits | `EE.LD.128.USAR.IP` and `EE.LD.128.USAR.XP` write the low 4 bits of the address register; also `wur.sar_byte` | `EE.SRCQ.*` and `EE.SRC.Q*`, and any instruction with a `.QUP` suffix | TRM Section 1.5.1.2[^trm] |
| `FFT_BIT_WIDTH` | 4 bits | `wur.fft_bit_width` | `EE.BITREV` only. Value 0 to 7 selects 3-bit to 10-bit mode | TRM Section 1.5.1.2[^trm] |
| `UA_STATE` | 128 bits | `EE.LD.UA_STATE.IP`, `EE.ST.UA_STATE.IP`, `wur.ua_state_n` | `EE.FFT.AMS.S16.LD.INCP.UAUP` only | TRM Section 1.5.1.2[^trm] |
| `GPIO_OUT` | 8 bits | `EE.WR_MASK_GPIO_OUT`, `EE.SET_BIT_GPIO_OUT`, `EE.CLR_BIT_GPIO_OUT` | The GPIO matrix, as a processor output port | TRM Section 1.5.2.1[^trm] |
| `GPIO_IN` | 8 bits | The GPIO matrix, as a processor input port | `EE.GET_GPIO_IN` | TRM Section 1.5.2.2[^trm] |

Two of these deserve more than a table row.

**SAR is shared with the base ISA.** It is the same 6-bit shift amount
register the scalar funnel shifts use, listed in the manual as a plain
special register rather than a custom one.[^trm] A scalar `ssr` or `ssl`
between two vector multiplies changes the rounding of the second multiply.
The esp-dsp fixed-point add kernel sets it with an ordinary `wsr.sar` before
entering the vector loop.[^espdsp-add]

How many bits of it a vector shift reads is stated twice in the manual and
not the same way. Section 1.5.1.2 says `EE.VSR.32` and `EE.VSL.32` use "the
lower 5 bits of SAR". Their own entries at 1.8.186 and 1.8.191 call the
register 6 bits and shift by `SAR[5:0]`, and Espressif's QEMU model hands
its helper the whole register with no 5-bit mask.[^trm-vsx][^qemu-vsx] Take
the per-instruction entries as the authority. Note also that only `wsr.sar`
can put a value above 31 in there: `ssr` writes `AR[s][4:0]` and clears the
top bit of SAR, and `ssai` takes 0 to 31.[^isa-ssr] Nothing states what a
32-bit lane does when shifted by 32 or more, so keep the amount inside 0 to
31, where the two readings agree.

**SAR_BYTE is the unaligned load path.** PIE loads and stores force their
addresses to be aligned by replacing the low address bits with zero, so a
128-bit access to `0x3fc80024` reads `0x3fc80020` instead. The manual states
this plainly and gives that exact example.[^trm] The masking is silent:
there is no exception and no status bit. The supported way to read a
128-bit value from an address that is not 16-byte aligned is the three
instruction sequence the manual gives, which the esp-dsp `memcpy` kernel
uses as its main loop:[^trm][^espdsp-memcpy]

```
ee.ld.128.usar.ip   q0, a8, 16   // loads the aligned block, and writes the
                                 // low 4 address bits into SAR_BYTE
ee.vld.128.ip       q1, a8, 16   // loads the next aligned block
ee.src.q            q2, q0, q1   // concatenates and shifts by SAR_BYTE bytes
```

Everything the shift depends on travels in SAR_BYTE. Nothing in the third
instruction names it.

## 5. Instruction encoding and the assembler

| Fact | Value | Source |
|---|---|---|
| Maximum instruction size on this core | 4 bytes | `core-isa.h`, `XCHAL_MAX_INSTRUCTION_SIZE`[^coreisa] |
| FLIX bundling | not configured (`XCHAL_HAVE_FLIX3` is 0) | `core-isa.h`[^coreisa] |
| Mnemonic strings in the ESP32-S3 dynconfig | 217 beginning with `ee.` | [measured][^m-enc] |
| Same count in the ESP32 dynconfig | 0 | [measured][^m-enc] |

PIE instructions come in two widths, and the split is not lopsided. The
manual works through the encoding of `EE.ZERO.QACC` and calls it a 24-bit
instruction.[^trm] The ESP32-S3 dynconfig sorts its 217 `ee.` opcodes into
two slot formats: 128 sit in the 24-bit `inst` slot and 89 in the 32-bit
slot.[^m-enc] What decides the width is the memory access, not the operand
count. Every form that fuses a load or a store on to an operation is 32
bits, and so are `ee.ldf.*`, `ee.stf.*`, `ee.ldxq.32` and `ee.stxq.32`.
`ld.qr`, `st.qr` and `mv.qr` are 24 bits.[^m-enc] Since FLIX is off, the
32-bit ones are single wide instructions and not bundles.[^coreisa]

The opcode table is not in the assembler. It lives in a per-target dynconfig
shared library that the driver passes with `--dynconfig`. `xtensa-esp-elf-as`
with no dynconfig rejects every `ee.` mnemonic as an unknown opcode, and
`xtensa-esp32s3-elf-as`, which is the same binary with the ESP32-S3
dynconfig selected, assembles all of them.[^m-enc] The same applies in
reverse to `objdump`: without the dynconfig it renders PIE encodings as
`excw`.[^m-enc] If a disassembly listing is full of `excw`, the tool is
missing its configuration, not reading corrupt code.

## 6. Context save is lazy, per task, and skips q registers on a yield

ESP-IDF uses the standard Xtensa lazy coprocessor scheme.[^vectors] On a
context switch the port saves `CPENABLE` into the task's coprocessor save
area and clears it.[^portasm] The next PIE instruction any task executes
then traps with EXCCAUSE 35.[^corebits] The handler `_xt_coproc_exc` saves
the previous owner's coprocessor 3 state into that task's save area,
restores the new owner's state, sets bit 3 of `CPENABLE`, and records the new
owner.[^vectors] A task that never touches PIE never pays for PIE state.

Three consequences matter when writing a kernel.

**A PIE instruction outside a task context panics.** If the coprocessor
exception fires when there is no owning thread, the handler writes
`PANIC_RSN_COPROCEXCEPTION` to `EXCCAUSE` and calls the panic
handler.[^vectors] That rules out PIE in an interrupt service routine under
the default configuration.

**A voluntary yield does not save q registers.** Every coprocessor 3
register is classified caller-saved in the save-area description, and
`tie-asm.h` labels the whole group "custom caller-saved registers not used by
default by the compiler".[^tie][^tieasm] `_xt_coproc_savecs`, which runs on a
solicited switch, selects only the callee-saved subset
(`XTHAL_SAS_CALE`).[^ctx] For coprocessor 3 that subset is empty, so nothing
is saved. This is correct by the ABI, because a voluntary switch happens
inside a function call where caller-saved state is already dead. It is also
why a kernel must not expect q register contents to survive a call to
anything.

**Do not write `CPENABLE` from a kernel.** The lazy scheme is what saves the
previous owner's state. Setting the bit by hand skips the exception, so the
previous owner's q registers and accumulators are never written to its save
area and are then overwritten. A bare-metal harness with one thread of
control can set `CPENABLE` in its own startup, because there is no other
owner to lose.

**`GPIO_OUT` is not saved.** The ESP-IDF save list covers user registers 0
to 11 and 13 to 18, skipping 12.[^tie] User register 12 is `GPIO_OUT`,
confirmed by assembling `rur.gpio_out` and reading the encoding's user
register field.[^m-ureg] That is reasonable, since `GPIO_OUT` drives pins
rather than holding per-task data, but it does mean two tasks using the fast
GPIO port share one 8-bit register with no arbitration.

## 7. What GCC knows about PIE: nothing

GCC 14.2 has no model of the q registers.

| Test | Result | Source |
|---|---|---|
| `__asm__("" ::: "q0")` | Error: unknown register name 'q0' in 'asm' | [measured][^m-gcc] |
| `int __attribute__((vector_size(16)))` addition | Four scalar `add.n` on AR registers, no PIE instruction | [measured][^m-gcc] |
| Documented Xtensa constraint letters | `a`, `b`, `A`, `I`, `J`, `K`, `L`. None names a q register | GCC 14 manual[^gccdoc] |

The header that describes the save area agrees, marking every coprocessor 3
register as not used by the compiler.[^tie][^tieasm]

For inline assembly this cuts both ways.

- You cannot ask for a q register operand, and you cannot declare one
  clobbered. A q register enters and leaves an asm block only through
  memory or through an AR register you loaded yourself.
- You do not have to declare a clobber, because no code GCC emits for the
  same function will ever hold a live value in a q register. Within one
  task, q registers are yours between the points where the scheduler can
  intervene.
- The AR registers you use for addresses and scalars are ordinary operands
  and follow the normal constraint and clobber rules.
- `SAR` is the exception that needs care, and the hazard runs one way.
  GCC 14.2 has no model of `SAR` either. It re-emits `ssr` or `ssl` before
  every variable shift and never reuses a value it did not just write, and
  a constant shift compiles to `srai` or `slli`, which do not touch the
  register at all. So compiler-emitted code never depends on a `SAR` an asm
  block left behind. The reverse is not safe: a variable shift placed
  between two asm blocks overwrites `SAR`, and the second block then reads
  the compiler's amount rather than the one the first block set.[^m-sar]
  Keep the `wsr.sar` and every instruction that depends on it inside one
  asm block.

## 8. esp-dsp as the reference corpus

Espressif's DSP library is the largest open PIE assembly corpus, published
under Apache 2.0. Files with the `_aes3` suffix are the ESP32-S3 PIE
versions, as against `_ae32` for base Xtensa assembly and `_ansi` for
portable C. At commit `3c8ac0f` there are 29 such files.[^espdsp] Useful
reading, by what each shows:

| Path | Shows |
|---|---|
| `modules/support/mem/esp32s3/dsps_memcpy_aes3.S` | The unaligned `usar` and `src.q` pipeline, software pipelined three deep[^espdsp-memcpy] |
| `modules/math/add/fixed/dsps_add_s16_aes3.S` | Setting `SAR`, a runtime alignment and length guard that falls back to scalar code, and a fused load-and-add inside `loopnez`[^espdsp-add] |
| `modules/dotprod/fixed/dsps_dp_s8_aes3.S` | Clearing `ACCX` through `wur.accx_0` and `wur.accx_1`, accumulating with `ee.vmulas.s8.accx.ld.ip`, reading back with `rur.accx_0`[^espdsp-dp] |

The alignment guard in the add kernel is the pattern worth copying. It tests
both input pointers against a mask of 15 and branches to the scalar path if
either is not 16-byte aligned, rather than letting the hardware silently
mask the address.[^espdsp-add]

## 9. Open questions

- **Cycle costs are not in this file.** TRM Section 1.7 documents data,
  resource and control hazards, including a per-instruction table of
  operand and special-register pipeline stages.[^trm] Turning that into a
  cost model belongs in a sibling leaf.
- **Writing `CPENABLE` needs an `RSYNC` after it.** Bit *n* enables
  coprocessor *n*, the register is 8 bits and privileged, and it is
  undefined after reset. An `RSYNC` must run between a write to `CPENABLE`
  and any instruction that references state the changed bits
  control.[^isa-cpen] A kernel does not write the register at all (Section
  6), but a bare-metal harness that does needs the sync.
- **The 217 dynconfig mnemonics match the manual's instruction count
  exactly.** TRM Section 1.8 numbers 220 entries, of which 1.8.1 to 1.8.217
  are `EE.*` and 1.8.218 to 1.8.220 are `LD.QR`, `ST.QR` and
  `MV.QR`.[^trm-1820] The dynconfig holds 217 unique `ee.` strings plus
  separate `ld.qr`, `st.qr` and `mv.qr` opcodes, so neither list carries
  aliases the other lacks.[^m-enc]
- **Reset values of the state registers** are given as 0 in the save-area
  description,[^tie] but the manual does not state them for PIE. Treat every
  state register as undefined at the start of a kernel and set what you
  read. [uncertain]

Anything above marked [uncertain] can be settled the same way the measured
rows were: assemble the case with the ESP32-S3 dynconfig and read the
encoding, or execute it under QEMU or on hardware and compare against a
portable reference.

## Footnotes

[^trm]: Espressif Systems, 2026. *ESP32-S3 Technical Reference Manual*,
    Version 1.8, Chapter 1 "Processor Instruction Extensions (PIE)",
    pages 39 to 303. Sections used: 1.2 Features, 1.3.1 to 1.3.5 structure
    and accumulators, 1.4.2 instruction field definition, 1.5.1 Table 1.5-1
    register list, 1.5.1.2 special registers, 1.5.2 fast GPIO interface,
    1.5.3 data format and alignment, 1.6.10 processor control instructions,
    1.7 instruction performance.
    <https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf>

[^trm-153]: Same manual, Section 1.5.3 "Data Format and Alignment", pages
    48 to 49: "there is no direct way to switch the data between the two
    special registers and memory. You can read and write data of QACC_H and
    QACC_L via five 4-byte (AR) registers or two 16-byte (QR) registers."

[^trm-qaccmem]: Same manual, Sections 1.8.20 to 1.8.23 and 1.8.60 to
    1.8.63, pages 96 to 99 and 136 to 139, the `EE.LD.QACC_*` and
    `EE.ST.QACC_*` forms, and Sections 1.8.29 to 1.8.36, pages 105 to 112,
    `EE.LDQA.*`.

[^trm-vsx]: Same manual, Sections 1.8.186 `EE.VSL.32` (page 268) and
    1.8.191 `EE.VSR.32` (page 274). Both read "the value in the 6-bit
    special register SAR" and both operate as a shift by `SAR[5:0]`,
    against Section 1.5.1.2's "the lower 5 bits of SAR" on page 47.

[^trm-1820]: Same manual, Sections 1.8.218 `LD.QR` (page 301), 1.8.219
    `ST.QR` (page 302) and 1.8.220 `MV.QR` (page 303). 1.8.220 is the last
    numbered entry of Chapter 1.

[^qemu-vsx]: Espressif QEMU fork, branch `esp-develop`, commit
    `febae182e132e4055529be423a818225ebddaa3a`,
    `target/xtensa/translate_tie_esp32s3.c`, `translate_vsx32_s3` and
    `HELPER(vsx32_s3)`. The translate function passes `cpu_SR[SAR]`
    unmasked. <https://github.com/espressif/qemu>

[^isa-ssr]: Cadence Tensilica, 2010. *Xtensa Instruction Set Architecture
    (ISA) Reference Manual*, issue 4/2010 (RC-2010.1). `SSR`, page 539:
    "The least significant five bits of address register as are written to
    SAR. The most significant bit of SAR is cleared. This instruction is
    similar to a WSR.SAR, but differs in that only AR[s]4..0 is used,
    instead of AR[s]5..0." `SSAI`, page 533: syntax `SSAI 0..31`, operation
    `SAR <- 0||sa`.

[^isa-cpen]: Same ISA manual, Section 4.3.9 "Coprocessor Option", page 64,
    and Table 5-184 "CPENABLE - Special Register #224", page 236. Bit *n*
    is coprocessor *n*; the register is 8 bits, privileged, and undefined
    after reset; "An RSYNC instruction must be executed after writing
    CPENABLE before executing any instruction that references state
    controlled by the changed bits of CPENABLE."

[^tie]: Cadence Design Systems, 2021 (Customer ID 15128, Build 0x90f1f).
    `xtensa/config/tie.h`, shipped in ESP-IDF 5.5.1 at
    `components/xtensa/esp32s3/include/xtensa/config/tie.h`. Definitions
    used: `XCHAL_CP_NUM`, `XCHAL_CP_MASK`, `XCHAL_CP3_NAME`,
    `XCHAL_CP_ID_COP_AI`, `XCHAL_CP3_SA_SIZE`, `XCHAL_CP3_SA_ALIGN`,
    `XCHAL_CP3_SA_NUM`, `XCHAL_CP3_SA_LIST`.

[^tieasm]: Cadence Design Systems, 2021. `xtensa/config/tie-asm.h`, ESP-IDF
    5.5.1, `components/xtensa/esp32s3/include/xtensa/config/tie-asm.h`,
    macros `xchal_cp3_store` and `xchal_cp3_load`. The comment classifying
    the group reads "Custom caller-saved registers not used by default by
    the compiler".

[^coreisa]: Cadence Design Systems, 2021. `xtensa/config/core-isa.h`,
    ESP-IDF 5.5.1,
    `components/xtensa/esp32s3/include/xtensa/config/core-isa.h`.
    `XCHAL_MAX_INSTRUCTION_SIZE` is 4, `XCHAL_HAVE_FLIX3` is 0,
    `XCHAL_HAVE_CP` is 1.

[^corebits]: Cadence Design Systems. `xtensa/corebits.h`, ESP-IDF 5.5.1,
    `components/xtensa/include/xtensa/corebits.h`. `EXCCAUSE_CP0_DISABLED`
    is 32 and `EXCCAUSE_CP3_DISABLED` is 35.

[^vectors]: Espressif Systems, ESP-IDF 5.5.1,
    `components/xtensa/xtensa_vectors.S`, symbols `_xt_coproc_exc`,
    `_xt_coproc_owner_sa`, `_xt_coproc_sa_offset`, and the
    `.L_xt_coproc_invalid` path that writes `PANIC_RSN_COPROCEXCEPTION`.

[^portasm]: Espressif Systems, ESP-IDF 5.5.1,
    `components/freertos/FreeRTOS-Kernel/portable/xtensa/portasm.S`, the
    `CPENABLE` save, clear and restore sequences in the context switch
    and interrupt entry paths.

[^ctx]: Espressif Systems, ESP-IDF 5.5.1,
    `components/xtensa/xtensa_context.S`, `_xt_coproc_savecs` and
    `_xt_coproc_restorecs`, which invoke `xchal_cp3_store` and
    `xchal_cp3_load` with `select=XTHAL_SAS_TIE|XTHAL_SAS_NOCC|XTHAL_SAS_CALE`.

[^gccdoc]: GNU Project, 2024. *GCC 14.2.0 Manual*, "Machine Constraints",
    Xtensa section.
    <https://gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Machine-Constraints.html>

[^espdsp]: Espressif Systems, *esp-dsp*, Apache 2.0, GitHub
    `espressif/esp-dsp` at commit
    `3c8ac0fdfec83740b783e200862c8d0c056de0ad` (2026-05-11). The repository
    holds 92 `.S` files, of which 29 carry the `aes3` suffix.
    <https://github.com/espressif/esp-dsp>

[^espdsp-memcpy]: `modules/support/mem/esp32s3/dsps_memcpy_aes3.S` at
    esp-dsp `3c8ac0f`, lines 123 to 149.

[^espdsp-add]: `modules/math/add/fixed/dsps_add_s16_aes3.S` at esp-dsp
    `3c8ac0f`. The alignment guard is the `movi a15, 0xF` and `bany` pair;
    the vector loop is `ee.vld.128.ip` plus `ee.vadds.s16.ld.incp` inside
    `loopnez`.

[^espdsp-dp]: `modules/dotprod/fixed/dsps_dp_s8_aes3.S` at esp-dsp
    `3c8ac0f`, the `wur.accx_0` / `wur.accx_1` clear, the `loopnez` body,
    and the `rur.accx_0` readback.

[^m-enc]: [measured] 2026-09-06, host assembly only, no device needed.
    Toolchain `xtensa-esp-elf` GCC 14.2.0 and GNU as 2.43.1, crosstool-NG
    `esp-14.2.0_20241119`. Mnemonic count:
    `strings lib/xtensa_esp32s3.so | grep -c '^ee\.'` gives 217, all
    unique, and the same over `lib/xtensa_esp32.so` gives 0. Width census:
    counting `Opcode_ee_*_Slot_inst_encode` strings in the same library
    gives 128 and counting `Opcode_ee_*_Slot_slot_format_32_0_encode`
    gives 89, which sum to 217; `ld.qr`, `st.qr` and `mv.qr` each carry an
    `_Slot_inst_encode` string. Individual encodings were confirmed by
    assembling with `xtensa-esp32s3-elf-as` and reading lengths from
    `xtensa-esp32s3-elf-objdump -d`: `ee.vld.128.ip`, `ee.src.q`,
    `ee.vadds.s16` and `ee.zero.qacc` are three bytes;
    `ee.vmulas.s8.accx.ld.ip` and `ee.ldf.128.ip` are four. The
    unknown-opcode and `excw` results come from running the same input
    through `xtensa-esp-elf-as` and `xtensa-esp-elf-objdump` with no
    `--dynconfig`.

[^m-ureg]: [measured] 2026-09-06, same toolchain and method as above.
    `rur.gpio_out a2` assembles to `e320c0`, `rur.qacc_l_4 a2` to `e320b0`,
    `rur.sar_byte a2` to `e320d0`. The last nibble tracks the user register
    number, which is 11 for `QACC_L_4` and 13 for `SAR_BYTE` in the save-area
    description,[^tie] placing `GPIO_OUT` at 12, the number the save list
    skips.

[^m-gcc]: [measured] 2026-09-06, same toolchain. `void f(void){
    __asm__ volatile("" ::: "q0"); }` compiled with
    `xtensa-esp32s3-elf-gcc -O2 -c` fails with "unknown register name 'q0'
    in 'asm'". `typedef int v __attribute__((vector_size(16))); v g(v a, v b)
    { return a+b; }` at `-O2` emits four `add.n` instructions on AR
    registers and no PIE instruction.

[^m-sar]: [measured] 2026-09-06, same toolchain, `-O2`. A function with two
    variable right shifts by the same amount emits `ssr` twice, once before
    each `sra`, so no `SAR` value is reused. A function whose body is
    `asm volatile("ssai 7" ::: "memory")`, then `p[0] = x >> n`, then
    `asm volatile("sll %0, %1" ...)` emits `ssr a3` between the two asm
    blocks, so the second block reads the compiler's shift amount and not
    the 7 the first block set. `return x >> 7` compiles to `srai`, which
    does not write `SAR`.
