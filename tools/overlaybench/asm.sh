#!/bin/bash
# Cross-compile a kernel file with the REAL device compiler and the real
# firmware's optimization level, and emit annotated Xtensa assembly.
#
# Unlike tools/animbench/xtensa-asm.sh, this does NOT need a cmd.exe round
# trip: the real firmware build (verified against
# .pio/build/display/compile_commands.json) uses toolchain-xtensa-esp-elf
# (crosstool-NG esp-14.2.0, GCC 14.2.0), whose xtensa-esp32s3-elf-g++ is a
# native Linux ELF binary -- animbench's script targets
# toolchain-xtensa-esp32s3 (GCC 8.4.0, Windows .exe only), which is NOT what
# ships. Worth knowing if the two tools' asm output ever seems to disagree.
#
#   ./asm.sh span_scan          # one kernel (kernels/span_scan.cpp)
#   ./asm.sh all                # every kernel
#
# Output: xtensa-asm/<name>.S (assembly) and a per-function cost report
# (reusing tools/animbench's xtensa_report.py, which is compiler-version
# agnostic -- it parses mnemonics, not toolchain metadata).
#
# Optimization level: -O2, matching platformio.ini's [display_common]
# build_flags override (upstream Arduino/ESP-IDF default is -Os; this repo
# overrides it for the display env specifically because the animation
# pipeline is the hot path -- see the comment there). -mlongcalls matches
# the real compile command; -fno-exceptions/-fno-rtti match the firmware's
# effective settings for this file (SleepAnimation.cpp compiles under
# -fno-rtti, -fexceptions is passed but the kernels here throw nothing so it
# does not change codegen).
set -e
cd "$(dirname "$0")"
CXX='/mnt/c/Users/Oystein/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++'
if [ ! -x "$CXX" ]; then
    echo "toolchain not found at $CXX -- check ~/.platformio/packages/toolchain-xtensa-esp-elf" >&2
    exit 1
fi
FLAGS="-O2 -std=gnu++20 -mlongcalls -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -fno-jump-tables -S -fverbose-asm"
mkdir -p xtensa-asm

build_one() {
    local base="$1"
    local src="kernels/${base}.cpp"
    [ -f "$src" ] || { echo "no such kernel: $src"; return 1; }
    "$CXX" $FLAGS -I. "$src" -o "xtensa-asm/${base}.S"
    python3 ../animbench/xtensa_report.py "xtensa-asm/${base}.S"
}

if [ "$1" = "all" ]; then
    for f in kernels/*.cpp; do
        build_one "$(basename "$f" .cpp)"
    done
else
    build_one "${1%.cpp}"
fi
