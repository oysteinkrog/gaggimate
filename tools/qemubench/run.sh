#!/bin/bash
# Boot a tools/qemubench ELF directly via QEMU's `-kernel` loader (no
# bootloader, no flash image, no partition table -- confirmed this QEMU
# fork's esp32s3 machine accepts -kernel and loads/executes an arbitrary
# ELF's entry point). Captures serial output to a log and exits once
# GM_QEMUBENCH_PIE_DONE appears or a timeout elapses, whichever first --
# the test programs spin forever after printing, so this can't just wait
# for the process to exit on its own.
#
#   ./run.sh build/pie_smoke.elf [timeout_seconds]
set -euo pipefail

ELF="${1:?usage: run.sh <elf-path> [timeout_seconds]}"
TIMEOUT="${2:-15}"
[ -f "$ELF" ] || { echo "no such ELF: $ELF"; exit 1; }

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
QEMU_BIN="${GAGGIMATE_QEMU_HOME:-/mnt/c/work/qemu-esp}/bin/qemu-system-xtensa.exe"
LOG="$(dirname "$ELF")/$(basename "$ELF" .elf).boot.log"
ELF_WIN="$(wslpath -w "$ELF")"

: > "$LOG"
echo "booting $ELF via -kernel, log -> $LOG (timeout ${TIMEOUT}s)"

# -serial file:... writes the UART stream straight to a Windows-side file;
# simpler than the console-port TCP bridge scripts/qemu-run.sh uses for the
# full firmware, since nothing here needs bidirectional input (no touch
# injection) -- just capture what the test prints and quit.
LOG_WIN="$(wslpath -w "$LOG")"
timeout "$TIMEOUT" "$QEMU_BIN" \
    -machine esp32s3 \
    -m 8M \
    -nographic \
    -serial "file:${LOG_WIN}" \
    -kernel "$ELF_WIN" \
    > /dev/null 2>&1 &
QEMU_PID=$!

for _ in $(seq 1 $((TIMEOUT * 5))); do
    if grep -q "GM_QEMUBENCH_PIE_DONE" "$LOG" 2>/dev/null; then
        break
    fi
    if ! kill -0 "$QEMU_PID" 2>/dev/null; then
        break
    fi
    sleep 0.2
done

kill "$QEMU_PID" 2>/dev/null || true
wait "$QEMU_PID" 2>/dev/null || true

echo "--- boot log ---"
cat "$LOG"
echo "--- end boot log ---"

if grep -q "GM_QEMUBENCH_PIE: PASS" "$LOG" 2>/dev/null; then
    echo "RESULT: PASS"
    exit 0
elif grep -q "GM_QEMUBENCH_PIE: FAIL" "$LOG" 2>/dev/null; then
    echo "RESULT: FAIL"
    exit 1
else
    echo "RESULT: NO OUTPUT (boot failure, wrong load address, or instruction fault -- nothing reached the print)"
    exit 2
fi
