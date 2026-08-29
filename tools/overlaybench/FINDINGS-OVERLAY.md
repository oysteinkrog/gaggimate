# Findings: overlay pipeline optimization pass

Baseline: `BASELINE-OVERLAY.md`. Harness: `tools/overlaybench` (`make check`,
16/16 golden OK at commit `b078f868`). Host numbers below are a fresh
`--iters 50` run taken immediately after committing all four workers' output
(so they reflect exactly what's in `kernels/*.cpp` at HEAD, not the workers'
own self-reported numbers). "est. dev. cy/px" is the pure-ALU calibration
from BASELINE-OVERLAY.md (`host_ns/px x 19.2`) -- read the per-kernel notes
below before trusting it, because for two of the four kernels this number is
actively misleading about which variant is better on the real device.

## Results table

| kernel | variant | ns/px | est. dev. cy/px | bit-exact | PIE pending |
|---|---|---|---|---|---|
| scrim_build | ref_scalar | 0.329 | 6.31 | yes | -- |
| scrim_build | transposed34 | 0.374 | 7.18 | yes | -- |
| scrim_build | transposed_all | 0.412 | 7.91 | yes | -- |
| overlay_blend | ref_scalar | 0.602 | 11.56 | yes | -- |
| overlay_blend | scrim_branchless | 0.567 | 10.88 | yes | -- |
| overlay_blend | scrim_branchless_word | 0.595 | 11.42 | yes | -- |
| overlay_blend | blend_pie_model | 0.711 | 13.66 | yes | -- |
| overlay_blend | combined_pie_model | 0.742 | 14.25 | yes | -- |
| overlay_blend | pie_asm (blend, partial) | -- | -- | no (device-only) | **yes** |
| span_scan | ref_scalar | 0.478 | 9.17 | yes | -- |
| span_scan | spec | 0.481 | 9.24 | yes | -- |
| span_scan | block16 | 0.533 | 10.23 | yes | -- |
| span_scan | block32 | 0.563 | 10.82 | yes | -- |
| span_scan | pie_model | 0.727 | 13.95 | yes | -- |
| span_scan | pie_asm | -- | -- | no (device-only) | **yes** |
| halfres_expand | ref_scalar | 0.146 | 2.80 | yes | -- |
| halfres_expand | wide64 | 0.128 | 2.45 | yes | -- |
| halfres_expand | pie_model | 0.112 | 2.15 | yes | -- |
| halfres_expand | pie_asm | -- | -- | no (device-only) | **yes** |

Every row is a real, committed, `make check`-passing variant except the four
`pie_asm` rows, which are Xtensa-only inline assembly (guarded
`#if defined(__XTENSA__)`) that cannot build or run on this x86 host --
they carry `piePending=true` in their variant-table entry and have **never
been executed anywhere**, only assembled in isolation against the real
toolchain's assembler to confirm the mnemonics exist (see per-kernel notes).
Read this table as "candidates ready to try on the rig," not "verdicts."

## Read this before trusting the host ns column

BASELINE-OVERLAY.md already flagged that host x86 (out-of-order, wide
register file, big L1) and device Xtensa LX7 (in-order, windowed-ABI
register-starved, PSRAM-backed working sets) can disagree, and this pass
produced two clean examples where they disagree in *opposite* directions:

- **scrim_build's transpose variants are slower on host but that tells you
  nothing about the device.** The whole reason they exist is a 14.4 KB
  working set that fits entirely in host L1 (so the added transpose is
  pure overhead on x86) but is `ps_malloc`'d PSRAM on device, where a
  120-byte-stride access pattern may or may not thrash the PSRAM cache --
  this harness cannot see PSRAM cache behavior at all, on either side of the
  comparison. Take the host ns column for this kernel as noise.
- **overlay_blend's scrim variants disagree with the codegen evidence.**
  `scrim_branchless` (0 hardware loops, 97 instructions -- *more* than
  `scrimRow_ref`'s 94) is the fastest of the three on host; `scrim_branchless_word`
  (1 recovered hardware loop, 64 instructions -- the one the worker's own
  register-pressure analysis identifies as the real fix) is barely faster
  than `ref_scalar` and slower than `scrim_branchless` on host. This is
  because the register pressure that blocks Xtensa's zero-overhead LOOP
  instruction (windowed ABI, a handful of address registers) doesn't exist
  on x86-64's much larger physical register file with renaming -- so the
  host simply cannot see the effect this variant was built to fix. Trust
  the instruction-count/loop-recovery evidence over the host ns here.

Both of these are exactly the "host bench and device disagree" phenomenon
`tools/animbench/OPTIMIZE.md` already documents, now with two fresh,
independently-derived examples in a different pipeline stage.

## Per-kernel findings and integration notes

### 1. scrim_build -- the top prize, and an open question this harness cannot close

Confirmed cost: 33-38ms of the ~44ms UI-task publish (measured on the rig,
`d999cc15`'s skip-guard only avoids this on a same-shape recolor publish --
it still has to run at full cost whenever coverage actually moves).

**Codegen is not the bottleneck.** `buildScrim_ref` at -O2 fully inlines all
six `scrimTap3_ref` calls (447 instructions, 12 zero-overhead LOOPs, 2 per
pass) and every pass is already branch-free, integer-only, no libm. The
worker confirmed this rather than assuming it. The real question is memory
traffic: 3 of the 6 passes ("vertical", `step=sw=120`) touch the 120x120
grid with a 120-byte stride instead of sequentially, and all four scrim
buffers are PSRAM (`ps_malloc`). Whether the ESP32-S3's PSRAM cache absorbs
that stride or thrashes on it is not something a host harness -- x86, 14.4KB
working set, sits in L1 regardless of stride -- can answer.

**What's ready to test:** `buildScrim_transposed34` brackets the two
adjacent vertical passes (3, 4 -- the only same-shape adjacent pair in the
fixed H,H,V,V,H,V order) with a cache-blocked transpose (16x16 tiles, so the
transpose itself never touches either buffer with a stride) so both run
sequentially; pass 6 (isolated between two horizontal passes) is left alone.
`buildScrim_transposed_all` adds a second, independent transpose bracket
around pass 6 too, trading two more transposes for eliminating the last
strided pass. Both are bit-exact by construction -- see the parameter-mapping
proof in `scrim_build.cpp` (a transposed `scrimTap3_ref` call over rotated
coordinates is proven algebraically identical to the original strided call,
not just empirically matched against golden).

**Integration path:** swap `buildScrim_ref` for `buildScrim_transposed34` at
the call site (`SleepAnimation::buildScrim`, same signature) and re-run
`tools/rig_soak.py` timing the coverage-changing publish path specifically.
If that measures faster, `buildScrim_transposed_all` is the natural
follow-up; if the single bracket doesn't help, the two-bracket version is
even less likely to (more transpose overhead for the same uncertain payoff),
so there's no reason to try it independently first. **This is the one
finding in the whole pass where the harness's answer is "here are two
correct candidates, the rig has to pick," not "here is the winner."**

### 2. overlay_blend -- ship scrimRow_branchlessWord, composite work is unfinished but bounded

Confirmed cost: 16ms/frame.

**Direction 1 (dim/scrim pass), resolved.** `scrimRow_ref` gets zero hardware
loops despite being call-free, because the per-cell `continue` on
`SCRIM_INV_NONE` breaks GCC's fixed-trip-count requirement for a
zero-overhead LOOP. The worker disproved the obvious fix first:
`scrimRow_branchless` (replace the skip with an unconditional
`scrimCell_ref` call -- exact no-op substitution, since 32 is
`scale565_ref`'s identity factor, and the same trick the shipped
`scrimRowPie` already uses for its non-skippable vector lane) *still* gets
zero loops and, at 97 instructions, is worse than the 94 of `scrimRow_ref`.
Comparing loop bodies directly found the real blocker: `scrimCell_ref`
computes both of a cell's two words in parallel, and that register pressure
-- not the branch -- is what starves GCC's loop-count mechanism.
`scrimRow_branchlessWord` restructures to one word (two pixels) per trip
instead of one cell (four pixels), halving live temporaries, and this is
confirmed (via `asm.sh`) to recover 1 zero-overhead LOOP at 64 instructions.
**This is a codegen-proven, host-numbers-be-damned win** (see the section
above on why the host ns for this one actively points the wrong way).

**Direction 2 (composite pass), new ground with an honest gap.**
`blendRow_pie_model` groups 8 pixels and picks one of three per-group paths
(all-opaque copy / no-opaque vector blend / mixed scalar fallback) instead
of branching per pixel. This required a genuine correctness proof, not just
an optimization: a worked counterexample shows `a==255` cannot be folded
into the general blend formula (`blend565_ref`'s own reason for
special-casing full alpha as a direct copy) -- it's off by one LSB in a
channel whenever a background channel is below the foreground one -- so the
3-way group split is load-bearing, not decorative.

The real PIE version (`blendRow_pie_asm`) only implements the all-opaque
path in actual vector instructions (a plain aligned load/store, no
arithmetic to get wrong). The general-path arithmetic needs a vector add
that `scale565Oct` (the one shipped PIE precedent in this file) never
needed; the worker confirmed `ee.vadds.s16` exists by feeding candidate
mnemonics to the real assembler (not this repo's own dumps -- a standalone
probe against `xtensa-esp32s3-elf-as`), worked out on paper that the general
path is buildable with it, and **declined to write it**, because getting a
long hand-scheduled SAR-juggling sequence subtly wrong with no device or
QEMU run available was judged more likely than not. This is the right call,
not an incomplete deliverable -- shipping unverified vector arithmetic in a
composite-blend hot path is a worse outcome than leaving it scalar.

**Integration path:** ship `blendStage_scrimBranchlessWord` (dim pass fixed,
composite pass unchanged) now -- it's a strict, codegen-proven improvement
with no open questions. Composite-pass vectorization (`blendRow_pie_asm`'s
general path) is a real follow-up but needs someone with device/QEMU access
to finish and validate the arithmetic the worker left specified but
unwritten; until then it only helps frames whose overlay content is heavily
opaque (icons, solid fills), not mixed-alpha content (anti-aliased edges,
the clock hands), since mixed groups fall back to scalar today.

### 3. span_scan -- ship scanRow_spec unconditionally, block-skip needs a device check

Confirmed cost: 8-9ms/publish.

`scanRow_spec` hoists the per-publish-constant `cellRow != nullptr` check
out of the per-pixel loop (previously re-tested on every covered pixel,
despite `doScrim` never changing mid-publish). This is a free, unconditional
win -- same instruction count reduction whether the device numbers move or
not -- and there's no reason not to take it.

The block-skip variants (`block16`/`block32`: branch-free OR-accumulate
emptiness probe over sub-row blocks before falling back to per-pixel scan)
are the harness's clearest case of "worse on host, plausibly much better on
device." A host census of the synthetic overlay found ~60-64% of 16/32px
blocks entirely empty even on rows that are themselves non-empty (the gaps
between the dial rim, status icons, and text glyph strokes) -- the whole
point BASELINE-OVERLAY.md flagged: a *whole-row* fast path would have missed
most of this, because most "non-empty" rows are still mostly empty in
patches. On host, the OR-probe's own cost isn't hidden by anything (x86 also
doesn't have a documented device-side branch-misprediction cost for the
per-pixel scan the way Xtensa's in-order pipeline does), so it shows up as
pure overhead. Every `emitRun_ref` call these variants produce is proven
(not just tested) identical in order and arguments to `scanRow_ref`'s own,
for any input.

`scanRow_pie_model` is the bit-exact C description of the 16-pixel,
3-lane-masked block probe `scanRow_pie_asm` computes on the real vector
unit; it's the slowest variant on host by design (a byte loop standing in
for `EE.ANDQ`/`EE.ORQ`), since its job is being the correctness gate for the
asm, not a fast path itself.

**Integration path:** take `scanRow_spec` now, no caveats. Evaluate
`scanRow_block16` on the rig specifically -- the host regression here is
exactly the kind BASELINE-OVERLAY.md predicted for a per-pixel-branch-heavy
kernel, and the census data suggests a real device win is plausible even
though this harness can't confirm it. `scanRow_pie_asm` is the natural
follow-up once `block16`'s device behavior is known.

### 4. halfres_expand -- bandwidth-bound per the source's own comment; one hypothesis left to test

Confirmed cost: 7.7ms/frame.

The firmware's own comment on this stage already states it's bounded by
PSRAM write bandwidth to `band[]` (~15 MB/s) and that no loop rearrangement
moves it -- so this kernel's brief was narrower than the other three: find
the one thing that comment doesn't rule out, not re-litigate arithmetic.

`expandRow_wide64` (four pixels packed into one `uint64_t` memcpy) is a
sanity check, not a candidate: it's faster on host but Xtensa has no native
64-bit store, so GCC lowers it back to two 32-bit stores (confirmed by
reading the asm dump) -- expected to be a wash-to-worse on device, kept only
to validate the pairing arithmetic before writing real vector asm.
`expandRow_pie_model` is the bit-exact C description of the real target:
`expandRow_pie_asm` assembles four independently-computed 32-bit words into
one vector register (`ee.movi.32.q` x4) and flushes all 16 bytes with one
`EE.VST.128.IP`, turning four scalar stores into one vector store. The
worker verified this idiom against real shipping code rather than inventing
it: disassembling Espressif's own `libespressif__esp-dsp.a` found no
lane-replicate instruction that would let this follow `scale565Oct`'s
load-compute-store shape, but found the exact `movi.32.q` x4 + one
`EE.VST.128` pattern already in use in `dsps_memset_aes3`. The read side
(`halfBuf`, this kernel's `src`) is deliberately left untouched --
it's `MALLOC_CAP_INTERNAL` (SRAM), confirmed never the bandwidth problem the
source comment is about.

**Integration path:** `expandRow_pie_asm` is a cheap, self-contained
hypothesis test -- does a single 16-byte PSRAM write get treated as a full
overwrite by the write-allocate cache rather than four separate
read-modify-write cycles? The worker was explicit that "no improvement" is
a valid, useful, and reasonably likely answer here, not a failure of the
optimization pass -- the source's own comment is the best evidence available
until someone runs it. Worth trying precisely because it's cheap to try,
not because there's strong reason to expect a win.

## Expected end-to-end savings

None of this has been measured on the rig yet -- everything below is
"what the codegen and correctness evidence supports trying," ordered by
confidence, against the measured per-stage costs from BASELINE-OVERLAY.md
(band=20ms, blend=16ms, expand=7.7ms, fill=3ms, copy=3ms, push=8ms per
frame; snapshot=55-75ms, publish=40-50ms per UI refresh, of which scrim
rebuild is 33-38ms and span scan 8-9ms):

- **overlay_blend (16ms/frame, per-frame render path):** `scrimRow_branchlessWord`
  is the one variant in this whole pass with unambiguous codegen evidence
  (a recovered hardware loop, matching the shipped `scrimRowPie`'s own
  branchless-identity precedent) and no open questions -- safe to integrate
  and measure directly. Composite-pass vectorization is real but incomplete;
  its ceiling depends on how opaque-heavy real overlay content is, which
  this harness's synthetic generator only approximates.
- **span_scan (8-9ms/publish):** `scanRow_spec` is a free win, take it
  regardless. `scanRow_block16`'s ceiling is bounded by how much of the
  8-9ms is per-pixel branch cost vs memory traffic -- the ~60% empty-block
  rate is suggestive but this harness cannot convert that into a ms figure;
  only the rig can.
- **scrim_build (33-38ms/publish, the single largest number in the whole
  pipeline):** highest potential payoff *and* the least certain outcome.
  Both transpose variants are ready to try; whether either wins depends
  entirely on PSRAM cache behavior across a 14.4KB-per-buffer, 4-buffer
  working set that this harness cannot observe. Test `transposed34` first.
- **halfres_expand (7.7ms/frame):** low expected payoff (source already
  says bandwidth-bound) but `expandRow_pie_asm` is cheap to try and the
  worker's own framing -- "no improvement possible" is a legitimate
  finding here, not a gap in the work -- should set expectations before
  the rig run, not after.

## Open risks

- All four `pie_asm` variants are `piePending`: confirmed to assemble
  against the real toolchain's assembler (mnemonics exist, operand forms
  accepted) but **never executed** on hardware or in QEMU. They must not
  ship without a device or QEMU validation pass -- this is the harness's
  hard boundary, by design (PIE is a thread-context-only coprocessor,
  illegal from an ISR context, and none of the workers had device access).
- `overlay_blend`'s composite-pass PIE variant is genuinely partial: the
  general (non-opaque) blend path was deliberately left unimplemented in
  real vector asm after the worker confirmed the needed instruction exists
  but judged the hand-scheduled sequence too likely to ship silently wrong
  without a way to test it. Whoever picks this up needs device/QEMU access
  as a hard prerequisite, not an optional nice-to-have.
- `scrim_build`'s transpose variants are the one place this pass produced
  two live candidates with no harness-side way to rank them. Test order
  matters less than testing at all: either could plausibly make the 33-38ms
  number worse, not better, if the PSRAM cache already absorbs the current
  strided access pattern better than a naive host-side mental model of
  "stride is always bad" would suggest.
- Every host ns number in this document should be treated as a codegen and
  correctness signal, not a device performance prediction -- see "Read this
  before trusting the host ns column" above. Two kernels in this pass
  produced host numbers that point the wrong way; do not re-derive expected
  device wins from the ns/px column without cross-checking against the
  per-kernel qualitative notes.
- All golden files (`golden/*.bin`, 16 total after this pass) are covered by
  the existing `golden/*.bin binary` glob in `.gitattributes` -- confirmed
  before committing, not assumed. If a future kernel's golden file looks
  corrupted after a commit, check that file first; see BASELINE-OVERLAY.md's
  golden/correctness note for the full history of why this exists.
