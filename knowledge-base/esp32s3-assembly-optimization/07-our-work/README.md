---
title: "07-our-work: bucket index"
id: 07-our-work/readme
schema_version: 1
doc_type: reference
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, our-work, index]
---

# 07-our-work: bucket index

The one bucket where this repo's files, tools, animations and measurements
are named. Every number here carries `[measured]` with its commit or log.
The generic mechanisms behind these leaves live in buckets 00 to 06 and are
linked from each leaf.

| File | Topic | Key claims |
|---|---|---|
| `frame-budget-and-measured-band-costs.md` | The cycle budget and the 2026-09-04 device table | 230,400 px at 30 fps is about 34 cycles per pixel at 240 MHz; the panel period is 19.7 ms at divider 6 and frame time quantises to it (39, 58, 78 ms); band, blend and frame times per animation; the stored pixel-clock divider trap |
| `animation-kernel-pass-2026-09-what-the-device-decided.md` | The hand-written kernel pass, per-animation outcome | Wins: orbits 2.0x, caustics 1.5x, nebula 1.3x, ripples 3x to 5x. Losses: mandala 0.85x, silk 0.73x (three silk kernels). Ties: lava 0.99x, Silk 2 1.16x on. Flags in platformio.ini record the device verdict |
| `host-versus-device-reversals-measured-here.md` | Every recorded case where host and device disagreed | Silk 4x unroll 158 to 430 instructions; aurora named locals lost a hardware loop; mandala packed u16 LUT kept despite a slower host; the x80 host calibration was 2 to 3x optimistic |
| `verification-ladder-host-asm-qemu-device.md` | The four rungs and what each proves | Host goldens and interlace_check; the real compiler's .S through xtensa-asm14.sh; QEMU bit-exactness (with the caveat that Espressif's QEMU has open PIE divergence issues); the device animtest with mismatch_px 0 |
| `kblob-hot-loading-kernels-on-the-device.md` | The display-kdev hot-load rig | compiledb first, then flash; blob vs blobref is the kernel comparison, blob vs band mixes in placement (nil at the minimum); never reflash during a round; a LEAK line pushes later benches into PSRAM |
| `hot-slab-table-placement-and-the-dram-budget.md` | Where kernel tables live and why | 12,288 B slab: 3,072 B shared, 9,216 B per animation, 16-byte aligned; SRAM vs PSRAM tables 1.3x to 2x band time; static tables are the same pool; the WiFi TX copies and the 20 KB asset gate |
| `pie-and-scalar-idioms-in-this-firmware.md` | Thirteen kernel idioms with file:line | scale565Oct as the model kernel; zip/unzip widen and narrow; the unaligned usar/src.q stream; multiply as the only shift in the blend kernels; the four-way interleaved gather; the bandRef twin and the flag gate |
| `band-contract-interlace-and-row-independence.md` | What a band() must satisfy | BAND_H is 2 on the device (240 calls per frame); rows==1 parity-skipping and 240-wide calls exist; a row depends only on its y and the frame state; derive from y & ~1 and memcpy only within a call |
| `fuzzing-the-fleet-and-the-silk-palette-pad.md` | The fuzzer and the defect that made the rule | fuzz.cpp sweeps parameters, themes and time under ASan and UBSan; silk's pad of 4 against a dither cap of 16 read past its LUT; size a pad from the producer's cap and re-fuzz the fleet on any table change |

## Not yet covered

- A per-animation table of which tables sit in the slab and how many reads per frame each gets (the ranking rule applied, not just stated).
- The overlay and scrim kernels in `tools/overlaybench` and `SleepAnimation.cpp` (blend, span scan, half-res expand) as their own leaf.
- A record of the QEMU-versus-silicon differences this repo has actually hit (the `ee.vadds.s8` floor is mentioned in the kernel-pass leaf).
- Measured cycle costs for the scalar multiply, divide and FP instructions on this board, which the generic buckets leave `[uncertain]`.
- The flash-cache patch (`scripts/patch_flash_cache_flag.py`), which exposes a flag to the panel refill interrupt while `spi_flash_disable_interrupts_caches_and_other_cpu` has the caches off; the runtime leaf in `08-frontiers` describes the generic mechanism only.
