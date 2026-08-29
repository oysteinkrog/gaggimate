# Baseline: overlay pipeline (span scan, scrim build, overlay blend, half-res expand)

Harness: `tools/overlaybench` (`make check`), host x86, 240 frames-equivalent
per kernel (`--iters 200` default; numbers below are a fresh `--iters 200` run
at commit 5e7d9d6d). Every kernel is a byte-for-byte extraction of the real
code at HEAD -- see the file header comment in each `kernels/*.cpp` for the
exact SleepAnimation.cpp line range it came from.

## Toolchain note (corrects an assumption in tools/animbench)

`tools/animbench/xtensa-asm.sh` cross-compiles with
`toolchain-xtensa-esp32s3` (GCC 8.4.0, Windows-only .exe, invoked through
`cmd.exe`). Checking `.pio/build/display/compile_commands.json` shows the
**real** firmware build uses `toolchain-xtensa-esp-elf` (crosstool-NG
esp-14.2.0, **GCC 14.2.0**) instead -- and that package ships a native Linux
ELF `xtensa-esp32s3-elf-g++`, so `asm.sh` in this directory calls it directly
under WSL with no `cmd.exe` round trip. Whether animbench's GCC-8.4 dumps
still track what actually ships is worth someone checking separately; this
harness's dumps are confirmed against the real build's own
`compile_commands.json`.

Also confirmed from `compile_commands.json` and `platformio.ini`: the
display env overrides upstream's `-Os` to **`-O2`** specifically for the
animation hot path (see the comment above the `-O2` line in
`[display_common]`). `asm.sh` uses `-O2 -mlongcalls -fno-exceptions
-fno-rtti`, matching the real compile command for `SleepAnimation.cpp`.

## Host numbers

| kernel | variant | ns/call | ns/px | est. dev. cy/px (pure-ALU, x19.2) |
|---|---|---|---|---|
| span_scan | ref_scalar | 110,330 | 0.479 | 9.2 |
| scrim_build | ref_scalar | 77,862 | 0.338 | 6.5 |
| overlay_blend | ref_scalar | 134,450 | 0.584 | 11.2 |
| halfres_expand | ref_scalar | 32,882 | 0.143 | 2.7 |

"ns/call" is one full-frame pass (all 480 rows for span_scan/overlay_blend/
halfres_expand; one whole 120x120-grid seven-pass rebuild for scrim_build).
"est. dev. cy/px" applies animbench's calibration (`host_ns x ~80 =~ device
ns` at 240 MHz, i.e. `cy/px = ns/px x 19.2`) -- this is a **pure-ALU** lower
bound; see below for why three of the four kernels blow through it by a wide
margin on the real device, and why that is not a mystery in two cases and an
open question in the third.

## Device-measured numbers (from the wider effort, not this harness)

Per-frame stage costs at the measured operating point: band(field)=20ms,
blend=16ms, expand=7.7ms, fill=3ms, copy=3ms, push=8ms. Per-publish (UI-task
side): snapshot=55-75ms, publish=40-50ms, of which the scrim rebuild alone is
**33-38ms** and the span scan is **8-9ms** (measured on the rig after the
publish sub-split landed, commit `5af8da05`; the scrim number is specifically
the cost `d999cc15`'s skip-guard now avoids paying on a same-shape recolor
publish -- the kernel itself, timed here, is what still runs on any publish
where coverage actually moved).

## Host-vs-device ratio: where the x80/x19.2 calibration holds and where it doesn't

| kernel | device measured | host (this harness) | ratio | pure-ALU calibration says |
|---|---|---|---|---|
| span_scan | ~8-9 ms/publish | 0.110 ms/call | **~77x** | ~80x -- matches |
| scrim_build | ~33-38 ms/rebuild | 0.078 ms/call | **~450x** | way over |
| overlay_blend | ~16 ms/frame | 0.134 ms/call | **~116x** | somewhat over |
| halfres_expand | ~7.7 ms/frame | 0.033 ms/call | **~235x** | way over |

**span_scan tracks the pure-ALU calibration almost exactly.** The per-pixel
access is a 3-byte stride read of a byte that's touched sequentially and
monotonically increasing, which is about as PSRAM-cache-friendly as a
sparse-access kernel gets. Nothing here says "rewrite the memory layout";
codegen is the lever (see below).

**scrim_build is the outlier, and not a small one.** `buildScrim_ref` at -O2
fully inlines all six `scrimTap3_ref` calls (447 instructions, 12 zero-
overhead LOOP instructions -- two per inlined pass, matching
`scrimTap3_ref`'s own report) and every pass is branch-free per element,
integer-only, no libm, no divides. **The compiler is not the bottleneck
here.** Three of the six passes (the "vertical" ones: `lineStep=1,
step=sw=120`) touch the 120x120 grid with a 120-byte stride instead of
sequentially. On a host x86 box a 14.4 KB working set sits entirely in L1 and
the stride costs nothing; on device, all four scrim buffers
(`scrimSrc`/`scrim`/`scrimTmp`/`scrimCmp`, ~14.4 KB each) are `ps_malloc`'d,
i.e. PSRAM. Whether the ESP32-S3's PSRAM cache (commonly 32-64 KB depending
on `sdkconfig`) is large enough to keep the whole grid resident across all
seven passes, or whether the stride pattern thrashes it, is **not
something a host harness can answer** -- this is the single most important
open question this baseline surfaces, and it's the one the scrim worker
should spend the most effort resolving, including by checking whether it's
worth cross-referencing the *real* `SleepAnimation.cpp` object's disassembly
(the isolated single-TU dump here could in principle inline differently than
the multi-thousand-line real TU, though nothing suggests that from this
comparison).

**halfres_expand exceeding the calibration is already explained in the
source.** The comment on the expansion loop in `SleepAnimation::renderFrame`
states directly that the expansion "is bounded by PSRAM write bandwidth to
`band[]` (~15 MB/s) and no arrangement of the loop moves it" -- i.e. this
kernel's device cost is not compute-bound at all, and no amount of ALU/SIMD
work on the *arithmetic* closes the gap. The one lever the source doesn't
rule out: whether a 128-bit PIE store (`ee.vst.128`, already used elsewhere
in this file for `scale565Oct`) writes a full-cache-line-sized chunk in a way
that the PSRAM controller can treat as a full overwrite rather than a
write-allocate read-modify-write -- the firmware's own past lesson
(documented at length in the two-rows-at-once revert this same function
records) was about *store ordering/locality*, not store *width*, so this is
a genuinely open, device-only-testable hypothesis, not a re-litigation of
the prior finding.

**overlay_blend sits between the two extremes.** `blendRow_ref` gets a
hardware loop (1 zero-overhead LOOP, per-pixel body: `mul16u x6` for the two
three-channel `blend565` multiplies, one branch for `a==0` skip, one for the
`a==255` fast path -- already close to what OPTIMIZE.md's guidance asks for).
`scrimRow_ref`, in contrast, gets **zero** hardware loops despite being
call-free (everything from `scrimCell_ref`/`scale565x2_ref` is `always_inline`)
-- the per-cell `continue` on `SCRIM_INV_NONE` breaks the fixed-trip-count
shape GCC needs for a zero-overhead LOOP. Elevated ratio vs. span_scan's is
consistent with the extra PSRAM reads this stage does (overlay colour rows,
scrim `invRow`) on top of reasonable ALU work, not a new mystery.

## Per-kernel codegen notes (from `./asm.sh all`, `xtensa-asm/*.S`)

- **span_scan** (`scanRow_ref`, 104 insns): **no hardware loop.** The
  per-pixel body branches on `*a != 0` and on `runStart < 0`, plus
  `emitRun_ref`'s own internal branch (gap-merge vs. new run) gets inlined in.
  11 unconditional jumps, ~9 conditional branches in the static body. This is
  squarely OPTIMIZE.md's "branch elimination in per-pixel paths" territory --
  but note the content is genuinely sparse (the firmware's own comment:
  ~14,200 candidate pixels/frame, ~5,300 with real coverage), so a
  whole-row-empty fast path (a cheap word-at-a-time zero test before the
  byte-at-a-time scan) is a plausible large win that a synthetic per-pixel
  branch-elimination pass alone would not capture. Flagged for the span_scan
  worker as a first-class idea, not just an afterthought.
- **scrim_build** (`scrimTap3_ref` 67 insns/2 loops; `buildScrim_ref` 447
  insns/12 loops, fully inlined): codegen is already good, per above. The
  worker's job is the memory-traffic/layout question, not the arithmetic.
- **overlay_blend**: `blendRow_ref` 71 insns/1 loop, `scrimRow_ref` 94
  insns/0 loops, `blendStage_ref` 16 insns (thin dispatcher, `noinline` to
  match how `renderFrame` -- itself far too large to inline into -- actually
  calls these two). Recovering a hardware loop in `scrimRow_ref` (e.g. by
  restructuring the `SCRIM_INV_NONE` skip so the trip count is fixed and the
  skip is a branchless identity multiply, the same trick the real
  `scrimRowPie` already uses for the *cells a gap-merge swallowed* case) is
  the concrete, evidence-backed target.
- **halfres_expand** (`expandRow_ref`, 28 insns/2 loops): codegen is
  already tight and hardware-looped for both the fill and copy passes. As
  above, no further win is expected from arithmetic changes; PIE store width
  is the one thing left to try, device-validation required.

## Golden/correctness note

Golden files are raw binary dumps (run tables, cell grids, RGB565 band
buffers), not images -- these kernels have no useful standalone visual.
`golden/*.bin` is marked `binary` in `.gitattributes`: without that, this
repo's pre-commit hook's trailing-whitespace fixup (`git diff-index --check`
+ `sed -i 's/[[:space:]]*$//'`) silently corrupted two of the four golden
files on every commit, because git's binary-file auto-detection didn't
reliably trip on this content. If a future binary artifact in this repo
looks "randomly" corrupted after committing, check `.gitattributes` first.

## Priority ranking for the optimization pass

1. **scrim_build** -- confirmed the single largest cost in the whole overlay
   pipeline (33-38 ms of a ~44 ms publish) and the kernel where host numbers
   are least informative about the real bottleneck. Opus.
2. **overlay_blend** -- 16 ms/frame, codegen has a concrete, identified gap
   (missing hardware loop in the dim pass). Fable.
3. **span_scan** -- 8-9 ms/publish, tracks the ALU calibration well, so
   gains here are more predictable from host numbers than the other three;
   the whole-row-empty fast path is the one idea likely to beat what a
   per-pixel rewrite alone would find. Fable.
4. **halfres_expand** -- 7.7 ms/frame, but the firmware's own comments
   already establish this is bandwidth-, not compute-, bound. Opus, with an
   explicit brief that the realistic ceiling here may be "no improvement
   possible without changing what gets written," and that finding is itself
   a valid, useful answer.
