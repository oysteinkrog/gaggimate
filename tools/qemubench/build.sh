#!/bin/bash
# Build a bare-metal PIE test image for direct QEMU `-kernel` boot. Standalone:
# does not touch .pio/, platformio.ini, or CMake -- a raw compile+link with the
# same Windows-hosted device toolchain tools/animbench/xtensa-asm.sh already
# established works from WSL only when invoked through cmd.exe with a Windows
# argv[0] (a WSL-path invocation fails to spawn cc1plus/collect2). Like that
# script, this uses plain relative paths with cmd.exe rather than converting
# to Windows paths: WSL's interop resolves cwd correctly for a process
# launched from a /mnt/c-rooted directory, which is what xtensa-asm.sh already
# relies on and this repo's builds always run from.
#
#   ./build.sh pie_smoke              # standalone: pie_smoke/{start.S,main.c,link.ld} -> build/pie_smoke.elf
#   ./build.sh tests/pie_smoke_full   # harness mode: tests/pie_smoke_full/main.c (a `main()`
#                                      # entry point only) is linked against the shared
#                                      # harness/{start.S,vectors.S,link.ld} -- use this shape
#                                      # for any new kernel test. Detection rule: a source dir
#                                      # with its own link.ld builds standalone (legacy
#                                      # pie_smoke/pie_smoke_noabi, which predate the harness and
#                                      # carry their own diagnostic boot code -- see their
#                                      # comments); a source dir with no link.ld is a harness-mode
#                                      # test and gets harness/{start.S,vectors.S,link.ld} added
#                                      # automatically. See harness/start.S and harness/vectors.S
#                                      # for what that gets you: a real Xtensa vector table
#                                      # (WindowOverflow4/Underflow4/8/12 + a diagnostic
#                                      # Kernel/User/Double-exception catch-all) and PS.WOE
#                                      # correctly enabled before the call into your main(), so a
#                                      # kernel under test can be normal windowed-ABI C/asm
#                                      # (entry/retw, arbitrary call depth) rather than the
#                                      # hand-rolled no-calls-at-all style pie_smoke_noabi needed.
set -e
cd "$(dirname "$0")"

NAME="${1:?usage: build.sh <source-subdir>}"
SRC_DIR="$NAME"
[ -d "$SRC_DIR" ] || { echo "no such source dir: $SRC_DIR"; exit 1; }

if [ -f "$SRC_DIR/link.ld" ]; then
    HARNESS_MODE=0
    LINK_LD="$SRC_DIR/link.ld"
    EXTRA_SRCS=()
else
    HARNESS_MODE=1
    LINK_LD="harness/link.ld"
    EXTRA_SRCS=(harness/start.S harness/vectors.S)
    echo "harness mode: linking against harness/{start.S,vectors.S,link.ld}"
fi

CC_WIN='C:\Users\Oystein\.platformio\packages\toolchain-xtensa-esp32s3\bin\xtensa-esp32s3-elf-gcc.exe'
CXX_WIN='C:\Users\Oystein\.platformio\packages\toolchain-xtensa-esp32s3\bin\xtensa-esp32s3-elf-g++.exe'
# C++-only extras, for a test driver that #includes a kernel's own .cpp
# (e.g. tools/animbench/kernels-blend/blend_pie_kernel.cpp) directly rather
# than duplicating it -- matches that directory's own build_and_report.sh
# flags (-std=gnu++20 -fno-exceptions -fno-rtti -mlongcalls) so the code
# compiles exactly the way it was already proven to assemble cleanly, not a
# second, drifted set of flags. -fno-exceptions/-fno-rtti: no runtime
# support library exists in this freestanding link, and the kernel code
# doesn't use either. -mlongcalls: matches the source directory's own
# build; overkill for this harness's small IRAM image (everything is well
# within direct-call range) but not wrong, and keeps the object closer to
# what was already verified rather than introducing a difference.
# This toolchain's g++ predates the "gnu++20" spelling (errors, suggests
# "gnu++2a" instead) -- same standard, older alias. blend-asm-lead's
# build_and_report.sh uses "gnu++20" against a different toolchain
# (toolchain-xtensa-esp-elf, not this repo's toolchain-xtensa-esp32s3); no
# semantic difference for code that doesn't probe __cplusplus's exact value.
CXXFLAGS_EXTRA="-std=gnu++2a -fno-exceptions -fno-rtti -mlongcalls"
# This toolchain build has no call0 multilib (`-print-multi-lib` shows a
# single "." variant; --abi-call0 is accepted by --target-help as a generic
# xtensa-backend option but rejected at compile time -- not actually wired
# up in this specific Espressif-shipped build). So: default windowed ABI,
# meaning ENTRY/RETW manage the register window in hardware on every call.
# That only traps into a window overflow/underflow exception once nested
# call depth exceeds the physical register file -- this program's call
# tree is _start -> main -> {uart_puts, uart_put_hex_byte, sat_add_s8},
# each a leaf, so it never gets close. No exception vector table is
# installed; if that assumption is ever wrong for a deeper test, the
# symptom is silence (no UART output, run.sh reports NO OUTPUT) rather
# than a wrong answer, which is the failure mode worth designing for here.
# -ffreestanding/-nostdlib/-nostartfiles: no libc, no crt0, no OS; _start
# (start.S) is the only entry code. -fno-builtin so plain-looking C (the
# uart_puts loop, etc.) doesn't get quietly rewritten into a libc
# memcpy/strlen call this link has no definition for.
# -mtext-section-literals: emit l32r literal pools inline in .text right
# before their use, instead of a separate .literal section this from-scratch
# linker script (no ESP-IDF default script to lean on) placed AFTER all
# .text -- which broke every l32r with "literal placed after use" (l32r's
# PC-relative encoding can only reach backward).
FLAGS="-mtext-section-literals -ffreestanding -nostdlib -nostartfiles -fno-builtin -Wall -Wextra -O1 -g"

mkdir -p build
rm -f "build/${NAME}.elf" "build/$SRC_DIR"/*.o
mkdir -p "build/$SRC_DIR"
[ "$HARNESS_MODE" = 1 ] && mkdir -p build/harness

OBJS=()
HAVE_CPP=0
for src in "${EXTRA_SRCS[@]}" "$SRC_DIR"/*.S "$SRC_DIR"/*.c "$SRC_DIR"/*.cpp; do
    [ -f "$src" ] || continue
    obj="build/${src%.*}.o"
    echo "compiling $src -> $obj"
    case "$src" in
        *.cpp)
            HAVE_CPP=1
            cmd.exe /c "$CXX_WIN $FLAGS $CXXFLAGS_EXTRA -c $src -o $obj"
            ;;
        *)
            cmd.exe /c "$CC_WIN $FLAGS -c $src -o $obj"
            ;;
    esac
    OBJS+=("$obj")
done

echo "linking -> build/${NAME}.elf"
LINKER="$CC_WIN"
[ "$HAVE_CPP" = 1 ] && LINKER="$CXX_WIN"
cmd.exe /c "$LINKER -nostdlib -Wl,-T,${LINK_LD} -Wl,--build-id=none ${OBJS[*]} -o build/${NAME}.elf"

echo "OK: build/${NAME}.elf"
