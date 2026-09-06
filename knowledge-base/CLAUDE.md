# knowledge-base/ — Multi-topic knowledge base

This directory hosts curated, citation-strict knowledge bases. Each topic
lives in its own subdirectory and is registered as its own qmd collection.
This level holds only shared infrastructure; topic content never lives here.

The layout, the scripts and the skills were ported from the Initial Force
monorepo's `knowledge-base/` on 2026-09-06. Same contract, fewer topics.

## Topics: work-domain to collection routing

Query a collection with `qmd query "<question>" -c <collection> --limit 10`
(always pin `-c`). Read the linked harness for that topic's bucket map and
citation contract before deep work.

| When your work touches… | Collection | Harness |
|---|---|---|
| Xtensa LX7 assembly, PIE vector kernels, ESP32-S3 cycle budgets, cache and PSRAM costs, GCC 14 Xtensa codegen, inline asm, measuring a kernel on the device or in QEMU | `esp32s3-assembly-optimization-kb` | [harness](./esp32s3-assembly-optimization/CLAUDE.md) |

When a new topic is added, give it a sibling directory, its own `CLAUDE.md`,
its own bucket layout, and its own `<topic>/scripts/install-collection.sh`.
Add a row here and in `REGISTRY.md`.

**Topic naming.** Slugs are lowercase and hyphenated. If a topic's
load-bearing claims are specific to one platform, prefix the slug with that
platform (`esp32s3-`, `esp-idf-`, `lvgl-`, `web-`). If the content holds
whatever the chip or framework (a protocol, an algorithm, the physics), use
no prefix. Test: would the load-bearing claims change if we swapped the
chip, OS or framework? Yes: prefix. No: no prefix.

## Shared infrastructure (kept here)

| Path | Purpose |
|---|---|
| `REGISTRY.md` | Where to look up a built KB: one row per built topic. Start here when you need a collection to query. |
| `BACKLOG.md` | What to build next: the ranked queue of un-built candidate topics. |
| `TOPICS.md` | Topic history: the graduation and decision log. Not a queue, not the registry. |
| `.schemas/base.schema.json` | JSON Schema for frontmatter. One base schema; a topic that needs extra fields adds a `<bucket>.schema.json` with `allOf` and registers it in the validator's `BUCKET_TO_SCHEMA`. |
| `scripts/validate-frontmatter.py` | Validator. Walks any topic subtree; finds `kb_root` via `.schemas/`. Unknown buckets use the base schema. Runs with `uv run` or plain `python3` (needs `jsonschema`, `referencing`, `pyyaml`). |

## Working inside a topic

Every topic subdirectory has its own `CLAUDE.md` with the full agent
contract for that topic: bucket layout, citation rules, anti-patterns, qmd
registration. Read it once per session before touching the topic's files.

Skills:

- Query craft and routing, any topic: `/kb-query`
- Bootstrap a new citation-strict topic: `/kb-new-topic`
- Measure whether a KB pays for itself (A/B, blind judge): `/kb-validate`
- Standing curation conventions: the `kb-curator` agent

## Shared agent conventions (every topic inherits these)

A topic's `CLAUDE.md` states only its topic-specific additions and points
back here for the rest. Do not re-copy this text into a topic file.

### Anti-patterns (apply everywhere)

- **No editorial markup.** Never strikethrough, "previously this said",
  "removed by wave X", "TODO before publication". Git history handles that.
- **No "added in round N" comments.** Provenance lives in the topic's
  bibliography and the git log, not in prose.
- **No emoji or ASCII-art decoration.**
- **No company- or repo-specific content outside the topic's designated
  our-work bucket** (conventionally `NN-our-work/`; check the topic's own
  layout table for the number).
- **No fabricated citations.** If you cannot resolve the DOI, URL, manual
  section or source file, mark the claim `[uncertain]` or remove it. The
  same goes for invented numbers. A wrong citation is worse than none.
- **No "currently" without a year.** Anchor temporal claims to a year and,
  where relevant, a tool or document version: "as of 2026, GCC 14.2".
- **No dump filenames.** `notes.md`, `temp.md`, `wip.md` never belong in a
  bucket.
- **No deletion without supersedes.** Sunset a file by setting
  `status: superseded` and `superseded_by` in frontmatter.
- **No em dashes.** Plain sentences, the same rule as the rest of this repo.

### Frontmatter validation

Every content `.md` file must validate against `.schemas/base.schema.json`
(harness files `CLAUDE.md` and `AGENTS.md` are exempt). Validate before
commit:

```bash
python3 knowledge-base/scripts/validate-frontmatter.py knowledge-base/<topic>/
```

Treat warnings as commit-blockers.

### Retrieval: finding what already exists

Search before writing; extend or supersede rather than duplicate.

```bash
qmd query "<question>" -c <topic>-kb --limit 10   # hybrid search
qmd ls <topic>-kb/<bucket>/                        # browse a bucket
qmd update && qmd embed -c <topic>-kb              # index after curated changes
```

`<topic>/scripts/install-collection.sh` registers that topic's collection on
a fresh checkout. On this machine `qmd` is already on PATH (`qmd 2.0.1`).

## Anti-patterns at the multi-topic level

- Do not write topic content at `knowledge-base/<file>.md`.
- Do not duplicate `.schemas/` or `scripts/validate-frontmatter.py` per
  topic. Topic-specific scripting goes under `<topic>/scripts/`.
- Do not assume `qmd` is configured. Each topic's install script registers
  its collection.
