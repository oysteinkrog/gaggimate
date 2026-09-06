# Interview, trackers, and execution-mode lock (phases 1 / 1.5 / 1.6)

## 1. Interview — define the topic

Before any agents spawn, you must have written-down answers to these.
Use `AskUserQuestion` one at a time, not in batches; record verbatim in
the topic's `MASTER-PLAN.md`.

- **Scope.** What's in, what's out, what's adjacent-but-deferred. A
  one-sentence "this KB covers X" tightened until both reader types
  (future agents fixing bugs, humans writing papers/courses) can use it.
- **Topic name & slug (platform prefix rule).** Lock the slug *after*
  scope, because scope decides the prefix. Slugs are lowercase and
  hyphenated. If the KB's claims are **specific to one platform,
  runtime, or framework**, prefix the slug with the broadest such
  platform the whole KB is scoped to: `dotnet-`, `mobile-`, `wpf-`,
  `windows-`, `ios-`, `android-`, `web-`, etc. If the content is
  **platform-agnostic** (the math, physics, protocol, or algorithm
  holds regardless of language/OS/framework), use **no** prefix and
  name it by domain. Decision test: *"Would this KB's load-bearing
  claims change if we swapped the language, OS, or framework?"* Yes →
  prefix; No → no prefix. Examples — prefixed: `dotnet-high-perf`
  (GC/`Span`/SIMD are .NET), `mobile-edge-ml` (CoreML/LiteRT are
  mobile), `wpf-internals` (WPF). Unprefixed: `state-estimation`,
  `clock-sync`, `inertial-fusion`, `cross-language-schema-contracts`
  (estimator theory / PTP / IMU math / wire-format governance hold
  everywhere). A `.NET`-only mechanism such as P/Invoke marshalling or
  `SafeHandle` is platform-specific → `dotnet-`. Don't bury a real
  platform constraint under a generic name (a reader who needs the
  .NET answer must not have to guess the KB is .NET-shaped), and don't
  over-prefix a general topic (a Kalman-filter KB is not `dotnet-`
  just because our code is C#).
- **Citation contract.** Strict (the clock-sync default — no citation,
  no claim) or relaxed (more `[experience]` / `[uncertain]` allowed).
  This sets agent prompts for the rest of the build.
- **Company-content policy.** Segregated under `06-our-work/` (or
  similar) vs. excluded entirely. Affects every later bucket boundary.
- **Bucket taxonomy.** Not the clock-sync names by default. Pick what
  the topic actually needs. A foundations-style "timeless" layer and an
  adversarial "gaps and contradictions" layer are nearly always useful;
  everything else is topic-shaped. Most technical topics benefit from
  buckets covering multiple **layers of abstraction**: the *math*
  (algorithms, derivations, bounds), the *systems / architecture* (how
  components compose), the *physical reality* (what the underlying
  physics actually permits), and the *hardware / components* (real
  devices, real datasheets, real failure modes). A topic that only
  covers one of these layers is usually under-scoped.
- **Frontier and far-future stance.** Confirm whether the topic warrants
  a dedicated speculative bucket (clock-sync's `08-far-future/` is the
  prototype). If the topic has active fundamental research moving the
  frontier — quantum, novel-physics, paradigm-shift candidates — yes.
  If it's a mature engineering discipline with no near-term physics
  surprises, fold speculation into the SOTA wave instead of carving a
  separate bucket.
- **Downstream outputs.** KB itself is durable reference material. Ask
  the user *which derived artifacts they ultimately want to build from
  it* — research paper / slide deck / university course / workshop /
  public website / internal design doc. This shapes depth and
  organisation: a paper needs tight argument flow, a course needs
  layered prerequisites, a workshop needs runnable examples, a website
  needs cross-cutting navigation. Use the answer to **calibrate**
  scope and bucket structure. Do **not** scaffold an `outputs/`
  directory, templates, or skeleton artifacts as part of the KB
  build — outputs are built later, on demand, from the KB; never
  pre-built speculatively.

If any answer is hand-wavy, stop and tighten. Vague scope produces a
KB nobody trusts.

## 1.5. File the tracker (bead)

Once the interview is locked, open the bead before any agents spawn.
It is how your future self finds out this work happened.

### Beads (local)

```bash
br create --type task -p 2 "<topic-slug> KB"
# prints e.g. br-zfl
```

Record the bead id in `MASTER-PLAN.md`. Remove the candidate's row from
the ranked queue in `knowledge-base/BACKLOG.md` and add a line to the
**Graduation & decision log** in `knowledge-base/TOPICS.md`, linking the
bead id. (Bead state governs — never edit these files without moving the
bead.)

## 1.6. Lock the execution mode

One more interview question before sizing — how the build *runs* once
scope is locked. Ask with `AskUserQuestion`:

- **Gated (default).** Each wave is a user gate: you fire the wave
  Workflow, report its convergence deltas, and wait for `proceed` before
  the next wave. Safest; the user sees every wave's cost and output. Still
  workflow-driven *within* a wave — the gate is only *between* waves.
- **Autonomous-to-PR.** You drive all waves → adversarial → consolidation
  → register → squash → PR with best judgement, WITHOUT gating between
  waves. You escalate to the user ONLY on a *premise-invalidating* finding
  (scope is wrong; the topic overlap-duplicates a sibling KB; a
  load-bearing contradiction forces a taxonomy change). This mode requires
  explicit ultracode opt-in.

Record the chosen mode in `MASTER-PLAN.md`. Gated is the default whenever
the user has not opted into autonomy.

**Self-pacing in autonomous mode.** Fire each wave Workflow with
`run_in_background: true`; the main loop is re-invoked automatically when
it completes, so you read deltas and fire the next wave event-driven — no
polling. As a robustness backstop against a hung or silently-dead
workflow, schedule a long fallback wake-up (`ScheduleWakeup`, 1200s+) via
`/loop` dynamic mode; a short interval just burns cache for nothing since
completion already notifies you. The mode governs only whether you stop
*between* waves — the within-wave fan-out is the same Workflow either way.
