---
name: kb-new-topic
model: opus
description: Bootstrap a new citation-strict KB topic under knowledge-base/<topic>/. The skill interviews the user to lock scope and sizing, then drives the build as a sequence of ultracode Workflows — each wave a dynamically authored write→triple-verify→fix pipeline — and curates the result into a qmd-indexed collection. Use when promoting a candidate from the ranked queue in BACKLOG.md, not for editing an existing topic.
triggers:
  - "new KB topic"
  - "bootstrap KB topic"
  - "build a new knowledge base"
  - "promote topic from TOPICS"
  - "kb-new-topic"
---

# kb-new-topic — bootstrap a new KB topic

A KB is only useful if it's right *and* deep. Most of the cost is in
scoping correctly, going deep enough to reach the frontier, and
rejecting hallucinated content — not in writing files. The scaffold
pattern from `knowledge-base/clock-sync/` is *one* shape; copy the bits
that fit, drop the bits that don't.

You are the **coordinator** for the whole build, not a contributor to
individual buckets: plan the next wave, spawn it, read the outputs,
decide what's still missing, gate on the user, repeat. **The one rule:
every claim in the curated buckets must trace to a verifiable source.**
See [references/PHILOSOPHY.md](references/PHILOSOPHY.md) for the full
goal (efficiency + ULTRA-SOTA depth), the leader stance, and the one
rule in full.

## How the build runs

Each wave is a *dynamically authored* `Workflow` script (ultracode), not
a hand-managed batch of `Agent` spawns. The Workflow owns the
deterministic per-leaf fan-out *within* a wave (write → triple-verify →
fix); you own scope, planning, and the convergence judgement *between*
waves. Leaf agents write + validate their one file and **return — they
do NOT git commit**; you leader-commit each wave once the Workflow
returns. Workflows fire **only after the user opts into ultracode**;
without opt-in, fall back to manual per-wave `Agent` spawns, gated. Full
mechanism (why-a-workflow, resumability, commit model, opt-in):
[references/EXECUTION-MODEL.md](references/EXECUTION-MODEL.md). Reference
shape: [`templates/wave-workflow.js.tmpl`](templates/wave-workflow.js.tmpl).

## Phase router

Work the phases in order. Each links to the reference that holds the
full procedure, decision rules, and examples.

| Phase | Do | Reference |
|---|---|---|
| **1. Interview** | Lock scope, slug (platform-prefix rule), citation contract, company-content policy, bucket taxonomy, frontier stance, downstream outputs. `AskUserQuestion` one at a time, record in `MASTER-PLAN.md`. | [INTERVIEW.md](references/INTERVIEW.md) |
| **1.5. File trackers** | Open the bead (`br create`). Move BACKLOG/TOPICS rows with the bead. | [INTERVIEW.md](references/INTERVIEW.md) |
| **1.6. Lock exec mode** | Gated (default) vs autonomous-to-PR. Record in `MASTER-PLAN.md`. Autonomy needs explicit ultracode opt-in. | [INTERVIEW.md](references/INTERVIEW.md) |
| **2. Sizing** | Run waves until **no new findings** (convergence), not to a quota. 8–16 leaves/wave. Always a SOTA wave, then a past-SOTA synthesis wave, an our-work wave, and a reserve wave. | [SIZING-AND-CONVERGENCE.md](references/SIZING-AND-CONVERGENCE.md) |
| **3. Model selection** | Sonnet default for every leaf; opus only for the few hardest lanes and the fidelity verify. Mix within a wave; set per leaf. | [MODEL-SELECTION.md](references/MODEL-SELECTION.md) |
| **4. Verification** | Triple-lens verify per leaf every wave (resolvability / fidelity / coherence). A dedicated late adversarial wave is non-optional. `[uncertain]` beats fabrication. | [VERIFICATION.md](references/VERIFICATION.md) |
| **5. Run the build** | Author the wave `args` (`leaves[]`), fire the Workflow, let the pipeline write→verify→fix, leader-commit, update convergence state in `PROGRESS.md`. | [RUNNING-WAVES.md](references/RUNNING-WAVES.md) |
| **6. Consolidate** | Bibliography, `COMPENDIUM.md`, `FINAL-CONVERGENCE-REPORT.md`, register the qmd collection, add routing + registry rows, optional `/kb-<slug>` skill. | [CONSOLIDATION.md](references/CONSOLIDATION.md) |
| **7. Squash & PR** | Rebuild on fresh `main` into **5 structural commits** (scaffold / content / adversarial / consolidation / register), refresh build-cost ledger last, validate, push. | [SQUASH-AND-PR.md](references/SQUASH-AND-PR.md) |

Between waves the mode (§1.6) decides: **gated** → surface deltas, gate
on `proceed`; **autonomous-to-PR** → proceed on your own judgement,
escalate only on a premise-invalidating finding.

## Before you start

Read [references/ANTI-PATTERNS.md](references/ANTI-PATTERNS.md) — the
full catalog of failure modes (quota-stopping, skipping the SOTA wave,
per-agent commits inside a Workflow, worktree isolation for leaves,
single-layer KBs, filing a Task instead of a Sub-task, pushing 200+
micro-commits, and more). Every anti-pattern maps to a phase above.

## References

- **[PHILOSOPHY.md](references/PHILOSOPHY.md)** — why we build KBs
  (efficiency + ULTRA-SOTA depth), the coordinator stance, the one rule.
- **[EXECUTION-MODEL.md](references/EXECUTION-MODEL.md)** — ultracode +
  dynamic Workflows: per-leaf pipeline, structured deltas, resumability,
  the ultracode opt-in gate, and the leader-commit model.
- **[INTERVIEW.md](references/INTERVIEW.md)** — phases 1 / 1.5 / 1.6:
  the interview questions (scope, slug prefix rule, contract, taxonomy),
  filing the bead, and locking gated vs autonomous mode.
- **[SIZING-AND-CONVERGENCE.md](references/SIZING-AND-CONVERGENCE.md)** —
  the convergence loop, per-wave sizing, the SOTA / synthesis / our-work /
  reserve waves, and the budget rough-cut table.
- **[MODEL-SELECTION.md](references/MODEL-SELECTION.md)** — sonnet-default
  discipline, when to promote a leaf to opus, and the verify/fix stage
  model assignments.
- **[VERIFICATION.md](references/VERIFICATION.md)** — the triple-lens
  verify, load-bearing-claim fidelity checks, the adversarial wave, and
  spot-check discipline during consolidation.
- **[RUNNING-WAVES.md](references/RUNNING-WAVES.md)** — the per-wave
  procedure: authoring `args`, firing the Workflow, leader-commit,
  convergence-state bookkeeping.
- **[CONSOLIDATION.md](references/CONSOLIDATION.md)** — the seven wrap-up
  steps: three artifacts, qmd registration, routing/registry rows, squash
  handoff, optional per-topic skill.
- **[SQUASH-AND-PR.md](references/SQUASH-AND-PR.md)** — the 5-commit
  structure, the rebuild-on-fresh-master squash procedure, and the
  build-cost ledger refresh.
- **[ANTI-PATTERNS.md](references/ANTI-PATTERNS.md)** — full catalog of
  known failure modes with the fix for each.

## Reference material (external)

- Canonical example: `knowledge-base/clock-sync/` — MASTER-PLAN.md,
  PROGRESS.md, CLAUDE.md, per-bucket sub-harnesses. Read these once
  before bootstrapping; copy the bits that fit your topic.
- Topic backlog (ranked queue): `knowledge-base/BACKLOG.md`.
- Topic registry (built KBs, lookup): `knowledge-base/REGISTRY.md`.
- Topic history (graduation log + discovery runs): `knowledge-base/TOPICS.md`.
- Shared infrastructure: `knowledge-base/CLAUDE.md`,
  `knowledge-base/.schemas/base.schema.json`,
  `knowledge-base/scripts/validate-frontmatter.py`.
- Swarm rules: `.claude/orchestration.md` (NxM notation, shared-checkout rules,
  file reservations).
- qmd registration template: `templates/install-collection.sh.tmpl`.
- Wave Workflow template (the per-wave write→verify→fix pipeline):
  `templates/wave-workflow.js.tmpl`. Fired via the `Workflow` tool; see
  [references/EXECUTION-MODEL.md](references/EXECUTION-MODEL.md) for the
  leader-commit + opt-in contract.
- Related skills: `/kb-query`, `/kb-clock-sync`, `/kb-validate`,
  `/setup-qmd-wsl1-wrapper`.
