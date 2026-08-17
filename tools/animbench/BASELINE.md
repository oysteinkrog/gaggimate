# Baseline (pre-optimization), 240 frames, host x86, 480x480

Harness: `./build/bench --frames 240` at commit sleep16. Golden PPMs in
`golden/` were produced by this exact code.

Band height at the time was 16 rows, on both the device and the harness, so
these numbers are internally consistent and comparable to each other. They are
NOT directly comparable to anything measured after `7fede9c9` took the device
to 8 rows: band height shifts individual animations by up to 9%, in either
direction (see the note on `BAND_H` in `bench.cpp`). To compare against a row
in this table, build with `-DGM_BENCH_BAND_H=16`.

| id | anim | frame_ms | band_ms | libm/frame | est_dev_ms (libm only) | worst |
|---|---|---|---|---|---|---|
| 0 | plasma | 0.002 | 0.122 | 0 | 0 | — |
| 1 | lava | 0.000 | 0.691 | 1441 | 0.54 | sqrtf=1440 |
| 2 | silk | 0.000 | 0.976 | 1441 | 0.24 | fmodf=1440 |
| 3 | starfield | 0.002 | 0.100 | 203 | 0.03 | fmodf=202 |
| 4 | aurora | 0.000 | 0.936 | 1 | 0 | — |
| 5 | ripples | 0.000 | 0.707 | 93390 | **35.02** | sqrtf=93389 |
| 6 | caustics | 0.000 | 1.661 | 1 | 0 | — |
| 7 | mandala | 0.000 | 1.259 | 1 | 0 | — |
| 8 | orbits | 0.001 | 0.040 | 238 | 0.09 | sqrtf=237 |
| 9 | fireflies | 0.001 | 0.145 | 32 | 0.03 | expf=30 |
| 10 | steam | 0.001 | 0.161 | 1 | 0 | — |
| 11 | ember | 0.000 | 0.294 | 1 | 0 | — |
| 12 | nebula | 0.000 | 0.560 | 1 | 0 | — |

## Calibration

Plasma is the reference: it demonstrably sustains 30 fps on the device with
headroom, costs 0.122 host band_ms, zero libm, ~10 cycles/pixel (LUT sum +
palette index). Treat **host band_ms x ~80 ≈ device ms/frame** as the scale
(pure-ALU code; libm-heavy code is WORSE on device than host time suggests —
add est_dev_ms on top, x86 does sqrtf/fmodf in hardware, Xtensa does not).

Device frame budget: 33 ms at 30 fps, and the render task also pays for
overlay blending and the panel push. **Target: host band_ms <= 0.20 and
zero per-pixel libm.** frame() may keep modest libm (a few hundred calls).

Estimated current device fps: caustics ~7, mandala ~10, silk ~13, aurora ~13,
ripples ~11 (sqrtf!), lava ~18, nebula ~22, ember ~28, the rest at 30.
