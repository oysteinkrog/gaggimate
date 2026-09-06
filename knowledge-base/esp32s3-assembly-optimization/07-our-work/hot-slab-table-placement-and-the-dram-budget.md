---
title: The hot slab, table placement, and the internal DRAM budget
id: 07-our-work/hot-slab-table-placement-and-the-dram-budget
schema_version: 1
doc_type: explanation
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, psram, sram, dram, placement, lut, wifi, measurement]
confidence: high
---

# The hot slab, table placement, and the internal DRAM budget

Where a kernel's lookup tables live decides more of its per-frame time than
the kernel does. In this repo's 13 background animations the same compiled
code ran 1.3x to 2x slower with its tables in PSRAM than in internal SRAM.
This file records how that was measured, why the placement used to change
from one boot to the next, what replaced the heuristic that caused it, and
why the amount of internal DRAM an animation may take is set by what the
WiFi driver needs rather than by what the animation wants.

The mechanisms behind the speed difference are generic and live elsewhere:
cache geometry, PSRAM bandwidth and the shared MSPI bus in
[`../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md`](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md),
and the ESP-IDF attributes and heap capabilities that express a placement in
[`../04-toolchain-and-codegen/code-and-data-placement-in-esp-idf.md`](../04-toolchain-and-codegen/code-and-data-placement-in-esp-idf.md).
What follows is this repo's arrangement on top of them.

## 1. The measurement that forced the design

Every table in an animation was pinned first to internal SRAM and then to
PSRAM, with the same firmware, at full resolution with interlace off. Time
is milliseconds per frame of `band()` work.[^bganim-h][^c-78a13bc5]

| Animation | Tables in SRAM | Tables in PSRAM |
|---|---|---|
| plasma | 9.1 | 17.8 |
| starfield | 15.6 | 27.8 |
| caustics | 22.6 | 37.5 |
| lava | 26.0 | 47.7 |
| aurora | 46.7 | 67.6 |

[measured] 2026-09-04, commit `78a13bc5`, recorded in the commit message and
in the header comment of `src/display/ui/default/bganim/BgAnimCommon.h`.

The panel scans out of PSRAM continuously and LVGL runs on the other core,
so a per-pixel gather into PSRAM is a data cache miss most of the time
rather than occasionally. A small table read 230,400 times a frame is worth
more in SRAM than any amount of instruction scheduling is worth
anywhere.[^bganim-h] This is the single largest effect the animation
optimisation pass found, and it is invisible to a host benchmark.

## 2. Why the placement used to flip between boots

Placement was decided at `init()` by asking the heap how much internal DRAM
was free, then taking some if there was enough. Three separate rules
accumulated on that idea, each fixing the previous one's failure:

**Size rule, 2026-08-15.** Cycling through all 13 animations in one power
cycle requested roughly 280 KB of internal SRAM, against a pool the firmware
had already claimed about 110 KB of, because tables were allocated lazily
and never freed. `alloc()` began sending anything over 8 KB to PSRAM first.
The largest single offender was the mandala animation's polar map at about
116 KB.[^c-0835b64a]

**Free-pool reserve, 2026-08-26.** The size rule bounded the wrong thing.
On real hardware internal free sat at 1,384 B and the WiFi driver could not
allocate the 180 bytes a probe request needs, so the display never
associated at all, and the panel garbled for the same reason. The failing
allocation was small, so this was exhaustion and not fragmentation. Both the
animation's tables and the render task's band buffers moved to checking the
free pool at the moment of the request and leaving `INTERNAL_RESERVE`
(48 KB) untouched, measured against the DMA-capable internal pool
specifically because that is the one WiFi draws from.[^c-a5c0e5a9]

**Radio gate, 2026-08-26.** The reserve still did not fire, because of an
ordering it could not see. With the all-screens animation setting the
animation starts as soon as the user interface is built, which is before
either radio comes up: it allocated with 89 KB of internal DRAM free, and
WiFi (47.2 KB) plus BLE (40.7 KB) then wanted 88 KB of it. A free-space
check at that moment reads a number that is already spoken for. So
`radiosSettled()` now refuses internal DRAM to the animation until
`esp_wifi` is initialised and the Bluetooth controller is enabled. On the
bench rig this moved internal free from 8,367 to 22,323 B, DMA-capable free
from 591 to 14,547 B, and failed allocations from 8 to 0.[^c-1d61b65f]

**The consequence nobody wanted.** After the radios settled, the pool that
`internalHasRoomFor()` measured peaked at 18,611 B against a 48 KB reserve,
so the SRAM path was unreachable by construction and every table lived in
PSRAM.[^c-e9810d29] A re-placement pass then re-ran `release()` and `init()`
for the resident animation the first time the gate opened, so tables that
were refused during boot could be placed once the real pool was
visible.[^c-49a17d6f] After the 2026-09-04 DRAM reclaim the pool idled
within a few kB of the reserve, which is the worst possible place for a
threshold: the same table landed in SRAM on one boot and in PSRAM on the
next, `band()` time swung by the 2x in the table above, and the internal
DRAM left for WiFi depended on which animation happened to be
running.[^c-78a13bc5][^bganim-h]

The lesson is not that the heuristic was tuned wrong. It is that a
free-pool query answers a question about the past, and both the kernel's
speed and the network stack's survival needed the answer to be a constant.

## 3. The slab

The hot tables now come from a slab of fixed size, allocated once at build
time in internal DRAM.[^bganim-h][^bganim-cpp] An animation's cost to the
pool is one constant, visible in the linker's RAM figure and identical on
every boot, and the placement of a table becomes a decision the animation
makes in source.

| Property | Value |
|---|---|
| Slab size | 12,288 B (`GM_BGANIM_HOT_SLAB`) |
| Shared reserve, top end | 3,072 B (`HOT_SHARED_RESERVE`) |
| Available to the resident animation, bottom end | 9,216 B |
| Alignment | 16 B (`alignas(16)`) |
| Static internal RAM in the linker's figure, with the slab | 98,312 B |

The shared reserve is exactly the two boot-lifetime tables that several
animations borrow: a 1,024-entry `int16_t` sine table (2,048 B) and a
256-entry `float` cosine table (1,024 B).[^bganim-cpp]

Three entry points, one slab:[^bganim-h]

- `allocHot(size)` gives per-animation tables read per pixel or per row.
  Bump-allocated from the bottom. It returns memory from the slab or, when
  the slab is full, falls back to `alloc()` so the animation still renders,
  just slower.
- `allocHotShared(size)` gives boot-lifetime tables owned by the common
  layer and borrowed by several animations. Carved from the top and never
  returned.
- `alloc(size)` is PSRAM, always. Bulk tables swept sequentially stream from
  PSRAM at close to SRAM speed because the cache prefetches the run, so they
  were never the tables the placement decision mattered for.

Four details of the bump allocator are load-bearing:

**The shared term is reserved before the shared tables exist.** They are
built lazily on first use, so an animation that filled the slab before
calling `sinLut()` would push a table every later animation borrows out to
PSRAM for the rest of the boot. `allocHot()` therefore clamps its limit to
`HOT_SLAB_BYTES - HOT_SHARED_RESERVE` whether or not the top end has been
touched.[^bganim-cpp]

**The bottom region resets only when its live count reaches zero.** Every
hot table needs a matching `release()`, exactly as heap allocations do. A
missed release keeps the slab from ever reusing that space, and `hotUsed()`
never returns to zero between animations.[^bganim-h]

**Releasing the newest table pops it; releasing an older one does not.** So
a table whose contents change at runtime, such as a palette rebuilt on a
theme change, is rebuilt in place rather than freed and
re-allocated.[^bganim-h][^bganim-cpp]

**16-byte alignment is a requirement, not a courtesy.** The PIE vector
load and store instructions mask the low four address bits of their operand
(see
[`../02-pie-vector/pie-load-store-and-alignment.md`](../02-pie-vector/pie-load-store-and-alignment.md)),
so a kernel that vector-loads a table straight from its start needs the base
aligned. The shared 256x256 noise texture is allocated with
`heap_caps_aligned_alloc(16, ...)` in PSRAM for the same reason: the nebula
animation seeds `SAR_BYTE` from the row through a PIE load, and on an
unaligned base the first load of row 0 would read up to fifteen bytes behind
the allocation. That would not show as wrong pixels, because `SAR_BYTE`
captures the true offset, only as a read of memory the animation does not
own.[^bganim-cpp]

### 3.1 Static arrays are not a way around it

An animation cannot dodge the slab by declaring its tables as file-scope
arrays. BSS is the same internal DRAM pool the slab and the radios are
competing for, so a static table costs the network stack exactly what a slab
table costs it, minus the accounting.[^claudemd] The animation pass removed
every static table in the fleet for this reason: static internal RAM across
all 13 animations returned to the pre-pass figure of 85,928 B, plus the
slab.[^c-8fc84e7a]

## 4. The other side of the subtraction

A budget on how much internal DRAM the animation may take is a bound on the
wrong side. What the radios need is a floor under what is left.[^bganim-h]

The web user interface on this device does not fail from bugs in the server.
It fails from internal DRAM starvation. lwIP's packet buffers live in PSRAM,
and the WiFi MAC cannot perform DMA out of PSRAM, so every outgoing segment
is first copied into an internal DMA-capable buffer of about 1,630 bytes
(capability mask `0x80c`). Each connection can hold `TCP_SND_BUF / MSS` of
those at once, so the send buffer times the connection count is the
DMA-capable transient a burst of browser traffic can take.[^sdkconfig] When
that allocation fails the driver logs `wifi:m f null`, the socket stalls,
and the reconnect loop never recovers.[^claudemd]

The numbers the slab was sized against:[^claudemd][^bganim-h]

| State | Internal free | DMA-capable free |
|---|---|---|
| Before the 2026-09-04 reclaim | ~16 kB | ~8 kB |
| After the reclaim, no animation | ~56 kB | ~48 kB |
| Normal boot with the slab and an animation resident | ~53 kB | ~45 kB |

Two cold browser tabs used to kill the web interface at 15 to 18 kB of
DMA-capable free.[^bganim-h] A 12 KB slab out of the 48 kB idle pool leaves
36 kB, and the acceptance test for that choice is the two, three and four
simultaneous cold-tab runs, not a calculation.

The same commit that introduced the slab made the asset gate adaptive on the
same pool, because a fixed slab is only half the protection. Large embedded
assets stream at most three at a time, and the second and third are admitted
only while DMA-capable free is above 20 KB (`kAssetGateDmaFloor`); the first
is always admitted so that a parked request can never wait on a pool nothing
is draining. The floor is one stream's in-flight copies, four segments or
about 6.5 kB, on top of the 15 to 18 kB at which the driver started failing
its own allocations.[^webui] Measured with the slab, three rounds each of
two, three and four simultaneous cold tabs: minimum DMA-capable free 15 kB,
6.8 kB and 2.3 kB, with zero request failures and zero refused WiFi
allocations on a normal boot.[^c-78a13bc5]

## 5. The ranking rule

The slab caps a single animation's appetite at 9,216 B, and animations used
to ask for more: the silk animation wanted 23.8 KB and starfield 15.9 KB
when the pool allowed it. The measurements in section 1 show plasma's
11.6 KB and caustics' 10.8 KB already buying most of the SRAM win, so the
rule is not to grant more slab but to rank an animation's tables by how many
pixels read each one, and hand the slab to the top of that list.[^bganim-h]

Two worked examples from the fleet:

**Silk.** Its per-row auxiliary table was an array of structures, four
copies by phase times 480 entries times 8 bytes, or 15,360 B. Three quarters
of those bytes were waste: one field never varied by phase, and the other
took only 16 distinct values. It became a 480-byte `uint8_t` row table plus
a 64-byte dither table. The animation's `bandRef` with 8.8 KB of slab beats
the old code that needed 23.8 KB of SRAM.[^silk][^c-8fc84e7a]

**Lava.** Round 4 moved the three tables at or under 8 KB into the slab
(512 B, 1,920 B and 3,840 B at a width of 480), using 6,272 B of the 9,216 B
share and leaving 2,944 B free. The fourth and largest table, read once per
touched pixel, was still in PSRAM by design. Round 5 narrowed it from
`int32_t` to `int16_t` and halved its bucket count to fit 2,304 B into that
2,944 B, so all four tables became slab-resident at once. Five bench runs
after the move put the candidate at 18.94 to 18.96 ms against 19.35 to
19.51 ms, a repeatable cut of 2.3 to 2.9 percent, outside the bench's 3
percent noise floor.[^lava] The same narrowing
had measured slower under the old heuristic, because back then putting that
table in SRAM meant starving another table of the placement it wanted.

That last point is the argument for a fixed slab stated as a measurement:
under a shared-pool heuristic, an animation cannot evaluate one table's
placement without perturbing the others.

## 6. Instrumentation

`/api/debug/heap` reports `hot_used`, `hot_shared`, `hot_peak`, `hot_slab`
and `hot_fail`, next to `int_free`, `dma_free`, `dma_min`, the PSRAM
figures, the animation's own SRAM and PSRAM byte counters, and the asset
gate's stream and queue depths.[^webui] A non-zero `hot_fail` is the count
of hot allocations that fell back to PSRAM since boot, and each one also
logs a warning naming the size and the slab state.[^bganim-cpp] The
counters that report the largest free block walk every block in the heap,
which costs about 1.3 ms and starves the panel's bounce refill for the
duration, so they belong on a debug endpoint and never on a status
poll.[^webui]

`/api/debug/heapmap` dumps the internal regions, a block-size histogram, and
every task's stack size and high-water mark, which is how service task
stacks were sized from measurement rather than from guesses.[^webui]

`/api/debug/anim?reserve=N` moves the band buffers' reserve for the life of
the boot, so one build can be measured with those buffers forced internal
(0) or forced to PSRAM (a value larger than the pool) without a rebuild in
between.[^bganim-h]

One bench-only hook exists because of a failure mode the counters made
visible. Under the kernel bench build, `hotReset()` forgets every bottom-end
table. Without it, a single candidate blob that skipped a `release()` pinned
the live count above zero, and every later bench on that boot silently ran
with its tables in PSRAM. The nebula animation measured 1.7x slower that way
with no other symptom.[^bganim-h][^claudemd]

## 7. What generalises

Four things here are not specific to animations.

1. **Rank tables by reads per frame, not by size.** The size rule of 2026-08-15
   sent everything over 8 KB to PSRAM, which is a reasonable first cut and
   was still wrong: a 512-byte palette read once per pixel matters more than
   a 64 KB texture swept sequentially.
2. **A free-pool query is not a placement policy** when the pool idles near
   the threshold. Make the cost a build-time constant so that it is the same
   on every boot and shows up in the linker's figure.
3. **Bound what is left, not what is taken.** The consumer that fails first
   is the one with the smallest, most urgent allocations. Here it was a
   180-byte WiFi probe request.
4. **A fixed budget measured on the wrong build is worse than none.** The
   first reserve was tuned on a build where the Bluetooth controller never
   starts, which has roughly 30 KB more internal DRAM free than production
   ever has.[^bganim-h][^c-a5c0e5a9]

## Footnotes

[^bganim-h]: This repo, `src/display/ui/default/bganim/BgAnimCommon.h`, the hot slab and `INTERNAL_RESERVE` comment blocks, at commit `754e4b14`.
[^bganim-cpp]: This repo, `src/display/ui/default/bganim/BgAnimCommon.cpp`, `allocHot`, `allocHotShared`, `alloc`, `release`, `radiosSettled`, `internalHasRoomFor`, `sinLut`, `cosTableF` and `noiseTex256`, at commit `754e4b14`.
[^webui]: This repo, `src/display/plugins/WebUIPlugin.cpp`, the `/api/debug/heap`, `/api/debug/heapmap` and `/api/debug/anim` handlers and `assetSlotFree`, `kMaxAssetStreams`, `kAssetGateMinBytes`, `kAssetGateDmaFloor`, at commit `754e4b14`.
[^sdkconfig]: This repo, `sdkconfig.gaggimate.defaults`, the comment blocks above `CONFIG_LWIP_TCP_SND_BUF_DEFAULT`, the WiFi TX buffer section, and `CONFIG_ESP_WIFI_TX_BA_WIN`, at commit `754e4b14`.
[^claudemd]: This repo, root `CLAUDE.md`, sections "Internal DRAM budget" and "Animation kernels", at commit `754e4b14`.
[^silk]: This repo, `src/display/ui/default/bganim/AnimSilk.cpp`, the fifth-pass note in the file header, at commit `754e4b14`.
[^lava]: This repo, `src/display/ui/default/bganim/AnimLava.cpp`, the round-4 and round-5 notes in the file header, at commit `754e4b14`.
[^c-0835b64a]: This repo, commit `0835b64aaa2d27c9eee118f8dea97c476e8997ec`, "fix(display): keep large animation tables out of internal SRAM", 2026-08-15.
[^c-a5c0e5a9]: This repo, commit `a5c0e5a93825afe10d142a3de690595e965fcdd3`, "display: stop the animation starving the radios of internal DRAM", 2026-08-26.
[^c-1d61b65f]: This repo, commit `1d61b65f549d16e41bbabba46099b26a4573d3ee`, "display: keep the animation off internal DRAM until the radios have claimed", 2026-08-26.
[^c-e9810d29]: This repo, commit `e9810d29be482052c682df554e691396de0e1f71`, "bganim: say why a table went to PSRAM instead of guessing", 2026-08-26.
[^c-49a17d6f]: This repo, commit `49a17d6f35e76404e0a24a77a88d6b45202b3f7a`, "bganim: re-place animation tables into SRAM once the radios settle", 2026-08-30.
[^c-78a13bc5]: This repo, commit `78a13bc5e6305f6b06126af8d11d7ce2ec11cead`, "bganim: fixed internal slab for hot tables, PSRAM for the rest, gate assets on dma_free", 2026-09-04.
[^c-8fc84e7a]: This repo, commit `8fc84e7af60947994ddf1335002c59a1c3edc32a`, "bganim: hand-written Xtensa kernels for the 13 animations, hot tables in the slab", 2026-09-04.
