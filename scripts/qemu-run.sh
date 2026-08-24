#!/usr/bin/env bash
# Boot a built firmware under Espressif's QEMU fork.
#
# Builds a flat 16 MB flash image out of the PlatformIO artifacts, seeds the
# otadata partition, and starts qemu-system-xtensa on it.
#
#   scripts/qemu-run.sh                       # display-qemu, SDL window
#   scripts/qemu-run.sh -e <env>              # another environment
#   scripts/qemu-run.sh --console             # serial on this terminal, no window
#   scripts/qemu-run.sh --gdb --halt          # wait for xtensa-esp32s3-elf-gdb on :1234
#   scripts/qemu-run.sh --log /tmp/boot.log   # tee the serial output
#   scripts/qemu-run.sh --monitor-port 0      # no monitor socket
#   scripts/qemu-run.sh --keep-image          # boot the existing image again
#   scripts/qemu-run.sh --console-port 55556  # console on TCP, for qemu-touch.py
#   scripts/qemu-run.sh --console-port 55556 --console-wait   # ... and hold the
#                                             # reset until the client attaches,
#                                             # so the boot log is not lost
#   scripts/qemu-run.sh -- -d guest_errors    # anything after -- goes to QEMU
#
# This drives the Windows build of QEMU from WSL. The Linux build aborts on WSL1
# with a GLib assertion, and the Windows one opens a native SDL window with no X
# server involved, so the paths handed to qemu and esptool are Windows paths
# while everything else here is a WSL path.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Prebuilt Espressif QEMU (esp_develop_9.2.2_20260417 or newer).
QEMU_HOME_WSL="${GAGGIMATE_QEMU_HOME:-/mnt/c/work/qemu-esp}"
QEMU_BIN="$QEMU_HOME_WSL/bin/qemu-system-xtensa.exe"
ESPTOOL_PY='C:\Users\Oystein\.platformio\packages\tool-esptoolpy\esptool.py'
WIN_PYTHON='C:\Users\oystein\AppData\Local\Programs\Python\Python310\python.exe'

ENV_NAME=display-qemu
FLASH_SIZE=16MB
PSRAM_SIZE=8M
DISPLAY_MODE=sdl
# The QEMU monitor, on localhost. Its "screendump" command writes the console
# surface straight to a file, which is the only reliable way to capture the
# emulated panel: a desktop screen grab of the SDL window returns whatever
# window happens to be on top of it. scripts/qemu-screenshot.sh drives it.
MONITOR_PORT=55555
# Rebuilding the image discards the guest's NVS and LittleFS partitions, so
# settings, profiles and shot history reset on every run. Keeping the image is
# how you test anything that has to survive a reboot.
KEEP_IMAGE=0
# Putting the console on a socket instead of this terminal lets one process own
# both directions of it, which is what mouse-as-touch injection needs: QEMU has
# no input device for the ESP32-S3, so touch records ride in on the console UART
# (scripts/qemu-touch.py, src/display/drivers/Qemu/QemuTouch.cpp).
CONSOLE_PORT=0
# QEMU discards console output written before a client attaches, which loses the
# whole bootloader and early-init log. wait=on holds the machine at reset until
# the bridge connects.
CONSOLE_WAIT=0
GDB=0
HALT=0
LOG=
QEMU_EXTRA=()

usage() { sed -n '2,17p' "${BASH_SOURCE[0]}" | sed 's/^# \?//'; }

while [[ $# -gt 0 ]]; do
    case "$1" in
    -e | --env)
        ENV_NAME="$2"
        shift 2
        ;;
    --console) # serial multiplexed onto this terminal; no display device at all
        DISPLAY_MODE=none
        shift
        ;;
    --gdb)
        GDB=1
        shift
        ;;
    --halt) # start with the CPU stopped, for attaching before the first instruction
        HALT=1
        shift
        ;;
    --log)
        LOG="$2"
        shift 2
        ;;
    --flash-size)
        FLASH_SIZE="$2"
        shift 2
        ;;
    --psram)
        PSRAM_SIZE="$2"
        shift 2
        ;;
    --keep-image) # reuse the previous image, warts and stored state included
        KEEP_IMAGE=1
        shift
        ;;
    --console-port) # 0 keeps the console on this terminal
        CONSOLE_PORT="$2"
        shift 2
        ;;
    --console-wait) # only meaningful with --console-port
        CONSOLE_WAIT=1
        shift
        ;;
    --monitor-port) # 0 disables the monitor entirely
        MONITOR_PORT="$2"
        shift 2
        ;;
    -h | --help)
        usage
        exit 0
        ;;
    --)
        shift
        QEMU_EXTRA=("$@")
        break
        ;;
    *)
        echo "unknown argument: $1" >&2
        usage >&2
        exit 2
        ;;
    esac
done

BUILD_DIR="$REPO/.pio/build/$ENV_NAME"
IMG_WSL="$QEMU_HOME_WSL/img/$ENV_NAME-flash.bin"

[[ -x "$QEMU_BIN" ]] || {
    echo "qemu not found at $QEMU_BIN (set GAGGIMATE_QEMU_HOME)" >&2
    exit 1
}
if ((KEEP_IMAGE)); then
    [[ -f "$IMG_WSL" ]] || {
        echo "no image at $IMG_WSL to keep -- run once without --keep-image first" >&2
        exit 1
    }
else
    for f in bootloader.bin partitions.bin firmware.bin; do
        [[ -f "$BUILD_DIR/$f" ]] || {
            echo "missing $BUILD_DIR/$f -- build $ENV_NAME first" >&2
            exit 1
        }
    done
fi
mkdir -p "$(dirname "$IMG_WSL")"

# wslpath is the only supported way to get a Windows path; hand-rolling the
# /mnt/c -> C: rewrite breaks on any non-default mount root.
img_win="$(wslpath -w "$IMG_WSL")"

if ((KEEP_IMAGE)); then
    echo "==> reusing $IMG_WSL (guest flash state preserved)"
else
    echo "==> merging $ENV_NAME into $IMG_WSL"
    (
        cd "$BUILD_DIR"
    # esptool 5.x: dashed subcommands and flags ("merge-bin", not "merge_bin").
    # PlatformIO's own esptool invocation is not reusable here because it targets
    # a serial port, so the offsets are repeated from the partition layout.
        cmd.exe /c "$WIN_PYTHON $ESPTOOL_PY --chip esp32s3 merge-bin -o $img_win \
            --format raw --pad-to-size $FLASH_SIZE \
            --flash-mode dio --flash-freq 80m --flash-size $FLASH_SIZE \
            0x0 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin" |
            grep -v '^Warning: DEPRECATED' || true
    )

    # Without this the bootloader writes otadata on first boot and QEMU exits(5)
    # mid-write, silently, right after "Loaded app from partition at offset
    # 0x10000".
    python3 "$REPO/scripts/qemu-seed-otadata.py" "$IMG_WSL"
fi

args=(
    -machine esp32s3
    # -m sizes the PSRAM, not the internal SRAM.
    -m "$PSRAM_SIZE"
    # The T-RGB and the controller board both carry octal (OPI) PSRAM; without
    # this the guest's octal_psram probe finds nothing and startup aborts.
    -global driver=ssi_psram,property=is_octal,value=true
    # 0x04 = boot from SPI flash. The default strapping lands in download mode.
    -global driver=esp32s3.gpio,property=strap_mode,value=0x04
    # Windows path: this is the Windows QEMU, it cannot see /mnt/c.
    -drive "file=$img_win,if=mtd,format=raw"
)

if ((MONITOR_PORT)); then
    args+=(-monitor "tcp:127.0.0.1:$MONITOR_PORT,server=on,wait=off")
fi

if ((CONSOLE_PORT)); then
    # server=on so the bridge can come and go without restarting the emulator.
    # wait=off boots immediately at the cost of the early log; see --console-wait.
    if ((CONSOLE_WAIT)); then
        serial=(-serial "tcp:127.0.0.1:$CONSOLE_PORT,server=on,wait=on")
    else
        serial=(-serial "tcp:127.0.0.1:$CONSOLE_PORT,server=on,wait=off")
    fi
else
    serial=(-serial stdio)
fi

if [[ "$DISPLAY_MODE" == none ]]; then
    # -nographic would also steal the monitor onto stdio; keep it on TCP and
    # only multiplex the serial port here.
    args+=(-display none "${serial[@]}")
else
    # esp_rgb, the synthetic framebuffer at 0x20000000, renders into this window.
    args+=(-display sdl "${serial[@]}")
fi
((GDB)) && args+=(-s)
((HALT)) && args+=(-S)
args+=("${QEMU_EXTRA[@]+"${QEMU_EXTRA[@]}"}")

echo "==> $QEMU_BIN ${args[*]}"
if ((MONITOR_PORT)); then
    echo "    monitor: scripts/qemu-screenshot.sh out.png"
fi
if ((CONSOLE_PORT)); then
    echo "    console: scripts/qemu-touch.py --port $CONSOLE_PORT"
    if ((CONSOLE_WAIT)); then
        echo "    (held at reset until that connects)"
    fi
fi
if ((GDB)); then
    echo "    gdb: xtensa-esp32s3-elf-gdb.exe -ex 'target remote :1234' $(wslpath -w "$BUILD_DIR/firmware.elf")"
fi

if [[ -n "$LOG" ]]; then
    # Unbuffered through tee so a hang still leaves the log complete on disk; a
    # plain redirect loses everything the moment the process is killed.
    "$QEMU_BIN" "${args[@]}" 2>&1 | tee "$LOG"
else
    "$QEMU_BIN" "${args[@]}"
fi
