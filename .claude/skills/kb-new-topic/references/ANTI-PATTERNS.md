# Anti-patterns

- **Skipping the interview.** Without locked-down scope, the build
  sprawls and the user pays for noise.
- **Stopping at a quota instead of convergence.** "We hit 100 runs, ship
  it" leaves coverage gaps. Stop when two consecutive waves produce no
  new findings, not when you hit a number.
- **Skipping the SOTA / frontier wave.** Without it, the KB summarises
  yesterday's understanding and breaks Goal #2. Generic LLM memory is
  not a substitute for current arXiv / IEEE / domain proceedings.
- **Cloning clock-sync's bucket names verbatim.** They are the
  clock-sync taxonomy, not a universal one. Rename early; renaming
  after content lands is expensive.
- **One model for the whole build.** Opus everywhere wastes budget;
  sonnet everywhere misses subtle inconsistencies.
- **Verification deferred to "after all waves".** Hallucinations bake
  in. The verify + fix stages live inside every wave Workflow by
  construction — never strip them to "go faster".
- **Running waves without ultracode opt-in.** A wave Workflow spawns
  dozens of agents and burns tokens; it fires only after the user opts
  into ultracode / autonomy (§1.6). No opt-in → manual `Agent` spawns,
  gated per wave.
- **Auto-spawning unauthorized waves in gated mode.** In gated mode every
  wave is a user gate. Only autonomous-to-PR mode (explicit opt-in) lets
  you proceed between waves on your own judgement — and even then you
  escalate on any premise-invalidating finding.
- **Per-agent `git commit` inside a Workflow.** Up to ~16 leaves run
  concurrently; their commits race on `.git/index.lock`. Leaf agents
  write + validate + return; the coordinator leader-commits the wave.
- **Worktree isolation for leaf agents.** They write files you read back
  and commit — `isolation: "worktree"` causes silent merge regressions
  (global "Agent Swarm Rules"). Never use it for any KB build agent.
- **Computed `meta` in a wave script.** The Workflow `meta` must be a pure
  literal — no variables, calls, spreads, or interpolation — or the script
  fails to load.
- **Single-layer KB.** A topic covered only at the algorithm layer with
  no physical / hardware grounding (or vice versa) is shallow even if
  it's long. Force coverage across math / systems / physical / hardware
  layers — or document why a layer was deliberately excluded.
- **Stopping at SOTA summary.** Goal #2 demands synthesis *past* the
  current frontier, not a clean recap of it. If no agent has been
  tasked with "where is this moving / what would improve on current
  SOTA", the build isn't done.
- **Scaffolding outputs during the build.** Do not create `outputs/`
  directories, paper skeletons, slide templates, course outlines, or
  workshop materials as part of the KB. The KB is the durable source;
  outputs are derived later, on demand, when someone actually commits
  to producing one. Pre-built skeletons rot before they ship.
- **Leader writing leaf content.** Coordinator stance — your job is
  planning, synthesis, verification, gating. If you find yourself
  authoring a bucket's reference files yourself between waves, spawn
  an agent instead.
- **Editing TOPICS.md without moving the bead.** Bead state governs.
- **Pushing the PR with 200+ wave-level micro-commits.** Convergent
  reviewers see the structural shape of the deliverable, not the
  per-agent commit stream. Always squash to 5 structural commits
  (§7) before opening or updating the PR.
- **Squashing via `rebase -i` of the micro-commits.** Conflict storm —
  upstream schema, sibling topics, and shared infra have all moved on
  during the build. Use the "rebuild on top of fresh `main`"
  pattern in §7 instead.
- **Forgetting to include `knowledge-base/.schemas/` enum extensions
  in the scaffold commit.** If the topic needed a new `status`,
  `component_class`, or `form_factor` value, the extension lives
  *outside* `<topic>/` but is logically part of the topic. Without it
  validation fails and the PR is unmergeable. Catch it via the
  validation step before pushing.
