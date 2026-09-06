---
title: esp-dsp, esp-nn and vendor kernel libraries as a frontier
id: 08-frontiers/esp-dsp-esp-nn-and-vendor-kernel-libraries
schema_version: 1
doc_type: explanation
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, pie, esp-nn, esp-dl, esp-dsp, esp32-p4, esp32-s31, roadmap]
confidence: medium
---

# esp-dsp, esp-nn and vendor kernel libraries as a frontier

[`esp-dsp-as-a-reference-kernel-library`](../06-kernel-patterns/esp-dsp-as-a-reference-kernel-library.md)
reads `esp-dsp` as a fixed snapshot: what one tagged release contains, and
what its ESP32-S3 assembly looks like as a pattern to copy. This document
looks at the same library, plus two more Espressif libraries with
hand-written PIE assembly (`esp-nn`, `esp-dl`), from a different angle:
how fast they are moving, what they added on ESP32-S3 recently, what
Espressif has said about where they are going, and a genuinely new chip
that showed up in their dispatch logic while this document was being
written. Checked 2026-09-06 against `esp-nn` tag `v1.3.0`
(`d8866fa3762ee9caf56712b4019d004d86e0f3f8`), `esp-dsp` tag `v1.8.2`
(`a53a0756833c045311ea1d79a2badf495cdfde4c`, the same tag the sibling
document uses) and `esp-dl` tag `v3.3.11`, cloned directly[^1].

## Release cadence: two different mechanisms

`esp-dsp` releases as git tags on a roughly-quarterly cadence that
accelerated through 2026: `v1.6.0` (2025-04-16), `v1.7.0` (2025-06-17),
`v1.8.0` (2026-04-08), `v1.8.2` (2026-05-11)[^2]. Every one of those tags
also exists as a matching version on the ESP Component Registry, the
package host `idf.py add-dependency` pulls from[^3].

`esp-nn`'s git tags tell a coarser story than its registry actually
shipped: `v1.2.0` (2026-04-06), `v1.3.0` (2026-08-26)[^4]. The registry, by
contrast, lists `1.2.1` through `1.2.9` and `1.3.1`, `1.3.2` between those
two tags, most of them from a burst between 2026-07-21 and 2026-09-04[^3].
The mechanism is visible in the repository's own CI history: a 2026-04
commit changed the component-upload workflow "from tag-based to
push-on-master trigger, use directories-based upload (reads version from
`idf_component.yml`)"[^5]. After that change, a version bump merged to
`master` publishes to the registry whether or not anyone tags it in git,
so the registry is the more current source for "what shipped," and a git
tag list understates how many releases a project pinning
`idf_component.yml` actually saw. `esp-dsp` has not adopted the same
untagged cadence as of `v1.8.2`; its registry and tag histories still
match[^2][^3].

## What the recent esp-nn releases added on ESP32-S3

The commits between `v1.2.0` and `v1.3.0` are almost entirely ESP32-S3
work, not new-chip work[^6]:

- A new im2col path for small input channel counts (`filter_wd * in_ch <
  16`) that "flattens the filter window for ACCX dot product instead of
  wasting SIMD lanes on padded channels," plus a fix for right/bottom
  padding handling that had been wrong when `pad_wd`/`pad_ht` were unset[^6].
- Depthwise convolution: routing `channels % 8` (not just `% 16`) through
  the padded assembly path, row-tiled processing for large inputs "to
  reduce cache pressure," and two crash fixes: a null-bias path that
  wrote to uninitialized scratch (`StoreProhibited` on device, fixed and
  covered by a new deterministic test case), and a scratch-buffer sizing
  function that used the wrong condition for asymmetric padding and
  silently under-allocated an 11,968-byte overflow on a 49x20x32 layer[^6].
- Fully connected: a new MAC16 assembly path with fused load-plus-MAC for
  aligned filters and a QUP-pattern unaligned path "from esp-dsp," 2x loop
  unrolling, and a C dispatch wrapper choosing among the resulting fast
  paths[^6].
- A shared, reusable dot-product kernel
  (`src/common/esp_nn_dot_s8_esp32s3.S`, two entry points: `_aligned_`
  using `ee.vld.128.ip` plus fused `ee.vmulas.s8.accx.ld.ip`, and
  `_unaligned_` using the USAR/QUP pattern for an unaligned filter
  pointer), which the convolution im2col path and the new fully-connected
  path both now call instead of each keeping its own inline dot
  product[^7].
- A new ESP32-S3 softmax kernel (vectorized find-max plus the softmax
  computation) and an average-pool C wrapper with an int16 accumulation
  fallback for pools too large for int8 accumulation to avoid
  overflow[^6].

The README's own before/after numbers for this window: 1x1 convolution
10.06x to 14.24x, fully connected 2.77x to 7.83x, ReLU6 9.87x to 11.48x,
max pool 6.33x to 7.83x, all on ESP32-S3[^8]. These are the library's own
measured ratios (optimized cycles against its `_ansi` reference), not a
number this document took; treat them as `esp-nn`'s claim, comparable in
kind to how this KB's own `[measured]` tag works but not eligible for
that tag here.

## A new chip: ESP32-S31

While reading `esp-nn` v1.3.0's dispatch code for this document, one
target name did not match anything in this repository's build: `esp32s31`.
It is not a typo for ESP32-S3. Espressif announced the ESP32-S31 in
2026-04 as a dual-core RISC-V SoC (Wi-Fi 6, Bluetooth 5.4, IEEE 802.15.4,
Gigabit Ethernet), with mass production and general availability
confirmed by 2026-07[^9]. `esp-nn` added a merge request titled "feat: add
ESP32-S31 support" that landed in `v1.3.0`[^6], and `esp-dsp`'s `v1.8.x`
series added a parallel `_arp4`-suffixed kernel family (21 files at
`v1.8.2`: dot products, FIR, biquad, FFT, matrix multiply) that both
ESP32-P4 and ESP32-S31 dispatch into through the same `#if
CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31` guard[^10].

Both libraries' comments say why the two chips share kernels: "ESP32-S31
shares the P4 PIE/SIMD ISA, so it reuses the ESP32-P4 kernels"
(`esp-nn` `include/esp_nn.h`, line 19)[^11]. `esp-dl`'s own support table
is the clearest statement of the grouping, and gives it vendor names:
ESP32-S3 has "PIE V1 instructions" with a "round half up" rounding
strategy; ESP32-P4 and ESP32-S31 have "PIE V2 instructions" with "round
half to even"[^12]. ESP32-S3's PIE is a Xtensa TIE extension (`EE.*`
mnemonics); ESP32-P4 and ESP32-S31 are RISC-V chips with a custom
extension using `esp.*` mnemonics and a GPR operand constraint of `x26`
through `x31`, confirmed directly in `esp-nn`'s `_riscv_pie` sources[^13].
This document takes no position on whether "PIE V2" is architecturally a
RISC-V vector extension distinct from Xtensa PIE, or the same design
retargeted to a different base ISA; Espressif's own text ("a bit refined
version," a maintainer's words in a GitHub issue[^14]) does not settle it,
and this repository's target chip is Xtensa ESP32-S3, so PIE V2's
internals are out of this document's scope. `[uncertain]`

## esp-dl: hand-written PIE kernels, not just a wrapper

Unlike `esp-dsp`, `esp-dl` (Espressif's inference framework for
quantized models, MIT-licensed[^15]) is not obviously a kernel library
from its name, so it is worth confirming directly: at `v3.3.11` it
carries 58 `.S` files under `dl/base/isa/tie728/` (Espressif's internal
name for the ESP32-S3 TIE PIE core config) and a parallel set under
`dl/base/isa/esp32p4/`[^16]. Coverage spans convolution, depthwise
convolution, matmul, add/sub/mul, pooling, requantization, PReLU, ReLU,
transpose and comparison ops, each usually with a plain and an
`_unaligned_` variant, dispatched at compile time through
`CONFIG_PIE_V1_BOOST` (TIE728) versus `CONFIG_PIE_V2_BOOST`
(ESP32-P4/ESP32-S31)[^17]. `[uncertain]`: this document did not locate
which ESP-IDF version or component first defines those two Kconfig
symbols; they do not exist in the copy of ESP-IDF 5.5.1 this repository
builds against, so `esp-dl`'s PIE dispatch likely needs a newer IDF.

`esp-dl`'s own conv2d kernel on ESP32-S3 uses the same instruction family
this KB's `02-pie-vector` bucket already documents in full
(`EE.VSMULAS.S8.QACC.LD.INCP`, accumulating per-lane into `QACC`)[^18],
while `esp-nn`'s shared dot-product kernel on the same chip uses
`EE.VMULAS.S8.ACCX` (accumulating to one 40-bit scalar)[^7]. That is not a
contradiction between the two libraries: the TRM documents both
registers for exactly this split, `QACC` for "multiplication-accumulation
operations" that keep one result per lane and `ACCX` for operations that
"require accumulating the result of all multipliers to one value"[^19],
and convolution needs one accumulator per output channel while a plain
dot product needs one scalar. See
[`pie-arithmetic-multiply-saturate-and-shuffle`](../02-pie-vector/pie-arithmetic-multiply-saturate-and-shuffle.md)
for the full instruction table; this document only notes that two
current, independently maintained Espressif codebases pick the register
that matches their output shape, which is a real-world confirmation of
that leaf's accumulator-choice guidance.

`esp-dl` ships an operator-authoring guide meant for an AI agent to
follow (`tools/agents/skills/espdl-operator/SKILL.md`, 773 lines, plus a
templates reference)[^20]. Two of its stated conventions are transferable
outside `esp-dl` itself: it tells the author not to place a new kernel in
`.section .iram1` ("IRAM is a scarce resource on ESP chips... let the
linker place functions in flash by default"), and it requires "both
aligned and unaligned cases" for every new SIMD kernel, matching the dual
entry points this document already found in `esp-nn`'s and `esp-dsp`'s
own code[^20].

## A documented alignment bug, and what fixed it

`esp-nn` issue #21, closed 2026-08-19, reported ESP32-S3's ReLU6 kernel
returning wrong values at one specific unaligned position: a 17-element
input produced a correct result everywhere except position 15, where a
value of `23` should have clamped to `6` and instead passed through
unclamped[^21]. The RISC-V port's ReLU kernel comment names this issue
directly while explaining its own fix: "`esp.vld.128`/`esp.vst.128` need a
16-byte aligned address (verified on ESP32-S31: unaligned pointers get
processed lane-shifted, see github.com/espressif/esp-nn issue #21 for the
ESP32-S3 counterpart). Consume bytes scalar until `data` is aligned."[^22]
The fix is a scalar head loop before the vector body, the same shape as
the alignment-precondition idiom the sibling `esp-dsp` document already
recorded[^23]; the difference here is that the bug shipped once on ESP32-S3
without that head loop, and got carried forward as a known trap into the
RISC-V port rather than repeated there.

## Espressif's own account of P4's maturity curve

`esp-nn` issue #11 ("Support for ESP32-P4?") is a maintainer's own
timeline, useful because it is a vendor engineer describing the process
rather than a release note describing the result. In order: "yes, the
optimizations will be included in esp-nn for esp32-p4... I do not have
any ETA at the moment"; then, after a user reported ESP32-P4 running
*slower* than ESP32-S3 on a real model, "we have just pushed the
first-cut support for ESP32-P4 - ... Convolution function is optimised
using inline assembly ... You should see yet better version for
convolution and also optimisations for other kernels as well in
follow-up releases"; then, after the user found the promised 2x-7x
speedup did not appear on their model, "this is just a start ... This
will definitely be better and surpass S3 in a week or two"; then a shared
patch that improved wall time but changed the model's output boxes
slightly (a quantization-adjacent regression the thread does not fully
resolve); then, weeks later, "the full-fledge ESP32-P4 optimizations and
further improvements with ESP32-S3 are added with the latest version"[^14].
The throughline: a new chip's PIE support in a vendor library ships
convolution first, arrives in stages measured in weeks rather than one
release, and an early "first-cut" claim of a multiplier speedup can miss
a real model's profile by a wide margin because the model's actual
bottleneck (in the thread's case, an unoptimized sigmoid/logistic kernel)
was not the operation the first pass optimized.

## What a kernel author can borrow from here

| Idiom | Library | Where |
|---|---|---|
| Compile-time PIE-generation dispatch keyed to Kconfig (`PIE_V1_BOOST` / `PIE_V2_BOOST`), not a runtime chip check | esp-dl | `dl/base/isa/dl_base_isa.hpp`[^17] |
| One reusable dot-product primitive (aligned and unaligned entry points) shared by every caller that reduces to a dot product, instead of each kernel inlining its own | esp-nn | `src/common/esp_nn_dot_s8_esp32s3.S`[^7] |
| Route a small-channel-count case through a flattened dot product instead of running the general vector path with padded, wasted lanes | esp-nn | v1.2.0-v1.3.0 conv im2col change[^6] |
| Choose `ACCX` for a single reduced scalar, `QACC` for one accumulator per output lane; do not default to one register for both shapes | esp-nn (ACCX) vs esp-dl (QACC) | [^7][^18], semantics in `02-pie-vector`[^19] |
| Never place a hot kernel in `.iram1`; let the linker keep it in flash-cached space | esp-dl | operator-authoring skill[^20] |
| An unaligned SIMD kernel needs an explicit scalar head/tail; a documented device bug (not a hypothetical) is what happens without one | esp-nn | issue #21 and its RISC-V fix[^21][^22] |
| Track the ESP Component Registry, not just git tags, for what actually shipped once a project's CI publishes on every `master` merge | esp-nn (registry) vs esp-dsp (still tag-based) | [^3][^5] |

## Open questions

- Which ESP-IDF release first defines `CONFIG_PIE_V1_BOOST` and
  `CONFIG_PIE_V2_BOOST`, and what selects between them per target.
  `[uncertain]`, see above.
- Whether ESP32-P4/ESP32-S31's RISC-V PIE extension and ESP32-S3's Xtensa
  PIE are the same microarchitectural design retargeted, or independently
  designed extensions that converged on a similar instruction shape
  (128-bit vector loads, an accumulator family, saturating multiply).
  Would be settled by an instruction-level comparison of the RISC-V PIE
  encoding against the Xtensa TIE definition, which is out of this
  document's primary sources.
- `esp-dl`'s benchmark_report.md and MobileNetV3/YOLO model-level numbers
  were not audited in detail here beyond the README's kernel table; a
  follow-up leaf could compare `esp-dl`'s S3 conv2d/depthwise numbers
  against `esp-nn`'s equivalent kernels on the same operations, since both
  libraries now optimize the same handful of NN primitives independently.
- Whether the ESP32-S31's "round half to even" default (as opposed to
  ESP32-S3's "round half up") is a QACC/PIE-V2 hardware instruction
  difference or a software convention `esp-dl` chose per target; the
  support-state doc states the behavior but not its cause[^12].

## Sources

[^1]: `github.com/espressif/esp-nn`, tag `v1.3.0`, commit `d8866fa3762ee9caf56712b4019d004d86e0f3f8`; `github.com/espressif/esp-dsp`, tag `v1.8.2`, commit `a53a0756833c045311ea1d79a2badf495cdfde4c`; `github.com/espressif/esp-dl`, tag `v3.3.11`. All three cloned directly (`git clone --depth 1 --branch <tag>`), fetched 2026-09-06.
[^2]: `github.com/espressif/esp-dsp` tag-to-commit mapping and each commit's committer date, via `gh api repos/espressif/esp-dsp/tags` and `gh api repos/espressif/esp-dsp/commits/<sha>`, fetched 2026-09-06: `v1.6.0` = `3a8bade3`, 2025-04-16; `v1.7.0` = `c36523d6`, 2025-06-17; `v1.8.0` = `196825de`, 2026-04-08; `v1.8.2` = `a53a0756`, 2026-05-11.
[^3]: ESP Component Registry API, `https://components.espressif.com/api/components/espressif/esp-nn` and `.../espressif/esp-dsp`, fetched 2026-09-06. `esp-nn` versions and `created_at` timestamps: `1.2.0~1` 2026-04-06, `1.2.1` 2026-04-08, `1.2.2` and `1.2.3` both 2026-04-27, `1.2.4` 2026-07-21, `1.2.5` 2026-07-29, `1.2.6` 2026-08-14, `1.2.7` 2026-08-18, `1.2.8` and `1.2.9` both 2026-08-19, `1.3.0` 2026-08-19, `1.3.1` 2026-08-28, `1.3.2` 2026-09-04. `esp-dsp` versions match its git tags 1:1 through `1.8.2`.
[^4]: `github.com/espressif/esp-nn` tags via `gh api repos/espressif/esp-nn/tags` plus per-commit committer dates, fetched 2026-09-06: `v1.2.0` = commit dated 2026-04-06, `v1.3.0` = commit dated 2026-08-26.
[^5]: `github.com/espressif/esp-nn`, commit message "Bump version to 1.2.0, update component upload workflow, add P4 to README" (part of the `v1.1.2`-to-`v1.2.0` range), via `gh api repos/espressif/esp-nn/compare/v1.1.2...v1.2.0`, fetched 2026-09-06.
[^6]: `github.com/espressif/esp-nn`, commit messages in the `v1.2.0`-to-`v1.3.0` range, via `gh api repos/espressif/esp-nn/compare/v1.2.0...v1.3.0`, fetched 2026-09-06. Commits quoted: "conv: S3 optimizations - im2col path, filter precompute, padding fix"; "depthwise_conv: S3 optimizations - dispatch, row-tiling, ch=8 fast path"; "fully_connected: S3 MAC16 assembly + C wrapper with multiple fast paths"; "softmax: add ESP32-S3 optimized implementation"; "avg_pool: add S3 C wrapper with int16 accumulation fallback"; "fix(depthwise/s3): initialize scratch for null bias" and its follow-up test commit; "fix(depthwise): size scratch for asymmetric padding on esp32s3"; "Merge branch 'feature/esp32s31_support'".
[^7]: `esp-nn` `src/common/esp_nn_dot_s8_esp32s3.S`, same repo/tag, full file (142 lines): header comment lines 8-15; `esp_nn_dot_s8_aligned_esp32s3` label line 33, `ee.zero.accx` line 36, fused MAC-and-load `ee.vmulas.s8.accx.ld.ip` line 51, final MAC line 57, `rur.accx_0` readout line 63; `esp_nn_dot_s8_unaligned_esp32s3` label line 83, USAR/QUP body lines 105-131. Commit "Add shared esp_nn_dot_s8 assembly and use in FC + conv", same compare range as [^6].
[^8]: `esp-nn` `README.md`, same repo/tag, "Kernelwise performance for s8 versions" ESP32-S3 table (lines approx. 62-77) and the commit "Bump version to 1.2.1, update S3 performance numbers in README" in the same compare range as [^6], which states the before/after ratios directly: "conv 1x1 14.24x (was 10.06x), FC 7.83x (was 2.77x), relu6 11.48x (was 9.87x), max_pool 7.83x (was 6.33x)."
[^9]: Espressif Systems news pages, fetched via web search 2026-09-06: "ESP32-S31 Now in Mass Production and Available for Purchase," `espressif.com/en/news/ESP32_S31_Mass_Production`; "Espressif Unveils ESP32-S31: A Dual-Core RISC-V SoC with Wi-Fi 6, Bluetooth 5.4, and Advanced HMI Capabilities," `espressif.com/en/news/ESP32_S31_Release`; product page `espressif.com/en/products/socs/esp32-s31`. Announcement dated 2026-04 per contemporaneous coverage (Hackster.io, Adafruit blog, Hackaday, 2026-04-07/08); mass production coverage dated 2026-07/08. This document did not independently open the Espressif news pages beyond the search result; the announcement and mass-production dates are `[uncertain]` to the extent they rely on third-party tech-press summaries rather than a directly fetched Espressif page.
[^10]: `esp-dsp` `v1.8.2`, `find . -iname '*_arp4.S'` under `modules/`, 21 files, cloned repo, fetched 2026-09-06 (list includes `dspm_mult_s16_arp4.S`, `dspm_mult_ex_f32_arp4.S`, `dspm_mult_f32_arp4.S`, the `dspi_dotprod_*_arp4.S` family, `dsps_dotprod_s16_arp4.S`, `dsps_dotprode_f32_arp4.S`, `dsps_dotprod_f32_arp4.S`, `dsps_fft2r_sc16_arp4.S`, `dsps_fft2r_fc32_arp4.S`, `dsps_fft4r_fc32_arp4.S`, `dsps_fird_s16_arp4.S`, `dsps_fird_f32_arp4.S`, `dsps_biquad_f32_arp4.S`); dispatch guard `#if CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31` in `modules/dotprod/include/dsps_dotprod_platform.h`, line 31.
[^11]: `esp-nn` `include/esp_nn.h`, same repo/tag, lines 19-20; identical guard and comment also in top-level `CMakeLists.txt`, lines 57-58.
[^12]: `esp-dl` `operator_support_state.md`, same repo/tag, "Quantization Strategy" section: "ESP32-S3 - PIE V1 instructions. Rounding strategy: rounding half up... ESP32-P4 / ESP32-S31 - PIE V2 instructions. Rounding strategy: rounding half to even."
[^13]: `esp-nn` `src/activation_functions/esp_nn_relu_s8_riscv_pie.c`, same repo/tag, line 46 comment "esp.* GPR operands must be x26-x31 (required on S31)"; same constraint repeated in `src/convolution/esp_nn_conv_riscv_pie.c` line 348 and `src/softmax/esp_nn_softmax_s8_riscv_pie.c` line 77.
[^14]: `github.com/espressif/esp-nn` issue #11, "Support for ESP32-P4?", fetched via `gh api repos/espressif/esp-nn/issues/11` and `.../issues/11/comments` 2026-09-06. Closed 2026-04-27. Quotes are the maintainer `vikramdattu`'s comments in that thread, in the order they appear there.
[^15]: `esp-dl` `LICENSE`, same repo/tag: MIT License, Copyright (c) 2021 Espressif Systems (Shanghai) Co., Ltd. (`esp-nn` and `esp-dsp` are both Apache-2.0, confirmed from each repo's own `LICENSE` file, matching the citation in the sibling `esp-dsp` document.)
[^16]: `esp-dl` `esp-dl/dl/base/isa/tie728/` and `esp-dl/dl/base/isa/esp32p4/`, same repo/tag, file counts by direct listing (58 `.S`/`.h` files under `tie728/`); "TIE728" naming confirmed in `tools/agents/skills/espdl-operator/SKILL.md`, "SIMD Architecture Overview": "ESP32-S3 (TIE728): Xtensa SIMD with 128-bit SIMD registers (Q registers)... ESP32-P4: RISC-V with PIE vector extension."
[^17]: `esp-dl` `esp-dl/dl/base/isa/dl_base_isa.hpp`, same repo/tag, lines 1-14 (`#if CONFIG_XTENSA_BOOST` / `CONFIG_PIE_V1_BOOST` / `CONFIG_PIE_V2_BOOST` guarding the three ISA headers); same macros referenced through `esp-dl/dl/base/dl_base.hpp` lines 45, 207, 245, 433. `[uncertain]`: the Kconfig symbol definitions were not located in this repository's local ESP-IDF 5.5.1 checkout (`~/.platformio/packages/framework-espidf`); likely defined in a newer IDF's SoC capability headers, not confirmed here.
[^18]: `esp-dl` `esp-dl/dl/base/isa/tie728/dl_tie728_s8_conv2d.S`, same repo/tag, lines 9, 25-33: comment "scalar * vecter and accumulate into QACC", instruction `EE.VSMULAS.S8.QACC.LD.INCP`.
[^19]: ESP32-S3 Technical Reference Manual, version 1.8 (local text, this repository's `docs-src/esp32s3-trm-v1.8.txt`), section 1.3.3 "QACC Accumulator Register" and 1.3.4 "ACCX Accumulator Register", page 41: "The QACC accumulator register is used for multiplication-accumulation operations on 8-bit or 16-bit data... QACC consists of 16 accumulator registers with 20-bit width [for 8-bit data]"; "Some operations require accumulating the result of all multipliers to one value. In this case, the ACCX accumulator should be used... ACCX is a 40-bit accumulator register." Full instruction semantics for both registers are in this KB's [`pie-arithmetic-multiply-saturate-and-shuffle`](../02-pie-vector/pie-arithmetic-multiply-saturate-and-shuffle.md), not repeated here.
[^20]: `esp-dl` `tools/agents/skills/espdl-operator/SKILL.md` (773 lines) and `tools/agents/skills/espdl-operator/references/esp-dl-templates.md`, same repo/tag. Quotes: Phase 7.4 "Important SIMD Conventions" - "Do NOT use `.section .iram1`... Let the linker place functions in flash by default"; Phase 7.3 "Always handle both aligned and unaligned cases."
[^21]: `github.com/espressif/esp-nn` issue #21, fetched via `gh api repos/espressif/esp-nn/issues/21` 2026-09-06. Closed 2026-08-19. Reproduction shows a ReLU6 kernel returning `23` (unclamped) instead of `6` at input position 15 of a 17-element buffer.
[^22]: `esp-nn` `src/activation_functions/esp_nn_relu_s8_riscv_pie.c`, same repo/tag, lines 26-30, comment directly citing issue #21 and describing the scalar head-alignment loop that follows.
[^23]: Alignment-precondition idiom (OR-then-mask two pointers before the vector path, or a scalar head loop up to the next aligned address) documented with esp-dsp examples in [`esp-dsp-as-a-reference-kernel-library`](../06-kernel-patterns/esp-dsp-as-a-reference-kernel-library.md), "Alignment and length preconditions" section.
