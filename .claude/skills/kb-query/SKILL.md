---
name: kb-query
model: haiku
description: Look things up in a registered qmd knowledge-base collection (hybrid BM25 + vector + reranker). Topic-agnostic — routes across the in-repo KBs under knowledge-base/ (work-domain → collection table in knowledge-base/CLAUDE.md). Use whenever an agent needs to ground a claim, fix a bug from documented prior-art, or quote a number from a curated source.
triggers:
  - "search the KB"
  - "search the knowledge base"
  - "qmd query"
  - "what does the KB say about"
  - "find in the KB"
  - "look up in the KB"
---

# kb-query — querying a curated knowledge base

This skill is about *using* qmd as a query engine over a registered
collection. It is platform-neutral and does not install anything.

## Which collection — route first, then query

A `-c <collection>` is mandatory (see "Pinning" below). Two ways to find
the right one, both count-independent as topics multiply:

- **`qmd collection list`** — the live registry of what's indexed right now.
  Collection names are self-descriptive (`force-measurement-kb`,
  `mobile-app-platform-kb`, …).
- **The work-domain → collection routing table** in
  `knowledge-base/CLAUDE.md` — maps "I'm working on X" to the matching `-kb`,
  and flags any topic that's on disk but not currently registered.

If still unsure, run a bare `qmd query "<phrase>"` (searches every registered
collection) and see where the hits land, then re-query pinned. Each topic's
bucket layout and citation contract live in its own
`knowledge-base/<topic>/CLAUDE.md` — read that before deep work in a topic.

> If qmd is not on PATH, or `qmd update` does not show `[embed]` lines,
> install or repair your qmd setup before running queries. The install
> path depends on platform — see `/setup-qmd-wsl1-wrapper` for the WSL1 +
> Windows-side CUDA case, or upstream `@tobilu/qmd` docs for native
> Linux / macOS / WSL2.

## Quick reference

```bash
qmd collection list                                       # show registered collections
qmd collection show <name>                                # show one collection (Path: ...)
qmd query "<question>"                                    # search every registered collection
qmd query "<question>" -c <name> --limit 5                # pin collection + limit
qmd ls <name>/<bucket>/                                   # browse files under a bucket
qmd multi-get '<name>/<bucket>/**/*.md'                   # batch-fetch by glob
qmd open "qmd://result/<n>"                               # open hit n from last query
qmd update                                                # rebuild lexical index (all collections)
qmd embed -c <name>                                       # rebuild embeddings for one collection
```

> `qmd query` does **not** take a path / prefix filter — the only scope flag
> is `-c, --collection <name>`. To bias search toward one bucket, include
> bucket-relevant keywords in the query itself (e.g. add the bucket slug
> or its dominant terms). To enumerate by path, use `qmd ls` or
> `qmd multi-get` instead of `qmd query`.

## Query craft

The reranker rewards specificity. Phrase the question as a focused
noun-phrase, not a bare keyword:

| Weak | Strong |
|---|---|
| `"PTP elections"` | `"PTP best master clock algorithm tiebreaker"` |
| `"force plate sync"` | `"force plate latching window in biomechanics capture"` |
| `"Allan"` | `"modified Allan deviation for OCXO short-term drift"` |

Aliases matter — collections usually carry both the acronym and the
spelled-out form. If the first query comes back empty, widen by dropping
adjectives or trying the alias before assuming the topic is absent. Every
KB has its own alias pairs; examples by topic:

- clock-sync: `WR`/`White Rabbit`, `gPTP`/`802.1AS`, `ADEV`/`MDEV`/`Allan`, `BMCA`/`best master clock algorithm`
- dotnet-high-perf: `GC`/`garbage collection`, `SIMD`/`vectorization`, `ArrayPool`/`pooling`
- mobile-app-platform: `JSI`/`JavaScript Interface`, `TurboModule`/`native module`, `Fabric`/`new renderer`, `CNG`/`continuous native generation`
- biomechanics: `JCS`/`joint coordinate system`, `ISB`/`recommendations`, `BSP`/`body-segment parameters`

## Pinning the collection

Never assume the "active" collection — agents come and go and the
default can drift. Pin explicitly for every query:

```bash
qmd query "<phrase>" --collection clock-sync-kb --limit 8
```

The clock-sync KB's collection name is `clock-sync-kb`. The convention
for this repo is `<topic-slug>-kb` (e.g. a future `imu-fusion/` topic
would register as `imu-fusion-kb`) — the topic directory under
`knowledge-base/` and the qmd collection are deliberately decoupled so
the collection name can stay stable when a topic is renamed.

## Reading hits

KB files carry YAML frontmatter (`id`, `doc_type`, `last_reviewed`,
`sources`, plus bucket-specific fields). Read these before quoting:

- `last_reviewed` older than ~12 months → flag the staleness to the
  user, even if the content looks right
- `status: superseded` → follow `superseded_by` to the replacement
- `sources` lists primary citations; quote footnotes verbatim from the
  body, do not paraphrase citation keys
- `doc_type` tells you what the file is for: `tutorial` walks a
  procedure, `how-to` is a recipe, `reference` is structured facts,
  `explanation` argues a rationale

## Citation discipline

Each KB defines its own citation contract in its `CLAUDE.md` — read it
before quoting. All eight are *citation-strict*; the shared invariants:

- Every non-obvious claim carries a verifiable footnote
- Personal-experience claims are marked `[experience]`
- Plausible-but-unverified claims are marked `[uncertain]`
- Standards include the revision year (e.g. IEEE 1588-2019)
- RFCs include the number and year

Provenance style varies by topic: the science KBs pin peer-reviewed
citations; fast-moving framework KBs (mobile-app-platform) pin a
**version + retrieval-date** tag (e.g. `[RN 0.76 / Expo SDK 52,
retrieved 2026-06]`) because the underlying APIs churn every few months.
Either way, the rule is identical: trace to a real source, never invent.

When propagating KB content into a PR, design doc, or user-facing
answer, preserve the footnote intact. Never invent a source the KB
does not contain.

## Indexing after edits

If you (or another agent) edited KB content, the lexical index is
stale until refreshed:

```bash
qmd update                          # lexical reindex (fast)
qmd embed --collection <name>       # rebuild embeddings (slower, GPU)
```

Run both after a batch of curated changes. Each topic ships its own
one-shot helper at `knowledge-base/<topic>/scripts/install-collection.sh`
that registers the collection (if new) and rebuilds both indices. Re-run
it from a checkout that has the files whenever a collection 404s (the
qmd index is global-per-user and pinned to one worktree path, so it can
drop out when that worktree moves — see the † topics above).

## Biasing toward a bucket

When the user is fuzzy about what to search for, bias the query toward a
top-level bucket by including bucket-relevant terms. Bucket names alone
are weak ranking signals — pair them with a content keyword. Every topic
numbers its buckets (`00-foundations/`, … `10-adversarial/`), but the
*names* differ per topic — read `knowledge-base/<topic>/CLAUDE.md` for
the layout. Pattern (clock-sync shown as the example):

```bash
qmd query "metrology Allan variance foundations" -c clock-sync-kb         # 00-foundations
qmd query "PTP gPTP standard revision year"      -c clock-sync-kb         # 01-standards-protocols
qmd query "tier decision tree synthesis"         -c clock-sync-kb         # 09-synthesis
```

To browse a bucket directly (no search), use `qmd ls` / `qmd multi-get`
against any collection:

```bash
qmd ls mobile-app-platform-kb/02-native-boundary/             # list files
qmd multi-get 'dotnet-high-perf-kb/**/COMPENDIUM.md'          # dump bodies by glob
```

## Per-topic orchestrators

For an end-to-end "I have a bug or design question in this domain, walk
me through it" flow, a topic *may* ship a dedicated orchestrator skill
that chains queries with an apply-and-test workflow. Currently only
**clock-sync** has one: `/kb-clock-sync`. For every other topic, drive
the queries above directly (or read the topic's `CLAUDE.md` and
COMPENDIUM first to orient).
