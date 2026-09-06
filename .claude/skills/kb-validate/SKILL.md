---
name: kb-validate
model: opus
description: >-
  Measure whether a KB, skill, or context-injection actually improves agent
  output on real tasks. A/B + blind-judge methodology — control (vanilla) vs
  treatment (with KB) across N trials × M files, scored by blind opus judges,
  aggregated with statistical confidence. Use when a KB has finished its
  build (e.g. via /kb-new-topic) and you want to know if it pays for itself
  before promoting it.
triggers:
  - "validate KB"
  - "measure KB impact"
  - "is the KB helping"
  - "A/B test the skill"
  - "kb-validate"
---

# kb-validate — measure KB / skill impact on real tasks

A KB or context-injection only earns its keep if it makes agents *measurably
better*. This skill is the experimental protocol for that measurement:
A/B + blind judge, replicated, randomized, statistically anchored.

## When to use

- A KB build has finished (e.g. via `/kb-new-topic`) and you want to know
  whether to promote it from `draft` → `stable` in `REGISTRY.md`.
- You've added a skill that injects context — does it help or is it noise?
- You're about to recommend a skill to teammates and want a defensible
  "yes, this lifts output quality" number behind it.

Do **not** use for:

- Citation correctness — that's the per-wave verification agent's job.
- Microbenchmarking specific code patterns — use BenchmarkDotNet.
- Subjective design / style review — use code review, not blind judges.

## Methodology — A/B + blind judge

For each trial of a campaign:

1. **Control agent** — opus, no KB access, audits the target file/task.
   Prompt explicitly forbids reading `knowledge-base/` or `.claude/skills/`.
2. **Treatment agent** — opus, *required* to read the KB or invoke the
   skill first, then audits the same target.
3. **Blind judge** — opus, reads target + both audits with A/B labels
   randomized per trial. Scores 5 dimensions (1–5 each), returns ONLY a
   structured SCORES block.

Audits write full reports to `/tmp/kb-eval/audits/`; judges write full
verdicts to `/tmp/kb-eval/judges/` and return only the score block. The
leader never sees the full audit prose — context stays clean across
dozens of trials.

## Trial sizing

| Goal | Trials | Files | Notes |
|---|---|---|---|
| Smoke test | 1 | 1 | Sanity only; do not trust |
| Directional | 3–5 | 1 | Signs whether KB helps; weak CI |
| **Recommended** | **5 × 3 = 15** | **3** | **p<0.05 for moderate effects** |
| Tight CI | 10 × 3 = 30 | 3 | Halves the SE; for marginal effects |
| Cross-domain | 5 × 5 = 25 | 5 | Tests generality across task shapes |

15 trials = 14 audit pairs + 14 judges = 42 opus agent runs ⊕ the prior
seed trial (if any). Roughly 30–60 min wall-clock at full parallelism;
single-digit USD on API spend.

## Target selection

Pick **real, hot, varied** files:

- **Real** — production code, not synthetic. KBs have to work where they count.
- **Hot** — files the KB is *for*. A perf KB validates on hot paths, not
  on a startup config loader. A domain KB validates on files that exercise
  the domain.
- **Varied** — different sub-domains within the KB's scope. If all 3 files
  surface the same finding, your N=15 is really N=3.

Three is the floor for "varied" — single-file results don't generalize.

## Randomized A/B assignment

Per trial, independently decide whether the treatment audit is labelled
A or B in the judge prompt. Balance ~50/50 across the campaign so any
positional bias in the judge averages out. Record the assignment per
trial *before* spawning judges — you'll need it to decode results.

## On-disk layout

```
/tmp/kb-eval/
├── audits/
│   ├── <file>-trial-<N>-control.md
│   └── <file>-trial-<N>-treatment.md
├── judges/
│   └── judge-<file>-<N>.md
├── aggregate.py        # records randomization + parses scores
└── REPORT.txt          # final aggregate output
```

`/tmp/` not the repo — the campaign is ephemeral. Commit only the final
report or skill update, not the raw audit transcripts.

## SCORES block contract (judges return this and nothing else)

```
SCORES
correctness:  A=<1-5> B=<1-5>
depth:        A=<1-5> B=<1-5>
mechanism:    A=<1-5> B=<1-5>
coverage:     A=<1-5> B=<1-5>
antipattern:  A=<1-5> B=<1-5>
total:        A=<sum> B=<sum>
winner: <A|B|TIE>
margin: <int>
key_unique_A: <one line>
key_unique_B: <one line>
```

The full verdict goes to disk. The block stays inline so the leader can
parse 15+ scores without context bloat.

## Dimensions — adapt to domain

Five worked for a .NET perf KB. Keep at five; judges start randomizing
above that. Substitute domain-appropriate axes:

- **Correctness** — are claims true? (universal)
- **Depth** — surface vs mechanism (universal)
- **Mechanism** — does the fix explain *why*? (technical KBs)
- **Coverage** — load-bearing issues caught? (universal)
- **Anti-pattern awareness** — avoids known-bad recommendations? (KB-specific)

Other axes that may fit: actionability, citation quality, novelty,
applicability, safety.

## Aggregation

Per dimension and per file, compute:

- Mean Δ (treatment − control)
- Std dev, std error, t-statistic, 95% CI
- Wins / losses / ties

Significance bars for N=15 (df=14):

- p<0.05: |t| > 2.145
- p<0.01: |t| > 2.977

If the **coverage** dimension loses while depth/mechanism win, that is a
KB **failure mode** worth surfacing: KB-aware agents tend to spend
context budget on mechanism and miss surface scans. Consider a two-pass
workflow (vanilla pass first, KB pass on top findings) and bake it into
the skill — that improves the KB by changing how it's invoked, not its
content.

## Aggregate script

Drop-in `aggregate.py` template at
`.claude/skills/kb-validate/templates/aggregate.py.tmpl`. Copy to
`/tmp/kb-eval/aggregate.py`, fill `TREATMENT_SIDE` with the campaign's
randomization, paste each judge's SCORES block into `RAW`, run.

## Anti-patterns

- **Same prompt across trials.** Variance is mostly LLM sampling noise;
  CIs at N=5 per file are wide. Don't claim wins beyond what t-tests support.
- **Judging totals only.** The per-dimension breakdown is where the
  actionable story lives ("KB helps mechanism +1.1, hurts coverage −1.1").
  Skip it and the campaign reduces to "did it win or not."
- **No A/B label randomization.** Judges have order bias. Force ~50/50.
- **Trusting a single trial.** Single trials are session-prompt-temperature
  noise. N≥5 per file is the floor; N=15 total is the recommended floor.
- **Same model for audit and judge with no sanity check.** Acceptable as
  the main run, but consider one alternate-model judge pass on a few
  trials as a calibration on judge bias.
- **Acting on the verdict without root-cause review.** If treatment lost
  on a trial, read `key_unique_A` from the judge — was it (a) the KB
  applied a pattern wrongly (dangerous; fix the KB) or (b) the auditor
  happened to miss something orthogonal (noise; ignore)? Only (a)
  warrants a KB revision.
- **Worktree isolation for audit/judge agents.** They write files the
  leader reads back; isolation breaks the pipeline. Plain spawn.
- **Letting one outlier dominate per-file mean at N=5.** Report median
  alongside mean when std exceeds 3.

## Run procedure

1. **Pick targets** — 3 real hot files in the KB's domain. Verify with `wc -l`.
2. **Set up workspace** — `mkdir -p /tmp/kb-eval/{audits,judges}`.
3. **Decide randomization** — write down per-trial A=control or A=treatment
   to balance ~50/50.
4. **Spawn 2N audit agents in one message** — parallel, opus, distinct
   output paths per `<file>-trial-<N>-{control,treatment}.md`. Each
   agent returns a 3-line summary; full reports go to disk.
5. **Wait for all audits** — context-clean because reports are on disk.
6. **Spawn N judge agents in one message** — parallel, opus, with the
   randomized A/B file paths. Each returns ONLY the SCORES block.
7. **Aggregate** — drop the SCORES blocks into `aggregate.py`, run, save
   `REPORT.txt`.
8. **Interpret** — overall t-stat, per-file medians, per-dimension
   pattern. Decide: promote, refine, or shelve.
9. **Write a one-page verdict** for the user. Per-file table, per-dimension
   table, one-line headline, one-paragraph honest reading, named failure
   modes if any.

## Reference run

Canonical execution: **dotnet-high-perf KB validation, 2026-05-27.**

- 3 files (`PressureDataUtils.cs`, `PressureIslandCluster.cs`,
  `CenterOfPressureAnalysisPressure.cs`) × 5 trials × 3 agents
  (control + treatment + judge) ≈ 45 agent runs.
- N=15, mean Δ=+2.13, t=2.17, **p<0.05**. Treatment 12 wins / 2 losses / 1 tie.
- Per-dimension: KB lifts **mechanism +16**, **anti-pattern +13**,
  **depth +11**, **correctness +5**, but hurts **coverage −16**.
- Surfaced "two-pass workflow" as a future KB-improvement TODO:
  vanilla audit first for breadth, KB-aware pass on top findings for
  mechanism depth.

This is the shape the skill is built around. When in doubt about a
parameter choice, do what that run did.
