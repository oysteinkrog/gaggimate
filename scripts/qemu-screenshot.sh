#!/usr/bin/env bash
# Capture the emulated panel from a running scripts/qemu-run.sh instance.
#
#   scripts/qemu-screenshot.sh shot.png
#   scripts/qemu-screenshot.sh --port 55556 shot.png
#
# This goes through the QEMU monitor's "screendump", which writes the console
# surface itself. Grabbing the SDL window off the desktop instead returns
# whatever window is stacked on top of it, and returns nothing at all when the
# emulator runs headless -- so this is the only capture path worth having.
set -euo pipefail

PORT=55555
OUT=

while [[ $# -gt 0 ]]; do
    case "$1" in
    --port)
        PORT="$2"
        shift 2
        ;;
    -h | --help)
        sed -n '2,11p' "${BASH_SOURCE[0]}" | sed 's/^# \?//'
        exit 0
        ;;
    *)
        OUT="$1"
        shift
        ;;
    esac
done

[[ -n "$OUT" ]] || {
    echo "usage: $(basename "$0") [--port N] <out.png>" >&2
    exit 2
}

# QEMU runs as a Windows process, so the path it writes to must be a Windows
# path even though everything else here is a WSL path.
mkdir -p "$(dirname "$OUT")"
out_abs="$(cd "$(dirname "$OUT")" && pwd)/$(basename "$OUT")"
# The prebuilt Espressif QEMU is built without libpng, so screendump can only
# write PPM. Dump next to the requested file and convert in-process below.
ppm_abs="$out_abs.ppm"
ppm_win="$(wslpath -w "$ppm_abs")"

python3 - "$PORT" "$ppm_win" "$out_abs" <<'PY'
import socket, struct, sys, time, zlib

port, ppm_win, out_path = int(sys.argv[1]), sys.argv[2], sys.argv[3]

with socket.create_connection(("127.0.0.1", port), timeout=5) as s:
    s.settimeout(5)
    banner = b""
    # Read up to the first prompt so the reply we parse below belongs to our own
    # command and not to the greeting.
    while b"(qemu)" not in banner:
        chunk = s.recv(4096)
        if not chunk:
            break
        banner += chunk
    s.sendall(b'screendump "%s"\n' % ppm_win.replace("\\", "\\\\").encode())
    time.sleep(0.4)
    reply = b""
    deadline = time.time() + 5
    while time.time() < deadline:
        try:
            chunk = s.recv(4096)
        except socket.timeout:
            break
        if not chunk:
            break
        reply += chunk
        if reply.count(b"(qemu)") >= 1:
            break

text = reply.decode(errors="replace")
# The monitor echoes the command back, so only look at what follows it.
tail = text.split("\n", 1)[1] if "\n" in text else text
if "Error" in tail or "error" in tail or "not found" in tail:
    print(tail.strip(), file=sys.stderr)
    sys.exit(1)


def ppm_to_png(ppm: bytes) -> bytes:
    """Minimal P6 -> PNG. Avoids depending on Pillow or an ffmpeg on PATH."""
    fields, pos = [], 2  # skip the "P6" magic
    while len(fields) < 3:
        while ppm[pos : pos + 1].isspace():
            pos += 1
        if ppm[pos : pos + 1] == b"#":
            while ppm[pos : pos + 1] not in (b"\n", b""):
                pos += 1
            continue
        start = pos
        while not ppm[pos : pos + 1].isspace():
            pos += 1
        fields.append(int(ppm[start:pos]))
    width, height, maxval = fields
    if maxval != 255:
        raise SystemExit(f"unexpected PPM maxval {maxval}")
    pixels = ppm[pos + 1 :]

    # PNG scanlines each carry a leading filter byte; 0 means "no filter".
    stride = width * 3
    raw = b"".join(b"\x00" + pixels[y * stride : (y + 1) * stride] for y in range(height))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 6))
        + chunk(b"IEND", b"")
    )


ppm_path = out_path + ".ppm"
with open(ppm_path, "rb") as f:
    ppm = f.read()
if not ppm.startswith(b"P6"):
    raise SystemExit(f"{ppm_path} is not a binary PPM")
with open(out_path, "wb") as f:
    f.write(ppm_to_png(ppm))
os_remove = __import__("os").remove
os_remove(ppm_path)
PY

[[ -s "$out_abs" ]] || {
    echo "screendump produced nothing at $out_abs" >&2
    exit 1
}
echo "$out_abs ($(stat -c%s "$out_abs") bytes)"
