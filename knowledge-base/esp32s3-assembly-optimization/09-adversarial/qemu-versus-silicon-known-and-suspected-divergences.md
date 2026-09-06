---
title: QEMU versus silicon, known and suspected divergences
id: 09-adversarial/qemu-versus-silicon-known-and-suspected-divergences
schema_version: 1
doc_type: reference
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, qemu, pie, adversarial, verification]
confidence: medium
---

# QEMU versus silicon, known and suspected divergences

[QEMU for the ESP32-S3, what it proves and what it cannot](../05-measurement/qemu-esp32s3-what-it-proves-and-what-it-cannot.md)
already states the headline rule: QEMU proves an instruction sequence is
legal and bit-exact against a reference; it never proves a cycle count, and
its PIE arithmetic has documented open bugs. This page goes one layer
further; it is a register of every way the emulator can hand a kernel
author a wrong answer while looking like a pass, sourced against the
espressif/qemu tree directly rather than against a summary of it. Read the
05-measurement leaf and
[PIE load and store instructions and their alignment rules](../02-pie-vector/pie-load-store-and-alignment.md)
first; this page extends both and repeats neither.

All source citations below are against `github.com/espressif/qemu`, branch
`esp-develop`, commit `febae182e132e4055529be423a818225ebddaa3a`, which was
also the branch tip (`git rev-parse esp-develop`, GitHub API) at the time of
writing.[^head] All issue and pull-request states were checked against the
GitHub API on 2026-09-06 and are current as of that date.

## 1. Confirmed mismatches: the open issue tracker

Six ESP32-S3 PIE issues were open, unassigned to any pull request, and
carrying hardware-measured expected values as of 2026-09-06:

| Issue | Divergence | Status 2026-09-06 |
|---|---|---|
| #161 (QEMU-300) | `EE.VADDS`/`EE.VSUBS` clamp a negative saturation to `-MAX` instead of `MIN` | Open, no linked PR[^issues] |
| #162 (QEMU-301) | `EE.LDXQ.32`/`EE.STXQ.32` compute an indexed address four bytes too low | Open, no linked PR[^issues] |
| #163 (QEMU-302) | With `SAR=32`, `EE.VSL.32`, `EE.VSR.32` and `EE.VMUL.*` shift by zero instead of shifting out | Open, no linked PR[^issues] |
| #164 (QEMU-303) | `EE.CMUL.S16` corrupts results when the destination aliases the first input | Open, no linked PR[^issues] |
| #165 (QEMU-304) | `EE.VMULAS.U8.QACC` fails unsigned 20-bit saturation | Open, no linked PR[^issues] |
| #166 (QEMU-305) | `EE.SLCXXP.2Q`/`EE.SRCXXP.2Q` do not mask a dynamic count to `as[3:0]` and can terminate QEMU | Open, no linked PR[^issues] |

A GitHub code search for `PIE` and for `translate_tie_esp32s3` across the
repository's issues and pull requests returns exactly these six and nothing
else: no seventh PIE arithmetic issue, open or closed, and no pull request
touching that file since the commit that extended it.[^search] The commit
history of `target/xtensa/translate_tie_esp32s3.c` on `esp-develop` has
exactly two entries: the original 2024-04-23 implementation and the
2026-03-27 extension commit `ba59503` that the 05-measurement leaf already
describes.[^history] Nothing has landed against PIE semantics since. Treat
the six-issue table as complete, not illustrative, until that history
changes.

### A seventh, unlisted divergence: CPENABLE resets to 0, not 0xff

The 05-measurement leaf reports issue #154 (a freestanding image hanging
because QEMU boots with `CPENABLE = 0`) and calls it unresolved, "whether
that is an emulator defect or a missing handler in the guest."[^qemu-leaf]
That framing is stale. Issue #155 opened the same day as #154 and is itself
an open pull request, not a discussion: it reports `rsr.cpenable` reading
`0xff` as the first instruction in `main` on real ESP32-S3 silicon, against
`0x00` under `esp-develop-9.2.2-20260417`, and proposes the one-line fix,
unmerged as of 2026-09-06.[^issue155] Reading `target/xtensa/cpu.c` confirms
the mechanism directly: `xtensa_cpu_reset_hold` sets
`env->sregs[CPENABLE] = 0xff` only inside the `CONFIG_USER_ONLY` branch,
which builds the linux-user emulator, never the `#ifndef CONFIG_USER_ONLY`
branch that `qemu-system-xtensa -machine esp32s3` runs.[^cpu-reset] The ESP32-S3
SoC's own reset handler, `esp32s3_soc_reset`, does not set `CPENABLE`
either; it resets the interrupt matrix, the UARTs and the CPU, and nothing
more.[^soc-reset] So under the machine every leaf in this corpus that runs
a kernel against, `qemu-system-xtensa -M esp32s3`, `CPENABLE` reads `0` at
reset, and the chip it emulates reads `0xff`. This is not unresolved. It is
a confirmed, sourced, currently unfixed divergence, and it means: do not
trust a QEMU test of "does my startup code correctly enable a coprocessor
before using it" to catch a missing `CPENABLE` write, because the emulator
will fail loudly (issue #154's hang) where hardware would have silently
worked from reset. The [coprocessor and lazy-context leaf](../00-foundations/coprocessors-cpenable-and-lazy-context.md)'s
advice to set `CPENABLE` explicitly in a bare-metal harness is unaffected
either way; it is correct regardless of which value reset leaves behind,
because ESP-IDF's lazy scheme means the value that matters is the one your
own startup or scheduler establishes, not the reset default.

## 2. Unmodeled behaviour: real answers QEMU cannot give

These are not bugs in the sense of "wrong instruction result." They are
places where QEMU has no model at all, so any question about them gets an
answer that looks precise and is not connected to the chip.

- **Cycle timing, at any granularity.** Already established in the
  05-measurement leaf: `CCOUNT` tracks a 40 MHz virtual clock derived from
  wall-clock time, not the 240 MHz core.[^qemu-leaf] Nothing here
  contradicts that; direct reading of `op_helper.c`'s `update_ccount`
  confirms the same mechanism at the current head commit.
- **Cache, PSRAM wait states and MSPI bus contention.** `hw/misc/esp32s3_cache.c`,
  454 lines, contains zero occurrences of "wait", "latency", "delay",
  "cycle", "penalty" or "stall" in its text; it implements address
  translation, decryption and the cache control registers and nothing that
  costs time.[^cache-src] The same search across `hw/xtensa/esp32s3.c` and
  `hw/dma/esp32s3_gdma.c`, the machine and GDMA files, also returns zero
  hits.[^hw-src] Flash and PSRAM sharing one MSPI bus, and two DMA masters
  contending for it, is a real cost on the device and an absent one in the
  emulator: a kernel whose real bottleneck is bus contention will look
  identical in QEMU regardless of how much contention a second core or a
  radio would add. The 03-memory-hierarchy bucket already flags cross-core
  MSPI contention as unquantified even on the device;[^mem-readme] QEMU
  does not even attempt it.
- **Coprocessor context-switch cost.** The trap that fires when a
  coprocessor bit is clear, `gen_check_cpenable` calling
  `gen_exception_cause`, is functionally faithful: it dispatches the same
  exception cause the ISA defines.[^translate-cp] It carries no modeled
  latency, because nothing in this emulator models latency for anything.
  The register-save microcode a real lazy-context switch executes on
  silicon has a real cost; in QEMU the whole trap-dispatch-and-return
  sequence costs whatever host wall-clock time it happens to take, scaled
  by the fake 40 MHz clock. A measurement of "how expensive is the first
  PIE instruction after a task switch" is a device-only question for this
  reason as much as for the general no-cycle-model reason.

## 3. An operand the TRM half-defines and QEMU drops

[PIE compute instructions](../02-pie-vector/pie-arithmetic-multiply-saturate-and-shuffle.md)
already documents this precisely: `EE.SRCMB.S8.QACC`, `EE.SRCMB.S16.QACC`
and `EE.SRS.ACCX` take a third operand the manual's assembler syntax line
writes as a literal `0`, with no field for it in the instruction-word
diagram, while the toolchain's own configuration names it `sel2` and
accepts 0 or 1 as two distinct encodings, and esp-dsp passes 1 once with no
comment.[^sel2] QEMU's translator for `EE.SRCMB.*` reads the same operand
into a variable named `sel` and passes it to the helper function, which
never references the parameter in its body; the `EE.SRS.ACCX` translator
does not decode the operand at all.[^sel2] That leaf correctly marks the
silicon behaviour `[uncertain]`.

The adversarial framing to add: QEMU's silence here is not evidence of
anything. An instruction whose only documented legal encoding is `sel2=0`
would look identical whether the hardware defines a real behaviour for
`sel2=1` that QEMU fails to model, or whether `sel2=1` is genuinely inert
on both hardware and in QEMU, because the toolchain accepts it as a
distinct encoding for reasons unrelated to runtime effect (die revision,
reserved-for-future-use, or an internal test hook). QEMU agreeing with a
hypothesis of "does nothing" is exactly what QEMU would also do if the true
answer were "does something the emulator's author did not implement." The
only test that distinguishes the two is running `sel2=1` against
`sel2=0` on hardware with an accumulator state chosen so any difference in
selected lanes would show up in the output, which is squarely a device-ladder
question and not one QEMU can help with regardless of how the emulator
result comes out.

## 4. A shift-amount shadow that can go stale: extending issue #163

Issue #163 reports `EE.VSL.32`, `EE.VSR.32` and `EE.VMUL.*` shifting by
zero instead of shifting out when `SAR=32`, and attributes it to a 32-bit C
shift by 32, undefined behaviour.[^issue163] Reading the translator shows a
second, distinct path into the same bug class that the issue's own
reproducer does not exercise.

`translate_vsx32_s3` (which backs `EE.VSL.32` and `EE.VSR.32`) and
`translate_vmul_s3` (which backs the whole `EE.VMUL.*` and `EE.CMUL.*`
family) both contain the same branch: if `dc->sar_m32_5bit` is set, the
shift amount handed to the helper is not the live `SAR` special register at
all, it is `dc->sar_m32`, a compiler-internal shadow value.[^vsx-src] That
shadow exists for the base Xtensa ISA's own left-funnel-shift idiom:
`SSL as` and `SSA8B as` write `SAR <- 32 - (AR[as] & 0x1f)` and separately
cache `AR[as] & 0x1f` in `dc->sar_m32`, so that a later base-ISA
shift-by-`SAR` in the same translation block can use the pre-negation value
directly instead of re-deriving it from `SAR`.[^shadow-src] `SSR`, `SSAI`,
`SSA8L` and `WSR.SAR` all correctly invalidate the shadow when they run,
setting `dc->sar_m32_5bit` back to false.[^shadow-src]

The PIE reuse of this shadow is where the divergence lives. Suppose a
kernel executes `SSL a4` with `AR[a4] = 0`, which the base ISA defines as
setting `SAR = 32`, immediately followed, in the same basic block with no
intervening branch or SAR write, by `EE.VSL.32`. On the TRM's own terms
`EE.VSL.32` reads the full 6-bit `SAR[5:0]`, which is `32`, and should
shift out every bit.[^trm-vsx] QEMU's translator, seeing
`dc->sar_m32_5bit` still true from the `SSL`, passes the shadow value,
`0`, not `32`, as the shift amount, and the vector shift becomes a no-op.
This is a different mechanism from the literal `>> 32` undefined behaviour
the issue names, reachable without ever writing 32 into `SAR` through a
direct `WSR.SAR` or `SSAI`, and it is not the reproducer issue #163
describes.

This is source-derived reasoning, not something run against hardware or
QEMU in the course of writing this page. `[uncertain]`, both whether real
silicon's behaviour differs from `dc->sar_m32`'s value in this exact case
(it plausibly does, since the TRM defines no relationship between the base
ISA's negated shift convention and the PIE unit's raw `SAR[5:0]` read) and
whether a real kernel would ever sequence `SSL`/`SSA8B` immediately before
a PIE shift, since the documented PIE idiom sets `SAR` with a plain
`WSR.SAR`.[^pie-reg-leaf] The device test that would settle it: execute
`ssl a4` with `a4=0`, then `ee.vsl.32 q1, q0` in the same block, on both
QEMU and hardware, and compare. If they agree, this shadow reuse is
harmless in practice because some other invalidation this reading missed
protects it; if they disagree, it is an eighth PIE divergence, unreported
as of 2026-09-06, and narrower in trigger than issue #163's own reproducer.

## 5. What is correctly modelled, for calibration

Not every corner is a trap, and an adversarial page that only lists
failures teaches the wrong lesson: that nothing in QEMU can be trusted. Two
places where direct source reading confirms the emulator matches the
manual, stated so the failures above read as the exception rather than the
rule.

- **The forced low-address-bit masking on every PIE memory access** that
  [the load/store leaf](../02-pie-vector/pie-load-store-and-alignment.md)
  documents from the TRM is modelled faithfully: `translate_vld_128_s3`
  masks the address with `0xfffffff0` before the memory operation, and
  `translate_ld_128_usar_s3` additionally captures the masked-off low bits
  into `SAR_BYTE`, matching the manual's own worked example.[^align-src]
  This is one of the few places a QEMU test of alignment behaviour is
  actually informative about the chip.
- **`LD.QR`'s masking, despite its own pseudocode showing none.** The
  load/store leaf notes the manual's operation line for `LD.QR` shows no
  address mask, unlike every other load entry, and treats that as an
  apparent manual inconsistency rather than real unmasked behaviour,
  because the general alignment rule in Section 1.5.3 covers the whole
  extended instruction set.[^ldqr-note] Direct reading of
  `translate_ld_qr_s3` confirms QEMU's author read it the same way:
  the address is masked with `0xfffffff0` before the displacement is
  added, exactly like every masked load.[^align-src] This is not proof the
  manual's pseudocode is wrong and QEMU is right; it is one data point in
  the same direction the load/store leaf already argued from the manual's
  prose. A device test of `LD.QR` at an unaligned address, comparing
  against `EE.VLD.128.IP` at the same address, is still the only way to
  close this out, because two documents agreeing that were plausibly
  written by people who read the same section is not two independent
  confirmations.

## 6. What a kernel author must re-check on the device, and where

| Category | What to re-check | Corpus leaf with the device ladder |
|---|---|---|
| Saturation and destination aliasing (#161, #164, #165) | Any use of `EE.VADDS`/`EE.VSUBS` at a saturating negative extreme, `EE.CMUL.S16` with the destination equal to an input, `EE.VMULAS.U8.QACC` near the unsigned 20-bit ceiling | [Bit-exact reference tests and fuzzing](../05-measurement/bit-exact-reference-tests-and-fuzzing.md), the QEMU-then-device step of the five-layer ladder |
| Indexed addressing (#162) | Any use of `EE.LDXQ.32`/`EE.STXQ.32` | Same ladder; also [PIE load and store instructions and their alignment rules](../02-pie-vector/pie-load-store-and-alignment.md) for the addressing model itself |
| `SAR` at or above 32, and any `SSL`/`SSA8B` immediately before a PIE shift or multiply (#163, and Section 4 above) | `EE.VSL.32`, `EE.VSR.32`, `EE.VMUL.*`, `EE.CMUL.*` whenever the shift amount can reach 32 or the preceding instructions set `SAR` through a base-ISA left-shift idiom | [PIE register file, state registers and context save](../02-pie-vector/pie-register-file-sar-and-context.md) for how `SAR` is shared with the base ISA; the ladder above for confirming the actual value |
| Dynamic cross-shift counts (#166) | `EE.SLCXXP.2Q`/`EE.SRCXXP.2Q` with a runtime-computed `as`, not just compile-time constants | Same ladder; the fuzzing leaf's sanitizer requirement matters here because an unmasked count is exactly the shape of bug ASan catches on the host reference first |
| `CPENABLE` at reset (Section 1 above) | Whether kernel startup code and the coprocessor test harness both discover a missing enable the same way | [Coprocessors, CPENABLE and lazy context](../00-foundations/coprocessors-cpenable-and-lazy-context.md); a QEMU pass here can hide a bug that would surface differently, or not at all, on silicon |
| `sel2` on `EE.SRCMB.*`/`EE.SRS.ACCX` | Whether the second encoding has any observable effect | [PIE compute instructions](../02-pie-vector/pie-arithmetic-multiply-saturate-and-shuffle.md); no ladder step below "device" applies, because QEMU cannot distinguish "inert" from "unimplemented" here |
| Anything timing-shaped: cache placement, MSPI contention, coprocessor switch cost | Every claim in this row category | [CCOUNT and timing a kernel](../05-measurement/ccount-cycle-counter-and-timing-a-kernel.md); QEMU is not on this ladder at all for timing |

## Open questions

- Whether the Section 4 shadow-reuse hazard is observable on real hardware,
  or masked by some invalidation path this reading did not find. Settling
  it needs one QEMU run and one device run of the four-instruction sequence
  given there, not a new tool.
- Whether `sel2=1` on `EE.SRCMB.*`/`EE.SRS.ACCX` does anything on silicon.
  `[uncertain]`, unchanged from the 02-pie-vector leaf; this page adds only
  the framing that QEMU's silence cannot help decide it either way.
- Whether espressif/qemu's issue tracker is complete, or whether divergences
  exist that nobody has filed. The six-issue table and the CPENABLE finding
  in Section 1 are what a code-search and a git-log pass surfaced on
  2026-09-06; a future pass should redo both searches rather than trust
  this page's count to still be current.
- Whether PR #155's fix, once merged, changes any measurement taken against
  an emulator built before the merge. Re-run any QEMU-side coprocessor test
  after upgrading past whatever release line first carries it.

## Sources

[^head]: espressif/qemu, GitHub API `repos/espressif/qemu/commits/esp-develop`, queried 2026-09-06. Head commit `febae182e132e4055529be423a818225ebddaa3a`. All file citations below are against this commit, fetched via `raw.githubusercontent.com` on 2026-09-06.
[^issues]: espressif/qemu issues #161 through #166, GitHub API `repos/espressif/qemu/issues/{161..166}` and `search/issues?q=repo:espressif/qemu+PIE+in:title,body`, queried 2026-09-06: total 6, all state `open`, none carrying a `pull_request` field.
[^search]: GitHub API `search/issues?q=repo:espressif/qemu+translate_tie_esp32s3+in:body` (6 results, the same six issues, no pull requests) and `search/issues?q=repo:espressif/qemu+EE.VMUL+in:title,body` (0 results), both queried 2026-09-06.
[^history]: GitHub API `repos/espressif/qemu/commits?path=target/xtensa/translate_tie_esp32s3.c&sha=esp-develop`, queried 2026-09-06. Two entries: `76f9e1f1546f` (2024-04-23, "hw/xtensa: implement ESP32-S3 TIE instructions") and `ba5950398f1e` (2026-03-27, "feat(xtensa/esp32s3): Extend the ESP32-S3's TIE instructions"). The same query against `target/xtensa/op_helper.c` shows no commit after 2023 touching that file, and none ever mentioning PIE.
[^qemu-leaf]: [QEMU for the ESP32-S3, what it proves and what it cannot](../05-measurement/qemu-esp32s3-what-it-proves-and-what-it-cannot.md), the paragraph beginning "One caveat sits on top of that," and its footnote 26 citing issue #154.
[^issue155]: espressif/qemu issue/PR #155, "hw/xtensa/esp32s3: reset CPENABLE to 0xff to match silicon (QEMU-294)", opened 2026-05-28, state `open` as of 2026-09-06 (GitHub API `repos/espressif/qemu/issues/155`, `pull_request.merged_at` is null). Body reports `rsr.cpenable` as the first instruction in `main` reading `0xff` on physical ESP32-S3 hardware against `0x00` under `esp-develop-9.2.2-20260417`, and proposes writing the vendor reset value in `esp32s3_soc_reset` rather than changing the shared `target/xtensa/cpu.c` reset path.
[^cpu-reset]: espressif/qemu, `target/xtensa/cpu.c`, `xtensa_cpu_reset_hold`. `env->sregs[CPENABLE] = 0xff;` appears once, inside the `#else` arm of `#ifndef CONFIG_USER_ONLY`, i.e. only under `CONFIG_USER_ONLY` (the linux-user build). The `#ifndef CONFIG_USER_ONLY` arm, which is what `qemu-system-xtensa` runs, sets `PS` and `pending_irq_level` and does not touch `CPENABLE`.
[^soc-reset]: espressif/qemu, `hw/xtensa/esp32s3.c`, `esp32s3_soc_reset`. The `ESP32S3_SOC_RESET_PROCPU` branch calls `xtensa_select_static_vectors`, `remove_cpu_watchpoints` and `cpu_reset` on the CPU object; no `CPENABLE` write anywhere in the function.
[^cache-src]: espressif/qemu, `hw/misc/esp32s3_cache.c`, 454 lines. `grep -ci 'wait\|latency\|delay\|cycle\|penalty\|stall'` returns 0.
[^hw-src]: espressif/qemu, `hw/xtensa/esp32s3.c` (1002 lines) and `hw/dma/esp32s3_gdma.c` (174 lines). Same grep as the cache file returns 0 for both.
[^mem-readme]: `03-memory-hierarchy/README.md`'s "not yet covered" list, as referenced from `05-measurement/README.md`'s own "not yet covered" entry on cross-core cache and MSPI-bus contention.
[^translate-cp]: espressif/qemu, `target/xtensa/translate.c`, `gen_check_cpenable`: `cp_mask &= ~dc->cpenable; if (option_enabled(dc, XTENSA_OPTION_COPROCESSOR) && cp_mask) { gen_exception_cause(dc, COPROCESSOR0_DISABLED + ctz32(cp_mask)); }`. No cycle or latency accounting in the function or in `gen_exception_cause`.
[^sel2]: [PIE compute instructions](../02-pie-vector/pie-arithmetic-multiply-saturate-and-shuffle.md), the paragraph beginning "`EE.SRCMB.*.QACC` and `EE.SRS.ACCX` take a third operand." Confirmed independently against `target/xtensa/translate_tie_esp32s3.c`: `translate_srcmb_qacc_s3` passes `arg[2].imm` to `HELPER(srcmb_qacc_s3)` as its `sel` parameter; the helper body never references `sel`. `translate_srs_accx_s3` reads only `arg[0]` for the shift amount and does not touch a third argument at all.
[^issue163]: espressif/qemu issue #163, "ESP32-S3 PIE: SAR=32 VSL/VSR/VMUL results differ from real hardware (QEMU-302)", opened 2026-08-02, state `open` as of 2026-09-06.
[^vsx-src]: espressif/qemu, `target/xtensa/translate_tie_esp32s3.c`. `translate_vsx32_s3` (backing `EE.VSL.32` and `EE.VSR.32`): `if (dc->sar_m32_5bit) { gen_helper_vsx32_s3(tcg_env, qa, qs, dc->sar_m32, op_type); } else { gen_helper_vsx32_s3(tcg_env, qa, qs, cpu_SR[SAR], op_type); }`. `translate_vmul_s3` (backing every `EE.VMUL.*`/`EE.CMUL.*` opcode table entry) contains the identical branch on `dc->sar_m32_5bit`.
[^shadow-src]: espressif/qemu, `target/xtensa/translate.c`. `gen_left_shift_sar` (used by `translate_ssl` and `translate_ssa8b`): `tcg_gen_andi_i32(dc->sar_m32, sa, 0x1f); tcg_gen_sub_i32(cpu_SR[SAR], tcg_constant_i32(32), dc->sar_m32); dc->sar_m32_5bit = true;`. `gen_right_shift_sar` (used by `translate_ssr` and `translate_ssai`) and `translate_wsr_sar` both set `dc->sar_m32_5bit = false`. `init_sar_tracker` resets the same flag to false at the start of each translation block.
[^trm-vsx]: Espressif Systems, 2025. *ESP32-S3 Technical Reference Manual*, Version 1.8, sections 1.8.186 `EE.VSL.32` and 1.8.191 `EE.VSR.32`. Both read "the value in the 6-bit special register SAR" and operate as a shift by `SAR[5:0]`.
[^pie-reg-leaf]: [PIE register file, state registers and context save](../02-pie-vector/pie-register-file-sar-and-context.md), section 4, "the esp-dsp fixed-point add kernel sets it with an ordinary `wsr.sar` before entering the vector loop."
[^align-src]: espressif/qemu, `target/xtensa/translate_tie_esp32s3.c`. `translate_vld_128_s3` masks the load address with `tcg_gen_andi_i32(addr, arg[1].in, 0xfffffff0)` before the memory access. `translate_ld_128_usar_s3` performs the same mask and additionally sets `SAR_BYTE` from the unmasked low four bits. `translate_ld_qr_s3` masks `arg[1].in` with `0xfffffff0` and then adds the displacement, the same pattern as every other masked load, despite the manual's `LD.QR` pseudocode showing no mask.
[^ldqr-note]: [PIE load and store instructions and their alignment rules](../02-pie-vector/pie-load-store-and-alignment.md), the paragraph on `LD.QR`'s pseudocode versus Section 1.5.3's general rule.

