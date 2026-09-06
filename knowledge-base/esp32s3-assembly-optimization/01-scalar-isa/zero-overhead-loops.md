---
title: Zero-overhead loops on the ESP32-S3 (LOOP, LOOPNEZ, LOOPGTZ)
id: 01-scalar-isa/zero-overhead-loops
schema_version: 1
doc_type: reference
status: review
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, lx7, loop, hardware-loop, gcc, binutils, inline-asm]
confidence: high
---

# Zero-overhead loops on the ESP32-S3

The ESP32-S3's Xtensa LX7 cores are configured with the Loop Option, so the
back edge of a counted loop can be free: no compare, no branch, no branch
penalty.[^coreisa][^isa-loopopt] The feature is three instructions (`LOOP`,
`LOOPNEZ`, `LOOPGTZ`), three special registers, and a set of restrictions
that are easy to trip over. This page covers those restrictions, the
conditions GCC 14 needs before it emits a hardware loop, and how to check
that you got one. For the option in the wider table of what this core was
configured with, see
[the configured-options leaf](./core-isa-and-configured-options.md); for
reading a lost loop out of a real function's assembly alongside its other
symptoms, see
[the register-pressure leaf](../04-toolchain-and-codegen/register-pressure-spills-and-reading-the-assembly.md).

## The three instructions

All three are `BRI8` format, three bytes, and all three set the same state.
They differ only in the guard they apply before entering.[^isa-loopopt]

| Instruction | Guard | Behaviour when the guard fails |
|---|---|---|
| `LOOP as, label` | none | always enters the body |
| `LOOPNEZ as, label` | `as != 0` | skips to `label` |
| `LOOPGTZ as, label` | `as > 0` (signed) | skips to `label` |

The operation for `LOOP` is:[^isa-loop]

```
LCOUNT <- AR[s] - 1
LBEG   <- PC + 3
LEND   <- PC + zero_extend(imm8) + 4
```

`LOOPNEZ` and `LOOPGTZ` perform the same three writes and then, if the guard
fails, set `nextPC` to the same address they put in `LEND`. The loop
registers are written even when the loop is skipped.[^isa-loopgtz]

Three consequences of that pseudocode:

- **The trip count is read once, from a general register.** `AR[s]` is
  sampled at the `LOOP` instruction; nothing later re-reads it. The register
  is free for the body to reuse as far as the hardware is concerned, though
  GCC keeps it live (see below).
- **The count is off by one, and zero means the maximum.** `LCOUNT` gets
  `as - 1`. A plain `LOOP` with `as == 0` iterates 2^32 times.[^isa-loop] Use
  `LOOPNEZ` unless you have already proved the count is positive.
- **`LEND` is the address after the body, not the last instruction.** The
  body runs from `LBEG` (the instruction after `LOOP`) up to but not
  including `LEND`.

The three loop registers are `LBEG` (Special Register 0, loop begin
address), `LEND` (1, loop end address) and `LCOUNT` (2, iterations
remaining). All three are 32 bits and read/write through `RSR`, `WSR` and
`XSR`. After reset `LBEG` and `LEND` are undefined and `LCOUNT` is
zero.[^isa-loopopt]

## Loopback semantics

The processor computes the next PC as:[^isa-loopback]

```
if LCOUNT != 0 and CLOOPENABLE and nextPC = LEND then
    LCOUNT  <- LCOUNT - 1
    nextPC  <- LBEG
endif
```

The condition is on the PC *incrementing* to `LEND`, not on reaching it by
any means. A taken branch or jump to `LEND` therefore does not loop back,
which is how you leave a loop early.[^isa-loopback] There is no way to start
the next iteration from the middle of the body; the documented workaround is
to branch to a `NOP` placed as the last instruction of the body.[^isa-loop]

## The rules you must not break

**Body size: 256 bytes.** `LEND` is `PC + 4 + imm8` with an unsigned 8-bit
`imm8`, so the furthest end label is 259 bytes past the 3-byte `LOOP`
instruction. That leaves 256 bytes of body.[^isa-loop] Measured against GNU
as from `xtensa-esp-elf` 14.2.0 (crosstool-NG esp-14.2.0_20241119) with
`--no-transform`: a 256-byte body assembles, a 258-byte body fails with
`operand 2 of 'loop' has out of range value '261'`.[^measured-range]

**No nesting.** There is one set of loop registers, so hardware loops cannot
be nested. The ISA manual says outer levels should use ordinary conditional
branches, and that a procedure call inside a hardware loop is usually a bad
idea because the callee may use a hardware loop of its own.[^isa-loop] GCC
follows this: only the innermost loop of a nest gets `loop`, and its setup
is re-executed on every outer iteration.[^measured-codegen]

**The last instruction of the body must not be a call, `ISYNC`, `WAITI` or
`RSR.LCOUNT`.** If the last instruction is a taken branch, `LCOUNT` becomes
undefined.[^isa-loopopt] A call at the end fails for the loopback reason
above: the return lands *on* `LEND` rather than incrementing to it, so the
loop exits.[^isa-loop]

**Alignment.** The first instruction of the body must fit entirely inside a
naturally aligned unit whose size is the next power of two at least as large
as the instruction, and at least 4 bytes.[^isa-loopopt] With a 32-bit fetch
width that means the `LOOP` instruction itself must sit at 1 or 2 mod
4.[^gas-align] The assembler fixes this for you by widening density
instructions or inserting a 2 or 3 byte no-op, and it does so even under
`--no-target-align`, because this is a correctness rule and not an
optimisation.[^measured-align]

**Loops are disabled while `PS.EXCM` is set.** This stops code from pointing
`LEND` into an exception handler and then taking the
exception.[^isa-excm] Practically: assembly written for an exception vector
prologue, before the handler clears `EXCM`, will not loop back. ESP-IDF's
low and medium priority interrupt vectors clear `EXCM` before entering C;
high priority (level above `XCHAL_EXCM_LEVEL`) handlers are expected to stay
in assembly and cannot call into the RTOS at all.[^idf-vectors]

## Interrupts and task switches

`LBEG`, `LEND` and `LCOUNT` are part of the saved context, so a loop
survives preemption. ESP-IDF's `_xt_context_save` stores all three under
`#if XCHAL_HAVE_LOOPS`, and `_xt_context_restore` writes them back;
`XCHAL_HAVE_LOOPS` is 1 for the ESP32-S3.[^idf-ctx][^coreisa] The FreeRTOS
Xtensa port calls the same two routines for its context
switch.[^idf-portasm] Nothing you write has to save them by hand. The
`PS.EXCM` rule above is the one interrupt interaction that does bite, and it
is about the handler's own code, not about the interrupted loop.

## Cost model

The ISA manual states that `LOOP` "is intended to be implemented with help
from the instruction fetch engine of the processor, and therefore should not
incur a mispredict or taken branch penalty".[^isa-loop] It qualifies this:
"In some implementations, `LOOP` takes an extra clock for the first loop
back of certain loops. In addition, certain instructions (such as `ISYNC` or
a write to `LEND`) may cause an additional cycle on the following loop
back."[^isa-loop] The manual does not give a cycle count for either the loop
back or the ordinary taken branch it replaces, because both are
implementation-specific. The ESP32-S3 TRM does give the taken-branch cost
for this core: 2 cycles, section 1.7.3 [Espressif 2026][^trm-branch]; see
[branches, jumps and control-flow costs](branches-jumps-and-control-flow-costs.md).
`[uncertain]` The loop-back cost itself is still unstated anywhere and needs
a device measurement.

The ESP32-S3 core configuration declares `XCHAL_LOOP_BUFFER_SIZE` of 256
bytes, described in the header as the "zero-ov. loop instr buffer
size".[^coreisa] `[uncertain]` What that buffer does for instruction fetch
on this part is not confirmed against a primary source.

What is solid: the back edge costs no instructions, no register, and no
branch. You pay for the setup instead, which for GCC's counted loops is the
arithmetic that materialises the trip count plus the three-byte `loop`, four
instructions in the example below. In a nest, that setup is paid once per
outer iteration.

## GCC 14: when a hardware loop is emitted

There is no `-mloops` or `-mno-loops` command-line switch in the GCC 14
Xtensa backend. `TARGET_LOOPS` is defined straight from the core
configuration: `#define TARGET_LOOPS XCHAL_HAVE_LOOPS`.[^gcc-h] For the
ESP32-S3 that is always 1,[^coreisa] so the feature cannot be turned off
from the command line, only avoided by writing code the backend rejects.

The RTL patterns are `zero_cost_loop_start`, `zero_cost_loop_end`,
`loop_end` and the `doloop_end` expander, all four guarded by `"TARGET_LOOPS
&& optimize"`.[^gcc-md] So at `-O0` no hardware loop is ever emitted. The
only output template is `"loop\t%0, %l1_LEND"`,[^gcc-md] so GCC 14 emits
`loop` and never `loopnez` or `loopgtz`; it puts its own conditional branch
in front instead.

The backend's own conditions, from `xtensa.cc`:[^gcc-cc]

- `xtensa_can_use_doloop_p` returns false if `loop_depth > 1` or if the loop
  is not entered at the top. The comment reads "Considering limitations in
  the hardware, only use doloop for innermost loops which must be entered
  from the top."
- `xtensa_invalid_within_doloop` rejects a body containing a call
  ("Function call in the loop.") or a return.
- `hwloop_optimize`, the callback run from `machine_dependent_reorg`,
  rejects the loop for any of six reasons, each with its own line in the
  RTL dump: `loop->depth > 1` ("is not innermost"), no single incoming
  destination ("has more than one entry"), `loop->incoming_dest !=
  loop->head` ("is not entered from head"), `loop->has_call ||
  loop->has_asm` ("has invalid insn"), `loop->iter_reg_used ||
  loop->iter_reg_used_outside` ("uses iterator"), and the start label not
  preceding the loop end ("start_label not before loop_end").

The two lists overlap but are not the same test. `xtensa_can_use_doloop_p`
and `xtensa_invalid_within_doloop` run in the earlier `loop2_doloop` pass,
which decides whether to insert the `doloop` pattern at all;
`hwloop_optimize` runs late, in `machine_dependent_reorg`, and can still
throw the loop away after the pattern is in place. Inline asm is only
caught by the late one, which is why a loop can look accepted in the
`loop2_doloop` dump and still come out as `bnez`.

**Any inline asm in the body kills the hardware loop.** That is the
`loop->has_asm` test, and it applies to an `__asm__` statement anywhere in
the loop, however small. Measured with `xtensa-esp32s3-elf-gcc -O2 -S`
(14.2.0, esp-14.2.0_20241119): two identical 16-bit copy loops, one using a
single `__asm__ ("l16ui %0, %1, 0")` for the load and one plain C, produced
`bnez.n` and `loop` respectively.[^measured-asm] If you want both
hand-written inner code and a hardware loop, write the whole loop in asm.

GCC also plants a `nop` at the end of the body when the last body
instruction is a branch or a label, because of the taken-branch and loopback
rules above.[^gcc-emit] The generated shape, from `-O2 -S` on
`for (i = 0; i < n; i++) d[i] = s[i] + k;`:[^measured-codegen]

```
	extui	a5, a5, 0, 16     # narrow k to 16 bits
	blti	a4, 1, .L1        # GCC's own zero-trip guard
	add.n	a4, a4, a4
	addi	a8, a4, -2
	srli	a8, a8, 1
	addi.n	a8, a8, 1         # a8 = n
	loop	a8, .L3_LEND
.L3:
	l16ui	a9, a3, 0
	addi.n	a3, a3, 2
	add.n	a9, a5, a9
	s16i	a9, a2, 0
	addi.n	a2, a2, 2
	.L3_LEND:
.L1:
	retw.n
```

The `_LEND` suffix on the label comes straight from the output template, and
it is the string to grep for in a `.s` file.

## Writing `LOOP` in inline or standalone assembly

A worked example. Add one to each of `a4` 16-bit values, `a3` to `a2`:

```asm
	.text
	.align 4
	.global inc16
	.type inc16, @function
inc16:
	loopnez a4, .Linc_end     # skip entirely when a4 == 0
	l16ui   a5, a3, 0
	addi    a5, a5, 1
	s16i    a5, a2, 0
	addi.n  a3, a3, 2
	addi.n  a2, a2, 2
.Linc_end:
	ret
	.size inc16, .-inc16
```

Assembled and disassembled with the same toolchain:[^measured-example]

```
00000000 <inc16>:
   0:	f03d        nop.n              # inserted by gas for loop alignment
   2:	0b9476      loopnez a4, 11
   5:	001352      l16ui   a5, a3, 0
   8:	551b        addi.n  a5, a5, 1  # gas narrowed addi to addi.n
   a:	005252      s16i    a5, a2, 0
   d:	332b        addi.n  a3, a3, 2
   f:	222b        addi.n  a2, a2, 2
  11:	f00d        ret.n
```

Rules for hand-written loops:

- **Let the assembler align the loop.** Do not try to place `LOOP` yourself.
  The `nop.n` above is gas moving the instruction to 2 mod 4 so that the
  three-byte `l16ui` fits inside the aligned unit 4..7.
- **Put the end label after the last body instruction**, and nothing else
  between it and the body. In an `__asm__` block use a local `.L` label and
  keep the whole loop in one statement, so the compiler cannot move code
  into the range.
- **Watch for oversize bodies, because the assembler will not complain.**
  When the label is out of range, gas silently rewrites the entry sequence
  instead of erroring: it emits a `loop` to a near label and then patches
  `LEND` from a literal. Assembling a `loop` over a 300-byte body produced
  this prologue inside the body region: `loop a2, <near>`, `rsr.lend a2`,
  `wsr.lbeg a2`, `l32r a2, <literal>`, `nop`, `wsr.lend a2`, `isync`,
  `rsr.lcount a2`, `addi a2, a2, 1` (the last two restore the register the
  `loop` clobbered).[^measured-relax] The loop still runs at full speed, but
  entry now costs an `L32R` literal load and an `ISYNC`, and the manual
  warns that a write to `LEND` can add a cycle to the following loop
  back.[^isa-loop]
- **Prefix the mnemonic with an underscore to make that an error instead.**
  `_LOOP`, `_LOOPNEZ` and `_LOOPGTZ` disable the relaxation.[^isa-loop]
  Measured: `_loop` over a 300-byte body fails with `operand 2 of 'loop' has
  out of range value '303'`.[^measured-underscore]
- `[uncertain]` No interaction between `-mlongcalls` and hardware loops was
  found. `-mlongcalls` changes how calls are emitted, and calls are already
  excluded from hardware-loop bodies, so there is nothing obvious for it to
  affect. Not confirmed against a primary source.

## Verifying that you got one

1. **Read the assembly.** `gcc -O2 -S` and grep for `loop`, `loopnez`,
   `loopgtz` and the `_LEND` label suffix. For an object, `objdump -d`.
2. **Ask GCC why not.** `-fdump-rtl-mach` writes a `.mach` dump with the
   hardware-loop pass's verdict, one line per loop. A loop containing inline
   asm produces `;; loop 0 has invalid insn` followed by `;; loop 0 is
   bad`.[^measured-dump] `-fdump-rtl-loop2_doloop` shows the earlier pass
   that inserts the `doloop` pattern.
3. **Check the body size in the disassembly**, not in the source. The
   distance from the `loop` instruction to its target must be at most 259
   bytes, or you have quietly taken the relaxed entry sequence.

## Footnotes

[^trm-branch]: Espressif Systems, ESP32-S3 Technical Reference Manual, version 1.8, PDF dated 2026-03-04, section 1.7.3, page 74: a taken branch flushes the R and E stages, a 2-cycle cost.
[^coreisa]: ESP-IDF 5.5.1, `components/xtensa/esp32s3/include/xtensa/config/core-isa.h`, lines 56 to 57: `#define XCHAL_HAVE_LOOPS 1` and `#define XCHAL_LOOP_BUFFER_SIZE 256`.
[^isa-loopopt]: Tensilica, *Xtensa Instruction Set Architecture (ISA) Reference Manual*, issue date 4/2010, Section 4.3.2 "Loop Option" and 4.3.2.1 to 4.3.2.2, pages 54 to 56. https://0x04.net/~mwk/doc/xtensa.pdf
[^isa-loop]: Same manual, Chapter 6, `LOOP` instruction description, pages 391 to 393.
[^isa-loopgtz]: Same manual, Chapter 6, `LOOPGTZ` pages 393 to 395 and `LOOPNEZ` pages 395 to 397.
[^isa-loopback]: Same manual, Section 4.3.2.4 "Loopback Semantics", page 56.
[^isa-excm]: Same manual, Section 4.3.2.3 "Loops Disabled During Exceptions", page 56.
[^gas-align]: GNU binutils, GNU assembler manual, "Xtensa Automatic Alignment": "the first instruction in the loop body does not cross an instruction fetch boundary (e.g., with a 32-bit fetch width, a `LOOP` instruction must be on either a 1 or 2 mod 4 byte boundary)". https://sourceware.org/binutils/docs/as/Xtensa-Automatic-Alignment.html
[^idf-ctx]: ESP-IDF 5.5.1, `components/xtensa/xtensa_context.S`, lines 133 to 140 (save) and 274 to 281 (restore), both guarded by `#if XCHAL_HAVE_LOOPS`.
[^idf-portasm]: ESP-IDF 5.5.1, `components/freertos/FreeRTOS-Kernel/portable/xtensa/portasm.S`, lines 133 and 263: `call0 _xt_context_save` / `call0 _xt_context_restore`.
[^idf-vectors]: ESP-IDF 5.5.1, `components/xtensa/xtensa_vectors.S`: the comment "Set up PS for C, enable interrupts above this level and clear EXCM" at line 1229 and repeated at 1318, 1398, 1477, 1556 and 1635, one per interrupt level; the debug vector at line 744 clears EXCM with its own wording ("re-enable debug and NMI interrupts"); the high priority interrupt commentary is the block at lines 1674 to 1687, which states that such handlers "cannot interact with the RTOS".
[^gcc-h]: GCC 14 branch, `gcc/config/xtensa/xtensa.h`, line 52: `#define TARGET_LOOPS XCHAL_HAVE_LOOPS`. https://github.com/gcc-mirror/gcc/blob/releases/gcc-14/gcc/config/xtensa/xtensa.h
[^gcc-md]: GCC 14 branch, `gcc/config/xtensa/xtensa.md`, "Zero-overhead looping support" section: `zero_cost_loop_start` (output template `"loop\t%0, %l1_LEND"`), `zero_cost_loop_end`, `loop_end` and `doloop_end`, all with condition `"TARGET_LOOPS && optimize"`. https://github.com/gcc-mirror/gcc/blob/releases/gcc-14/gcc/config/xtensa/xtensa.md
[^gcc-cc]: GCC 14 branch, `gcc/config/xtensa/xtensa.cc`: `xtensa_can_use_doloop_p`, `xtensa_invalid_within_doloop`, `hwloop_optimize`, `hwloop_fail` and `xtensa_reorg`'s call to `reorg_loops`. https://github.com/gcc-mirror/gcc/blob/releases/gcc-14/gcc/config/xtensa/xtensa.cc
[^gcc-emit]: Same file, `xtensa_emit_loop_end` and its comment explaining why a `nop` is needed when the body ends in a label or a branch.
[^measured-range]: `[measured]` 2026-09-06, `xtensa-esp-elf-as --no-transform` from `xtensa-esp-elf` GCC 14.2.0 (crosstool-NG esp-14.2.0_20241119). Bodies of 250, 254 and 256 bytes assemble; 258 and 260 fail with `Error: operand 2 of 'loop' has out of range value '261'` and `'263'`.
[^measured-align]: `[measured]` 2026-09-06, same assembler. Two mechanisms were seen, both documented: in one input a `nop.n` was inserted ahead of the `loop`, in another a preceding `nop.n` was widened to a 3-byte `nop` instead, which is what the ISA manual describes for this case ("the insertion of NOP instructions or adjustment of which instructions are 16-bit density instructions").[^isa-loop] With `--no-transform` the same input warns `unaligned loop: N bytes at 0xM` and emits nothing to fix it; `--no-target-align` does not suppress the fix.
[^measured-asm]: `[measured]` 2026-09-06, `xtensa-esp32s3-elf-gcc -O2 -S` (14.2.0, esp-14.2.0_20241119). Two 16-bit copy loops in one translation unit: the one whose load is written as `__asm__ ("l16ui %0, %1, 0")` ends in `bnez.n`, the plain C one gets `loop a8, .L9_LEND`.
[^measured-codegen]: `[measured]` 2026-09-06, same compiler and flags, on `void f(uint16_t *d, const uint16_t *s, int n, uint16_t k) { for (int i = 0; i < n; i++) d[i] = s[i] + k; }`. A two-level nest compiled with `-fno-unroll-loops` puts `loop` on the inner loop only and re-runs the four-instruction setup inside the outer `bne` loop.
[^measured-example]: `[measured]` 2026-09-06, `xtensa-esp-elf-as` then `xtensa-esp-elf-objdump -d`, same toolchain, on the `inc16` listing above.
[^measured-relax]: `[measured]` 2026-09-06, same assembler without `--no-transform`, on a `loop` with a 300-byte body. Disassembly shows the nine-instruction relaxed entry sequence given in the text.
[^measured-underscore]: `[measured]` 2026-09-06, same assembler, `_loop a2, .Lend` with a 300-byte body: `Error: operand 2 of 'loop' has out of range value '303'`.
[^measured-dump]: `[measured]` 2026-09-06, `xtensa-esp32s3-elf-gcc -O2 -S -fdump-rtl-mach` on the inline-asm loop above; the `.339r.mach` dump contains `;; loop 0 has invalid insn` and `;; loop 0 is bad`.
