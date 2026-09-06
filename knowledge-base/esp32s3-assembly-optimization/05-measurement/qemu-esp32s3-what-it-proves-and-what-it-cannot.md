---
title: QEMU for the ESP32-S3, what it proves and what it cannot
id: 05-measurement/qemu-esp32s3-what-it-proves-and-what-it-cannot
schema_version: 1
doc_type: explanation
status: review
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, qemu, pie, measurement, verification, gdb]
confidence: medium
---

# QEMU for the ESP32-S3, what it proves and what it cannot

Espressif maintains a fork of QEMU that emulates the ESP32-S3, including
the PIE vector unit. It runs a kernel and tells you what the kernel
computes. It does not tell you how long the kernel takes.

Use it to check that an instruction sequence is legal and produces the
values you expect, on a machine you can run in a loop with no board
attached. Do not take a number out of it. And do not treat QEMU agreement
as hardware agreement: the emulator has open, reported cases where its
arithmetic differs from silicon.

## The machine and the release line

The fork lives at `github.com/espressif/qemu`, on the `esp-develop`
branch, which is periodically rebased on upstream QEMU.[^1] Three chips
are emulated: ESP32, ESP32-S3 and ESP32-C3.[^2] The ESP32-S3 machine is
selected with `-machine esp32s3` (equivalently `-M esp32s3`), and the
binary is `qemu-system-xtensa`.[^3]

ESP32-S3 support first appeared in the release
`esp-develop-9.0.0-20240606` (June 2024), whose notes read "Added initial
ESP32-S3 support".[^4] As of September 2026 the current line is QEMU
9.2.2, and the newest release is `esp-develop-9.2.2-20260417`, published
2026-04-19.[^4]

The machine class allows two CPUs and defaults to two, matching the dual
core part.[^5] Default RAM size is 0, and the `-m` value is what creates
the emulated PSRAM chip on SPI1, rounded up to 2, 4, 8, 16 or 32 MiB.[^5]
So `-M esp32s3` with no `-m` gives you a board with no PSRAM.

Wi-Fi, Bluetooth, USB, I2C, I2S and the GPIO matrix are not
implemented.[^2] Anything you write that touches those will not run.

## What the Xtensa core model executes

The ESP32-S3 core overlay declares the configuration options the LX7 has:
windowed registers, zero-overhead loops, NSA, MIN/MAX, SEXT, CLAMPS,
MUL32 and MUL32_HIGH, booleans, MAC16, and single-precision floating
point with DIV, RECIP, SQRT and RSQRT. Double precision is off.[^6]

The FPU is emulated. The core registers three translator tables, and one
of them is `xtensa_fpu_opcodes`, QEMU's own FP0 implementation.[^7]
Single-precision arithmetic therefore runs, subject to the `CPENABLE`
caveat under "What QEMU proves" below.

The PIE (EE.*) extension is implemented, and implemented in bulk. It
lives in a single Espressif-written file, `target/xtensa/
translate_tie_esp32s3.c`, registered as `xtensa_tie_opcodes` in the
core's translator list.[^7] At commit `ba59503` (2026-03-27) that file's
opcode tables name 258 distinct mnemonics: 217 beginning with `ee.`,
19 `rur.*` and 19 `wur.*` accessors for the PIE user registers (ACCX,
QACC high and low, SAR_BYTE, UA_STATE, FFT_BIT_WIDTH, GPIO_OUT), plus
`ld.qr`, `st.qr` and `mv.qr`.[^8] The families a kernel writer reaches
for are all present: aligned and unaligned q loads and stores with
increment and index update, broadcast loads, `ldqa` widening loads, zip
and unzip, saturating add and subtract, the `vmulas` accumulate family,
min, max, compare, funnel shift (`src`), FFT helpers, `cmul`, bit
reverse, and the logical ops on q registers.[^8]

Coverage is not the same as completeness, and it was not complete on
arrival. The extension shipped in April 2024 and was materially extended
in March 2026, and the extension commit is a list of what had been
missing or wrong for nearly two years: the bare `ee.vldbc.{8,16,32}`
forms, the `ee.st.qacc_*` stores, `ee.st.accx.ip`, `ee.ld.ua_state.ip`
and `ee.st.ua_state.ip`, and `ee.ldxq.32` / `ee.stxq.32`. The same commit
fixed `ee.src.q`, which had been copying qs1 into qs0 for the bare form
when the manual says only the `.qup` variant does that.[^9] Treat the
instruction set as "mostly there, verify the exotic corners".

## Timing: there is none

Nothing in the ESP32-S3 machine models cycle cost. The claim rests on
three separate places in the source.

**CCOUNT is a clock reading, not a counter of work.** The helper that
updates CCOUNT samples `QEMU_CLOCK_VIRTUAL` and converts nanoseconds to
ticks with the CPU's clock object.[^10] So `RSR.CCOUNT` under QEMU
returns elapsed virtual time scaled by a frequency, and the instructions
executed in between do not enter the arithmetic.

**The frequency is wrong for the part, too.** The Xtensa CPU object sets
its clock from `config->clock_freq_khz` at init,[^11] the ESP32-S3 core
config sets that field to 40000,[^12] and the machine creates the CPUs
without overriding the clock.[^5] CCOUNT therefore advances as if the
core ran at 40 MHz, not 240 MHz. A CCOUNT delta from QEMU is not the
device's cycle count and is not even a fixed multiple of it.

**Cache, PSRAM and flash cost nothing.** The ESP32-S3 cache device is an
address-translation and decryption model. It maps flash and PSRAM pages
into the address space and implements the cache control registers. It
contains no wait states, no miss penalty and no MSPI arbitration.[^13]
The whole memory hierarchy that decides a real kernel's speed, icache
misses, dcache misses, load-use stalls, PSRAM latency and the flash and
PSRAM sharing one bus, is absent. A kernel whose real cost is table
placement will look identical in QEMU whichever memory the table is in.

`-icount` does not rescue this. It makes the virtual CPU execute one
instruction every 2^N nanoseconds of virtual time, which makes runs
deterministic, and QEMU's own documentation says plainly that it "does
not provide cycle accurate emulation".[^14] With `-icount` a CCOUNT delta
becomes a linear function of instructions retired, which is a useful
determinism property and still not a cycle count. [uncertain] I did not
run this combination to confirm the exact scaling.

## What QEMU proves

Given all that, three things are worth doing in QEMU and cannot be done
as cheaply anywhere else.

**Instruction validity.** If an instruction is not in the translator
tables, execution does not quietly continue with a plausible value. The
opcode fails to decode and the guest takes an illegal instruction
exception. There is no path where an unimplemented EE.* instruction
returns a correct-looking result. This is the property that makes QEMU a
gate rather than an advisory.

**Bit-exactness against a reference.** Run the hand-written kernel and
the portable reference over the same inputs in the same emulated process,
compare the outputs byte for byte, and print a pass or fail line over the
UART. It catches wrong lane order, wrong saturation width, wrong shift
direction, wrong accumulator drain, and misaligned spans, over as many
inputs as you care to feed it, at the cost of starting a process.

**Control flow and state.** Window overflow and underflow handling,
exception entry, coprocessor enable, and whether a routine clobbers a
callee-saved register are all emulated faithfully enough to be worth
testing.

One caveat sits on top of that. Issue #154 reports a freestanding image
hanging under the ESP32-S3 machine because it boots with `CPENABLE = 0`
and the first floating-point operation recurses through the Cp0Disabled
handler.[^26] It is an emulator defect: the open pull request #155 on
that issue measured `CPENABLE = 0xff` at reset on silicon and traces the
QEMU behaviour to `target/xtensa/cpu.c`, whose reset hook sets `CPENABLE`
to `0xff` only in the user-mode build, never in the system emulation that
`qemu-system-xtensa` runs.[^26] The fix was unmerged as of 2026-09-06, so
set `CPENABLE` in your own startup rather than relying on the reset state.
The [adversarial register of QEMU divergences](../09-adversarial/qemu-versus-silicon-known-and-suspected-divergences.md)
carries the details.

## What QEMU does not prove

**Speed, at any granularity.** Not cycles, not relative cost between two
kernels, not whether a table belongs in internal RAM. Every performance
question goes to the device.

**Hardware agreement.** This is the trap. QEMU's PIE implementation is a
second implementation of the manual, and it has documented divergences.
As of September 2026 the fork's tracker carries six open ESP32-S3 PIE
issues filed 2026-08-02, each with expected values measured on silicon:

| Issue | Divergence |
|---|---|
| #161 (QEMU-300) | `EE.VADDS` / `EE.VSUBS` clamp negative results to `-MAX` instead of `MIN`, so an S8 result that should be `0x80` comes out `0x81`[^15] |
| #162 (QEMU-301) | `EE.LDXQ.32` / `EE.STXQ.32` compute an indexed address four bytes too low[^16] |
| #163 (QEMU-302) | With SAR=32, `EE.VSL`, `EE.VSR` and `EE.VMUL` shift by zero instead of shifting out, because the helpers shift a 32-bit C value by 32, which is undefined behaviour[^17] |
| #164 (QEMU-303) | `EE.CMUL.S16` corrupts results when the destination register aliases the first input[^18] |
| #165 (QEMU-304) | `EE.VMULAS.U8.QACC` fails unsigned 20-bit saturation[^18] |
| #166 (QEMU-305) | `EE.SLCXXP.2Q` and `EE.SRCXXP.2Q` do not mask the dynamic count to `as[3:0]`. With `as=16` the helper indexes past the q register arrays and QEMU terminates, where hardware treats the count as zero[^19] |

Read that table as the shape of the risk, not as a fixed list. Each is a
case where a kernel passes in QEMU and computes something else on the
chip, or the reverse. Saturation edges, shift counts at the width
boundary, destination aliasing and dynamic shift amounts are the four
places to distrust the emulator and confirm on hardware.

**Anything the peripherals do.** No radio, no USB, no I2C or I2S.[^2]

## Running a kernel

Two flows exist, and for kernel work the first is much lighter.

**Bare ELF with `-kernel`.** The machine loads the file given to
`-kernel` (or `-bios`; `-kernel` wins if both are present) as an ELF, and
if the entry point is not the reset vector it writes a small boot stub at
`0x40000400` that does `l32r a0, elf_entry` then `jx a0`, and starts the
CPU there.[^20] No bootloader, no partition table, no flash image. If
neither option is given the machine needs `esp32s3_rev0_rom.bin` and
exits if it cannot find it.[^20]

```
qemu-system-xtensa -machine esp32s3 -nographic -no-reboot \
    -m 8M -kernel build/test.elf \
    -serial file:uart.log
```

`-m 8M` attaches 8 MiB of PSRAM; drop it if the test does not need
any.[^5] `-serial file:uart.log` writes the emulated UART0 stream to a
file, which is what a batch harness reads to decide pass or fail.

**Full ESP-IDF image.** `idf.py qemu` builds and merges a flash image,
then starts `qemu-system-xtensa -M esp32s3` with that image as an MTD
drive plus an eFuse drive, the timer group watchdog disabled, and
`-nographic` or `-display sdl`.[^21] `idf.py qemu monitor` adds the IDF
monitor on the emulated UART and `idf.py qemu gdb` opens a GDB
session.[^22] The arguments it builds contain no `-m`, so PSRAM is absent
under this flow unless you add it through `--qemu-extra-args`.[^21] Use
this when the code under test needs the IDF runtime.

**Semihosting.** The Xtensa `SIMCALL` instruction is wired to QEMU's
semihosting layer, gated on `semihosting_enabled()`, so it works when the
emulator is started with `-semihosting` and logs "SIMCALL but semihosting
is disabled" otherwise.[^23] `SIMCALL` is privileged.[^23] This gives a
freestanding test host file access and an exit code without a
filesystem.

## A minimal freestanding test

To run windowed-ABI C and hand-written assembly with no operating system,
the entry point has to establish four things before it calls anything:

1. A stack pointer in `a1` pointing into DRAM.
2. `PS` with `WOE` set, `EXCM` clear and `INTLEVEL` at the maximum, so
   window overflow and underflow exceptions are dispatched rather than
   fatal. Write it with `WSR.PS` followed by `RSYNC`.
3. `WINDOWBASE` and `WINDOWSTART` consistent, and `VECBASE` pointing at a
   vector table that carries at least the window overflow and underflow
   vectors at their fixed offsets, plus a level-1 user vector.
4. `CPENABLE` set for the coprocessors the test uses. A bare-metal
   harness has no lazy coprocessor-disabled handler, so it sets
   `CPENABLE` itself, once, in its own startup. Production code under an
   RTOS must not do this, because writing `CPENABLE` by hand bypasses the
   context save that preserves another task's coprocessor state.

With `-kernel` the machine jumps straight to the ELF entry point,[^20] so
that entry point is the first code that runs and no ROM bootloader has
done any of the above for you.

## GDB, and the register you cannot read

Start the emulator with `-s -S` to open a GDB stub on TCP port 1234 and
hold the CPU stopped before the first instruction, then attach with the
Xtensa GDB from the toolchain and `target remote :1234`. Under
`idf.py qemu gdb` the port is 3333 and the flags are `-gdb tcp::3333
-S`.[^22] Stepping, breakpoints and reading `a0` to `a15`, the special
registers and the user registers all work through the standard stub
paths.[^24]

The q registers are the exception, and this matters for PIE debugging.
The ESP32-S3 GDB register map declares `q0` through `q7`, each a 16-byte
register of type `xtRegisterTypeTieRegfile`.[^25] But the stub's read and
write handlers for that type switch on size and implement only 4 and 8,
the FP register sizes. A 16-byte register falls to the default arm, which
logs `LOG_UNIMP`, returns zeroes on read and discards the value on
write.[^24] Reading `$q0` in GDB gives you zeroes, not the contents.

The PIE accumulators are readable, because they are exposed differently.
`ACCX` and `QACC` also appear in the map as 32-bit user registers
(`accx_0`, `accx_1`, `qacc_h_0` to `qacc_h_4`, `qacc_l_0` to
`qacc_l_4`), which route through the user-register path and read out
correctly.[^25] [^24] To see a q register's contents, store it to memory
with `EE.VST.128.IP` and examine the memory.

## What to prove where

| Question | QEMU | Host reference build | Device |
|---|---|---|---|
| Does the instruction exist and decode? | Yes, this is the cheapest gate | No, the host does not have the ISA | Yes, but slower to iterate |
| Does the kernel match the reference bit for bit? | Yes, over many inputs, in a loop | Yes, for the portable reference against itself | Yes, and this is the one that counts |
| Does the emulator's arithmetic match silicon? | No, this is the open question | No | Yes, only here |
| Windowed ABI, exception and register-window correctness | Yes | No | Yes |
| Saturation edges, SAR at the width boundary, destination aliasing, dynamic shift counts | Suspect, see the issue table | No | Yes, required |
| How many cycles? | Never | Never | Only here |
| Is a table better in internal RAM or PSRAM? | Never, no memory timing model | Never | Only here |
| Which of two kernels is faster? | Never | No, an instruction count is code shape, not time | Only here |
| Where does the time go inside a frame? | No | Sometimes, as a hypothesis to test | Yes, with a cycle counter |
| Does the code survive random inputs? | Yes, but slowly | Yes, and with sanitizers on, which is the point | Yes, as a final pass |

The ladder that falls out of this: get the values right on the host
against a portable reference, prove the sequence legal and bit-exact in
QEMU, then confirm on the device, where both the arithmetic and the
timing are the real ones. QEMU sits in the middle because it is the only
step that runs the actual instructions without a board. It is not the
last step, and a claim about speed never comes from it.

## Footnotes

[^1]: Espressif, "esp-toolchain-docs: QEMU", `github.com/espressif/esp-toolchain-docs/blob/main/qemu/README.md`, retrieved 2026-09-06. States the fork is maintained on the `esp-develop` branch and is periodically rebased.
[^2]: Espressif, "esp-toolchain-docs: QEMU", same page. Target table lists ESP32, ESP32-S3 and ESP32-C3; the limitations section names Wi-Fi, Bluetooth, USB, I2C, I2S and GPIO matrix as not implemented.
[^3]: ESP-IDF v5.5.1, `tools/idf_py_actions/qemu_ext.py`, `QEMU_TARGETS['esp32s3']`, which pairs the program `qemu-system-xtensa` with the argument string `-M esp32s3`.
[^4]: Espressif QEMU releases, `github.com/espressif/qemu/releases`, retrieved 2026-09-06. `esp-develop-9.0.0-20240606` (2024-06-06) notes "Added initial ESP32-S3 support"; `esp-develop-9.2.2-20260417` (published 2026-04-19) is the latest release.
[^5]: espressif/qemu, `hw/xtensa/esp32s3.c`, branch `esp-develop`, retrieved 2026-09-06. `esp32s3_machine_class_init` sets `max_cpus = 2`, `default_cpus = 2`, `default_ram_size = 0` and `fixup_ram_size = esp32s3_fixup_ram_size`, which rounds to 2, 4, 8, 16 or 32 MiB. `esp32s3_machine_init_psram` attaches a `TYPE_SSI_PSRAM` of `machine->ram_size / MiB` on SPI1 CS1. CPUs are created with `object_initialize_child(obj, name, &s->cpu[i], TYPE_ESP32S3_CPU)` with no clock override.
[^6]: espressif/qemu, `target/xtensa/core-esp32s3/core-isa.h`, branch `esp-develop`. `XCHAL_HAVE_WINDOWED`, `LOOPS`, `NSA`, `MINMAX`, `SEXT`, `CLAMPS`, `MUL32`, `MUL32_HIGH`, `BOOLEANS`, `MAC16`, `FP`, `FP_DIV`, `FP_RECIP`, `FP_SQRT`, `FP_RSQRT` are 1; `XCHAL_HAVE_DFP` is 0; `XCHAL_HAVE_DFPU_SINGLE_ONLY` is 1.
[^7]: espressif/qemu, `target/xtensa/core-esp32s3.c`, branch `esp-develop`. `esp32s3_opcode_translators` is `{ &xtensa_core_opcodes, &xtensa_fpu_opcodes, &xtensa_tie_opcodes }`.
[^8]: espressif/qemu, `target/xtensa/translate_tie_esp32s3.c`, commit `ba5950398f1e` (2026-03-27). Counts obtained by extracting every `.name = "..."` entry from the opcode tables: 258 distinct mnemonics, of which 217 start with `ee.`, 19 with `rur.` and 19 with `wur.`, plus `ld.qr`, `st.qr` and `mv.qr`.
[^9]: espressif/qemu, commit `ba5950398f1e`, "feat(xtensa/esp32s3): Extend the ESP32-S3's TIE instructions" (2026-03-27). Commit message enumerates the added instructions and the `ee.src.q` fix. The prior implementation is commit `76f9e1f1546f`, "hw/xtensa: implement ESP32-S3 TIE instructions" (2024-04-23).
[^10]: espressif/qemu, `target/xtensa/op_helper.c`, branch `esp-develop`, `HELPER(update_ccount)`: `now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)` then `env->sregs[CCOUNT] = env->ccount_base + clock_ns_to_ticks(cpu->clock, now - env->time_base)`.
[^11]: espressif/qemu, `target/xtensa/cpu.c`, branch `esp-develop`: `cpu->clock = qdev_init_clock_in(...)` followed by `clock_set_hz(cpu->clock, env->config->clock_freq_khz * 1000)`.
[^12]: espressif/qemu, `target/xtensa/core-esp32s3.c`, `XtensaConfig xtensa_core_esp32s3` sets `.clock_freq_khz = 40000`.
[^13]: espressif/qemu, `hw/misc/esp32s3_cache.c`, branch `esp-develop`, 454 lines, header comment "ESP32-S3 ICache emulation". The file implements cache and MMU register behaviour and flash/PSRAM page mapping with XTS-AES decryption. It contains no wait-state, latency or miss-penalty modelling.
[^14]: QEMU project, "Invocation", `qemu.org/docs/master/system/invocation.html`, `-icount` entry, retrieved 2026-09-06: "The virtual cpu will execute one instruction every 2^N ns of virtual time. ... Note that while this option can give deterministic behavior, it does not provide cycle accurate emulation."
[^15]: espressif/qemu issue #161, "ESP32-S3 PIE: EE.VADDS/EE.VSUBS clamp signed negative results to -MAX instead of MIN (QEMU-300)", opened 2026-08-02, open as of 2026-09-06.
[^16]: espressif/qemu issue #162, "ESP32-S3 PIE: EE.LDXQ.32/EE.STXQ.32 indexed address is four bytes too low (QEMU-301)", opened 2026-08-02, open as of 2026-09-06.
[^17]: espressif/qemu issue #163, "ESP32-S3 PIE: SAR=32 VSL/VSR/VMUL results differ from real hardware (QEMU-302)", opened 2026-08-02. The report gives hardware-measured expected vectors from an ESP32-S3 revision v0.2 and notes that a 32-bit C shift by 32 is undefined behaviour, so the result may depend on the host compiler.
[^18]: espressif/qemu issues #164 (QEMU-303) and #165 (QEMU-304), both opened 2026-08-02, open as of 2026-09-06.
[^19]: espressif/qemu issue #166, "ESP32-S3 PIE: dynamic cross-shifts do not mask as[3:0] and can terminate QEMU (QEMU-305)", opened 2026-08-02. The report cites `target/xtensa/translate_tie_esp32s3.c` at commit `febae182e132e4055529be423a818225ebddaa3a` and states that both instructions ran without exception on ESP32-S3 revision v0.2.
[^20]: espressif/qemu, `hw/xtensa/esp32s3.c`, `esp32s3_machine_init`. `-kernel` overrides `-bios` with a warning; `load_elf` is called on the chosen file; if the entry point is not `XCHAL_RESET_VECTOR_PADDR` (0x40000400 per `core-isa.h`) a boot blob of `j 1; .literal elf_entry; 1: l32r a0, elf_entry; jx a0` is placed at the reset vector. With neither option the machine loads `esp32s3_rev0_rom.bin` or exits.
[^21]: ESP-IDF v5.5.1, `tools/idf_py_actions/qemu_ext.py`. Builds `qemu-system-xtensa -M esp32s3` plus `-drive file=<image>,if=mtd,format=raw`, an eFuse drive, `-global driver=timer.esp32s3.timg,property=wdt_disable,value=true`, `-nic user,model=open_eth`, and `-nographic` or `-display sdl`. GDB adds `-gdb tcp::3333 -S`. No `-m` argument is emitted.
[^22]: Espressif, "QEMU Emulator", ESP-IDF Programming Guide v5.5.1, ESP32-S3 target, `docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-guides/tools/qemu.html`, retrieved 2026-09-06. Documents `idf.py qemu monitor`, `idf.py qemu gdb`, `--gdb`, `--graphics`, `--flash-file` and the eFuse subcommands.
[^23]: espressif/qemu, `target/xtensa/translate.c`, branch `esp-develop`: `translate_simcall` calls `gen_helper_simcall` only when `semihosting_enabled(dc->cring != 0)`; `test_exceptions_simcall` logs "SIMCALL but semihosting is disabled" otherwise. The opcode entry carries `XTENSA_OP_PRIVILEGED`.
[^24]: espressif/qemu, `target/xtensa/gdbstub.c`, branch `esp-develop`. `xtensa_cpu_gdb_read_register` and `xtensa_cpu_gdb_write_register` handle `xtRegisterTypeTieRegfile` only for `reg->size` 4 and 8; other sizes hit the default arm, which calls `qemu_log_mask(LOG_UNIMP, ...)` and `gdb_get_zeroes(mem_buf, reg->size)` on read. `xtRegisterTypeUserReg` reads and writes `env->uregs[...]` directly.
[^25]: espressif/qemu, `target/xtensa/core-esp32s3/gdb-config.inc.c`, branch `esp-develop`. `q0` to `q7` are declared with bit size 128, byte size 16 and type 4 (`xtRegisterTypeTieRegfile`); `accx_0`, `accx_1`, `qacc_h_0` to `qacc_h_4` and `qacc_l_0` to `qacc_l_4` are declared with type 3 (`xtRegisterTypeUserReg`). Field order per the `XTREG` macro in `target/xtensa/overlay_tool.h`.

[^26]: espressif/qemu issue #154, "ESP32-S3: qemu-system-xtensa boots with CPENABLE = 0; first FP op recurses through the Cp0Disabled handler (QEMU-293)", opened 2026-05-28, open as of 2026-09-06. The reproducer is a Rust `no_std` binary run from a flash image. Pull request #155 on the same repository (open as of 2026-09-06) carries the silicon measurement and the `cpu.c` root cause.
