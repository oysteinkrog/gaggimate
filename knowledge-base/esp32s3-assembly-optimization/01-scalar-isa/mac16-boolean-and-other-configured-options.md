---
title: MAC16, Boolean, and the other configured scalar options on the LX7
id: 01-scalar-isa/mac16-boolean-and-other-configured-options
schema_version: 1
doc_type: reference
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, lx7, mac16, boolean-option, gcc, codegen, core-isa, conditional-store]
confidence: medium
---

# MAC16, Boolean, and the other configured scalar options on the LX7

[The configured-options leaf](./core-isa-and-configured-options.md) lists
every `XCHAL_HAVE_*` macro this core was built with. [The scalar arithmetic
leaf](./scalar-arithmetic-shifts-and-bit-tricks.md) gives the instruction
encodings for `sext`, `clamps`, `min`/`max`, `nsa`/`nsau`, the multiply and
divide forms, and the two Boolean-gated conditional moves. This page does
not repeat either. It answers one narrower question for MAC16, the Boolean
Option, `MUL32`/`MUL32H`, `DIV32`, `NSA`/`NSAU`, `MINMAX`, `SEXT`, `CLAMPS`,
Conditional Store, and briefly Loop, Sync, and interrupt levels: **is the
option built into this core, and does GCC 14 ever emit the instruction from
ordinary C, or does reaching it need inline assembly?** Every codegen claim
below was checked by compiling a small C file with the installed
`xtensa-esp32s3-elf-gcc` (crosstool-NG esp-14.2.0_20241119, GCC 14.2.0) at
`-O2 -mlongcalls` and disassembling it with the matching
`xtensa-esp32s3-elf-objdump`, both from
`~/.platformio/packages/toolchain-xtensa-esp-elf/bin/`.[^gcc]

## MAC16: present, and GCC reaches for it more than expected

`XCHAL_HAVE_MAC16` is 1 in the packaged ESP-IDF 5.5.1 header, line 97 of
`components/xtensa/esp32s3/include/xtensa/config/core-isa.h`.[^coreisa] The
option adds a 40-bit accumulator (`ACCLO`/`ACCHI`), four 32-bit data
registers `MR[0..3]` (`m0`..`m3` in assembler), and 72 instructions built
around a 16x16 multiply whose two operands can each come from an address
register or an `MR` register, high or low half.[^isa-mac16] Four of the 72
also load an `MR` register from memory with auto-increment or
auto-decrement (`LDINC`, `LDDEC`) in the same instruction as the
multiply-accumulate.[^isa-mac16] `CLAMPS` is documented as its natural
partner, clamping an accumulator result to 16 bits before a store, though
nothing in `CLAMPS` itself requires MAC16.[^isa-mac16-clamps]

[The configured-options leaf](./core-isa-and-configured-options.md) states
that "modern GCC does not generate it from ordinary C; reaching it needs
inline asm or a compiler builtin." That is only true part of the time.
Compiling this reduction loop over 16-bit inputs,

```c
int macloop(const short *a, const short *b, int n) {
    int acc = 0;
    for (int i = 0; i < n; i++) acc += (int)a[i] * (int)b[i];
    return acc;
}
```

at `-O2` produces one `wsr.acclo` before a hardware `LOOP`, a single
`mula.aa.ll` plus two 16-bit loads inside it, and one `rsr.acclo` after it,
moving the 40-bit accumulator entirely off the register file for the
loop's whole run.[^gcc]

At `-Os` GCC still chooses MAC16 for the same source, but the shape is
worse: an ordinary `blt`-terminated loop, not a hardware `LOOP`, with the
`wsr.acclo`/`rsr.acclo` pair **inside** the loop body every iteration
instead of once outside it.[^gcc] At `-O1` GCC does not use MAC16 at all:
it widens each `short` load, calls `mull`, and accumulates with `add.n`
into a general register.[^gcc] Changing the array element type from
`short` to `int` also turns MAC16 off at every level tried, since MAC16's
multiplier only ever takes 16-bit operands.[^gcc] The rule this supports: a
16x16 accumulate-into-32-bit reduction loop is exactly the shape GCC's
Xtensa backend recognizes for MAC16, but only at `-O2` and above, so check
the disassembly at your project's actual optimization level rather than
assume it. This contradicts the configured-options leaf's blanket claim;
see Open questions.

## Boolean Option: present, and it is how a float compare reaches a branch

`XCHAL_HAVE_BOOLEANS` is 1, line 94 of the same header.[^coreisa] The option
adds 16 one-bit registers (`BR`, addressed individually as `b0`..`b15`) plus
branches and logic that read them: `ALL4`/`ALL8` (true only if all 4 or 8
tested Booleans are true), `ANY4`/`ANY8` (true if any are), `ANDB`,
`ANDBC`, `ORB`, `ORBC`, `XORB`, and the two branches `BT`/`BF`.[^isa-bool] A
coprocessor comparison, not general integer code, is what produces a
Boolean: ordinary condition-code architectures have one place to store a
compare result, while multiple Booleans let a kernel do several compares
before consuming any, which matters for SIMD-style code testing several
lanes at once.[^isa-bool] `ANY2`/`ALL2` do not exist; the manual notes
`ANDB`/`ORB` on `b_s+0` and `b_s+1` already give that result.[^isa-bool]

The Floating-Point Coprocessor Option's own prerequisites are "Coprocessor
Option ... and Boolean Option," and every `.S` comparison (`OEQ.S`,
`OLE.S`, `OLT.S`, `UEQ.S`, `ULE.S`, `ULT.S`) writes its result into a
Boolean register, never an integer one.[^isa-fp-prereq] Plain integer C
code has no route to a Boolean on this core: the only producer is an FP
compare. Compiling `if (a < b) side_a(); else side_b();` for `float a, b`
at `-O2` gives `olt.s b0, f0, f1` followed by `bf b0, ...`, the Branch-if-
Boolean-False form, to route around the taken side.[^gcc] The selection
form of the same comparison, `return (a < b) ? x : y;`, compiles instead
to the same `olt.s` feeding `movt a5, a4, b0` (move if true), no branch at
all.[^gcc] So on this core: a float comparison used for control flow with
real side effects on both arms costs a compare plus `BT`/`BF`; the same
comparison used only to pick a value costs a compare plus `MOVT`/`MOVF`
and no branch. Both are Boolean Option instructions, and neither is
reachable from integer-only C.

## MUL32, Mul32High, and DIV32: all present, GCC uses all four directly

`XCHAL_HAVE_MUL32` (`MULL`, low 32 bits of a 32x32 product),
`XCHAL_HAVE_MUL32_HIGH` (`MULUH`/`MULSH`, high 32 bits), and
`XCHAL_HAVE_DIV32` (`QUOS`/`QUOU`/`REMS`/`REMU`) are all 1, lines 64, 65,
and 66.[^coreisa] GCC lowers plain `int * int` to `MULL` on its own
(already shown in the sibling leaf), and `(long long)a * (long long)b` for
two `int` inputs compiles to one `mulsh` (the high half) plus one `mull`
(the low half), no call to a software 64-bit multiply routine; the
unsigned high-half version compiles to a bare `muluh`.[^gcc] Signed and
unsigned `/` and `%` on `int` compile straight to `quos`, `quou`, `rems`,
`remu`, one instruction each,
no library call.[^gcc] The ISA reference gives semantics only, never a
cycle count, for either option: it leaves the algorithm to the
implementation and warns that "some hardware implementations may be slower
than the software implementations for some operand values."[^isa-div] No
per-core LX7 latency for `MULL` or `QUOS` is published anywhere consulted
for this leaf, so treat both as correct but not free, and get a real cycle
number from a device or QEMU measurement before budgeting either in a hot
loop. [uncertain]

## NSA/NSAU, MINMAX, and SEXT: all present, all reachable without inline asm

`XCHAL_HAVE_NSA` (line 58), `XCHAL_HAVE_MINMAX` (line 59), and
`XCHAL_HAVE_SEXT` (line 60) are all 1.[^coreisa] `NSAU` (leading zero
count) backs `__builtin_clz` directly, and `NSA` (leading redundant
sign-bit count) backs `__builtin_clrsb` directly: both compile to the bare
instruction, no library fallback.[^gcc] A plain ternary `a < b ? a : b` /
`a > b ? a : b` compiles to `min`/`max` directly, the unsigned form to
`minu`, confirming the sibling leaf's encoding table is also what GCC
reaches for from ordinary comparison code, not just an instruction that
exists on paper.[^gcc] A bitfield sign extension (a `struct` with an
`int v : 8` member assigned then read back) compiles to one
`sext ar, as, 7`, stronger than "needs a shift-left/shift-right pair": GCC
recognizes the bitfield shape and reaches for `SEXT` on its own, at least
for this exact assign-then-read pattern.[^gcc]

## CLAMPS: present in the header, absent from GCC's own target config, still accepted by the assembler

`XCHAL_HAVE_CLAMPS` is 1 in `core-isa.h`, line 62.[^coreisa] But GCC's own
predefined macro for the same option, `__XCHAL_HAVE_CLAMPS`, is **0**.
Both `xtensa-esp32s3-elf-gcc` and `xtensa-esp32-elf-gcc` in this toolchain
package agree on that. Running `xtensa-esp32s3-elf-gcc -dM -E -x c
/dev/null | grep CLAMPS` prints `#define __XCHAL_HAVE_CLAMPS 0`.[^gcc]
Compare that against `core-isa.h`'s own `XCHAL_HAVE_CLAMPS`, which expands
to `1` when preprocessed with the ESP-IDF include path added.[^gcc] The two macro
names differ only in a leading underscore pair, and they disagree. Not a
typo: `xtensa-esp32s3-elf-gcc` is a 375 KB binary distinct from the generic
2 MB `xtensa-esp-elf-gcc`, built with a baked-in per-chip Xtensa
configuration that drives its own scheduling and `__XCHAL_*` macros, and
that baked-in configuration says no `CLAMPS`, regardless of which
`core-isa.h` a `#include` pulls in.[^gcc] Compiling the textbook
saturating-clamp shape (`if (x > 127) return 127; if (x < -128) return
-128; return x;` on an `int`) at `-O2` produces a `max` then a `min`
against two loaded immediates, two instructions, never `clamps`.[^gcc]
That matches the configured-options leaf's claim for this instruction.
But the assembler still accepts the mnemonic when written by hand:
`echo 'clamps a2, a2, 7' | xtensa-esp32s3-elf-as -o t.o` assembles clean,
and `xtensa-esp32s3-elf-objdump -d t.o` reads it back as `clamps a2, a2, 7`
at encoding `332200`.[^gcc]

This reproduces exactly what an earlier pass through this bucket flagged.
The assembler's `CLAMPS` gate follows `core-isa.h`, and the silicon has the
instruction. GCC's optimizer never plans around it existing, because its
internal config says it does not. So the only way to reach `CLAMPS` on
this chip is to write it by hand, in inline or standalone assembly. Never
count on the compiler finding it on its own.

## Conditional Store: present, and both `__sync` idioms use it

`XCHAL_HAVE_S32C1I` is 1 (the configured-options leaf already covers this;
restated here since Conditional Store is in this leaf's scope). The option
adds one instruction, `S32C1I`, which stores to memory only if the memory
still holds the value in `SCOMPARE1`, atomically, and returns the old
value either way; `Prerequisites: Multiprocessor Synchronization
Option`.[^isa-cas] The manual's own worked example is an atomic increment:
read, write the read value to `SCOMPARE1`, compute the new value, `S32C1I`,
retry on mismatch.[^isa-cas] GCC reaches `S32C1I` from both common atomic
builtins. `__sync_bool_compare_and_swap` compiles to a single
`wsr.scompare1` / `s32c1i` pair with no retry loop, since the builtin only
promises one attempt, and `__sync_fetch_and_add` compiles to the manual's
retry shape: `wsr.scompare1` / `s32c1i` / `bne ... retry`.[^gcc] There is
no load-linked/store-conditional pair here (`XCHAL_HAVE_EXCLUSIVE` is 0,
`core-isa.h` line 71) and no atomic wider than one 32-bit word; anything
bigger needs a lock built from `S32C1I`.[^coreisa]

## Loop, Sync, and interrupt levels, briefly

**Loop** (`LOOP`/`LOOPNEZ`/`LOOPGTZ`, `XCHAL_HAVE_LOOPS` = 1) has its own
leaf: [zero-overhead loops](./zero-overhead-loops.md). It matters here
because the MAC16 measurement above shows the two options combining: at
`-O2` GCC wraps the `mula.aa.ll` body in a hardware `LOOP`; at `-Os` it
does not, part of why that shape is worse, not just smaller.

**Sync.** `ISYNC`, `RSYNC`, `ESYNC`, and `DSYNC` are core architecture
instructions, not a separate option.[^isa-sync] Each waits for previously
issued special-register writes to take effect, before an instruction
fetch, a register-field use, a register-value use, or a load/store address
use respectively, and each stronger form also does the weaker ones.[^isa-sync]
One rule matters for any kernel touching `PS`: a `WSR.PS` or `XSR.PS`
followed by `RSIL` needs an `ESYNC` between them, to guarantee the written
value comes back on the next read.[^isa-rsil] The manual also warns that
"on some Xtensa implementations the latency of RSR is greater than one
cycle," so a special-register read's result should be used a few
instructions later where the schedule allows.[^isa-sync]

**Interrupt levels and RSIL.** `XCHAL_NUM_INTLEVELS` is 6 and
`XCHAL_EXCM_LEVEL` is 3 (`core-isa.h` lines 359 and 361): a C interrupt
handler can run at level 1, 2, or 3, and anything at level 4 or above must
be hand-written assembly, since the C runtime assumes `PS.EXCM` is
set.[^coreisa] `RSIL at, 0..15` reads the current `PS` and raises
`PS.INTLEVEL` in one step, and the manual states plainly that "on some
Xtensa ISA implementations the latency of RSIL is greater than one cycle,
and so it is advantageous to schedule uses of the RSIL result later."[^isa-rsil]
The instruction right after `RSIL` already runs at the new level, so no
sync is needed for that; a sync is needed only around explicit `PS` reads
and writes, as above.[^isa-rsil] No source read for this leaf gives an
actual LX7 cycle count for `RSIL`; Cadence stays implementation-agnostic
here, the same as for `MULL`/`QUOS`. [uncertain] Settling it needs a
device or QEMU measurement of a disable/enable pair back to back, out of
scope for a generic options leaf.

## Open questions

- This leaf's MAC16 measurement contradicts
  [the configured-options leaf](./core-isa-and-configured-options.md),
  which says "modern GCC does not generate it from ordinary C." That leaf
  needs a reconciling edit; this leaf does not touch it, out of scope for
  this pass. The two claims likely describe different things: general
  MAC16 forms with address-register operands may still need inline asm,
  while the 16x16 accumulate-reduction shape measured here is reached by
  ordinary, optimization-level-dependent pattern matching. A future edit
  should state the claim at that finer grain, not as a blanket no.
- Why GCC's `-O2` MAC16 shape hoists `wsr.acclo`/`rsr.acclo` outside the
  loop while `-Os` does not is unexplained by anything read here. [uncertain]
- No cycle cost for `RSIL`, `RSR`, `QUOS`/`QUOU`/`REMS`/`REMU`, or `MULL`
  on this core came from any source consulted; each needs a device or
  QEMU measurement before use in a cycle budget.

## Sources

[^coreisa]: Espressif, ESP-IDF 5.5.1 packaged Xtensa toolchain,
`components/xtensa/esp32s3/include/xtensa/config/core-isa.h`, installed at
`~/.platformio/packages/framework-espidf/components/xtensa/esp32s3/include/xtensa/config/core-isa.h`.
Line numbers cited above: `XCHAL_HAVE_LOOPS` 56, `XCHAL_HAVE_NSA` 58,
`XCHAL_HAVE_MINMAX` 59, `XCHAL_HAVE_SEXT` 60, `XCHAL_HAVE_CLAMPS` 62,
`XCHAL_HAVE_MUL32` 64, `XCHAL_HAVE_MUL32_HIGH` 65, `XCHAL_HAVE_DIV32` 66,
`XCHAL_HAVE_EXCLUSIVE` 71, `XCHAL_HAVE_BOOLEANS` 94, `XCHAL_HAVE_MAC16` 97,
`XCHAL_NUM_INTLEVELS` 359, `XCHAL_EXCM_LEVEL` 361.

[^isa-mac16]: Cadence/Tensilica, *Xtensa Instruction Set Architecture (ISA)
Reference Manual* (4/2010), Section 4.3.7 "MAC16 Option," p. 60, Table
4-37 and Table 4-38, p. 61.

[^isa-mac16-clamps]: Same manual, Section 4.3.7.2 "Use With CLAMPS
Instruction," p. 62.

[^isa-bool]: Same manual, Section 4.3.10 "Boolean Option," pp. 65-66,
Table 4-43, Table 4-44, and Section 4.3.10.2 "Booleans," p. 66.

[^isa-fp-prereq]: Same manual, Section 4.3.11 "Floating-Point Coprocessor
Option," p. 66, "Prerequisites: Coprocessor Option (page 63) and Boolean
Option (page 65)."

[^isa-div]: Same manual, Section 4.3.6 "32-bit Integer Divide Option," p.
59 (the "may be slower than software" caveat is stated for this option and
for 4.3.5, 32-bit Integer Multiply, in the same terms).

[^isa-cas]: Same manual, Section 4.3.13 "Conditional Store Option," p. 77,
Table 4-52/4-53, and Section 4.3.13.2, p. 78 (the atomic-increment
example).

[^isa-sync]: Same manual, Section 3.8.10 "Processor Control Instructions,"
p. 45, Table 3-23, including the RSR latency note.

[^isa-rsil]: Same manual, Chapter 6, "RSIL, Read and Set Interrupt Level,"
pp. 497-498, including the RSIL latency note and the ESYNC-after-WSR.PS
rule.

[^gcc]: Measured on this machine, 2026-09-06, crosstool-NG
`esp-14.2.0_20241119`, `xtensa-esp32s3-elf-gcc (crosstool-NG
esp-14.2.0_20241119) 14.2.0`, target `xtensa-esp-elf` (a single-target
build; `-mcpu=esp32s3` is not recognized by this GCC). Every C snippet
above was compiled with `xtensa-esp32s3-elf-gcc -O2 -mlongcalls -c
<file>.c` (the `-O1`/`-Os` MAC16 variants use those flags in place of
`-O2`) and read back with `xtensa-esp32s3-elf-objdump -d`. The
`__XCHAL_HAVE_CLAMPS` predefined macro was read with
`xtensa-esp32s3-elf-gcc -dM -E -x c /dev/null`; the header's
`XCHAL_HAVE_CLAMPS` was read by preprocessing a file including
`xtensa/config/core-isa.h` with `-I` pointed at the path in [^coreisa]. The
assembler test used `xtensa-esp32s3-elf-as` and `xtensa-esp32s3-elf-objdump`
from the same toolchain package.
