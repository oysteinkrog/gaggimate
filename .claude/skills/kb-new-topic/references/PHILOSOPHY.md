# kb-new-topic — goal, stance, and the one rule

A KB is only useful if it's right *and* deep. Most of the cost is in
scoping correctly, going deep enough to reach the frontier, and
rejecting hallucinated content — not in writing files. The scaffold
pattern from `knowledge-base/clock-sync/` is *one* shape; copy the bits
that fit, drop the bits that don't.

## Goal — why we build KBs

A topic KB exists for two reasons. Every later decision (scope, wave
count, model mix, citation contract) trades against these:

1. **Efficiency for downstream agents.** A future agent fixing a bug or
   designing a feature should find the answer in one `qmd query` instead
   of spending tokens re-researching the web. The KB is the answer to
   "we already paid to learn this; don't pay again."
2. **Depth at the frontier — and past it.** When the topic has a SOTA,
   the KB carries it. Academic preprints, primary literature, IEEE/ACM
   proceedings, metrology reports — whatever the absolute frontier of
   human knowledge on the topic is, the KB pulls it in so our agents
   pick the *best* math/algorithm/architecture, not the first one a
   generic LLM remembers. The bar is **ULTRA-SOTA**: not just a clean
   summary of today's frontier, but explicit synthesis of where the
   frontier is *moving* — academic work that improves on current SOTA,
   contradictions across competing approaches, and speculative
   far-future directions worth tracking. A KB that only describes "what
   exists" leaves the synthesis work undone.

These are non-negotiable. A KB that only summarizes Wikipedia-tier
material wastes everyone's time. A KB that is deep but hallucinated
poisons downstream decisions. The process below is the cheapest way to
satisfy both.

## Your stance as leader

You are the **coordinator** for the whole build, not a contributor to
individual buckets. Your job across every wave: plan the next batch,
spawn it, read the outputs, decide what's still missing, gate on the
user, repeat. Resist the urge to write KB content yourself between waves
— your bandwidth is best spent on planning, synthesis, and verification.
Individual leaf files are agent work; the convergence judgement is
yours.

## The one rule

Every claim in the curated buckets must trace to a verifiable source.
Hallucinated citations destroy the corpus and break Goal #2 (frontier
depth needs trustworthy citations to be useful at all).
