# 5. Run the build

Each wave is one `Workflow` invocation, fired from
[`../templates/wave-workflow.js.tmpl`](../templates/wave-workflow.js.tmpl).
The per-wave procedure:

1. **Author the wave plan** as the workflow `args`: `topicSlug`,
   `topicDir`, `waveType` (`foundations` | `core` | `sota` | `synthesis`
   | `our-work` | `adversarial`), `waveNo`, the locked `contract` from the
   interview, and `leaves[]` — one `{bucket, file, topic, model, brief,
   citationContract}` per leaf. Each `brief` carries the scope + overlap
   guard so the leaf agent cites outward instead of re-deriving a sibling
   KB's material.
2. **Fire the Workflow.** Copy the template, adapt it to the wave if
   needed (an adversarial wave drops the write stage and only surveys; a
   synthesis wave passes `sotaContext`), and invoke with the `args`. In
   autonomous mode pass `run_in_background: true` so the main loop is
   re-invoked on completion.
3. **The pipeline does the rest.** Per leaf: write+validate → triple-lens
   verify → fix loop. Leaf agents **write and validate their one file via
   `validate-frontmatter.py`, then return — they do NOT git commit** (see
   the leader-commit rule in *Execution model*). No `isolation:
   "worktree"` for any leaf — they write files you read back (global
   CLAUDE.md "Agent Swarm Rules"). `[uncertain]` over fabrication, always.
4. **Leader-commit the wave** when the Workflow returns. Stage the wave's
   files explicitly and commit:
   `git add <topic>/<bucket>/... && git commit -m "kb(<slug>): wN content — <buckets>"`.
   Validate first: `python3 knowledge-base/scripts/validate-frontmatter.py knowledge-base/<topic>/`
   (invalid=0; warnings are commit-blockers).
5. **Update convergence state.** Diff the returned `newClaims` /
   `newSources` / `newSubAreas` / `newInsights` against your cumulative
   sets; record the wave in `PROGRESS.md` with its deltas and `audit`.

Between waves the mode (§1.6) decides: **gated** → surface deltas, gate on
`proceed`; **autonomous-to-PR** → proceed on your own judgement, escalate
only on a premise-invalidating finding.
