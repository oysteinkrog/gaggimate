#!/bin/bash
# Compile blend_pie_kernel.cpp with the REAL device compiler at the real
# firmware's optimization level, confirm it compiles/assembles cleanly
# (i.e. every EE.* mnemonic and operand form in the inline asm blocks is
# accepted, not just eyeballed), and report per-function instruction
# counts via tools/animbench/xtensa_report.py.
#
# Mirrors tools/overlaybench/asm.sh's flags exactly (that file's own
# comment documents why: this is the toolchain confirmed, via
# .pio/build/display/compile_commands.json, to be what the real firmware
# build uses -- toolchain-xtensa-esp-elf, crosstool-NG esp-14.2.0,
# GCC 14.2.0 -- and -O2 is platformio.ini's [display_common] override of
# upstream's -Os default specifically for the animation hot path).
#
# This ONLY compiles/assembles. It never links, flashes, or runs anything
# -- out of scope for this pass (no device, no QEMU access here).
set -e
cd "$(dirname "$0")"
CXX='/mnt/c/Users/Oystein/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++'
if [ ! -x "$CXX" ]; then
    echo "toolchain not found at $CXX" >&2
    exit 1
fi
FLAGS="-O2 -std=gnu++20 -mlongcalls -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -fno-jump-tables -S -fverbose-asm"
mkdir -p xtensa-asm build

echo "=== compiling blend_pie_kernel.cpp (text emission) ==="
"$CXX" $FLAGS -I. blend_pie_kernel.cpp -o xtensa-asm/blend_pie_kernel.S
echo "OK: xtensa-asm/blend_pie_kernel.S written"

echo "=== compiling to object (confirms the inline asm actually ASSEMBLES, not just emits text) ==="
"$CXX" -O2 -std=gnu++20 -mlongcalls -fno-exceptions -fno-rtti -c -I. blend_pie_kernel.cpp -o build/blend_pie_kernel.o
echo "OK: build/blend_pie_kernel.o written -- inline asm blocks assembled cleanly"

echo
echo "=== xtensa_report.py: blend_pie_kernel.S (whole dispatch function + the vector helper) ==="
python3 ../xtensa_report.py xtensa-asm/blend_pie_kernel.S

echo
echo "=== xtensa_report.py: blend_group8.S (standalone probe, cross-checked below) ==="
python3 ../xtensa_report.py blend_group8.S
