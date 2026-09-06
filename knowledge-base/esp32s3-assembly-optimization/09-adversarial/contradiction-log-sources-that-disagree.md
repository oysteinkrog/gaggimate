---
title: Contradiction log, where the sources disagree
id: 09-adversarial/contradiction-log-sources-that-disagree
schema_version: 1
doc_type: reference
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, lx7, adversarial, contradictions, trm, datasheet, gcc, toolchain, provenance]
confidence: high
sources: [xtensa-isa-rm-2010, esp32s3-trm-v18, esp32s3-datasheet-v22, esp-idf-5.5.1, gcc-14.2-xtensa]
---

# Contradiction log, where the sources disagree

Anyone who reads two documents about this chip closely will find places where
they do not agree. Without a record, every reader rediscovers the same handful
and spends the same hour on each. This page is that record. Each row names the
two sources, what each one says, which one to follow, and the evidence that
settled it.

Three classes, in three tables:

1. **A manual against a manual, or a manual against itself.** Mostly the
   ESP32-S3 Technical Reference Manual, which is where the density of
   disagreement is highest.
2. **A header against a tool.** The core configuration header says the chip has
   a feature and the compiler behaves as though it does not, or the other way
   round.
3. **The corpus against itself.** Places where an earlier draft of a leaf in
   this knowledge base was wrong and a later verification pass corrected it.
   This class is a lesson for future writers, so it ends with the error kinds
   ranked by how often they occurred.

A note on what "resolution" means here. None of these were settled by asking a
person at Espressif. They were settled by reading the primary text closely, by
running the shipped assembler or compiler, or by executing the instruction.
Where the resolution rests on a tool rather than on silicon, the row says so.

## 1. Manual against manual, and manual against itself

Every row was re-read in the manual text before being logged here.

| Source A | Source B | What each says | Resolution | Evidence |
|---|---|---|---|---|
| TRM Section 1.5.3 | TRM Sections 1.8.20 to 1.8.23 and 1.8.60 to 1.8.63 | A: "there is no direct way to switch the data between the two special registers and memory. You can read and write data of QACC_H and QACC_L via five 4-byte (AR) registers or two 16-byte (QR) registers." B: defines `EE.LD.QACC_H.L.128.IP`, `EE.LD.QACC_H.H.32.IP` and the matching stores, four per half. | Follow B. The instruction entries exist and the assembler takes them. Section 1.5.3 reads as text that predates those entries. | Both read in the manual[^trm-153][^trm-qacc]; the leaf that carries this is [the PIE register file leaf](../02-pie-vector/pie-register-file-sar-and-context.md) |
| TRM 1.8.98 syntax line | TRM 1.8.98 description, same page | A: `EE.VLDBC.32.IP qu, as, -256..252`. B: "incremented by 8-bit sign-extended constant in the instruction code segment left-shifted by 2", which is -512 to 508. | Follow B. The syntax line is wrong. | `[measured]` The assembler accepts 508 and -512 and rejects 512[^gas][^trm-vldbc] |
| TRM 1.8.59 syntax line | TRM 1.8.59 description, same page | A: `EE.ST.ACCX.IP as, -512..508`. B: "left-shifted by 3", which is -1024 to 1016. | Follow B. The syntax line is wrong, and the matching load entry has the correct range. | `[measured]` The assembler accepts 1016 and -1024 and rejects 1024[^gas][^trm-accx] |
| TRM 1.8.219 syntax line | TRM 1.8.219 title, encoding and description | A: `LD.QR qs, as, imm, -128..112`. B: the section is titled `ST.QR`, the instruction word differs from `LD.QR` in one field, and the description says "stores 128 bits from the source QR register qs to memory". | Follow B. The syntax line is a copy of the previous entry's, left unedited. | Both entries read side by side[^trm-stqr] |
| TRM Section 4.3.2, prose for RTC SLOW | TRM Table 4.3-1, same section | A: RTC SLOW is "addressed by the CPU through the data/instruction bus via shared address 0x5000_E000 ~ 0x5001_FFFF, as described in Table 4.3-1". B: the table's row gives 0x5000_0000 to 0x5000_1FFF, 8 KB. | Follow B. The prose range is 72 KB wide for an 8 KB memory, and it cites the table it disagrees with. | Both read in the manual[^trm-rtc]; carried by [the memory map leaf](../00-foundations/esp32s3-memory-map-and-address-spaces.md) |
| TRM Section 1.7.1 worked example | TRM Table 1.7-1, two pages earlier | A: "Suppose the SA pipeline stage of instruction A is W and the SB pipeline stage of instruction B is E, instruction B is issued to the pipeline D=max(2-1+1, 0)=2 cycles later". B: numbers the stages R 0, E 1, M 2, W 3. | The arithmetic is right and the label is wrong. With SA at stage 2 the example is a load, whose result is ready at the end of M. Reading it as W would give 3, not 2. | Both read in the manual[^trm-pipe]. Found while writing this page |
| Xtensa ISA Reference Manual, Section 8.4.1 | TRM Table 1.7-2 | A: refuses per-instruction latency on purpose. "Instead of using a per-instruction latency number, instructions are modeled as taking their operands in various pipeline stage numbers". B: gives a def stage and a use stage for every operand of every extended instruction on this part. | Not a conflict, a division of labour. The architecture manual owns the rule, the chip manual owns the numbers the rule needs. The trap is that Table 1.7-2 covers the extended instructions only, so there is no published stage table for the base integer instructions on this core. | [^isa][^trm-pipe] |
| Datasheet v2.2 Section 4.1.2.3 feature list | TRM Section 4.3.3.2 and the IDF Kconfig for this target | A: data cache is "four-way set associative", block size "16 bytes or 32 bytes". B: TRM says the DCache block "can be configured to 16 B, 32 B or 64 B"; the IDF Kconfig offers 4 or 8 DCache ways and defaults to 8, and offers a 64 byte line. | Follow B. The datasheet's list is narrower than both the chip manual and the code that configures the cache at startup. | All three read[^ds][^trm-cache][^kconfig]. Found while writing this page |
| Datasheet SPI feature list | Datasheet Table 5-12, same document | A: "Configurable clock frequency with a maximum of 120 MHz for 8-line SPI SDR/DDR modes". B: PSRAM maximum clock frequency 80 MHz. | Both are true of different things. The controller can clock a bus at 120 MHz; the in-package PSRAM part is rated at 80. IDF exposes 120 MHz for octal PSRAM and marks it experimental, warning that accesses "will crash randomly" after a temperature swing of roughly 20 degrees Celsius. Treat 80 MHz as the number to budget with. | [^ds][^kconfig]; carried by [the caches and MSPI leaf](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md) |
| TRM register overview | TRM entries for `EE.VSR.32` and `EE.VSL.32` | A: those instructions use "the lower 5 bits of SAR". B: the entries call the register 6 bits and read `SAR[5:0]`. | Follow B, the instruction entries. Keep the shift amount inside 0 to 31, where the two readings agree, because what a 32-bit lane does when shifted by 32 or more is stated nowhere. | [The PIE arithmetic leaf](../02-pie-vector/pie-arithmetic-multiply-saturate-and-shuffle.md) |
| `EE.CMUL.S16` prose | `EE.CMUL.S16` operation section, same entry | A: puts the real part in the upper 16 bits of each 32-bit pair. B: computes with the real part in the low half. | Follow the operation section. It also matches the order a `re, im` array lands in on a little-endian machine, and it matches Espressif's emulator. The two agreeing sources are not independent, so a device test is still worth having. | Same leaf |
| `EE.MOV.S16.QACC` prose | `EE.MOV.S16.QACC` pseudo-code, same entry | A: correct. B: uses the wrong bit positions as the sign bits of the two highest lanes. | Follow the prose. | Same leaf |

Two more manual defects have no second source to resolve them, so they stay
open rather than resolved. `EE.SRCMB.S16.QACC`, `EE.SRCMB.S8.QACC` and
`EE.SRS.ACCX` each take a third operand that the manual writes as a literal
zero and never describes, and whose instruction-word diagram shows no field for
it. The toolchain names it `sel2` and encodes 0 and 1 differently, and the
emulator ignores it. What the bit does on silicon is `[uncertain]`; only a
device test settles it.[^pie-arith]

### What the pattern says about reading this manual

Four of the rows above are a syntax line, a prose sentence or a pseudo-code
line disagreeing with its own entry's description or operation section, on the
same page. The rule that falls out is worth stating plainly: **in the TRM, the
description and the operation section outrank the assembler syntax line and the
surrounding prose.** When the two still disagree, the shipped assembler is the
tiebreak for an immediate range, and the emulator is the tiebreak for semantics
until a device test exists.

## 2. Header against tool

These are places where the ESP-IDF core configuration header for this chip and
the shipped toolchain say different things. All of them were established by
[the headers-versus-tools leaf](../04-toolchain-and-codegen/what-the-headers-say-versus-what-the-tools-do.md)
and [the MAC16 and Boolean options leaf](../01-scalar-isa/mac16-boolean-and-other-configured-options.md).
None of the experiments are repeated here.

| Source A | Source B | What each says | Resolution | Evidence |
|---|---|---|---|---|
| `core-isa.h`, `XCHAL_HAVE_CLAMPS` 1 | GCC's own predefined `__XCHAL_HAVE_CLAMPS` 0 | A: the chip has `CLAMPS`. B: it does not. Every neighbouring option in the same group prints 1 in the compiler. | The chip has it, GCC is configured as though it does not, so GCC never emits it. Reach it from inline assembly only. The leaf's summary line: "the ESP32-S3 has `CLAMPS`, and GCC 14.2 is configured as though it does not, so GCC lowers a saturating clamp to `MIN` plus `MAX` instead." | The dynconfig shared object holds both the opcode encoder and the string `__XCHAL_HAVE_CLAMPS=0`; the emulator executed the instruction over fifteen inputs at two immediates with no trap `[measured]`[^hdrtool] |
| Assembler acceptance | Anything about the core | Writing `clamps` and having it assemble looks like proof the core has it. | It is not. The generic driver, with no chip configuration at all, also assembles `clamps` from its built-in table, while rejecting the vector instructions the chip driver accepts. Acceptance of a base-ISA option proves nothing; acceptance of a vector mnemonic does, because that knowledge is configuration-driven. | Same leaf |
| `core-isa.h`, `XCHAL_UNALIGNED_LOAD_HW` 1 | The compiler under this chip's configuration | A: the hardware does unaligned loads and stores. B: does not define the macro at all, so an `#if` on it reads as absent, and the documented `-mstrict-align` default follows from that. | Both are true and the gap costs instructions. The leaf measured it: a four-byte copy from an arbitrary pointer compiles to ten instructions through the stack, and to one load with `-mno-strict-align`. IDF passes neither form of the flag. | `[measured]` in the same leaf |
| `core-isa.h`, `XCHAL_HAVE_FP_DIV` and `_SQRT` 1 | The assembler | A: reads as though divide and square root exist. B: rejects `div.s` and `sqrt.s` as unknown opcodes, and accepts the seed instructions instead. | No disagreement, a naming trap. The bits mean the refinement seed instructions exist. There is no divide and no square root instruction, and GCC calls a library routine even under fast-math flags. | [The floating-point option leaf](../01-scalar-isa/floating-point-option-on-lx7.md) and the headers-versus-tools leaf |
| Habit from other backends | This backend | A: `-mcpu=esp32s3` ought to select the core. B: "there is no `-mcpu=` option on this backend". | The selector is `-mdynconfig=`, which the per-chip driver passes for you. Both drivers reject `-mcpu=` outright. | `[measured]` in the headers-versus-tools leaf |
| Reasonable guess about the `q` constraint | The backend's own header and the compiler | A: `q` looks like it should name a vector register. B: "on Xtensa `q` is the stack-pointer constraint, and `q0` is not a register name at all". | Follow B. A `"=q"` output fails, and `"q0"` and `"sar"` as clobbers fail. A block that touches the vector registers, the shift-amount register or the byte shift-amount register owns them silently, and a comment is the only record. | `[measured]`, three verbatim compiler errors, in the same leaf |
| `core-isa.h`, `XCHAL_HAVE_MAC16` 1, plus folklore | The compiler at `-O2` | A: the option is present but modern compilers never reach it from ordinary C. B: a 16 by 16 multiply accumulating into a 32-bit sum compiles to `mula.aa.ll` inside a hardware loop, with the accumulator moved off the register file for the loop's whole run. | Follow B, at a finer grain than either blanket claim. The compiler reaches MAC16 for that one reduction shape at `-O2` and `-Os`, not at `-O1`, and not with 32-bit operands. Every other MAC16 form still needs inline assembly. | `[measured]` in the MAC16 leaf, by compiling and disassembling at four optimization levels |

## 3. Corpus against corpus, resolved

A verification pass in September 2026 read every leaf in the first seven
buckets against the manuals, the vendor tree and the installed toolchain, and
corrected them. The commit message records what changed.[^verify] Each row
below gives the claim as it stood, the claim as it stands, the leaf that now
carries it, and what kind of mistake produced the first version.

| Was | Is | Leaf | Error kind |
|---|---|---|---|
| `memcpy` does not come from the boot ROM; the ROM exports only a Tensilica bulk-copy helper, so `memcpy` resolves to the toolchain's own newlib object. | `memcpy` is an absolute symbol at a ROM address, assigned by a linker fragment that the vendor tree links whenever the target declares that it has newlib in ROM. The ROM body is newlib's Xtensa algorithm and never occupies the instruction cache. | [loop shapes and per-call setup](../06-kernel-patterns/loop-shapes-scheduling-and-per-call-setup.md) | Unread source. The linker fragment and the capability header settle it in three lines, and neither was opened. |
| The shipped single-precision divide routine is 29 instructions. | It is 30. | [the floating-point option leaf](../01-scalar-isa/floating-point-option-on-lx7.md) | Miscount. The number was reported without the disassembly being counted again. |
| After a `call8`, return values come back in the caller's `a2` to `a5`. | They come back in the caller's `a10` to `a13`. Return values follow the same rotation as arguments. This is also what limits the deepest call form to returning two words. | [register windows and the windowed ABI](../00-foundations/register-windows-and-windowed-abi.md) | Generalising. The rule for where a callee puts a return value was carried over to the caller's view without applying the window rotation. |
| A loop body over 256 bytes is one of the reasons the compiler refuses to emit a hardware loop. | Neither pass in the compiler tests the body's byte size. The compiler emits the loop instruction regardless and the assembler relaxes it into a longer entry sequence, which is still a hardware loop. | [register pressure and reading the assembly](../04-toolchain-and-codegen/register-pressure-spills-and-reading-the-assembly.md) | Unread source. The hardware limit is real and the guard was assumed rather than looked for in the backend. |
| The compiler does not generate MAC16 from ordinary C; reaching it needs inline assembly or a builtin. | It reaches MAC16 for one shape, at some optimization levels, as described in class 2 above. | [core ISA and configured options](../01-scalar-isa/core-isa-and-configured-options.md) | Experiment not run. A plausible general claim stood in for a compile and a disassembly. |
| A taken branch kills two fetch slots, and the dependent-instruction arithmetic for a load was written with stage numbers 4 and 3. | The chip manual puts a number on the branch, two cycles, and the stage numbers on this core are 2 for the load result and 1 for the use. | [the pipeline and cost model leaf](../00-foundations/lx7-core-pipeline-and-cost-model.md) | Generalising from another core. The architecture manual's stage table has names and no numbers; the 4 and 3 belong to a deeper pipeline than this one. |
| Vector registers do not need to appear in a clobber list, because the allocator can never put a C value in one. | The second half is right and the first half understates it. They cannot be named at all: `q0` as a clobber is rejected as an unknown register name. | [coprocessors and lazy context](../00-foundations/coprocessors-cpenable-and-lazy-context.md) | Partial reading. The register-class fact was checked and the register-name fact was not. |
| Two named vendor kernel files were cited as evidence for a claim about that library's assembly kernels. | Neither file exists in that repository. The naming pattern the citation followed does not hold for those two kernels. | [esp-dsp as a reference kernel library](../06-kernel-patterns/esp-dsp-as-a-reference-kernel-library.md) | Fabricated citation. A filename was inferred from a pattern instead of read from a directory listing. |

### Error kinds, ranked

Counting the eight rows above:

1. **Unread source, three cases.** A file that settles the question exists, is
   small, and was not opened. This is the most common kind and the cheapest to
   avoid. If a claim is about what a build does, the answer is in the build
   system or the backend, not in a manual.
2. **Generalising, two cases.** A true rule from a neighbouring context was
   applied where it does not hold: another core's pipeline depth, or the
   callee's view of a register instead of the caller's.
3. **Experiment not run or run once, two cases.** A number reported without
   recounting, and a codegen claim asserted without compiling. Both would have
   taken a minute.
4. **Fabricated citation, one case.** A filename produced from a naming pattern
   rather than from a listing. One is one too many: this is the failure the
   citation contract exists to prevent.

No case in this batch was a stale-version error, where a claim was true of an
older toolchain or manual and quietly went out of date. That is worth noting
because it is the kind most likely to appear later, once this corpus is old
enough for the versions under it to move.

The practical reading for anyone writing here: the first two kinds together are
five of eight, and both are failures of reading rather than failures of
reasoning. Open the file. Check that the rule you are applying was stated about
the thing you are applying it to.

## Open questions

- Whether Espressif treats the compiler's `CLAMPS` configuration value as a
  defect or a deliberate choice is `[uncertain]`. A search on 2026-09-06 found
  no issue report, changelog entry or commit message about it. An issue or
  commit in the toolchain repository, or a later configuration build whose
  value differs, would settle it.[^hdrtool]
- Whether the silicon implements `CLAMPS` is `[uncertain]` at the strength
  reachable from documents and an emulator. The header says yes and the
  emulator executes it, but both are Espressif-authored and neither is the
  chip. Running the same test on hardware and checking for an illegal
  instruction exception would settle it.
- The third operand of `EE.SRCMB.S16.QACC`, `EE.SRCMB.S8.QACC` and
  `EE.SRS.ACCX` is `[uncertain]`. A device test comparing the two encodings on
  the same inputs would settle it.
- Whether the datasheet's narrower data-cache feature list is an error or a
  qualified statement about a validated configuration is `[uncertain]`. Running
  a build with an eight-way, 64 byte-line data cache and confirming correct
  operation would settle the practical half; only Espressif can settle the
  documentary half.
- Whether the TRM's mislabelled pipeline example is a typo in the label or in
  the arithmetic is `[uncertain]` in principle, though the arithmetic matches
  the architecture manual's own load-use case and the label does not, so the
  label is the likely error.
- No source consulted gives a cycle count for the base integer multiply, the
  divide instructions, a special-register read, or the interrupt-level raise on
  this core. The architecture manual declines on purpose and the chip manual's
  stage table covers the extended instructions only. `[uncertain]`, and a
  device or emulator measurement is the only route.

## Sources

[^trm-153]: Espressif Systems, *ESP32-S3 Technical Reference Manual*, version
    1.8, Section 1.5.3 "Data Format and Alignment", page 48. Quoted sentence
    read from the manual text on 2026-09-06.
    <https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf>

[^trm-qacc]: Same manual, Sections 1.8.20 to 1.8.23 (`EE.LD.QACC_H.H.32.IP`
    page 96 onward, `EE.LD.QACC_H.L.128.IP`) and 1.8.60 to 1.8.63 (the
    `EE.ST.QACC_*` forms, `EE.ST.QACC_H.L.128.IP` page 136 onward). Read
    2026-09-06.

[^trm-vldbc]: Same manual, Section 1.8.98 `EE.VLDBC.32.IP`, page 174. Syntax
    line `EE.VLDBC.32.IP qu, as, -256..252`; description "incremented by 8-bit
    sign-extended constant in the instruction code segment left-shifted by 2".

[^trm-accx]: Same manual, Section 1.8.59 `EE.ST.ACCX.IP`, page 135. The entry's
    `Assembler Syntax` line reads `EE.ST.ACCX.IP as, -512..508` and its
    description says "left-shifted by 3". The matching load, Section 1.8.19
    `EE.LD.ACCX.IP` page 95, prints the correct range,
    `EE.LD.ACCX.IP as, -1024..1016`.

[^trm-stqr]: Same manual, Section 1.8.218 `LD.QR` page 301 and Section 1.8.219
    `ST.QR` page 302. The `ST.QR` entry's assembler syntax line reads
    `LD.QR qs, as, imm, -128..112` while its title, encoding and description are
    all the store.

[^trm-rtc]: Same manual, Chapter 4 "System and Memory": the RTC SLOW Memory
    paragraph in Section 4.3.2, and Table 4.3-1 "Internal Memory Address
    Mapping", whose RTC SLOW Memory row gives 0x5000_0000 to 0x5000_1FFF, 8 KB.

[^trm-pipe]: Same manual, Section 1.7 "Instruction Performance" and Table 1.7-1
    "Five-Stage Pipeline of Xtensa Processor" page 65, which numbers R 0, E 1,
    M 2 and W 3; Section 1.7.1 "Data Hazard" page 65 for the interlock formula
    and the worked example quoted above; Table 1.7-2 "Extended Instruction
    Pipeline Stages", pages 66 to 74, for the per-operand def and use stages of
    the extended instructions.

[^trm-cache]: Same manual, Section 4.3.3.2 "Cache": "The size of DCache can be
    configured to 32 KB or 64 KB, while its block size can be configured to
    16 B, 32 B or 64 B. When a DCache is configured to 64 KB, its block cannot
    be 16 B."

[^ds]: Espressif Systems, *ESP32-S3 Series Datasheet*, version 2.2. Section
    4.1.2.3 "Cache", page 39, feature list: "Data cache: 32 KB (one bank) or
    64 KB (two banks)", "Data cache: four-way set associative", "Block size of
    16 bytes or 32 bytes for both instruction cache and data cache". The SPI
    feature list: "Configurable clock frequency with a maximum of 120 MHz for
    8-line SPI SDR/DDR modes". Table 5-12 "PSRAM Specifications": maximum clock
    frequency 80 MHz. Read 2026-09-06.
    <https://documentation.espressif.com/esp32-s3_datasheet_en.pdf>

[^isa]: Tensilica (Cadence Design Systems), April 2010, *Xtensa Instruction Set
    Architecture (ISA) Reference Manual*, release RC-2010.1. Section 8.4.1
    "Processor Performance Terminology and Modeling", pages 605 to 607, for the
    quoted refusal of per-instruction latency and the `D = max(SA - SB + 1, 0)`
    rule. Section 8.4.2 "Xtensa Processor Family", pages 608 to 609, and Table
    8-247 "Xtensa Pipeline", which names the five stages and assigns no numbers
    to them. Public mirror: <https://0x04.net/~mwk/doc/xtensa.pdf>

[^kconfig]: ESP-IDF 5.5.1, as installed in the local `framework-espidf`
    package. `components/esp_system/port/soc/esp32s3/Kconfig.cache`: the data
    cache ways choice offers 4 and 8 and defaults to 8, and the data cache line
    size choice offers 16, 32 and 64 bytes and defaults to 32. The PSRAM
    Kconfig offers 40, 80 and 120 MHz, marks octal 120 MHz experimental and
    carries the temperature warning quoted above. Read 2026-09-06.

[^gas]: `[measured]` 2026-09-06. `xtensa-esp32s3-elf-as` from the installed
    `xtensa-esp-elf` toolchain, GCC 14.2.0 (crosstool-NG
    `esp-14.2.0_20241119`), GNU binutils 2.43.1. One mnemonic per file in a
    scratch directory outside any project tree. `ee.st.accx.ip a2, N` assembles
    for N of -1024 and 1016 and is rejected for 1024, so the range is the
    description's and not the syntax line's. `ee.vldbc.32.ip q0, a2, N`
    assembles for N of -512 and 508 and is rejected for 512, the same result.

[^hdrtool]: [Where the Xtensa headers and the tools
    disagree](../04-toolchain-and-codegen/what-the-headers-say-versus-what-the-tools-do.md),
    this corpus, for every row of the class 2 table and its `[measured]`
    commands. Quoted resolutions are that leaf's own summary sentences.

[^pie-arith]: [PIE arithmetic, multiply, saturate and
    shuffle](../02-pie-vector/pie-arithmetic-multiply-saturate-and-shuffle.md),
    this corpus, section "Documentation defects found while writing this", for
    the four manual defects in the PIE compute instructions and for the
    undescribed third operand.

[^verify]: Repository history, commit `4621722f`, "verify pass over buckets 00
    to 06". Its message lists the corrections summarised in the class 3 table,
    and the diff carries each one. Read 2026-09-06.
