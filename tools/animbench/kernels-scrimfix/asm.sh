#!/bin/bash
# Cross-compile scrimrow.cpp with the REAL device compiler at the real
# firmware's optimization flags, and report per-function instruction counts
# and zero-overhead LOOP recovery.
#
# tools/animbench/xtensa-asm.sh targets toolchain-xtensa-esp32s3 (GCC 8.4.0,
# Windows .exe only, via a cmd.exe round trip) -- NOT what ships.
# tools/overlaybench/BASELINE-OVERLAY.md checked .pio/build/display/
# compile_commands.json (a sibling worker's finding) and found the real
# firmware build uses toolchain-xtensa-esp-elf (crosstool-NG esp-14.2.0,
# GCC 14.2.0), whose xtensa-esp32s3-elf-g++ is a native Linux ELF binary.
# This script targets that toolchain directly, independently confirmed here
# by locating the binary at the path below (present on this box) and cross-
# checking platformio.ini's [display_common]/[env:display] sections
# directly, since this checkout's .pio/build/ only has display-loadtest and
# display-qemu built (no display-env compile_commands.json to read):
#   - [display_common] build_flags: -std=gnu++20, -O2 (comment there:
#     "-O2, not upstream's -Os. The animation pipeline is the hot path...").
#   - [env:display] build_unflags: -std=gnu++11, -std=gnu++17, -std=gnu++2b,
#     -Os -- strips the framework's -Os occurrences so -O2 is the last -O
#     flag GCC sees (GCC honours the last one given).
# Flags below match what tools/overlaybench/asm.sh already uses (that
# script's own header comment records the same cross-check against a
# populated compile_commands.json at the time it was written) --
# -mlongcalls/-fno-exceptions/-fno-rtti/-ffunction-sections/-fdata-sections/
# -fno-jump-tables match the real compile command for SleepAnimation.cpp;
# -fno-exceptions is technically stricter than the ini's default
# -fexceptions, but SleepAnimation.cpp throws nothing so it does not change
# codegen for this file (same rationale overlaybench/asm.sh's header gives).
#
#   ./asm.sh              # both variants (scrimrow.cpp has all three)
#
# Output: xtensa-asm/scrimrow.S and a per-function cost report (reusing
# tools/animbench/xtensa_report.py directly -- it parses mnemonics, not
# toolchain metadata, so it is compiler-version agnostic; calling an
# existing tool by path is fine, this script does not write into
# tools/overlaybench/).
set -e
cd "$(dirname "$0")"
CXX='/mnt/c/Users/Oystein/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++'
if [ ! -x "$CXX" ]; then
    echo "toolchain not found at $CXX -- check ~/.platformio/packages/toolchain-xtensa-esp-elf" >&2
    exit 1
fi
FLAGS="-O2 -std=gnu++20 -mlongcalls -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -fno-jump-tables -S -fverbose-asm"
mkdir -p xtensa-asm

"$CXX" $FLAGS -I. scrimrow.cpp -o xtensa-asm/scrimrow.S
python3 ../xtensa_report.py xtensa-asm/scrimrow.S
