---
title: PIE and scalar kernel idioms in this firmware
id: 07-our-work/pie-and-scalar-idioms-in-this-firmware
schema_version: 1
doc_type: reference
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, pie, inline-asm, kernels, our-work, rgb565]
confidence: high
---

# PIE and scalar kernel idioms in this firmware

Every hand-written kernel in this repository is built from the same small set
of shapes. This leaf is the catalogue. Each entry names the file and line at
commit `1cef2de8`, shows the shortest useful piece of it, and says what the
shape is for and what breaks if you change it.

Instruction semantics are not re-derived here. For what the load and store
forms do to an address, see
[`../02-pie-vector/pie-load-store-and-alignment.md`](../02-pie-vector/pie-load-store-and-alignment.md).
For the multiply, saturate and shuffle instructions, see
[`../02-pie-vector/pie-arithmetic-multiply-saturate-and-shuffle.md`](../02-pie-vector/pie-arithmetic-multiply-saturate-and-shuffle.md).
For the `q` register file, `SAR`, and coprocessor context, see
[`../02-pie-vector/pie-register-file-sar-and-context.md`](../02-pie-vector/pie-register-file-sar-and-context.md).
For the constraint letters, clobbers and `asm volatile` rules the wrappers
below rely on, see
[`../04-toolchain-and-codegen/gcc-extended-inline-asm-on-xtensa.md`](../04-toolchain-and-codegen/gcc-extended-inline-asm-on-xtensa.md).

Two sibling leaves in this bucket carry what this one only points at. The
checks a kernel must pass before it ships are in
[`verification-ladder-host-asm-qemu-device.md`](./verification-ladder-host-asm-qemu-device.md).
The speed results that set the flag defaults quoted below are in
[`animation-kernel-pass-2026-09-what-the-device-decided.md`](./animation-kernel-pass-2026-09-what-the-device-decided.md).

## Index

| Idiom | Where |
|---|---|
| The wrapper shape (`noinline`, plain pointers, build guard) | `src/display/ui/default/bganim/AnimOrbits.cpp:350` |
| Canonical PIE kernel: constant table, hoisted `ssai`, read/write pointer split | `src/display/ui/default/SleepAnimation.cpp:261` and `:270` |
| Byte widen and narrow with zip and unzip | `src/display/ui/default/bganim/AnimNebula.cpp:326` |
| Unaligned streaming source with `ee.ld.128.usar.ip` and `ee.src.q` | `src/display/ui/default/bganim/AnimNebula.cpp:384` |
| Doubled buffer for a rotated read of arbitrary length | `src/display/ui/default/bganim/AnimNebula.cpp:636` |
| Per-channel RGB565 arithmetic in lanes, register budgeting | `tools/animbench/kernels-blend/blend_group8.S:77` |
| The same body as pasteable C++, kept in lockstep | `tools/animbench/kernels-blend/blend_pie_kernel.cpp:109` |
| Broadcasting a runtime constant through an aligned stack buffer | `src/display/ui/default/bganim/AnimOrbits.cpp:375` |
| Scalar gather loop, four pixels interleaved | `src/display/ui/default/bganim/AnimNebula.cpp:847` |
| Scalar quad finalize with `loopnez`, `min`/`max`, `extui` | `src/display/ui/default/bganim/AnimLava.cpp:934` |
| The `bandRef` twin in the registry | `src/display/ui/default/bganim/BgAnim.h:46` |
| The row-pair shape | `src/display/ui/default/bganim/AnimLava.cpp:860` |
| Flag-gated kernel with a portable twin | `src/display/ui/default/bganim/AnimSilk2.cpp:134`, `:610`, `:667` |

## The wrapper shape

Assembly lives inside a normal C++ function, never at file scope in a `.S`
that ships. The function is `__attribute__((noinline))`, takes plain pointers
and integers, and sits under one build guard.[^brief]

```cpp
#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)
__attribute__((noinline)) static void fillBgPie(uint16_t *dst, int nOct, uint16_t bg) {
```

Four properties, each load-bearing:

- **`__XTENSA__` guards the target, `GM_BGANIM_NO_ASM` guards the choice.**
  The first excludes the host bench and the simulator, which cannot assemble
  `EE.*`. The second lets a device build turn every kernel off at once, which
  is how the portable path gets exercised on real hardware.
- **`noinline` protects the register budget.** An `asm volatile` block inlined
  into a larger loop competes with that loop's live values for the roughly
  thirteen address registers available before GCC starts spilling around the
  block. The overlay composite loop in `SleepAnimation.cpp` is standalone for
  the same reason, and the comment there records the disassembly reloading the
  span end, the alpha pointer and the scrim row from the stack on every pass
  when it was inlined.
- **Plain pointers and integers mean the same function drops into a test.**
  `tools/qemubench/tests/anim_<id>/` includes the kernel function unchanged and
  runs it against a C reference on a bare-metal ELF. A kernel that took a
  struct of firmware state would need a second copy for the test, and a second
  copy is a second thing to keep correct.
- **The compiler generates the ABI prologue.** `blend_group8.S` exists only as
  an assembler-syntax probe and says so in its own header: it uses `ret.n`
  rather than a windowed-ABI `entry`/`retw` pair because it is never linked or
  executed.[^blendS] The shipping form is the `asm volatile` block.

Constraints follow one pattern. Every pointer the loop walks is an in-out
operand (`"+r"`), because the PIE load and store forms post-increment the
address register themselves. The trip counter is `"+r"` because the loop
decrements it. Anything read but not advanced is `"r"`. Scratch registers are
early-clobber outputs (`"=&r"`), never hardcoded register names, so GCC
allocates them. `"memory"` is always clobbered.

There is no clobber list entry for `q0` to `q7`. There is no constraint syntax
for them, and the compiler never allocates them, so an asm block may use all
eight freely. Every kernel states that fact in a comment rather than relying on
the reader knowing it.

## The canonical PIE kernel

`scale565Oct` multiplies eight RGB565 pixels by eight per-pixel factors. It is
the kernel every later one was written against.[^brief]

```cpp
alignas(16) static const DRAM_ATTR uint16_t kPieMasks[16] = {
    0xF81F, 0xF81F, ... // red and blue
    0x07E0, 0x07E0, ... // green
};
```

```cpp
    asm volatile("ee.vld.128.ip q3, %[m], 16\n" // q3 = 0xF81F x8
                 "ee.vld.128.ip q4, %[m], 16\n" // q4 = 0x07E0 x8
                 "ssai 5\n"
                 "1:\n"
                 "ee.vld.128.ip q0, %[rd], 16\n" // eight pixels
                 "ee.vld.128.ip q5, %[iv], 16\n" // eight factors, one per pixel
                 ...
                 : [rd] "+r"(rd), [wr] "+r"(wr), [iv] "+r"(inv), [m] "+r"(masks), [n] "+r"(n)
                 :
                 : "memory");
```

Five things to copy:

**One constant table, loaded through one walking pointer.** `kPieMasks` is a
single `alignas(16)` array holding both masks back to back, and the two setup
loads consume it with the same auto-incrementing register. The order of the
table is the order of the loads. This scales: `blend_pie_kernel.cpp`'s constant
table is six vectors long and its comment says the order must match the load
sequence in the asm block, because nothing else enforces it.

**`DRAM_ATTR` on a constant read by the kernel.** Without it a `const` table
can land in flash and every read goes over the MSPI bus that PSRAM shares.

**`ssai` above the loop label, not inside it.** `SAR` is part of the ordinary
task context frame, so a shift amount set once survives an interrupt and a task
switch. Setting it per iteration is pure waste. Where a kernel genuinely needs
two shift amounts it toggles them inside the loop and says why: `nebulaFieldPie`
toggles between 0 and 6 twice per sixteen pixels because batching by shift
amount would need both passes' partial products live at once, which does not
fit in the three non-resident registers left.

**A separate read pointer and write pointer over the same buffer.** `rd` and
`wr` both start at `dst`. Both load and store post-increment their own address
register, so one pointer cannot serve both when the read must happen before the
group's other pointers advance and the write after. `blend_group8.S` names this
explicitly as "exactly scale565Oct's rd/wr split, same reason".

**The alignment warning, stated at the function, not left implicit.** The
comment above `scale565Oct` reads: `EE.VLD.128` and `EE.VST.128` force the low
four address bits to zero rather than trapping, so a misaligned pointer
corrupts neighbouring pixels silently instead of failing. Nine kernels in this
repository cite that comment rather than restating the rule.

Two callers act on it rather than just documenting it. `AnimNebula.cpp:1310`
checks all six of its hot-slab pointers for 16-byte alignment once per `band()`
call and falls back to the portable path if any fails, because the slab's bump
allocator only guarantees alignment for pointers actually carved from the slab
and a PSRAM fallback does not promise it. The same function falls back when the
width is not a multiple of sixteen.

## Byte widening and narrowing

The vector multiply works on 16-bit lanes. Byte data has to be widened first.
`lerpRowPie` interpolates sixteen bytes per iteration.[^trm-lane]

```cpp
                 "ee.zero.q q2\n"
                 "ee.vzip.8 q0, q2\n" // q0 = a lanes 0-7, q2 = a lanes 8-15
                 ...
                 "ee.vmul.s16 q4, q4, q7\n" // ((b-a)*f) >> 8, arithmetic
                 ...
                 "ee.vunzip.8 q0, q2\n" // sixteen lanes back to sixteen bytes
```

`ee.vzip.8` interleaves two registers' bytes and writes both of them. Zipped
against a zeroed register that is a zero-extending widen, and one instruction
produces both halves. That is why the zero is re-made per zip rather than kept
resident: the zip overwrites it. `ee.vunzip.8` is the inverse and takes every
other byte, which on little-endian 16-bit lanes is the low byte, the same
truncation a byte store would have done.

Two correctness notes the kernel's comment records:

- **The signed multiply is required.** `ee.vmul.s16` shifts the full 32-bit
  product arithmetically before keeping the low sixteen bits. The difference
  `b - a` is signed, so a logical shift gives wrong pixels for half the inputs.
- **Saturation never fires here, and that is a fact, not a hope.** A lerp
  between two bytes cannot leave 0 to 255 for any factor in 0 to 255. The
  `blend_group8.S` header does the same arithmetic in the other direction: it
  bounds every intermediate at 16,128 against a 32,767 saturation threshold and
  concludes saturation "is a non-issue, not a risk this design leans on".

## Reading a source at an unaligned offset

`lerpShiftRowPie` needs `a[i+1]` next to `a[i]`, and a 128-bit load cannot start
at an odd address. The hardware answer is `SAR_BYTE`.

```cpp
    const uint8_t *pa = a + 1; // sets SAR_BYTE = 1 in the setup load
    asm volatile("ee.vld.128.ip q7, %[f], 0\n"
                 "ee.ld.128.usar.ip q6, %[pa], 16\n" // block 0, SAR_BYTE = 1
                 "ssai 8\n"
                 "1:\n"
                 "ee.vld.128.ip q1, %[pa], 16\n" // next aligned block
                 "ee.src.q q2, q6, q1\n"         // a[i+1 .. i+16]
                 "ee.orq q3, q1, q1\n"           // stash it for the next pass
```

`ee.ld.128.usar.ip` loads the aligned block containing the address and captures
that address's low four bits into `SAR_BYTE`. `ee.src.q` then shifts the 32-byte
concatenation of two consecutive aligned blocks right by that many bytes, which
is the unaligned window. Pointing the setup load at `a + 1` fixes `SAR_BYTE` at
1 for the whole loop.

Three details worth copying:

- **`ee.orq qd, qs, qs` is the register move.** There is no vector move
  instruction, so the loop stashes the next block by OR-ing it with itself.
- **The reach past the end is real and must be paid for.** Iteration `k` reads
  aligned block `k+1` and writes block `k`, so the source needs one extra block
  of slack. `noiseTex256` carries sixteen bytes of padding for exactly this.
- **The rotation length decides how much slack.** `nebulaFieldPie` reads the
  same source at an arbitrary rotation for up to 256 further bytes, so sixteen
  bytes of slack is not enough and the buffer is doubled: a full 512-byte array
  whose second half is a copy of the first, refreshed once per row. Its comment
  spells out why the smaller trick does not generalise.

`lerpShiftRowPie` also documents its one known wrong output. The kernel's wrap
at index 255 wants `a[0]` and gets `a[256]`, and the caller fixes that single
entry rather than the kernel special-casing it.

## Per-channel RGB565 arithmetic and the register budget

The blend kernel in `tools/animbench/kernels-blend/` is the fullest worked
example. It blends eight RGB565 pixels against eight colours with per-pixel
alpha, one colour channel at a time.[^blendS]

```asm
    // ---- R channel (bit position 11, mask 0xF800) ----
    ee.vld.128.ip q4, a7, 16       // q4 = maskR
    ee.andq  q5, q0, q4            // q5 = fgR bitpos
    ee.andq  q4, q1, q4            // q4 = bgR bitpos
    ssai 11
    ee.vmul.u16 q5, q5, q7         // q5 = fgR raw (0..31)
    ee.vmul.u16 q4, q4, q7         // q4 = bgR raw (0..31)
    ssai 0
    ee.vmul.u16 q5, q5, q2         // q5 = fgR_raw * a      (<=7905)
    ee.vmul.u16 q4, q4, q3         // q4 = bgR_raw * inv    (<=7936)
```

**Multiply is the only shift.** `EE.VMUL.U16` keeps the low sixteen bits of the
product after shifting right by `SAR`. There is no vector left shift at all. So
one primitive serves both directions. Extracting a channel's raw magnitude is a
multiply by an all-ones vector at `SAR` equal to the channel's bit position.
Putting the result back is a multiply by 2048 or 32 at `SAR` 0. The all-ones
vector is therefore the most reused constant in the kernel, and it also does
the final divide by 256 at `SAR` 8.

**The register budget decides the schedule, not the algebra.** Five of the eight
`q` registers are pinned for the whole group: foreground, background, alpha,
inverse alpha, and the ones vector. That leaves three. A schedule batched by
shift amount would need two channels' extracted values live across a `SAR`
change and does not fit. So channels run serially, with one of the three
scratch registers doubling as the running OR accumulator, and no channel's
result ever spills to memory. The cost is nine `ssai` mode changes instead of
five. The kernel's own comment states the tradeoff and names the risk it buys:
get the `ssai` sequence wrong and a later multiply silently uses the wrong shift
amount.

**One `ssai` is skipped on purpose, with a comment saying so.** Before the blue
channel's two multiplies the assembly notes that `SAR` is already 0 from the
green channel's repositioning multiply. That comment is what stops a later edit
from reordering the channels and breaking it.

**Two copies of one instruction stream, checked mechanically.**
`blend_group8.S` and the `asm volatile` block in `blend_pie_kernel.cpp` carry
the identical mnemonic and operand sequence. `prove_asm_match.py` extracts the
stream from both files and diffs them, so they cannot drift. The `.S` exists to
prove every mnemonic and operand form assembles against the real device
assembler and to give the instruction counter something to read. The `.cpp` is
what would ship.

**The bound that the vector path must never be handed.** The header works a
counterexample by hand: at alpha 255 the weighted-average formula does not
reproduce the foreground exactly, so any group containing a fully opaque lane
must take the copy path instead. The caller enforces it by construction.

## Runtime constant broadcast

PIE has no scalar-broadcast-into-lanes instruction. `EE.MOVI.32.Q` sets one
32-bit lane group at a time, which is four instructions to fill a register. The
idiom instead builds the value in a small aligned stack buffer and loads it
once.[^brief]

```cpp
__attribute__((noinline)) static void fillBgPie(uint16_t *dst, int nOct, uint16_t bg) {
    alignas(16) uint16_t bcast[8] = {bg, bg, bg, bg, bg, bg, bg, bg};
    ...
    asm volatile("ee.vld.128.ip q0, %[src], 0\n" // q0 = bg x8, resident for the whole loop
                 "1:\n"
                 "ee.vst.128.ip q0, %[wr], 16\n"
```

Note the immediate 0 on the setup load. The value is read once and the pointer
must not advance. The same shape appears in `AnimFireflies.cpp:341` and in every
kernel that needs a per-call constant in lanes.

Where the broadcast table is a per-call constant rather than a per-loop one, it
is hoisted further out. `nebulaFieldPie` takes its three resident constants as
a 24-entry `int16_t` array built once per `band()` call, because an earlier
round rebuilt it twice per row and the device measurements said table access is
where the cycles are.

## Scalar shapes

Two things have no vector form on this part: gathers and per-pixel clamping
into a lookup. Both are written by hand in scalar assembly rather than left to
the compiler or forced into PIE.

### The gather loop

`nebulaGatherScalar` is the shape the brief describes: index load, scaled add,
palette load, pack, store.[^brief]

```cpp
    asm volatile("1:\n"
                 "l16si %[s0], %[pi], 0\n"
                 ...
                 "addx2 %[s0], %[s0], %[pal]\n"
                 ...
                 "l16ui %[s0], %[s0], 0\n"
                 ...
                 "slli %[s1], %[s1], 16\n"
                 "or %[s0], %[s0], %[s1]\n"
                 ...
                 "s32i %[s0], %[pr], 0\n"
```

Four pixels are interleaved by stage, not by pixel. All four indices load, then
all four addresses compute, then all four colours load, then the two pack and
store pairs. The scalar load-use interlock costs one stall cycle when the very
next instruction consumes a load, and staging this way leaves only two such
pairs in the body.

Three instruction choices carry meaning:

- `addx2` folds the `uint16_t` stride into the add, so the address is one
  instruction rather than a shift and an add.
- `l16si` loads the index and sign-extends, because the index can be negative.
  `l16ui` loads the colour and does not, because a colour's bits are not a
  signed quantity. Getting these the wrong way round is a silent wrong-pixel
  bug, not a fault.
- Two colours pack into one `s32i`. The destination row is 4-byte aligned and
  the groups are even-based, so this halves store traffic against two `s16i`.

The register count is deliberate. Four scratch plus four fixed values stays
inside the roughly thirteen usable address registers, and the comment says an
8-wide unroll would not.

### Vector index computation feeding a scalar gather

The two halves compose. `nebulaFieldPie` computes sixteen palette indices per
iteration in lanes and stores them to an aligned `int16_t` buffer.
`nebulaGatherScalar` then walks that buffer. `AnimNebula.cpp:1489` runs the
vector pass in two calls split at the table period, then one gather call over
the whole row. The arithmetic is vectorised and the addressing is not, because
only the addressing lacks an instruction.

### Clamp, index and pack with a hardware loop

`lavaFinalizeQuadAsm` does four pixels per iteration inside a `loopnez`.

```cpp
    asm volatile("movi    %[cap], 255\n"
                 "movi    %[zero], 0\n"
                 "loopnez %[n], 1f\n"
                 "l32i    %[t3], %[field], 12\n" // fieldRow[x+3]
                 ...
                 "min     %[t3], %[cap], %[t3]\n"
                 ...
                 "max     %[t3], %[t3], %[zero]\n"
                 "extui   %[t3], %[t3], 0, 16\n"
                 "addx2   %[t3], %[t3], %[lut]\n"
                 "l16ui   %[t3], %[t3], 0\n"
```

`loopnez` rather than `loop`, because a span with no full quad left after the
alignment prefix must run zero iterations and a plain `loop` runs one. The clamp
is `min` then `max` against two registers holding the bounds, not a branch.
`extui` is the free shift-and-mask. The four lanes are software-pipelined by
hand so no load feeds the instruction after it.

The sibling kernel in the same file records the opposite result honestly. The
field-accumulation loop was transcribed instruction for instruction off GCC's
own output into a hand-written `loopnez` and is at parity by construction,
because the compiler had already filled the one load-use gap with an independent
add. No edge was available past the transcription, and the comment says so.

## The `bandRef` twin

Every animation whose `band()` dispatches to assembly carries a portable C++
implementation as `bandRef` in its registry entry.[^bganim] The registry field
comment states the contract:

> Same contract and the same output, pixel for pixel: the on-device equivalence
> test (`SleepAnimation::runAnimTest`, `/api/debug/animtest`) renders every band
> of several frames through both and reports the first pixel that differs, which
> is the only way an assembly kernel gets validated, since the host bench
> compiles the C++ path only.

Two consequences shape how the files are written. On a non-Xtensa build,
`band()` is `bandRef` rather than merely equivalent to it, so there is no second
scalar path to keep in sync. And where a kernel restructures the algorithm, the
reference is restructured the same way, because it is the specification rather
than a fallback.

`AnimOrbits.cpp` shows the cheapest version of this. The kernel replaces only
the background fill, and everything after it is a call to the same shared scalar
overlay function `bandRef` calls, so pixel-exactness holds by construction
instead of by a maintained duplicate.

## The row-pair shape

Half the animations render one row of a pair and copy it to the other. Getting
this wrong is the most reproduced bug in the fleet, so the shape is fixed.

```cpp
        const int y = y0 + row;
        const int ySrc = y & ~1;
        const int phase = (y >> 1) & 3;
        uint16_t *out = dst + static_cast<size_t>(row) * w;
        if ((y & 1) == 0 && row + 1 < rows) {
            renderRow(out, ySrc, w, phase);
            memcpy(out + w, out, static_cast<size_t>(w) * sizeof(uint16_t));
            row += 2;
        } else {
            renderRow(out, ySrc, w, phase);
            row += 1;
        }
```

The rule is that a row's content and its dither phase come from `y` alone,
derived before the code decides whether to duplicate. Never from the call's
shape, and never from a neighbour's position inside the call buffer.

The reason is a real production path, not a test artifact. The interlaced render
calls `band()` with `rows == 1`, one row at a time, every other row per frame.
A version that kept the pair phase only when both rows were in the same call,
and fell back to `y & 3` for a lone row, rendered the same absolute row with a
different dither phase depending on who its neighbours were. The host golden
diff passes that bug. `interlace_check` in `tools/animbench` exists to catch it
and did.

`bandAsm` in the same file copies the pairing logic verbatim from `bandRef` and
swaps only the row renderer, so the two cannot diverge on this.

## Flag-gated kernels and their portable twins

Three kernels ship behind a per-animation flag: `GM_BGANIM_LAVA_ASM`,
`GM_BGANIM_SILK_ASM` and `GM_BGANIM_SILK2_ASM`. The pattern is the same in all
three.

```cpp
#ifndef GM_BGANIM_SILK2_ASM
#define GM_BGANIM_SILK2_ASM 1
#endif
```

The default records the device verdict, not the author's hope. Silk 2 measured
1.16x and defaults on. Lava tied at 0.99x and defaults on. Silk lost at 0.87x
against the compiler and defaults to 0, with its kernels kept in the file,
bit-exact and fuzzed, for a next attempt. The comment at the silk flag names the
verification ladder that would let the default flip back.[^measured]

The part worth copying is the twin. Inside the flag, the `__XTENSA__` guard has
an `#else` branch that defines a portable C++ function with the same name and
signature as the assembly kernel:

```cpp
uint32_t silk2PairRowAsm(uint16_t *out, const int16_t *cfA, const int16_t *cfB, const uint16_t *lut,
                          const int16_t *sheenLut, uint32_t turn, int32_t step2, int nPairs) {
```

That branch compiles when the flag is on but the target is not Xtensa, which is
the host bench, and also when `GM_BGANIM_NO_ASM` forces the kernel off on a real
device. Either way the dispatching `band()` is one piece of source, unmodified
by which branch compiled. Only the body of the named function differs. Without
the twin, turning the flag on would take the glue code out of the host bench,
the fuzzer and the interlace check, which are the three places the glue's own
bugs get caught. The twins are copied from the plain C references in
`tools/qemubench/tests/anim_<id>/main.c`, so the same arithmetic is what the
QEMU test checked the assembly against.

## Footnotes

[^brief]: This repository, `tools/animbench/ASM_BRIEF.md` at commit `1cef2de8`,
    sections "Deliverable shape" and "PIE facts you will need". The brief names
    `scale565Oct`, the Nebula lerp kernels and the blend kernels as the idioms
    to copy, states the wrapper shape (`asm volatile` inside
    `__attribute__((noinline))` functions taking plain pointers and ints under
    `#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)`), the absence of a
    vector gather and of a scalar-broadcast instruction, the roughly thirteen
    usable address registers before GCC spills around a block, and the gather
    loop shape `l16ui idx / addx2 / l16ui pal / slli+or pack / s32i`.

[^blendS]: This repository, `tools/animbench/kernels-blend/blend_group8.S` and
    `blend_pie_kernel.cpp` at commit `1cef2de8`. The `.S` header states its
    role as a standalone assembler-syntax probe, the `ret.n` instead of a
    windowed-ABI pair, the register budget forcing per-channel-serial
    processing, and the nine versus five `ssai` tradeoff.
    `prove_asm_match.py` in the same directory diffs the two instruction
    streams.

[^bganim]: This repository, `src/display/ui/default/bganim/BgAnim.h:46` to `:54`
    at commit `1cef2de8`, the `bandRef` field of `struct BgAnimation` and its
    comment.

[^measured]: [measured] Device A/B via `/api/debug/anim?useref=1` on the bench
    board, 2026-09-05, recorded in the root `CLAUDE.md` "Animation kernels"
    section and in the flag comments at
    `src/display/ui/default/bganim/AnimSilk.cpp:243` and
    `AnimSilk2.cpp:133` at commit `1cef2de8`. Silk 2 1.16x, lava 0.99x, silk
    0.87x against each animation's own `bandRef` under production conditions.

[^trm-lane]: Espressif Systems, 2026. *ESP32-S3 Technical Reference Manual*,
    version 1.8, chapter 1 "Processor Instruction Extensions (PIE)", sections
    1.8.209 `EE.VUNZIP.8` and 1.8.212 `EE.VZIP.8` for the interleave and its
    inverse, 1.8.122 `EE.VMUL.S16` and 1.8.128 `EE.VMUL.U16` for the shifted
    32-bit product, 1.8.216 `EE.ZERO.Q`, and 1.8.49 `EE.SRC.Q`.
    https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf
