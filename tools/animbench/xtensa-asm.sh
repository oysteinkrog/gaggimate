#!/bin/bash
# Cross-compile bganim sources with the REAL device compiler (xtensa-esp32s3
# GCC 8.4, same -O2 as the firmware build) and emit annotated assembly +
# a per-function cost report. This is ground truth for what the ESP32-S3
# actually executes — soft-float libcalls, zero-overhead LOOP instructions,
# and inner-loop instruction counts are all visible here.
#
#   ./xtensa-asm.sh AnimCaustics        # one animation
#   ./xtensa-asm.sh all                 # the whole fleet + common
#
# Output: xtensa-asm/<name>.S (assembly) and a report on stdout.
# Must be run from tools/animbench (the Windows-hosted compiler resolves
# relative paths against the WSL cwd; absolute /mnt/c paths would break).
set -e
cd "$(dirname "$0")"
# The toolchain is the Windows build (PlatformIO runs under cmd.exe on this
# box). Invoke it through cmd.exe with a Windows argv[0] — launched via a WSL
# path the GCC driver fails to spawn cc1plus ("CreateProcess: No such file").
CXX_WIN='C:\Users\Oystein\.platformio\packages\toolchain-xtensa-esp32s3\bin\xtensa-esp32s3-elf-g++.exe'
FLAGS="-O2 -std=gnu++17 -mlongcalls -fno-exceptions -fno-rtti -ffunction-sections -S -fverbose-asm"
mkdir -p xtensa-asm

build_one() {
    local base="$1"
    local src="../../src/display/ui/default/bganim/${base}.cpp"
    [ -f "$src" ] || { echo "no such source: $src"; return 1; }
    cmd.exe /c "$CXX_WIN $FLAGS -Ishim $src -o xtensa-asm/${base}.S"
    python3 xtensa_report.py "xtensa-asm/${base}.S"
}

if [ "$1" = "all" ]; then
    for f in ../../src/display/ui/default/bganim/Anim*.cpp ../../src/display/ui/default/bganim/BgAnimCommon.cpp; do
        build_one "$(basename "$f" .cpp)"
    done
else
    build_one "${1%.cpp}"
fi
