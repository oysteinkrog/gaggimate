---
title: GCC 14 Xtensa Flags and What They Cost
id: 04-toolchain-and-codegen/gcc14-xtensa-flags-and-what-they-cost
schema_version: 1
doc_type: reference
status: review
last_reviewed: 2026-09-06
created: 2026-09-06
tags: [esp32s3, xtensa, gcc, toolchain, codegen, esp-idf]
confidence: high
---

# GCC 14 Xtensa Flags and What They Cost

ESP-IDF does not build with plain `gcc -O2`. It adds about a dozen flags
that change instruction selection, call sequences, and switch-statement
codegen before your code ever runs. This page lists each flag, what it
does to the generated instructions, and what ESP-IDF's build system
actually passes, checked against the local toolchain and the vendored
ESP-IDF 5.5.1 source. [Caches, SRAM, PSRAM and the MSPI
bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md) and
[Code and data placement in
ESP-IDF](./code-and-data-placement-in-esp-idf.md) cover where code and
data end up. This page covers how the compiler shapes the code that goes
there.

## Toolchain identity

The installed toolchain reports itself as `gcc version 14.2.0
(crosstool-NG esp-14.2.0_20241119)`, target `xtensa-esp-elf`. `[measured]`,
`xtensa-esp32s3-elf-gcc -v`, toolchain at
`~/.platformio/packages/toolchain-xtensa-esp-elf/bin/`, 2026-09-06.[^1]

The package ships two driver binaries. `xtensa-esp-elf-gcc` is a generic,
multi-chip driver: on its own it does not know which Xtensa core it
targets, and `-print-multi-lib` lists the per-chip configurations it can
select, for example `esp32s3;@mdynconfig=xtensa_esp32s3.so`.[^1]
`-mdynconfig=<file>.so` loads a shared object naming the target core's
register file, ISA options, and ABI details. It is not a `-mcpu=` style
name, and `-mcpu=esp32s3` is rejected outright. `xtensa-esp32s3-elf-gcc`
is a second, per-chip driver that pre-selects the dynconfig: it reports
passing `-mdynconfig=xtensa_esp32s3.so -O2` even though neither was
written on the command line.[^1] ESP-IDF's CMake toolchain file sets
`CMAKE_C_COMPILER` to `xtensa-esp32s3-elf-gcc` directly, so a build never
names the dynconfig itself.[^2]

## Optimization levels

ESP-IDF exposes four levels as a Kconfig choice, `CONFIG_COMPILER_OPTIMIZATION`,
default `Debug`:[^3]

| Kconfig choice | Flag | GCC's own summary |
|---|---|---|
| `COMPILER_OPTIMIZATION_NONE` | `-O0` | No optimization |
| `COMPILER_OPTIMIZATION_DEBUG` (default) | `-Og` | "Optimize debugging experience," fast compilation, good debugging |
| `COMPILER_OPTIMIZATION_SIZE` | `-Os` plus `-freorder-blocks` under GCC (`-Oz` under Clang) | `-O2` minus optimizations that typically grow code size |
| `COMPILER_OPTIMIZATION_PERF` | `-O2` | Nearly every optimization that does not trade space for speed |

GCC 14's manual describes `-O1` as reducing code size and execution time
without spending much compile time, and `-O3` as more aggressive
optimization on top of `-O2`.[^4] Neither is an ESP-IDF Kconfig choice in
5.5.1: there is no `COMPILER_OPTIMIZATION_LEVEL3` option in the
framework's top-level `Kconfig`, so getting `-O3` on an ESP-IDF component
means a per-file override (see below), not a `menuconfig` pick.
`[measured]`, `Kconfig` at the framework root, lines 340 to 374, and a
search of the whole vendored tree for an `O3` Kconfig string, which
returns nothing.[^3]

The Kconfig choice maps to a flag through a plain `if/elseif` chain in
the framework's top-level `CMakeLists.txt`, lines 47 to 67: `-Os` for
Size, `-Og` plus `-fno-shrink-wrap` for Debug, `-O0` for None, `-O2` for
Perf.[^2] `-fno-shrink-wrap` rides with `-Og` only. It disables a
transformation that can move a function's prologue past its first
branch, trading a slightly smaller binary for a frame that is easier to
read on a debugger break.

## -mlongcalls: every cross-file call becomes indirect

The Xtensa `CALL8`/`CALL4`/`CALL0` instructions encode their target as a
PC-relative offset with limited range. `-mlongcalls` tells the compiler
the target of a call may be out of that range and to translate it to an
indirect sequence: load the target address into a register with `L32R`,
then branch through it with a `CALLX` variant.[^5] Compiling the same
one-line wrapper both ways and reading the linked object, not the `.s`
text, shows the real difference:

```
-mlongcalls:      l32r a8, <literal>   (R_XTENSA_ASM_EXPAND far_func)
                  callx8 a8
-mno-longcalls:   call8 8 <caller+0x8> (R_XTENSA_SLOT0_OP far_func)
```

`[measured]`, `xtensa-esp32s3-elf-gcc -O2 -mlongcalls -c` versus
`-mno-longcalls -c`, disassembled with `objdump -d -r`, 2026-09-06.[^1]
The `.s` text GCC emits still shows a plain `call8` pseudo-mnemonic
either way. GCC's manual is explicit that the translation happens in the
assembler, not the compiler, and that the disassembled object is where
the real instructions are visible.[^5] It also warns that the assembler
expands "every cross-file call, not just those that really are out of
range."[^5]

The linker takes most of that back. The `R_XTENSA_ASM_EXPAND` relocation
marks the expanded form as a candidate for relaxation, and linking the
two objects above put `call8 far_func` back in the caller, one direct
call and no literal; passing `-Wl,--no-relax` kept the `L32R` plus
`CALLX8` pair. `[measured]`, same two objects linked both ways,
disassembled with `objdump -d`, 2026-09-06.[^1] So `-mlongcalls` costs an
extra `L32R` (one load-use latency, one literal-pool slot) only where the
final link cannot place the callee in range.

ESP-IDF passes `-mlongcalls` unconditionally, for every language and
every target, in each `toolchain-<chip>.cmake` file. It is not a Kconfig
option, and there is no way to build a component with `-mno-longcalls`
short of a per-file override.[^2] The reason is structural: an
application is assembled from many components as separate archives, and
nothing in one component's compile step can bound the eventual
link-time distance to every other component's code.

## Literal pools: the default, -mtext-section-literals and -mauto-litpools

Xtensa has no immediate-load-anything instruction. A 32-bit constant that
does not fit `MOVI`'s encoding is loaded with `L32R`, which reads a
PC-relative literal from a pool placed somewhere reachable. GCC has three
possible arrangements, and ESP-IDF picks none of them explicitly. No
`toolchain-<chip>.cmake` file, and no line of the framework's own CMake
or Kconfig, names either of the two flags, so every Xtensa ESP-IDF build
runs on the compiler default.[^2]

That default is `-mno-text-section-literals`, which puts literals in a
separate section for the linker to place.[^6] The two overrides:
`-mtext-section-literals` has the literals "interspersed in the text
section in order to keep them as close as possible to their
references,"[^6] and `-mauto-litpools` has the compiler emit `MOVI`, not
`L32R`, for every out-of-range constant and defer the encoding choice to
the assembler, which is free to relax `MOVI` into an `L32R` plus a nearby
literal and to create as many litpools per function as needed. GCC's
manual gives the reason for the third: "very big functions, which may not
be possible with `-mtext-section-literals`."[^6]

One function loading one out-of-range constant, compiled all three ways,
shows where the difference lives. The compiler's `.s` output is identical
for the default and for `-mtext-section-literals` (both emit
`.literal_position` and a `.literal` directive) and differs for
`-mauto-litpools` (a plain `MOVI`). The object files are the other way
round: the default lands the constant in a separate `.literal` input
section, while both overrides land it inside `.text` four bytes before
the function's first instruction. The flag reaches the assembler, not the
compiler: `-###` shows `--text-section-literals` and `--auto-litpools`
passed to `as`, and nothing passed in the default case. `[measured]`,
`-O2 -S` and `-O2 -c` on the same one-constant function, `objdump -d -r`
and `objdump -h`, 2026-09-06.[^1] `[uncertain]` the function or file size
at which `-mauto-litpools` starts to matter for correctness; not measured
on a file large enough to exhaust `L32R`'s range.

## -fno-jump-tables and -fno-tree-switch-conversion

Both are appended unconditionally in the framework's top-level
`CMakeLists.txt`, with a comment naming the reason: "Placing jump tables
in flash would cause issues with code that required to be placed in
IRAM."[^2] A `switch` with enough dense, small-integer cases is a
candidate for two GCC transformations, both suppressed here.
`-ftree-switch-conversion` turns some switches into an indexed load from
a small constant array.[^7] Plain jump-table generation, which GCC's
manual says uses tables "even where it would be more efficient than
other code generation strategies" when left enabled, is disabled by
`-fno-jump-tables`.[^8] Both transformations put table data in
`.rodata`, which lives in flash by default, so these flags guarantee
every `switch` in the build compiles to a compare-and-branch chain
instead, whether or not the function is ever placed in IRAM.

The two transformations are separate, and turning off only one shows
which is which. An eight-case dense `switch` on `int` returning
unrelated constants, at `-O2`, compiled three ways:

| Flags | Whole function | Shape |
|---|---|---|
| neither flag | 8 instructions | switch conversion: `addx4` into a `CSWTCH` word array in `.rodata`, then one `l32i` |
| `-fno-tree-switch-conversion` only | 24 instructions | jump table: `l32i` from a `.rodata` array of label addresses, then `jx a8` |
| both, as ESP-IDF builds | 26 instructions | chained `beqi`/`bgei` compares, no `.rodata` table |

`[measured]`, 2026-09-06.[^1] Two cautions on reading those counts. The
totals are for the whole function, and the compare chain reaches an early
case in far fewer instructions than a late one, so the worst case is what
the last arm costs. And the counts move with the case values: the same
switch returning an arithmetic progression is recognised as arithmetic
and compiles to six instructions with no table at all, flags or no flags.
The cost is paid once per `switch`, in code size and worst-case compares,
not per call the way `-mlongcalls` is.

## -fstack-protector and its cost

ESP-IDF exposes four modes as `CONFIG_COMPILER_STACK_CHECK_MODE`, default
`None`:[^9]

| Mode | Flag | What it protects |
|---|---|---|
| None (default) | (nothing added) | no canary, no check |
| Normal | `-fstack-protector` | "only functions that call alloca, and functions with buffers larger than 8 bytes are protected"[^9] |
| Strong | `-fstack-protector-strong` | "like NORMAL, but includes additional functions to be protected: those that have local array definitions, or have references to local frame addresses"[^9] |
| Overall | `-fstack-protector-all` | "all functions are protected"[^9] |

The quoted wording is ESP-IDF's own Kconfig help, which restates GCC's
definitions of the same three flags.[^10] Compiling a function with a 32-byte stack buffer whose address escapes to
another function, with and without `-fstack-protector-strong`, measured
the concrete cost: the stack frame grew from 64 to 80 bytes, and the
function grew from 5 instructions to 18. The added work is a load of the
global `__stack_chk_guard` into the frame before the buffer is touched, a
reload and a second read of the global after the call returns, a compare,
a branch, and a `call8 __stack_chk_fail` on mismatch, with four `MEMW`
memory-ordering barriers around the canary reads and write.
`[measured]`, `xtensa-esp32s3-elf-gcc -O2 -fstack-protector-strong -S`,
2026-09-06.[^1] The exact count moves with the function; the frame growth
and the pair of guarded accesses do not.
ESP-IDF's own Kconfig help states the ordering plainly, "performance:
NORMAL > STRONG > OVERALL" and "coverage: NORMAL < STRONG < OVERALL,"
and notes the cost "includes increasing the amount of stack memory
required for each task": relevant on a system running many small
FreeRTOS tasks, where the guard variable's frame slot is paid on every
task's stack, on every protected call, not only the one that
overflowed.[^9]

## -mdisable-hardware-atomics: a PSRAM workaround, not a default

Xtensa's `S32C1I` (store 32-bit conditional, one instruction,
compare-and-swap) implements the hardware side of C11/C++11 atomics.
ESP-IDF does not disable it by default. The flag is appended only when
`CONFIG_STDATOMIC_S32C1I_SPIRAM_WORKAROUND` is set, whose Kconfig default
is `SPIRAM && (IDF_TARGET_ESP32 || IDF_TARGET_ESP32S3) &&
!IDF_TOOLCHAIN_CLANG`: on by default whenever PSRAM is configured on an
ESP32 or ESP32-S3 build using the GCC toolchain.[^11] When active, every
`stdatomic.h` operation calls a libatomic helper function instead of
emitting `S32C1I` inline, and one file inside the framework's own
atomics fallback is compiled back with `-mno-disable-hardware-atomics`,
because that file's job is to be the software fallback and needs the
real instruction to implement it.[^12] `[uncertain]` the exact hardware
defect this works around. The option's name and scope condition imply a
problem with `S32C1I` against PSRAM addresses, but neither the vendored
5.5.1 tree (searched for every `S32C1I` and
`STDATOMIC_S32C1I_SPIRAM_WORKAROUND` occurrence) nor the v5.5.1 external
RAM guide, whose own restrictions list covers cache, DMA and task stacks,
says what goes wrong.[^11] Treat the flag as a fact about the build, not
as an explanation.

## -ffunction-sections / -fdata-sections

Covered in depth in [Code and data placement in
ESP-IDF](./code-and-data-placement-in-esp-idf.md) as the mechanism that makes per-symbol linker-fragment mappings
possible; noted here only for the codegen angle. `xtensa-esp32s3-elf-gcc
--help=common` documents them as "Place each function into its own
section" and "Place data items into their own section,"[^1] applied
unconditionally at `tools/cmake/build.cmake` lines 129 to 130.[^2] The
per-function and per-variable ELF sections these flags produce are what
`--gc-sections` at link time, and the `.lf` mapping system, both depend
on. Without them, a linker-fragment rule naming one function has no
smaller unit than the whole translation unit to grab.

## -fno-builtin-memcpy / -fno-builtin-memset (and friends)

The per-chip toolchain CMake files add `-fno-builtin-memcpy
-fno-builtin-memset -fno-builtin-bzero -fno-builtin-stpcpy
-fno-builtin-strncpy` to C and C++ flags, not to plain assembly.[^2]
Without these, GCC can recognize a hand-written byte-copy loop as
equivalent to `memcpy` and replace it with a call to its own builtin,
which may inline a sequence tuned for a generic target rather than
calling the platform's actual function. The flags stop GCC from silently
substituting a different implementation than the project links. For a
kernel author writing a byte-copy loop expecting it to run verbatim, this
is why it does.

Which implementation that is, on the ESP32-S3, is worth stating plainly
because it is easy to get wrong. `memcpy`, `memset`, `memmove`, `bzero`
and `strncpy` are all absolute addresses in the chip's boot ROM, assigned
by a linker fragment: `components/esp_rom/esp32s3/ld/esp32s3.rom.libc.ld`
holds `memcpy = 0x400011f4;` and its siblings, and the build includes
that fragment whenever the target declares `ESP_ROM_HAS_NEWLIB`, which
the ESP32-S3 does.[^16] Because a linker-script assignment defines the
symbol, no library member supplying `memcpy` is ever pulled in.

ESP-IDF does compile its own tuned string routines, but not for this
chip. `components/newlib/CMakeLists.txt` gates them behind
`CONFIG_LIBC_OPTIMIZED_MISALIGNED_ACCESS`, that option `depends on
ESP_ROM_HAS_SUBOPTIMAL_NEWLIB_ON_MISALIGNED_MEMORY`, and that capability
is declared only by the family's RISC-V members; the ESP32-S3's
`esp_rom_caps.h` does not define it, and the sources themselves live
under `src/port/riscv/`.[^16] See [Loop shapes, scheduling and per-call
setup](../06-kernel-patterns/loop-shapes-scheduling-and-per-call-setup.md)
for when a hand-written copy loop beats the shipped one.

## -fstrict-volatile-bitfields

Applied unconditionally when the compiler ID is GNU, `CMakeLists.txt`
line 199 of the framework root.[^2] GCC's manual: this option "should be
used if accesses to volatile bit-fields ... should use a single access
of the width of the field's type, aligned to a natural alignment if
possible."[^13] ESP32-series peripheral register structs are typically
bitfields over a `volatile uint32_t`. Without this flag, GCC is free to
satisfy a bitfield write with a byte or halfword access that happens to
touch the right bits, which a memory-mapped peripheral register can
read as a distinct, unintended write to a neighboring field.

## LTO status on Xtensa in ESP-IDF 5.5.1

The toolchain supports link-time optimization: its own `configured-with`
string includes `--enable-lto`, and `xtensa-esp32s3-elf-gcc -v` reports
"Supported LTO compression algorithms: zlib zstd."[^1] ESP-IDF does not
use it. The framework's top-level `CMakeLists.txt` appends `-fno-lto`
unconditionally to `link_options`,[^2] and there is no
`CONFIG_COMPILER_LTO`-style Kconfig choice anywhere in the vendored 5.5.1
source tree searched for this page. `[uncertain]` whether a project can
still opt in per-target with a matching linker override; not tested
against a real multi-component link in this session. `-fno-lto` would
need to be overridden for every component archive, not just one
requesting LTO, for whole-program optimization to take effect across
component boundaries.

## -mabi=windowed (default) vs. call0

Neither the `xtensa-esp32s3-elf-gcc` chip-specific driver nor any
`toolchain-<xtensa-chip>.cmake` file passes `-mabi=`. Only the RISC-V
chip toolchain files set an ABI flag (`-mabi=ilp32` / `-mabi=ilp32f`).[^2]
So Xtensa ESP-IDF builds run on the default, and the manual says where
that default comes from: "Default ABI is chosen by the Xtensa core
configuration,"[^14] which here is the dynconfig the chip-specific driver
loads. For the ESP32-S3 it is windowed: a trivial function compiled with
no ABI flag opens with `entry sp, 32` and closes with `retw.n`, the
windowed convention's frame setup and register-window rotation. The same
function under `-mabi=call0` produces no frame instructions at all, a
plain `ret.n`. `[measured]`, both forms, 2026-09-06.[^1]

The manual states the register consequences of each. Under `call0`,
"function parameters are passed in registers `a2` through `a7`, registers
`a12` through `a15` are caller-saved, and register `a15` may be used as a
frame pointer." Under `windowed`, "function parameters are passed in
registers `a10` through `a15`, and called function rotates register
window by 8 registers on entry so that its arguments are found in
registers `a2` through `a7`."[^14] A project chooses `call0` to avoid the
windowed ABI's register-window spill and fill machinery in code paths
where that machinery cannot safely run, historically low-level
interrupt or boot code before the window overflow/underflow handlers are
installed. ESP-IDF's own low-level assembly is written for the windowed
default, and a project has no supported way to build ordinary
application components as `call0` without also replacing that startup
code.

## Per-file and per-function overrides

Three real ESP-IDF examples of narrowing a flag to less than the whole
build. Per-file, with `set_source_files_properties`:
`components/newlib/CMakeLists.txt` compiles exactly one file, `heap.c`,
with `-fno-builtin` (broader than the memcpy-family flags above,
disabling builtin recognition for that whole file), and
`components/newlib/project_include.cmake` compiles one atomics-fallback
source file with `-mno-disable-hardware-atomics` even when the rest of
the build has hardware atomics disabled.[^12] Per-function, with
`__attribute__((optimize(...)))`: `components/bootloader_support/src/esp_image_format.c`
forces `__attribute__((optimize("O0")))` on one function regardless of
the file's build-wide level, and `components/esp_hw_support/port/esp32/rtc_clk.c`
forces `__attribute__((optimize("-O2")))` the other direction, to keep a
clock-configuration routine optimized inside a `-Og` or `-Os` build.
GCC's manual: the attribute's argument string "behave[s] as if appended
to the command-line," so any `-f`/`-O` combination valid on the command
line is valid here.[^15] Per-target, at the build-system level: a
component's own `CMakeLists.txt` can call `target_compile_options`
after `idf_component_register`, or `idf_component_set_property(<component>
COMPILE_OPTIONS <list> APPEND)` before registration, to change flags for
every file in one component without touching the top-level
`CMakeLists.txt`.

## What to keep identical when compiling a kernel out of tree

Compiling one file's kernel outside the project only matches the
in-tree build's codegen if the optimization level, `-mlongcalls`, the
`-fno-jump-tables`/`-fno-tree-switch-conversion` pair, the active
`-fstack-protector*` mode, `-mdisable-hardware-atomics`, and the ABI
(`-mabi=`, windowed by the core configuration and never overridden by
ESP-IDF for Xtensa) all match: each changes instruction selection, call
sequences, or the calling convention itself, as shown above. Add
`-mtext-section-literals` or `-mauto-litpools` only if the real build
passes one, which for ESP-IDF it does not: adding either out of tree
moves literals into `.text` and makes an otherwise clean disassembly
diff look wrong. Mixing a
`call0`-compiled snippet into a windowed-ABI project silently corrupts
the register windows. `-ffunction-sections -fdata-sections` and
`-fstrict-volatile-bitfields` matter less for instruction selection but
still change section layout and bitfield-access width, which can affect
a disassembly diff. The reliable way to guarantee a match is to copy the
flags the real build actually used, not to reconstruct this list from
memory.

## Printing the actual flags of a real build

ESP-IDF's CMake layer sets `CMAKE_EXPORT_COMPILE_COMMANDS ON`
unconditionally in `tools/cmake/project.cmake`,[^2] so every build
produces `build/compile_commands.json`: one JSON object per translation
unit with the exact compiler invocation, in order, as issued. This is
the authoritative source, since it reflects the real per-file flags
after every Kconfig choice and per-file override has been applied.
Reading Kconfig and CMakeLists.txt, as this page does, shows what should
end up in the invocation; only the generated command line guarantees it
for one specific file. `idf.py -v build` reprints every underlying build
command, compiler invocations included, to the console as the build
runs, useful for watching one file's flags scroll by without opening the
JSON.

## Compiling one file to assembly with annotations

```
xtensa-esp32s3-elf-gcc -O2 -S -fverbose-asm somefile.c -o somefile.s
```

`-fverbose-asm` adds a trailing comment to most generated instructions
naming the C-level values or temporaries involved, and a header comment
naming the exact GCC version and options actually passed for that
compile (`# options passed: -mdynconfig=xtensa_esp32s3.so -O2`, for one
two-line test function). `[measured]`, 2026-09-06.[^1] This is the
fastest way to correlate one line of C with the instructions it
produced, without a full build. Combine it with flags copied from
`compile_commands.json`, not guessed, whenever the source file has
project-specific include paths or macros that change which branch of an
`#ifdef` actually compiles.

## Footnotes

[^1]: [measured] Local toolchain, `xtensa-esp32s3-elf-gcc` and `xtensa-esp-elf-gcc`, `gcc version 14.2.0 (crosstool-NG esp-14.2.0_20241119)`, at `~/.platformio/packages/toolchain-xtensa-esp-elf/bin/`. Commands run: `-v`, `-print-multi-lib`, `--help=target`, `--help=common`, `-###`, `-O2 -S [-fverbose-asm]`, `-O2 -c` (disassembled with `objdump -d -r` and inspected with `objdump -h`), and a two-object link with and without `-Wl,--no-relax`. All against small standalone test files under `/tmp`, none part of any project source tree. All runs 2026-09-06.

[^2]: Source, `framework-espidf` package under `~/.platformio/packages/`, ESP-IDF 5.5.1 (`version.txt`): `CMakeLists.txt` (root, lines 47-67 optimization mapping, 160-165 stack-protector mapping, 199 `-fstrict-volatile-bitfields`, 217 `-fno-lto`, 262-267 `-fno-jump-tables`/`-fno-tree-switch-conversion`); `tools/cmake/toolchain-esp32s3.cmake` (`-mlongcalls`, the `-fno-builtin-*` list); `tools/cmake/build.cmake` (lines 129-130, `-ffunction-sections -fdata-sections`); `tools/cmake/project.cmake` (line 593, `CMAKE_EXPORT_COMPILE_COMMANDS ON`). Neither flag naming a literal-pool mode appears anywhere in the package's CMake or Kconfig files, checked by searching the whole tree for `text-section-literals` and `auto-litpools`.

[^3]: Source, `framework-espidf` package, root `Kconfig`, lines 340-374, `choice COMPILER_OPTIMIZATION` (`endchoice` at 374; the enclosing `menu "Compiler options"` opens at 338).

[^4]: Free Software Foundation, *Using the GNU Compiler Collection (GCC)*, 14.2.0, "Optimize Options" (gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Optimize-Options.html), read 2026-09-06.

[^5]: Free Software Foundation, *GCC*, 14.2.0, "Xtensa Options," `-mlongcalls`/`-mno-longcalls` (gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Xtensa-Options.html), read 2026-09-06.

[^6]: Free Software Foundation, *GCC*, 14.2.0, "Xtensa Options," `-mtext-section-literals`/`-mno-text-section-literals` and `-mauto-litpools`/`-mno-auto-litpools` (gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Xtensa-Options.html), read 2026-09-06.

[^7]: Free Software Foundation, *GCC*, 14.2.0, "Optimize Options," `-ftree-switch-conversion` (gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Optimize-Options.html), read 2026-09-06.

[^8]: Free Software Foundation, *GCC*, 14.2.0, "Options for Code Generation Conventions," `-fno-jump-tables` (gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Code-Gen-Options.html), read 2026-09-06.

[^9]: Source, `framework-espidf` package, root `Kconfig`, lines 505-539, `choice COMPILER_STACK_CHECK_MODE` and its help text, read 2026-09-06.

[^10]: Free Software Foundation, *GCC*, 14.2.0, "Instrumentation Options," `-fstack-protector-strong` and `-fstack-protector-all` (gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Instrumentation-Options.html), read 2026-09-06.

[^11]: Source, `framework-espidf` package, `components/newlib/Kconfig`, lines 165-167, config `STDATOMIC_S32C1I_SPIRAM_WORKAROUND`. Also Espressif Systems, *ESP-IDF Programming Guide* v5.5.1, "Support for External RAM," Restrictions section (docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-guides/external-ram.html), read 2026-09-06. It names cache, DMA descriptors and task stacks, but not atomics.

[^12]: Source, `framework-espidf` package, `components/newlib/project_include.cmake` (lines 1-2, appends `-mdisable-hardware-atomics` when the workaround config is on) and `components/newlib/CMakeLists.txt` (lines 89-93, `heap.c` compiled with `-fno-builtin`, `src/port/xtensa/stdatomic_s32c1i.c` compiled with `-mno-disable-hardware-atomics`).

[^13]: Free Software Foundation, *GCC*, 14.2.0, "Options for Code Generation Conventions," `-fstrict-volatile-bitfields` (gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Code-Gen-Options.html), read 2026-09-06.

[^14]: Free Software Foundation, *GCC*, 14.2.0, "Xtensa Options," `-mabi=call0`/`-mabi=windowed` (gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Xtensa-Options.html), read 2026-09-06.

[^15]: Free Software Foundation, *GCC*, 14.2.0, "Common Function Attributes," `optimize` (gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Common-Function-Attributes.html), read 2026-09-06; source examples, `components/bootloader_support/src/esp_image_format.c` line 669 and `components/esp_hw_support/port/esp32/rtc_clk.c` line 424, `framework-espidf` package.

[^16]: Source, `framework-espidf` package, ESP-IDF 5.5.1: `components/esp_rom/esp32s3/ld/esp32s3.rom.libc.ld` lines 7-17 (`memset`, `memcpy`, `memmove`, `strncpy`, `bzero` as absolute ROM addresses); `components/esp_rom/CMakeLists.txt` line 308, which includes that fragment when `CONFIG_ESP_ROM_HAS_NEWLIB` is set; `components/esp_rom/esp32s3/esp_rom_caps.h` line 26, `ESP_ROM_HAS_NEWLIB (1)`. For the override that does not apply here: `components/newlib/CMakeLists.txt` lines 41-55, `components/newlib/Kconfig` lines 146-149, and the absence of `ESP_ROM_HAS_SUBOPTIMAL_NEWLIB_ON_MISALIGNED_MEMORY` from `components/esp_rom/esp32s3/esp_rom_caps.h`. Read 2026-09-06.
