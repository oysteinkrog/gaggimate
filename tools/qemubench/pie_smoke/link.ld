/* Minimal linker script for a bare-metal, no-OS ESP32-S3 test image loaded
 * directly by QEMU's `-kernel` ELF loader (Espressif's qemu-system-xtensa
 * fork sets PC to the ELF entry point after loading LOAD segments -- there
 * is no ROM bootloader, no flash cache/MMU, no partition table involved).
 *
 * Addresses are not guessed: 0x40374000 (IRAM) and 0x3fc88000 (DRAM) are
 * lifted from `readelf -h -l` on this repo's own working display-qemu
 * firmware.elf, which boots correctly on this exact QEMU machine (esp32s3)
 * via the normal flash+bootloader path. Both regions are internal SRAM,
 * directly bus-addressable without any cache/MMU setup -- unlike the
 * flash-mapped regions (0x42000000 / 0x3c000000 in that same ELF), which
 * require ROM code this harness never runs to configure the cache. Placing
 * everything in IRAM keeps this test independent of whether `-kernel`
 * leaves DRAM in a known state.
 */
MEMORY
{
    iram (RWX) : ORIGIN = 0x40374000, LENGTH = 0x20000
}

ENTRY(_start)

SECTIONS
{
    .text : {
        *(.text._start)
        *(.text)
        *(.text.*)
        /* Xtensa PC-relative l32r needs its constants in a literal pool;
         * GAS emits these into .literal (explicit .literal directives) and
         * .literal.* (GCC's per-function pools) unless told otherwise. Both
         * must land in the same mapped region as the code that reads them,
         * or the l32r offset either fails to link or reads garbage. */
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

    /* Stack grows down from the top of the IRAM window this script claims.
     * No guard page, no overflow detection -- fine for a single-function
     * smoke test with no recursion and a handful of stack-resident locals. */
    _stack_top = ORIGIN(iram) + LENGTH(iram);
}
