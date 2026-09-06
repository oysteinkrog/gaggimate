# Execution model — ultracode + dynamic Workflows

This build runs on **ultracode**: each wave is a *dynamically authored*
`Workflow` script, not a hand-managed batch of `Agent` spawns. You — the
coordinator in the main loop — still own scope, planning, and the
convergence judgement; the Workflow tool owns the deterministic fan-out
*within* a wave (write → triple-verify → fix), so a wave's 8–16 leaves
each verify the moment they are written instead of waiting on a barrier.

Why a Workflow per wave instead of manual `Agent` spawns:

- **Per-leaf pipeline.** `pipeline(leaves, write, verify, fix)` runs each
  leaf through all three stages independently — leaf A is being verified
  while leaf B is still being written. No wave-wide barrier; wall-clock is
  the slowest single chain, not the slowest stage.
- **Verification is structural, not bolted on.** The triple-lens verify
  (resolvability / fidelity / coherence) and the fix loop live *in the
  script*, so every leaf is adversarially checked by construction — Goal-#1
  trust is enforced mechanically, not by remembering to spawn a checker.
- **Structured convergence deltas.** The wave returns a typed object
  (`newClaims`, `newSources`, `newSubAreas`, `newInsights`, `audit`). You
  diff those against the cumulative sets to score new-findings rate — the
  convergence signal becomes data, not vibe.
- **Resumable.** A killed wave resumes via `resumeFromRunId`; completed
  leaves return cached, only the failed leaf re-runs.

The reference shape is [`../templates/wave-workflow.js.tmpl`](../templates/wave-workflow.js.tmpl)
— copy it, fill `args` from your wave plan, fire it with the `Workflow`
tool. Adapt the script per wave (a SOTA wave's lenses differ from a
foundations wave's; an adversarial wave may skip the write stage and only
survey). It is a reference shape, not a frozen API. `meta` must be a pure
literal (no variables, calls, or interpolation).

**Opt-in is mandatory.** Workflows spawn dozens of agents and burn
tokens, so they only fire when the user has opted into ultracode — the
keyword "ultracode", an ultracode-on session, or an explicit "use a
workflow" / "drive this autonomously". The §1.6 execution-mode question
captures that consent up front; if the user has *not* opted in, fall back
to manual per-wave `Agent` spawns and gate every wave.

**Commit model — leader-commit, NOT per-agent.** Workflow leaves run up
to ~16 concurrent; concurrent `git commit` races on `.git/index.lock`.
So workflow agents **write + validate their one file and return — they do
NOT git commit**. Files persist on disk, so a workflow death loses
nothing (resume, or commit the working tree). You — the coordinator —
commit each wave's output as one step after the Workflow returns. Because
every leaf writes a disjoint path, agent-mail file reservations are
unnecessary *inside* a wave; the reserve-before-write rule only matters
when two agents might touch the same file, which the per-leaf pipeline
guarantees they don't.
