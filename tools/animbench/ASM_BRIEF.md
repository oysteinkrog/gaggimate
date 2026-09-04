# bganim assembly pass: brief (2026-09-04)

You are rewriting the hot path of ONE background animation as hand-written
Xtensa LX7 assembly for the ESP32-S3, using the PIE vector unit (EE.*
instructions, eight 16-bit lanes or sixteen 8-bit lanes per 128-bit q
register) wherever the arithmetic vectorises, and hand-scheduled scalar code
with zero-overhead loops where it does not. Read OPTIMIZE.md first: every
rule there still holds (zero libm per pixel, no float divides, tables built
in frame(), device-over-host when they disagree). This brief adds what the
assembly pass needs on top.

## Where the fleet stands on the device (2026-09-04, production build)

Measured on the bench device with `C:\work\camshots\anim_devbench.py`:
full resolution, interlace pinned off, default params, default theme.
`band_us` is the whole 480x480 field for one frame (240 band() calls of
2 rows each), wall clock on the render task, so it includes the preemption
the task suffers at priority 1 on core 0 under BLE, WiFi and the control
loop. `blend_us` is the overlay composite (not yours) and `frame_us` the
frame period, which quantises to the panel's 19.7 ms VSYNC (39 ms = 25 fps,
58 ms = 17 fps, 78 ms = 12.7 fps).

| id | anim      | band_us | blend_us | frame_us | host band_ms | host x80 |
|----|-----------|--------:|---------:|---------:|-------------:|---------:|
| 0  | plasma    |  13,334 |    5,594 |   38,288 | 0.070 |  5.6 |
| 1  | lava      |  32,506 |    8,072 |   57,961 | 0.225 | 18.0 |
| 2  | silk      |  21,187 |    5,161 |   39,000 | 0.120 |  9.6 |
| 3  | starfield |  18,665 |    8,909 |   42,016 | 0.086 |  6.9 |
| 4  | aurora    |  57,920 |    6,633 |   77,990 | 0.225 | 18.0 |
| 5  | ripples   |  14,004 |    6,366 |   38,988 | 0.118 |  9.4 |
| 6  | caustics  |  28,255 |    5,722 |   58,539 | 0.137 | 11.0 |
| 7  | mandala   |  46,739 |    7,252 |   77,989 | 0.256 | 20.5 |
| 8  | orbits    |   6,236 |    5,731 |   38,941 | 0.047 |  3.8 |
| 9  | fireflies |  13,415 |    6,100 |   38,500 | 0.123 |  9.8 |
| 10 | steam     |   7,990 |    6,112 |   39,922 | 0.108 |  8.6 |
| 11 | ember     |  35,399 |    6,132 |   58,541 | 0.265 | 21.2 |
| 12 | nebula    |  39,453 |    5,517 |   57,993 | 0.320 | 25.6 |

The device is 2 to 3x worse than the host-scaled estimate for most of the
fleet. The host number counts instructions on a wide out-of-order core; the
device pays for load-use stalls (one cycle whenever the instruction after a
load consumes it), for spills out of a 16-register window, for PSRAM cache
misses on any table over 8 KB (noiseTex256 is 64 KB and lives in PSRAM;
`bganim::alloc` sends anything over `SRAM_ALLOC_LIMIT` = 8 KB there), and
for the 240 band() calls per frame (BAND_H is 2 on the device, not the
bench's 8: per-call setup is paid 240 times). Plasma's compiled inner loop
is 15 instructions per pixel pair inside a hardware loop and still costs
14 cycles per pixel on the device, so instruction count is roughly half the
story and scheduling and memory are the other half.

Target: every animation at full resolution inside one VSYNC of render time
(band + blend under 19.7 ms, so band under ~12 ms) so the frame runs at the
panel rate, and the heavy ones (aurora, mandala, nebula, ember, lava,
caustics) at least under 39 ms so they stop dropping to half resolution and
17 or 12 fps. Cheaper than that is better: whatever the band saves is
headroom against the radios.

## Deliverable shape

1. `band()` stays the registry's band entry. On the device it dispatches to
   your assembly kernel(s); on the host it is (or calls) the portable C++
   implementation.
2. The portable C++ band becomes `bandRef` and goes into the registry entry
   as the new trailing field of `BgAnimation` (BgAnim.h). It is the spec:
   the host bench runs it against the goldens, and the device equivalence
   test runs it against your kernel. Keep it pixel-exact with the kernel.
   If you restructure the algorithm (new table layouts, Q formats, a
   coarse grid), restructure BOTH and keep the goldens OK.
3. Assembly lives in your `Anim<X>.cpp` under
   `#if defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)` as `asm volatile`
   blocks inside `__attribute__((noinline))` functions that take plain
   pointers and ints, so the same function can be dropped into a QEMU test
   unchanged. Precedents to copy the idiom from, all in this repo:
   - `SleepAnimation.cpp` scale565Oct (~line 234): the canonical PIE kernel
     with its constant-table load, SAR hoisted above the loop, and the
     alignment warning.
   - `AnimNebula.cpp` lerpRowPie / the unaligned variant (~line 247): byte
     widen/narrow with EE.VZIP.8 / EE.VUNZIP.8, EE.VMUL.S16 with an
     arithmetic shift, EE.LD.128.USAR.IP + EE.SRC.Q for an unaligned source.
   - `tools/animbench/kernels-blend/blend_group8.S` and
     `blend_pie_kernel.cpp`: per-channel RGB565 arithmetic in lanes, the
     "multiply by 2048 to shift left" trick (there is no vector left shift),
     mask tables, register budgeting across a whole group.
   - `tools/overlaybench/kernels/span_scan.cpp`, `overlay_blend.cpp`.
4. A QEMU test for each kernel in `tools/qemubench/tests/anim_<yourid>/`
   (see below). It runs the kernel and the C reference on synthetic inputs
   covering the full input range and prints PASS/FAIL over UART.

Where PIE does not fit (palette gathers: there is no vector gather), write
the scalar loop by hand anyway: interleave two or four pixels so no load is
consumed by the next instruction, keep every base pointer and constant in a
register, use `loop`/`loopnez` (LOOP option, body under 256 bytes, no
nesting) and emit pixel pairs with one `s32i`. A typical gather loop is
`l16ui idx / addx2 / l16ui pal / slli+or pack / s32i` per pixel, about 4.5
instructions with the index computation vectorised beforehand into a small
aligned stack buffer (EE.VST.128.IP of the eight indices, then eight scalar
lookups). Measure both shapes on the device before choosing.

## PIE facts you will need

Verified on this hardware or in the precedents above; anything else, test
in QEMU first (see below) and say in your report that you did.

- PIE is coprocessor CP3, thread context only. band() runs on the SleepAnim
  task, never in an ISR, so the q registers are saved lazily per task and
  SAR is in the ordinary context frame: `ssai` hoisted above a loop survives
  interrupts and task switches. The compiler never allocates q registers,
  so an asm block may use q0-q7 freely without a clobber list (there is no
  constraint syntax for them); state the fact in a comment.
- `ee.vld.128.ip qN, aR, imm` / `ee.vst.128.ip qN, aR, imm` load/store 16
  bytes and post-increment aR by imm (imm is a multiple of 16, may be 0 or
  negative). THEY MASK THE LOW FOUR ADDRESS BITS SILENTLY instead of
  trapping: a misaligned pointer corrupts the neighbours. Band rows are 960
  bytes and the band buffer is 64-byte aligned, so `dst` rows are aligned;
  your own tables are aligned only if you make them so (bganim::alloc
  returns heap_caps_malloc alignment, at least 4; over-allocate and align
  up by hand, or use `alignas(16)` statics for small constant tables).
  For unaligned sources use `ee.ld.128.usar.ip` + `ee.src.q` as Nebula does.
- `ee.vmul.u16 qd, qa, qb`: eight 32-bit products, each shifted right by
  SAR (logical), low 16 bits kept. `ee.vmul.s16`: same with an arithmetic
  shift and signed operands. There is no vector left shift; multiply by a
  power-of-two constant at SAR=0.
- `ee.vadds.s16` / `ee.vsubs.s16` saturate to int16; `ee.vadds.s8` etc.
  for bytes. `ee.andq`, `ee.orq`, `ee.xorq` are bitwise. `ee.zero.q`.
  `ee.vzip.8/16` interleaves a register pair (writes both), `ee.vunzip.8/16`
  is the inverse; zip against a zeroed register is a zero-extending widen.
  `ee.movi.32.q qN, aR, sel` moves one 32-bit word into lane group sel of
  a q register (broadcast by loading a 16-byte constant instead).
- Xtensa scalar: `l16si/l16ui/l8ui` (no sign-extending byte load), `addx2/
  addx4/addx8` fold a scaled index into an add, `extui` is the free
  shift-and-mask, `mull` is a 32x32 multiply (2 cycles), `min/max/minu/maxu`
  exist, `movltz/movgez/moveqz/movnez` are conditional moves, `sext`
  sign-extends. Load-use interlock: one stall cycle if the very next
  instruction consumes a load. Branch to a not-yet-fetched target costs
  taken-branch cycles; the `loop` instructions remove the back edge.
- Windowed ABI inside inline asm: your operands arrive in whatever ARs GCC
  picks; you have roughly 13 usable ARs before GCC starts spilling around
  the block. Fewer live scalar values beats more unrolling (OPTIMIZE.md's
  silk finding). Use `"+r"`/`"r"` constraints, `"memory"` clobber, and list
  any scratch AR you name explicitly in the clobber list.

## Verification ladder (all four rungs, in order, evidence in the report)

1. Host goldens, C++ path: `make BIN=build/bench_<id>` then
   `./build/bench_<id> --anim <N> --frames 240 --compare golden` (mean <= 3,
   max <= 48 at f030/f120/f210, and prefer 0/0). Then the full harness once.
2. Device compiler, real codegen: `./xtensa-asm14.sh Anim<X>` compiles your
   file with the firmware's own compiler (GCC 14.2, the real flags) to
   `xtensa-asm14/Anim<X>.S` and `.o`. The .o proves every EE.* mnemonic and
   operand form assembles; the .S shows what GCC did around your blocks.
   (The older `xtensa-asm.sh` uses a GCC 8.4 toolchain the firmware does
   not; ignore its counts.)
3. QEMU, real execution: `tools/qemubench` boots a bare-metal ELF on
   Espressif's qemu-system-xtensa fork, which executes PIE instructions
   bit-exactly (verified for ee.vld/vst.128.ip, ee.zero.q, ee.vadds.s8/s16,
   ee.vmul.u16, ee.andq/orq, ssai, ee.vzip/vunzip via the existing tests).
   Make `tests/anim_<yourid>/main.cpp` (harness mode: a `main()` only, the
   harness supplies start/vectors/link) that includes your kernel function
   and its C reference, runs both over synthetic inputs spanning the full
   range of every operand (including the parameter extremes 0 and 100 as
   they reach the kernel), and prints `GM_QEMUBENCH_PIE: PASS ...` or
   `GM_QEMUBENCH_PIE: FAIL ...` naming the first failing lane (run.sh greps
   exactly those two strings), then `GM_QEMUBENCH_PIE_DONE` (run.sh waits for
   that marker). `./build.sh tests/anim_<yourid>` then
   `./run.sh build/tests/anim_<yourid>.elf 30`. Freestanding: no libc,
   libm, malloc, or globals with constructors; the kernel must therefore
   take everything through its arguments. Read tests/blend_row/main.c for
   the UART and the harness conventions. If an instruction you need is not
   on the verified list, write a one-instruction QEMU probe for it first;
   an unimplemented instruction shows up as garbage or an exception dump,
   never as a quiet pass.
4. Device (leader runs this; you cannot flash): `/api/debug/animtest?anim=N`
   renders 8 frames x 3 parameter sets (defaults, all 0, all 100) through
   band() and bandRef() on the real chip and reports `mismatch_px` (must be
   0), the first differing pixel, and `band_us` vs `ref_us` (your kernel's
   speedup free of preemption noise, since both run back to back).
   `anim_devbench.py N` gives the production band_us. Report your kernel
   ready when rungs 1-3 hold; the leader flashes, runs rung 4 for the
   fleet, and sends you the numbers or the first mismatch to fix.

## Hard constraints

- Edit ONLY `src/display/ui/default/bganim/Anim<X>.cpp` and your own
  `tools/qemubench/tests/anim_<yourid>/`. Not BgAnim.h, BgAnimCommon.*,
  the registry, other animations, SleepAnimation.*, bench sources,
  golden/, BASELINE.md, xtensa-asm*.sh, qemubench/build.sh or harness/.
  A shared helper that several animations would want goes in your report,
  not into BgAnimCommon.
- Public contract unchanged: registry id, name, param defs and semantics,
  init/frame/band/release signatures, theme reactivity (poll
  bganim::themeGen() in frame() and rebuild palettes on change).
- Memory: `bganim::alloc` and `SRAM_TOTAL_BUDGET` (28 KB fleet ceiling,
  8 KB per-table SRAM limit) are the law; every table you add gets a
  matching entry in release(). Internal SRAM is what the web UI lives on
  (see CLAUDE.md "Internal DRAM budget"); do not raise limits.
- No commit. Leave your files modified; the leader builds the firmware,
  flashes, runs rung 4 and commits.
- Comment style as in the file: algorithm and fixed-point scheme at the top
  of the kernel, every non-obvious constant, and a note wherever a choice
  is device-over-host so the next pass does not revert it against the host
  number.
- No em dashes in comments or reports. Plain sentences.

## Report back (final message, compact)

1. Device-relevant instruction counts before/after from xtensa-asm14 for
   band and each kernel, and the estimated cycles/pixel from ops counting,
   with the load-use stalls you could not schedule away.
2. Host bench before/after (band_ms, libm/frame), golden diffs at
   f030/f120/f210 for the C++ path.
3. QEMU test: what it covers, PASS line verbatim.
4. Techniques applied, one line each; what PIE does and what stays scalar,
   and why.
5. Risks: overflow corners, param extremes, alignment assumptions, anything
   rung 4 should look at first. Shared-helper suggestions.
