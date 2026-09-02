/* Minimal Xtensa exception vector table for the reusable PIE-kernel
 * harness. Two jobs: (1) the WindowOverflow4/WindowUnderflow4 handlers,
 * without which ANY windowed-ABI call (entry/retw -- unavoidable, this
 * toolchain build has no call0 multilib, see ../build.sh) faults with
 * nowhere to vector to and double-faults forever (empirically confirmed:
 * QEMU monitor register dump on the earlier windowed-ABI attempt showed
 * EPC1-EPC7 all 0, DEPC==PC at the double-exception vector, no interrupt
 * enabled at all -- the signature of an unhandled exception escalating
 * with no vector table present, not a watchdog/NMI as first suspected).
 * (2) a diagnostic catch-all at the Kernel/User/Double exception vectors,
 * so if a kernel under test does anything else exception-worthy, the
 * harness reports EXCCAUSE/EPC1/DEPC over UART instead of silently
 * hanging -- a run.sh timeout with no output is a much harder failure to
 * debug than a printed cause code.
 *
 * WindowOverflow4/WindowUnderflow4/8/12 bodies below are copied verbatim
 * from Espressif's framework-espidf xtensa/xtensa_vectors.S (Cadence/
 * Tensilica reference boilerplate, not ESP-IDF-specific -- pure
 * register-window spill/fill, identical on every XEA2 windowed-ABI
 * Xtensa core). Section names and the .org offsets within each vector
 * region also match that file and its accompanying sections.ld.in exactly,
 * so the same handler code lands at the same VECBASE-relative offsets
 * this harness's own link.ld places them at.
 */
    .section .WindowVectors.text, "ax"

    .org    0x0
    .global _WindowOverflow4
_WindowOverflow4:
    s32e    a0, a5, -16
    s32e    a1, a5, -12
    s32e    a2, a5,  -8
    s32e    a3, a5,  -4
    rfwo

    .org    0x40
    .global _WindowUnderflow4
_WindowUnderflow4:
    l32e    a0, a5, -16
    l32e    a1, a5, -12
    l32e    a2, a5,  -8
    l32e    a3, a5,  -4
    rfwu

    .org    0x80
    .global _WindowOverflow8
_WindowOverflow8:
    s32e    a0, a9, -16
    l32e    a0, a1, -12
    s32e    a1, a9, -12
    s32e    a2, a9,  -8
    s32e    a3, a9,  -4
    s32e    a4, a0, -32
    s32e    a5, a0, -28
    s32e    a6, a0, -24
    s32e    a7, a0, -20
    rfwo

    .org    0xC0
    .global _WindowUnderflow8
_WindowUnderflow8:
    l32e    a0, a9, -16
    l32e    a1, a9, -12
    l32e    a2, a9,  -8
    l32e    a7, a1, -12
    l32e    a3, a9,  -4
    l32e    a4, a7, -32
    l32e    a5, a7, -28
    l32e    a6, a7, -24
    l32e    a7, a7, -20
    rfwu

    .org    0x100
    .global _WindowOverflow12
_WindowOverflow12:
    s32e    a0,  a13, -16
    l32e    a0,  a1,  -12
    s32e    a1,  a13, -12
    s32e    a2,  a13,  -8
    s32e    a3,  a13,  -4
    s32e    a4,  a0,  -48
    s32e    a5,  a0,  -44
    s32e    a6,  a0,  -40
    s32e    a7,  a0,  -36
    s32e    a8,  a0,  -32
    s32e    a9,  a0,  -28
    s32e    a10, a0,  -24
    s32e    a11, a0,  -20
    rfwo

    .org 0x140
    .global _WindowUnderflow12
_WindowUnderflow12:
    l32e    a0,  a13, -16
    l32e    a1,  a13, -12
    l32e    a2,  a13,  -8
    l32e    a11, a1,  -12
    l32e    a3,  a13,  -4
    l32e    a4,  a11, -48
    l32e    a5,  a11, -44
    l32e    a6,  a11, -40
    l32e    a7,  a11, -36
    l32e    a8,  a11, -32
    l32e    a9,  a11, -28
    l32e    a10, a11, -24
    l32e    a11, a11, -20
    rfwu

    .section .KernelExceptionVector.text, "ax"
    .global _KernelExceptionVector
_KernelExceptionVector:
    j       gm_exc_catchall

    .section .UserExceptionVector.text, "ax"
    .global _UserExceptionVector
_UserExceptionVector:
    j       gm_exc_catchall

    .section .DoubleExceptionVector.text, "ax"
    .global _DoubleExceptionVector
_DoubleExceptionVector:
    j       gm_exc_catchall

    /* Shared diagnostic handler, out in normal .text (reached via plain
     * `j`, +/-128KB range, comfortably covers this harness's whole IRAM
     * window) -- never expected to return, so free to clobber everything
     * rather than following the exception-handler register-preservation
     * convention a resumable handler would need. */
    .section .text, "ax"
    .global gm_exc_catchall
gm_exc_catchall:
    movi    a4, 0x60000000        /* UART0 base, FIFO at +0 */

    .align 4
    .literal .Lexc_msg1_lit, gm_exc_msg1
    l32r    a5, .Lexc_msg1_lit
1:  l8ui    a6, a5, 0
    beqz    a6, 2f
    s32i    a6, a4, 0
    addi    a5, a5, 1
    j       1b
2:
    rsr.exccause a6                /* a6 = EXCCAUSE, print as 2 hex digits */
    extui   a7, a6, 4, 4
    bgeui   a7, 10, 3f
    addi    a7, a7, 48
    j       4f
3:  addi    a7, a7, 87
4:  s32i    a7, a4, 0

    extui   a7, a6, 0, 4
    bgeui   a7, 10, 5f
    addi    a7, a7, 48
    j       6f
5:  addi    a7, a7, 87
6:  s32i    a7, a4, 0

    movi    a7, 10
    s32i    a7, a4, 0

.Lhang:
    j       .Lhang

    .literal_position

    .section .rodata, "a"
gm_exc_msg1:
    .asciz "GM_QEMUBENCH_HARNESS_EXC cause=0x"
