# bganim hyper-optimization brief (sleep17)

You are optimizing ONE background animation for the GaggiMate display firmware.
Target hardware: **ESP32-S3 (Xtensa LX7) @ 240 MHz**, 480x480 RGB565 panel,
render task budget ~**34 CPU cycles per pixel** total (230,400 px @ 30 fps).
Some animations miss this by 5-20x today — the user sees them as choppy/laggy.
Goal: every animation sustains 30 fps on device. Get band() under ~25
cycles/pixel and frame() under ~2 ms-equivalent so there's headroom for the
overlay blend and panel push that SleepAnimation.cpp adds on top.

## Device cost model (why the rules below exist)

| operation | approx cycles (S3) |
|---|---|
| int32 add/shift/mul | 1 |
| float add/mul/madd (HW FPU) | 1-2 |
| float compare/convert | 2-4 |
| **float divide** | ~30 (no fdiv instruction) |
| sqrtf | ~90 |
| sinf / cosf / exp2f | ~150 |
| expf / logf | ~200 |
| atan2f / tanhf | ~300 |
| powf | ~400 |
| **any double math** | 5-10x float (soft) |
| PSRAM read (cache miss) | ~40-80/line |

Consequences, in priority order:
1. **Zero libm calls per pixel.** One per-pixel sinf alone is 4x over budget.
2. **Zero float divides per pixel.** Precompute reciprocals per frame/row.
3. Per-pixel work should be integer/fixed-point (Q15/Q16.16) or LUT lookups.
   Float mul/add per pixel is acceptable if the count is small (~4-6).
4. Hoist everything row-constant out of the x loop (per-row tables built in
   frame() or at band-row start). Classic decompositions: f(x)+g(y) plasma
   terms, per-row scanline params for radial/polar effects.
5. Prefer incremental stepping (add a delta per pixel) over evaluating a
   closed form at every pixel.
6. LUTs: bganim::sin1024 (int16 ±512) and 256-entry theme ramps already
   exist — index them with integer phase accumulators. New LUTs are fine:
   allocate once in init() via bganim::alloc(), keep them small (SRAM is
   scarce; a few KB each, 64 KB absolute max — noiseTex256 already spends
   64 KB shared).
7. frame() runs once per frame — libm/float is fine THERE (that's where
   per-frame phases, reciprocals, palettes belong). Keep it under ~50 us
   host time as a proxy.
8. Never allocate per frame. Never call double-precision math (sin, cos,
   pow, fmod — the f-less versions).

## Hard constraints

- Edit ONLY your assigned `src/display/ui/default/bganim/Anim<X>.cpp`.
  Do NOT touch BgAnimCommon.*, BgAnim.h, the registry, other anims, or the
  harness. If a shared helper would obviously help several anims, note it in
  your report instead of adding it.
- Public contract unchanged: same registry entry (id, name, param defs), same
  init/frame/band signatures, same param semantics (0-100 knobs must keep
  their meaning and approximate visual effect, p[0] stays Speed via
  bganim::speedMul or an integer-equivalent of it).
- Theme reactivity preserved: poll bganim::themeGen() in frame() and rebuild
  palettes on change, exactly like the current code.
- Visual output must stay faithful to the current look. Structured field
  animations (plasma, silk, caustics, aurora, lava, mandala, nebula, ember)
  should match the golden frames closely. Particle systems (starfield,
  fireflies, steam, ripples, orbits) may drift in particle phase if you
  change math order — early-frame (f030) fidelity matters most; justify any
  later-frame drift in your report.
- Keep the file's comment style: explain the algorithm and the fixed-point
  scheme at the top; comment non-obvious constants.

## Workflow (evidence required)

```sh
cd /mnt/c/work/gaggimate/tools/animbench
make BIN=build/bench_<yourid>                 # monolithic build, ~5 s
./build/bench_<yourid> --anim <N> --frames 240 --compare golden
```

- `golden/` holds pre-optimization reference frames (f030/f120/f210). The
  compare line prints mean/max RGB888 diff; "OK" ≈ mean<=3.0 max<=48.
- The table prints host ms/frame (relative truth), libm calls/frame (device
  truth), and est_dev_ms (soft-float lower bound at 240 MHz).
- Baseline numbers for your animation are in BASELINE.md. You must beat them
  convincingly: libm/frame -> ~0 (band) and host band_ms down by the factor
  your baseline overshoots the budget.
- Iterate until the numbers hold, then run the full harness once
  (`./build/bench_<yourid>`) to confirm nothing else broke (statics are
  per-anim, but verify).
- Do NOT modify golden/, BASELINE.md, or bench sources.
- Do NOT commit. Leave your file modified; the leader reviews, builds the
  real firmware, and commits.

## Report back (final message, compact)

1. Baseline vs final: host frame/band ms, libm/frame, est_dev_ms.
2. Estimated device cycles/pixel after (band work only, from ops counting).
3. Techniques applied (one line each).
4. Golden diff results at f030/f120/f210 + justification if not OK.
5. Any shared-helper suggestions or risks (overflow corners, param edges you
   tested: p=0, p=100).
Test param extremes: run once with defaults; reason through 0/100 extremes
for overflow (Q16.16 ranges, LUT index wraps) — document, don't guess.

## ADDENDUM: real-CPU codegen analysis (xtensa-asm) — USE THIS

New tool, ground truth for what the ESP32-S3 executes — the exact device
compiler (xtensa-esp32s3 GCC 8.4, -O2 like the firmware):

```sh
cd /mnt/c/work/gaggimate/tools/animbench
./xtensa-asm.sh AnimYourfile      # writes xtensa-asm/AnimYourfile.S + report
```

The report shows per-function instruction counts, libcalls, and zero-overhead
loop usage. Read the .S for your band() inner loop before declaring victory —
x86 hides costs this reveals. Empirical facts from this toolchain:

- **float division compiles to a __divsf3 LIBCALL** (~30-50 cy). Never divide
  in a loop — multiply by a precomputed reciprocal.
- **fmaxf/fminf are LIBCALLS**, not inlined. Use ternaries/compares.
- sqrtf/sinf/cosf/expf/fmodf are libcalls as expected (no FPU sqrt).
- bganim::fastCosRad/fastSinRad call cosTableF() (call8 + load) each use —
  hoist `const float *ct = bganim::cosTableF();` out of loops and index it
  directly, or better, avoid float trig in loops entirely.
- GCC emits Xtensa zero-overhead LOOP instructions — but only when the loop
  body is CALL-FREE. One call in the loop kills it. Check your .S.
- Only 16 FP registers: heavy float expressions spill; prefer int.

Demo-scene tricks that map well to this chip (encouraged where they fit):
- Write pixel PAIRS as one 32-bit store (build `(c1<<16)|c0`, s32i) — the
  band buffer is 4-byte aligned and w=480 is even. Halves store traffic.
- Second-order differences: evaluate quadratics (x^2 terms, radial-ish
  fields) incrementally with two adds per pixel instead of any multiply.
- Bresenham-style error accumulators replace divides/mods in steppers.
- Span/tile skipping: if a row segment maps to a constant or linear field,
  memset/lerp the span instead of per-pixel evaluation.
- Coarse-grid + integer bilinear upsample (2x2 or 4x4 cells) for smooth
  fields: per-cell DDA in Q8, imperceptible at 480x480 — verify vs golden.
- Fold brightness/param scaling into the palette/ramp ONCE per frame, never
  per pixel.
- Keep per-pixel state in int registers; Q16.16/Q8.8 with mull (2 cy) beats
  float pipelines that spill.

## When the host bench and the device disagree, the device wins

Several passes have now found changes that are FASTER on the x86 bench and
SLOWER on the real ESP32-S3. This is structural, not noise: x86 is wide,
out-of-order, and has many more registers, so it hides exactly the costs that
dominate the in-order LX7. Known instances, all verified in real assembly:

- **Manual unrolling with more live values** (silk, 4x unroll): host 0.337 ->
  0.291, device 158 -> 430 instructions. The windowed ABI leaves only ~13-14
  usable registers; the unroll spilled almost everything and lost the hardware
  loop. Reverted.
- **Named locals instead of a pointer array** (aurora): faster on host, device
  883 -> 949 instructions and one of two hardware loops lost. Reverted.
- **Packing two uint8 LUTs into one uint16 LUT** (mandala): host ~9% slower,
  device dropped a base pointer out of a starved register file and halved the
  per-pixel spills. KEPT — the host number is the misleading one here.
- **Counted-down loops** (lava): marginally slower on host, but a trip count
  known at entry is what lets GCC emit the Xtensa zero-overhead LOOP and drop
  the per-iteration branch. KEPT.

So: use the host bench to find WHERE the time goes and to gate fidelity, and
use ./xtensa-asm.sh to decide register-pressure and loop-shape questions. If a
change makes host worse but provably reduces device instructions/spills or
restores a hardware loop, keep it and say so in your report — and leave a
comment in the source, because the next pass optimizing blindly against the
host number will otherwise revert it.

Two techniques worth reusing, both from this class of finding:
- Pack two same-index uint8 tables into one uint16 table to free a base
  pointer register.
- Template-parameterize a compile-time-constant loop stride instead of passing
  it as a runtime int, so it stops being a spilled live value.
