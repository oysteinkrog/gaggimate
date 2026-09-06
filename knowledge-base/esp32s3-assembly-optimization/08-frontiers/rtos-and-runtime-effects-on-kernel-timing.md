---
title: What the runtime does to a kernel's measured time, and how that changes across ESP-IDF FreeRTOS, Zephyr, NuttX and bare metal
id: 08-frontiers/rtos-and-runtime-effects-on-kernel-timing
schema_version: 1
doc_type: explanation
status: draft
last_reviewed: 2026-09-06
tags: [esp32s3, xtensa, freertos, zephyr, nuttx, bare-metal, interrupts, coprocessor, scheduling, measurement]
confidence: medium
---

# What the runtime does to a kernel's measured time, and how that changes across runtimes

A hot loop's cycle count is a property of the instructions and the
memory they touch. Everything else that can land inside a measured
interval, a tick interrupt, a coprocessor exception, a flash write that
freezes the core, comes from the runtime around the loop, not from the
loop. This leaf catalogues those sources for ESP-IDF's FreeRTOS on the
ESP32-S3, then asks which still apply under Zephyr, under NuttX, and
with no RTOS at all. Read
[CCOUNT and timing a kernel](../05-measurement/ccount-cycle-counter-and-timing-a-kernel.md)
first for the measurement method whose perturbations this leaf explains,
and
[coprocessors, CPENABLE, and lazy context switching](../00-foundations/coprocessors-cpenable-and-lazy-context.md)
first for the coprocessor mechanism this leaf only adds a timing angle
to. Neither is repeated here.

## The tick, and how often it can visit

FreeRTOS on ESP-IDF ticks at `configTICK_RATE_HZ`, the Kconfig option
`FREERTOS_HZ`, range 1 to 1000, default 100[^1]. Two timer sources exist
on Xtensa targets, chosen by `FREERTOS_CORETIMER`: a dedicated hardware
timer read through `CCOUNT` comparators (`Timer 0`, level 1, or `Timer
1`, level 3), or the SYSTIMER peripheral at level 1 or 3, the default
for the ESP32-S3 build this KB targets, at level 1[^2]. On a dual-core
build each core gets its own alarm on the same counter, staggered by
half a tick period so the cores do not both take the tick interrupt at
once[^3]. A tick here is therefore a level-1 interrupt, up to 100 times
a second by default, at a different offset on each core. Whichever
source is picked, the handler re-arms the next compare value from the
old one rather than from service time, to avoid clock drift; if a tick
is ever serviced more than one period late, the handler loops and
processes the backlog before returning[^4][^5]. So a kernel invocation
interrupted by a very late tick can, in principle, absorb more than one
tick's worth of handler work in a single measured interval.

## Two separate axes: FreeRTOS task priority and Xtensa interrupt level

A kernel timed with `CCOUNT` deltas is exposed to two independent
scheduling mechanisms, and conflating them is a common source of a
"the priority-1 task never gets interrupted" assumption that is false
on this chip.

**FreeRTOS task priority** decides which *task* the scheduler resumes
after a context switch. It says nothing about interrupts: it is a
software queue-ordering rule the tick handler and other kernel calls
consult, not a hardware gate.

**The Xtensa interrupt level** (`PS.INTLEVEL`, part of special register
230) decides which *interrupts* the core will currently take, and it
preempts running code regardless of task priority. `esp_intr_alloc()`'s
level flags run `ESP_INTR_FLAG_LEVEL1` through `ESP_INTR_FLAG_LEVEL6`
plus `ESP_INTR_FLAG_NMI` (level 7, highest)[^6]. Levels 1 through 3 are
`ESP_INTR_FLAG_LOWMED`, "can be handled in C"; levels 4 through 6 and NMI
are `ESP_INTR_FLAG_HIGH`, "need to be handled in assembly"[^6]. That
split is not style: `portDISABLE_INTERRUPTS()` masks only up to
`XCHAL_EXCM_LEVEL`, 3 on this core[^7][^ccount-portmacro], so a
level-4-or-above interrupt fires even inside a FreeRTOS critical
section, with `PS.EXCM` still clear; the assembly-only rule exists
because window overflow and coprocessor exception handling both assume
the interrupted context can safely take further exceptions, which is not
guaranteed above `XCHAL_EXCM_LEVEL`.

Consequence for measurement: a task pinned to a core at the highest
FreeRTOS priority is not thereby immune to interrupts. It is still
preempted by every level-1-to-3 source on that core (the tick, UART,
GPIO, timers, anything not explicitly masked) and by any level-4+
handler regardless of `portDISABLE_INTERRUPTS()`. Masking interrupts
around a measured interval, as the CCOUNT leaf describes, only ever
reaches levels 1 to 3.

## The window overflow and underflow handlers are on every measured call

Every windowed `CALL4`/`CALL8`/`CALL12` in the kernel or its call chain
can, on first use of a stale register group, take a Window Overflow
exception, and every `RETW` can take a Window Underflow one. These are
not FreeRTOS constructs: they are Xtensa exceptions, handled by
depth-specific routines (`_WindowOverflow4`, `_WindowUnderflow4`, and
the 8- and 12-deep siblings) ESP-IDF ships transcribed from the
architecture[^8]. They run whether or not an RTOS is present, on any
windowed-ABI binary; see
[register windows and the windowed ABI](../00-foundations/register-windows-and-windowed-abi.md)
for the mechanism, which flags the per-exception cost `[uncertain]`,
unpublished in either primary source checked. The point for this leaf is
scheduling, not cost: being exceptions rather than interrupts gated by
`PS.INTLEVEL`, they are not something `portDISABLE_INTERRUPTS()` defers,
and a kernel built `-mabi=call0` removes them entirely by removing the
windows, which is CALL0's actual timing argument, separate from its
code-size one.

## The coprocessor-disabled exception's timing shape

The coprocessor leaf covers the ownership mechanism in full. From a
timing angle: the first PIE (`EE.*`) or floating-point instruction a
task executes after being scheduled onto a core where a *different*
task owns that coprocessor takes the Coprocessor `n` Disabled exception
once, and that exception's handler is where the 208-byte (PIE) or
72-byte (FPU) save and restore actually happen[^coproc-save]. Every
later coprocessor instruction from the same task, until it is
descheduled and something else claims the unit, executes with no
exception at all. A kernel using PIE pays this cost at most once per
contended handoff, not once per call, so a benchmark loop calling the
same kernel back to back on an otherwise-idle core will not see it after
the first iteration, exactly what `min-of-n` is built to discard. A
benchmark that alternates between two PIE-using tasks, or shares a core
with anything else touching the FPU or PIE between calls, pays it on
every call instead, a worse regime than a dedicated benchmark task
normally measures.

## Critical sections cost more on SMP, and disabling interrupts is not exclusion

`portDISABLE_INTERRUPTS()` alone stops this core from taking levels 1 to
3. It says nothing about the other core, which can still execute and
touch the same memory. `portENTER_CRITICAL(mux)` on the SMP port
additionally takes a spinlock, so two cores contending for the same
section serialize through that spinlock on top of each masking its own
interrupts[^ccount-portmacro]. A kernel benchmark that wraps its timed
interval in `portENTER_CRITICAL` to keep the number clean is not paying
only the interrupt-masking cost; it is also exposed to however long the
other core is already inside the same section, the cross-core
contention the CCOUNT leaf flags `[uncertain]` and defers to the
memory-hierarchy bucket.

## Flash cache disable: the one that stops the *other* core too

The most severe FreeRTOS-adjacent perturbation here is not an interrupt
at all. Any NVS commit, or any other call into the SPI flash driver that
erases or writes, calls
`spi_flash_disable_interrupts_caches_and_other_cpu()`[^flash1]. On a
dual-core build this does not merely mask interrupts on its own core: it
suspends the scheduler, sends the *other* core an inter-processor call
that spins it in a busy loop with its own non-IRAM interrupts and
scheduler disabled, disables non-IRAM interrupt sources on both cores,
and only then disables the instruction and data caches[^flash1][^flash2].
While the caches are down, any code or data not resident in internal
SRAM (anything cached out of flash or PSRAM) is unreachable: a fetch or
load into that range from either core hangs until the flash operation
finishes and cache is restored[^flash2]. A kernel or benchmark harness
in flash-cached code, with a table in flash-cached `.rodata`, running on
the *idle* core while the *busy* core commits to NVS, does not run
slower during that window. It does not run at all, for as long as the
write takes. A benchmark loop that straddles a background NVS or OTA
write shows one outlier sample far larger than every other; min-of-n
discards it, but knowing the mechanism is what tells you not to blame
the kernel for that one sample.

## Bare metal: none of the above is generated for you

A bare-metal or freestanding harness, the kind
[QEMU for the ESP32-S3](../05-measurement/qemu-esp32s3-what-it-proves-and-what-it-cannot.md)'s
minimal-harness section describes, has no tick interrupt unless the code
sets one up, no scheduler and so no task-priority axis at all, and no
coprocessor-disabled *handler* unless startup installs one. That cuts
both ways: without a handler, the first PIE or FPU instruction after
reset, with `CPENABLE` still zero, takes an exception with nowhere to
go, which is exactly that QEMU leaf's `[uncertain]` issue #154, a
freestanding image hanging on this[^qemu26]. The convention that leaf
gives, set `CPENABLE` once at startup and never again, sidesteps the
exception rather than handling it: a bare-metal benchmark pays no
coprocessor-disabled cost because it never takes the exception, not
because the cost is hidden elsewhere. Window overflow and underflow
exceptions still happen for windowed-ABI code; they are an ISA property
of `CALLn`/`RETW`, not an RTOS service, so avoiding them entirely still
means building CALL0. What genuinely disappears with no RTOS is the
tick, task-switch overhead, and any background flash write the
application does not choose to perform: a bare-metal timing floor is
closer to "instructions and memory only" than any RTOS build reaches,
which is also why it is the wrong place to validate interrupt
coexistence.

## Zephyr on the ESP32-S3: AMP, not SMP, and PIE is not a modeled coprocessor

Espressif's own Zephyr support-status page and the Zephyr project's
ESP32-S3 SoC-features page both describe the dual-core ESP32-S3 as
supported through an **Asymmetric Multiprocessing** model built on
OpenAMP: each core runs its own independent Zephyr image, optionally
exchanging data over OpenAMP, with the documented limitation that
Zephyr-managed serial output (`printk()`, logging, the console UART) is
not implemented for code running on the app CPU[^z-soc]. That is a
materially different shape from ESP-IDF's single FreeRTOS SMP scheduler
spanning both cores: there is no cross-core task migration to reason
about, because there is no single scheduler; each core's Zephyr instance
has its own tick, tasks, and critical sections, separate from the
other's. `[uncertain]` No citable statement was found of whether the two
images can cooperate more tightly than message passing; treat
single-core-per-image AMP as the documented baseline.

Symmetric multiprocessing proper is narrower still. A 2026 Zephyr issue
reports SMP broken even on the original ESP32, where it had previously
worked, and the reporter asks whether SMP could instead be extended to
the ESP32-S3, implying it is not there as of that report[^z-smp].
Fetched 2026-09-06, no maintainer response confirming or denying that
request was visible. Treat ESP32-S3 SMP under Zephyr as unsupported as
of this writing, not merely undocumented.

Zephyr's Xtensa architecture layer documents a lazy-versus-eager
coprocessor sharing scheme, a close structural cousin of ESP-IDF's: the
default eager mode saves and restores every thread's registers on every
switch regardless of use; the optional lazy mode tracks a per-coprocessor
owner and defers the save until a different thread's use forces it, with
an SMP handoff to a still-live remote owner settled by an inter-processor
interrupt that triggers the save remotely[^z-xtensa]. But every place
that page names a coprocessor, it names Cadence's HiFi audio DSP
extension, the Tensilica/Cadence configuration Zephyr's Xtensa port was
built against; CP3, PIE, `q` registers and Espressif do not appear on it
anywhere[^z-xtensa]. The Zephyr ESP32-S3 SoC-features page separately
lists "additional vector instructions support for AI acceleration" as a
chip feature[^z-soc], which reads as datasheet capability copy, not a
statement that Zephyr's coprocessor-sharing code or toolchain recognizes
CP3 the way ESP-IDF's port does. `[uncertain]` Whether Zephyr on the
ESP32-S3 saves or restores PIE state at all is not confirmed by any
source found here; a kernel using `EE.*` instructions under Zephyr
should be verified against Zephyr's own coprocessor list first.

## NuttX on the ESP32-S3: SMP is real, and the coprocessor scheme is the same idea under a different name

NuttX documents actual SMP on this chip, not AMP: its ESP32-S3 platform
page states plainly that "SMP is enabled to enhance Wi-Fi performance,"
a single kernel image spanning both cores in the same sense ESP-IDF's
FreeRTOS SMP build does, and its Xtensa architecture layer carries an
`ARCH_HAVE_MULTICPU` selection for both the ESP32 and the
ESP32-S3[^nuttx-soc][^nuttx-kconfig]. This is the frontier's one clean
match to ESP-IDF's scheduling shape: two cores, one scheduler, cross-core
task migration a real possibility, so the "an interrupt level, not a
task priority, is what actually preempts you" reasoning earlier in this
leaf applies without translation.

NuttX's coprocessor handling names the same mechanism as ESP-IDF, under
a different name and as an explicit option rather than the only mode.
Its Xtensa Kconfig option `XTENSA_CP_LAZY` describes itself: "CPENABLE
is set to zero on each context switch, disabling all co-processors[;]
if/when the task attempts to use the disabled co-processor, an
exception occurs[;] the co-processor exception handler re-enables the
co-processor"[^nuttx-lazy]. That is the same disabled-exception,
lazy-owner scheme ESP-IDF uses, offered as an alternative to a standard
mode that instead saves and restores `CPENABLE` and coprocessor state
unconditionally on every switch, whether or not either task touched a
coprocessor[^nuttx-lazy]. Unlike ESP-IDF, where lazy save is the only
implemented path, NuttX exposes the choice per build: a kernel using PIE
under NuttX should confirm which mode a given board selects, since the
always-save mode turns "cost paid once per contended handoff" into
"cost paid on every switch of every task, whether it touches PIE or not."

## What carries across runtimes, and what does not

| Effect | ESP-IDF FreeRTOS (this KB's target) | Zephyr (S3) | NuttX (S3) | Bare metal |
|---|---|---|---|---|
| Task-priority preemption | Yes, one scheduler across both cores | Yes, but per-core: no cross-core task migration under AMP | Yes, one scheduler across both cores (SMP) | No scheduler; not applicable |
| Interrupt level preempts regardless of task priority | Yes, levels 1-6 plus NMI, `[uncertain]` above `XCHAL_EXCM_LEVEL`=3 for masking | Presumed yes (same core, same ISA); not separately confirmed here | Presumed yes (same ISA); not separately confirmed here | Yes, for whatever the harness itself enables |
| Periodic tick interrupt in every measured interval | Yes, default 100 Hz, level 1, staggered per core[^1][^2][^3] | Per-core tick under its own Zephyr image; rate not checked here | Per-core tick under SMP; rate not checked here | None unless the harness installs one |
| Window overflow/underflow exceptions | Yes, ISA property, RTOS-independent[^8] | Yes, same ISA | Yes, same ISA | Yes, unless built CALL0 |
| Lazy coprocessor save/restore on contended handoff | Yes, the only mode ESP-IDF implements[^coproc-save] | `[uncertain]`, PIE not confirmed as a modeled coprocessor at all[^z-xtensa] | Yes if `XTENSA_CP_LAZY` selected; an always-save mode also exists[^nuttx-lazy] | Only if the harness writes its own handler; the reference harness instead sets `CPENABLE` once and never triggers the exception |
| Flash-cache-down freeze reaching the *other* core | Yes, by design, during any flash write[^flash1][^flash2] | `[uncertain]`, not checked against Zephyr's own flash driver here | `[uncertain]`, not checked against NuttX's own flash driver here | Not applicable without a flash filesystem |

## Open questions

- Whether Zephyr's Xtensa port saves or restores CP3 (PIE) state on the
  ESP32-S3 at all is unconfirmed; the only coprocessor its architecture
  documentation names is Cadence's HiFi DSP extension. Settling this
  needs the Zephyr ESP32-S3 SoC layer source itself, not the architecture
  guide.
- Whether NuttX's flash driver on the ESP32-S3 disables caches and
  stalls the other core the way ESP-IDF's does was not checked against
  NuttX source; likely given the shared MSPI hardware, but an inference,
  not a citation.
- The per-exception cost, in cycles, of a window overflow/underflow
  handler and of a coprocessor-disabled handler is `[uncertain]` in the
  leaves that introduce each mechanism; a device measurement (trigger
  one in isolation under `CCOUNT`, subtract the calibrated empty-interval
  floor) would settle both, and belongs in `05-measurement/` if generic
  or `07-our-work/` if tied to this repo's rig.
- Whether Zephyr's per-core AMP tick rate and NuttX's SMP tick rate
  default to the same order of magnitude as ESP-IDF's 100 Hz was not
  checked; a benchmark ported across runtimes should not assume the tick
  frequency carries over.

## Sources

[^1]: ESP-IDF 5.5.1, `components/freertos/Kconfig`, `config FREERTOS_HZ`: `range 1 1000`, `default 100`, help text "Sets the FreeRTOS tick interrupt frequency in Hz".
[^2]: ESP-IDF 5.5.1, `components/freertos/Kconfig`, `choice FREERTOS_CORETIMER`: options `FREERTOS_CORETIMER_0` ("Timer 0 (int 6, level 1)"), `FREERTOS_CORETIMER_1` ("Timer 1 (int 15, level 3)"), `FREERTOS_CORETIMER_SYSTIMER_LVL1` ("SYSTIMER 0 (level 1)"), `FREERTOS_CORETIMER_SYSTIMER_LVL3` ("SYSTIMER 0 (level 3)"); default is `FREERTOS_CORETIMER_SYSTIMER_LVL1` when systimer tick support is available. `FREERTOS_SYSTICK_USES_SYSTIMER` and `FREERTOS_SYSTICK_USES_CCOUNT` are the two resulting booleans.
[^3]: ESP-IDF 5.5.1, `components/freertos/port_systick.c`, `vSystimerSetup()`: per-core alarm IDs `SYSTIMER_ALARM_OS_TICK_CORE0 + cpuid`, each configured with period `1000000UL / CONFIG_FREERTOS_HZ`, and the comment "SysTick of core 0 and core 1 are shifted by half of period" above the `systimer_hal_counter_value_advance(..., 1000000UL / CONFIG_FREERTOS_HZ / 2)` call gated on `!CONFIG_FREERTOS_UNICORE`.
[^4]: ESP-IDF 5.5.1, `components/freertos/FreeRTOS-Kernel-SMP/portable/xtensa/portasm.S`, `_frxt_timer_int` header comment: "To avoid clock drift due to interrupt latency, the new cycle count is computed from the old, not the time the interrupt was serviced. However if a timer interrupt is ever serviced more than one tick late, it is necessary to process multiple ticks", and the `.L_xt_timer_int_catchup` loop implementing it (`CONFIG_FREERTOS_SYSTICK_USES_CCOUNT` path).
[^5]: ESP-IDF 5.5.1, `components/freertos/port_systick.c`, `SysTickIsrHandler()`: the `diff > 0` check against `s_handled_systicks[cpuid]` and the `do { xPortSysTickHandler(); } while (--diff);` catch-up loop (`CONFIG_FREERTOS_SYSTICK_USES_SYSTIMER` path).
[^6]: ESP-IDF 5.5.1, `components/esp_hw_support/include/esp_intr_alloc.h`: `ESP_INTR_FLAG_LEVEL1` ("lowest priority") through `ESP_INTR_FLAG_LEVEL6` and `ESP_INTR_FLAG_NMI` ("Level 7 interrupt vector (highest priority)"); `ESP_INTR_FLAG_LOWMED` comment "Low and medium prio interrupts. These can be handled in C."; `ESP_INTR_FLAG_HIGH` comment "High level interrupts. Need to be handled in assembly."
[^7]: ESP-IDF 5.5.1, `components/xtensa/esp32s3/include/xtensa/config/core-isa.h`: `XCHAL_EXCM_LEVEL 3` ("level masked by PS.EXCM").
[^ccount-portmacro]: ESP-IDF 5.5.1, `components/freertos/FreeRTOS-Kernel/portable/xtensa/include/freertos/portmacro.h`: `portDISABLE_INTERRUPTS()` expands to `XTOS_SET_INTLEVEL(XCHAL_EXCM_LEVEL)`; `portENTER_CRITICAL(mux)` additionally takes a spinlock on the SMP port. Cross-referenced from [CCOUNT and timing a kernel](../05-measurement/ccount-cycle-counter-and-timing-a-kernel.md).
[^8]: ESP-IDF 5.5.1, `components/xtensa/xtensa_vectors.S`: `.section .WindowVectors.text`, `_WindowOverflow4` at offset `0x0` and `_WindowUnderflow4` at offset `0x40`, transcribing the Window Overflow/Underflow Exception for Call4 sequence (`s32e`/`l32e` of `a0`-`a3` relative to `a5`, `rfwo`/`rfwu`), with matching depth-8 and depth-12 handlers following in the same file. See [register windows and the windowed ABI](../00-foundations/register-windows-and-windowed-abi.md) for the full mechanism and its `[uncertain]` per-exception cost.
[^coproc-save]: ESP-IDF 5.5.1, `components/xtensa/xtensa_vectors.S`, `_xt_coproc_exc` and the `_xt_coproc_owner_sa` ownership array. Full mechanism in [coprocessors, CPENABLE, and lazy context switching](../00-foundations/coprocessors-cpenable-and-lazy-context.md), not repeated here.
[^flash1]: ESP-IDF 5.5.1, `components/spi_flash/cache_utils.c`, `spi_flash_disable_interrupts_caches_and_other_cpu()`: calls `vTaskPreemptionDisable(NULL)` (SMP FreeRTOS, non-unicore) or `vTaskSuspendAll()`, sends `esp_ipc_call_nonblocking(other_cpuid, &spi_flash_op_block_func, ...)` to stall the other core, calls `esp_intr_noniram_disable()`, then `spi_flash_disable_cache()` for the current core (and the other core too when `SOC_IDCACHE_PER_CORE`).
[^flash2]: ESP-IDF 5.5.1, `components/spi_flash/cache_utils.c`, `spi_flash_op_block_func()` (`IRAM_ATTR`): the other core's busy-wait, `while (!s_flash_op_complete) { /* busy loop */ }`, entered with its scheduler and non-IRAM interrupts disabled; and `spi_flash_enable_interrupts_caches_and_other_cpu()`, which restores cache and resumes both. Comment above `spi_flash_op_block_func`: "If you're going to modify this, keep in mind that while the flash caches of the pro and app cpu are separate, the psram cache is *not*."
[^qemu26]: espressif/qemu issue #154, "ESP32-S3: qemu-system-xtensa boots with CPENABLE = 0; first FP op recurses through the Cp0Disabled handler (QEMU-293)", opened 2026-05-28, open as of 2026-09-06. Discussed in full in [QEMU for the ESP32-S3](../05-measurement/qemu-esp32s3-what-it-proves-and-what-it-cannot.md).
[^z-soc]: Zephyr Project Documentation, "ESP32-S3 Features", `docs.zephyrproject.org/latest/boards/espressif/common/soc-esp32s3-features.html`, retrieved 2026-09-06: describes dual-core operation through Asymmetric Multiprocessing with OpenAMP, "each core can be enabled to execute customized tasks in stand-alone mode and/or exchanging data over OpenAMP framework", the limitation that Zephyr-managed serial drivers are "not yet implemented for applications running on the APPCPU", and "Additional vector instructions support for AI acceleration" as a listed chip feature.
[^z-smp]: zephyrproject-rtos/zephyr, GitHub issue #83168, "SMP on ESP32 seems to be broken", retrieved 2026-09-06. Reporter: the classic ESP32 board is "one of the few targets I know for having SMP capabilities in Zephyr" and is now marked unsupported; asks "if you're dropping support for one of the few boards that feature SMP could you maybe extend this functionality for other targets (e.g. ESP32s3)?" No maintainer confirmation of ESP32-S3 SMP support was visible in the issue as fetched.
[^z-xtensa]: Zephyr Project Documentation, "Xtensa Developer Guide", `docs.zephyrproject.org/latest/hardware/arch/xtensa.html`, retrieved 2026-09-06: describes `CONFIG_XTENSA_HIFI_SHARING` with eager mode ("the HiFi registers are saved and restored during every thread context switch, regardless of whether the thread used them or not") as default and lazy mode ("the kernel tracks the thread that 'owns' the coprocessor[;] if the 'owning' thread is switched out, the HiFi registers will not be saved until a new thread attempts to use the HiFi") as `CONFIG_XTENSA_LAZY_HIFI_SHARING`, with an SMP note that a lazy handoff to a still-live remote owner sends an IPI to force the save. The page names only Cadence's HiFi DSP coprocessor; ESP32-S3, Espressif, and PIE do not appear on it.
[^nuttx-soc]: Apache NuttX, "Espressif ESP32-S3", `nuttx.apache.org/docs/latest/platforms/xtensa/esp32s3/index.html`, retrieved 2026-09-06: "On ESP32-S3, SMP is enabled to enhance Wi-Fi performance" (stated in the Wi-Fi and Wi-Fi SoftAP sections).
[^nuttx-kconfig]: Apache NuttX source, `arch/xtensa/Kconfig`, retrieved 2026-09-06 (`raw.githubusercontent.com/apache/nuttx/master/arch/xtensa/Kconfig`): `ARCH_HAVE_MULTICPU` selected for both the ESP32 and ESP32-S3 SoC configurations.
[^nuttx-lazy]: Apache NuttX source, `arch/xtensa/Kconfig`, retrieved 2026-09-06: option `XTENSA_CP_LAZY`, help text quoted verbatim: "CPENABLE is set to zero on each context switch, disabling all co-processors[.] If/when the task attempts to use the disabled co-processor, an exception occurs[.] The co-processor exception handler re-enables the co-processor," contrasted with the standard mode that "saves and restores the co-processor enabled (CPENABLE) register on each context switch," described as costing a save/restore "even if the co-processor was never used."
