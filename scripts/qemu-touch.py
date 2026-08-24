#!/usr/bin/env python3
"""Attach to a QEMU console chardev: print the guest log, inject mouse as touch.

QEMU's fork for the ESP32-S3 has no input device, and the panel device is a
bare framebuffer, so host mouse events have nowhere to go through the usual
channels. This bridges them over the console UART instead: it reads the host
mouse position over the QEMU window (scripts/qemu-mouse.ps1) and writes

    ESC 'T' <x> ',' <y> ',' <1|0> '\\n'

records, which src/display/drivers/Qemu/QemuTouch.cpp parses. Guest output
still comes out on stdout, so this replaces the terminal you would otherwise
have attached to the serial port.

    scripts/qemu-run.sh --console-port 55556 &
    scripts/qemu-touch.py --port 55556 --log boot.log
"""

import argparse
import socket
import subprocess
import sys
import threading
from pathlib import Path

PS_SCRIPT = Path(__file__).resolve().parent / "qemu-mouse.ps1"


def windows_path(path: Path) -> str:
    """Translate a WSL path for powershell.exe.

    Handing the Windows binary a path like /mnt/c/work/... makes it look for
    C:\\mnt\\c\\work\\..., which does not exist, and -File then fails. Outside WSL
    there is no wslpath and the path is already native.
    """
    try:
        out = subprocess.run(["wslpath", "-w", str(path)], capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return str(path)
    return out.stdout.strip()


def pump_guest_output(sock: socket.socket, log) -> None:
    """Guest -> terminal. Runs until the emulator goes away."""
    pending = b""
    while True:
        try:
            chunk = sock.recv(4096)
        except OSError:
            break
        if not chunk:
            break
        pending += chunk
        # Write through line by line so a log file is never left with a partial
        # line, and so the terminal keeps up with a chatty guest.
        while b"\n" in pending:
            line, pending = pending.split(b"\n", 1)
            text = line.decode(errors="replace").rstrip("\r")
            print(text, flush=True)
            if log:
                log.write(text + "\n")
                log.flush()
    print("[qemu-touch] console closed", file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=55556, help="console chardev port (default 55556)")
    ap.add_argument("--panel", default="480x480", help="guest panel size, for scaling (default 480x480)")
    ap.add_argument("--log", help="also append the guest output to this file")
    ap.add_argument("--no-mouse", action="store_true", help="just tail the console, inject nothing")
    args = ap.parse_args()

    panel_w, panel_h = (int(v) for v in args.panel.lower().split("x"))

    sock = socket.create_connection(("127.0.0.1", args.port), timeout=10)
    sock.settimeout(None)
    log = open(args.log, "a", encoding="utf-8") if args.log else None

    reader = threading.Thread(target=pump_guest_output, args=(sock, log), daemon=True)
    reader.start()

    if args.no_mouse:
        reader.join()
        return 0

    if not PS_SCRIPT.exists():
        print(f"[qemu-touch] missing {PS_SCRIPT}", file=sys.stderr)
        return 1

    mouse = subprocess.Popen(
        ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", windows_path(PS_SCRIPT)],
        stdout=subprocess.PIPE,
        # Inherited, not swallowed: a poller that cannot start is otherwise
        # indistinguishable from a mouse that is never moved.
        stderr=None,
        text=True,
        bufsize=1,
    )
    print(f"[qemu-touch] injecting mouse over the QEMU window as touch on :{args.port}", file=sys.stderr)

    try:
        for line in mouse.stdout:
            parts = line.split()
            if len(parts) != 5:
                continue
            x, y, down, client_w, client_h = (int(v) for v in parts)
            if client_w <= 0 or client_h <= 0:
                continue
            # The SDL window is normally the panel at 1:1, but it can be
            # resized, so map through the client area rather than assuming it.
            px = min(panel_w - 1, max(0, x * panel_w // client_w))
            py = min(panel_h - 1, max(0, y * panel_h // client_h))
            try:
                sock.sendall(b"\x1bT%d,%d,%d\n" % (px, py, down))
            except OSError:
                break
    except KeyboardInterrupt:
        pass
    finally:
        if mouse.poll() is not None:
            print(f"[qemu-touch] mouse poller exited ({mouse.returncode}); no touch input", file=sys.stderr)
        mouse.terminate()
        sock.close()
        if log:
            log.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
