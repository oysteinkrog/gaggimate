/* Bare-metal entry point for QEMU's `-kernel` ELF loader. No ROM boot, no
 * OS, no interrupts expected -- this only needs to set a stack pointer and
 * fall into main(). `j`, not `call0`/`callx`: a plain jump does not touch
 * the register window, so this is safe regardless of ABI or of whatever
 * window state QEMU's -kernel boot leaves the CPU in. main() itself is
 * compiled with this toolchain's only available ABI (windowed -- see
 * ../build.sh for why call0 isn't an option here); its call tree is
 * shallow enough to never trigger a window overflow/underflow exception,
 * which is why this harness gets away with installing no exception vector
 * table at all.
 */
    .text
    .align 4
    .global _start
_start:
    /* Third empirical finding, and the real root cause: `-d int` showed
     * xtensa_cpu_do_interrupt(12) firing continuously, starting at pc=
     * 0x40000400 (the ROM entry, before any of this program's own code
     * runs) and persisting even at ps=0x1f (INTLEVEL=15, all maskable
     * levels blocked) -- an NMI-class event, unmaskable by rsil. That
     * matches the standard ESP32-family bring-up gotcha: the ROM leaves
     * the RTC_CNTL and TIMG0/TIMG1 hardware watchdogs armed, expecting the
     * normal boot flow to feed or disable them promptly; -kernel mode
     * skips that flow entirely (confirmed real ROM instructions execute
     * first, per the in_asm trace), so the watchdog(s) fire almost
     * immediately via NMI. Disable all three before anything else,
     * including before the rsil below (which cannot mask this anyway, but
     * order still matters: the watchdog write-protect unlock/disable
     * sequence must complete before the NMI can retrigger).
     * WDTWPROTECT unlock value (0x50D83AA1) is the standard ESP32-family
     * magic constant, identical across RTC_CNTL and TIMG. */
    movi    a4, 0x60008000        /* RTC_CNTL base */
    movi    a5, 0x50d83aa1
    s32i    a5, a4, 0x00a8        /* RTC_CNTL_WDTWPROTECT_REG: unlock */
    movi    a5, 0
    s32i    a5, a4, 0x0090        /* RTC_CNTL_WDTCONFIG0_REG: disable all stages */

    movi    a4, 0x6001f000        /* TIMG0 base */
    movi    a5, 0x50d83aa1
    s32i    a5, a4, 0x0064        /* TIMG_WDTWPROTECT_REG: unlock */
    movi    a5, 0
    s32i    a5, a4, 0x0048        /* TIMG_WDTCONFIG0_REG: disable */

    movi    a4, 0x60020000        /* TIMG1 base */
    movi    a5, 0x50d83aa1
    s32i    a5, a4, 0x0064
    movi    a5, 0
    s32i    a5, a4, 0x0048

    /* Mask every maskable interrupt before anything else runs. First empirical
     * finding from this harness: without this, main()'s windowed-ABI `entry`
     * prologue is immediately followed by a jump to 0x400003c0 and an illegal-
     * instruction fault loop (seen via `qemu-system-xtensa -d in_asm`) -- some
     * interrupt source left armed by the ROM boot stub that ran before -kernel
     * handed off to this code fires at the first instruction-boundary
     * opportunity, and vectors into an exception table this harness never
     * installs (there is no ISR here for it to reach). rsil sets
     * PS.INTLEVEL=15, masking all maskable interrupt priorities; it does not
     * touch NMI, but nothing here should provoke one. */
    rsil    a2, 15

    /* Second empirical finding: rsil alone didn't fix it -- the fault at
     * 0x400003c0 persisted right after main()'s `entry` prologue, meaning
     * it's not a maskable interrupt but a window-overflow exception. `entry`
     * checks CALLINC against WINDOWSTART/WINDOWBASE on every windowed call,
     * not just past a nesting depth, and the ROM boot stub that ran before
     * -kernel handed off to this code (visible in the trace: real
     * instructions execute at 0x40000400/0x40000408 first) apparently left
     * that state looking non-empty to the very first `entry` this program
     * executes. Declare window 0 as the only valid frame explicitly, rather
     * than trusting whatever the ROM stub left behind. rsync is required
     * after writing WINDOWBASE/WINDOWSTART so the change is visible to
     * subsequent instruction dispatch before falling through to `entry`. */
    movi    a3, 1
    wsr.windowstart a3
    movi    a3, 0
    wsr.windowbase  a3
    rsync

    .literal .Lstack_top_lit, _stack_top
    l32r    a1, .Lstack_top_lit   /* a1 is the stack pointer regardless of ABI */
    j       main
