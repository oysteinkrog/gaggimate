---
title: "Knowledge base topic backlog"
id: kb-backlog
schema_version: 1
doc_type: explanation
status: draft
last_reviewed: 2026-09-06
tags: [planning, roadmap, multi-topic]
---

# KB topic backlog: what to build next

The single ranked queue of candidate KB topics for this repo. Before
building anything, check [`REGISTRY.md`](./REGISTRY.md): if a collection
exists, the topic is done.

A topic earns the citation-strict treatment only if it is durable (a year
without going stale), citation-strict (every load-bearing claim traces to a
verifiable, version-anchored source) and synthesised rather than a copy of
one manual.

## Ranked queue

| Rank | Candidate slug | Why | Notes |
|---|---|---|---|
| 1 | `esp32s3-rgb-panel-scanout-and-dma` | The bounce-buffer, PSRAM-bandwidth and interrupt-placement history in the root `CLAUDE.md` is already a KB in draft form. | Hardware invariants section of `CLAUDE.md` is the seed. |
| 2 | `esp-idf-internal-dram-and-wifi-coexistence` | The DRAM budget, lwIP and WiFi buffer rules cost weeks to learn and are version-specific. | `sdkconfig.gaggimate.defaults` comments are the seed. |
| 3 | `lvgl8-render-pipeline-and-invalidation` | Dirty-rect handling, meter invalidation and overlay compositing decisions recur on every UI change. | UI-pipeline invariants section of `CLAUDE.md`. |
| 4 | `ble-coexistence-timing-on-esp32` | Controller link timing versus radio coexistence is measured but undocumented as a corpus. | Open cleanups in `CLAUDE.md`. |
