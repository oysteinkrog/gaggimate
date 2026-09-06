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

## Follow-ups inside built topics

Gaps the build of a topic recorded but did not fill. Each is a reserve wave
for that topic, not a new topic.

| Topic | Gap | Where recorded |
|---|---|---|
| `esp32s3-assembly-optimization` | One complete PIE kernel walked from portable C to a bit-exact vector loop (prologue, alignment guard, main loop, tail) as a template | `06-kernel-patterns/README.md`, `10-synthesis/README.md`, `COMPENDIUM.md` |
| `esp32s3-assembly-optimization` | A device run of the two experiments QEMU cannot answer: the `sel2` operand of `EE.SRCMB.*` and `EE.SRS.ACCX`, and the `SSL`-then-`EE.VSL.32` stale-shadow case | `09-adversarial/README.md` |
| `esp32s3-assembly-optimization` | Measured cycle costs on the board for the scalar multiply, divide and FP seed sequence, the window overflow handler, and a flash or PSRAM cache miss; these are 60 of the open uncertainty tags | `09-adversarial/uncertainty-register-what-the-corpus-does-not-know.md` |
| `esp32s3-assembly-optimization` | A measured GCC 14 against esp-clang comparison on one kernel, and a GCC 15 or 16 rebuild of the kernel set | `08-frontiers/README.md`, `10-synthesis/what-would-change-the-answers.md` |
| `esp32s3-assembly-optimization` | Our-work leaves on the flash-cache patch and the overlay and scrim kernels | `07-our-work/README.md` |
