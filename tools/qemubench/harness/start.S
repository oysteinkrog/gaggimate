/* Entry point for the reusable PIE-kernel harness. Unlike pie_smoke's
 * noabi variant (which avoided entry/retw entirely as a diagnostic), this
 * one enables real windowed-ABI calls, so an arbitrary kernel under test
 * -- compiled normally by this toolchain, which has no call0 multilib and
 * therefore always emits entry/retw -- can be called as a normal C
 * function via callx4.
 *
 * Root cause of the earlier double-fault, now resolved with evidence
 * rather than trial and error: EXCCAUSE=0 is EXCCAUSE_ILLEGAL
 * (xtensa/corebits.h). entry/retw are only legal when PS.WOE=1
 * (PS_WOE_SHIFT=18, corebits.h) -- with WOE=0, the very first entry this
 * harness's target code executes is not "an unexpected window overflow",
 * it is an illegal instruction, full stop. Combined with no vector table
 * (vectors.S fixes that half), this is a complete explanation requiring
 * no watchdog/NMI theory. PS_WOECALL4_ABI (corebits.h: PS_WOE|CALLINC(1)
 * = 0x50000) is the standard priming value software must set once before
 * the *first* windowed call from a non-windowed context, since CALLINC is
 * normally maintained by call4/call8/call12 themselves, not by the
 * hardware at reset.
 */
    .text
    .align 4
    .global _start
_start:
    /* Watchdog disable -- kept from pie_smoke/start.S. Harmless, cheap
     * insurance; not the actual fix (see above), but no reason to drop it. */
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

    /* Install this harness's own vector table before anything that could
     * fault -- VECBASE must be set before the first PS.WOE-gated entry
     * below, or that entry's illegal-instruction exception has nowhere
     * valid to land, same failure as before. */
    .literal .Lvecbase_lit, _vector_table
    l32r    a2, .Lvecbase_lit
    wsr.vecbase a2
    rsync

    /* Explicit, clean window state: exactly one valid frame (this one),
     * nothing stale left by QEMU's -kernel loader for the very first
     * entry to trip over. */
    movi    a3, 1
    wsr.windowstart a3
    movi    a3, 0
    wsr.windowbase  a3
    rsync

    /* Mask maskable interrupts (this harness installs no ISR for any of
     * them); enable windowed calls (PS.WOE=1) and prime CALLINC=1 for the
     * first call4 below. INTLEVEL=15 does not block window-overflow or
     * illegal-instruction exceptions -- those are synchronous exceptions,
     * not interrupts, and are unaffected by INTLEVEL masking. */
    movi    a2, 0x5000f            /* PS_WOECALL4_ABI (0x50000) | INTLEVEL=15 */
    wsr.ps  a2
    rsync

    .literal .Lstack_top_lit, _stack_top
    l32r    a1, .Lstack_top_lit    /* a1 is the stack pointer regardless of ABI */

    .literal .Lmain_lit, main
    l32r    a4, .Lmain_lit
    callx4  a4                     /* call into the normally-compiled kernel/test */

.Lhang:
    j       .Lhang
