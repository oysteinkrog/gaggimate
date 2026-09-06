# 2. Sizing — run until convergence, not to a quota

The goal is depth, not budget. Run waves until the next wave produces
**no new findings** — that is the convergence signal. A fixed
agent-count table is the wrong frame; clock-sync's 250–400 runs was
what convergence cost on that topic, not a target.

**The convergence loop (workflow-driven):**

1. Plan the next wave — name 8–16 specific leaves, each with its
   `{bucket, file, topic, model, brief, citationContract}`, informed by
   what previous waves found and what gaps the adversarial wave flagged.
   This plan IS the `args.leaves` you hand the wave Workflow.
2. Fire the wave Workflow (`../templates/wave-workflow.js.tmpl`). It runs
   write → triple-verify → fix per leaf and returns a structured deltas
   object. Commit the wave's files (leader-commit) once it returns.
3. Score the wave by **new findings** from the returned object: diff
   `newClaims` / `newSources` / `newSubAreas` (and `newInsights` on
   synthesis waves) against the cumulative sets you maintain across waves.
   The `audit` block (`p1`/`p2`/`p3`/`fixed`/`unresolved`) reports what
   the in-wave verify already caught and fixed.
4. If new findings still arrive in volume → plan another wave on the
   surfaces still producing.
5. When the new-findings rate drops to near zero across two consecutive
   waves → converged on that branch. Move to adversarial / consolidation.

Initial budget rough-cuts so the user knows what they are authorising:

| Topic shape | Likely runs to convergence | Likely waves |
|---|---|---|
| Narrow, well-bounded (single discipline) | 20–80 | 3–6 |
| Mid-size (cross-discipline) | 80–250 | 6–10 |
| Sprawling, multi-domain | 250–500+ | 12–16+ |

These are calibration numbers, not targets. Stop when the topic is
covered; do not pad to hit a count.

**Per-wave sizing.** 8–16 leaves per wave is the sweet spot, and it lines
up with the Workflow concurrency cap (`min(16, cores−2)`): a 16-leaf wave
fans out ~16 writers, each leaf then costing up to 1 write + 3 verify + 1
fix agent-runs (~80 agent-runs/wave, far under the 1000-run backstop).
Past ~16 leaves the leader's synthesis context starts to drown on the
returned deltas. If a wave wants 30 topics, split it into two waves of 15.

**Always include a SOTA / frontier wave** before consolidation. arXiv
2024-onward, IEEE Xplore recent, ACM, domain-specific venues (NIPS /
Metrologia / Nature Photonics / whatever the topic's primary venues
are). This is the wave that delivers Goal #2 — without it, the KB is a
tidy summary of yesterday's understanding.

**Then a synthesis wave that goes past SOTA.** After the frontier wave
has landed the citations, run a synthesis wave (opus leaves for the
most forward-looking ones) whose explicit brief is *not*
"summarise what we found" but "given everything we
collected, where is the frontier moving, what improvements on current
SOTA are plausible, what cross-pollination from adjacent fields hasn't
happened yet, what would a paper published in five years claim?" This
is where the "compendium from the far future" emerges. Cite where you
can; mark `[uncertain]` where you cannot. This wave is the difference
between a literature survey and a forward-looking KB.

**Map our own work.** Most topics have prior internal context — code,
PRDs, post-mortem patterns, vendor experience. A dedicated wave that
catalogues "what we already know and have built" (segregated under
`06-our-work/` or similar) prevents the rest of the KB from drifting
into the abstract. Run this once the public-literature surface is
mostly converged, so internal claims land against an external baseline
instead of inventing one.

**Plan a reserve wave** for targeted follow-ups (gaps the adversarial
wave flagged, novel ideas the synthesis wave proposed, refreshes against
newer preprints). The reserve is what makes convergence cheap; without
it, you re-launch a fresh build to fix gaps.
