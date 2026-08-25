#!/usr/bin/env python3
"""Capture a burst of webcam frames of the panel and lay them out as a contact sheet.

Why this exists: single photos of an RGB LCD are a bad instrument. The camera's
auto-exposure swings about 4x between consecutive shots of an unchanging image,
and cover-glass glare is fixed in camera coordinates, so any per-frame metric
tracks the camera rather than the panel. Looking at a grid of many frames at once
is what actually separates the two: a real scanout fault moves between frames and
sits in panel coordinates, glare does not.

Run from WSL; it shells out to the Windows ffmpeg for dshow capture.

  ./campanel.py grab NAME [-n 12] [-t 12]   capture and build NAME_sheet.png
  ./campanel.py sheet NAME [-n 12]          rebuild the sheet from existing frames
  ./campanel.py crop NAME IDX               write one full-size crop, for a close look
"""
import argparse, glob, os, subprocess, sys

OUT = "/mnt/c/work/camshots"
OUT_WIN = r"C:\work\camshots"
FFMPEG = (r"C:\Users\oystein\AppData\Local\Microsoft\WinGet\Packages"
          r"\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe"
          r"\ffmpeg-6.0-full_build\bin\ffmpeg.exe")
CAM = "Logitech StreamCam"

# The panel's bounding box inside the 1280x720 frame. Hard-coded rather than
# found by thresholding: an unconstrained brightness search locks onto white
# paper and the monitor behind the bench instead of the panel.
CROP = (110, 650, 380, 890)  # y0, y1, x0, x1


def capture(name, nframes, secs):
    os.makedirs(OUT, exist_ok=True)
    for f in glob.glob(f"{OUT}/{name}_[0-9]*.jpg"):
        os.remove(f)
    # -framerate 60 pins the sensor's integration time to 1/60 s so one photo
    # holds about one panel frame. Left to auto-expose in a dim room the sensor
    # integrates ten frames and ordinary widget motion smears into what looks
    # like corruption.
    rate = f"{nframes}/{secs}"
    # Driven through a generated .bat rather than cmd.exe /c with an inline
    # command string: cmd strips the quoting that dshow's device name needs,
    # and the failure is a bare exit 1 with no diagnostic.
    bat = f"{OUT}/_grab.bat"        # cmd.exe cannot resolve a /mnt/c path
    bat_win = rf"{OUT_WIN}\_grab.bat"
    with open(bat, "w", newline="\r\n") as fh:
        fh.write("@echo off\r\n")
        fh.write(f'"{FFMPEG}" -y -hide_banner -loglevel error -f dshow '
                 f'-framerate 60 -video_size 1280x720 -i video="{CAM}" '
                 f'-t {secs} -vf fps={rate} -q:v 2 '
                 rf'{OUT_WIN}\{name}_%%03d.jpg' + "\r\n")
        fh.write("echo rc=%ERRORLEVEL%\r\n")
    r = subprocess.run(["cmd.exe", "/c", bat_win], capture_output=True, text=True)
    print(r.stdout.strip() or r.stderr.strip())


def frames(name):
    return sorted(glob.glob(f"{OUT}/{name}_[0-9]*.jpg"))


def sheet(name, cols=4, scale=1.0):
    from PIL import Image, ImageDraw
    import numpy as np
    y0, y1, x0, x1 = CROP
    fs = frames(name)
    if not fs:
        sys.exit(f"no frames for {name}")
    tiles = []
    for f in fs:
        a = np.array(Image.open(f))[y0:y1, x0:x1]
        # The camera hunts its gain for the first frames of a run and returns
        # near-black frames; they carry no information and skew the layout.
        if (a.mean(axis=2) > 60).sum() < 3000:
            continue
        tiles.append((os.path.basename(f).rsplit("_", 1)[1][:3], a))
    if not tiles:
        sys.exit("every frame was underexposed")
    th, tw = tiles[0][1].shape[:2]
    tw, th = int(tw * scale), int(th * scale)
    rows = (len(tiles) + cols - 1) // cols
    sh = Image.new("RGB", (cols * tw, rows * th), (20, 20, 20))
    d = ImageDraw.Draw(sh)
    for i, (tag, a) in enumerate(tiles):
        im = Image.fromarray(a).resize((tw, th), Image.LANCZOS)
        px, py = (i % cols) * tw, (i // cols) * th
        sh.paste(im, (px, py))
        d.text((px + 4, py + 4), tag, fill=(255, 255, 0))
    p = f"{OUT}/{name}_sheet.png"
    sh.save(p)
    print(f"{p}  {len(tiles)} frames  {sh.size[0]}x{sh.size[1]}")


def crop_one(name, idx, zoom=2):
    from PIL import Image
    import numpy as np
    y0, y1, x0, x1 = CROP
    p = f"{OUT}/{name}_{int(idx):03d}.jpg"
    a = np.array(Image.open(p))[y0:y1, x0:x1]
    im = Image.fromarray(a)
    im = im.resize((im.width * zoom, im.height * zoom), Image.LANCZOS)
    o = f"{OUT}/{name}_{int(idx):03d}_crop.png"
    im.save(o)
    print(o)


ap = argparse.ArgumentParser()
ap.add_argument("cmd", choices=["grab", "sheet", "crop"])
ap.add_argument("name")
ap.add_argument("idx", nargs="?")
ap.add_argument("-n", type=int, default=12, help="frames to capture")
ap.add_argument("-t", type=int, default=12, help="seconds to capture over")
ap.add_argument("-c", type=int, default=4, help="contact-sheet columns")
ap.add_argument("-s", type=float, default=0.75, help="tile scale")
a = ap.parse_args()
if a.cmd == "grab":
    capture(a.name, a.n, a.t)
    sheet(a.name, a.c, a.s)
elif a.cmd == "sheet":
    sheet(a.name, a.c, a.s)
else:
    crop_one(a.name, a.idx)
