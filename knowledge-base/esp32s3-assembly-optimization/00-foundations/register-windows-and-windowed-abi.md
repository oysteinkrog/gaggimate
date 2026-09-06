---
title: Xtensa register windows and the windowed calling convention
id: 00-foundations/register-windows-and-windowed-abi
schema_version: 1
doc_type: reference
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, register-windows, windowed-abi, calling-convention, gcc, call0]
confidence: medium
---

# Xtensa register windows and the windowed calling convention

The Xtensa LX7 core in the ESP32-S3 keeps 64 physical 32-bit address
registers, but a running program can only name 16 of them at a time,
`a0` through `a15`[^9]. The Windowed Register Option makes this work: a
function call slides a 16-register "window" a few slots along the
64-register file, so a fresh function gets registers that look empty
even though the physical storage is shared with its caller. This lets
small functions pass arguments in registers and keep locals in
registers across calls, without the compiler emitting explicit save
and restore code at every call site[^1]. This document explains the
mechanism, the ABI it produces, and what a hand-written kernel or an
inline assembly block can and cannot assume about it, and covers the
CALL0 ABI as the non-windowed alternative: ESP-IDF builds application
code windowed by default but writes some low-level assembly to the
CALL0 convention on purpose.

## The physical file and the window

The ESP32-S3 has 64 physical address registers (`XCHAL_NUM_AREGS = 64`;
the Xtensa ISA also allows a 32-register configuration, but this chip
has 64)[^9]. Only 16, `a0`-`a15`, are visible in the instruction stream
at once[^1]. Two special registers manage that visibility:

| Special register | Number | Width | Role |
|---|---|---|---|
| `WindowBase` | 72 | 4 bits (this chip) | Offset of the current 16-register window into the physical file, in units of 4. Width is `log2(NAREG/4)` = `log2(64/4)` here[^2]. |
| `WindowStart` | 73 | `NAREG/4` bits (16 here) | One validity bit per 4-register group; set if that group starts some call's window. Hardware uses it to detect overflow and underflow, and to size a spill[^2]. |

Both registers are undefined after reset; startup code must initialize
them before any windowed call executes[^2]. Numbers and widths: Xtensa
ISA Reference Manual, Table 4-112[^1]; ESP-IDF's headers carry the same
numbers[^11].

## CALL4, CALL8, CALL12, and CALLX: how the window rotates

A subroutine call in the windowed ABI is one of `CALL4`, `CALL8`, or
`CALL12` (PC-relative) or `CALLX4`, `CALLX8`, `CALLX12` (register
indirect). The suffix is the number of registers the call rotates the
window by. `CALL0`/`CALLX0` exist too, but they do not rotate anything
and belong to the CALL0 ABI, described later[^1].

The rotation itself happens in two steps, split between the caller and
the callee. The `CALLn`/`CALLXn` instruction sets `PS.CALLINC` to `n/4`
and writes the return address into `AR[n]` of the *caller's* window
(for `call8`, into what will become the callee's `a0`), but does not
move `WindowBase` yet[^4]. The callee's `ENTRY` instruction then
computes the new stack pointer from the incoming `a1` (the argument
register `as` in `entry as, imm`) minus a frame size, and adds
`PS.CALLINC` to `WindowBase`: that addition is what actually slides
the window[^4].

So after `entry`, the callee's `a0`-`a15` are 16 fresh-looking
registers, but the physical registers underneath are `n` slots further
into the 64-register file than the caller's were. For a `call8`, the
caller's `a8` becomes the callee's `a0`, the caller's `a9` becomes the
callee's `a1`, and so on: caller's `a_(n+k)` is callee's `a_k`[^8].

`RETW` (or the compressed `RETW.N`) reverses this: it decrements
`WindowBase` by the same `n`, so the caller's original window
reappears[^1].

### Which registers carry arguments and return values

Xtensa's windowed ABI, not just the raw ISA, fixes a convention on top
of this rotation:

| Register | Windowed ABI role |
|---|---|
| `a0`, `a1` | Return address and stack pointer, both reserved: they must hold those values whenever a debugger or the exception unwinder might inspect the frame[^6]. |
| `a2` - `a7` | Incoming arguments, as seen by the callee after `entry` has rotated the window[^6]. |
| `a7` | May optionally double as the callee's frame pointer, if the function needs one (for example, if it calls `alloca`)[^6]. |

The table above is the *callee's* view, after the window has rotated.
From the *caller's* side, before the call, the same physical registers
have different names depending on which `CALLn` is used: for a `call8`
the caller places the first six outgoing arguments in its own
`a10`-`a15`, and after the call those become the callee's `a2`-`a7`[^8].
Return values follow the same rule in reverse, up to 4 values coming
back in `a2`-`a5` of the caller's window once `retw` has rotated
back[^8][^6]. In general, for a `CALLn`/`CALLXn` call, the caller's
`a_n` becomes the callee's `a0`, `a_(n+1)` becomes `a1`, and so on[^1].
`call4` is the shallowest rotation and `call12` the deepest; the
compiler picks whichever hides the registers live at the call site[^1].

### Worked example: a call8

```
; caller                          ; bar, after entry (WindowBase += 2)
        mov   a10, x              bar's a0 = caller's a8 = return address
        mov   a11, y              bar's a1 = caller's a9 = new stack pointer
        call8 bar                 bar's a2 = caller's a10 = argument x
        mov   foo, a10  ; return  bar's a3 = caller's a11 = argument y
```

`bar` itself opens with `entry a1, <frame-size>` and closes with
`retw`, which decrements `WindowBase` back by 2. (Mirrors the example
in Espressif's Xtensa ISA overview[^8] and the `CALLn`/`ENTRY`/`RETW`
semantics in the architecture manual[^4].)

## Window overflow and underflow: the cost of going too deep

Rotating the window does not, by itself, save or restore anything.
`ENTRY` only moves `WindowBase`; it does not guarantee the 16 registers
now visible are actually free. Xtensa defers the cost: the processor
waits until an instruction actually *references* a register group that
still holds a live ancestor's data, and only then raises a **Window
Overflow exception** to spill that group[^3]. A shallow call tree that
only reaches `a4`-`a7` may never take an overflow; a deep chain of
`call8`s will.

The check runs on every instruction referencing `a4`-`a15` (grouped in
4s) and asks whether the matching `WindowStart` bit says an ancestor's
data is still cached there. If so, it sets `PS.EXCM` and jumps to one
of three fixed handlers by depth: `WindowOverflow4`, `WindowOverflow8`,
or `WindowOverflow12`[^3]. One instruction can raise more than one
overflow in sequence if it crosses two stale groups at once: the
manual's example spills 4 registers, retries, then spills 4 more[^3].
`RETW` triggers the mirror image, a **Window Underflow exception**,
when the window it unwinds into has a clear `WindowStart` bit, meaning
that frame was spilled and must be reloaded before the caller can use
it[^4][^5].

ESP-IDF ships hand-written overflow and underflow handlers for depths
4, 8, and 12, matching the architecture manual's reference sequences
instruction for instruction[^12][^5]. The 4-deep overflow handler:

```
_WindowOverflow4:
    s32e    a0, a5, -16     /* save a0 to call[j+1]'s stack frame */
    s32e    a1, a5, -12     /* save a1 to call[j+1]'s stack frame */
    s32e    a2, a5,  -8     /* save a2 to call[j+1]'s stack frame */
    s32e    a3, a5,  -4     /* save a3 to call[j+1]'s stack frame */
    rfwo                    /* rotates back to call[i] position */
```

(ESP-IDF 5.5.1, `components/xtensa/xtensa_vectors.S`, label
`_WindowOverflow4`[^12]; matching code in the architecture manual[^5].)
`S32E` and its load counterpart `L32E` exist only for this purpose:
negative offsets shorten the handler, and on an MMU-equipped chip they
run at the interrupted code's privilege level, not the handler's[^5].
The underflow handler is the same shape in reverse, using `L32E` and
`RFWU`[^5][^12].

An overflow or underflow is a full exception: entry, the handler body
above, and an exception return. The architecture manual gives no cycle
count and this document invents none; treat it as materially more
expensive than a register move, and measure it on the core itself if a
hot path's call depth is in question. `[uncertain]`

## PS.WOE and PS.CALLINC

Two fields of the Processor State register (`PS`) control this
mechanism:

| Field | Meaning |
|---|---|
| `PS.WOE` | Window Overflow Enable. When clear, no overflow or underflow exceptions are taken at all; this is the state early in exception and reset handling, before the windowed mechanism is safe to use[^1]. |
| `PS.CALLINC` | Set by the most recent `CALLn`/`CALLXn` to `n/4` (1, 2, or 3 for call4/8/12); `ENTRY` reads it to know how far to rotate `WindowBase`[^4]. `PS.OWB` (Old Window Base) is set alongside it by an overflow or underflow exception, to the window base at the moment the exception was taken, so the handler knows which physical registers to touch[^1]. |

ESP-IDF's headers define the bit positions for these fields
(`PS_WOE_MASK` at bit 18, `PS_CALLINC_MASK` at bits 17:16, `PS_OWB_MASK`
at bits 11:8)[^10], matching the manual's placement of all three inside
special register 230[^1]. Code that enables interrupts or sets up a
fresh execution context must set `PS.WOE` correctly or windowed calls
fault or behave undefined; ESP-IDF's vector and RTOS port code does
this at every interrupt-level entry point[^12].

## The stack frame: base save area and extra save area

The windowed ABI reserves two areas of a function's stack frame purely
for exception handlers to spill into, laid out so the overflow handler
above finds them without extra bookkeeping. The **register-spill area**
is 4 words (16 bytes) at the bottom of the frame, immediately below
the incoming stack pointer: this is where `a0`-`a3` land when overflow
spills them[^6]. The **register-spill overflow area** is 0 to 8 more
words, sized `N-4` where `N` is the widest `CALLn`/`CALLXn` the
function issues: this is where `a4`-`a11` (as applicable) land, at the
*top* of the frame the call originated from, not the callee's[^6].

The handler code above shows the exact offsets. For a `call8` overflow,
`a0`-`a3` go into the callee-to-be's base save area at offsets -16,
-12, -8, -4 from that frame's stack pointer, while `a4`-`a7` go into
the *caller's own* frame at offsets -32, -28, -24, -20[^5][^12]. For
`call12` the same pattern extends to `a0`-`a11`, spilling `a4`-`a11` at
offsets -48 through -20[^5].

The stack pointer must stay 16-byte aligned and should only be touched
by `ENTRY` and `MOVSP`; anything else risks leaving the register-spill
area unmoved relative to a stack pointer that has already changed[^6].

## The CALL0 ABI, and why it exists alongside windowed code

`-mabi=call0` is GCC's other Xtensa ABI. It has no register windows: a
call is a plain `CALL0`/`CALLX0`, arguments arrive in `a2`-`a7` exactly
as named (no rotation), and the compiler falls back to a conventional
caller-saved/callee-saved split: `a1` and `a12`-`a15` are callee-saved,
everything else is caller-saved by convention[^7][^14]. `a0` holds the
return address on entry but, unlike the windowed ABI, is not reserved
for it afterward and may be reused[^7].

| | Windowed ABI | CALL0 ABI |
|---|---|---|
| Argument registers | `a2`-`a7` (callee's view, after rotation) | `a2`-`a7` |
| Return address | `a0`, reserved for the whole call | `a0`, reusable once saved |
| Callee-saved | None by convention; the window mechanism preserves everything | `a1`, `a12`-`a15` |
| Optional frame pointer / extra cost per call | `a7`; a possible overflow or underflow exception | `a15`; none, just ordinary loads and stores if anything needs saving |

Windowed is the default for ESP-IDF application code, including
FreeRTOS tasks and ordinary C and C++ source, giving small leaf
functions and shallow call trees register-passed arguments without
explicit spill code[^1]. CALL0 is what ESP-IDF chooses instead for
low-level assembly that must run before the windowed mechanism is
safe, or that cannot tolerate a window exception mid-work, such as an
exception handler still sorting out why the exception happened.
ESP-IDF's generic context save/restore file says so directly in its
header comment: it implements "Low-level Call0 functions for handling
generic context save and restore of registers not specifically
addressed by the interrupt vectors and handlers," noting that "in
Call0 ABI, interrupt handlers are expected to preserve the callee-save
regs (A12-A15)"[^13].

`-mlongcalls` is a related but separate GCC option: when the assembler
cannot prove a call target is in range for a direct `CALL`, it emits an
`L32R` (load a literal) then a `CALLX` instead. It applies to both
ABIs and is not part of the windowed mechanism itself[^14].

## Register pressure: what the ABI guarantees, and what it does not

The windowed ABI reserves `a0` and `a1` outright. GCC's Xtensa backend
marks both `FIXED_REGISTERS` (unavailable to the register allocator at
all) in the windowed configuration, leaving `a2`-`a15`, 14 registers,
as the pool the compiler can assign values to inside one window[^15].
The register constraint used for general-purpose values in inline
assembly (constraint `a`) is documented the same way: "General-purpose
AR registers a0-a15, except a1 (sp)"[^16], with `a0` excluded from
practical use by the fixed-register rule above rather than by the
constraint syntax itself.

That 14-register figure is a hard ABI fact, but not the same question
as how many scalars a leaf function or an inline assembly block can
keep live in practice, and neither factor below has a fixed, citable
number. A nested call reserves registers for its own outgoing
arguments and the callee's rotation depth (a `call8` needs `a8`-`a15`
free of values the caller still needs after the call, since those are
what the callee spills on overflow), so a function that calls out has
less headroom than a true leaf. And GCC's allocation order for the
windowed ABI assigns `a8`-`a15` before `a7`-`a2`[^15], deliberately,
since a shallow `call4` never touches `a8`-`a15`; a kernel already
holding its live values in `a2`-`a7` (a `call8`'s incoming arguments)
competes with the compiler's own preferred range.

How many registers a real leaf function or an inline `asm` block can
keep live before GCC spills to memory depends on argument count,
whether it calls anything else, and what surrounding code already
holds live across the `asm` block. There is no single number to cite.
`[uncertain]` Treat "few scalars, more pointer walks" as the safe
default, and confirm pressure by reading the generated assembly rather
than assuming a budget.

## What this means for hand-written kernels

- Keep few scalars live and walk pointers instead: incrementing an
  address register and dereferencing it reuses the same 1 to 3
  registers across a whole loop body, instead of claiming a wide slice
  of the 14 available.
- A leaf function that never calls out does not, on its own, cause a
  window overflow. Overflow comes from nested-call depth exceeding
  what fits in the 64-register file, not from how many registers one
  function uses, so a hot path's whole call chain is what to count.
- Code that must never take a window exception, an interrupt handler
  or anything running with `PS.EXCM` already set, needs the CALL0 ABI
  or windowed-aware assembly proven never to overflow, not an
  assumption that "the window has room."

## Footnotes

[^1]: Cadence/Tensilica, *Xtensa Instruction Set Architecture (ISA) Reference Manual*, Section 4.7.1.1 "Windowed Register Option Architectural Additions" and Table 4-112, pp. 179-182. Copy consulted: https://0x04.net/~mwk/doc/xtensa.pdf
[^2]: Same manual, Section 4.7.1.2 "Managing Physical Registers", p. 183.
[^3]: Same manual, Section 4.7.1.3 "Window Overflow Check", pp. 184-185.
[^4]: Same manual, Section 4.7.1.4 "Call, Entry, and Return Mechanism", p. 186.
[^5]: Same manual, Section 4.7.1.6 "Window Overflow and Underflow to and from the Program Stack", pp. 192-193.
[^6]: Same manual, Chapter 8 "Using the Xtensa Architecture", Section 8.1.1 "Windowed Register Usage and Stack Layout", Table 8-242 and Figure 8-53, pp. 587-588.
[^7]: Same manual, Section 8.1.2 "CALL0 Register Usage and Stack Layout", Table 8-243, p. 588.
[^8]: Espressif Systems, *Overview of Xtensa ISA*, version 0021604, 2021-02-17, Section 1.3 "Windowed Register" and Section 1.4.1 "Windowed register calling convention", pp. 4-6. https://dl.espressif.com/github_assets/espressif/xtensa-isa-doc/releases/download/latest/Xtensa.pdf
[^9]: ESP-IDF 5.5.1, `components/xtensa/esp32s3/include/xtensa/config/core-isa.h`: `XCHAL_HAVE_WINDOWED` (1) and `XCHAL_NUM_AREGS` (64).
[^10]: ESP-IDF 5.5.1, `components/xtensa/include/xtensa/corebits.h`: `PS_WOE_SHIFT`/`PS_WOE_MASK`, `PS_CALLINC_SHIFT`/`PS_CALLINC_MASK`, `PS_OWB_SHIFT`/`PS_OWB_MASK`.
[^11]: ESP-IDF 5.5.1, `components/xtensa/include/xtensa/specreg.h`: `WINDOWBASE` (72), `WINDOWSTART` (73).
[^12]: ESP-IDF 5.5.1, `components/xtensa/xtensa_vectors.S`: `_WindowOverflow4`, `_WindowUnderflow4`, `_WindowOverflow8`, `_WindowUnderflow8`, `_WindowOverflow12`, `_WindowUnderflow12` handlers.
[^13]: ESP-IDF 5.5.1, `components/xtensa/xtensa_context.S`, file header comment.
[^14]: GNU Project, GCC documentation, "Xtensa Options" (`-mabi=windowed`, `-mabi=call0`, `-mlongcalls`). https://gcc.gnu.org/onlinedocs/gcc/Xtensa-Options.html
[^15]: GCC 14.2.0 source, `gcc/config/xtensa/xtensa.h`: `FIXED_REGISTERS`, `CALL_REALLY_USED_REGISTERS`, `REG_ALLOC_ORDER`. https://github.com/gcc-mirror/gcc/blob/releases/gcc-14.2.0/gcc/config/xtensa/xtensa.h (this is the GCC release the ESP32-S3 `xtensa-esp-elf` toolchain, crosstool-NG `esp-14.2.0_20241119`, is built from)
[^16]: GCC 14.2.0 source, `gcc/config/xtensa/constraints.md`: register constraint `a`. https://github.com/gcc-mirror/gcc/blob/releases/gcc-14.2.0/gcc/config/xtensa/constraints.md
