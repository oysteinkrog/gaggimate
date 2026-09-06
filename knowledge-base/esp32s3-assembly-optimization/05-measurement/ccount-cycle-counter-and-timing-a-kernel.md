---
title: CCOUNT and timing a kernel on the ESP32-S3
id: 05-measurement/ccount-cycle-counter-and-timing-a-kernel
schema_version: 1
doc_type: how-to
status: review
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, measurement, ccount, esp-timer, benchmarking]
confidence: medium
---

# CCOUNT and timing a kernel on the ESP32-S3

How to put a cycle number on a compute kernel so the number is the chip's
and not an artifact of the harness. Scope: short deterministic compute on
one core, the case where you compare two versions of the same loop.
Wall-clock timing of a whole subsystem is a different job, and
`esp_timer_get_time` is the tool for it.

## The counter

The Xtensa Timer Interrupt Option adds a 32-bit free-running cycle
counter, `CCOUNT`, as special register 234, plus the `CCOMPAREn`
registers that raise the timer interrupts.[^isa] The ESP32-S3 core
configuration enables it with three `CCOMPAREn` registers
(`XCHAL_HAVE_CCOUNT` is 1, `XCHAL_NUM_TIMERS` is 3),[^coreisa] and the
same generated config puts `CCOUNT` at 234.[^specreg] It increments once
per CPU clock cycle: "Each CPU core maintains an internal counter (i.e.,
cycle count) that increments every CPU clock cycle."[^espcpu]

Four ways to read it, all the same one instruction underneath:

| Accessor | Where | Notes |
|---|---|---|
| `rsr.ccount %0` | inline asm | what the others expand to |
| `XTHAL_GET_CCOUNT()` | `xtensa/core-macros.h` | macro over `__asm__ __volatile__("rsr.ccount %0")`[^macros] |
| `xthal_get_ccount()` | `xtensa/hal.h` | out-of-line function, returns 0 if the option is absent[^hal] |
| `esp_cpu_get_cycle_count()` | `esp_hw_support/include/esp_cpu.h` | portable across Xtensa and RISC-V, forwards to `xt_utils_get_cycle_count()`, which is `RSR(CCOUNT, ccount)`[^espcpu][^xtutils] |

Prefer `esp_cpu_get_cycle_count()`. It is the supported name, it is
`FORCE_INLINE_ATTR`, and it costs the same instruction.

### It wraps, so subtract unsigned

The counter is 32 bits. At 240 MHz it wraps every 2^32 / 240e6 seconds,
about 17.9 s. ESP-IDF's Kconfig gives the same figure for run-time stats
built on the CPU clock: "If the CPU clock consistently runs at the
maximum frequency of 240MHz, it will overflow in approximately 17
seconds."[^kconfig]

Store both endpoints in `uint32_t` and subtract in `uint32_t`. Unsigned
wraparound is defined in C, so `end - start` is correct across exactly
one wrap and wrong across two. Keep one timed interval well under 17.9 s.
If it could be longer, it is not a kernel measurement; use
`esp_timer_get_time`.

### It is per core

Each core has its own `CCOUNT`. The ESP-IDF speed guide draws the
consequence: "The CPU cycles are counted per-core, so only use this
method from an interrupt handler, or a task that is pinned to a single
core."[^speed] The two counters are not aligned with each other, and
nothing in ESP-IDF synchronises them after start-up. `[uncertain]` No
primary source states an alignment guarantee either way; ESP-IDF ships a
test that prints both cores' counters through an inter-processor call,
which reads as an acknowledgement that they can differ.[^ipctest] A start
read on one core and an end read on the other is meaningless.

### It counts core clocks, not time

Cycles are the honest unit for a compute kernel, but converting to
seconds depends on the CPU frequency at that moment. Three things move
it.

- **Dynamic frequency scaling.** With `CONFIG_PM_ENABLE` and a call to
  `esp_pm_configure()`, the CPU runs between `min_freq_mhz` and
  `max_freq_mhz` depending on which power management locks are
  held.[^pmhdr][^pmdoc] ESP-IDF's Kconfig puts the range plainly: the CPU
  clock "can fluctuate between 80 to 240MHz", and run-time stats built on
  it "DOES NOT reflect the amount of time each task runs for (as CPU
  clock frequency can change)".[^kconfig]
- **Automatic light sleep**, built on FreeRTOS tickless idle:
  `esp_pm_configure()` returns `ESP_ERR_NOT_SUPPORTED` if
  `light_sleep_enable` is set without
  `CONFIG_FREERTOS_USE_TICKLESS_IDLE`.[^pmdoc] The core stops, so the
  counter stops with it.
- **Start-up frequency changes rescale the counter.** After setting the
  CPU frequency, ESP-IDF writes the counter back scaled, commented
  "Re-calculate the ccount to make time calculation correct".[^rtcclk]
  The same rescale is in the system clock init path.[^clk] So `CCOUNT` is
  not strictly a count of cycles executed since reset. It can jump.

The runtime scaling switch does not rescale it: `esp_pm` calls
`on_freq_update`, which recomputes the tick divisor and the `CCOMPARE`
target and leaves `CCOUNT` alone.[^pmimpl] That is right for a cycle
counter, and it is why a cycles-to-microseconds conversion taken across a
frequency switch is wrong.

**The practical rule.** Hold the CPU at a fixed frequency: build with
power management off, or take an `ESP_PM_CPU_FREQ_MAX` lock for the
duration. Then report cycles, and report the clock you ran at.

## The wall-clock alternative

`esp_timer_get_time()` returns microseconds since boot as
`int64_t`.[^esptimerhdr] Its documented resolution is one
microsecond.[^esptimerdoc] On the ESP32-S3 it is backed by the systimer,
whose clock source is fixed to the 40 MHz crystal with a fixed 2.5
divider, so the hardware ticks at 16 MHz and ESP-IDF converts with
`ticks / 16`.[^systimer] That is a 62.5 ns hardware tick truncated to a
1 us API. It does not move with the CPU frequency, so it is the correct
clock across a frequency switch or a sleep, but its granularity is about
240 CPU cycles at 240 MHz. Use it for a whole frame or a whole batch,
never for one iteration.

| | `CCOUNT` | `esp_timer_get_time` |
|---|---|---|
| Unit | core clock cycles | microseconds |
| Resolution | 1 cycle | 1 us, about 240 cycles at 240 MHz |
| Width | 32 bits, wraps in about 17.9 s at 240 MHz | 64 bits |
| Per core | yes | no, one system clock |
| Moves with CPU frequency | yes | no |
| Read cost | one instruction | a function call and a peripheral read |

## What perturbs a reading

**Interrupts on this core.** Any interrupt taken between the two reads is
counted as part of your kernel. This is the dominant error source and the
reason min-of-n works.

**The other core.** It does not consume this core's issue slots; the two
LX7 cores are separate pipelines. It does contend for shared memory, so a
kernel whose operands live in PSRAM or in flash-cached `.rodata` can
measure slower while the other core is busy on the same bus.
`[uncertain]` The exact cache and bus arbitration is a memory-hierarchy
question; see
[caches, SRAM, PSRAM and the MSPI bus](../03-memory-hierarchy/caches-sram-psram-and-the-mspi-bus.md).
Measure under the neighbouring load
you expect in production, and say what that load was.

**Preemption.** A context switch inside the interval adds another task's
time to your number.

Two ways to deal with all three.

1. **Disable interrupts around a short kernel.** `portDISABLE_INTERRUPTS()`
   expands to `XTOS_SET_INTLEVEL(XCHAL_EXCM_LEVEL)`.[^portmacro] On the
   ESP32-S3 `XCHAL_EXCM_LEVEL` is 3 out of 6 interrupt levels, so this
   masks levels 1 to 3 and leaves levels 4 and above able to
   fire.[^coreisa] It is a large reduction, not a guarantee. Use
   `portENTER_CRITICAL(mux)` with your own spinlock when you also need
   mutual exclusion on an SMP build.[^portmacro] Keep the window short:
   blocking interrupts for a long kernel breaks whatever else the system
   was doing, including the watchdog.
2. **Run many repetitions and take the minimum.** The better default.
   Nothing an interrupt or a context switch does makes the kernel finish
   sooner, so the minimum of n runs is the run that was least disturbed.

**When the minimum is right, and when it is not.** For deterministic
compute, where every iteration does the same arithmetic on resident data,
the true cost is a fixed number and every deviation above it is noise.
The minimum estimates that number and converges fast. For code dominated
by cache misses, the misses are part of the work, not noise. The minimum
there reports the one iteration that found everything cached, a number
the kernel will never deliver. For that code report the mean and the
spread, and say which you reported.

**Warm versus cold cache.** The first run pays instruction cache misses
on its own code and data cache misses on its tables and inputs. Later
runs do not. Both numbers are real: the cold one is what a kernel called
once per frame sees, the warm one is what a kernel called in a tight loop
sees. Report the first run and the minimum of the rest, labelled.

**Where the operands live.** A gather from a table in internal SRAM, in
flash-cached `.rodata`, and in PSRAM are three different numbers. Measure
with the placement the production build uses, and state it in the result.
A bench that copies a table into SRAM to make the measurement clean
measures a kernel nobody ships.

**Pin the measuring task, and give it a priority.** Pin to one core so
the counter means something, and run above whatever else shares that
core, or the minimum will chase a quiet window that never comes.

**The measurement costs something too.** `rsr.ccount` is one instruction
and the accessors above inline to it. `[uncertain]` No primary source
gives its issue latency on the LX7, so do not assume a number: measure
the empty interval on your own build and subtract it. Wrapping the kernel
in a non-inlined call adds the call, the return, and possibly a register
window rotation, so measure the same call shape you ship.

## Cycles per element, and the instruction count

Divide measured cycles by the elements produced. That is the number to
compare across versions and against a budget. Read it next to the static
instruction count of the inner loop from the disassembly. Cycles per
element close to the instruction count means the loop is issue-bound, and
the way forward is fewer instructions. Cycles well above it means the
loop is stalling, and fewer instructions will not help; look at load-use
distance, alignment, and where the operands live. An instruction count is
never a timing: a cycle claim comes from a device or emulator
measurement, or from a documented instruction latency, and says which.

## Pitfalls that quietly produce a wrong number

**The compiler deletes the kernel.** At `-O2` a result nobody reads is
dead code. Sink it with an empty asm that claims to consume the value:
`static inline void sink(uint32_t v) { __asm__ volatile("" :: "r"(v)); }`.
Prefer that over a `volatile` variable: a `volatile` store inside the
loop adds a real store to every iteration and changes what you measure.

**The compiler hoists the kernel out of the repetition loop.** If the
kernel is pure and its inputs do not change, GCC may run it once. Defeat
this by making the input depend on the iteration, or with a compiler
barrier, `__asm__ volatile("" ::: "memory");`, at the top of each
repetition. Check the disassembly of the harness, not just of the kernel.
If the repetition loop body is empty, you measured an empty loop.

**Measuring through a debugger.** With OpenOCD attached and the GDB stub
active, breakpoints, single stepping and stub entry all run code on the
core and land inside your interval. `[uncertain]` The size of the effect
is not documented in a source I could cite; it is not zero. Take timing
runs with the debugger detached.

**The console interrupts you.** A `printf` inside the timed region, or a
UART transmit finishing during it, costs an interrupt and possibly a
blocking wait on the driver. Collect results into an array and print
after the last read. The same applies to JTAG-based logging.

## A harness

Call it from a task pinned to one core, at a priority above its
neighbours.

```c
#include <stdint.h>
#include "esp_cpu.h"

static inline void sink(uint32_t v) { __asm__ volatile("" :: "r"(v)); }

/* min of n; *first_out gets the cold run, *ov_out the read-pair floor. */
static uint32_t bench(void (*kernel)(void *ctx), void *ctx,
                      int n, uint32_t *first_out, uint32_t *ov_out)
{
    uint32_t ov = UINT32_MAX;              /* calibrate: same read pair */
    for (int i = 0; i < 64; i++) {
        uint32_t a = esp_cpu_get_cycle_count();
        __asm__ volatile("" ::: "memory");
        uint32_t b = esp_cpu_get_cycle_count();
        uint32_t d = b - a;                /* unsigned: correct across wrap */
        if (d < ov) ov = d;
    }
    uint32_t best = UINT32_MAX, first = 0;
    for (int i = 0; i < n; i++) {
        __asm__ volatile("" ::: "memory"); /* stop hoisting out of this loop */
        uint32_t t0 = esp_cpu_get_cycle_count();
        kernel(ctx);
        uint32_t t1 = esp_cpu_get_cycle_count();
        uint32_t d = t1 - t0;
        if (i == 0) first = d;
        if (d < best) best = d;
    }
    sink(*(volatile uint32_t *)ctx);       /* the kernel's output is used */
    *first_out = first;
    *ov_out = ov;
    return best;
}
```

The reads and the subtraction are `uint32_t`. The calibration loop uses
the same read pair, so the number you subtract matches the number you
measured. There is no `printf` inside. For the interrupt-masked variant,
put `portDISABLE_INTERRUPTS()` and its matching restore immediately
around the `t0`/kernel/`t1` triple, and only for a kernel short enough to
justify it.

## What a result must state

A cycle number without its conditions is not reproducible. Report all of
this, every time:

- min of n and the value of n, the first (cold) run alongside it, and the
  mean and spread as well if the kernel is cache-miss bound
- where the operands and tables live (internal SRAM, flash-cached
  `.rodata`, PSRAM) and where the code lives (IRAM or flash-cached)
- compiler flags, toolchain version, and cache configuration (instruction
  and data cache size and line size)
- CPU frequency, and whether power management was on or a frequency lock
  was held
- the core the task was pinned to, its priority, what else ran on that
  core, and whether interrupts were masked

Two numbers taken under different entries in that list are not
comparable. The most common way a kernel regression turns out to be
nothing is that one of them changed.

## Footnotes

ESP-IDF paths are relative to an ESP-IDF 5.5.1 checkout (`version.txt`).

[^isa]: Cadence/Tensilica, *Xtensa Instruction Set Architecture (ISA) Reference Manual*, issue 4/2010 (RC-2010.1), Section 4.4.6 "Timer Interrupt Option" (p. 110), Table 5-175 "CCOUNT - Special Register #234" (p. 232) and Table 5-176 "CCOMPARE0..2 - Special Register #240-242" (p. 233): "The CCOUNT register increments on every processor-clock cycle."
[^coreisa]: `components/xtensa/esp32s3/include/xtensa/config/core-isa.h`: `XCHAL_HAVE_CCOUNT 1 /* CCOUNT reg. (timer option) */` (354), `XCHAL_NUM_TIMERS 3` (355), `XCHAL_NUM_INTLEVELS 6` (359), `XCHAL_EXCM_LEVEL 3` (361).
[^specreg]: `components/xtensa/include/xtensa/specreg.h`: `#define CCOUNT 234` (100), `CCOMPARE_0` 240 to `CCOMPARE_2` 242.
[^espcpu]: `components/esp_hw_support/include/esp_cpu.h`, `esp_cpu_get_cycle_count()` (181) and its doc comment: "Each CPU core maintains an internal counter (i.e., cycle count) that increments every CPU clock cycle."
[^macros]: `components/xtensa/include/xtensa/core-macros.h` (346): `XTHAL_GET_CCOUNT()` expands to `__asm__ __volatile__("rsr.ccount %0" : "=a"(__ccount))`.
[^hal]: `components/xtensa/include/xtensa/hal.h`, Core Counter section: `extern unsigned xthal_get_ccount(void);`, "get CCOUNT register (if not present return 0)".
[^xtutils]: `components/xtensa/include/xt_utils.h` (68): `xt_utils_get_cycle_count()`, body `RSR(CCOUNT, ccount)`.
[^kconfig]: `components/freertos/Kconfig` (549 to 556), help text for the CPU-clock run-time-stats option.
[^speed]: Espressif, *ESP-IDF Programming Guide* v5.5.1, ESP32-S3, "Speed Optimization", section on measuring performance. https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-guides/performance/speed.html
[^ipctest]: `components/esp_system/test_apps/esp_system_unity_tests/main/test_ipc_isr.c` (60 to 66), the test that prints CPU0 and CPU1 cycle counts separately.
[^pmhdr]: `components/esp_pm/include/esp_pm.h`: `esp_pm_config_t` with `max_freq_mhz`, `min_freq_mhz`, `light_sleep_enable`; `esp_pm_configure()`; `ESP_PM_CPU_FREQ_MAX`.
[^pmdoc]: Espressif, *ESP-IDF Programming Guide* v5.5.1, ESP32-S3, "Power Management". https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/system/power_management.html
[^rtcclk]: `components/esp_hw_support/port/esp32s3/rtc_clk_init.c` (62 to 63): `esp_cpu_set_cycle_count((uint64_t)esp_cpu_get_cycle_count() * cfg.cpu_freq_mhz / freq_before);`.
[^clk]: `components/esp_system/port/soc/esp32s3/clk.c` (137 to 138), the same rescale on a frequency change.
[^pmimpl]: `components/esp_pm/pm_impl.c`: the frequency switch (668 to 680) calls `rtc_clk_cpu_freq_set_config_fast()` in a critical section then `on_freq_update()`; `on_freq_update()` (573) recomputes `_xt_tick_divisor` and the `CCOMPARE` target and does not write `CCOUNT`.
[^esptimerhdr]: `components/esp_timer/include/esp_timer.h` (223): `int64_t esp_timer_get_time(void);`, "Get time in microseconds since boot".
[^esptimerdoc]: Espressif, *ESP-IDF Programming Guide* v5.5.1, ESP32-S3, "ESP Timer": "The time resolution: one microsecond." https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/system/esp_timer.html
[^systimer]: `components/esp_hw_support/port/esp32s3/systimer.c` (9 to 22): "systimer's clock source is fixed to XTAL (40MHz), and has a fixed fractional divider (2.5). So the resolution of the systimer is 40MHz/2.5 = 16MHz." `systimer_ticks_to_us()` returns `ticks / 16`.
[^portmacro]: `components/freertos/FreeRTOS-Kernel/portable/xtensa/include/freertos/portmacro.h`: `portDISABLE_INTERRUPTS()` expands to `XTOS_SET_INTLEVEL(XCHAL_EXCM_LEVEL)` (418); `portENTER_CRITICAL(mux)` (451, 455).
