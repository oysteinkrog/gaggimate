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

# Candidate #2: attributing and attacking the `lv_obj_redraw` tree-walk

Same env, same sandbox rules. Picks up from "Recommended next step" above:
sub-attribute the ~69-77%-of-draw tree-walk into style resolution, event
dispatch, descriptor init, and geometry/clip math, measure rect-count
sensitivity (`GM_DIRTY_RECT_CAP` 8/4/2/1), and prototype the best structural
fix. Counters added: `lv_event_send` (depth-guarded top-level total, plus a
total-including-nested count), `lv_obj_get_style_prop` (same pattern),
`lv_obj_init_draw_{rect,label,img,line,arc}_dsc` (rename-and-wrap, all five),
`_lv_area_intersect` (count-only, paired with a microbench for a ns/call
estimate). All gated on a shared `gm_in_snapshot_refresh()` check
(`lv_refr.c`) so nothing here ever counts LVGL's normal on-screen refresh of
the real panel, only `DefaultUI`'s offscreen snapshot pass.

## A merge conflict, fixed before any of this could run

Between Phase 1 and this pass, `scripts/patch_lvgl_setpx_fast.py` landed
(candidate #1's fast path, made official) and tried to patch files this
profiling run had already hand-modified. It applied a working-but-shadowed
patch to `lv_hal_disp.c` (would have collided with this run's own
`gm_disp_uses_generic_true_color_alpha`) and hard-failed on
`lv_draw_sw_blend.c` ("anchor found 0 times"), aborting the build -- and, on
that first invocation, wrote `.gm-orig` backups that captured this run's
modified content as the assumed-pristine baseline, not real pristine LVGL.
Fixed by restoring both files from the genuinely-pristine `.bak` saved in
Phase 1, letting the official patch reapply cleanly (confirmed via a clean
build log, no anchor errors), then re-layering this run's own
instrumentation back on top -- careful this time not to redefine
`gm_disp_uses_generic_true_color_alpha` (now owned by the official patch)
and to time the official `gm_fill_set_px_rgb565a8`/`gm_map_set_px_rgb565a8`
functions by name instead of this run's old candidate #1 prototype names.
No further action needed; noted here only so the next person who edits
these files in parallel with a `patch_*.py` script knows the failure mode.

## PIE re-verification: Phase 1's "confirmed twice" had a live alignment bug

Re-running the exact Phase 1 poison-then-check test after the file restore
above, `GM_PIETEST` came back **FAIL** on the first boot of this pass --
contradicting Phase 1's "PASS, two independent boots." The test's poison
buffers (`uint8_t src[16]; uint8_t dst[16];`) were plain stack arrays with no
alignment attribute, and `ee.vst.128.ip` requires a 16-byte-aligned address;
Phase 1's two PASS results were apparently stack-alignment luck, not a
verified property of the instruction. Adding
`__attribute__((aligned(16)))` to both buffers made the test deterministic:
**PASS on every subsequent boot in this pass** (5 independent QEMU boots,
covering the cap=8/4/2/1 sweep below plus the initial re-verification boot).
The verdict itself is unchanged and now stronger -- `ee.zero.q`/
`ee.vst.128.ip` do execute with correct semantics in this QEMU -- but Phase
1's methodology for reaching it was unsound. Anyone writing a similar
poison-then-check test for other PIE opcodes should align the buffer
explicitly rather than rely on the toolchain's default stack layout.

## Sub-attribution of the walk

Eight samples (two per boot, four independent rebuild+boot cycles -- see the
rect-cap sweep below, which held the workload fixed and only varied
`GM_DIRTY_RECT_CAP`) all touched the *exact same content*: 6,920 pixels
through the fast path, 24 top-level event-dispatch calls (40 including
nested), 134 style-prop lookups, 7 draw-descriptor inits, 144
`_lv_area_intersect` calls. That fixed fingerprint, replayed identically
across four rebuilds, is what makes averaging across them a fair way to
smooth QEMU's run-to-run timing noise (see below) without averaging across
different scenes.

| Sub-stage | Mean time (8 samples) | Range | % of walk (event+style+dscinit+area) |
|---|---|---|---|
| Event dispatch (`lv_event_send`, top-level, depth-guarded) | 3671us | 3323-4351us | **78.7%** |
| Style resolution (`lv_obj_get_style_prop`, top-level) | 525us | 409-639us | 11.3% |
| Draw-descriptor init (`lv_obj_init_draw_*_dsc`, all 5) | 453us | 335-583us | 9.7% |
| Geometry/clip math (`_lv_area_intersect`, count x ns/call) | 12us | 12-13us | 0.3% |
| **Walk total (sum of the above)** | **4661us** | | **100%** |
| Pixel-blend (candidate #1 fast path, for comparison) | 363us | 290-462us | -- |
| **Walk share of walk+blend** | | | **92.8%** (blend: 7.2%) |

This tightens Phase 1's coarser split (draw = 69-77% walk / 23-31% blend,
derived by subtracting leaf totals from a sibling wall-clock probe) into a
walk share that's directly leaf-timed rather than derived by subtraction --
92.8% walk / 7.2% blend for the components this pass could measure
directly. The two methods aren't computing quite the same thing (Phase 1's
"draw" bucket included some untimed residual this pass's leaf timers don't
cover either), so treat 92.8% as a tighter lower bound on the walk's true
share, not a contradiction of the 69-77% figure.

The naive recursive `lv_obj_redraw` wrap (`gm_redraw_us`, kept from Phase 1
for magnitude sanity only -- see "a measurement bug worth naming" -- never
used for a real split) averaged 7405us across the same 8 samples, 1.59x the
4661us clean walk total. That ratio is a direct, now-quantified measure of
how much the `refr_obj`/`lv_obj_redraw` mutual recursion inflates a naive
wrap: real, but nowhere near the naive 2x someone might assume from "it
recurses into itself once."

### Why event dispatch dominates: it isn't wasted dispatch overhead

`lv_event_send` at 78.7% of the walk sounds like an obvious target for
"skip dispatch when nobody's listening" -- but in LVGL 8's architecture,
sending `LV_EVENT_DRAW_MAIN`/`_BEGIN`/`_END` **is** the mechanism by which a
widget's own class draws itself: `lv_label_event`, `lv_img_event`, and
friends respond to those events by calling `lv_draw_label`/`lv_draw_img`/
etc. from inside the class's own event callback. The `gm_event_us` timer
here wraps the *entire* `lv_event_send` call, so it necessarily includes
whatever the callback does -- for this scene's 7 touched objects, that
includes the three meters' `action_on_meter_draw` custom tick-drawing
handler (`src/display/ui/default/eez/actions.cpp:106`), which does real
per-tick geometry and line/dot drawing work inside a `DRAW_PART`-style
callback. Reading `lv_event_send` and `lv_refr.c`'s `lv_obj_redraw` directly
rather than guessing from the timer name: there is no "dispatch with no
hooks" waste to cut here for the base widget types, because dispatch *is*
draw for them.

### Candidate ideas already implemented in the code as read

Two of the four mitigation ideas turn out to already be in place, found by
reading rather than assuming:

- **"Skip DRAW event dispatch for objects with no registered draw hooks"**
  -- not applicable as stated (see above): for standard widgets the
  dispatch *performs* the draw. There's no separable "hook" to skip without
  skipping the drawing itself.
- **"Early-out for objects whose clipped area is empty before style
  resolution happens"** -- already implemented, at two levels:
  - `lv_obj_redraw` itself (`lv_refr.c:160-169` in this pass's instrumented
    sandbox copy -- stock LVGL 8.4 logic, unmodified by this project, just
    shifted a few lines down by this pass's own timer wrap) computes
    `_lv_area_intersect` against the clip *before* sending any `DRAW_MAIN*`
    event, and gates all three draw events (and the post-draw events)
    behind `should_draw = com_clip_res || LV_OBJ_FLAG_OVERFLOW_VISIBLE`. An
    object entirely outside the clip never reaches style resolution or
    descriptor init for its own draw.
  - `action_on_meter_draw` (`actions.cpp:151-181`) does its own per-tick
    early-out *before* touching color/style, with an explicit comment
    calling this out: "Geometry before colour, so ticks outside the clip
    can be rejected early." This is finer-grained than the object-level
    check above (per-tick, not per-object) and is why only 6,920 of a
    500x500-meter's ~250,000px face actually reach a `set_px_cb` call in
    this scene.

  The children-pruning path (`lv_refr.c:187-196`, same file) has one gap: for a
  container with `LV_OBJ_FLAG_OVERFLOW_VISIBLE` set, `clip_coords_for_children`
  is assigned the unclipped parent clip directly, with no bounding check at
  all -- every child still gets its own (cheap, ~88ns) `_lv_area_intersect`
  check when its own turn comes, so this doesn't look like a real gap, just
  a deferred one; not chased further, no `OVERFLOW_VISIBLE` container was
  observed in this scene's touched-object set.

  Net: neither idea has a QEMU prototype in this pass, because both are
  already shipped in the code being profiled.

## Rect-count sensitivity: `GM_DIRTY_RECT_CAP` 8 / 4 / 2 / 1

Swept all four requested values (current production value is 4, landed
mid-session at `197f97c9`/`5d71fa99` -- see below). Each value: edit
`GM_DIRTY_RECT_CAP` in `LV_Helper.h` and `OVERLAY_DIRTY_RECTS` in
`DefaultUI.h` together (the `static_assert` at `DefaultUI.cpp:953` requires
them equal), full rebuild, fresh QEMU boot, same scripted tap sequence.

**Result: identical sub-attribution at every cap value tested**, both
occurrences of the 6,920px sample, every boot -- fastpath_calls=6920,
event_top=24/event_total=40, style_top=134, dscinit_calls=7, area_calls=144,
matching to the call for cap=8, 4, 2, and 1. Timings varied by the same
~15-25% run-to-run noise seen elsewhere in this pass (see caveats), but the
*content* touched by the redraw never changed.

Root cause, not a bug: `GM_FAKE_CONTROLLER` sends one synthetic `SystemInfo`
at boot and never updates it again (see Phase 1's caveats). With only one
value source ticking, this environment never produces more than one
disjoint dirty region per refresh -- so the union/fold algorithm that
`GM_DIRTY_RECT_CAP` bounds never has more regions than headroom, at any cap
from 1 to 8. **This is a real limitation of this bench for this specific
question, not a finding that the cap doesn't matter.** The already-landed
production decision (cap=4, `197f97c9`, reasoning recorded in `5d71fa99`)
must have come from richer traffic than this env can generate -- the rig,
or `display-loadtest`'s `GM_SYNTH_HANDSHAKE` continuous telemetry ramp,
either of which can move more than one widget in the same tick. If a QEMU-side
validation of the cap curve is wanted later, `GM_FAKE_CONTROLLER` would need
a mode that updates 2+ independent widgets concurrently -- this pass did
not attempt that, since it would mean changing `Controller.cpp`'s synth
path, outside the vendored-LVGL sandbox this task was scoped to.

## What landed elsewhere mid-session: candidate #3 is already shipped

Partway through this pass, `idf5` picked up commits `fcb29ace`/`c3ddbde1`
("invalidate a sector, not the screen, when a meter value moves" /
"document the pitch-clamp assumption") -- a `scripts/patch_lvgl_meter_inv.py`
patch to the vendored `lv_meter.c`, replacing the stock `else: lv_obj_invalidate(obj)`
catch-all (full-meter invalidation) with a narrow tick-sector box for
`LV_METER_INDICATOR_TYPE_SCALE_LINES` value changes. This is a different
mechanism than "hoist the per-rect walk cost" (the team lead's framing),
but the same effect on the metric that matters: it shrinks *how much area
gets invalidated in the first place* when a meter's tick colors update,
rather than caching/skipping the walk after the fact. Checked against this
scene's actual indicator types (`screens.c:5913-5979`): every meter here
uses `lv_meter_add_needle_img` (already routed through a narrower
`inv_line` path in stock LVGL, not the full-invalidate catch-all) plus
`lv_meter_add_scale_lines` (the one that *was* hitting the catch-all before
this patch, now fixed). No `lv_meter_add_arc` indicators are used by this
UI, so "background arc" in the original framing maps to the tick-ring
(scale-lines), not a separate arc indicator.

This pass's sub-attribution independently corroborates the fix was aimed
at the right thing without having been informed by it: event dispatch
(78.7% of the walk, tied to the meters' custom tick-draw handler and
LVGL's own needle-image draw) is the dominant cost here, and shrinking the
invalidated area is exactly what reduces how much of that dispatch work
happens per value change. No further prototype attempted for this
candidate; it's shipped.

## Candidate #1's idea (per-refresh style/descriptor cache): sized, not prototyped

Style resolution + descriptor init together are 21.0% of the walk in this
scene (11.3% + 9.7%). A perfect per-refresh cache -- reusing an unchanged
object's resolved style/descriptor across the (now capped at 4, and per the
finding above, typically just 1 in this env's actual traffic) per-refresh
rect passes -- would save at most that 21%, and the rect-cap sweep above
shows this env's synthetic traffic essentially never exercises more than
one rect pass per refresh, so a cross-rect cache would have close to zero
real hit rate to measure here. Beyond the sizing, this pass did not
prototype it: a cache has to be invalidated correctly on every real style
change (`lv_obj_set_style_*`, state transitions, theme changes) and scoped
correctly to "this refresh" without leaking into the next one -- getting
that wrong produces *visually wrong* output (stale colors/sizes), a worse
failure mode than the performance regression it would be trying to fix, and
verifying that correctness needs either a scene with real per-object style
churn (which this env, per the same `GM_FAKE_CONTROLLER` limitation above,
doesn't generate) or reading through every style-mutation call site by hand.
Neither was done in the time available for this pass. Documenting the size
of the opportunity (up to ~21% of walk time, contingent on real cross-rect
reuse existing on the rig, which this bench can't confirm) rather than
shipping an unverified cache.

## QEMU-vs-device caveats (addendum to Phase 1's list)

- **PSRAM pointer-chase, again, more sharply.** `lv_conf.h`'s
  `LV_MEM_CUSTOM_ALLOC = ps_malloc` means every `lv_obj_t`, style list, and
  `_lv_ll_get_head`/`_get_next` linked-list hop this walk performs (scale
  list, indicator list, child list, event-descriptor list) is a real PSRAM
  access on device. This pass's 92.8%/7.2% walk/blend split is flat-memory;
  the device split is almost certainly more walk-dominant than that, not
  less, since the walk is the pointer-chase-heavy side and the blend is the
  sequential-buffer-write side.
- **Run-to-run noise, quantified this time**: identical-workload samples
  (same call counts, different boots or different points in the same boot)
  varied 15-25% in absolute time (e.g. fastpath_us: 290-462us for the same
  6,920-pixel push). Percentage splits stayed stable to within ~1
  percentage point across all 8 samples, which is why this section reports
  percentages from an 8-sample mean rather than trusting any single sample.
  Plausible cause not chased down: QEMU's TCG dynamic translation compiles
  a code path on first execution and runs from the cached translation
  after, so a "cold" first occurrence of a given call pattern in a boot
  measured consistently slower than a "warm" second occurrence of the same
  pattern later in the same boot.
- The rect-cap sweep's negative result (above) is itself a caveat: this
  bench cannot validate `GM_DIRTY_RECT_CAP` choices, full stop, until
  `GM_FAKE_CONTROLLER` can move more than one widget per tick.

## Files touched (uncommitted, kept for reference; this pass)

- `.pio/libdeps/display-qemu/lvgl/src/hal/lv_hal_disp.c` -- re-layered
  Phase 1's counters/PXBENCH/PIETEST (now with the alignment fix) on top of
  the official `scripts/patch_lvgl_setpx_fast.py` patch, plus `gm_redraw_us`
  and the Phase 2 counter externs/log line. `.bak` (pristine) and
  `.gm-orig` (official-patch-only, this pass's recovery snapshot) both kept.
- `.pio/libdeps/display-qemu/lvgl/src/draw/sw/lv_draw_sw_blend.c` -- timing
  wraps around `fill_set_px`/`map_set_px` (generic path) and the official
  `gm_fill_set_px_rgb565a8`/`gm_map_set_px_rgb565a8` (fast path). Same `.bak`
  / `.gm-orig` pair kept.
- `.pio/libdeps/display-qemu/lvgl/src/core/lv_refr.c` -- `gm_in_snapshot_refresh()`
  gate (shared by every counter below) plus the existing `gm_redraw_us`
  wrap from Phase 1.
- `.pio/libdeps/display-qemu/lvgl/src/core/lv_event.c` -- depth-guarded
  `lv_event_send` timing (`gm_event_us`/`gm_event_toplevel_calls`/
  `gm_event_calls_total`).
- `.pio/libdeps/display-qemu/lvgl/src/core/lv_obj_style.c` -- depth-guarded
  `lv_obj_get_style_prop` timing (`gm_style_us`/`gm_style_toplevel_calls`/
  `gm_style_calls_total`).
- `.pio/libdeps/display-qemu/lvgl/src/core/lv_obj_draw.c` -- all five
  `lv_obj_init_draw_*_dsc` renamed to `gm_orig_*` plus thin timed wrappers
  (`gm_dscinit_us`/`gm_dscinit_calls`).
- `.pio/libdeps/display-qemu/lvgl/src/misc/lv_area.c` -- `_lv_area_intersect`
  call count only (`gm_area_intersect_calls`), paired with the `GM_PXBENCH2`
  microbench in `lv_hal_disp.c` for a ns/call estimate.
- `src/display/drivers/common/LV_Helper.h` and
  `src/display/ui/default/DefaultUI.h` -- `GM_DIRTY_RECT_CAP` /
  `OVERLAY_DIRTY_RECTS` swept to 8, 2, and 1 in turn for the sweep above,
  then **restored to the committed value (4)** before finishing; `git
  status`/`git diff` on both files is clean.

Combined instrumentation diff (this pass's additions only, against the
`.bak` pristine files, all seven vendored-LVGL files above):
`/tmp/claude-1000/-mnt-c-work-gaggimate/07be1453-f4b0-4be8-9c5f-5664583e9577/scratchpad/candidate2-subattribution-instrumentation.diff`.
No new production-integration diff is proposed here: the two structural
mitigations this pass's own data points at most strongly (shrink meter
invalidation, reduce the rect cap) were both already shipped mid-session by
a different route (`fcb29ace`/`c3ddbde1`, `197f97c9`/`5d71fa99`) before this
pass's measurements were in hand to inform them; the one remaining idea
(style/descriptor cache) is sized above but not prototyped, for the
correctness reasons given.

## Addendum: rig updates folded in, and the per-refresh descriptor cache prototype

Two updates arrived from the team lead after the section above was written,
folded in here rather than rewritten above (git history has the original):
candidate #1 is confirmed shipped as `scripts/patch_lvgl_setpx_fast.py`
(`2b477bfb`) -- no integration diff needed from this pass, consistent with
what this report already said. The rect-count question is closed on-device:
cap=2 measured on the rig at ~103k px/refresh redrawn (from ~15k at cap=4)
and ~90ms snapshot time (from ~55ms) -- cap=4 is the floor. This matches
this pass's own QEMU finding that the fold algorithm has no headroom to
usefully exercise below cap=4 in the traffic this bench can generate, and
explains *why* on the rig specifically: real refreshes carry 3-4 disjoint
widget regions, so a cap of 2 forces the least-growth merge to union across
much of the screen. **Drop the rect-count question entirely, per the team
lead -- nothing further attempted on it here.**

### Prototype: a per-refresh cache for `lv_obj_init_draw_*_dsc`

Candidate #1 from the team lead's mitigation list (the per-refresh
style/descriptor cache), sized earlier in this section at up to ~21% of
the walk (style 11.3% + descriptor init 9.7%, and descriptor
init calls into style resolution internally so the two overlap -- a full
cache hit saves both). Implemented as a generation-scoped cache: one slot
per distinct `(object pointer, part, descriptor kind)` seen during a
refresh, stamped with a counter (`gm_refresh_generation`) that increments
once per call to `refreshSleepOverlay()`. Confirmed by reading that
function directly: its per-rect loop (`for (int i = 0; i < clipN; i++)`,
`DefaultUI.cpp`) calls `snapshotAreaToOverlay()` back-to-back with no
`lv_timer_handler()` or style-transition tick in between, so a descriptor
resolved on rect 0 cannot have gone stale by rect 1..clipN-1 of the *same*
call -- and a generation mismatch on the *next* call makes every entry an
automatic miss, so nothing needs explicit invalidation when real style state
changes on the next refresh.

**A collision, caught before it produced a wrong number.** Partway through
prototyping this, another concurrent edit to `DefaultUI.cpp` (a `GM_LVMEM`
heap-stats addition, not authored by this pass) silently overwrote this
pass's one-line `gm_refresh_generation++` hook -- the file changed out from
under a local, uncommitted edit in the shared checkout. The first symptom
was a follow-up measurement (an artificial 4x-repeat harness meant to proxy
the rig's multi-rect-per-refresh traffic in QEMU) producing an impossible
result: 100% cache hits on the *second* `refreshSleepOverlay()` call,
which only makes sense if the generation counter had frozen at 0 -- i.e.
exactly what happens when the increment silently stops running. Caught by
checking `git diff` on the file before trusting the number, not by the
number looking suspicious on its own; the number alone would have read as
"great, the cache works even better than expected." Flagged to the team
lead, re-applied the one-line hook on top of the current file, verified it
survived the next build, and **did not re-attempt the 4x-repeat harness**
given a second collision was one more concurrent write away -- see below
for how that gap is closed analytically instead. The hook also had a real
bug of its own, independent of the collision: as first written it was
unconditional, and since `DefaultUI.cpp` is shared by every env, an
unconditional `extern volatile uint32_t gm_refresh_generation;` would
link-fail `display` and `display-loadtest` the moment the cache patch
script ships to `display-qemu` only, since no other env's vendored LVGL
would define that symbol. Someone (likely the team lead, reacting to the
collision flag) caught this too and wrapped it in `#ifdef GM_FAKE_CONTROLLER`
before this pass got back to it -- adopted as-is, it's correct, and it's
reflected in the production diff below.

**QEMU-measured effect** (4 samples, 2 independent boots, both showing the
generation-scoping working correctly -- consistently `dsc_cache_hits=1,
dsc_cache_misses=6` per refresh, never drifting to full-hit or full-miss):

| | baseline (no cache) | with cache | delta |
|---|---|---|---|
| descriptor-init substage | 453us | 425us | -6.2% |
| style-resolution substage | 525us | 426us | -18.9% |
| combined (style+dscinit) | 978us | 851us | **-13.0%** |
| style calls (`style_top`) | 134 | 129 | -3.7% (call count) |

The style-time drop (18.9%) is disproportionate to the style-*call-count*
drop (3.7%) -- the one cached object's internal style resolution costs
above the group's per-call average, or this is noise within this pass's
established ~15-25% run-to-run QEMU variance; four samples isn't enough to
tell those apart, so treat the 18.9% figure as directional, not precise.

**This measured number is deliberately not the number that matters.** The
hit rate here (1 of 7 descriptor calls, ~14%) is *incidental* -- one object
happened to get queried twice within a single dirty-rect pass -- not the
cross-rect reuse this cache is actually for, because `GM_FAKE_CONTROLLER`'s
scene structurally never produces more than one dirty rect per refresh (the
same limitation as the rect-count sweep above). The cache cannot show its
real effect in this bench.

**Analytical projection for the rig's actual regime** (3-4 disjoint widget
regions per refresh, cap=4, per the team lead): if `k` of a refresh's
touched objects overlap all `R` rects (the three ~500x500 meters are the
obvious candidates -- plausible but *not verified*, this pass did not
instrument which specific objects those 7 dscinit calls belong to) and the
rest are single-rect-only, the cache turns `k*R` redundant resolutions into
`k`, saving `k*(R-1)` of `k*R + (7-k)` total calls for this scene's 7-object
mix. For `k=3`: `R=4` saves 9 of 16 (56%), `R=3` saves 6 of 13 (46%). This
is a **projection, not a measurement** -- it assumes which objects overlap
multiple rects, which this bench cannot confirm. **Validate on the rig**
against `gm_dscinit_cache_hits`/`gm_dscinit_cache_misses` (wire the same
pair into whichever build gets this patch) before trusting either number;
if the real hit rate comes back well under the projection, the objects
doing the overlapping aren't what this projection assumed.

**Memory cost**: 48 slots x 72 bytes = 3,456 bytes static, measured via a
boot-time `sizeof` log (`GM_DSCCACHE_SIZE`) rather than computed by hand
(the payload union includes `lv_draw_rect_dsc_t`'s gradient descriptor
member, not a trivial size to eyeball correctly). Slot count (48) is a
guess sized above the 7-30 distinct objects this pass's samples touched per
rect-pass, times up to 4 rects/refresh; recount against the real screens'
worst case before shipping.

**Correctness caveat, stated plainly**: safe only if `refreshSleepOverlay()`
never runs concurrently across two threads/cores (no locking added) and its
per-rect loop never yields to a timer/style-transition tick between
iterations -- both true in the code as read here, but the loop this cache
depends on is the *exact* function that already got edited out from under
this pass once during this session; re-verify against whatever it looks
like when this actually ships, and pixel-diff a real screen before trusting
it, same discipline as candidate #1's fast path.

Production diff: `candidate2-dsccache-production.diff` (scratchpad, same
directory as `candidate1-fastpath-production.diff`). Full instrumented
sandbox diff (this pass's counters/timers plus the cache):
`candidate2-subattribution-instrumentation.diff`. The one `src/` line this
prototype needed (`DefaultUI.cpp`'s `gm_refresh_generation++`, now correctly
`#ifdef GM_FAKE_CONTROLLER`-guarded) is isolated in
`defaultui-gm-refresh-generation-hook.diff`, left **uncommitted** in the
working tree per the sandbox rules -- `git status`/`git diff` on it is not
clean at the time of this commit (that one line is the only content); the
team lead already has the diff and can drop it or fold it into the real
patch script.

## Candidate #2 epilogue: device A/B says no (added by the team lead)

The descriptor cache went onto the rig as a vendored patch
(rename-and-wrap, canonical-input bypass, PSRAM-resident 48-slot table,
generation bumped in _lv_disp_refr_timer and refreshSleepOverlay). Measured
hit rate: 71% -- above the 46-56% multi-rect projection. Measured effect,
within-run A/B (log 33, alternating 60 s bypass phases, 31-32 windows per
arm, same boot): draw/area 3.87 us/kpx with the cache vs 3.71 us/kpx
without. The cache makes the draw ~4% SLOWER. The saved style cascade and
the added slot-table scan + ~150 B payload copy are both PSRAM traffic, and
the second costs more than the first at this scene's descriptor mix.

Reverted (patch script retired to the session scratchpad, vendored files
restored, hooks removed). What survives: the attribution method, the 71%
hit-rate datum (the reuse is real, a cheaper memo could still win), and the
projection-vs-device lesson -- QEMU's instruction-count shares do not rank
PSRAM-bound work.

## Alloc-site histogram and a redraw-census correction

Two follow-ups from the rig's heap accounting (2e047075: ~800 transient
LVGL alloc/free per second at a flat 65 kB / 1600-block live set) and from
candidate #1's original lead (does most of the tree walk land on objects
that get gated out, or objects that go on to draw). Both instrumented in
`display-qemu`'s vendored LVGL only (`lv_mem.c`, `lv_refr.c`,
`lv_hal_disp.c`, `lv_timer.c` -- all `.bak`-backed, none committed), boot
captured over a continuous 60 s run with the synthetic touch-tap driver.

**A methodology fix worth stating up front**: the first attempt at both
instruments hooked their periodic dump into `lv_disp_drv_init()`, which
only fires once per overlay-refresh dirty rect. In a 75 s first run that
produced exactly one dump window, because this scene's `GM_FAKE_CONTROLLER`
traffic only drove one refresh -- while the existing `GM_LVMEM` counter
kept climbing the entire time regardless. That first histogram would have
answered "who allocates during one overlay refresh", not "who's behind the
steady churn", which is the wrong question. Fixed by hooking the dump into
`lv_timer_handler()` instead, which runs on every pass (~130-700 per 5 s
here) independent of refresh activity. Flagging this because the first
run's numbers were never reported -- they'd have been actively misleading
if they had been.

### Alloc-site histogram: who's behind the churn

`lv_mem_alloc`/`lv_mem_free`/`lv_mem_realloc` (`lv_mem.c`) are the single
choke point every LVGL allocation passes through, on-device and here alike
(`LV_MEM_CUSTOM_ALLOC/FREE/REALLOC` in `lv_conf.h` map straight to them).
Hooked `__builtin_return_address(0)` at each entry into a 40-slot table
keyed by call-site PC (alloc count/bytes, free count), dumped sorted by
bytes on a 3 s gate. Aggregated across the full 60 s run (432 dump lines,
52 distinct call sites captured, 267 `overflow_events` against the
40-slot cap -- the table is undersized for this workload's full site
diversity, so the tail is incomplete, but the top sites accumulate
correctly regardless and that's what answers the question) and resolved
against `firmware.elf` with `addr2line`:

| call site | alloc_count | alloc_bytes | avg bytes/call |
|---|---:|---:|---:|
| `eez::ArrayElementValue::ArrayElementValue()` (eez-flow.h:1153) | 87,679 | 2,805,728 | 32 |
| `eez::StringRef::StringRef()` (eez-flow.h:1113, site A) | 12,538 | 150,456 | 12 |
| `eez::StringRef::StringRef()` (eez-flow.h:1113, site B) | 10,313 | 123,756 | 12 |
| `lv_mem_buf_get` (lv_mem.c:433) | 97 | 107,234 | 1,105 |
| `circ_calc_aa4` (lv_draw_mask.c:1370) | 85 | 85,860 | 1,010 |
| `eez::Value::concatenateString(...)` | 10,313 | 70,175 | 7 |
| `lv_style_set_prop_internal` | 1,680 | 62,784 | 37 |
| `eez::Value::makeStringRef(...)` | 12,538 | 52,566 | 4 |
| `eez::alloc(unsigned int, unsigned long)` | 12,357 | 49,417 | 4 |
| `lv_obj_class_create_obj` | 235 | 15,264 | 65 |

The top site alone is 81% of total captured bytes and 67% of total captured
calls. It, and everything else in the top nine except two, is the **eez-flow
expression VM** (`src/display/ui/default/eez/eez-flow.{h,cpp}`) -- the
EEZ-Studio-generated bytecode interpreter that evaluates the UI's bound
expressions (dynamic label text, conditional styling, formatted values).
Every `eez::Value` and `eez::StringRef` it touches heap-allocates on
construction. Confirmed this is churn, not a leak: the free-side of the
histogram (frees with no matching alloc in the same site, since
construction and destruction naturally attribute to different call sites)
is the mirror image of the same VM --
`eez::Value::operator=`/`~Value()`/`~StringRef()`,
`eez::flow::EvalStack::push`, and the `do_OPERATION_TYPE_{NOT,ADD,MUL,
CONDITIONAL}` bytecode handlers all show up as the dominant free sites,
each pairing with a same-class constructor above. Matches `GM_LVMEM`'s
independent observation of a flat live set (56848-56856 B) under
continuously climbing alloc-call counts.

**None of team-lead's four original candidates (lv_mem_buf reallocs,
masks, descriptors, snapshot temporaries) is the dominant source.**
`lv_mem_buf_get` and the mask allocator (`circ_calc_aa4`) are both present
but two orders of magnitude down on call count (97 and 85 calls vs. 87,679)
-- they're infrequent, large (~1 KB) scratch buffers, not the steady churn.
Style/descriptor allocation (`lv_style_set_prop_internal`) is present but
also a minor contributor (2% of bytes). Team-lead's caveat about
`__builtin_return_address(0)` only naming the direct caller, so a thin
wrapper like `lv_mem_buf_get` could mask the real subsystem behind it,
doesn't change this conclusion: the #1 site (`ArrayElementValue`'s own
constructor) isn't a wrapper at all -- it's the actual allocating code, so
there's no intermediary layer to see through here. `lv_mem_buf_get` itself
would be worth a second-layer histogram if it had shown up as dominant; it
didn't.

**Size distribution and the pooling question**: the dominant sites are
tiny and uniform -- 32, 12, 12, 7, 4, 4 bytes/call for the top eez-flow
sites, against 1,105 and 1,010 bytes/call for the two LVGL-internal
scratch allocators. A small fixed pool sized for a handful of size classes
(4/8/12/32 B covering `Value` and `StringRef`) would eliminate the large
majority of call volume -- for a PSRAM-backed heap, each malloc/free pair
pays roughly the same pointer-chase cost regardless of size, so cutting
call count matters far more than cutting bytes moved. This is a
substantially more specific and more promising target than pooling
`lv_mem_buf`, which the original framing (from the rig's LVGL-only heap
view) pointed at by default. **Not measured**: whether QEMU's allocation
rate here (~2,200+/s summed across sites over this 60 s window) is a
faithful proxy for the rig's ~800/s -- QEMU's tick/pass rate runs
substantially faster than the device's fixed 100 ms period (`passes`
~130-700 per 5 s here vs. the rig's real cadence), so only the *ranking*
of call sites should be trusted from this run, not the absolute rate.

### Redraw census: a correction to the earlier report

Earlier in this pass I reported one sample -- visited=303, drawn=300,
drawn_pct=99% -- from the first (and, in that shorter run, only) refresh,
and concluded a tree-cull wouldn't have much headroom. **That conclusion
doesn't hold up against a fuller sample and should be treated as
superseded, not confirmed.** The same instrument (`gm_redraw_visited`/
`gm_redraw_drawn` in `lv_obj_redraw`, `lv_refr.c`, gated on LVGL's own
`should_draw` early-out) dumped 13 windows over this run's 60 s instead of
one:

```
visited=310 drawn=304  98%   <- boot-time full-screen draw (outlier)
visited=106 drawn=62   58%
visited=45  drawn=24   53%
visited=45  drawn=24   53%
visited=42  drawn=22   52%
visited=45  drawn=24   53%
visited=24  drawn=12   50%
visited=12  drawn=7    58%
visited=27  drawn=15   55%
visited=24  drawn=12   50%
visited=27  drawn=15   55%
visited=27  drawn=15   55%
visited=24  drawn=12   50%
```

The very first window is the boot-time full-screen draw, where nearly
everything on screen is legitimately visible -- that's why the earlier
single-sample report read as "no headroom". Excluding it, the 12
steady-state windows (each a touch-triggered incremental refresh) average
**244/448 = 54.5% drawn**. Roughly half of what `lv_obj_redraw` visits
during normal operation is gated out by `should_draw` and never reaches a
`DRAW_MAIN*` event. That's real headroom for a structural cull *if* the
gated-out half is cheap to identify without a full per-object descent --
e.g., a bounding-box precheck against a container's own children before
walking into each one individually, rather than paying the walk-plus-gate
cost per child regardless of outcome. Not designed or prototyped here;
this corrects the earlier negative finding to a positive one, it doesn't
size the win.

**Caveats, same discipline as the rest of this report**: single scene,
`GM_FAKE_CONTROLLER` synthetic traffic (not the rig's real telemetry mix
or screen set), QEMU-vs-device rate mismatch noted above applies equally
here. The 44%-visited-not-drawn figure is a property of *this* scene's
object layout (how much of the tree sits outside each refresh's dirty
rect) and should be treated as one data point, not a universal constant --
worth rechecking against the rig's actual screens before sizing a cull's
expected win.

### Files touched this pass (uncommitted, `.bak`-backed, display-qemu only)

`lv_mem.c` (alloc histogram), `lv_refr.c` (redraw census + `gm_redraw_us`
comment update), `lv_hal_disp.c` (dump hooks, `GM_SNAPATTR2` and its now-
orphaned extern block removed after `lv_obj_draw.c` was restored to
pristine -- see below), `lv_timer.c` (the corrected dump hook location).
`lv_obj_draw.c`, `lv_event.c`, `lv_obj_style.c`, `lv_area.c` are back to
pristine `.bak` state: their Phase 2 sub-attribution instrumentation
(event/style/dscinit/area timing) depended on symbols that lived in the
hand-rolled descriptor-cache prototype in `lv_obj_draw.c`, which got
restored to pristine (twice, once for the official patch to apply, once
more after that patch's own revert left a stray copy from a build/revert
race -- see team-lead's epilogue above and the session's coordination
history). That data is already captured in this file's earlier sections
and wasn't needed live for this pass, so it was retired rather than
revived.
