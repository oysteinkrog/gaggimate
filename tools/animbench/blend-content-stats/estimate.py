#!/usr/bin/env python3
"""
Synthetic estimator: what fraction of aligned 8-pixel groups in blendRow's
runs land in each of the three group-dispatch paths a real PIE vector
composite kernel (tools/animbench/kernels-blend/, and the pure-C model
proven in tools/overlaybench/kernels/overlay_blend.cpp's blendRow_pie_model)
would take:

  (a) all 8 lanes a==255            -- trivial vector copy
  (b) no lane a==255                -- real vector arithmetic, a genuine win
  (c) a mix of the two              -- scalar fallback, no vector win

Pixels in a run that never reach a full 8-pixel-aligned window at all
(prologue before the first boundary, epilogue after the last) ALWAYS take
the scalar path too -- tracked as a fourth bucket, "prologue/epilogue",
which behaves like (c) for the "no win" tally.

No device access and no captured real overlay content exist for this task
-- this is a synthetic model built from what the codebase documents about
run structure, run count, and content density, run as a Monte Carlo sweep
over plausible parameters. Read the FACTS / INFERENCES / GUESSES sections
below before trusting any single number out of the table; the point of
this script is the SHAPE of the answer and its sensitivity to the
uncertain inputs, not a false-precision point estimate.

Usage: python3 estimate.py
"""
import random
import statistics

# =============================================================================
# FACTS (verified in this session, cited by file:line; re-checked directly
# against source rather than trusted from the task brief -- see the
# "IMPORTANT CORRECTION" section below for why that mattered)
# =============================================================================
#
# 1. Group size is 8 pixels (16 bytes), aligned to the row's ABSOLUTE
#    position, not the run's own start. src/display/ui/default/
#    SleepAnimation.cpp's band buffer is internal SRAM, row stride 480 px =
#    960 B, and tools/overlaybench/kernels/overlay_blend.cpp's
#    blendRow_pie_model comment establishes this alignment reasoning
#    directly (16 | 960). A run starting mid-row is NOT 8-aligned in
#    general -- confirmed structurally: scanOverlayRow (SleepAnimation.cpp
#    ~1327-1360) emits a run wherever `*a != 0` starts, with no alignment
#    constraint at all.
#
# 2. RUN_GAP_MERGE = 4 (SleepAnimation.cpp:99, comment at 96-98): "Runs
#    closer together than this are emitted as one... antialiased text puts
#    one- and two-pixel gaps everywhere." This is the source's own
#    statement that a merged run can and does contain an internal
#    zero-alpha (or near-zero) gap up to 4 px wide -- i.e. a run is not
#    guaranteed to be one uniform block of coverage.
#
# 3. RUNS_PER_ROW = 24 (SleepAnimation.cpp:94, comment 90-93): "24 is well
#    past what the standby widgets produce; a row through the clock and
#    both icons emits about eight." A content-bearing row has on the order
#    of 8 runs, not 1 and not 24 -- i.e. runs are individually short
#    relative to the 480 px row, consistent with thin glyph strokes and
#    icon edges rather than a few wide fills.
#
# 4. blendRow's own comment (SleepAnimation.cpp ~365-366): "Opaque is most
#    of a glyph's interior and needs no arithmetic at all; only the
#    antialiased rim reaches blend565." This establishes the run-internal
#    SHAPE this model uses (opaque core, non-opaque rim at each end) as the
#    source's own mental model, not an invention -- but it does NOT state a
#    rim width in pixels; that part is an inference (see below).
#
# 5. blendRow is entered ~3,100 times a frame if called per-run instead of
#    per-row (SleepAnimation.cpp:336-337, "Entered once per run it was
#    called ~3,100 times a frame") -- i.e. ~3,100 runs/frame, current,
#    confirmed by git blame to be the SAME commit (f5df47d13, "drive the
#    overlay composite from exact runs, not block masks") that introduced
#    today's exact-run design, so this figure describes the design the
#    firmware actually ships.
#
# =============================================================================
# IMPORTANT CORRECTION to this task's own brief
# =============================================================================
#
# The task brief handed to this worker asserted "~24,600 overlay
# pixels/frame across ~3,100 runs/frame (~7.9 px/run average -- note how
# close this is to 8, it matters for task 2 below)," citing
# BASELINE-OVERLAY.md/FINDINGS-OVERLAY.md. Both files were read in full for
# this task and NEITHER contains the figures "24,600" or "3,100" anywhere
# (grepped directly, zero hits). The only place "24,600" appears in this
# repository is SleepAnimation.cpp:378, in the SAME commit (f5df47d13) as
# the "~3,100 runs" comment, but describing something DIFFERENT: "the halo
# is four times the area of the glyphs: 19,000 of the 24,600 pixels the
# composite walked paid a load whose answer was always zero" -- this is the
# OLD, pre-optimization MERGED pass (scrim dimming and glyph compositing
# done together, before this same commit split them into two passes), and
# 24,600 counts HALO pixels (19,000 of them, a zero-answer load) PLUS real
# glyph pixels (~5,600) together. Today's blendRow, post-split, walks only
# the glyph runs -- it never touches halo pixels at all (scrimRow, a
# separate pass over a separate 1/4-resolution grid, handles those). So
# 24,600 is not blendRow's current per-frame pixel count; dividing it by
# the (correctly current) 3,100 run count conflates a pre-optimization
# total that includes now-eliminated halo work with a post-optimization run
# count that does not.
#
# The commit's own message states the corrected total directly: "Of the
# 14,200 pixels a frame that sent it through, 5,300 had any coverage" --
# and 24,600 - 19,000 = 5,600, consistent with that 5,300 to within
# rounding (both describing the same real-glyph-coverage quantity from two
# angles in the same commit). Pairing the RIGHT numerator with the RIGHT
# denominator: 5,300-5,600 real-coverage pixels / ~3,100 runs implies an
# average run WIDTH (not just covered-pixel density -- see below) very
# roughly in the 1.7-1.8 px/run range from covered pixels alone, likely
# somewhat higher once RUN_GAP_MERGE's internal gap pixels (fact 2 above)
# are counted into the walked width -- but nowhere near 7.9-8 px.
#
# This is a real, evidence-based correction, not a nitpick: it points the
# conclusion in a MORE pessimistic direction for the vector kernel than the
# task brief's own framing did (a shorter average run means proportionally
# MORE of every frame's touched pixels fall outside any full aligned
# 8-pixel group entirely, i.e. more prologue/epilogue, less path (a)/(b)).
# Given the genuine uncertainty in both figures (three-day-old profiling
# snapshot, content mix drifts with which screen/animation is active), this
# script sweeps average run width across BOTH the brief's stated ~7.9 px
# and this session's re-derived ~2-5 px range, so the conclusion below does
# not depend on picking a winner between them.
#
# =============================================================================
# INFERENCES (plausible, weakly-to-moderately supported, not directly
# measured)
# =============================================================================
#
# - AA rim width of 1-2 px per edge: standard for antialiasing of small
#   bitmap/vector fonts at typical UI pixel densities, and loosely
#   corroborated by RUN_GAP_MERGE's OWN comment ("antialiased text puts
#   one- and two-pixel gaps everywhere") -- that sentence is literally
#   about GAP width between strokes, not rim width on a single glyph edge,
#   so treat the 1-2 px figure as a same-order-of-magnitude inference, not
#   a direct measurement. Searched src/display/lv_conf.h for a harder
#   number (LV_USE_FONT_SUBPX, LV_FONT_*_SUBPX, antialiasing-related
#   defines) and found only that subpixel rendering is OFF
#   (LV_USE_FONT_SUBPX 0) -- no rim-width parameter exists in LVGL's config
#   surface to confirm or refute this. This script sweeps rim width 1-3 px
#   to cover the plausible range.
#
# - Icons vs. text mix: RUNS_PER_ROW's comment mentions "the clock and both
#   icons" sharing a row with ~8 runs total, suggesting icons are NOT huge
#   solid fills that would dominate the pixel count with wide opaque runs
#   (a whole-icon-is-one-run design would show as very few, very long
#   runs, not ~8 modest ones) -- but this is inferred from run COUNT, not a
#   confirmed statement about icon rendering internals. Not modeled
#   separately here; if icons render as large filled glyphs (as opposed to
#   thin vector strokes), the true path-(a) fraction could be higher than
#   this script's numbers for the rows that contain them.
#
# =============================================================================
# GUESSES (not corroborated by anything in the codebase; included only to
# make the model's sensitivity to them visible)
# =============================================================================
#
# - The exact interior-length distribution (this script uses interior
#   length L as a swept, fixed parameter per scenario rather than a
#   modeled distribution).
# - How often RUN_GAP_MERGE's internal-gap merging actually happens on
#   real content vs. runs already being single unbroken segments (this
#   script includes a "merged" scenario alongside a "simple" one, but the
#   real MIX between them on live content is a guess).


def run_pixel_kinds(total_width, interior_len, rim_each_side, internal_gap=0, interior_split=0.5):
    """Build one run's per-pixel opacity classification as a list of bool
    (True = a==255 "opaque", False = "non-opaque": AA rim OR a==0 gap-merge
    filler -- both collapse to the same group-dispatch bucket, since
    blendRow_pie_model's group split only distinguishes "any lane opaque"
    from "no lane opaque", not rim vs. gap; see the header comment above.

    Shape modeled, per blendRow's own comment ("opaque is most of a
    glyph's interior... only the antialiased rim reaches blend565"):
        [rim]*rim_each_side + [opaque interior, optionally split by one
        internal gap] + [rim]*rim_each_side
    padded/truncated to exactly total_width pixels (a run's actual x1-x0
    width; interior_len + 2*rim_each_side does not have to equal it -- a
    short run may be ALL rim with zero interior, matching a lone AA pixel
    isolated by more than RUN_GAP_MERGE from its neighbours).
    """
    if interior_len <= 0:
        # No room for an opaque interior at all: whole run is non-opaque
        # (a lone antialiased pixel or two, the shortest runs RUNS_PER_ROW's
        # comment implies are common).
        return [False] * total_width

    left_rim = min(rim_each_side, total_width)
    remaining = total_width - left_rim
    right_rim = min(rim_each_side, max(0, remaining - 0))
    interior_budget = max(0, total_width - left_rim - right_rim)
    interior = min(interior_len, interior_budget)

    pixels = [False] * left_rim
    if internal_gap > 0 and interior > internal_gap:
        # Split the interior into two opaque spans separated by an
        # internal non-opaque gap (RUN_GAP_MERGE evidence: a merged run can
        # carry an internal near-zero-alpha gap up to 4 px).
        span1 = max(1, int(interior * interior_split))
        span1 = min(span1, interior - 1)
        gap = min(internal_gap, interior - span1 - 1) if interior - span1 - 1 > 0 else 0
        span2 = interior - span1 - gap
        pixels += [True] * span1 + [False] * gap + [True] * max(0, span2)
    else:
        pixels += [True] * interior
    pixels += [False] * max(0, total_width - len(pixels) - right_rim)
    pixels += [False] * min(right_rim, total_width - len(pixels))
    # Exact-length guard (rounding in the split above must never change the
    # run's own width -- this is a model, but it must not silently drop or
    # invent pixels).
    if len(pixels) < total_width:
        pixels += [False] * (total_width - len(pixels))
    return pixels[:total_width]


def classify_groups(pixels, start_offset):
    """Given one run's pixel-opacity list and its absolute row start offset
    (0..7, position of pixels[0] within the 8-pixel alignment grid), return
    pixel counts per bucket: opaque_group (a), general_group (b),
    mixed_group (c), prologue_epilogue.
    """
    n = len(pixels)
    counts = {"opaque_group": 0, "general_group": 0, "mixed_group": 0, "prologue_epilogue": 0}
    i = 0
    while i < n:
        abs_pos = start_offset + i
        if abs_pos % 8 == 0 and i + 8 <= n:
            group = pixels[i:i + 8]
            n_opaque = sum(group)
            if n_opaque == 8:
                counts["opaque_group"] += 8
            elif n_opaque == 0:
                counts["general_group"] += 8
            else:
                counts["mixed_group"] += 8
            i += 8
        else:
            counts["prologue_epilogue"] += 1
            i += 1
    return counts


def simulate(total_width, interior_len, rim_each_side, internal_gap, trials, rng):
    """Monte Carlo over random 0..7 alignment offsets for a fixed run
    shape. Alignment is the one thing genuinely uniform-random here (a
    run's start has no reason to correlate with the 8-pixel grid), so this
    is the part of the model that does NOT need a documented distribution
    to justify -- it's a structural fact (fact 1 above), not a guess.
    """
    totals = {"opaque_group": 0, "general_group": 0, "mixed_group": 0, "prologue_epilogue": 0}
    for _ in range(trials):
        offset = rng.randrange(8)
        pixels = run_pixel_kinds(total_width, interior_len, rim_each_side, internal_gap)
        c = classify_groups(pixels, offset)
        for k in c:
            totals[k] += c[k]
    total_px = sum(totals.values())
    return {k: v / total_px for k, v in totals.items()}


def fmt_pct(x):
    return f"{100 * x:5.1f}%"


def main():
    rng = random.Random(0xB1EDC0DE)
    trials = 20000

    print("=" * 100)
    print("Synthetic group-dispatch estimate for blendRow's 8-pixel vector paths")
    print("=" * 100)
    print()
    print("Columns: opaque_group=(a) trivial copy | general_group=(b) real vector win |")
    print("         mixed_group=(c) scalar fallback | prologue_epilogue=scalar (never in an")
    print("         aligned group at all) | win=(a)+(b) fraction of pixels | noWin=(c)+prologue")
    print()

    # -------------------------------------------------------------------
    # Sweep 1: "simple" runs (one opaque interior, no internal gap), across
    # both the task brief's ~7.9 px/run figure and this session's
    # re-derived ~2-5 px range, and across rim widths 1-3 px.
    # -------------------------------------------------------------------
    print("--- Sweep 1: simple runs (opaque interior + rim, no internal gap) ---")
    print(f"{'width W':>8} {'rim R':>6} {'interior L':>11} {'opaque(a)':>10} {'general(b)':>11} "
         f"{'mixed(c)':>9} {'prologue':>9} {'win a+b':>8} {'noWin':>7}")
    widths = [2, 3, 4, 5, 6, 7.9, 8, 10, 12, 16]
    rims = [1, 2, 3]
    win_fracs_sweep1 = []
    for w in widths:
        w_int = max(1, round(w))
        for r in rims:
            interior = max(0, w_int - 2 * r)
            res = simulate(w_int, interior, r, internal_gap=0, trials=trials, rng=rng)
            win = res["opaque_group"] + res["general_group"]
            nowin = res["mixed_group"] + res["prologue_epilogue"]
            win_fracs_sweep1.append(win)
            print(f"{w:>8} {r:>6} {interior:>11} {fmt_pct(res['opaque_group']):>10} "
                 f"{fmt_pct(res['general_group']):>11} {fmt_pct(res['mixed_group']):>9} "
                 f"{fmt_pct(res['prologue_epilogue']):>9} {fmt_pct(win):>8} {fmt_pct(nowin):>7}")
    print()
    print(f"Sweep 1 win-fraction (a+b) range: {fmt_pct(min(win_fracs_sweep1))} .. "
         f"{fmt_pct(max(win_fracs_sweep1))}, median {fmt_pct(statistics.median(win_fracs_sweep1))}")
    print()

    # -------------------------------------------------------------------
    # Sweep 2: "merged" runs (RUN_GAP_MERGE evidence) -- an internal
    # non-opaque gap up to 4 px splits what would be one opaque interior
    # into two smaller ones, which can only hurt path (a)'s odds (splitting
    # a run that might have had one full aligned all-opaque group into two
    # shorter opaque spans, each less likely to fill a whole group).
    # -------------------------------------------------------------------
    print("--- Sweep 2: merged runs (internal RUN_GAP_MERGE-style gap, 1-4 px) ---")
    print(f"{'width W':>8} {'rim R':>6} {'gap G':>6} {'opaque(a)':>10} {'general(b)':>11} "
         f"{'mixed(c)':>9} {'prologue':>9} {'win a+b':>8} {'noWin':>7}")
    win_fracs_sweep2 = []
    for w in [8, 12, 16, 20]:
        for r in [1, 2]:
            for g in [1, 2, 4]:
                interior = max(0, w - 2 * r)
                res = simulate(w, interior, r, internal_gap=g, trials=trials, rng=rng)
                win = res["opaque_group"] + res["general_group"]
                nowin = res["mixed_group"] + res["prologue_epilogue"]
                win_fracs_sweep2.append(win)
                print(f"{w:>8} {r:>6} {g:>6} {fmt_pct(res['opaque_group']):>10} "
                     f"{fmt_pct(res['general_group']):>11} {fmt_pct(res['mixed_group']):>9} "
                     f"{fmt_pct(res['prologue_epilogue']):>9} {fmt_pct(win):>8} {fmt_pct(nowin):>7}")
    print()
    print(f"Sweep 2 win-fraction (a+b) range: {fmt_pct(min(win_fracs_sweep2))} .. "
         f"{fmt_pct(max(win_fracs_sweep2))}, median {fmt_pct(statistics.median(win_fracs_sweep2))}")
    print()

    # -------------------------------------------------------------------
    # Sweep 3: the specific scenario the task brief's own framing pointed
    # at -- W == 7.9 (rounded to 8), taken at face value, across rim widths.
    # Included on its own so the "if the brief's number were right" case is
    # visible without digging through Sweep 1's full table.
    # -------------------------------------------------------------------
    print("--- Sweep 3: face-value check of the task brief's ~7.9-8 px/run figure ---")
    win_fracs_sweep3 = []
    for r in rims:
        w_int = 8
        interior = max(0, w_int - 2 * r)
        res = simulate(w_int, interior, r, internal_gap=0, trials=trials, rng=rng)
        win = res["opaque_group"] + res["general_group"]
        win_fracs_sweep3.append(win)
        print(f"  W=8, rim={r}: interior={interior}, win(a+b)={fmt_pct(win)}, "
             f"noWin={fmt_pct(res['mixed_group'] + res['prologue_epilogue'])}")
    print()

    # -------------------------------------------------------------------
    # Sweep 4: this session's re-derived, evidence-corrected run-width
    # range (~1.7-1.8 px covered-pixel density, generously padded to ~2-5
    # px to account for RUN_GAP_MERGE's internal filler pixels widening the
    # walked range beyond the raw coverage count).
    # -------------------------------------------------------------------
    print("--- Sweep 4: this session's re-derived run-width range (~2-5 px) ---")
    win_fracs_sweep4 = []
    for w in [2, 3, 4, 5]:
        for r in [1, 2]:
            interior = max(0, w - 2 * r)
            res = simulate(w, interior, r, internal_gap=0, trials=trials, rng=rng)
            win = res["opaque_group"] + res["general_group"]
            win_fracs_sweep4.append(win)
            print(f"  W={w}, rim={r}: interior={interior}, win(a+b)={fmt_pct(win)}, "
                 f"noWin={fmt_pct(res['mixed_group'] + res['prologue_epilogue'])}")
    print()
    print(f"Sweep 4 win-fraction (a+b) range: {fmt_pct(min(win_fracs_sweep4))} .. "
         f"{fmt_pct(max(win_fracs_sweep4))}")
    print()

    print("=" * 100)
    print("Summary")
    print("=" * 100)
    all_wins = win_fracs_sweep1 + win_fracs_sweep2 + win_fracs_sweep4
    print(f"Across every scenario swept (widths 2-20 px, rim 1-3 px, with and without an")
    print(f"internal RUN_GAP_MERGE-style gap): win-fraction (a)+(b) ranges from "
         f"{fmt_pct(min(all_wins))} to {fmt_pct(max(all_wins))}.")
    print(f"Restricted to this session's re-derived, evidence-corrected run-width estimate")
    print(f"(~2-5 px, Sweep 4): {fmt_pct(min(win_fracs_sweep4))} to {fmt_pct(max(win_fracs_sweep4))}.")
    print(f"Restricted to the task brief's original ~7.9-8 px figure (Sweep 3, taken at face")
    print(f"value despite the correction above): "
         f"{fmt_pct(min(win_fracs_sweep3))} to {fmt_pct(max(win_fracs_sweep3))}.")


if __name__ == "__main__":
    main()
