# kblob: kernel iteration on the device

Flashing to measure a kernel change costs two minutes and a reboot. This
rig compiles one animation source into a blob, loads it into the running
`display-kdev` firmware over HTTP and times it with the CPU cycle counter,
so an edit-measure loop is a few seconds and the numbers come from the real
core, caches and PSRAM bus. The device pieces are
`src/display/ui/default/bganim/KBlob.{h,cpp}` (loader),
`SleepAnimation::runKBench` (bench) and the `/api/debug/kblob`,
`/api/debug/kbench` and `/api/debug/anim?useblob=` endpoints, all behind
`GM_KBLOB`; the host piece is `kb.py`.

## Setup (once per firmware)

    pio run -e display-kdev -t compiledb   # once: the compile flags kb.py reuses
    pio run -e display-kdev                # then flash .pio/build/display-kdev/firmware.bin
    tools/kblob/kb.py info                 # buffer addresses, fw_sha, what is loaded

The compiledb step comes first because PlatformIO recreates the env's build
tree when it writes compile_commands.json (the ELF goes with it), and the file
is one per project, so a compiledb for another env replaces this one's; kb.py
keeps its own copy of the command under `.pio/kblob/<env>/` and never runs
compiledb itself.

A blob is linked against the firmware's symbol table, so it is only valid for
the ELF the device is running. kb.py indexes the ELF's symbols once, keyed by
the sha the device reports (`.pio/kblob/<env>/syms-<sha>.json`), and the
device checks the sha again before installing; after any firmware change,
flash first and build blobs second.

## Loop

    tools/kblob/kb.py run src/display/ui/default/bganim/AnimPlasma.cpp --anim 0

builds, uploads and benches in one command and prints, per variant, the
milliseconds per full-resolution frame:

    kbench plasma (anim 0) n=8 frames=2, ms per frame at 240 MHz
    variant     min_ms  first_ms   mean_ms  bands vs band()
    band          6.81      7.74      8.26    480 1.00x
    ref           7.29      9.09      9.36    480 0.93x  same pixels as band()
    blob          6.80     12.13      9.81    480 1.00x  same pixels as band()
    blobref       7.29      8.12     10.89    480 0.93x  same pixels as band()

`band` and `ref` are the flashed firmware's `band()` and `bandRef()`; `blob`
and `blobref` are the uploaded source's. `min_ms` sums the best of `--n` runs
of every band (interrupts filtered out; the deterministic compute + memory
cost), `first_ms` the first run of each band, `mean_ms` all runs. Every
variant's output is hashed per band against the firmware's `band()`, so a
blob that is meant to be the same algorithm is checked for equality in the
same pass; a blob that is a different animation reports MISMATCH, which is
then expected.

Two numbers to keep apart:

- **blob vs blobref** is your asm against your C++ reference, both in the
  blob, both in IRAM, both with the same table placement. This is the kernel
  comparison.
- **blob vs band** mixes in placement: the blob runs from IRAM, the firmware's
  kernels from flash through the 16 KB instruction cache. Run the unchanged
  source once as a blob to see that offset before reading it as a code win.
  Measured 2026-09-04 on the unchanged sources it was nil at the minimum
  (plasma 6.80 vs 6.81 ms, ripples 1.84 vs 1.89 ms): a band kernel fits the
  cache and the first run of a band pays the misses, which is what first_ms
  shows and min_ms filters.

The bench inits each object (the firmware's descriptor, then the blob's)
with the whole hot slab to itself, the way production does, and times both
of its kernels band by band on the same `frame()` state, alternating which
one meets a band first. `--anim N` picks the registry entry whose parameters
and hot-slab tables it is measured with. Both blob variants of a fresh
animation (one not in the registry) can still be benched: give `--anim` any
id and read only the blob rows.

Two things the equality column cannot see through:

- An animation whose `frame()` carries state across calls (nebula's scroll
  accumulators, starfield's RNG) keeps that state in its own globals, and the
  firmware's globals hold whatever the panel left there while the blob's
  start zeroed. For those animations `blob` can differ from `band()` run to
  run while `blobref` still equals `blob`; the blobref-vs-blob check is the
  asm-against-C++ one and holds regardless. `/api/debug/animtest` (leader,
  after a flash) compares band() and bandRef() inside one resident instance
  and is the final equality gate.
- `kb: LEAK: ... left N B in the hot slab after release()` means a table
  allocated in `init()` has no matching `release()`. Fix it before anything
  else: in production that table pins the slab for the rest of the boot. The
  bench resets the slab after reporting, so one leaking candidate no longer
  pushes every later bench on the board into PSRAM (it did, for most of the
  first evening: every bench after the first leak ran with its tables in
  PSRAM until the next reboot).

## Visual check and production A/B

    tools/kblob/kb.py useblob 1        # live render loop uses the blob
    tools/kblob/kb.py useblob 0

With `useblob=1` the panel shows the blob, so a new animation or a kernel
that changes output can be looked at (`/api/debug/fb`) without a flash. The
production A/B (`C:\work\camshots\anim_devbench.py` with `KNOB=useblob`)
samples frame times alternating blob and firmware under real conditions,
which is the number that has to move before a kernel ships.

## Sharing the board

Upload, bench and useblob take `.pio/kblob.lock` (flock), so several workers
can point at one device; a run waits for the previous one. Building does
not take the lock. The blob is one slot: the next upload replaces it, so keep
build + upload + bench in one `kb.py run` rather than separate commands when
others are active.

## What it cannot do

- Load code that needs static constructors (the link asserts none) or
  exceptions in flight (unwind tables are discarded).
- Change anything outside the one source: a blob can only call what the
  firmware exports. A new `bganim::` helper means a flash.
- Stand in for the production A/B. IRAM placement and warm caches flatter a
  blob; `min_ms` ranks variants, it does not predict the frame time.
- Time a design that caches a row across band() calls. The bench calls the
  same band n times back to back and keeps the minimum, so a cache keyed by
  absolute row either hits on every repeat (the minimum is a call that did
  no sampling at all) or misses twice per call (when the call ends with a
  different row cached than it starts with). Mandala's interpolated design
  measured 8.1 and 19.8 ms this way for variants that production runs a few
  ms apart. A cross-call cache is timed by the useblob A/B only.
