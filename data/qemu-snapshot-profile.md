# QEMU snapshot-path profile: DefaultUI::snapshotAreaToOverlay

Offline attribution of the snapshot overlay's per-rect cost, done entirely in
Espressif's `qemu-system-xtensa` fork against the `display-qemu` env
(`GM_FAKE_CONTROLLER`). Companion to the on-device measurement (55-75ms per
refresh for ~15-20k px of clip area). All numbers below are QEMU-side; see
**QEMU-vs-device caveats** before using them to prioritize device work.

## Before any of this ran: display-qemu did not build

`src/display/drivers/Qemu/QemuPanel.h:52` declared
`uint16_t *directFrameBuffer() override` with no arguments. `Display.h`'s
base declares `directFrameBuffer(int index = 0)` (added by the "dump the
panel framebuffers over HTTP" work) -- the override no longer matches, so
every env that touches `QemuDriver.h` fails to compile ("marked 'override',
but does not override"). This blocks `display-qemu` unconditionally, for
anyone, not just this profiling run. Fixed locally (uncommitted, see
**Files touched** below):

```cpp
uint16_t *directFrameBuffer(int index = 0) override {
    (void)index;
    return framebuffer();
}
```

## Attribution table

Two substages were measured directly; two more are derived. "snap" = the
`clear` + `draw` region inside `snapshotAreaToOverlay`, timed by an
in-flight sibling change to `DefaultUI.cpp`/`LV_Helper.cpp` (uncommitted at
the time of this run -- see caveat below) that was already adding exactly
this split; this profiling run only had to flip `-DGM_TOUCH_PROBE=1` on for
`display-qemu` to read it. `redraw` = `lv_obj_redraw` (the tree-walk + all
drawing for one clip rect); `setpx` = the generic `set_px_cb` dispatch loop
(`fill_set_px`/`map_set_px` in `lv_draw_sw_blend.c`) that stage (c) runs
through.

| Stage | % of snapshot | Evidence |
|---|---|---|
| (d) alpha/color memset clear | ~7-9% | `GM_UISTAT clear=` field, both a 230400px cold-buffer pass (192-289us) and a 400px steady-state pass (6-7us) land in the same 7-9% band -- consistent across a ~500x change in rect area. |
| (a)+(b)+(c) `lv_obj_redraw` total ("draw") | ~75-81% | `GM_UISTAT draw=` field: 2068-2243us fastpath / 2444us baseline, against snap totals of 2810-3009us. ~10-17% of "snap" is unaccounted by clear+draw (see **Residual**, below). |
| (a) tree-walk / style lookups / event dispatch (`lv_obj_redraw` minus the leaf blend calls) | ~69-77% of "draw" (≈55-65% of snapshot) | Derived: draw-wall-time (from GM_UISTAT, ×2 passes) minus the leaf-function `setpx`/`fastpath` total from the same window, where zero additional redraws occurred in between (confirmed by `GM_UISTAT refreshes=0` on every intervening line). Baseline: 4888us − 1528us = 3360us (69%). Fast path: 4486us − 1042us = 3444us (77%). |
| (b)+(c) pixel-blend loop (`fill_set_px`/`map_set_px`, or the fast-path replacement) | ~23-31% of "draw" (≈17-24% of snapshot) | Same derivation, the other side of the subtraction: 1528us baseline / 1042us fast path, for an *identical* 13,840-pixel workload both times (see below -- this identity is the load-bearing check that makes the comparison apples-to-apples). |
| — (c) alone: time inside the `set_px_cb` callback body | ~38% of the (b)+(c) baseline total | `GM_PXBENCH` isolated microbench: calling `set_px_true_color_alpha` directly (no loop, no function-pointer dispatch), 200,000 iterations, opa fixed at 200 → 42-49ns/call. 13,840 calls × ~42ns ≈ 581us of the 1528us baseline setpx time. Caveat: the microbench always takes the partial-alpha branch (opa=200), never the opa==255 fast-exit real screen content also hits, so this is a rough, not exact, split. |
| — (b) alone: per-pixel loop/dispatch overhead around the callback | ~62% of the (b)+(c) baseline total | Baseline setpx total (1528us) minus the (c) estimate above: ~947us. This is pure per-pixel function-pointer-call and parameter-marshalling cost, doing no actual color work. |

**Residual** (~10-17% of "snap", not `clear` or `draw`): `refreshSleepOverlay`
calls `lv_obj_update_layout(scr)` before the timed region, and the debt-list
bookkeeping around it is untimed. Not attributed further here.

**Pixel-touch-count vs. clip area**: the 230400px figure DefaultUI logs as
"area" is the *clip rectangle's bounding box* (full screen on the first use
of each overlay buffer), not pixels actually painted. Real `set_px_cb` calls
for that same pass totalled 13,840 -- about 6% of the bounding box -- which
matches the brief's note that `action_on_meter_draw` is already
clip-prechecked: most of the 500x500 meter face is transparent background
that a tick/arc/label draw call never touches.

## Candidate #1 (RGB565+A8 fast path): measured effect

Implemented in the `display-qemu` vendored LVGL only (see integration diffs
below for where else it needs to go). Two builds, same tap sequence (a
synthetic touch driver replayed 0.08s-press/2.2s-gap taps at a fixed point,
since QEMU has no real input device), same resulting pixel-touch count both
times -- **that identity (13,840 == 13,840) is what makes this a clean
before/after**, not a different scene:

| | baseline (generic `set_px_cb`) | candidate #1 (RGB565+A8 fast path) | delta |
|---|---|---|---|
| pixel-blend substage total | 1528us | 1042us | **-31.8%** |
| ns / pixel through the loop | 110ns | 75ns | -32% |
| `lv_obj_redraw` draw-substage total (2 cold-buffer passes) | 4888us | 4486us | -8.2% |
| snapshot total (2 cold-buffer passes) | ~6018us (3009×2) | ~5663-5700us | ~-6-9% |

The fast path removes the per-pixel indirect call through
`disp->driver->set_px_cb(...)` (7 arguments marshalled per pixel on Xtensa's
register-window ABI) and short-circuits the common `opa == LV_OPA_COVER`
case (full-opacity tick marks, arc fills, solid label glyph pixels) straight
to a 3-byte store, skipping the background read and
`lv_color_mix_with_alpha` entirely for that case.

Given the attribution table above, **this is the smaller of the two
addressable buckets** -- it only touches (b)+(c), which is 23-31% of
"draw". Candidate #2 (reduce `lv_obj_redraw`'s own per-rect overhead, not
attempted here) addresses the larger bucket (69-77% of "draw") and is the
better next target; see **Recommended next step** below.

## PIE (SIMD) instructions in this QEMU: **yes, they execute correctly**

Test: poison a 16-byte aligned buffer with `0xAA`, then run
```
ee.zero.q q0
ee.vst.128.ip q0, <ptr>, 0
```
and read the buffer back. Assembled cleanly with
`xtensa-esp32s3-elf-gcc -mcpu=esp32s3` (real opcodes, not typos --
verified by disassembly before wiring into the firmware). Ran at boot, in
two independent QEMU boots: both times the buffer read back all-zero
(`GM_PIETEST: PASS-executed-correctly`). A poisoned buffer only reads back
zero if the PIE coprocessor's zero-register and 128-bit store instructions
both executed with correct semantics -- an unimplemented/stubbed opcode
would have left the `0xAA` poison in place, and a trap would have aborted
the boot before the confirming log line printed (it printed, consistently).
This only exercises one minimal instruction pair (register zero + store);
a kernel using PIE's multiply-accumulate/FFT-specific opcodes should still
get its own smoke test, but the coprocessor itself is modelled here, not
absent.

## QEMU-vs-device caveats (read before prioritizing device work from this)

- **Not cycle-accurate, no PSRAM model.** This QEMU is instruction-level
  emulation; it has no memory-latency or cache model, and does not
  reproduce the PSRAM stalls the project's own hardware notes identify as
  the dominant real-world cost (the device measures 55-75ms for a refresh
  QEMU does in ~2.8-3.0ms here -- a ~20-25x gap almost certainly dominated
  by real PSRAM latency and cache misses that this run cannot see).
- **Percentage splits likely transfer better than absolute times**, since
  they mostly reflect instruction-count ratios in the same code paths, not
  wall-clock. But even the splits have a directional bias: (b)+(c)+(d) all
  do scattered small reads/writes into the PSRAM-backed overlay buffer,
  while (a) is mostly small-struct field reads and function calls. If
  PSRAM latency inflates memory-touching work more than call/branch-heavy
  work, **the real-hardware share of (b)+(c)+(d) is probably larger than
  the ~40% this profile shows**, which would make candidate #1's real
  benefit *bigger* than the ~6-9% measured here, not smaller.
- The synthetic touch-tap driver (a raw socket writing `QemuTouch.cpp`'s
  `ESC 'T' x,y,d\n` protocol, since QEMU's ESP32-S3 model has no input
  device and the project's own `qemu-touch.py` expects a live Windows mouse
  position) is a scripted proxy for real interaction, not a real workload
  replay.
- Only one scene was profiled: the standby screen behind the sleep
  animation, at the moment its two overlay buffers first go valid
  (cold-buffer full-screen passes) and at one small icon/indicator update.
  `GM_FAKE_CONTROLLER` sends a single synthetic `SystemInfo` at boot and
  never updates it again (unlike `GM_SYNTH_HANDSHAKE`/`display-loadtest`,
  which ramps telemetry), so the three meters never animate under this env
  -- this profile does not cover a continuously-updating meter needle.
- Visual correctness of the fast path was **not** checked pixel-by-pixel
  against the unpatched output -- only that both paths touched the same
  13,840 pixels. Screenshot/pixel-diff before shipping to hardware.

## A measurement bug worth naming, so nobody repeats it

An early version of this profile wrapped `lv_obj_redraw` directly (in
`lv_refr.c`) to get a clean "whole tree-walk" total, expecting it to match
the application-level probe. It did not -- it ran ~2x high. Cause:
`lv_obj_redraw`'s own children loop calls `refr_obj()` per child, and
`refr_obj()` (for any child whose `layer_type == LV_LAYER_TYPE_NONE`, the
common case) calls `lv_obj_redraw()` again, once per child, recursively
down the tree. Wrapping the entry point that recurses into itself
double(multi)-counts every level of nesting, since each level's span
already contains all of its descendants' spans. The instrumented file still
carries this counter (`gm_redraw_us`) for magnitude/sanity purposes only --
**it is not used anywhere in the attribution table above**, which instead
derives "draw" from the already-correct, non-recursive application-level
probe (wraps only the single outermost call DefaultUI makes) minus the
leaf-level, non-recursive `setpx`/`fastpath` totals (`fill_set_px`/
`map_set_px` do not call back into `lv_obj_redraw`, so summing their
disjoint spans across one pass is trustworthy). Whoever attempts candidate
#2's per-object attribution next will want to either instrument `refr_obj`
directly with a reentrancy guard, or attribute only leaf time and derive
overhead by subtraction, as done here -- not wrap `lv_obj_redraw` itself.

## Integration diffs

Two files in the scratchpad (not committed, paths below since this is a
subagent report):

- `candidate1-fastpath-production.diff` -- the recommended fast-path change
  alone (no timers/counters/logging), described precisely against
  `lvgl/src/hal/lv_hal_disp.c` and `lvgl/src/draw/sw/lv_draw_sw_blend.c`.
  Apply the same hunks to **every** vendored LVGL copy that should carry it
  (`display`, `display-loadtest`, `display-qemu`). Given this project
  already re-patches vendored ESP-IDF/LVGL sources at build time
  (`scripts/patch_*.py`, e.g. `patch_lvgl_meter_inv`), the natural home for
  this is a new `scripts/patch_lvgl_fastpath_setpx.py` following that same
  pattern (keeps a pristine `.gm-orig`, re-applies across IDF/LVGL updates)
  rather than hand-editing each `.pio/libdeps/*/lvgl` copy.
- `qemu-profile-full-instrumentation.diff` -- everything this profiling run
  actually added to the `display-qemu` vendored LVGL copy, including the
  fast path above plus all the timers/counters/`GM_PXBENCH`/`GM_PIETEST`/
  periodic logging used to produce this report. QEMU-only debug scaffolding,
  not meant to ship; kept for provenance and in case someone wants to
  re-run or extend this measurement.

Both files: `/tmp/claude-1000/-mnt-c-work-gaggimate/07be1453-f4b0-4be8-9c5f-5664583e9577/scratchpad/candidate1-fastpath-production.diff`
and `.../qemu-profile-full-instrumentation.diff` in that same scratchpad
directory.

## Recommended next step

Attribution says the tree-walk/event-dispatch stage (a), not the pixel
loop, is the larger bucket (~69-77% of "draw", ~55-65% of the whole
snapshot). Candidate #2 (reduce `lv_obj_redraw`'s per-rect overhead) was not
attempted here -- time went to attribution + candidate #1 + the PIE
question. Concrete leads for whoever picks it up:

1. `lv_obj_redraw` sends three events per visited node
   (`DRAW_MAIN_BEGIN`/`MAIN`/`END`, then `POST_BEGIN`/`POST`/`POST_END`)
   even for nodes the clip check ultimately excludes from drawing, if
   `LV_OBJ_FLAG_OVERFLOW_VISIBLE` forces traversal; worth counting how many
   of the ~scr's descendants get visited vs. how many actually draw
   anything, per rect.
2. Fix the `refr_obj`/`lv_obj_redraw` double-counting (above) before trying
   to get a clean per-object-type or per-depth breakdown of the 69-77% --
   the current instrumentation can only report the aggregate, not where
   inside the walk it goes.

## Files touched (uncommitted, kept for reference)

Backups (`.bak`) sit beside each file. Only this report is committed
(`bench:` prefix), per the coordinator's instructions not to commit `src/`
or the vendored-LVGL sandbox changes.

- `src/display/drivers/Qemu/QemuPanel.h` -- the `directFrameBuffer` override
  signature fix (needed for `display-qemu` to build at all, unrelated to
  profiling; recommend committing this one for real, separately).
- `platformio.ini` -- `-DGM_TOUCH_PROBE=1` added to `env:display-qemu`, to
  read the sibling `DefaultUI.cpp` clear/draw/scan/scrim probe from this
  env. That sibling change was uncommitted, in-flight, and not authored by
  this profiling run; if it lands differently than measured here, the
  `clear`/`draw` split above should be re-derived.
- `.pio/libdeps/display-qemu/lvgl/src/hal/lv_hal_disp.c`,
  `.../draw/sw/lv_draw_sw_blend.c`, `.../core/lv_refr.c` -- all the
  instrumentation and the candidate #1 prototype described above.
