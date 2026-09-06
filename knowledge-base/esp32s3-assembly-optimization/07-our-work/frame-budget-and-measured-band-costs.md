---
title: The frame budget the animations live in, and what the device measured
id: 07-our-work/frame-budget-and-measured-band-costs
schema_version: 1
doc_type: reference
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, lx7, gaggimate, frame-budget, cost-model, band, measurement]
confidence: high
---

# The frame budget the animations live in, and what the device measured

This page states the cycle budget one background-animation frame has to fit
inside on this repo's hardware, the panel period that budget is chasing, and
the fleet-wide device numbers the 2026-09-04 assembly pass measured against
it. It is the numeric backdrop for every kernel-specific claim elsewhere in
`07-our-work/`.

## 1. The per-pixel cycle budget

The render task's target is stated as an arithmetic identity, not a rule of
thumb: at 480x480 and 30 fps, one frame is 230,400 pixels, and at
240 MHz the CPU executes 240,000,000 cycles per second. Thirty of those
frames per second gives a per-frame budget of

    240,000,000 cycles/s / 30 frames/s = 8,000,000 cycles/frame

and dividing that frame budget by the pixel count gives the per-pixel figure
the animbench brief quotes directly:

    8,000,000 cycles/frame / 230,400 px/frame ~= 34.7 cycles/px

stated as "~34 CPU cycles per pixel total (230,400 px @ 30 fps)"[^optimize].
That 34 cycles covers everything the render task does for one pixel, not
just the animation: the brief carves out band() at under ~25 cycles/pixel,
leaving headroom for the overlay blend and the panel push that
`SleepAnimation.cpp` adds on top[^optimize]. A kernel that lands at 25
cycles/pixel is at budget only if the blend and push are free; §5 gives the
blend's own measured cost, which is not free.

The same brief's device cost model table is the reference for what any of
that budget buys: an int32 add, shift or multiply costs about 1 cycle, a
hardware-FPU float add or multiply-add 1 to 2, a float compare or convert
2 to 4, a float divide about 30 (there is no `fdiv` instruction), `sqrtf`
about 90, `sinf`/`cosf`/`exp2f` about 150, `expf`/`logf` about 200,
`atan2f`/`tanhf` about 300, `powf` about 400, any double-precision math 5 to
10x the float cost (it is done in software), and a PSRAM cache-miss read
about 40 to 80 cycles per line[^optimize]. One per-pixel `sinf` call alone
is stated as four times the whole per-pixel budget, which is why the
consequence list built on this table opens with "zero libm calls per
pixel"[^optimize].

## 2. The panel's period is a separate clock from the budget above

The 34-cycles/pixel figure is the render task's own arithmetic target for
30 fps; it says nothing about when the panel actually reads out a frame.
That is set independently, by the pixel-clock divider. At divider n=6 the
panel's pixel clock is 80 MHz / 6 = 13.33 MHz, measured on the bench as
50.7 fps scan-out with zero resyncs over a 304-second loadtest window under
live WiFi churn, BLE scanning and flash writes[^n6]. A 50.7 Hz scan-out is a
19.7 ms period, and that period is the quantisation grid every animation's
`frame_us` lands on: 39 ms is 25 fps (two panel periods), 58 ms is 17 fps
(three periods), 78 ms is 12.7 fps (four periods)[^asmbrief]. A render pass
that misses one period's deadline does not run a little slower; it waits for
the next whole period, because the flip blocks on the scan-out[^rates]. This
is also why "n=6" is not just a display setting: the same section states the
panel scans at 50.7 Hz regardless of what the render task is doing, so a
full-frame animation that wants to hit the panel's own rate needs render
plus push under 19.7 ms, and nothing in the fleet was within 4 ms of that on
the standby screen[^rates].

Divider n=5 (16.0 MHz, ~61 Hz) was tried and rejected: at n=5 the bounce-
buffer refill (a CPU memcpy out of PSRAM, competing with flash-resident BLE
host and WiFi code on the same MSPI bus) consumes PSRAM bandwidth at about
28 MB/s, matching what the panel itself consumes at that divider, so a
transient stall never clears and laps the 8-buffer bounce pool; at n=6 the
refill consumes about 24 MB/s and the lag clears[^n6]. `RGB_MAX_PIXEL_CLOCK_HZ`
in `platformio.ini` sets only the boot default; the live `panelClockDiv`
setting can override it, floored at `panelclock::MIN_USER_DIV` = 6, with
`/api/debug/pclk` deliberately left unfloored so the live divider can be
read back for measurement[^pclk]. §6 gives the trap this creates for anyone
comparing rates across boots.

## 3. Where the render task sits, contending for the core

The animation's render task runs on core 0 at priority 1, below `SleepPush`
at priority 2 and `Controller::loopLogic` at priority 3, and shares that
core with the BLE stack and WiFi[^prio]. The panel's own interrupts are
pinned to core 1 for an unrelated reason (keeping the BLE link-layer ISR
from preempting the bounce-buffer refill; see the memory-hierarchy and
foundations buckets), but the render task's compute never moves off core 0,
so every device band_us number in §4 already includes whatever BLE, WiFi and
the control loop did to that core during the frame[^asmbrief].

## 4. The 2026-09-04 device table

Measured on the bench device with `anim_devbench.py`, full resolution,
interlace pinned off, default parameters and default theme, after the first
hand-written-assembly pass on the fleet[^asmbrief]. `band_us` is the whole
480x480 field for one frame (240 calls to `band()`, two rows per call);
`blend_us` is the overlay composite, a separate cost from band() (see §5);
`frame_us` is the frame period, quantised to the panel's 19.7 ms VSYNC as in
§2; `host band_ms` and `host x80` are the same kernel's cost on the x86 host
bench, at 1x and at an 80-frame batch, included because the device number is
2 to 3x worse than the host-scaled estimate for most of the fleet[^asmbrief]:

| id | anim      | band_us | blend_us | frame_us | host band_ms | host x80 |
|----|-----------|--------:|---------:|---------:|--------------:|---------:|
| 0  | plasma    |  13,334 |    5,594 |   38,288 | 0.070 |  5.6 |
| 1  | lava      |  32,506 |    8,072 |   57,961 | 0.225 | 18.0 |
| 2  | silk      |  21,187 |    5,161 |   39,000 | 0.120 |  9.6 |
| 3  | starfield |  18,665 |    8,909 |   42,016 | 0.086 |  6.9 |
| 4  | aurora    |  57,920 |    6,633 |   77,990 | 0.225 | 18.0 |
| 5  | ripples   |  14,004 |    6,366 |   38,988 | 0.118 |  9.4 |
| 6  | caustics  |  28,255 |    5,722 |   58,539 | 0.137 | 11.0 |
| 7  | mandala   |  46,739 |    7,252 |   77,989 | 0.256 | 20.5 |
| 8  | orbits    |   6,236 |    5,731 |   38,941 | 0.047 |  3.8 |
| 9  | fireflies |  13,415 |    6,100 |   38,500 | 0.123 |  9.8 |
| 10 | steam     |   7,990 |    6,112 |   39,922 | 0.108 |  8.6 |
| 11 | ember     |  35,399 |    6,132 |   58,541 | 0.265 | 21.2 |
| 12 | nebula    |  39,453 |    5,517 |   57,993 | 0.320 | 25.6 |

[^asmbrief]

The gap between the host and device columns is attributed to four causes,
not one: the host number counts instructions on a wide out-of-order x86
core, while the device pays for load-use stalls (one cycle whenever the
instruction right after a load consumes its result), for spills out of a
16-register window, for PSRAM cache misses on any table over 8 KB (one
concrete example named: a 64 KB noise texture that `bganim::alloc` places in
PSRAM because it exceeds the 8 KB SRAM allocation limit), and for the fact
that `BAND_H` is 2 on the device rather than the host bench's 8, so
per-call setup is paid 240 times a frame instead of 30[^asmbrief]. A
concrete instruction-count counterexample is given for plasma: its compiled
inner loop is 15 instructions per pixel pair inside a hardware loop, yet
still costs 14 cycles per pixel on the device, so instruction count is
roughly half the story with scheduling and memory the other half[^asmbrief].
The target this table is measured against is every animation inside one
VSYNC of render time (band plus blend under 19.7 ms, so band under about
12 ms), with the six heaviest (aurora, mandala, nebula, ember, lava,
caustics) held to at least under 39 ms so they stop dropping to half
resolution at 17 or 12.7 fps[^asmbrief].

## 5. The overlay blend is a second, separate cost

`blend_us` in §4's table is not part of band(); it is the LVGL-snapshot
overlay the render task composites on top of the animation. On the brew
screen, whose plates are about 60% opaque, that overlay is roughly 106,000
non-transparent pixels and costs 12 to 13 ms of blend per frame, measured at
any render task priority[^layers]. That figure is what makes "band under
~25 cycles/pixel" insufficient on its own: a kernel at budget for band()
still has 12 to 13 ms of blend competing for the same 19.7 ms panel period
on a screen with translucent plates, so how much of the screen is
translucent is stated as the budget knob for everything else on top of the
animation[^layers].

## 6. Two traps that break a rate comparison across measurements

**The stored pixel-clock divider silently overrides the build default.**
`RGB_MAX_PIXEL_CLOCK_HZ` sets only the boot value; the bench board used for
the on-display-settings device runs stored divider 8, not the build's
default 6, so its measured frame rates are not directly comparable to a run
at the default divider. `/api/debug/pclk` reports the live divider and the
settings-test runner reads it and warns before comparing[^pclk8]. This is
the general form of the n=5-vs-n=6 trap in §2: a compile-time constant can
be silently replaced by a runtime setting, and any rate comparison across
two runs has to confirm both ran the same divider first.

**Interlace changes the per-row rate without changing the loop rate.** The
animation render loop paces to the stored `bg_fps` setting (30 by default);
with interlacing on (the default), each pixel row is only refreshed every
other frame, so a 30 fps loop delivers 15 Hz per row[^rates]. Measured with
the cap raised to 60: nine animations reach 41 to 52 fps interlaced (20 to
26 Hz per row) and the five heaviest (lava, aurora, mandala, ember, nebula)
27 to 34, with zero scan-out slips in every window measured[^rates]. The
same loop with interlacing off (a whole frame rendered every frame) tops out
at 16 to 25 fps for every animation and cannot exceed 25.3, because the
cheapest full frame measured is about 24 ms of work (roughly 3 ms band,
6 to 7 ms blend, 3 to 4 ms push handoff, 6 to 7 ms waiting on the band-DMA
slot, and about 5 ms of per-band overhead spread over 240 bands), and the
frame flip waits for the scan-out, so anything past one 19.7 ms panel
period costs two[^rates]. The same measurement found that half resolution
does not help (the 2x pixel expansion costs back what the smaller render
saved), that a CPU push is slower than the GDMA push (16 MB/s versus
21.5 MB/s), and that a slower pixel clock (divider 7 or 8) does not shorten
the work[^rates]. A crop that pushes only each row's visible chord instead
of the full row width (`dmaCrop`, disabled by `crop=0` on the debug
endpoint) was measured against `crop=0` on five animations at about 15%
less work per interlaced frame and a further 1 to 2 fps at the 60 cap, with
no measurable change at the 30 cap or on the quantised full-frame path,
because the panel is round and a full-width row can be up to 100% pixels the
panel never displays[^rates]. Comparing two rate measurements therefore
needs the interlace setting, the fps cap, and the crop setting held equal,
not just the pixel-clock divider from the first trap.

## Sources

[^optimize]: `tools/animbench/OPTIMIZE.md`, "bganim hyper-optimization
    brief (sleep17)": target hardware, the render-task budget line, and the
    device cost model table. Undated in the file; current as of this
    repo's `idf5` branch, 2026-09-06.
[^asmbrief]: `tools/animbench/ASM_BRIEF.md`, "bganim assembly pass: brief
    (2026-09-04)", section "Where the fleet stands on the device
    (2026-09-04, production build)": the per-animation table, the
    host-versus-device gap analysis, the plasma instruction-count
    counterexample, and the per-VSYNC target. [measured] on the bench
    device with `C:\work\camshots\anim_devbench.py`, 2026-09-04.
[^n6]: `platformio.ini`, comment block above `RGB_MAX_PIXEL_CLOCK_HZ`
    (~line 260-310): the n=5/n=6/n=7 divider grid, the 50.7 fps / 13.33 MHz
    figure for n=6, the 304 s loadtest window with zero resyncs, and the
    28 MB/s versus 24 MB/s refill-bandwidth measurement that separates n=5
    from n=6. [measured], dated 2026-09-03 in the comment's own note; see
    also commit `84126b1a` ("display: floor the stored pixel-clock divider
    at n=6; the garbling was n=5", 2026-09-03), which carries the
    within-boot A/B (div 5: 17.7 slow copies/s and 6 resyncs in 180 s,
    worst copy 571 us; div 6: none, worst copy 190 us) and the 45-minute
    soak (137,052 frames, 0 resyncs, worst copy 249 us against 632 us of
    pool slack).
[^pclk]: `84126b1afb5ec838fd1ceba6d7ef0cd56085c3d4` ("display: floor the
    stored pixel-clock divider at n=6; the garbling was n=5", 2026-09-03):
    `PanelClock.h` adds `MIN_USER_DIV = 6` and `clampUserDiv()`, applied at
    every stored-setting read site; `/api/debug/pclk` is deliberately left
    unfloored so the live divider is observable.
[^prio]: `src/display/ui/default/SleepAnimation.cpp`, comments at line 1124
    ("Core 0, priority 1: below the push task (2), Controller::loopLogic
    (3)") and line 1159, and the `createAnimTask(pushTaskEntry, "SleepPush",
    ..., 2, ...)` call at line 1165, read 2026-09-06 on the `idf5` branch.
[^layers]: Commit `ef121153dd9c9f6a9170bafce862b67106989f88` ("CLAUDE.md:
    layers, and what a moving foreground element costs", 2026-09-05):
    root `CLAUDE.md`, "UI-pipeline invariants" section. [measured] on the
    standby screen, 2026-09-05: the layer-cost figure (170 to 250 ns per
    non-transparent pixel per frame, vector and scalar blends measuring the
    same) and the brew-screen overlay figure (about 106,000 non-transparent
    pixels, 12 to 13 ms of blend a frame at any render priority) are both
    recorded only in this CLAUDE.md prose; no separate log file is cited in
    the commit for these two numbers.
[^rates]: Commit `8f8ad95e` ("display: measure the animation's own frame
    rate and crop the band DMA to the chord", 2026-09-05): the commit
    message states the loop-rate/interlace measurement (30 fps loop, 15 Hz
    per row; cap-60 figures of 41-52 fps for nine animations and 27-34 for
    the five heaviest; full-frame ceiling of 25 fps, none exceeding 25.3,
    cheapest full frame about 24 ms), the negative results (half resolution,
    CPU push, slower pixel clock all tried and none shortened the work),
    and the `dmaCrop` result (about 15% less work per interlaced frame
    against `crop=0` on five animations, +1-2 fps at cap 60, no change at
    cap 30 or full-frame, dma_errors 0, slips 0 over 290k checks). The same
    content is mirrored in root `CLAUDE.md`'s "UI-pipeline invariants"
    section (2026-09-05/06); the full 24 ms full-frame breakdown (band 3,
    blend 6-7, push handoff 3-4, band-DMA slot wait 6-7, ~5 per-band
    overhead over 240 bands) appears only in that CLAUDE.md prose, not in
    the commit message itself.
[^pclk8]: Commit `6cb39a1a` ("settings: document the on-display settings
    architecture and tests", 2026-09-06): root `CLAUDE.md`, "On-display
    settings" section, "What the device runs taught" subsection: the bench
    board used for the settings device runs stores pixel-clock divider 8,
    not the build default of 6, and the settings-test runner reads
    `/api/debug/pclk` and warns before comparing rates. Recorded only in
    CLAUDE.md prose; no separate log file is cited for this specific fact.
