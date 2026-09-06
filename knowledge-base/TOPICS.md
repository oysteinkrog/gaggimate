---
title: "Knowledge base topic history"
id: kb-topics
schema_version: 1
doc_type: reference
status: stable
last_reviewed: 2026-09-06
tags: [history, decisions, multi-topic]
---

# KB topic history

The record of what happened and why. Not a queue (that is `BACKLOG.md`)
and not the registry (that is `REGISTRY.md`). In-progress state lives in
bead status (`br`) and each topic's `PROGRESS.md`.

## Scope rule

A KB topic is durable domain knowledge that outlives one bug or one
feature. Product state, bead lists, code style and single-vendor manuals
are not KB topics.

## Graduation and decision log

| Date | Topic | Decision | Bead |
|---|---|---|---|
| 2026-09-06 | `esp32s3-assembly-optimization` | Harness ported from the Initial Force monorepo; topic scaffolded and build started. Strict citation contract, our-work bucket `07-our-work/`, platform prefix `esp32s3-`. | gm-08i |
