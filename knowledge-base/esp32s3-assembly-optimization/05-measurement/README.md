---
title: "05-measurement: bucket index"
id: 05-measurement/readme
schema_version: 1
doc_type: reference
status: review
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, measurement, ccount, qemu, testing, benchmarking]
---

# 05-measurement: bucket index

How to put a trustworthy number on a kernel, and how to prove a kernel
computes the right answer before that number matters. The counter and its
traps, what QEMU can and cannot tell you, how to test for bit-exactness and
fuzz for correctness, and why a host build's timing is a different
question from a device measurement. The instruction facts these leaves
lean on live in `01-scalar-isa/` and `02-pie-vector/`; the memory-hierarchy
facts live in `03-memory-hierarchy/`; the decision rules built on this
bucket's methods live in `10-synthesis/`.

| File | Topic | Key claims |
|---|---|---|
| [ccount-cycle-counter-and-timing-a-kernel.md](./ccount-cycle-counter-and-timing-a-kernel.md) | The `CCOUNT` register, its accessors, the wall-clock alternative, what perturbs a reading, and a working harness | `CCOUNT` is special register 234 (Timer Interrupt Option, ISA manual Section 4.4.6, Table 5-175), reachable through `esp_cpu_get_cycle_count()`. It is 32 bits, wraps in about 17.9 s at 240 MHz, is per-core, and moves with CPU frequency (dynamic frequency scaling, light sleep, and a start-up rescale all affect it, confirmed line-for-line against ESP-IDF 5.5.1). `esp_timer_get_time()` is the alternative: microsecond API over a 16 MHz systimer tick, correct across a frequency switch but far coarser. Min-of-n is right for deterministic compute and wrong for cache-miss-bound code, where mean and spread are the honest report. A full harness (calibration loop, sink, compiler barrier) is given, plus the list of what a result must state to be reproducible. |
| [qemu-esp32s3-what-it-proves-and-what-it-cannot.md](./qemu-esp32s3-what-it-proves-and-what-it-cannot.md) | The Espressif QEMU fork's ESP32-S3 machine: what it emulates, why its `CCOUNT` and clock are not the device's, what it is good for, and its documented PIE divergences from silicon | The core model implements the PIE extension in bulk (258 mnemonics as of commit `ba59503`, verified by direct extraction) but times nothing: `CCOUNT` tracks a 40 MHz virtual clock (`clock_freq_khz = 40000`, confirmed in `core-esp32s3.c`), not the real 240 MHz, and the cache model has no wait states. Six open PIE-divergence issues (#161-#166, opened 2026-08-02, all verified open with matching descriptions) name saturation, SAR-at-32, destination aliasing and dynamic-shift-count bugs to distrust. GDB cannot read the 128-bit `q` registers (returns zero; store to memory and inspect instead) but can read `ACCX`/`QACC` as user registers. A "what to prove where" table closes the leaf. |
| [bit-exact-reference-tests-and-fuzzing.md](./bit-exact-reference-tests-and-fuzzing.md) | Differential testing against a portable reference twin, input selection, golden outputs and tolerance, host fuzzing with sanitizers, and the five-layer evidence ladder | Keep a portable twin next to every hand-written kernel; on disagreement the twin is the specification until proven otherwise. Four input classes (full-range sweep, seeded random, parameter extremes, adversarial shapes) cover different bug classes; a split-buffer property test catches state that quietly leaks across calls. AddressSanitizer and UndefinedBehaviorSanitizer (both confirmed against current Clang and GCC documentation) are required, not optional, for fuzzing to be worth anything: an unpadded table overrun reads a plausible neighboring byte and passes without them. The five-layer ladder (host fuzz, cross-compiled assembly, QEMU bit-exactness, device bit-exactness, device timing) is the order evidence accumulates; a later layer's disagreement is a defect, never a reason to skip an earlier one. |
| [host-benchmarks-versus-the-device.md](./host-benchmarks-versus-the-device.md) | Why an x86-64 host's timing of the same source can disagree with the LX7, sometimes in the opposite direction, and what a host run still proves | The LX7 is in-order and single-issue (TRM Section 1.7, confirmed verbatim) against a host's out-of-order, superscalar execution; a load-use stall costs real cycles only on the device. The windowed 16-register view backs onto a real spill once a kernel's live values exceed it, where a host's renamed physical file (168 to 180 registers on Zen 1/2) usually absorbs the same pressure for free. Float divide and square root are windowed library calls on this target (`call8 __divsf3`, `call8 sqrtf`, confirmed against the FP and cost-model leaves), not single instructions; double precision is software-only and the source difference is silent. The PIE extension has no multi-lane gather (`EE.LDXQ.32` is a one-lane indexed load); a host's AVX2 auto-vectorized gather measures a strategy the device cannot use. A reversal-pattern table closes the leaf, tagged `[experience]` throughout since none of it comes from a citable manual section. |

## Not yet covered

- A published per-instruction issue latency for `rsr.ccount` or any other single instruction on the LX7: not found in the ISA manual or the TRM in the sources checked here; flagged `[uncertain]` in the ccount leaf rather than assumed. A device measurement of the empty-interval floor is the way to get one, and belongs in `07-our-work/` if it is repo-specific or here if generic.
- Cross-core cache and MSPI-bus contention while both cores run: named as a memory-hierarchy question in the ccount leaf and deferred to `03-memory-hierarchy/`, which does not yet quantify it either (see that bucket's own "not yet covered" list).
- QEMU's `-icount` mode combined with a `CCOUNT` delta: QEMU's own documentation disclaims cycle accuracy, and this bucket did not run the combination to confirm the exact instructions-to-ticks scaling; flagged `[uncertain]` in the QEMU leaf.
- A worked example of the split-buffer property test or the fuzzing harness running against one of this repo's actual kernels: out of scope here by design (`07-our-work/` is the only bucket allowed to name repo kernels) and a candidate for that bucket.
- The exact cost, in cycles, of the `div0.s`/`recip0.s`/`sqrt0.s`/`rsqrt0.s` seed-and-refine sequence when hand-written instead of left as a library call: the seed instructions and a worked refinement sequence are documented in `01-scalar-isa/floating-point-option-on-lx7.md`, but no device timing of that sequence exists yet in any bucket.
- A primary-source confirmation of the McKeeman 1998 page range for "Differential Testing for Software": corroborated here only through a secondary summary (Wikipedia), not the original *Digital Technical Journal* issue; flagged `[uncertain]` in the bit-exact leaf.
