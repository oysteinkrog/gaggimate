/* Pure straight-line variant of the PIE smoke test: no `entry`/`retw`, no
 * calls of any kind, so the windowed-ABI register-window exception
 * machinery never engages. Diagnostic rationale (see start.S's own
 * comments for the fuller trail): the earlier attempt, which called into a
 * C-compiled main() using this toolchain's only available ABI (windowed --
 * no call0 multilib), double-faulted right after main()'s `entry`
 * prologue. EPC1-EPC7 all reading 0 while DEPC==PC at the fault address
 * (captured via the QEMU monitor's `info registers`) is the signature of a
 * double exception with no vector table installed anywhere -- not a
 * watchdog/NMI (INTENABLE and INTERRUPT both read 0 in that same dump,
 * ruling out any live interrupt source). Windowed `entry` can require a
 * WindowOverflow exception handler on the very first call after reset to
 * spill/validate window state; this harness never installs one, so that
 * first exception (of whatever cause) has nowhere valid to vector to,
 * faults again immediately, and escalates to the double-exception vector,
 * which is equally unmapped here -- hence the perpetual stall this file
 * exists to route around, rather than fix by hand-writing
 * WindowOverflow/WindowUnderflow handlers (the single fiddliest piece of
 * hand-written Xtensa assembly, normally vendor-supplied startup code).
 * Fix: never execute `entry`/`retw` at all. Every instruction below is
 * straight-line or a plain branch/jump -- no `call0`/`callx`/`entry`.
 */
    .text
    .align 4
    .global _start
_start:
    /* Watchdog disable -- cheap insurance, kept from the windowed variant
     * even though the double-exception theory doesn't need it; harmless
     * either way and this rules the watchdog theory out for good if this
     * variant still runs cleanly with it. */
    movi    a4, 0x60008000
    movi    a5, 0x50d83aa1
    s32i    a5, a4, 0x00a8
    movi    a5, 0
    s32i    a5, a4, 0x0090

    movi    a4, 0x6001f000
    movi    a5, 0x50d83aa1
    s32i    a5, a4, 0x0064
    movi    a5, 0
    s32i    a5, a4, 0x0048

    movi    a4, 0x60020000
    movi    a5, 0x50d83aa1
    s32i    a5, a4, 0x0064
    movi    a5, 0
    s32i    a5, a4, 0x0048

    rsil    a2, 15

    /* Load test vectors, run the PIE ops under test. */
    .literal .La_lit, test_a
    .literal .Lb_lit, test_b
    .literal .Lr_lit, test_result
    l32r    a4, .La_lit
    l32r    a5, .Lb_lit
    l32r    a6, .Lr_lit

    ee.vld.128.ip q0, a4, 0
    ee.vld.128.ip q1, a5, 0
    ee.vadds.s8   q2, q0, q1
    ee.vst.128.ip q2, a6, 0

    /* Print the 16 result bytes as hex over UART0, straight-line, no calls. */
    .literal .Luart_lit, 0x60000000
    .literal .Lr_lit2, test_result
    l32r    a8, .Luart_lit
    l32r    a6, .Lr_lit2
    movi    a9, 16
.Lprint_loop:
    l8ui    a10, a6, 0
    addi    a6, a6, 1

    extui   a11, a10, 4, 4
    bgeui   a11, 10, .Lhigh_alpha
    addi    a11, a11, 48        /* '0' */
    j       .Lhigh_done
.Lhigh_alpha:
    addi    a11, a11, 87        /* 'a'-10 */
.Lhigh_done:
    s32i    a11, a8, 0

    extui   a11, a10, 0, 4
    bgeui   a11, 10, .Llow_alpha
    addi    a11, a11, 48
    j       .Llow_done
.Llow_alpha:
    addi    a11, a11, 87
.Llow_done:
    s32i    a11, a8, 0

    addi    a9, a9, -1
    bnez    a9, .Lprint_loop

    movi    a11, 10             /* '\n' */
    s32i    a11, a8, 0

    /* Fixed marker string, one instruction per char -- no loop needed. */
    movi    a11, 71   /* G */
    s32i    a11, a8, 0
    movi    a11, 77   /* M */
    s32i    a11, a8, 0
    movi    a11, 95   /* _ */
    s32i    a11, a8, 0
    movi    a11, 81   /* Q */
    s32i    a11, a8, 0
    movi    a11, 69   /* E */
    s32i    a11, a8, 0
    movi    a11, 77   /* M */
    s32i    a11, a8, 0
    movi    a11, 85   /* U */
    s32i    a11, a8, 0
    movi    a11, 66   /* B */
    s32i    a11, a8, 0
    movi    a11, 69   /* E */
    s32i    a11, a8, 0
    movi    a11, 78   /* N */
    s32i    a11, a8, 0
    movi    a11, 67   /* C */
    s32i    a11, a8, 0
    movi    a11, 72   /* H */
    s32i    a11, a8, 0
    movi    a11, 95   /* _ */
    s32i    a11, a8, 0
    movi    a11, 80   /* P */
    s32i    a11, a8, 0
    movi    a11, 73   /* I */
    s32i    a11, a8, 0
    movi    a11, 69   /* E */
    s32i    a11, a8, 0
    movi    a11, 95   /* _ */
    s32i    a11, a8, 0
    movi    a11, 68   /* D */
    s32i    a11, a8, 0
    movi    a11, 79   /* O */
    s32i    a11, a8, 0
    movi    a11, 78   /* N */
    s32i    a11, a8, 0
    movi    a11, 69   /* E */
    s32i    a11, a8, 0
    movi    a11, 10   /* \n */
    s32i    a11, a8, 0

.Lhang:
    j       .Lhang

    .data
    .align 16
test_a:
    .byte 196, 204, 212, 220, 228, 236, 244, 252, 4, 12, 20, 28, 36, 44, 52, 60
test_b:
    .byte 5, 5, 5, 5, 5, 5, 5, 5, 100, 100, 100, 100, 100, 100, 100, 100
    .align 16
test_result:
    .space 16
