---
title: Where the Xtensa headers and the tools disagree
id: 04-toolchain-and-codegen/what-the-headers-say-versus-what-the-tools-do
schema_version: 1
doc_type: reference
status: draft
last_reviewed: 2026-09-06
created: 2026-09-06
tags: [esp32s3, xtensa, gcc, toolchain, dynconfig, clamps, inline-asm, codegen]
confidence: high
---

# Where the Xtensa headers and the tools disagree

Five questions about this toolchain have a header saying one thing and a tool
doing another. Each is reproduced below with the command that shows it, on the
toolchain ESP-IDF 5.5.1 ships for the ESP32-S3: `xtensa-esp-elf` GCC 14.2.0
(crosstool-NG `esp-14.2.0_20241119`) with GNU binutils 2.43.1, and the vendored
ESP-IDF 5.5.1 headers beside it.[^tc] Each section states the resolution first,
then the evidence. Neighbours: [GCC extended inline assembly on the Xtensa
LX7](./gcc-extended-inline-asm-on-xtensa.md), [GCC 14 Xtensa flags and what they
cost](./gcc14-xtensa-flags-and-what-they-cost.md), and [the floating-point
option on the LX7](../01-scalar-isa/floating-point-option-on-lx7.md).

## Summary

| Question | Resolution |
|---|---|
| Does the chip have `CLAMPS`? | Yes, but GCC's own configuration says no, so GCC never emits it. Reach it with inline asm. |
| Are `div.s` and `sqrt.s` real instructions? | No. The `XCHAL_HAVE_FP_DIV` family means the seed instructions exist, and GCC calls a library routine regardless. |
| Can inline asm name a PIE `q` register? | No. On Xtensa `q` is the stack-pointer constraint, and `"q0"` is not a register name at all. |
| Does `-mcpu=esp32s3` select the core? | No such option exists. The selector is `-mdynconfig=`, which the chip driver passes for you. |
| Does GCC ever emit a PIE instruction? | No. The compiler cannot print one, and it has no Xtensa vector builtins. |

## 1. CLAMPS: the compiler's config says no, everything else says yes

**Resolution: the ESP32-S3 has `CLAMPS`, and GCC 14.2 is configured as though
it does not, so GCC lowers a saturating clamp to `MIN` plus `MAX` instead. The
instruction is reachable only from inline assembly.**

ESP-IDF's per-core header for this chip sets `XCHAL_HAVE_CLAMPS` to 1 with the
comment "CLAMPS instruction".[^coreisa] The compiler's own predefined macro says
the opposite. `xtensa-esp32s3-elf-gcc -dM -E -x c /dev/null` prints `#define
__XCHAL_HAVE_CLAMPS 0`, while every neighbouring option in the same
Miscellaneous Operations group prints 1 (`__XCHAL_HAVE_MINMAX`,
`__XCHAL_HAVE_SEXT`, `__XCHAL_HAVE_NSA`).[^tc] The same zero appears in the
dynconfig libraries for the other two Xtensa ESP chips, whose ESP-IDF headers
also say 1.[^tc][^coreisa]

The disagreement lives inside one file. `strings` on `lib/xtensa_esp32s3.so`,
the dynconfig the ESP32-S3 driver loads, returns both the instruction's encoder
(`Opcode_clamps_encode_fns`, `Opcode_clamps_Slot_inst_encode`) and the macro
string `__XCHAL_HAVE_CLAMPS=0`.[^tc] The opcode table and the configuration
table in the same shared object do not agree.

The consequence is a real instruction-selection loss, because GCC has the
pattern and cannot use it. The Xtensa machine description defines
`*xtensa_clamps`, output template `clamps\t%0, %1, %d`, condition
`TARGET_MINMAX && TARGET_CLAMPS && xtensa_match_CLAMPS_imms_p (operands[2],
operands[3])`,[^xtensamd] and the toolchain's shipped header defines
`TARGET_CLAMPS` as `XCHAL_HAVE_CLAMPS`.[^xtensah] So the condition is false and
the pattern is dead. All three strings sit in the shipped `cc1` binary at once:
`*xtensa_clamps`, `clamps\t%%0, %%1, %d`, and `__XCHAL_HAVE_CLAMPS=0`.[^tc]

What you get instead, at `-O2`, for `x > 127 ? 127 : (x < -128 ? -128 : x)`, is
`movi a8, -128; max a2, a2, a8; movi a8, 127; min a2, a2, a8`. Four instructions
and 12 bytes where `clamps a2, a2, 7` is one instruction and 3 bytes. With a
wider clamp the two `movi` become two `l32r` plus two literal words.
`[measured]`[^tc]

The assembler accepts the instruction whichever driver you use, so acceptance
alone proves nothing about the core: `xtensa-esp-elf-as`, the generic driver
with no dynconfig, also assembles `clamps` from its built-in table. That same
generic driver rejects `ee.vld.128.ip`, which the ESP32-S3 driver accepts, and
the 217 `ee.*` mnemonics live in `xtensa_esp32s3.so` rather than in the `as`
binary.[^tc] The assembler's ISA knowledge is configuration-driven for PIE and
not for `clamps`. The immediate range it enforces is the manual's, `CLAMPS ar,
as, 7..22` encoded as 0 to 15 in the `t` field: 6 and 23 are both rejected with
"operand 3 has invalid value".[^tc][^isa]

Execution settles it. A bare-metal ESP32-S3 image built with this toolchain and
booted under Espressif's `qemu-system-xtensa` fork ran `clamps` with immediates
7 and 14 over fifteen inputs, including both saturation directions and
`INT32_MIN`/`INT32_MAX`, and matched a scalar reference on every one. The
instruction did not trap. `[measured]`[^qemu] That is the emulator's core model,
so it is Espressif's own answer to what the S3 core contains, not a hardware
measurement.

One correction to a neighbouring leaf. [The configured-options
leaf](../01-scalar-isa/core-isa-and-configured-options.md) says of `CLAMPS` that
"GCC does not generate it from ordinary saturating-add C idioms, so reaching it
needs inline asm or a builtin." Right about the first, and the builtin is not an
option: GCC 14.2 for Xtensa defines no `__builtin_xtensa_*` functions at all,
and the `cc1` binary contains no such string.[^tc]

## 2. Float divide and square root: the option bits are about seeds

**Resolution: `XCHAL_HAVE_FP_DIV`, `_RECIP`, `_SQRT` and `_RSQRT` being 1 means
the refinement seed instructions exist. There is no `div.s` and no `sqrt.s`,
and GCC calls `__divsf3` and `sqrtf` even under `-ffast-math`.**

This confirms [the floating-point option
leaf](../01-scalar-isa/floating-point-option-on-lx7.md) rather than
contradicting it; it is repeated here because the macro names are the trap.
Unlike `CLAMPS`, the FP option has no header-versus-tool disagreement: the
compiler's `__XCHAL_HAVE_FP`, `_FP_DIV`, `_FP_RECIP`, `_FP_SQRT` and `_FP_RSQRT`
are all 1 and `__XCHAL_HAVE_DFP` is 0, matching ESP-IDF's
header.[^tc][^coreisa]

The assembler rejects `div.s` and `sqrt.s` as unknown opcodes and accepts
`div0.s`, `recip0.s`, `sqrt0.s`, `rsqrt0.s`, `divn.s`, `nexp01.s`, `maddn.s`,
`const.s`, `addexp.s` and `mkdadj.s`.[^tc] The ISA reference manual's tables for
this option list add, subtract, multiply, multiply-add, multiply-subtract,
negate, absolute value, moves, compares and conversions, and no divide or square
root.[^isa] Those seeds produce a low-precision estimate that a short
Newton-Raphson sequence of ordinary multiply-adds then sharpens.

GCC does not use them. At `-O2 -mlongcalls`, `a / b` on `float` relocates
against `__divsf3` and `__builtin_sqrtf(a)` against `sqrtf`, and adding
`-ffast-math` or `-O3 -ffast-math -funsafe-math-optimizations` changes nothing:
the same two library calls, no seed instruction anywhere in the object.[^tc]
There is no Xtensa equivalent of the reciprocal-estimate flags some other
backends offer. In the same object `a * b + c` compiles to a single `madd.s` at
plain `-O2`, so the FP option is used, just not for division.

## 3. The `"q"` constraint is the stack pointer

**Resolution: on Xtensa `"q"` binds the stack pointer, not a PIE vector
register, and it is unusable in ordinary inline asm because the stack pointer is
a fixed register. `"q0"` is not a register name, so it cannot be clobbered
either.**

Both failures, verbatim from this compiler:[^tc]

```
error: inconsistent operand constraints in an 'asm'      /* "=q"(y) on an int */
error: unknown register name 'q0' in 'asm'               /* : : : "q0"       */
error: unknown register name 'sar' in 'asm'              /* : : : "sar"      */
```

`"=q"` fails for an ordinary integer output, and it fails just as hard when the
operand really is a frame address, because the register class has exactly one
member and that member is fixed. The toolchain's shipped `xtensa.h` names the
class `SP_REG` with the comment "sp register (aka a1)", marks `a0`, `sp`, `fp`
and `argp` fixed, and lists in `REGISTER_NAMES` only `a0`, `sp`, `a2` to `a15`,
`fp`, `argp`, `b0`, `f0` to `f15` and `acc`, with no `q` register of any
kind.[^xtensah] `"acc"` is accepted as a clobber; `"q0"` and `"sar"` are
not.[^tc] That local header is the authority worth citing, because the installed
compiler was built from it. The practical rule is unchanged from [the inline-asm
leaf](./gcc-extended-inline-asm-on-xtensa.md): a block that touches `q`
registers, `SAR` or `SAR_BYTE` owns them silently, and a comment at the top of
the block is the only way to record it.

## 4. `-mcpu=esp32s3` does not exist

**Resolution: there is no `-mcpu=` option on this backend, so there is no macro
diff to compute. The core is selected by `-mdynconfig=`, and the per-chip driver
passes it for you.**

Both drivers reject it outright, with `error: unrecognized command-line option
'-mcpu=esp32s3'`, which confirms [the flags
leaf](./gcc14-xtensa-flags-and-what-they-cost.md).[^tc] What replaces it is a
multilib whose name happens to be `esp32s3`: `-print-multi-lib` lists
`esp32s3;@mdynconfig=xtensa_esp32s3.so`, and compiling anything with
`xtensa-esp32s3-elf-gcc` shows `COLLECT_GCC_OPTIONS='-mdynconfig=xtensa_esp32s3.so'`
even when the command line said nothing of the sort.[^tc] The dynconfig can also
be named by the `XTENSA_GNU_CONFIG` environment variable. A bare filename
resolves only through the per-chip driver, which supplies the search path; the
generic driver needs an absolute path.[^tc]

The diff that does exist is generic driver versus ESP32-S3 dynconfig, and it is
what a portable `#if` sees. Thirty macro lines differ:[^tc]

| Macro group | Generic default | ESP32-S3 dynconfig |
|---|---|---|
| Endianness, `__XCHAL_HAVE_FP` and its four seed bits, `_BOOLEANS`, `_MAC16`, `_MUL32_HIGH`, `__FP_FAST_FMAF` | big-endian, options off | little-endian, options on |
| `__XCHAL_HAVE_MMU` | 1 | 0 |
| `__XCHAL_NUM_AREGS` | 32 | 64 |
| `__XCHAL_UNALIGNED_LOAD_HW` and its three siblings | defined, 0 | not defined at all |

`__XCHAL_HAVE_CLAMPS` is not in that diff. It is 0 in both, so nothing anywhere
in the compiler sets it to 1.

### The unaligned macros are the one with teeth

The last row costs real instructions. ESP-IDF's header for this chip sets
`XCHAL_UNALIGNED_LOAD_HW` and `XCHAL_UNALIGNED_STORE_HW` to 1 and both unaligned
exception flags to 0.[^coreisa] The compiler does not define those macros at all
under the ESP32-S3 dynconfig, so an `#if` or `#ifdef` on them reads as absent.
GCC's documented default is `-mno-strict-align` "for cores that support both
unaligned loads and stores in hardware and `-mstrict-align` for all other
cores",[^xtopts] and this core lands on the wrong side of that test. A four-byte
`memcpy` from an arbitrary pointer compiles at `-O2` to four `l8ui`, four `s8i`
and one `l32i.n`, ten instructions through the stack; with `-mno-strict-align`
it is a single `l32i.n`. `[measured]`[^tc] ESP-IDF passes neither form of the
flag, so the byte-by-byte version is what every component gets. This is the
codegen half of the alignment note in [LUT gathers,
palettes and RGB565](../06-kernel-patterns/lut-gathers-palettes-and-rgb565.md),
which reads the same header and concludes an unaligned `S32I` should not be
truncated on this part. Both can be true: the hardware permits it, and the
compiler will not emit it unless told.

## 5. GCC never emits a PIE instruction

**Resolution: no. GCC 14.2's Xtensa backend has no vector instruction patterns,
no vector builtins, and no way to print a PIE mnemonic. Every `EE.*` instruction
in a binary came from hand-written assembly or from a library that contains it.**

Three independent checks, all negative.

**The compiler binary contains no PIE mnemonic.** `strings` on the shipped `cc1`
returns zero lines beginning `ee.`, while `xtensa_esp32s3.so` returns 217 and
the assembler binary returns zero.[^tc] The assembler learns PIE from the
dynconfig; the compiler never learns it at all.

**There are no intrinsics.** `cc1` contains no `__builtin_xtensa` string, a call
to a plausibly named one is diagnosed as an implicit function declaration,[^tc]
and the machine description has no pattern whose mnemonic starts `ee.` or
`EE_`.[^xtensamd]

**The auto-vectorizer does not reach it.** At `-O3` a byte-wise add loop reports
"loop vectorized using 4 byte vectors", meaning four bytes packed into one
32-bit address register, and the object contains no `ee.*` instruction.[^tc] A
`__attribute__((vector_size(16)))` byte addition, which names a 16-byte vector
explicitly, is lowered to four 32-bit lanes in `a` registers using the
carry-free byte-add idiom of masks, adds and exclusive-ors.[^tc] Adding
`-ftree-vectorize` at `-O2` adds no PIE either.

## Open questions

- Whether Espressif treats `__XCHAL_HAVE_CLAMPS=0` as a defect or a deliberate
  choice is `[uncertain]`; a web search on 2026-09-06 found no issue report,
  changelog entry or commit message about it. What would settle it: an issue or
  commit in the toolchain's own repository, or a later dynconfig build whose
  value differs.
- Whether the silicon implements `CLAMPS` is `[uncertain]` at the strength this
  page can reach. The header says yes and the emulator executes it correctly,
  which is two Espressif-authored answers agreeing, but neither is the chip.
  What would settle it: running the fifteen-case test above on real hardware and
  checking that no illegal-instruction exception is raised. See [what QEMU proves
  and what it cannot](../05-measurement/qemu-esp32s3-what-it-proves-and-what-it-cannot.md).
- Whether `-mno-strict-align` is safe for a whole ESP-IDF build is
  `[uncertain]`. The header permits it, but unaligned access behaviour in cached
  external memory is not confirmed by the sources read here. What would settle
  it: an unaligned load and store sweep across internal SRAM, cached flash and
  cached PSRAM on real hardware. Why the dynconfig leaves those four macros
  undefined is `[uncertain]` too; its structure was not read field by field
  here.

## Sources

[^tc]: `[measured]` 2026-09-06. Toolchain at
    `~/.platformio/packages/toolchain-xtensa-esp-elf/`, `xtensa-esp32s3-elf-gcc
    (crosstool-NG esp-14.2.0_20241119) 14.2.0`, GNU assembler 2.43.1. Run
    against small standalone C and `.S` files in a scratch directory outside any
    project tree: `-dM -E -x c /dev/null` on both drivers, sorted and diffed;
    `-O2 -mlongcalls -c` with `objdump -d -r`, and the same with
    `-mno-strict-align`; `-O3 -fopt-info-vec-all`; `-mcpu=esp32s3`;
    `-print-multi-lib`; `-v` on a compile to read `COLLECT_GCC_OPTIONS`;
    `--help=target`; both assemblers on one mnemonic per file; and `strings` on
    `cc1`, `xtensa-esp32s3-elf-as` and `lib/xtensa_esp32s3.so`.

[^coreisa]: ESP-IDF 5.5.1, `components/xtensa/esp32s3/include/xtensa/config/core-isa.h`,
    in the local `framework-espidf` PlatformIO package: `XCHAL_HAVE_CLAMPS` line
    62, the floating-point block lines 130 to 135, the unaligned block lines 202
    to 205. The `esp32` and `esp32s2` variants also set `XCHAL_HAVE_CLAMPS` to 1
    on line 62.

[^xtensah]: Local toolchain header
    `lib/gcc/xtensa-esp-elf/14.2.0/plugin/include/config/xtensa/xtensa.h`,
    shipped with the installed compiler: line 39 `#define TARGET_CLAMPS
    XCHAL_HAVE_CLAMPS`, `FIXED_REGISTERS` line 215, `enum reg_class` line 343
    (including `SP_REG, /* sp register (aka a1) */`), `REGISTER_NAMES` line 654.
    The companion `xtensa-dynconfig.h` declares `int xchal_have_clamps` as a
    dynconfig struct field and defines `XCHAL_HAVE_CLAMPS` as a read of it.

[^xtensamd]: GCC machine description, `gcc/config/xtensa/xtensa.md`, Espressif
    fork branch `esp-14.2.0_20241119`, retrieved 2026-09-06 from
    <https://raw.githubusercontent.com/espressif/gcc/esp-14.2.0_20241119/gcc/config/xtensa/xtensa.md>:
    the `*xtensa_clamps` `define_insn_and_split` and `define_insn`, output
    template `clamps\t%0, %1, %d`, condition `TARGET_MINMAX && TARGET_CLAMPS &&
    xtensa_match_CLAMPS_imms_p (operands[2], operands[3])`. No pattern in the
    file uses an `ee.` or `EE_` mnemonic.

[^xtopts]: Free Software Foundation, *Using the GNU Compiler Collection (GCC)*
    14.2.0, "Xtensa Options",
    <https://gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Xtensa-Options.html>, fetched
    2026-09-06, for the quoted `-mstrict-align` default. The page documents
    eleven options and neither `-mcpu=` nor `-mdynconfig=`; the installed
    driver's `--help=target` lists five more, `-mdynconfig=` among them.

[^isa]: Tensilica, *Xtensa Instruction Set Architecture (ISA) Reference
    Manual*, issue date 4/2010 (for all Xtensa processor cores). Section 4.3.8
    "Miscellaneous Operations Option", page 62, Tables 4-39 and 4-40. The
    `CLAMPS` instruction page, 311 to 312: RRR format, "Required Configuration
    Option: Miscellaneous Operations Option", syntax `CLAMPS ar, as, 7..22`, and
    the note that those immediates are encoded in the `t` field as 0 to 15.
    Section 4.3.11 "Floating-Point Coprocessor Option", pages 67 to 74, Tables
    4-46, 4-49 and 4-50, which list no divide and no square root. Public mirror:
    <https://0x04.net/~mwk/doc/xtensa.pdf>.

[^qemu]: `[measured]` 2026-09-06. A freestanding ESP32-S3 image with no ROM
    boot, no OS and the UART0 FIFO written directly, compiled with the toolchain
    in [^tc] at `-O1 -mtext-section-literals -ffreestanding -nostdlib
    -nostartfiles -fno-builtin` and booted under Espressif's
    `qemu-system-xtensa` fork as `-machine esp32s3 -m 8M -nographic -kernel
    <elf>`. It ran `clamps` at immediates 7 and 14 over fifteen inputs spanning
    zero, both saturation boundaries, both directions of overflow and the two
    32-bit extremes, against a C reference computing `min(max(x, -2^imm), 2^imm
    - 1)`. All fifteen matched for both immediates and no exception was raised.
