#!/bin/bash
# Like xtensa-asm.sh, but with the compiler the firmware is actually built
# with: toolchain-xtensa-esp-elf (crosstool-NG esp-14.2.0, GCC 14.2), the
# flags a verbose `pio run -e display` prints for src/display (-O2 is the
# only -O on the line; -fstack-protector and -fno-jump-tables are on). The
# old script's GCC 8.4 build differs in register allocation and loop shape,
# so its instruction counts are not the device's. This one is.
#
#   ./xtensa-asm14.sh AnimCaustics      # one animation
#   ./xtensa-asm14.sh all               # the fleet + BgAnimCommon
#
# Output: xtensa-asm14/<name>.S (annotated assembly), xtensa-asm14/<name>.o
# (proof that every inline-asm block actually assembles, not only emits
# text), and the xtensa_report.py summary on stdout. This toolchain runs
# from a WSL path directly, no cmd.exe detour needed.
set -e
cd "$(dirname "$0")"
CXX='/mnt/c/Users/Oystein/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++'
[ -x "$CXX" ] || { echo "toolchain not found at $CXX" >&2; exit 1; }
FLAGS="-O2 -std=gnu++20 -mlongcalls -mdisable-hardware-atomics -fno-builtin-memcpy -fno-builtin-memset \
 -ffunction-sections -fdata-sections -fstack-protector -fstrict-volatile-bitfields -fno-jump-tables \
 -fno-tree-switch-conversion -fexceptions -fno-rtti -Wall -Wno-unused-function"
mkdir -p xtensa-asm14

build_one() {
    local base="$1"
    local src="../../src/display/ui/default/bganim/${base}.cpp"
    [ -f "$src" ] || { echo "no such source: $src"; return 1; }
    "$CXX" $FLAGS -Ishim -S -fverbose-asm "$src" -o "xtensa-asm14/${base}.S"
    "$CXX" $FLAGS -Ishim -c "$src" -o "xtensa-asm14/${base}.o"
    python3 xtensa_report.py "xtensa-asm14/${base}.S"
}

if [ "$1" = "all" ]; then
    for f in ../../src/display/ui/default/bganim/Anim*.cpp ../../src/display/ui/default/bganim/BgAnimCommon.cpp; do
        build_one "$(basename "$f" .cpp)"
    done
else
    build_one "${1%.cpp}"
fi
