---
title: "Knowledge base topic registry"
id: kb-registry
schema_version: 1
doc_type: reference
status: stable
last_reviewed: 2026-09-06
tags: [registry, lookup, multi-topic]
---

# KB topic registry

The single lookup for which curated KBs exist and how to query them. One
row per built topic. Three levels of disclosure:

1. This file: find the right collection from the one-liner.
2. `qmd query "<question>" -c <collection> --limit 10` (always pin `-c`).
3. `<slug>/CLAUDE.md` (linked per row): the full bucket map, citation
   contract and anti-patterns. Read it once per session before deep work.

Related files: what to build next is in [`BACKLOG.md`](./BACKLOG.md); the
decision log is in [`TOPICS.md`](./TOPICS.md); the routing table with
trigger terms is in [`CLAUDE.md`](./CLAUDE.md).

Status legend: `stable` = converged and promoted; `converged` = build
converged, promotion pending; `building` = waves in progress.

## Firmware and hardware

| Topic | Query with `-c` | Covers | Status |
|---|---|---|---|
| [ESP32-S3 assembly optimization](./esp32s3-assembly-optimization/CLAUDE.md) | `esp32s3-assembly-optimization-kb` | Xtensa LX7 scalar and PIE vector kernels, the memory hierarchy, GCC 14 codegen, measurement, kernel patterns, this repo's kernel work | building |
