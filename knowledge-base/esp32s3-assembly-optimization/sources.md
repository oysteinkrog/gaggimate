---
title: "ESP32-S3 assembly optimization KB: sources"
id: sources
schema_version: 1
doc_type: reference
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, bibliography, sources, citations]
confidence: medium
---

# Sources

This is the bibliography for the whole corpus (buckets `00-foundations`
through `09-adversarial`). It was built mechanically: a script pulled every
footnote definition (a line starting `[^id]:`) out of every leaf, grouped
them into distinct sources, and counted how many leaves cite each one. It
does not repeat what the leaves say about a source, only what the source
is and how often the corpus points to it.

A citation to this repository's own files or commits is not a source in
this list. Those citations live in `07-our-work/`, which is this
project's own evidence, not an outside reference. A citation to another
leaf in this same collection is a cross-reference, not a source, and is
also left out. Both are counted in the summary at the bottom so the
totals add up.

## 1. Primary documents

Published manuals, datasheets and papers the corpus cites by title.

| Title | Publisher | Version / date | Text consulted at | Leaves citing it |
|---|---|---|---|---|
| *Xtensa Instruction Set Architecture (ISA) Reference Manual* (document PD-09-0801-10-01) | Cadence Design Systems (Tensilica IP), also cited as "Tensilica" | Issue 4/2010, release RC-2010.1 | Public mirror `https://0x04.net/~mwk/doc/xtensa.pdf` | 22 |
| *ESP32-S3 Technical Reference Manual* | Espressif Systems | Version 1.8 | `https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf` (also mirrored at `https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf`) | 21 |
| *ESP32-S3 Series Datasheet* | Espressif Systems | Version 2.2 | `https://documentation.espressif.com/esp32-s3_datasheet_en.pdf` (also mirrored at `https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf`) | 5 |
| GNU Compiler Collection manual, "Xtensa Options" and "Machine Constraints" sections | GNU Project | GCC 14.2.0, matching the `xtensa-esp-elf` toolchain this KB cites elsewhere | `https://gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Xtensa-Options.html` and `.../Machine-Constraints.html`. One leaf also cites the unversioned, rolling `.../gcc/Xtensa-Options.html` page and flags that the rolling page had already dropped one flag documented on the pinned 14.2.0 page | 11 |
| GNU Binutils, `as` manual, "Xtensa Options" and "Xtensa Automatic Alignment" | GNU Project | Matches GNU assembler 2.43.1 (shipped with the `esp-14.2.0_20241119` toolchain) | `https://sourceware.org/binutils/docs/as/Xtensa-Options.html` | 2 |
| *Overview of Xtensa Instruction Set Architecture* (also titled *Overview of Xtensa ISA* in one citing leaf) | Espressif Systems | v0021604, 2021-02-17 | `https://dl.espressif.com/github_assets/espressif/xtensa-isa-doc/releases/download/latest/Xtensa.pdf` | 2 |
| *The microarchitecture of Intel, AMD, and VIA CPUs* | Agner Fog | Last updated 2026-05-23 | `https://www.agner.org/optimize/microarchitecture.pdf` | 1 |
| *ESP32-P4 Series Datasheet* | Espressif Systems | Pre-release v0.7 | `https://documentation.espressif.com/esp32-p4_datasheet_en.pdf`, fetched 2026-09-06 and converted with `pdftotext` | 1 |
| *ESP32-P4 Chip Revision v1.3 Technical Reference Manual* | Espressif Systems | Pre-release v0.4 | `https://documentation.espressif.com/esp32-p4-chip-revision-v1.3_technical_reference_manual_en.pdf`, fetched 2026-09-06 and converted with `pdftotext` | 1 |
| "Algorithm for computer control of a digital plotter" | J. E. Bresenham, *IBM Systems Journal* | Vol. 4, no. 1, 1965, pp. 25-30 | DOI `10.1147/sj.41.0025` | 1 |
| "Software Pipelining: An Effective Scheduling Technique for VLIW Machines" | Monica S. Lam, *Proceedings of the ACM SIGPLAN 1988 Conference on Programming Language Design and Implementation* | pp. 318-328 | DOI `10.1145/53990.54022` | 1 |
| *Digital Image Processing Laboratory: Image Halftoning* | C. A. Bouman, Purdue University | 2011-05-11 | `https://engineering.purdue.edu/~bouman/grad-labs/Image-Halftoning/pdf/lab.pdf` | 1 |
| "An optimum method for two-level rendition of continuous-tone pictures" | B. E. Bayer, *IEEE International Conference on Communications*, vol. 1 | 11-13 June 1973, pp. 11-15 | Not consulted directly; cited at one remove as a paper's own reference [1]. Marked `[uncertain]` in the citing leaf | 1 |

Three more documents are named in the corpus but were never actually
read; they are in section 4 instead of this table.

## 2. Source trees pinned by version

Software repositories cited by tag, branch or commit.

| Repository | Pin | What else is cited from it | Leaves citing it |
|---|---|---|---|
| ESP-IDF (`espressif/esp-idf`) | v5.5.1 (PlatformIO package `framework-espidf` 3.50501) | One leaf also reads the `master`-branch `tools/tools.json` for a current toolchain recommendation, and a GitHub API query against `repos/espressif/esp-idf/releases` for release dates; both are folded into this row rather than pinned separately | 33 |
| `xtensa-esp-elf` toolchain package (crosstool-NG build) | `esp-14.2.0_20241119` (GCC 14.2.0, GNU assembler 2.43.1, GNU Binutils 2.43.1) | Header and library files shipped inside the package (e.g. `lib/gcc/xtensa-esp-elf/14.2.0/...`) and every `[measured]` note that names "the same toolchain" | 18 |
| `espressif/gcc` (Espressif's GCC fork) | Branch/tag `esp-14.2.0_20241119`; lineage comparisons also read `esp-13_2_0`, `esp-15_1_0`, `esp-15_2_0` and `esp-16_1_0` | `gcc/config/xtensa/*.md`, `*.cc`, `*.opt` at those refs | 3 |
| `gcc-mirror/gcc` (read-only mirror of upstream GCC) | `releases/gcc-14` branch; annotated tags `releases/gcc-13.1.0`, `14.1.0`, `14.2.0`, `15.1.0`, `16.1.0` for lineage dates; commits `45f1fed76`, `9eba97e41`, `2379d07ac`, `23141088e` for specific fixes | `gcc/config/xtensa/xtensa.cc`, `xtensa.md` | 8 |
| `espressif/qemu` | Branch `esp-develop`, head commit `febae182e132e4055529be423a818225ebddaa3a`; earlier commits `ba5950398f1e` and `76f9e1f1546f`; releases `esp-develop-9.0.0-20240606` and `esp-develop-9.2.2-20260417` | `target/xtensa/*.c`, `hw/xtensa/esp32s3.c`, `hw/misc/esp32s3_cache.c`, `hw/dma/esp32s3_gdma.c` | 6 |
| `espressif/esp-dsp` | Tag `v1.8.2`, commit `a53a0756833c045311ea1d79a2badf495cdfde4c`; one negative search also checked `master` at `3c8ac0fdfec83740b783e200862c8d0c056de0ad` | `modules/dotprod/`, `modules/fir/`, `modules/iir/`, `modules/conv/`, `modules/fft/`, `docs/esp_bm_results.csv` | 9 |
| `espressif/esp-nn` | Tag `v1.3.0`, commit `d8866fa3762ee9caf56712b4019d004d86e0f3f8` | `src/common/`, `src/activation_functions/`, `include/esp_nn.h`, `README.md`, commit-range diffs `v1.1.2...v1.2.0` and `v1.2.0...v1.3.0` | 1 |
| `espressif/esp-dl` | Tag `v3.3.11` | `esp-dl/dl/base/isa/`, `operator_support_state.md`, `LICENSE`, `tools/agents/skills/espdl-operator/` | 1 |
| `espressif/llvm-project` (Espressif's LLVM/Clang fork) | Tag `esp-21.1.3_20260408` | `llvm/lib/Target/Xtensa/`, `clang/include/clang/Basic/BuiltinsXtensaESP32S3.def`, `clang/lib/Basic/Targets/Xtensa.h` | 1 |
| `llvm/llvm-project` (upstream LLVM) | `main` branch, most recent relevant commit `72417eb7`, 2026-09-06 | `llvm/lib/Target/Xtensa/`, `llvm/CMakeLists.txt` | 1 |
| LVGL | v8.4.0 (vendored UI library) | `src/misc/lv_color.h`, `src/misc/lv_math.h`, `src/draw/sw/lv_draw_sw_dither.c` | 1 |
| Apache NuttX | `arch/xtensa/Kconfig` on the `master` branch, unpinned, fetched 2026-09-06 | `ARCH_HAVE_MULTICPU`, `XTENSA_CP_LAZY` Kconfig options | 1 |
| `jcmvbkbc/newlib-xtensa` (community mirror) | `xtensa` branch | `newlib/libc/machine/xtensa/memcpy.S`, copyright header dated 2002-2008 to Tensilica Inc. | 1 |

## 3. Web pages and issues

### 3.1 Web pages, blogs, forums and documentation sites

| Title | URL | Date fetched | Leaves citing it |
|---|---|---|---|
| Espressif developer blog, "Cores with FPU" | `https://developer.espressif.com/blog/2025/10/cores_with_fpu/` | 2026-09-06 | 3 |
| gigabowser.dev, blog post on Xtensa `RSQRT`/reciprocal-square-root sequences | `https://gigabowser.dev/posts/xtensa-rsqrt/` | 2026-09-06 | 1 |
| Digital Signal Labs, floating-point paper (`fp.pdf`) | `http://www.digitalsignallabs.com/downloads/fp.pdf` | 2026-09-06 | 1 |
| ESP Component Registry API, `esp-nn` and `esp-dsp` component pages | `https://components.espressif.com/api/components/espressif/esp-nn` and `.../espressif/esp-dsp` | 2026-09-06 | 1 |
| Zephyr Project Documentation, "ESP32-S3 Features" | `https://docs.zephyrproject.org/latest/boards/espressif/common/soc-esp32s3-features.html` | 2026-09-06 | 1 |
| Zephyr Project Documentation, "Xtensa Developer Guide" | `https://docs.zephyrproject.org/latest/hardware/arch/xtensa.html` | 2026-09-06 | 1 |
| Apache NuttX documentation, "Espressif ESP32-S3" | `https://nuttx.apache.org/docs/latest/platforms/xtensa/esp32s3/index.html` | 2026-09-06 | 1 |
| Wikipedia, "Differential testing" (corroborates the McKeeman citation in section 4) | `https://en.wikipedia.org/wiki/Differential_testing` | 2026-09-06 | 1 |
| esp32.com forum thread on Cadence not publishing Xtensa per-instruction cycle counts | Not given in the citing leaf; consulted for that one observation only, not as a semantic source | Not given | 1 |
| Clang documentation, "AddressSanitizer" | `https://clang.llvm.org/docs/AddressSanitizer.html` | 2026-09-06 | 1 |
| Clang documentation, "UndefinedBehaviorSanitizer" | `https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html` | 2026-09-06 | 1 |
| LLVM documentation, "libFuzzer, a library for coverage-guided fuzz testing" | `https://llvm.org/docs/LibFuzzer.html` | 2026-09-06 | 1 |
| GCC Online Documentation, "Instrumentation Options" | `https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html` | 2026-09-06 | 1 |
| `espressif/esp-toolchain-docs`, `qemu/README.md` (`main` branch) | `https://github.com/espressif/esp-toolchain-docs/blob/main/qemu/README.md` | 2026-09-06 | 1 |
| `espressif/esp-toolchain-docs`, `clang/esp-idf-app-clang-build.md` (`main` branch) | `https://github.com/espressif/esp-toolchain-docs` | 2026-09-06 | 1 |
| QEMU project documentation, "Invocation" (the `-icount` entry) | `https://www.qemu.org/docs/master/system/invocation.html` | 2026-09-06 | 1 |
| Espressif Systems news pages, "ESP32-S31 Now in Mass Production and Available for Purchase" | `https://www.espressif.com/en/news/ESP32_S31_Mass_Production` (fetched via web search) | 2026-09-06 | 1 |
| LLVM Discourse, "[RFC] Tensilica Xtensa (ESP32) backend" (Andrei Safronov, Espressif) | `https://discourse.llvm.org/t/rfc-tensilica-xtensa-esp32-backend/57835` | 2026-09-06 | 1 |
| LLVM Discourse, "[RFC] Request for upstream Tensilica Xtensa (ESP32) backend" | `https://discourse.llvm.org/t/rfc-request-for-upstream-tensilica-xtensa-esp32-backend/65355` | 2026-09-06 | 1 |
| LLVM Discourse, "[RFC] Add Xtensa support to lld/ELF (static linking for embedded targets)" (gerekon, posted 2026-08-19) | `https://discourse.llvm.org/t/rfc-add-xtensa-support-to-lld-elf-static-linking-for-embedded-targets` | 2026-09-06 | 1 |

### 3.2 GitHub issues and pull requests

| Repository | Number | Title | Opened / state | Leaves citing it |
|---|---|---|---|---|
| `espressif/qemu` | #154 | "ESP32-S3: qemu-system-xtensa boots with CPENABLE = 0; first FP op recurses through the Cp0Disabled handler (QEMU-293)" | Opened 2026-05-28, open as of 2026-09-06 | 2 |
| `espressif/qemu` | #155 (issue/PR) | "hw/xtensa/esp32s3: reset CPENABLE to 0xff to match silicon (QEMU-294)" | Opened 2026-05-28, open as of 2026-09-06 | 1 |
| `espressif/qemu` | #161 | "ESP32-S3 PIE: EE.VADDS/EE.VSUBS clamp signed negative results to -MAX instead of MIN (QEMU-300)" | Opened 2026-08-02, open as of 2026-09-06 | 2 |
| `espressif/qemu` | #162 | "ESP32-S3 PIE: EE.LDXQ.32/EE.STXQ.32 indexed address is four bytes too low (QEMU-301)" | Opened 2026-08-02, open as of 2026-09-06 | 1 |
| `espressif/qemu` | #163 | "ESP32-S3 PIE: SAR=32 VSL/VSR/VMUL results differ from real hardware (QEMU-302)" | Opened 2026-08-02, open as of 2026-09-06 | 2 |
| `espressif/qemu` | #164 | "QEMU-303" (title not quoted in the citing leaf) | Opened 2026-08-02, open as of 2026-09-06 | 1 |
| `espressif/qemu` | #165 | "QEMU-304" (title not quoted in the citing leaf) | Opened 2026-08-02, open as of 2026-09-06 | 1 |
| `espressif/qemu` | #166 | "ESP32-S3 PIE: dynamic cross-shifts do not mask as[3:0] and can terminate QEMU (QEMU-305)" | Opened 2026-08-02, open as of 2026-09-06 | 2 |
| `espressif/esp-nn` | #11 | "Support for ESP32-P4?" | Closed 2026-04-27 | 1 |
| `espressif/esp-nn` | #21 | ReLU6 kernel returns an unclamped value at one input position (no title quoted) | Closed 2026-08-19 | 1 |
| `zephyrproject-rtos/zephyr` | #83168 | "SMP on ESP32 seems to be broken" | Retrieved 2026-09-06; no maintainer confirmation of ESP32-S3 SMP support was visible in the thread as fetched | 1 |

## 4. Sources cited but not resolvable

Three sources are named in the corpus but the leaves that cite them could
not read them directly. This list is copied from those leaves' own
`[uncertain]` notes, not re-derived here.

| Source | Why it could not be resolved | Cited from |
|---|---|---|
| Cadence, *Xtensa ISA Summary for all Xtensa LX Processors*, RI-2021.8, 04/2022 | The Cadence product page returned HTTP 403 on an unauthenticated fetch (2026-09-06). The corpus uses this document only to name `DIV0.S`, `RECIP0.S` and `RSQRT0.S` as partial ("Begin") operations; their exact defined wording is `[uncertain]` | `01-scalar-isa/floating-point-option-on-lx7.md`, `05-measurement/host-benchmarks-versus-the-device.md` |
| Cadence/Tensilica's LX7-specific processor data book | Not openly published. The ISA Reference Manual (section 1 of this file) explicitly defers per-instruction latency tables to this document, and the Cadence product pages returned HTTP 403 on an unauthenticated fetch (2026-09-06). Every non-structural latency in the corpus's cost model is `[uncertain]` as a result; it is cited only in prose, never in a footnote | `00-foundations/lx7-core-pipeline-and-cost-model.md`, `00-foundations/README.md`, `09-adversarial/uncertainty-register-what-the-corpus-does-not-know.md` |
| William M. McKeeman, "Differential Testing for Software", *Digital Technical Journal*, vol. 10, no. 1, 1998, pp. 100-107 | The original journal issue was not directly reachable. The page range is corroborated only through a secondary summary (Wikipedia, "Differential testing"; see section 3.1), not the primary issue | `05-measurement/bit-exact-reference-tests-and-fuzzing.md`, `05-measurement/README.md`, `09-adversarial/uncertainty-register-what-the-corpus-does-not-know.md` |

## 5. Count summary

Counted by script from every `[^id]:` footnote definition in every leaf
of buckets `00-foundations` through `09-adversarial`, excluding each
bucket's own `README.md` (which carries no footnotes of its own).

| Count | Value |
|---|---|
| Leaves scanned | 47 |
| Total footnote definitions | 745 |
| Distinct external sources (documents, repositories, web pages, issues and PRs) | 60 |
| Primary documents (section 1) | 13 |
| Source trees (section 2) | 13 |
| Web pages, blogs and forums (section 3.1) | 20 |
| GitHub issues and PRs (section 3.2) | 11 |
| Sources cited but not resolvable (section 4) | 3 |
| Footnotes citing this repository's own files or commits (`07-our-work/` only, not a bibliography source) | 107, across 7 leaves |
| Footnotes that are cross-references to another leaf in this same collection, not a source | 19, across 4 leaves |
| Footnotes citing this knowledge base's own git history or its own grep count, not a source | 2, across 2 leaves |
| Footnotes citing no identifiable source at all | 1 |

The one footnote with no identifiable source is
`06-kernel-patterns/lut-gathers-palettes-and-rgb565.md`, footnote 34: a
self-run host check ("Host check, 2026-09-06 `[experience]`: a C program
that computes the packed form and the per-channel form and compares
them...") that documents its own method rather than citing anything
external. That is consistent with its `[experience]` tag; it is not a
defect.
