---
title: Reading GCC 14 Xtensa assembly for a hot loop
id: 04-toolchain-and-codegen/register-pressure-spills-and-reading-the-assembly
schema_version: 1
doc_type: how-to
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, gcc, codegen, register-pressure, spills, loop, abi]
confidence: high
---

# Reading GCC 14 Xtensa assembly for a hot loop

You have a loop that is too slow. Before writing assembly by hand, read what the
compiler already produced. Four faults account for most of the loss, and all four
are visible in the `.S` file: values spilled to the stack inside the loop, a lost
hardware loop, a library call in the body, and a load sitting next to the
instruction that consumes it.

All assembly quoted here was compiled on this machine with `xtensa-esp-elf-gcc
(crosstool-NG esp-14.2.0_20241119) 14.2.0` targeting
`-mdynconfig=xtensa_esp32s3.so`, and is tagged `[measured]`.

## 1. Produce the .S with the build's own flags

```sh
xtensa-esp32s3-elf-gcc -O2 -S -fverbose-asm -mlongcalls -o loop.s loop.c
```

Use the same `-O` level as the production build, because the optimisation level
decides whether the hardware loop appears at all (section 5). Use the same `-m`
flags: ESP-IDF adds `-mlongcalls` to every C, C++ and assembler compile through
its toolchain file, so a snippet built without it is not the code that
ships.[^idf]

`-fverbose-asm` puts the source line and variable name beside each instruction,
which turns a wall of registers into something readable. Two more flags help when
the output surprises you: `-fdump-rtl-loop2_doloop-details` says why the hardware
loop was refused, and `-Q --help=optimizers` says which passes are on.

## 2. Anatomy of a windowed function

Every compiled function opens with `entry` and closes with `retw` or `retw.n`:

```
good_loop:
	entry	sp, 32
	...
	retw.n
```

`entry` rotates the register window and subtracts the frame size from the stack
pointer in one instruction. The ISA defines it as `AR[s] - (0^17 || imm12 ||
0^3)`, so the immediate is scaled by eight and legal frame sizes run 0 to 32760
in steps of 8.[^entry] The number after `sp` is the whole frame, spill area
included, and it is the cheapest pressure reading you have. A leaf loop needing
no locals should show `entry sp, 32`; 48 or 64 means something was spilled.
`retw` reverses the rotation, taking its window count from the top two bits of
`a0`, which the calling `call4`, `call8` or `call12` wrote.[^entry]

## 3. How many address registers you actually have

Sixteen address registers exist, but not sixteen are yours. `a0` holds the return
address and `a1` is the stack pointer, and the ISA says both must always hold
those values, because debuggers and exception handling unwind through them.[^abi]
GCC marks exactly those two fixed, leaving the allocator `a2` to
`a15`.[^fixedregs] Arguments arrive in `a2` to `a7`, six of them; a seventh
argument onward is passed on the stack.[^abi][^fixedregs]

The number that decides your loop is what survives a call. Under the windowed ABI
a `call8` advances the window base by eight, so the callee's `a0` to `a7` are
physically the caller's `a8` to `a15`.[^entry] Those are destroyed by every
`call8`. GCC encodes this directly: in `CALL_REALLY_USED_REGISTERS`, `a2` to `a7`
are marked clobbered only under the CALL0 ABI, `a8` to `a11` under both, and
`a12` to `a15` under the windowed ABI.[^fixedregs]

| Function shape | Registers available for values |
|---|---|
| Leaf, makes no call | `a2` to `a15`, so 14 |
| Makes any `call8` | `a2` to `a7` across the call, so 6 |

GCC's windowed `REG_ALLOC_ORDER` is `8, 9, ..., 15, 7, 6, ..., 2`:[^fixedregs]
volatile high registers first, `a2` to `a7` saved for values that live across a
call. So a body using `a9` through `a15` freely is a leaf loop, and a body
crammed into `a2` to `a7` with stack traffic around it has a call in it.

## 4. Recognising spills

A spill is a store to an `a1`-relative address, a reload is a load from one. GCC
writes `a1` as `sp`, so grep the loop body for `sp,`:

```
	l32i	a6, sp, 0	# reload
	s32i	a6, sp, 4	# spill
```

Any `sp` reference between the loop label and the closing branch is work paid on
every iteration. Stack traffic in the prologue and epilogue is free by
comparison; ignore it. The same reduction written with twelve live accumulators
and with six, at `-O2` `[measured]`:

| Variant | Frame | Body insns | `sp` refs in body | Hardware loop |
|---|---|---|---|---|
| 12 accumulators, unrolled by 4 | `entry sp, 64` | 32 | 9 | no, `bnez.n` |
| 6 accumulators, unrolled by 2 | `entry sp, 32` | 11 | 0 | yes, `loop` |

Per element that is 8.0 instructions against 5.5, so the deeper unroll made the
loop worse. In the twelve-accumulator version even the incoming pointer argument
was spilled so its register could be reused, which is the signature of an
allocator out of room.

## 5. Recognising a lost hardware loop

A good loop opens with `loop a8, .L3_LEND` and ends at a bare `.L3_LEND:` label.
A lost one ends in an explicit counter update and a branch, `addi.n a6, a6, -1`
then `bnez.n a6, .L5`.

`LOOP` sets `LCOUNT`, `LBEG` and `LEND`, and the fetch engine handles the branch
back, so it should cost no mispredict and no taken-branch penalty.[^loop] Losing
it adds an add and a branch per iteration, against a `BRANCH_COST` of 3 in the
backend.[^fixedregs] Four causes, all checkable:

- **A call in the body.** `xtensa_invalid_within_doloop` returns "Function call
  in the loop." for any call insn.[^doloop] The hardware reason is in the ISA:
  `LCOUNT`, `LBEG` and `LEND` are single registers, so loops cannot nest and a
  callee might use a loop of its own.[^loop] A `return` as the last instruction
  is refused for the same reason: a return at `LEND` does not trigger the loop
  back.[^loop][^doloop]
- **Not innermost, or not entered at the top.** `xtensa_can_use_doloop_p` returns
  false when `loop_depth > 1` or `entered_at_top` is false.[^doloop]
- **A body over 256 bytes.** `LEND` is the `LOOP` address plus four plus a
  zero-extended 8-bit offset, so the body can be at most 256 bytes.[^loop]
- **`-Os`.** `[measured]` The same countable loop that gets `loop` at `-O1` and
  `-O2` gets `blt` at `-Os`. The dump names it: `Loop rejected by
  can_use_doloop_p`, because at `-Os` the loop was not shaped so that it is
  entered from the top.

To confirm which applies, read the dump. A good run says `Doloop: Inserting
doloop pattern (runtime iterations).`

```sh
xtensa-esp32s3-elf-gcc -O2 -S -mlongcalls -fdump-rtl-loop2_doloop-details -o /dev/null loop.c
grep -iE 'Doloop|reject' loop.c.*loop2_doloop*
```

## 6. Recognising libcalls

A `call8` to a name with two leading underscores is a compiler support routine.
It is a real call: it rotates the window, kills the hardware loop, and forces
every loop-carried value into `a2` to `a7`. Which ones appear depends on what the
chip has. On the ESP32-S3 configuration at `-O2` `[measured]`:

| Operation | Emitted |
|---|---|
| `uint32_t / uint32_t`, `int32_t / int32_t`, `%` | `quou`, `quos`, `remu`, inline |
| `float` add, multiply, `(float)int` | `add.s`, `mul.s`, `float.s`, inline |
| `uint64_t * uint64_t` | three `mull`, inline |
| `float / float` | `call8 __divsf3` |
| `sqrtf`, `fmodf` | `call8 sqrtf`, `call8 fmodf` |
| any `double` arithmetic | `call8 __muldf3` and friends |

Two things follow. Integer divide and single-precision add, multiply and convert
are hardware here, so `__udivsi3` and `__floatsisf` do not appear; expect those
only on a configuration without DIV32 or without the FPU. Single-precision divide
has no instruction, so a divide per element is a call per element, and every
`double` in a hot loop is a call per operation because the FPU is single
precision.

The fix for a divide is almost always a hoisted reciprocal: compute `1.0f /
scale` once before the loop and multiply inside it. That turns a per-element
`call8 __divsf3` into a per-element `mul.s`.

## 7. Constants: movi, addmi and l32r

Three ways a constant reaches a register, cheapest first.

`movi at, -2048..2047` is one instruction for a small signed value.[^movi]
Verified against the installed assembler: 2047 and -2048 assemble, 2048 and -2049
do not `[measured]`.

`addmi at, as, -32768..32512` adds a multiple of 256, encoded as a sign-extended
8-bit field shifted left by eight.[^addmi] It is how GCC reaches a far offset
from a base pointer without spending a second register. Verified: 256 and 32512
assemble, 255 and 32768 do not. Plain `addi` is limited to -128..127; 127
assembles, 128 does not `[measured]`.

`l32r at, label` is a PC-relative load from a literal pool. Its offset is
one-extended, so the pool always lies 4 to 262141 bytes *before* the
instruction.[^l32r] Two costs: it is a memory reference, so latency 2 in GCC's
own model (section 8), and the pool sits with the code, so on a cold path the
first load can take an instruction-fetch miss. `L32R` is one of the few memory
instructions that can read instruction RAM or ROM, which is why the pool can live
there.[^l32r]

GCC does hoist it. In a loop multiplying by `0x9E3779B9`, the `l32r` lands above
the `loop` instruction and the body holds only the `mull` `[measured]`. An `l32r`
inside the body means the constant could not stay in a register. That is a
pressure symptom, not a constant problem.

## 8. Load-use pairs and what the scheduler does

The Xtensa backend has a pipeline description, and it is latency-only. The
machine description says the core is a simple 5-stage RISC pipeline that issues
one instruction per cycle, so no CPU units are defined.[^md] The reservations:
everything 1 cycle, except `load` and `fload` 2, `rsr` 2, `mul16` and `mul32` 2,
`fconv` 2, `fmadd` 4.[^md]

So a load consumed by the very next instruction is a one-cycle stall by the
compiler's own model, and a `mull` or an FP convert has the same shape. That is
the smell: an `l32i` or `l16ui` immediately followed by the instruction reading
its destination.

Both schedulers are on at `-O2` here. `-fschedule-insns` and `-fschedule-insns2`
both report `[enabled]` under `-Q --help=optimizers`, and they do move things
`[measured]`: in the good loop below the pointer increment sits between the byte
load and its consumer, and turning both off moves it back to the bottom of the
body. Turning off only one changes nothing, because the other still performs the
move.

Do not read this as "the scheduler will handle it". The model has no memory
hierarchy in it. A load whose data is in PSRAM costs far more than 2 and the
scheduler does not know that, so latency 2 is the floor.

## 9. A good loop, annotated

```c
void good_loop(uint16_t *dst, const uint8_t *src, const uint16_t *lut, int n)
{
    for (int i = 0; i < n; i++)
        dst[i] = lut[src[i]];
}
```

`-O2 -mlongcalls`, comments trimmed `[measured]`:

```
good_loop:
	entry	sp, 32		# smallest frame, nothing spilled
	blti	a5, 1, .L1	# n < 1, skip
	add.n	a5, a5, a5
	addi	a8, a5, -2
	srli	a8, a8, 1
	addi.n	a8, a8, 1	# trip count computed once, outside
	loop	a8, .L3_LEND	# zero-overhead loop
.L3:
	l8ui	a9, a3, 0	# load
	addi.n	a3, a3, 1	# scheduled into the load's shadow
	addx2	a9, a9, a4	# lut + 2*index in one instruction
	l16ui	a9, a9, 0
	s16i	a9, a2, 0
	addi.n	a2, a2, 2
	.L3_LEND:
.L1:
	retw.n
```

Six instructions in the body, no `sp` reference, no call, hardware loop present,
and the one load-use pair broken up by the scheduler. There is nothing here for
hand-written assembly to take back.

## 10. A bad loop, annotated

The same job with six separate tables and a divide per element, declared as
`bad_loop(uint16_t *dst, const uint8_t *src, const uint16_t *t0, ... *t5, float
scale, int n)`. The body is 32 instructions with 5 `sp` references and one
`call8` `[measured]`. The telling lines:

```
bad_loop:
	entry	sp, 48		# frame grew: something is spilled
	l32i	a7, sp, 60	# n is the 10th argument, so it is on the stack
	...
	s32i	a7, sp, 4	# spill the end pointer
.L3:
	l8ui	a8, a7, 0
	l32i	a9, sp, 48	# t4 reloaded from the stack every iteration
	l32i	a9, sp, 52	# t5 reloaded every iteration
	l32i.n	a11, sp, 56	# scale reloaded every iteration
	call8	__divsf3	# a call in the body
	l32i	a8, sp, 4	# end pointer reloaded every iteration
	bne	a8, a2, .L3	# hardware loop lost
```

Every fault at once, with one root cause. Ten parameters means four arrive on the
stack, because only six fit in registers (section 3). The `call8` forces the six
loop-carried pointers into `a2` to `a7`, leaving no room for the tail arguments
or the end pointer, so those are reloaded from the frame every iteration. The fix
removes all four faults together:

```c
const float rcp = 1.0f / scale;              /* divide hoisted out */
...
    uint32_t v = t[s] + t[256 + s] + ... ;   /* one packed table */
    dst[i] = (uint16_t)((float)v * rcp);
```

Result `[measured]`: `entry sp, 32`, hardware `loop` restored, 24 instructions in
the body, zero `sp` references, no call inside the loop. The six table offsets
are reached with `addmi a8, a9, 0x200` and so on, one instruction each and no
register.

## 11. Interventions, cheapest first

Work down this list and stop when the `.S` is clean.

1. **Drop an unroll.** Fewer live values is the cheapest fix and often the only
   one needed. The 12-versus-6 accumulator pair in section 4 is the shape: the
   deeper unroll cost 45% more instructions per element.
2. **Hoist the call out.** A reciprocal instead of a divide, a hoisted `sqrtf`, a
   table instead of `fmodf`. This alone can restore the hardware loop and free
   `a8` to `a15`.
3. **Make a stride a compile-time constant.** A template parameter or a `switch`
   over the two or three real strides turns a runtime multiply into a shift and
   frees the register holding the stride.
4. **Pack tables into one allocation.** Six base pointers become one base plus
   six `addmi` offsets: five registers freed for one instruction each.
5. **Split the loop into two passes.** Two loops with half the state each usually
   beat one spilling loop, even with the extra memory traffic. Measure this one;
   it is the first item whose answer is not visible in the `.S`.
6. **Move the body into a `noinline` leaf function.** If a call in an outer loop
   is what costs the high registers, put the hot part in its own function so that
   function is a leaf and gets `a2` to `a15`.
7. **Inline asm, or a hand-written `.S`.** Last, and only when the `.S` is clean
   and you still want an edge. A cycle claim from here needs a device or QEMU
   measurement, never an instruction count.

One caution on the last step. Transcribing GCC's own loop into inline asm usually
reproduces its speed and nothing more, because the compiler's schedule is already
reasonable for this latency model. Wins come from something structural: a closed
loop the compiler would not form, a walking pointer instead of an index, or an
instruction it never generates.

## Footnotes

[^idf]: ESP-IDF `tools/cmake/toolchain-esp32s3.cmake` lines 10 to 21 prepend
`-mlongcalls` to `CMAKE_C_FLAGS`, `CMAKE_CXX_FLAGS` and `CMAKE_ASM_FLAGS`. Read
from the local `framework-espidf` install, 2026-09-06.

[^entry]: Tensilica, *Xtensa Instruction Set Architecture (ISA) Reference
Manual*, Issue Date 4/2010. Section 4.7.1.4 "Call, Entry, and Return Mechanism",
page 186: `ENTRY s, imm12` is `AR[PS.CALLINC||s] <- AR[s] - (0^17||imm12||0^3)`
and sets `WindowBase <- WindowBase + PS.CALLINC`; `CALLn` sets `PS.CALLINC <- n`,
which is 2 for `CALL8`; `RETW` takes its window count from `AR[0]31..30`. The
ENTRY instruction page, page 340, gives the syntax `ENTRY as, 0..32760`.

[^abi]: Same manual, section 8.1.1 "Windowed Register Usage and Stack Layout",
page 587, Table 8-242: "Registers a0 and a1 are reserved for the return address
and stack pointer, respectively. They must always contain those values...
Incoming arguments are stored in registers a2 through a7." Table 4-114, page 188,
repeats the roles.

[^fixedregs]: GCC 14.2.0 `gcc/config/xtensa/xtensa.h`, read from the installed
header under `lib/gcc/xtensa-esp-elf/14.2.0/plugin/include/config/xtensa/`,
2026-09-06. `FIXED_REGISTERS` marks registers 0 and 1 fixed.
`CALL_REALLY_USED_REGISTERS` is `{1, 0, 4, 4, 4, 4, 4, 4, 1, 1, 1, 1, 2, 2, 2, 2,
...}`; the comment above it defines the encoding as value 1 for all ABIs, bit 1
for the windowed ABI, bit 2 for CALL0. `REG_ALLOC_ORDER` is `{8, 9, 10, 11, 12,
13, 14, 15, 7, 6, 5, 4, 3, 2, ...}`. `MAX_ARGS_IN_REGISTERS` is 6, `GP_ARG_FIRST`
is `a2`, `GP_ARG_LAST` is `a7`, and `BRANCH_COST` is 3.

[^loop]: ISA manual, LOOP instruction page, page 392: "LEND ... is loaded with
the address of the LOOP instruction plus four, plus the zero-extended 8-bit
offset encoded in the instruction (therefore, the loop code may be up to 256
bytes in length)"; "should not incur a mispredict or taken branch penalty";
"Because LCOUNT, LBEG, and LEND are single registers, zero-overhead loops may not
be nested"; "a return from a call instruction as the last instruction of the loop
would not trigger loop back".

[^doloop]: GCC 14 `gcc/config/xtensa/xtensa.cc`, `releases/gcc-14` branch of the
gcc-mirror repository, read 2026-09-06. `xtensa_invalid_within_doloop` returns
"Function call in the loop." for `CALL_P (insn)` and "Return from a call
instruction in the loop." for a `return` jump. `xtensa_can_use_doloop_p` returns
false when `loop_depth > 1 || !entered_at_top`, commented "only use doloop for
innermost loops which must be entered from the top".

[^movi]: ISA manual, MOVI page 421: `MOVI at, -2048..2047`.

[^addmi]: ISA manual, ADDMI page 253: `ADDMI at, as, -32768..32512`, the operand
being "multiples of 256 ranging from -32768 to 32512... decoded by sign-extending
imm8 and shifting the result left by eight bits."

[^l32r]: ISA manual, L32R page 382: "the offset can always specify 32-bit aligned
addresses from -262141 to -4 bytes from the address of the L32R instruction";
"L32R is one of only a few memory reference instructions that can access
instruction RAM/ROM."

[^md]: GCC 14 `gcc/config/xtensa/xtensa.md` lines 117 to 149, `releases/gcc-14`
branch of the gcc-mirror repository, read 2026-09-06. Comment: "The Xtensa
basically has simple 5-stage RISC pipeline. Most instructions complete in 1
cycle... The Xtensa can issue one instruction per cycle, so defining CPU units is
unnecessary." Reservations: `xtensa_any_insn` 1, `xtensa_memory` 2 for
`load,fload`, `xtensa_sreg` 2, `xtensa_mul16` 2, `xtensa_mul32` 2,
`xtensa_fmadd` 4, `xtensa_fconv` 2.
