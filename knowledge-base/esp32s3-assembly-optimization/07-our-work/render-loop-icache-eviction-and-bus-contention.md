---
title: Render loop icache eviction and bus contention, measured on the bench board
id: 07-our-work/render-loop-icache-eviction-and-bus-contention
schema_version: 1
doc_type: reference
status: draft
last_reviewed: 2026-09-07
tags: [esp32s3, xtensa, gaggimate, icache, iram, psram, mspi, measurement]
confidence: high
---

# Render loop icache eviction and bus contention, measured on the bench board

Three things were measured on this repo's bench board on 2026-09-07 while
chasing the animation render task's frame rate under UI churn: the render
loop losing half its speed to shared instruction-cache eviction, a
compositing change that ran into PSRAM bus contention instead, and a
proof of concept that tried to remove the framebuffer altogether and hit
the same bus limit from the producer side. All three are `[measured]` on
the bench board (LilyGo T-RGB, ESP32-S3) with the panel's stored
pixel-clock divider at 8 (38.0 Hz scan-out, 26.3 ms frame, 110 us per
2-row band). The generic mechanisms behind each result live in
[`../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md`](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md)
and
[`../04-toolchain-and-codegen/code-and-data-placement-in-esp-idf.md`](../04-toolchain-and-codegen/code-and-data-placement-in-esp-idf.md);
the frame budget and panel period these numbers sit inside are in
[`frame-budget-and-measured-band-costs.md`](./frame-budget-and-measured-band-costs.md),
and the table-placement question this bucket already answers for PSRAM
versus SRAM is in
[`hot-slab-table-placement-and-the-dram-budget.md`](./hot-slab-table-placement-and-the-dram-budget.md).

## 1. Shared instruction cache eviction

The ESP32-S3 has one instruction cache and one data cache, shared by both
Xtensa cores, both carved from internal SRAM.[^icache] The default ICache
is 16 KB, 8 ways, 32 B lines.[^icache] This repo's build does not raise
that default: the `CONFIG_ESP32S3_INSTRUCTION_CACHE_SIZE` setting does not
appear in `sdkconfig.gaggimate.defaults`, so the board runs the Kconfig
default of 16 KB.[^sdkconfig]

LVGL redraws run on core 1: on the status and brew screens, about three
times a second, each pass costs 40 to 55 ms of drawing.[^gm212] The render
task's hot path (`renderFrame`, `renderLoop`, `presentFrame`, `pushLoop`
and the scrim-row helpers, in
`src/display/ui/default/SleepAnimation.cpp`) also runs on core 1, sharing
the same 16 KB cache with whatever LVGL just filled it with. Nothing in
the render task's data path changed between the two measurements below;
the band kernel reads and writes SRAM only.[^gm212]

Measured on the bench board (`display-loadtest`, Starfield, cap 45),
while the status screen's LVGL passes were running:

| Placement | Loop rate | band | push | blend |
|---|---|---|---|---|
| Render loop in flash | 17 fps | 7 to 14 ms | 7 to 15 ms | 4 to 17 ms |
| Render loop in IRAM | 28 fps | 7 to 9 ms | 5 to 8 ms | 8 to 10 ms |

[^gm212]

The fix was `IRAM_ATTR` on `renderFrame`, `renderLoop`, `presentFrame`,
`pushLoop`, `scrimRow`, `scrimRowPie` and `expandScrimInv`, shipped in
commit `1fc09db7`.[^c1fc09db7] `IRAM_ATTR` places a function in
`.iram1`,[^iram-attr] which this board pays for out of internal DRAM: IRAM
text beyond the first 16 KB of SRAM0 is taken from DRAM one for one, so
`.dram0.dummy` grows by the same amount as `.iram0.text`.[^iram-attr] The
change cost about 7,680 to 9,296 B of internal RAM depending on how it is
counted: the commit states IRAM text grew from 98,335 to 105,983 bytes
(7,648 B) with a matching 7,680 B of DRAM, and the bead's working notes on
the same change round to "about 8 KB".[^c1fc09db7][^gm212] The loadtest
build's idle internal-free dropped from about 46 KB to 37 KB, and
DMA-capable free from about 38 KB to 29 KB, at 37 s uptime.[^c1fc09db7]

Pinning was not extended to the animation kernels themselves: all 13
kernels in IRAM together would cost roughly another 11 KB, and a trial
that pinned only Starfield's kernel bought about 2 ms of band time for
1.1 KB, which the accepted change does not take.[^gm212] The render loop
was pinned instead because it is the code every animation shares, unlike
a kernel, which is animation-specific.[^c1fc09db7]

Acceptance on the board: 27.7 to 28.6 fps under synthetic-brew churn at
cap 45 (target 27), band time 10 to 12 ms against 7 to 8 ms quiet (a 1.4x
ratio, against a 1.3x target that was recorded rather than met), for a
DRAM cost of 7,680 B.[^gm212]

## 2. Flat-run compositing: killed by bus contention, not pixel count

A separate change tried to cut the overlay blend's cost by detecting runs
of 16 or more identical 3-byte pixels (flat plates) and compositing them
with a constant-colour kernel that never reads PSRAM, instead of the
per-pixel `blendRow` path that does.[^gm211] The overlay blend on the
brew, status and menu screens reads about 106,000 non-transparent pixels
from PSRAM every frame, 3 bytes each.[^gm211]

Measured on the bench board with plates on (Custom theme, 35 percent
black): 51 percent of the overlay was covered, 77 percent of that
coverage fell in flat runs of 16 px or more, and the blend went from 28.8
ms without flat runs to 25.3 ms with them, a 12 percent gain against a 2x
acceptance target.[^gm211] With plates hidden (this board's setting),
only 3 to 4 percent of covered pixels were flat and the blend was
unchanged.[^gm211]

The reason the win was small: the blend under UI churn is bound by bus
and cache contention with LVGL on core 1, not by how many pixels it
reads. The same brew screen blended in 4 ms when the UI was quiet and 12
to 17 ms while LVGL refreshed three times a second, regardless of the
flat-run optimisation.[^gm211] Removing PSRAM reads from the flat-run
pixels did not remove the contention, because the contention was never
about those particular reads; it is the same shared-bus and shared-cache
effect that section 1 fixed for the render loop's own code fetches, here
showing up on the data side instead.

The change was reverted. The `/api/debug/ovl` overlay dump written for
the analysis was kept in the tree; a scratch script
(`ovl_analyse.py`, not committed) reads it.[^gm211]

## 3. No-framebuffer raster POC: producer is PSRAM-bound, not preempted

A third experiment asked whether the framebuffer, and the 7 to 15 ms push
stage it costs every frame, could be removed entirely by rendering
straight into the panel's bounce buffers at raster time: configure the
RGB panel without its PSRAM framebuffer (`esp_lcd` `no_fb` bounce mode)
and have a producer task on core 0 fill each emptied 2-row bounce buffer
just ahead of the scan.[^gm213]

Budget per 2-row band at the board's stored divider 8 (38.0 Hz) is 110
us.[^gm213] Two rounds were run, both with Starfield (band cost about 29
us per band at full frame rate, overlay blend about 17 us) on the
standby screen:[^gm213]

| Round | Producer priority | Kernel placement | Underruns | band + blend per frame |
|---|---|---|---|---|
| 1 | Normal (1) | Flash | 45% of requests, quiet | 9 to 11 ms + 11 to 15 ms |
| 2 | 22 (above BLE host) | IRAM (band, vignette, star plotter) | 36 to 43% quiet, 30% under brew | 9 to 11 ms + 12 to 14 ms |

[^gm213]

Per band, that is 85 to 100 us of work against the 110 us slot in both
rounds, before any wake-up or interrupt overhead, so the producer ran
behind the scan-out beam and the panel showed the stale bounce content as
garbling.[^gm213] Neither raising the producer's priority nor moving the
kernel to IRAM changed the band or blend time, which is the signature of
a bus-bound stage rather than a preempted one: each band is a
480-pixel gather from PSRAM tables plus a 3 B/px overlay read, and the
producer was not being interrupted, it was waiting on the same PSRAM bus
the panel's own bounce-buffer refill and LVGL's drawing already
contend for.[^gm213] The blend did not get cheaper without the
framebuffer either, for the same reason section 2 found: LVGL's drawing
and the snapshot copy, not the scan-out, are what compete for PSRAM
bandwidth.[^gm213]

The kill criterion was zero underruns per minute; the measured rate
missed it by about a factor of 3,000 at divider 8, the easiest divider,
with the cheapest animation, on the quietest screen.[^gm213] Making the
approach work would need band plus blend under about 60 us per band with
headroom, a roughly 3x cut in the producer's PSRAM traffic, before the
UI's own overlay reads are even added to the same slot.[^gm213] The
framebuffer path stays. The full diff (487 lines) is kept only as an
uncommitted scratch patch and was not merged.[^gm213]

## 4. A PlatformIO hazard hit twice during this work

Adding or removing a build environment in `platformio.ini` changes the
project's checksum, and PlatformIO deletes every environment's
`.pio/build` tree when that checksum changes, not just the one that
changed.[^gm213] The raster POC's temporary `display-raster` environment
triggered this once; removing it afterwards triggered it a second time.
Each time cost a full loadtest rebuild (about 7 minutes) before the board
could be reflashed and restored.[^gm213] This is the same hazard the
root `CLAUDE.md` records for the `display-kdev` bench environment; adding
a throwaway env for a one-off measurement is not free on this repo's
build.

## Sources

[^icache]: This collection, [`../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md`](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md),
    section 1: one ICache and one DCache, shared by both cores, carved
    from internal SRAM; default geometry 16 KB ICache, 8 ways, 32 B
    lines, cited there to the ESP32-S3 Technical Reference Manual's cache
    chapter and to the target's Kconfig. This leaf does not re-resolve the
    TRM section number; see that leaf's own footnotes for it.
[^sdkconfig]: `sdkconfig.gaggimate.defaults`, read 2026-09-07 on the
    `idf5` branch: no `CONFIG_ESP32S3_INSTRUCTION_CACHE_SIZE` line is
    present, so the Kconfig default of 16 KB applies. `[uncertain]`
    whether any other sdkconfig fragment in the build overrides this;
    only `sdkconfig.gaggimate.defaults` was checked.
[^gm212]: Bead `gm-2cl.12` ("Render task hot path in IRAM so LVGL cannot
    halve the animation rate"), closed 2026-09-07 (`br show gm-2cl.12`).
    [measured] on the bench board, `display-loadtest`, Starfield, cap 45,
    status screen under synthetic UI churn, 2026-09-07.
[^c1fc09db7]: Commit `1fc09db7f4dd9b9b96f41cc8832c7fe77db0c137`
    ("display: pin the render task's hot path in IRAM (gm-2cl.12)"),
    2026-09-07: `IRAM_ATTR` added to `expandScrimInv`, `scrimRow`,
    `scrimRowPie`, `SleepAnimation::pushLoop`, `SleepAnimation::presentFrame`
    and `SleepAnimation::renderLoop` in
    `src/display/ui/default/SleepAnimation.cpp`; `SleepAnimation::renderFrame`
    carries the same attribute and a code comment recording the same
    measurement. [measured] on the bench board, 2026-09-07.
[^iram-attr]: This collection, [`../04-toolchain-and-codegen/code-and-data-placement-in-esp-idf.md`](../04-toolchain-and-codegen/code-and-data-placement-in-esp-idf.md),
    section "IRAM_ATTR: code in instruction RAM": `IRAM_ATTR` places a
    function in the `.iram1` section; IRAM text beyond the first 16 KB of
    SRAM0 is taken from DRAM one for one.
[^gm211]: Bead `gm-2cl.11` ("Flat runs: composite constant-colour overlay
    spans without reading PSRAM"), closed 2026-09-07 (`br show gm-2cl.11`).
    [measured] on the bench board, 2026-09-07; change reverted after
    measurement, not shipped.
[^gm213]: Bead `gm-2cl.13` ("POC: render the background at raster time
    into the bounce ring, no framebuffer"), closed 2026-09-07 (`br show
    gm-2cl.13`). [measured] on the bench board, `display-raster` build
    behind `GM_RASTER_POC`, two rounds, 2026-09-07; approach killed, not
    shipped, framebuffer path retained.
