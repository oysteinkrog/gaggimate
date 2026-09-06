---
title: "The verification ladder: host, real assembly, QEMU, device"
id: 07-our-work/verification-ladder-host-asm-qemu-device
schema_version: 1
doc_type: how-to
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, pie, verification, qemu, animbench, qemubench, gaggimate]
confidence: high
---

# The verification ladder: host, real assembly, QEMU, device

This repo's 13 background animations run a hot per-pixel path, `band()`,
that has hand-written Xtensa kernels behind a compiler-generated
reference (`bandRef()`). Before any such kernel change ships, it climbs
four rungs, each proving something the rung below it cannot. None of the
four steps is skippable, because none subsumes another: a host build
cannot run the real instructions, the real compiler's assembly cannot
run at all, QEMU can run the instructions but not time them and can
disagree with silicon on some of them, and the device is the only rung
that is both correct and fast in the sense that ships.

## The four rungs

| Rung | Tool | Proves | Cannot prove |
|---|---|---|---|
| 1. Host goldens | `tools/animbench`, `make check` | The kernel's C++ or portable form matches a reference image and the call-shape invariant holds | Nothing about the real instruction set; a host build never touches PIE |
| 2. Real compiler's assembly | `tools/animbench/xtensa-asm14.sh` | What the firmware's own compiler actually emits for this source: instruction shape, libcalls, loop form | Whether it runs, or runs correctly |
| 3. QEMU | `tools/qemubench` | The instruction sequence is legal and bit-exact against a reference, under Espressif's ESP32-S3 emulation | Timing, and agreement with silicon on the arithmetic itself |
| 4. Device | `/api/debug/animtest`, `/api/debug/anim?useref=1` | Bit-exact on the real chip, and the real speed | Nothing, but it is slow to iterate on |

A kernel that has not climbed a rung has not proven what that rung
proves. A change that skips straight from a host golden pass to a
device flash has skipped rungs 2 and 3, and the record below is why each
one caught something the others did not.

## Rung 1: host goldens

`tools/animbench` compiles every animation's `.cpp` directly with host
g++, no cross-toolchain, no PIE: `make check` builds two binaries from
one `Makefile` and runs both.

`./build/bench --compare golden --frames 240` renders each animation at
frames 30, 120 and 210, decodes the stored reference PPM, and computes a
mean and max absolute difference per RGB888 channel. The gate is
`mean <= 3.0 && max <= 48`; a failure is either a real regression or a
new animation that shipped with no golden. The tolerance is deliberately
loose because goldens are meant to catch a wrong-picture regression, not
to enforce bit-identical output between a kernel and its portable twin
(that comparison is rung 3 and 4's job, and it is exact there, not
loose).

`./build/interlace_check` checks a different property: that `band()` is
pure with respect to a call's own rows. Production's interlaced path
calls `band()` once per animation per frame with `rows==1` and
parity-skipping row sequences, never a single whole-frame call, so a
kernel that derives "which row is real" from an offset local to the call
or copies a row from a neighbour inside the destination buffer can pass
the golden-image diff above and still paint wrong rows once interlacing
engages on the device. The checker renders one `frame()` and then
re-renders the same frame in every shape a real call site uses: 8-, 16-,
4- and 2-row bands, solitary `rows==1` calls, and both parity-skipping
`rows==1` sequences, each at 480x480 and again at 240x240 (the
half-resolution path's row width). Every shape's output must be
identical row for row; rendering the 8-row reference shape twice is the
control that proves the render itself is deterministic. Three of four
kernel redesigns attempted on 2026-09-05 broke exactly this invariant
while still passing the golden diff, until the checker caught the first
one at integration.

A kernel not yet wired into the animation registry gets the same two
checks through `render_one`: `make -f Makefile.render CAND=<descriptor>`
builds a per-candidate binary, and `--shapes` on that binary runs the
same call-shape sweep `interlace_check` runs for the registered fleet,
so an unregistered candidate is proven before it ever touches `src/`.
See [`bit-exact reference tests and
fuzzing`](../05-measurement/bit-exact-reference-tests-and-fuzzing.md)
for the general differential-testing method this rung is an instance
of, and `Makefile.fuzz` for the sanitizer-driven fuzz pass, which needs
ASan on to be a fuzzer at all: without it a one-entry table overrun
reads the neighbouring byte and passes silently, which is how one
animation shipped a palette pad against a dither amplitude wider than
its lookup table.

## Rung 2: the real compiler's assembly

`tools/animbench/xtensa-asm14.sh` cross-compiles one animation's source
with the exact toolchain the firmware build uses:
`toolchain-xtensa-esp-elf` (crosstool-NG esp-14.2.0, GCC 14.2), and the
flags a verbose `pio run -e display` prints for `src/display` (`-O2` is
the only optimization flag on that line; `-fstack-protector` and
`-fno-jump-tables` are on). It writes annotated assembly to
`xtensa-asm14/<name>.S`, assembles it to `xtensa-asm14/<name>.o` as
proof that every inline-asm block in the source actually assembles and
not merely emits plausible-looking text, and pipes the `.S` through
`xtensa_report.py`, which demangles each function and reports
instruction count, a rough LX7 cycle weight, whether the compiler
emitted a zero-overhead loop, and whether any call resolves to a
soft-float libcall (flagging double-precision math, at roughly 4 to 8
times a soft-float call's own already-large cost, as a likely mistake in
a per-pixel path).

An older script, `xtensa-asm.sh`, does the same job against
`toolchain-xtensa-esp32s3` (plain GCC 8.4). Its register allocation and
loop shape are not the firmware's: an instruction count or loop form
measured there is evidence about a different compiler, not this repo's
build. `xtensa-asm14.sh`'s header names this explicitly as the reason it
exists. Use it, not the GCC 8.4 script, for any claim about what ships.

This rung is necessary but not sufficient on its own: it proves what the
compiler emitted, not that the emitted code runs or computes the right
answer. That is rung 3's job.

## Rung 3: bit-exact execution in QEMU

`tools/qemubench` builds a bare-metal Xtensa image and boots it directly
under Espressif's `qemu-system-xtensa` fork with `-kernel`, no
bootloader, no flash image, no partition table.

- **`build.sh <source-subdir>`** compiles and links one test. A source
  directory carrying its own `link.ld` builds standalone (used by the two
  earliest smoke tests, which predate the harness and carry their own
  diagnostic boot code). A source directory with no `link.ld` is
  "harness mode": it gets `harness/start.S`, `harness/vectors.S` and
  `harness/link.ld` added automatically. This is the shape every new
  kernel test under `tests/` uses.
- **The harness** (`harness/start.S`, `harness/vectors.S`) exists because
  a windowed-ABI test needs real `entry`/`retw` support, which only works
  when `PS.WOE` (window overflow enable) is set before the first call,
  and needs a real exception vector table, because `entry`/`retw` traps
  into a window overflow or underflow handler once nested call depth
  exceeds the physical register file. `vectors.S` installs a real Xtensa
  vector table with the `WindowOverflow4`/`Underflow4`/`8`/`12` handlers
  plus a diagnostic catch-all for kernel, user and double exceptions;
  `start.S` sets up a stack pointer, writes `PS` with `WOE` set through
  `WSR.PS` followed by `RSYNC`, and primes `WINDOWSTART`/`CALLINC`
  before calling into the test's `main()`. Getting this right is what
  lets a kernel test be ordinary windowed-ABI C++ with arbitrary call
  depth, rather than the hand-rolled no-calls-at-all style the two
  legacy standalone tests needed.
- **`run.sh <elf> [timeout]`** boots the ELF, captures the emulated
  UART0 stream to a log file, and polls for either the string
  `GM_QEMUBENCH_PIE_DONE` or the timeout, whichever comes first, because
  the test programs spin forever after printing rather than exiting.
  It then greps the log for `GM_QEMUBENCH_PIE: PASS` or `GM_QEMUBENCH_PIE:
  FAIL` and sets its own exit code accordingly; no output at all means a
  boot failure, a wrong load address, or an instruction fault before the
  test ever reached its print.
- **What each test verifies.** Every test under `tools/qemubench/tests/`
  runs a hand-transcribed Xtensa kernel, the same mnemonics, operand
  roles and instruction ordering as the production source (only the
  physical registers may differ, GCC's own allocator choice), against a
  portable C or C++ reference, over test vectors chosen to walk every
  corner the two implementations could disagree on: full boundary sweeps
  of a lookup index, production-width geometry (480 and 240 pixels), the
  minimum trip count a `loopnez` can take (so the zero-iteration guard
  is exercised, not skipped), and case-specific traps such as the
  fixed-point phase accumulators wrapping their index mask more than
  once in one run, or a documented order-sensitive path exercised in
  both directions. Confirmed passing this way across the fleet: PIE
  arithmetic and gather instructions (`ee.vadds.s8`, `ee.vld.128.ip`),
  scalar Xtensa idioms used in the hand-written kernels (`loopnez`,
  `extui`, `addx4`/`addx2`, `addmi`, `mull`, `max`, `srli`), and, in the
  harness itself, `entry`/`retw` under a real vector table with
  `PS.WOE` set.

A test's log line is unambiguous about what it checked: for example
`GM_QEMUBENCH_PIE: PASS lavaFinalizeQuadAsm+lavaFieldGatherAsm bit-exact
vs lavaFinalizeQuadRef+lavaFieldGatherRef reference (9 cases: minimal
trip count, n=0 defensive loopnez checks, production widths 480 and
240, an odd/unaligned span with nonzero curvature, and LUT indices
spanning both clamp/margin extremes)`. That is the standard this rung
holds every kernel to: not "it ran", but "it ran and matched, over a
named list of cases chosen to be adversarial".

**The caveat that keeps this rung from being rung 4.** Espressif's QEMU
fork has open, hardware-confirmed divergences in exactly the PIE
instructions this repo's kernels use: signed clamp direction on
`EE.VADDS`/`EE.VSUBS`, indexed-address offset on `EE.LDXQ.32`/
`EE.STXQ.32`, shift-by-32 behaviour at `SAR=32` on `EE.VSL`/`EE.VSR`/
`EE.VMUL`, destination aliasing on `EE.CMUL.S16`, unsigned saturation on
`EE.VMULAS.U8.QACC`, and unmasked dynamic shift counts on
`EE.SLCXXP.2Q`/`EE.SRCXXP.2Q`. See [`QEMU for the ESP32-S3, what it
proves and what it cannot`](../05-measurement/qemu-esp32s3-what-it-proves-and-what-it-cannot.md)
for the full issue table and citations. A PASS at this rung is real: it
proves the sequence is legal and matches a reference on the emulator's
own arithmetic. It is not proof the chip agrees, and it is never
evidence about speed (QEMU's `CCOUNT` is a wall-clock reading scaled by
a fixed, wrong-for-the-part frequency, not a count of work; see the
same leaf). Both properties are why rung 4 still has to run.

## Rung 4: the device

The device rung is where correctness and speed both become the real
ones, and it is the only rung that can.

- **`/api/debug/animtest?anim=N[&frames=K]`** (`SleepAnimation.cpp`,
  `WebUIPlugin.cpp`) queues a run on the render task and returns
  immediately; a plain GET without `anim` returns the last completed
  result, with `pending: true` while a run is still queued. The test
  installs the named animation, then for each of 3 fixed parameter sets
  (the animation's own defaults, all-zero, and all-100) renders `frames`
  timesteps (default 8) and, for every band of every frame, runs both
  `band()` (the kernel under test) and `bandRef()` (the portable
  reference) into separate buffers pre-filled with different marker
  bytes, so a kernel that leaves pixels unwritten cannot pass by luck.
  Which of the two runs first alternates band by band, so neither one
  systematically pays the other's cold-cache cost. The result reports
  `mismatch_px`, which must be 0, plus the frame, parameter set, and
  pixel coordinates of the first mismatch when it is not. This is the
  rung 3 test's exact shape, run on real silicon instead of an emulator,
  and it is what actually decides whether a kernel is correct: an
  animation with no `bandRef()` reports `has_ref: false` and the test
  proves nothing for it.
- **`/api/debug/anim?useref=1`** swaps the render loop over to
  `bandRef()`, so the device's own frame-rate and timing counters (see
  `/api/debug/anim`'s frame counters) measure the reference path under
  the same production conditions the hand-written kernel normally runs
  under. Comparing a `useref=0` run against a `useref=1` run is the
  speed claim, measured, not estimated from an instruction count or a
  host timing.
- **`C:\work\camshots\anim_rung4.py [ids]`** and **`anim_devbench.py`**
  are the driver scripts for this rung, and they live outside this repo
  (a separate Windows-side tools tree, per the root `CLAUDE.md`'s "Bench
  facts" section). `anim_rung4.py` runs the `animtest` correctness check
  and the `useref` speed A/B together across the named animations, or
  the whole fleet with no arguments; `anim_devbench.py` alone runs just
  the speed A/B, and takes a `RESERVE=` environment variable that pins
  the band buffers' memory pool so a run is not measuring an incidental
  allocation-pool difference between the two paths.

The fleet's default-flag decisions came out of exactly this rung, not
rungs 1 through 3: three lava/silk/Silk-2 kernels were bit-exact in QEMU
and on the device, and still split on the device's own speed measurement
into one win, one tie and one loss against the compiler's own code,
which is why "passed QEMU" and "ships enabled" are different sentences
in this repo's history.

## Why the order cannot collapse

Each rung's failure mode is invisible to the rungs around it:

- A host golden pass says nothing about the real instruction set: rung 1
  never touches PIE, and a host build's out-of-order, superscalar core
  hides load-use stalls and cache misses a kernel's real cost depends
  on (see [`Host benchmarks versus the
  device`](../05-measurement/host-benchmarks-versus-the-device.md)).
- A clean rung-2 disassembly says nothing about whether the code runs
  correctly; it is a description of what the compiler emitted, read by a
  human or `xtensa_report.py`, not an execution.
  Reading it against GCC 8.4 instead of GCC 14.2 (the older
  `xtensa-asm.sh`) reads a different compiler's decisions and is not
  evidence about this build at all.
- A rung-3 PASS says the sequence is legal and self-consistent inside
  Espressif's emulator, on the emulator's own arithmetic; it does not
  say silicon agrees, and it says nothing whatsoever about speed.
- Rung 4 is the only rung that is both real and slow to iterate on
  (a flash-and-run cycle, not a compile). Climbing rungs 1 through 3
  first is what keeps rung 4 runs down to confirming a kernel that has
  already been proven legal, bit-exact against a reference, and
  call-shape safe, rather than debugging all of that on hardware.

The four rungs are not redundant checks of the same fact; they are four
different, non-overlapping facts, and a kernel change earns the right to
ship only once it has all four.
