---
title: "ESP32-S3 assembly optimization KB: convergence report"
id: final-convergence-report
schema_version: 1
doc_type: reference
status: review
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, convergence, report]
---

# Convergence report

Built 2026-09-06 in one session for bead gm-08i, from the plan in
[`MASTER-PLAN.md`](./MASTER-PLAN.md). This file says what the build
produced, how it was checked, what changed under checking, and what it
left open. The wave-by-wave log is [`PROGRESS.md`](./PROGRESS.md).

## What exists

| Bucket | Leaves | Second reader |
|---|---|---|
| 00-foundations | 4 | verified |
| 01-scalar-isa | 6 | verified |
| 02-pie-vector | 4 | verified |
| 03-memory-hierarchy | 2 | verified |
| 04-toolchain-and-codegen | 5 | verified |
| 05-measurement | 4 | verified |
| 06-kernel-patterns | 4 | verified |
| 07-our-work | 9 | writer only |
| 08-frontiers | 5 | verified |
| 09-adversarial | 4 | writer only |
| 10-synthesis | 3 | writer only |

Fifty leaves, every bucket indexed by a README with a key-claims column,
plus [`COMPENDIUM.md`](./COMPENDIUM.md) (forty facts, one link each),
[`sources.md`](./sources.md) (the bibliography) and this report. About
18,600 lines in all. The bibliography resolves 745 footnotes to 60 distinct
external sources, 13 of them primary documents and 13 source trees pinned by
tag or commit; 107 more footnotes are the our-work bucket citing this repo's
own files and commits. The corpus carries 153 `[measured]` tags, each with
the command that produced the number, and 138 `[uncertain]` tags in
buckets 00 to 08, every one listed with what would settle it in
[the uncertainty register](./09-adversarial/uncertainty-register-what-the-corpus-does-not-know.md).

The qmd collection `esp32s3-assembly-optimization-kb` is registered and
embedded (636 chunks from 61 documents at the first index; re-run
`scripts/install-collection.sh` after the final commit).

## How it was built

Six waves, each a set of parallel one-leaf writers with a written brief,
then a leader pass that routed cross-leaf findings and committed. Sonnet
wrote most leaves; Opus wrote the hazard tables, the header-versus-tool
reconciliation, the P4 leaf, the myths, the contradiction log and the
synthesis. Writers never committed. Seven verifiers then read buckets 00 to
06 leaf by leaf against the local manual texts, the ESP-IDF 5.5.1 tree, the
installed GCC 14.2 toolchain and live fetches; four more did the same for
the ten wave 4 leaves. Every `[measured]` compile was re-run; every manual
page cite was checked in the extracted text.

## What checking changed

The verify pass corrected eight load-bearing claims, logged in
[the contradiction log](./09-adversarial/contradiction-log-sources-that-disagree.md)
with the error kind of each. The ones a reader is most likely to have
believed:

- `memcpy` on the ESP32-S3 is a ROM routine linked by an absolute symbol,
  not the toolchain's newlib object. Two leaves and the leader's own brief
  had it wrong.
- After `call8` the return value lands in `a10` to `a13`, not `a2` to `a5`.
- The 256-byte loop body limit is an assembler relaxation, not a compiler
  refusal, and the relaxed sequence restores the count register.
- GCC 14.2 does reach MAC16 from plain C for one loop shape.
- The esp-clang fork exposes 252 PIE builtins, not 269 (a line count).
- About 35 ISA manual page cites in the branches leaf were off by one.
- New in the P4 leaf: ESP-IDF 5.5.1's RISC-V context switch saves seven of
  the eight P4 vector registers and drops `q3`; upstream fixed it, this
  branch does not have the fix.

The verifiers also found the TRM contradicting itself four times and the
datasheet disagreeing with the TRM on the data cache geometry; those are
recorded as manual defects, with the assembler or the chip as tiebreaker.

Error kinds, ranked by count: a source that existed and was not opened
(three), a rule generalised from another core or another view (two), an
experiment not run or miscounted (two), a filename produced from a naming
pattern rather than a listing (one).

## What the build settled

The facts that changed the leader's own understanding, each carried by a
leaf and restated in the compendium:

- This core has no Xtensa cache option at all. Every cache instruction fails
  to assemble; the cache is a peripheral reached through ROM functions.
- GCC 14 emits no PIE instruction and has no vector register class, so a
  `q` clobber cannot be written and is not needed; its own `__XCHAL_HAVE_CLAMPS`
  is 0 against the header's 1, so `clamps` is never emitted either.
- PIE loads and multiplies define at stage 2, adds and logic at stage 1,
  and MAC chains are free; the four FFT store forms and `EE.VRELU` are the
  exceptions. A taken branch is 2 cycles and there is no predictor.
- QEMU has no timing model anywhere and six open PIE semantics issues; the
  CPENABLE reset divergence has a root cause in an open pull request.
- ESP-IDF 6.x already ships GCC 15.2, so the GCC 14 baseline every kernel
  here was measured against is the first thing that will move.

## What it left open

Recorded rather than fixed, with the follow-up rows in
[`../BACKLOG.md`](../BACKLOG.md):

- The our-work, adversarial and synthesis buckets had one reader each. A
  second pass over them is the next verification step.
- No leaf walks one complete PIE kernel from portable C to a bit-exact
  vector loop; three bucket indexes name this gap independently.
- The per-instruction scalar latencies, the multiplier reservation table,
  the window overflow cost and the cache-miss cost are the uncertainties
  that would change the most decisions, and all need a device run.
- Two QEMU questions need silicon: the `sel2` operand and the stale
  shift-shadow case.
- No measured comparison of esp-clang against GCC 14 exists anywhere.

## Status

Registry status is `converged`. Promotion to `stable` should follow the
second-reader pass over buckets 07, 09 and 10 and the first device run
against the uncertainty register's top ten.
