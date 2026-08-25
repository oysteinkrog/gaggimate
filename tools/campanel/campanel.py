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
  ./campanel.py scan NAME [-n 180] [-t 3]   dense capture, then score every frame

`scan` is the instrument for a fault that is rare rather than continuous. `grab`
decimates -- it spreads N frames over T seconds -- so at one bad frame in a
hundred it reports clean sheets no matter how many times it is run. `scan`
captures at the full 60 fps instead, so the frames are consecutive and a burst
that lasts one frame cannot fall between samples, and then scores them instead
of asking a person to eyeball two hundred tiles.

The score keys on the one thing the fault reliably does. The UI text is white
and the animations are green, so blue is the channel that separates them, and
white is also what a scan-out slip destroys: RGB565 puts green in bits 5..10, so
content displaced by an odd number of bytes comes back green. Counting bright
blue pixels per row therefore tracks the text and nothing else. Rows are then
compared against their own temporal median across the run rather than against
any absolute level, which is what makes the measure survive the camera's
auto-exposure swinging about 4x between consecutive shots.
"""
import argparse, glob, os, subprocess, sys

OUT = "/mnt/c/work/camshots"
OUT_WIN = r"C:\work\camshots"
FFMPEG = (r"C:\Users\oystein\AppData\Local\Microsoft\WinGet\Packages"
          r"\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe"
          r"\ffmpeg-6.0-full_build\bin\ffmpeg.exe")
CAM = "Logitech StreamCam"

# Fallback panel bounding box inside the 1280x720 frame, used only when the
# automatic locator below cannot find the panel. A fixed box is not safe on its
# own: the rig gets nudged, and a stale box silently crops to bezel and reports
# a clean run no matter what the panel is doing. That happened, and it cost a
# whole capture before the frames were looked at directly.
CROP = (110, 650, 380, 890)  # y0, y1, x0, x1

# Cache so every command in one run locates the panel once.
_located = {}


def locate(name):
    """Find the panel by what moves, not by what is bright.

    Brightness alone locks onto white paper and the monitor behind the bench,
    which is why this used to be hard-coded. Temporal variance does not have
    that problem: over a capture the animation is the only thing in frame that
    changes, so the pixels with real variance ARE the panel. Bezel, desk, paper
    and glare are static and score zero however bright they are.

    Falls back to CROP when too few frames exist to measure variance, or when
    the result is implausibly small.
    """
    from PIL import Image
    import numpy as np
    if name in _located:
        return _located[name]
    fs = frames(name)
    if len(fs) < 12:
        return CROP
    # Skip the ends: the camera ramps its gain at the start of a run and again
    # as ffmpeg finalises, and that ramp is a variance signal across the whole
    # frame, panel or not.
    use = fs[len(fs) // 6: -max(1, len(fs) // 6)]
    st = np.stack([np.array(Image.open(f)).astype(np.float32).mean(axis=2) for f in use[:60]])
    v = st.std(axis=0)
    ys, xs = np.where(v > v.max() * 0.35)
    if ys.size < 500:
        return CROP
    box = (int(ys.min()), int(ys.max()) + 1, int(xs.min()), int(xs.max()) + 1)
    if box[1] - box[0] < 80 or box[3] - box[2] < 80:
        return CROP
    _located[name] = box
    return box


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
    fs = frames(name)
    if not fs:
        sys.exit(f"no frames for {name}")
    y0, y1, x0, x1 = locate(name)
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


def scan(name, top=12):
    """Score every captured frame against the run's own per-row median.

    Returns nothing; prints a ranked table and writes a sheet of the worst
    frames so the ranking can be checked by eye rather than trusted.
    """
    from PIL import Image
    import numpy as np
    fs = frames(name)
    if not fs:
        sys.exit(f"no frames for {name}")
    y0, y1, x0, x1 = locate(name)
    print(f"panel located at y {y0}..{y1}  x {x0}..{x1}")
    rows = []
    kept = []
    for f in fs:
        a = np.array(Image.open(f))[y0:y1, x0:x1].astype(np.int16)
        if (a.mean(axis=2) > 60).sum() < 3000:
            continue  # camera still hunting its gain; carries no information
        # Blue isolates the white UI from the green animation behind it. The
        # threshold is deliberately well above the animations' blue content and
        # well below white, so it does not have to track exposure precisely.
        rows.append((a[:, :, 2] > 110).sum(axis=1))
        kept.append(os.path.basename(f).rsplit("_", 1)[1][:3])
    if len(rows) < 8:
        sys.exit(f"only {len(rows)} usable frames; capture more")
    m = np.stack(rows).astype(np.float64)    # frames x panel rows
    # Normalise each frame to its own total before comparing. Without this the
    # measure tracks the camera: auto-exposure ramps at the start and end of a
    # run move every row's count together, which outscores any real fault by an
    # order of magnitude and fills the ranking with contiguous runs of frames.
    # A scan-out slip does not change how much white there is, it changes which
    # rows hold it, so a shape comparison sees the fault and ignores the gain.
    tot = m.sum(axis=1, keepdims=True)
    m = m / np.maximum(tot, 1.0)
    # Only rows that actually carry text can report anything. Background rows
    # are all zero, and dividing their noise by their own tiny spread manufactures
    # huge scores out of single stray pixels.
    med = np.median(m, axis=0)
    live = med > (med.max() * 0.02)
    spread = np.median(np.abs(m - med), axis=0) + 1e-4
    dev = np.where(live, np.abs(m - med) / spread, 0.0)
    score = dev.max(axis=1)
    order = np.argsort(-score)
    print(f"{len(kept)} frames scored, median score {np.median(score):.2f}")
    print(f"{'frame':>6}{'score':>9}{'worst row':>11}")
    for i in order[:top]:
        print(f"{kept[i]:>6}{score[i]:>9.2f}{int(dev[i].argmax()):>11}")
    hi = float(np.median(score)) * 3.0 + 2.0
    bad = [kept[i] for i in order if score[i] > hi]
    print(f"threshold {hi:.2f} -> {len(bad)} outlier frame(s): {' '.join(bad) if bad else '(none)'}")


def crop_one(name, idx, zoom=2):
    from PIL import Image
    import numpy as np
    y0, y1, x0, x1 = locate(name)
    p = f"{OUT}/{name}_{int(idx):03d}.jpg"
    a = np.array(Image.open(p))[y0:y1, x0:x1]
    im = Image.fromarray(a)
    im = im.resize((im.width * zoom, im.height * zoom), Image.LANCZOS)
    o = f"{OUT}/{name}_{int(idx):03d}_crop.png"
    im.save(o)
    print(o)


ap = argparse.ArgumentParser()
ap.add_argument("cmd", choices=["grab", "sheet", "crop", "scan"])
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
elif a.cmd == "scan":
    # Only capture when there is nothing to re-score, so a run can be analysed
    # again with a different top-N without going back to the camera.
    if not frames(a.name):
        capture(a.name, a.n, a.t)
    scan(a.name)
elif a.cmd == "sheet":
    sheet(a.name, a.c, a.s)
else:
    crop_one(a.name, a.idx)
