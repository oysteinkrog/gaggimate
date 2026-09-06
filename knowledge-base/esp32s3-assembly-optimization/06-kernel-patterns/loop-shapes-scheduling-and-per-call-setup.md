---
title: Loop shapes, scheduling, and per-call setup on the LX7
id: 06-kernel-patterns/loop-shapes-scheduling-and-per-call-setup
schema_version: 1
doc_type: how-to
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, lx7, loops, scheduling, software-pipelining, memcpy, register-pressure, gcc]
confidence: medium
---

# Loop shapes, scheduling, and per-call setup on the LX7

The LX7 issues at most one instruction per cycle, in order, with no branch
prediction beyond the Loop Option's hardware loop and no out-of-order
window to hide a stall behind. `00-foundations/lx7-core-pipeline-and-cost-model.md`
gives the full cost model; the fact this page leans on is that a loaded
value is not ready for the instruction right after the load, so a
dependent instruction issued back to back stalls one cycle, and one
independent instruction placed in between removes the stall
entirely[^pipe]. Everything below shapes a loop, and the calls around it,
so that gap gets filled and the loop's own overhead is not paid at all.

## 1. Give the compiler a trip count it can hand to LOOPNEZ

`01-scalar-isa/zero-overhead-loops.md` covers the instruction-level
mechanics of `LOOP`/`LOOPNEZ`/`LOOPGTZ`: one set of loop registers, no
nesting, a 256-byte body cap, and a trip count read once at loop entry.
The consequence for how you write the loop: state the iteration count as
one value, known before the first iteration runs, and never let the body
recompute or reread it. A `for` loop over a pointer difference computed
once above the loop, or over a `size_t` parameter, is the shape a compiler
can turn into `LOOPNEZ`; a loop whose trip count depends on a condition
evaluated inside the body cannot use the hardware loop at all, because
`LOOPNEZ` samples the count once and nothing changes it mid-loop[^loopnez].

## 2. Keep the loop body call-free

The manual states this as a consequence of there being only one
`LBEG`/`LEND`/`LCOUNT`: zero-overhead loops cannot nest, so "it is usually
inappropriate to include a procedure call inside a loop (the callee might
itself use a zero-overhead loop)"[^loopnez]. A call inside the body also
rotates the register window on every `CALLn`, and pays for that rotation,
and for any window overflow it triggers, on every iteration rather than
once[^windows]. A loop that must call out either inlines the callee, or is
restructured so the call happens once, outside the loop, over a batch the
loop already produced.

## 3. Hoist what does not change across the loop

Two hoisting boundaries matter for a loop nested over rows and columns (or
frames and rows): anything constant for the whole frame comes out of the
outer loop once, and anything constant for one row, but not one element,
comes out of the inner loop and is computed once per row. GCC's own
loop-invariant motion pass does this for plain C, from `-O1` up[^licm], but
it does not reach inside an inline `asm` block: "GCC does not parse the
assembler instructions themselves and does not know what they mean"[^extasm].
Any row- or frame-constant value used inside a hand-written kernel's `asm`
block must therefore be hoisted by hand and passed in as an input operand,
never left for the compiler to notice it does not change.

## 4. Incremental stepping instead of recomputing a closed form

If element `i` needs a value that is an arithmetic function of `i` (an
address, a table index, a phase), carrying it in a register and adding a
fixed delta each iteration removes whatever multiply or shift the closed
form needed, at the cost of one register that must survive the whole loop
and stay correct under whatever rounding the closed form had. It is a
trade against the register budget in §6, not a free win.

Address arithmetic already has a hardware shortcut that makes this
unnecessary for plain pointer walks. Addressing is register-plus-immediate
only: "the core ISA does not implement auto-incrementing stores or
indexed loads"[^addrmode]. A pointer walk is therefore a load or store at
offset zero, then an explicit `ADDI`/`ADDI.N` to advance the pointer.
Computing `base + index*stride` fresh each time instead uses
`ADDX2`/`ADDX4`/`ADDX8` (add two registers, one shifted left by 1, 2, or 3
bits), core architecture and always present[^addx2]. That costs one more
instruction than the walked pointer for the same access, so prefer the
pointer walk unless the loop needs random rather than sequential access.

## 5. Software pipelining by hand: two-way interleave

Restructuring a loop so each iteration's load issues while a previous
iteration's load is being consumed, so independent work is always in
flight, is software pipelining, named for exactly this kind of inner-loop
scheduling[^lam]. On the LX7 the useful case is narrow: interleave two
independent elements A and B, so the sequence is load A, load B, consume
A, consume B, store A, store B, rather than finishing A before starting B.
Each element's load-use gap is filled by the other element's load.

A deeper interleave fills more gaps in principle but needs more live
registers per stage in flight. A leaf loop that makes no call has 14
usable address registers (`a2`-`a15`)[^regs]. A two-way interleave
typically fits this with headroom; a four-way interleave on the same body
is frequently the difference between fitting and spilling to the stack,
and a spilled value reintroduces the load-use stall the interleave was
trying to remove, on the reload. Measure both shapes; deeper is not
automatically faster.

## 6. Unrolling: what it buys and what it costs

The hardware loop already removes loop-overhead cost, so unrolling here is
not chasing that. What it can still buy is scheduling room: more
independent instructions in one expanded body give the scheduler more
candidates to place between a load and its consumer, the same benefit §5
gets by hand. GCC's own description of `-funroll-loops` gives the benefit
and the cost together: it unrolls "loops whose number of iterations can be
determined at compile time or upon entry to the loop" but "makes code
larger, and may or may not make it run faster"[^unroll], and it is not part
of `-O2`/`-O3` by itself, only of profile-guided builds[^unroll]. The
stronger `-funroll-all-loops` "usually makes programs run more
slowly"[^unroll2].

Two limits cap how far unrolling helps here. The Loop Option's body cap is
256 bytes[^loopnez]; unrolling inside a `LOOPNEZ` body eats into that
budget, and a body that no longer fits loses the hardware loop entirely.
And unrolling multiplies live values in the expanded body against the same
14-register budget as §5; past some factor the extra copies stop
scheduling into filled gaps and start spilling.

## 7. Per-call setup: amortize it, or cache it across calls

Some kernel APIs hand the pixel loop small units of work, a handful of
rows per call rather than a whole frame. Whatever per-call setup the
kernel does then runs at the same frequency as the outer level of the
pixel loop, not once per frame, and a setup step that is invisible against
a body running many elements is not invisible against one or two rows per
call. Two ways out: make the per-call setup as cheap as the pixel loop's
own inner step (the hoisting discipline of §3, applied to per-call state),
or cache the derived state across calls behind a key, so a repeated call
with the same key skips rederiving it. Caching only pays off if the key
comparison is cheaper than the recomputation it replaces.

## 8. Stores: sequential, word-sized, not read-modify-write

The core's write buffer can stall a later store: "the processor avoids
overflowing its write buffer by interlocking in the R stage on stores when
the write buffer is full or might become full"[^storeinterlock]. A run of
sequential, aligned word stores drains predictably; a mixed run of byte
and halfword stores to the same region does not, and on downstream memory
that cannot accept a narrower-than-word write atomically, a sub-word store
can force a read-modify-write in the memory itself. `[experience]` Prefer
accumulating a full word and issuing one `S32I` over several `S16I`/`S8I`
to the same word when the data packs that way; this is a general rule
about avoiding sub-word writes, not a published cycle cost on this core.

## 9. memcpy and memset: what actually runs, and when a hand loop wins

On the ESP32-S3, `memcpy` is not a byte loop and does not come from the
boot ROM. The ROM's linker script exports exactly one memcpy-shaped
symbol, `xthal_memcpy`[^romld], the Tensilica HAL bulk-copy helper, not
the C library's `memcpy`. The C library's own optimized override for
misaligned access, gated by `CONFIG_LIBC_OPTIMIZED_MISALIGNED_ACCESS`,
depends on a ROM capability, `ESP_ROM_HAS_SUBOPTIMAL_NEWLIB_ON_MISALIGNED_MEMORY`,
defined only for this chip family's RISC-V members (C2, C3, C5, C6, C61,
H2, H21, P4)[^idfcaps], never for the ESP32-S3. So on the ESP32-S3 that
override never compiles in, and `memcpy` resolves to the `xtensa-esp-elf`
GCC 14 toolchain's own bundled C library.

That implementation is a hand-written Xtensa assembly kernel, not a
portable C loop: newlib's `libc/machine/xtensa/memcpy.S`. Its own header
gives the algorithm: align the destination with conditional 1- and 2-byte
copies, then "if the source is aligned, copy 16 bytes with a loop... Else
(if source is unaligned), do the same, but use SRC to align the source
data"[^newlibmemcpy]. Disassembling the object actually linked for this
target confirms the same shape: a `LOOPNEZ`-driven aligned path moving 16
bytes per iteration with `l32i.n`/`s32i.n`, and a misaligned path that uses
`SSA8L` to set a shift amount and `SRC` to combine two loaded words before
each store, so even a misaligned source still produces word-sized
stores[^toolchainmemcpy]. It never touches the PIE vector unit (see
`02-pie-vector/`).

A hand-written copy loop wins only where one of the library's fixed costs
does not apply: a compile-time-constant length short enough that the
library's own alignment and tail-length dispatch cost more than the copy;
a length and alignment known to be a multiple of 16 bytes, making the tail
handling dead weight; or a throughput need above what word-sized transfers
give, which needs the PIE unit this library never reaches for. Absent one
of those, the shipped `memcpy` already does what this page recommends: a
hardware loop, hoisted alignment handling, and word-sized stores.

## 10. Branchless selection instead of compare-and-branch

`MOVEQZ`/`MOVNEZ`/`MOVLTZ`/`MOVGEZ` are conditional register moves in the
core architecture, present on every configuration: `MOVEQZ ar, as, at` sets
`ar` to `as` if `at` is zero and otherwise leaves it unchanged, with the
other three testing not-equal, less-than, and greater-or-equal against
zero[^moveqz]. `MAXU`/`MINU` (and signed `MAX`/`MIN`) are Miscellaneous
Operations Option instructions, configurable rather than always
present[^maxu]. Both families replace a compare-and-branch with one
data-flow instruction that never kills fetched fall-through instructions
the way a taken branch does[^pipe]. A clamp, a saturate, or a per-element
pick is almost always cheaper as one conditional move or one `MAXU`/`MINU`
than as a branch, especially since a branch to a `LOOPNEZ` body's own end
address does not loop back and instead exits the hardware loop
early[^loopnez]. That is exactly the shape a compare-and-branch inside the
body tends to produce.

## 11. Splitting a fused loop when the body spills

A loop doing two largely independent things per element is a candidate for
splitting into two simpler loops, each needing fewer live values, at the
cost of walking the data twice. GCC has an automatic version of this
trade, loop distribution, "enabled by default at -O3"[^distribute], because
it "can improve cache performance on big loop bodies." The same reasoning
applies by hand: if register-pressure analysis (§5, §6) shows a fused body
spilling where either half alone would not, two passes within budget can
beat one pass that spills every iteration. It is a real trade against
memory traffic, so it favors a body that spills badly over data small
enough to stay resident across both passes.

## 12. Confirming the shape actually landed

None of this is real until it is checked in the compiler's own output.
`04-toolchain-and-codegen/` covers producing the `.S` for one file and
reading it for spills, a lost hardware loop, and libcalls in place of
inline instructions. The checklist specific to this page: confirm a
`LOOPNEZ`/`LOOPGTZ`/`LOOP` actually appears rather than a
compare-and-branch (§1, §2); confirm hoisted values are computed once,
above the loop label (§3); confirm an interleaved or unrolled body's live
range does not spill (§5, §6); and confirm a call believed removed by
inlining does not still appear as a `CALLn`/`CALLXn` (§2). Reading the
assembly, not predicting from the source, is what turns a claim here into
a fact about the build in hand.

## Footnotes

[^pipe]: Tensilica, Inc., *Xtensa Instruction Set Architecture (ISA)
    Reference Manual*, issue 4/2010, release RC-2010.1, document
    PD-09-0801-10-01. Section 8.4.2 "Xtensa Processor Family" and
    Figure 8-56, pp. 608-609: a load result is available two cycles after
    issue, so put one independent instruction between a load and its
    consumer. Mirror: <https://0x04.net/~mwk/doc/xtensa.pdf>, read
    2026-09-06. Restated for this chip in
    `00-foundations/lx7-core-pipeline-and-cost-model.md` §3.
[^loopnez]: Same manual, "LOOPNEZ" instruction description, pp. 396-397
    (no mid-loop restart; no nesting; calls inside a loop are usually
    inappropriate), and Section 4.3.2/4.3.2.2, pp. 54-55 (256-byte body
    cap: `LEND` is the `LOOP` address plus an unsigned 8-bit immediate plus
    4). Full instruction-level treatment:
    `01-scalar-isa/zero-overhead-loops.md`.
[^windows]: This collection, `00-foundations/register-windows-and-windowed-abi.md`:
    every `CALLn`/`CALLXn` rotates `WindowBase`; a window overflow or
    underflow costs an exception, a handler that spills or reloads 4, 8, or
    12 registers, and an exception return.
[^licm]: Free Software Foundation, *GCC*, 14.2.0, "Optimize Options",
    `-fmove-loop-invariants`, enabled at `-O1` and higher except `-Og`.
    gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Optimize-Options.html, read
    2026-09-06.
[^extasm]: Free Software Foundation, *GCC*, 14.2.0, "Extended Asm",
    introductory text. gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Extended-Asm.html,
    read 2026-09-06.
[^addrmode]: Tensilica ISA manual (see [^pipe]), Section 3.5.2 "Addressing
    Modes", p. 28: register-plus-immediate only; no auto-increment or
    indexed load in the core ISA (coprocessors such as the FPU add
    indexed modes).
[^addx2]: Same manual, "ADDX2" instruction description, p. 254: Required
    Configuration Option "Core Architecture"; used for address calculation
    and multiplying by small constants. `ADDX4`/`ADDX8` are the same
    instruction shifted by 2 and 3 bits.
[^lam]: Monica S. Lam, "Software Pipelining: An Effective Scheduling
    Technique for VLIW Machines", *Proceedings of the ACM SIGPLAN 1988
    Conference on Programming Language Design and Implementation*,
    pp. 318-328. DOI: 10.1145/53990.54022.
[^regs]: This collection, `00-foundations/register-windows-and-windowed-abi.md`:
    `a0`/`a1` reserved for return address and stack pointer under the
    windowed ABI, leaving `a2`-`a15` (14 registers) for a leaf function; a
    function issuing a `call8` has only `a2`-`a7` (6) live across the call.
[^unroll]: Free Software Foundation, *GCC*, 14.2.0, "Optimize Options",
    `-funroll-loops`, enabled by `-fprofile-use`/`-fauto-profile`, not by
    plain `-O2`/`-O3`. gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Optimize-Options.html,
    read 2026-09-06.
[^unroll2]: Same manual, same section, `-funroll-all-loops`.
[^storeinterlock]: Tensilica ISA manual (see [^pipe]), Section 8.4.2,
    p. 609.
[^romld]: ESP-IDF v5.5.1 (PlatformIO package `framework-espidf` 3.50501),
    `components/esp_rom/esp32s3/ld/esp32s3.rom.ld`, line 496:
    `xthal_memcpy = 0x40001ba8;`. No `memcpy`/`memset`/`memmove` symbol
    appears in this chip's ROM link fragments.
[^idfcaps]: ESP-IDF v5.5.1: `components/newlib/CMakeLists.txt` gates
    `src/port/riscv/memcpy.c` and siblings behind
    `CONFIG_LIBC_OPTIMIZED_MISALIGNED_ACCESS`; `components/newlib/Kconfig`
    makes that option `depends on ESP_ROM_HAS_SUBOPTIMAL_NEWLIB_ON_MISALIGNED_MEMORY`;
    that macro is defined in `esp_rom_caps.h` for esp32c2, c3, c5, c6, c61,
    h2, h21, and p4 only. No match under `esp32s3/esp_rom_caps.h`.
[^newlibmemcpy]: Tensilica Inc., `newlib/libc/machine/xtensa/memcpy.S`,
    copyright header 2002-2008. Mirror: `jcmvbkbc/newlib-xtensa`
    repository, `xtensa` branch, same path, read 2026-09-06.
[^toolchainmemcpy]: Toolchain observation, `xtensa-esp-elf-gcc` 14.2.0
    (crosstool-NG `esp-14.2.0_20241119`), target `xtensa-esp32s3-elf`,
    2026-09-06: `memcpy` extracted from `xtensa-esp-elf/lib/esp32s3/libc.a`
    (member `libc_a-memcpy.o`), disassembled with `xtensa-esp-elf-objdump
    -d`; matches [^newlibmemcpy] instruction for instruction.
[^moveqz]: Tensilica ISA manual (see [^pipe]), "MOVEQZ" instruction
    description, p. 415: Required Configuration Option "Core Architecture".
    `MOVNEZ`, `MOVLTZ`, `MOVGEZ` are the same option.
[^maxu]: Same manual, "MAXU" instruction description, p. 407: Required
    Configuration Option "Miscellaneous Operations Option" (Section 4.3.8,
    p. 62), not core architecture. `MINU` and signed `MAX`/`MIN` are the
    same family and option.
[^distribute]: Free Software Foundation, *GCC*, 14.2.0, "Optimize
    Options", `-ftree-loop-distribution`.
    gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Optimize-Options.html, read
    2026-09-06.
