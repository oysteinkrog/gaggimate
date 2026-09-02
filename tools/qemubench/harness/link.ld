/* Linker script for the reusable PIE-kernel harness. Same IRAM base as
 * ../pie_smoke/link.ld (see that file's comment for how these addresses
 * were sourced from this repo's own working firmware.elf), but with a
 * real vector table region up front: WindowVectors at +0x0, Kernel/User/
 * Double exception vectors at the fixed offsets Xtensa's exception
 * architecture and ESP-IDF's own sections.ld.in both use (+0x300/+0x340/
 * +0x3C0 -- the +0x3C0 offset is not a guess, it's where this harness's
 * predecessor (pie_smoke) empirically landed when it had no vector table
 * at all: VECBASE read back as 0x40000000 and the fault PC as 0x400003c0
 * via the QEMU monitor). .text starts at +0x400, clear of the table.
 */
MEMORY
{
    iram (RWX) : ORIGIN = 0x40374000, LENGTH = 0x20000
}

ENTRY(_start)

SECTIONS
{
    .vectors : {
        _vector_table = .;
        . = 0x0;
        KEEP(*(.WindowVectors.text))
        . = 0x300;
        KEEP(*(.KernelExceptionVector.text))
        . = 0x340;
        KEEP(*(.UserExceptionVector.text))
        . = 0x3C0;
        KEEP(*(.DoubleExceptionVector.text))
        . = 0x400;
    } > iram

    .text : {
        *(.text._start)
        *(.text)
        *(.text.*)
        *(.literal)
        *(.literal.*)
        *(.lit4)
        *(.lit4.*)
    } > iram

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    } > iram

    .data : {
        *(.data)
        *(.data.*)
    } > iram

    .bss (NOLOAD) : {
        _bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        _bss_end = .;
    } > iram

    _stack_top = ORIGIN(iram) + LENGTH(iram);
}
