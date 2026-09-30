# tools/qemubench

Bare-metal Xtensa test harness: boots a freestanding ELF straight in
QEMU's `-kernel` loader (no bootloader, no ESP-IDF, no libc) to prove that
a hand-written asm kernel from `src/display/ui/default/bganim/Anim*.cpp`
(or, for `blend_row`, from `tools/animbench/kernels-blend/`) executes
correctly on real Xtensa instruction semantics -- something neither the
host bench (`tools/animbench`, which never compiles the asm path) nor
`xtensa-asm14.sh` (proves assembly and register allocation, never
execution) can check. See `build.sh`'s own header comment for the build
modes and CLAUDE.md's "Animation kernels" section for how this fits the
rest of the kernel-verification ladder.

## check_copies.py: is the copy still the kernel?

Every test under `tests/anim_*` (plus `blend_row`) says in its own header
comment that its kernel body is "transcribed verbatim" from a named
function in `src/`. That was true when each test was written, but nothing
enforced it staying true: `src/`'s kernel can change -- a fixed clamp, a
new instruction, a wider unrolled loop -- while the test's hand copy does
not, and the test keeps passing against its own stale copy. A PASS then
proves nothing about the file that actually ships (gm-bzu.35).

`check_copies.py` is the enforcement. For every test it knows about, it
extracts the named function's `asm volatile(...)` /
`__asm__ volatile(...)` block from both the `src/` (or
`tools/animbench/kernels-blend/`) file and the test file, normalises away
comments and incidental whitespace, and diffs the rest. `build.sh` runs it
before compiling and refuses to build a test whose copy has drifted.

```sh
python3 check_copies.py                 # every mapped test, exit 1 on any mismatch
python3 check_copies.py anim_lava       # just one test
python3 check_copies.py --repo-root DIR # check a different checkout (see "Reproducing a
                                         # failure" below)
```

### What counts as a difference

Normalisation strips only:

- comment style and wording (`// fieldRow[x+2]` vs `/* fieldRow[x+2] */`
  are the same comment about the same instruction, not a divergence);
- `asm` vs `__asm__` (the same GCC keyword, spelled either way);
- incidental whitespace and how the C++ source wraps a template string or
  an operand list across physical lines (`asm volatile(\n"..."` and
  `asm volatile("..."` are the same call; a long operand list on one
  physical line or wrapped across several is the same operand list).

Everything else -- mnemonics, operand names, immediates, load/store order,
the output/input/clobber lists and their order, even a cast style
(`static_cast<int32_t>(x)` vs `(int32_t)x`) or which local variable a named
operand is bound to -- must match exactly. The mapping table's own header
comment in `check_copies.py` explains why the diff is this literal rather
than judging which differences are "just style": a renamed register or a
reordered instruction is exactly the kind of thing a looser check would
wave through, and this project's own history (AnimCaustics below) is a
kernel that grew a wider unrolled loop while its test's copy did not --
that is also "just" an operand-list and instruction-count difference by a
loose enough reading.

### Mapping table

`check_copies.py`'s `MAPPING` dict names, for every test, which `src/`
function(s) it claims to transcribe. A test with two definitions under the
same function name (a device asm body plus a portable C++ twin used off
real hardware -- `AnimLava.cpp`'s and `AnimSilk.cpp`'s `*Asm` functions,
`AnimSilk2.cpp`'s `silk2PairRowAsm`) is handled automatically: the script
tries every definition and keeps the one whose body actually contains an
asm block, so the mapping only needs the function name once.

Tests with **no** source to check against are listed separately, in
`NO_CORRESPONDENCE`, each with a one-line reason (a standalone PIE/QEMU
capability probe that was never a transcription of anything, or
`blend_group8`, which `#include`s its kernel directly and so cannot drift
from it by construction). A test name `build.sh` or `check_copies.py` does
not recognise at all -- neither mapped nor declared exempt -- is refused,
not silently skipped, so a newly-added test must be triaged into one
bucket or the other before it can build. That refusal is deliberate: see
Astra's note on the parent bead, which is exactly this failure mode (a
check that can be silently bypassed by adding a test outside its
awareness).

### Refreshing a copy

When a kernel in `src/` genuinely changes and the test's hand copy needs to
follow it:

1. Open the `src/` function and the test's copy of it side by side.
2. Re-transcribe the `asm volatile(...)` block into the test file by hand
   -- mnemonics, operand names, immediates, load/store order, and the
   output/input/clobber lists, in the same order as the source. Keep the
   test's own comment style if you like (comments are not compared); do
   not keep a stale instruction.
3. If the kernel's signature changed (a new parameter, a widened loop),
   update the test's synthetic input cases too -- `check_copies.py` only
   proves the two texts match, not that the test still exercises the new
   shape usefully.
4. Re-run `python3 check_copies.py <test_name>` until it reports OK, then
   `./build.sh tests/<test_name>` and `./run.sh build/tests/<test_name>.elf`
   to confirm it still assembles and executes correctly.

### Reproducing a failure without touching the real checkout

`--repo-root` lets you point the script at a scratch copy instead of this
checkout -- useful for proving the check actually catches drift, without
editing a real `src/` file:

```sh
REPRO=/tmp/some-scratch-dir
mkdir -p "$REPRO/src/display/ui/default/bganim" "$REPRO/tools/qemubench/tests/anim_lava"
cp ../../src/display/ui/default/bganim/AnimLava.cpp "$REPRO/src/display/ui/default/bganim/"
cp tests/anim_lava/main.cpp "$REPRO/tools/qemubench/tests/anim_lava/"
# edit one instruction in $REPRO's AnimLava.cpp, e.g. change "movi %[cap], 255" to 254
python3 check_copies.py --repo-root "$REPRO" anim_lava   # now fails with a diff
```

## Current findings (2026-09-30, gm-bzu.35)

Running `check_copies.py` against this checkout for the first time found
that **15 of 19 test directories match their source verbatim** and **4 do
not** -- real drift this check exists to catch, not tool bugs (each was
read side by side with its source to confirm). `build.sh` now refuses to
build these four until they are corrected; that refusal is the intended
effect of landing this bead, not a regression it introduced. None of these
are fixed here: this bead's file scope is `check_copies.py`, `build.sh`,
and this README, not the test or source files themselves.

- **`anim_caustics`**: the most significant one. `AnimCaustics.cpp`'s
  `causticsRowKernel` carries its own comment, `"GRID==8, was 2 at
  GRID==4"` -- the kernel was widened to an 8-pixel unrolled loop and the
  test's copy was never updated past the old 4-pixel version (different
  shift immediate, half the unrolled body, different step-table names).
  The test currently proves the *old* kernel's instructions are self
  consistent, not anything about the kernel that ships.
- **`anim_aurora`**: `auroraPixelsAsm`'s `movi %[t3], 0` / `max %[t1],
  %[t1], %[t3]` pair in `src/` is `movi %[t2], 0` / `max %[t1], %[t1],
  %[t2]` in the test, with the `movi` moved one instruction later. Whether
  this is a harmless register-reuse rewrite or a real behavioural
  difference was not investigated further here -- flagging it precisely is
  this bead's job, not adjudicating it.
- **`anim_fireflies`** (`drawGlowSpanAsm`) and **`anim_steam`**
  (`fillRowPie`): cosmetic-looking differences only -- a cast style
  (`static_cast<int32_t>(x)` vs `(int32_t)x`) in fireflies' operand list,
  and steam's local variables `dst`/`wr` bound to the opposite operand
  names in source vs test (source parameter `wr` copied to local `dst`;
  test parameter `dst` copied to local `wr` -- the same register slot,
  named the other way around). The instruction template itself is
  identical in both. These still fail under the "normalise only
  whitespace" rule this script deliberately applies (see "What counts as a
  difference" above): a looser rule that waved these through is the same
  kind of looseness that let `anim_caustics` go stale.

## On Astra's note: building with the production toolchain

The bead's second-oracle note (Astra, 2026-09-30) is right that a source
diff alone does not prove a kernel behaves the same under the toolchain
that actually ships it: `build.sh` compiles with the 2021r2 toolchain
(`toolchain-xtensa-esp32s3`, GCC 8.4.0) at `-O1`, while the production
`display`/`display-loadtest` envs build with GCC 14.2.0
(`toolchain-xtensa-esp-elf`, the same one `xtensa-asm14.sh` uses) at `-O2`
(`platformio.ini`'s `display_common.build_flags`).

This was tested, not just reasoned about: every `tools/qemubench` test
was rebuilt by hand with the GCC 14 toolchain's native Linux binaries
(`toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-{gcc,g++}` -- no
`cmd.exe` needed for these, unlike the 2021r2 package; see build.sh's own
comment for why that dance exists at all) at `-O2` and re-run under QEMU.
13 of 14 came back bit-exact, identical to the GCC 8.4/-O1 result. **One
did not: `anim_ripples`'s `accumulateBandAsm` check showed 6 lane
mismatches, every one a 1-ULP float difference** (e.g. asm result
`0x3ef8649e` vs the C++ reference's `0x3ef8649f`) that do not appear under
the current toolchain. This is evidence for exactly the risk Astra named:
the diff-only check in this bead cannot see a toolchain-dependent
behaviour change, because both sides of the diff are unaffected by which
compiler builds the test binary.

This was not chased further or fixed here -- it needs its own
investigation (most likely the float reference's own codegen shifting
under `-O2`/GCC 14, e.g. FP-contraction or a different libm `sqrtf`
lowering, rather than the hand-written kernel itself, since the kernel's
instructions are unchanged bytes either way) and is not a `tools/qemubench`
text-and-wiring change. Recorded here as a concrete, reproduced finding
for whoever picks up production-toolchain QEMU builds next, rather than
switching `build.sh`'s default toolchain in this bead and silently
breaking a fifth, currently-passing test.
