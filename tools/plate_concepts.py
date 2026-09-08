#!/usr/bin/env python3
"""Plate coverage concepts for the brew screen (gm-2cl.10).

The brew screen's plate is the 360 px disc behind the dial (objects.obj2,
theme colour, radius 180). In the Custom plate mode it is translucent, and
every translucent pixel over the animation is one 16-bit alpha blend a
frame on the render task: about 102k pixels and 12 to 13 ms of blend at
60 percent (UI-pipeline invariants, CLAUDE.md). This script renders the
options the owner has to choose between, from the board's own pixels:

  1. Read the brew screen's framebuffer with the plate hidden (the
     board's stored mode). The device serves /api/debug/fb at step 2
     only (240x240), so the renders are half size and the pixel counts
     are scaled to the panel.
  2. Draw each candidate plate over it with the same 565 blend the render
     task does, quantised, masked to the panel circle.
  3. Count the blended pixels and estimate the blend time from the
     measured 0.12 us per pixel.

Writes <out>/plate-<name>.png and <out>/plate-concepts.json. Loadtest
build; leaves the board on the brew screen with the synthetic brew off.

    python3 tools/plate_concepts.py [--host 192.168.1.121] [--out DIR]
"""
import argparse
import json
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "settings_ui_tests"))
from rig import Rig  # noqa: E402

CX, CY = 240, 240
US_PER_PX = 0.12  # brew screen, 106k px in 12.5 ms (2026-09-07, bench board)

# name, radius, opacity percent, note
CONCEPTS = [
    ("disc180-60", 180, 60, "today's Custom plate: the whole dial disc at 60 percent"),
    ("disc140-60", 140, 60, "smaller disc, still behind the dial ring's inner edge"),
    ("disc110-60", 110, 60, "supporting plate behind the numbers only"),
    ("disc180-100", 180, 100, "opaque disc: the copy path, no blend at all"),
    ("none", 0, 0, "no plate (the board's stored setting)"),
]


def read_fb(rig):
    with rig._open("/api/debug/fb?step=2", timeout=90) as resp:  # noqa: SLF001
        size = resp.headers.get("X-FB-Size", "480x480")
        data = resp.read()
    w, h = (int(v) for v in size.split("x"))
    if len(data) != w * h * 2:
        raise RuntimeError("fb: expected %d bytes, got %d" % (w * h * 2, len(data)))
    px = [data[i] | (data[i + 1] << 8) for i in range(0, len(data), 2)]
    return w, h, px


def blend565(dst, src, a):
    """Same arithmetic as SleepAnimation's blend: a in 0..256."""
    if a >= 256:
        return src
    ia = 256 - a
    r = (((src >> 11) & 31) * a + ((dst >> 11) & 31) * ia) >> 8
    g = (((src >> 5) & 63) * a + ((dst >> 5) & 63) * ia) >> 8
    b = ((src & 31) * a + (dst & 31) * ia) >> 8
    return (r << 11) | (g << 5) | b


def to888(v):
    r = (v >> 11) & 31
    g = (v >> 5) & 63
    b = v & 31
    return bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))


def write_png(path, px, w, h):
    rows = []
    for y in range(h):
        row = bytearray([0])
        for x in range(w):
            row += to888(px[y * w + x])
        rows.append(bytes(row))
    raw = b"".join(rows)

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    out += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(out)


def render(base, w, h, radius, opa_pct, color565):
    """Returns the composed pixels and the blended pixel count at panel scale."""
    px = list(base)
    a = (opa_pct * 256 + 50) // 100
    scale = 480.0 / w
    n = 0
    if radius > 0:
        r2 = radius * radius
        for y in range(h):
            dy = (y + 0.5) * scale - CY
            for x in range(w):
                dx = (x + 0.5) * scale - CX
                if dx * dx + dy * dy <= r2:
                    px[y * w + x] = blend565(px[y * w + x], color565, a)
                    if a < 256:
                        n += 1
    return px, int(round(n * scale * scale))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.1.121")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "overlay_footprint"))
    ap.add_argument("--color", default=None, help="plate colour as RRGGBB hex; default reads bgAnimPlateColor")
    args = ap.parse_args()
    rig = Rig(args.host)
    try:
        rig.synth(brew=0)
    except Exception as e:  # noqa: BLE001
        print("synth off:", e, file=sys.stderr)
    rig.anim(interlace=-1)
    for _ in range(3):
        try:
            rig.touchmap(screen=2, load=True)
        except Exception as e:  # noqa: BLE001
            print("load brew:", e, file=sys.stderr)
        import time
        time.sleep(3)
        if rig.touchmap().get("screen_id") == 2:
            break
    color = args.color
    if color is None:
        # Only the one field is read; the settings document is not printed.
        s = rig.get_json("/api/settings")
        v = s.get("bgAnimPlateColor", "#101010")
        c = int(str(v).lstrip("#"), 16) if isinstance(v, str) else int(v)
        color = "%06X" % (c & 0xFFFFFF)
    c24 = int(color, 16)
    color565 = ((c24 >> 19) << 11) | (((c24 >> 10) & 63) << 5) | ((c24 >> 3) & 31)
    w, h, base = read_fb(rig)
    os.makedirs(args.out, exist_ok=True)
    report = {"host": args.host, "plate_color": color, "us_per_px": US_PER_PX, "concepts": []}
    for name, radius, opa, note in CONCEPTS:
        px, n = render(base, w, h, radius, opa, color565)
        path = os.path.join(args.out, "plate-%s.png" % name)
        write_png(path, px, w, h)
        est = n * US_PER_PX / 1000.0
        report["concepts"].append({"name": name, "radius": radius, "opacity_pct": opa, "note": note,
                                   "blended_px": n, "blend_ms_est": round(est, 1), "png": path})
        print("%-12s r=%3d opa=%3d%% blended px %6d  est blend %5.1f ms  %s" % (name, radius, opa, n, est, note))
    with open(os.path.join(args.out, "plate-concepts.json"), "w") as f:
        json.dump(report, f, indent=1)
    print("wrote", os.path.join(args.out, "plate-concepts.json"))


if __name__ == "__main__":
    main()
