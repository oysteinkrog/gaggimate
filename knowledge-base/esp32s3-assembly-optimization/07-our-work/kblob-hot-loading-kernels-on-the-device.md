---
title: "kblob: hot-loading kernels onto the running firmware"
id: 07-our-work/kblob-hot-loading-kernels-on-the-device
schema_version: 1
doc_type: how-to
status: review
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, gaggimate, kblob, measurement, iram, memory-protection, cycle-count]
confidence: high
---

# kblob: hot-loading kernels onto the running firmware

Flashing a new firmware build to measure one kernel change costs about two
minutes plus a reboot. A round of kernel tuning needs dozens of such
measurements, so a flash-per-edit loop dominates the wall time of the work
rather than the kernel work itself[^1]. This repository's `kblob` rig closes
that loop: it compiles one animation source, sends it to a running device
over HTTP, links it against that device's own symbol table, and times it
with the CPU cycle counter, so an edit-measure round trip is a few seconds
instead of a flash cycle, and the numbers come from the real core, the real
caches, and the real PSRAM bus rather than a model of them[^1].

This document describes the rig as it exists in this repository: the build
environment it needs, the device-side loader, the bench that runs on the
device, the four debug endpoints, the host tool that drives all of it, how
to read its output table, and the traps its first evening of use already
paid for.

## The `display-kdev` build

`kblob` is a separate PlatformIO environment, `display-kdev`, defined by
extending the production `display` environment and adding one build flag
and one layered `sdkconfig` file[^2]:

```
[env:display-kdev]
extends = env:display
board_build.cmake_extra_args = -DEXECUTABLE_COMPONENT_NAME=src -DSDKCONFIG_DEFAULTS=sdkconfig.common.defaults;sdkconfig.gaggimate.defaults;sdkconfig.kdev.defaults
build_flags =
    ${env:display.build_flags}
    -DGM_KBLOB=1
```

`sdkconfig.kdev.defaults` is layered after the production `sdkconfig`
files and sets two Kconfig options[^3]:

- `CONFIG_ESP_SYSTEM_MEMPROT_FEATURE=n` turns off the ESP32-S3's memory
  protection scheme (PMS). The loader (below) writes the incoming kernel's
  machine code into a buffer through that buffer's DRAM alias, because the
  buffer itself sits in the IRAM address space and the instruction bus
  cannot be written through directly. With memory protection on, production
  firmware's PMS configuration makes the IRAM half of SRAM1 execute-only,
  and writing through its DRAM alias faults; that fault is exactly the W^X
  (write-xor-execute) guarantee production is built to keep, so this option
  is off only for `display-kdev` and never for a production build[^3].
- `CONFIG_APP_RETRIEVE_LEN_ELF_SHA=16` widens the number of hex digits
  `esp_app_get_elf_sha256()` reports, from the ESP-IDF default of 9 to 16,
  so the full 8-byte firmware-identity field in the blob's header (below)
  is checked in full rather than against a shortened prefix[^3].

Because the PMS is a permission filter on the bus and not a cache, an
arbiter, or anything else that changes timing, turning it off does not
change how any instruction or memory access is timed; a cycle count taken
on `display-kdev` is stated to transfer unchanged to the production
build[^3]. `display-kdev` is nonetheless a bench environment and never a
production knob for two reasons beyond the security posture: it disables a
guarantee production is built to keep, and this repository's own comparison
of an idle `display-kdev` boot against an idle production boot on the same
internal-DRAM instrumentation found `display-kdev` idling about 16 KB lower
on internal free heap[^4]. Nothing this document measures about a kernel's
own instruction and memory cost depends on that gap, but it means the
environment's own idle heap cannot be read as a production heap number.

## The device-side loader: `KBlob.h` / `KBlob.cpp`

`src/display/ui/default/bganim/KBlob.h` and `.cpp`, compiled only when
`GM_KBLOB` is defined, hold two fixed buffers[^5]:

- `g_text`, `GM_KBLOB_TEXT_CAP` bytes (12 KiB by default), placed in the
  `.iram1.kblob` linker section so the CPU fetches from it like any other
  `IRAM_ATTR` function.
- `g_data`, `GM_KBLOB_DATA_CAP` bytes (4 KiB by default), an ordinary
  writable array holding the incoming rodata, initialised data, and (once
  zeroed) bss.

An incoming image is a 64-byte header (magic `"GMKB"`, a version, the two
buffers' addresses and sizes as the host must have linked them, the
firmware ELF sha the blob was linked against, a name, and a CRC-32 over the
payload) followed by the text image (padded to 4 bytes) and the data
image[^5]. `kblob::install()` validates, in order: the magic and version;
that the header's addresses and sizes match this build's buffers (so a
blob linked against a different build's buffer layout is rejected before
any size arithmetic can wrap); that the total length matches the header's
declared sizes; that the firmware-sha field matches this device's own
sha (comparing the full 16 hex digits `CONFIG_APP_RETRIEVE_LEN_ELF_SHA=16`
makes available); that the payload's CRC-32 matches; that the header's
descriptor offset lies inside the data image; and, after copying, that the
descriptor's `init`, `frame`, and `band` function pointers are non-null and
point into executable memory[^5]. Any failure before the copy step leaves
the previous blob, if any, still resident; any failure after the copy has
already discarded it, because at that point installing anything, even a
descriptor that turns out to be invalid, is preferable to leaving stale
half-written state resident[^5].

Two details the source calls out as hard-won:

- The data image is copied to its full declared size and then the
  **entire remainder of the buffer** is memset to zero, not just the
  header's declared `bssSize`. An early version of the host tool computed
  the span to zero from the linker's `.kbbss` section size alone and
  ignored the alignment gap between the end of the data image and the
  start of that section, so the device could leave up to three stale bytes
  at the very end of a blob's bss. A stale high byte landing inside a
  table pointer made that pointer's `init()` skip its own allocation and
  the following `frame()` dereference null[^5][^6].
- After the copy, the code executes a plain `memw` followed by `isync`
  before ever calling into the new code. Internal SRAM is not cached, so
  nothing needs invalidating; the barrier exists only to guarantee the
  stores are visible ahead of the fetch and to discard anything the
  pipeline may have already prefetched from the old contents[^5].

## `SleepAnimation::runKBench`: what the device measures

`SleepAnimation::runKBench()`[^7] is the device-side bench. It measures up
to two animation objects side by side: object 0 is always the flashed
firmware's own descriptor for the requested registry id; object 1, when
present, is the hot-loaded blob's descriptor. For whichever objects are
requested, it calls that object's `init()` once, then repeatedly calls
`frame()` to advance state and times `band()` and, where present,
`bandRef()` on the resulting rows with `esp_cpu_get_cycle_count()`[^7].
This produces four "variants" per bench run:

| Variant | Object | Kernel |
|---|---|---|
| `band` | firmware descriptor | `band()` |
| `ref` | firmware descriptor | `bandRef()` |
| `blob` | hot-loaded blob descriptor | `band()` |
| `blobref` | hot-loaded blob descriptor | `bandRef()` |

For each band, the bench alternates which of the object's two kernels
meets that band first, band to band, "as `animtest` does"[^7], specifically
so that neither kernel's first (cold-cache) run always follows the other's
warm-up of the same tables; this is what the request parameter `which`'s
bit mask selects among (bit 0 firmware `band()`, bit 1 firmware `bandRef()`,
bit 2 blob `band()`, bit 3 blob `bandRef()`)[^7][^8]. For each band, each
requested kernel is called `n` times back to back (the request's `n`
parameter, min-of-n) and the bench records the minimum cycle count, the
first call's cycle count, and the running sum across all `n` calls[^7].

Every kernel's output for a band is hashed (FNV-1a over the row buffer) and
compared against the firmware's own `band()` hash for that same band,
which is what produces the "same pixels as band()" / "MISMATCH N bands"
line the host tool prints[^7][^9].

Because object 0 and object 1 are inited and stepped through `frame()`
independently, and only within their own loop, the bench measures each
object's two kernels (`band()` and `bandRef()`) against the *same*
`frame()`-produced state; it does not attempt to synchronise the firmware's
own possibly-already-running state with a fresh blob's. This is the reason
the two comparisons below have to be kept apart.

Before and after each object's turn, the bench reads the hot-slab
allocator's live-byte count (`bganim::hotUsed()`) and, if it is non-zero
after `release()` has run, logs a `kb: LEAK` line and resets the slab
before continuing[^7]. See "Traps" below.

## The four endpoints, and `useblob`

All behind `GM_KBLOB`, implemented in `src/display/plugins/WebUIPlugin.cpp`:

- **`GET /api/debug/kblob`** returns the two buffers' base addresses and
  capacities, the running firmware's ELF sha (`fw_sha`), and what is
  currently installed (`loaded`, `gen`, `name`, sizes, `last_err`)[^10].
- **`POST /api/debug/kblob`** takes the container image as the raw request
  body (staged in one PSRAM allocation as the body streams in, capped at
  64 KiB), asks the render task to detach the currently-resident blob if
  any (`SleepAnimation::kblobBeginInstall`, a two-second handshake), then
  calls `kblob::install()`. A blob that stays resident because the
  animation loop is stopped refuses with HTTP 409; a malformed or
  mismatched image refuses with HTTP 422[^10].
- **`GET /api/debug/kbench[?anim=N&n=8&frames=2&which=15]`** with the
  `anim` argument present, queues a bench run for registry id `N` and
  returns immediately; without it, or on a later poll, it returns the
  latest result and whether one is still `pending`. Cycle counts are
  reported at the live CPU clock (`cpu_mhz`); `min_cyc` sums, over all
  bands, the best of `n` runs per band; `first_cyc` sums each band's first
  run; `mean_cyc` is the mean over all runs[^11].
- **`GET /api/debug/anim?useblob=0|1`** switches the live render loop
  between the stored animation and the hot-loaded blob's descriptor, so the
  blob's actual visual output can be inspected on the panel
  (`/api/debug/fb`) without a flash[^12].

`SleepAnimation::activeSlot()` treats the blob as one more animation slot
(`KBLOB_SLOT`): with `useBlob` set, a blob installed, and no install in
progress, `activeSlot()` and `animBySlot()` route to `kblob::anim()`'s
descriptor instead of the stored registry entry, so the rest of the render
path (dirty tracking, band dispatch) does not need to know the difference[^13].

## The host tool: `tools/kblob/kb.py`

`kb.py` drives all of the above from a workstation. Its five subcommands:

```
kb.py info                            # what the device has loaded
kb.py build  AnimFoo.cpp [-o X]       # compile + link into X.kblob
kb.py upload X.kblob                  # install it
kb.py bench  --anim N [--n 8] [--frames 2] [--which 15]
kb.py run    AnimFoo.cpp --anim N     # build + upload + bench in one command
kb.py useblob 0|1                     # route the live render loop
```

### Compiling with the firmware's own flags

A blob has to be built with the exact compiler flags the firmware's own
build uses for that source, or its ABI and its assumptions about
optimisation, calling convention, and section placement would not match
what it is being linked and run against. `kb.py` gets that command from
`compile_commands.json`, PlatformIO's compilation database, by finding the
entry for an animation source of the target environment and stripping its
`-o` and input file[^14]. This is why the setup sequence is compiledb
first, flash second:

```
pio run -e display-kdev -t compiledb   # once: recreates .pio/build/display-kdev, writes compile_commands.json
pio run -e display-kdev                # then flash .pio/build/display-kdev/firmware.bin
```

PlatformIO's `compiledb` target deletes and rebuilds the whole environment's
`.pio/build/<env>` tree, the ELF included, and `compile_commands.json` is a
single file for the whole project, so running `compiledb` for one
environment overwrites what any other environment's build had written
there[^14][^15]. `kb.py` therefore never invokes `compiledb` itself; it
reads the database once and caches the extracted command per environment
under `.pio/kblob/<env>/compile_cmd.json`, so later `kb.py build`/`run`
invocations do not depend on the database still describing this
environment[^14].

### The firmware symbol index, keyed by ELF sha

A blob calls into the firmware's own exported helpers, libc, and libm
rather than linking its own copies, so every symbol the blob's object file
leaves undefined has to be resolved to the address that symbol has *in the
firmware the device is currently running*[^16]. `kb.py` builds that index
once per firmware build by running `nm --defined-only` over
`.pio/build/<env>/firmware.elf` and caching the result as
`.pio/kblob/<env>/syms-<sha>.json`, keyed by the ELF's own sha256 prefix so
the cache survives that ELF being rebuilt or deleted out from under it, and
so a stale index is never silently reused: if the device reports a
firmware sha with no matching cache file, `kb.py` requires the matching
ELF to exist on disk and its own hash to match before it will build a new
index[^16]. Every remaining undefined symbol in the blob's object is
resolved to that address with a generated linker script
(`.pio/kblob/<env>/<name>.ld`) that assigns each one to its firmware
address as an absolute symbol, before linking the blob's object and a
small generated descriptor stub (exposing the animation's `bg_anim_*`
symbol under the fixed section name `.kblob.desc`) at the device's two
buffer addresses[^17]. Any symbol the blob object needs that the firmware
does not export is a build-time error, listed by name, rather than a link
that silently produces a broken blob[^17].

### Upload and bench

`kb.py upload` and `kb.py bench` (and `useblob`) each take an exclusive
file lock (`.pio/kblob.lock`) around the device request, so multiple
workers can point at one physical board without their uploads or bench
runs interleaving; building does not take the lock, only device access
does[^18]. `kb.py run` performs build, upload, and bench as one operation
while holding the lock across upload and bench, which the README calls out
as the way to share one board: prefer one `kb.py run` invocation over
separate `build`/`upload`/`bench` calls when other workers may be
active[^1].

## Reading the output table

```
kbench plasma (anim 0) n=8 frames=2, ms per frame at 240 MHz
variant     min_ms  first_ms   mean_ms  bands vs band()
band          6.81      7.74      8.26    480 1.00x
ref           7.29      9.09      9.36    480 0.93x  same pixels as band()
blob          6.80     12.13      9.81    480 1.00x  same pixels as band()
blobref       7.29      8.12     10.89    480 0.93x  same pixels as band()
```

`band` and `ref` are the flashed firmware's `band()` and `bandRef()`;
`blob` and `blobref` are the hot-loaded source's[^1]. `min_ms` is `min_cyc`
converted to milliseconds per frame at the reported clock: it is the
deterministic compute-and-memory cost of the kernel, with interrupts (which
would otherwise inflate a run's cycle count) filtered out by taking the
best of `n` runs[^1][^19]. `first_ms` is each band's first run, which is
not filtered and so includes any one-time cost of that run (an
instruction-cache miss on a kernel that has not run yet, a table not yet
resident in whichever memory it will end up cached in). `mean_ms` is the
mean over all `n` runs and sits between the two, since only some of the
`n` runs are the "first" one[^19]. Every non-`band` variant's row is
checked, per band, against the firmware `band()` hash for that band, and
reports either "same pixels as band()" or a mismatch count with the first
mismatching frame and row[^7][^19].

### The two comparisons, and why they are not the same question

- **`blob` vs `blobref`** is the hand-written kernel against its own
  portable C++ reference, both compiled into the same blob, both running
  from the same IRAM buffer, both reading tables from the same placement.
  This is the comparison that answers "does the assembly (or the C++
  rewrite) actually help", because everything else about the two runs is
  identical[^1].
- **`blob` vs `band`** additionally differs in *where the code executes
  from*: the blob runs entirely from IRAM, while the firmware's own
  `band()` runs from flash through the 16 KiB instruction cache[^1]. A
  kernel that already fits in that cache pays for a miss only on its first
  invocation after a period of disuse, which `first_ms` reflects and
  `min_ms` filters out by construction (the minimum of several back-to-back
  runs is, for a kernel small enough to stay resident in the cache, a run
  that already found everything cached). Measured on the unchanged sources
  on 2026-09-04, the offset between `blob` and `band` at the minimum was
  effectively nil: plasma 6.80 ms (blob) against 6.81 ms (band), ripples
  1.84 ms (blob) against 1.89 ms (band)[^20]. Reading a `blob` vs `band`
  delta as a code win without first uploading the unchanged source once (to
  see what the placement offset alone is worth for that particular kernel)
  risks attributing a placement effect to a code change.

The host tool's own README states the practical rule that follows: run the
unchanged source once as a blob to establish the placement offset before
reading any further delta as a win, and confirm any apparent win with the
production `useblob` A/B before it is taken as ready to flash[^1].

## Traps the first evening on the rig taught

- **Never reflash the board while a bench round is running.** The blob's
  symbol resolution and its bench comparison are both against whatever ELF
  the device is currently running; reflashing mid-round changes that ELF
  out from under an in-flight comparison, producing numbers that describe
  neither the old nor the new firmware.
- **A `kb: LEAK: ... left N B in the hot slab after release()` line means a
  table allocated in that object's `init()` has no matching `release()`.**
  In production that pins the table's slab allocation for the remainder of
  the boot; in the bench, until the bench's own leak-detection-and-reset
  was added, one such leaking candidate silently pushed every later bench
  run on that same boot into measuring with its hot tables placed in PSRAM
  instead of the fast slab, which is roughly a 1.7x slowdown for the
  animation that first exposed it (nebula)[^7]. The bench now measures the
  slab's live-byte count before and after each object's turn and resets it
  on a non-zero reading, but a table missing its `release()` is still a
  correctness bug worth fixing at the source, not only masking in the
  bench[^7].
- **An animation whose `frame()` carries state across calls (nebula's
  scroll accumulators, starfield's RNG) cannot be compared `blob` against
  `band()` and expect matching hashes run to run**, because the firmware's
  own globals hold whatever state the panel's own history left there, while
  the blob's object starts from its own zeroed globals; the comparison that
  still holds unconditionally for such an animation is `blobref` against
  `blob`, both being the same object's own two kernels advanced through the
  same `frame()` calls[^7]. `/api/debug/animtest`, which compares `band()`
  and `bandRef()` inside one single resident instance after a flash, is the
  equality gate of last resort for a stateful animation[^1].
- **`min_ms` (min-of-n) mis-measures any kernel design that caches a row
  across separate `band()` calls keyed by absolute row position**, because
  the bench calls the same band `n` times back to back: such a cache either
  hits on every one of those repeats, in which case the reported minimum is
  a call that performed no work at all, or it misses on alternating calls,
  in which case the minimum still does not reflect a representative call.
  A cross-call, absolute-row cache has to be timed by the production
  `useblob` A/B instead of by this bench's `min_ms`[^1].

## Footnotes

[^1]: This repository, `tools/kblob/README.md` ("kblob: kernel iteration on the device").
[^2]: This repository, `platformio.ini`, `[env:display-kdev]` section.
[^3]: This repository, `sdkconfig.kdev.defaults`.
[^4]: This repository, `CLAUDE.md`, "Animation kernels" section ("the display-kdev env ... needs memory protection off ... so it is a bench env and never a production knob ... it idles 16 KB lower on internal free than production"), as of 2026-09-06. `[measured]`, no more precise log than this statement was found; treated here as this repository's own recorded comparison rather than an independently reproduced number.
[^5]: This repository, `src/display/ui/default/bganim/KBlob.h` and `KBlob.cpp`.
[^6]: This repository, commit `abc9bc82` ("bganim: kblob rig, load a kernel over HTTP and time it on the device"), commit message, first bullet under "Lessons the first evening on it paid for".
[^7]: This repository, `src/display/ui/default/SleepAnimation.cpp`, `SleepAnimation::runKBench()`.
[^8]: This repository, `src/display/plugins/WebUIPlugin.cpp`, comment above the `/api/debug/kbench` handler, describing the `which` bit mask.
[^9]: `[measured]` naming convention only; the hash function itself (`fnv1a`) is defined locally in `SleepAnimation.cpp` next to `runKBench()`.
[^10]: This repository, `src/display/plugins/WebUIPlugin.cpp`, the `/api/debug/kblob` `HTTP_GET | HTTP_POST` handler and its preceding comment block.
[^11]: This repository, `src/display/plugins/WebUIPlugin.cpp`, the `/api/debug/kbench` handler and its preceding comment.
[^12]: This repository, `src/display/plugins/WebUIPlugin.cpp`, the `/api/debug/anim` handler's `useblob` argument handling.
[^13]: This repository, `src/display/ui/default/SleepAnimation.cpp`, `SleepAnimation::activeSlot()` and `SleepAnimation::animBySlot()`.
[^14]: This repository, `tools/kblob/kb.py`, `compile_command()`.
[^15]: This repository, `CLAUDE.md`, note on PlatformIO recreating `.pio/build/<env>` (and its `compile_commands.json`) whenever the project checksum, which includes the core version, changes; the same recreate-on-`compiledb` behaviour is what `kb.py`'s `compile_command()` works around.
[^16]: This repository, `tools/kblob/kb.py`, `firmware_symbols()` and the module docstring.
[^17]: This repository, `tools/kblob/kb.py`, `build()`, the `LD_SCRIPT` and `DESC_C` templates.
[^18]: This repository, `tools/kblob/kb.py`, `DeviceLock`, and `tools/kblob/README.md`, "Sharing the board".
[^19]: This repository, `tools/kblob/kb.py`, `print_bench()`, and `tools/kblob/README.md`, the description of `min_ms`/`first_ms`/`mean_ms`.
[^20]: `[measured]` 2026-09-04, this repository, `tools/kblob/README.md`, "Two numbers to keep apart" section, citing the unchanged-source plasma and ripples figures quoted here.
