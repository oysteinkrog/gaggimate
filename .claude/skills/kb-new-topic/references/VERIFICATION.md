# 4. Verification — finding the hallucinations

This is where the KB earns its trust. With the workflow model,
verification is **built into every wave by construction** — it is the
verify and fix stages of the pipeline, not a step you might forget.

- **Triple-lens verify, per leaf, every wave.** The wave Workflow's
  stage-2 runs three *independent* verifiers on each leaf the moment it
  is written, each blind to the others (fidelity on opus, resolvability
  and coherence on sonnet — see §3):
  - **resolvability** — every DOI/URL resolves, every SHA exists, every
    `file:line` is real; a dead or placeholder reference fails;
  - **fidelity** — open each cited source and confirm it *actually says*
    what the leaf claims (the specific number/quote/section) — "source
    exists but says something else" is the failure to catch;
  - **coherence** — the leaf is internally consistent and not contradicted
    by any sibling leaf in the corpus.
  Each verifier defaults to `pass=false` when it cannot positively
  confirm. Stage-3 then applies surgical fixes for any P1/P2 finding, or
  marks the claim `[uncertain]`. The aggregate lands in the wave's
  returned `audit` block — that is your per-wave citation-health readout.
- **Load-bearing claims get the fidelity lens independently.** Numbers,
  bounds, "X achieves Y ns" — anything the user would cite back at a
  customer — are exactly what the fidelity verifier opens the source to
  confirm. Two independent confirmations or it gets `[uncertain]`.
- **Adversarial wave is still non-optional.** Even on a small topic. The
  per-leaf coherence lens catches *local* contradictions; a dedicated late
  adversarial wave (an adversarial-type Workflow that surveys the whole
  corpus instead of writing new leaves) catches the cross-bucket gaps and
  contradictions the per-leaf view cannot see. One agent (opus, for the
  load-bearing reasoning when the corpus is large and contested) whose only job is "find
  contradictions across these files, find unsupported assertions, find
  citations that don't say what the prose claims". Run it before
  consolidation, not after.
- **Reject quietly-recurring patterns.** If three agents independently
  cite the same paper with subtly different DOIs, one of them is
  wrong. Stop and resolve before the consolidation wave bakes the
  error in.
- **No batch curation without spot-checks.** During the consolidation
  wave, the leader (you) reads at least one leaf file per bucket and
  walks its citations end-to-end. If three random spot-checks pass,
  the bucket is probably fine. If one fails, audit the whole bucket.

LLMs hallucinate references with high confidence. The defense is
redundancy + an explicit "could you not find a source?" agent per
batch. A KB that says `[uncertain]` honestly beats one that fabricates.
