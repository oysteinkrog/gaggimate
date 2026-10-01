#!/usr/bin/env python3
"""Contact sheet for the built-in gradients in data/gradients.json.

One PNG with every gradient in the set, grouped in the declared category
order, each strip drawn from the RGB565 palette the panel would actually
build. Made for reviewing the set as a whole: which categories are thin, which
two entries are the same idea twice, whether a ramp really rises.

    python3 tools/gradient_sheet.py                     -> tools/gradient_sheet.png
    python3 tools/gradient_sheet.py --out sheet.png
    python3 tools/gradient_sheet.py --brightness 60 --knee 65
    python3 tools/gradient_sheet.py --mode wheel        (Plasma's cyclic ramp)

The strips are not CSS gradients between six stops. Every column is one entry
of the 256-entry ramp, sampled through web/src/config/gradientRamp.js, which
tools/animbench/web/ramp_parity.js proves equal to the firmware's own C++ entry
by entry. There is no tone curve in this file; see the sampling contract at
the top of tools/gradient_check.py.

At the default 100/100 the tone transform is the identity, so a second strip
to compare raw against toned colours would be the same strip twice. At other
settings the tone is in the heading, in every row label and in the PNG's text
metadata, because a sheet saved at 60/65 and one saved at 100/100 are
different documents and only the file says which is which.


COMPARING THIS AGAINST THE DEVICE'S FRAMEBUFFER
===============================================

Done, on the bench board, 2026-09-13 (gm-nov3.10). The strips above are what
the compositor writes into framebuffer memory.

That is the whole claim (gm-nov3.22). The comparison reads framebuffer memory
through /api/debug/fb, so it is silent about RGB scan-out, panel timing, the
wiring, the controller board and the glass: a match says the right colours were
written, not that they were shown. The pixel-clock divider is not in this path
at all, so the divider a board stores neither qualifies a pass nor explains a
failure.

The command is tools/gradient_fb_check.py:

    python3 tools/gradient_fb_check.py --host 192.168.1.121 \
        --tone 100,100 --tone 60,65 --selftest --json report.json

It arms a bench-only fixture on the device (/api/debug/gradfix, GM_TOUCH_PROBE
builds), reads the framebuffer back through /api/debug/fb, and compares
individual RGB565 samples against this file's own sampler. It exits non-zero on
any mismatch. --batch walks every entry in data/gradients.json instead of the
device's own theme, which is the form gm-nov3.5 needs for an appended set.

What the fixture does: rows 200 to 239 of every band are overwritten with the
active theme's 256-entry ramp, after every compositing stage and before the
push, so no overlay, layer, element or scrim reaches them. Column x carries
ramp index ((x + xoff) * 255) / (w - 1), clamped to 255. Interlacing is vetoed
while it is armed, so both framebuffers carry the strip rather than one.

Why xoff exists: at the default --step 2 the endpoint hands back even columns
only, so a host would never read column 479, where index 255 sits. The fixture
is read twice, at xoff 0 and 1, and the two passes together cover the index of
every column. Measured coverage: 256 of 256 indices. --step 1 reads every
column in one pass and needs no second offset; the recorded runs below predate
that being usable (gm-6ivh) and were taken at step 2.

The settings are read from the fixture's own report, not assumed and not read
from /api/settings, which returns the WiFi password in clear text. The report
carries the stops before tone, whether they are uniform or positional, both
tone percentages with the integer values they converted to, the palette gain
and the sampled region. btone=/ktone= hold a tone against DefaultUI's per-pass
re-apply of the stored one, so a run at 60/65 writes nothing to NVS.

The recorded runs, in tools/gradient_fb_check/:

  report.json, the device's own theme at two tones

    firmware    v1.9.8-sleep9-644-g265501e0, built 2026-09-13T10:09:56Z
    board       192.168.1.121 (the divider it stores, 7, is not in this path)
    gradient    Aurora, the six-stop built-in the board had stored
    tone        100/100 and 60/65, four passes (two tones x xoff 0 and 1)
    samples     38,400 compared, 0 mismatches
    coverage    ramp indices 0 to 255, every one of the 256

  batch_report.json, every built-in through the preview path

    same firmware and board, tone 100/100, 18 gradients x xoff 0 and 1
    samples     172,800 compared, 0 mismatches

The same run's self test is what makes that zero mean something: the same
armed strip compared against a ramp sampled at 50/50 gives 4,800 mismatches,
and the correct ramp compared against the region with the fixture off gives
4,724. A check that cannot fail is not evidence.

This note used to warn that /api/debug/fb starts dropping its response partway
on a board that has been up a while, and that the tool keeps a short read as
long as it reaches the strip's rows. Both are gone. The dropped responses were
the filler returning 0 when the send budget could not hold one more output row,
which the web server reads as the end of the body, and the heap drift blamed
for it was a leak in the same filler that only aborted requests could reach, so
the retry loop written to cope with the short reads was causing it (gm-6ivh,
28cec8ec, 2026-09-13). The endpoint delivers every step whole, and the tool
now fails on a short read instead of sampling it.

"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import gradient_check as gc  # noqa: E402

try:
    from PIL import Image, ImageDraw, ImageFont
    from PIL.PngImagePlugin import PngInfo
except ImportError:
    sys.stderr.write('gradient_sheet: needs Pillow (python3 -m pip install pillow)\n')
    sys.exit(2)

FONT_DIRS = ('/usr/share/fonts/truetype/dejavu',)

# Layout, in pixels.
MARGIN = 24
LABEL_W = 210
STRIP_W = 512           # two columns per ramp entry, so no resampling
STRIP_H = 40
ROW_GAP = 8
CATEGORY_GAP = 22
HEADER_H = 64

BG = (22, 22, 26)
FG = (232, 232, 236)
DIM = (150, 150, 158)
RULE = (60, 60, 68)


def load_font(size, bold=False):
    name = 'DejaVuSans-Bold.ttf' if bold else 'DejaVuSans.ttf'
    for d in FONT_DIRS:
        path = os.path.join(d, name)
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def build(doc, ramps, args):
    categories = doc['categories']
    gradients = list(enumerate(doc['gradients']))
    # Declared category order, then the stored order inside a category, which
    # is the id order and the order both pickers show.
    groups = [(c, [(i, g) for i, g in gradients if g.get('category') == c]) for c in categories]
    orphans = [(i, g) for i, g in gradients if g.get('category') not in categories]
    if orphans:
        groups.append(('(no declared category)', orphans))
    groups = [(c, rows) for c, rows in groups if rows]

    # Same arithmetic the drawing loop below walks, so the last strip is not
    # cut off by a group heading nobody counted.
    height = HEADER_H + MARGIN + MARGIN
    for _, rows in groups:
        height += CATEGORY_GAP + 4 + len(rows) * (STRIP_H + ROW_GAP) + 6
    width = MARGIN + LABEL_W + STRIP_W + MARGIN

    img = Image.new('RGB', (width, height), BG)
    draw = ImageDraw.Draw(img)
    title_font = load_font(19, bold=True)
    cat_font = load_font(14, bold=True)
    name_font = load_font(14)
    small_font = load_font(11)

    tone = 'brightness %d%%, rolloff %d%%, gain %d, %s' % (
        args.brightness, args.knee, args.gain, args.mode)
    draw.text((MARGIN, MARGIN - 6), 'GaggiMate built-in gradients', font=title_font, fill=FG)
    draw.text((MARGIN, MARGIN + 18),
              '%d gradients, %d categories. RGB565 ramp samples at %s.'
              % (len(doc['gradients']), len(categories), tone),
              font=small_font, fill=DIM)

    y = HEADER_H + MARGIN
    for category, rows in groups:
        draw.text((MARGIN, y), category.upper(), font=cat_font, fill=FG)
        draw.line([(MARGIN, y + 17), (width - MARGIN, y + 17)], fill=RULE)
        y += CATEGORY_GAP + 4
        for index, g in rows:
            ramp = ramps[index]
            draw.text((MARGIN, y + 4), g['name'], font=name_font, fill=FG)
            draw.text((MARGIN, y + 22), 'id %d - %s' % (index, g.get('category', '?')),
                      font=small_font, fill=DIM)
            x0 = MARGIN + LABEL_W
            if ramp is None:
                draw.rectangle([x0, y, x0 + STRIP_W - 1, y + STRIP_H - 1], outline=(200, 60, 60))
                draw.text((x0 + 8, y + 12), 'the firmware parser rejects these stops',
                          font=small_font, fill=(230, 120, 120))
            else:
                for k in range(256):
                    rgb = gc.expand_rgb565(ramp[k])
                    draw.rectangle([x0 + k * 2, y, x0 + k * 2 + 1, y + STRIP_H - 1], fill=rgb)
                draw.rectangle([x0, y, x0 + STRIP_W - 1, y + STRIP_H - 1], outline=RULE)
            y += STRIP_H + ROW_GAP
        y += 6
    return img


def main(argv=None):
    ap = argparse.ArgumentParser(description='Contact sheet for data/gradients.json')
    ap.add_argument('--source', default=gc.SOURCE)
    ap.add_argument('--out', default=os.path.join(gc.ROOT, 'tools', 'gradient_sheet.png'),
                    help='output PNG (not checked in; regenerate it after a change)')
    ap.add_argument('--brightness', type=int, default=100,
                    help='animation brightness setting, 0..100 (default 100)')
    ap.add_argument('--knee', type=int, default=100,
                    help='highlight rolloff setting, 0..100 (default 100)')
    ap.add_argument('--gain', type=int, default=256,
                    help="an animation's extra palette gain, 256 for none")
    ap.add_argument('--mode', default='ramp', choices=('ramp', 'reversed', 'wheel'))
    ap.add_argument('--node', default='node')
    args = ap.parse_args(argv)

    if not 0 <= args.brightness <= 100 or not 0 <= args.knee <= 100:
        sys.stderr.write('gradient_sheet: brightness and rolloff are percentages, 0 to 100\n')
        return 2

    with open(args.source, encoding='utf-8') as f:
        doc = json.load(f)
    ramps = gc.sample_ramps(doc['gradients'], brightness_pct=args.brightness,
                            knee_pct=args.knee, gain256=args.gain, mode=args.mode,
                            node=args.node)
    img = build(doc, ramps, args)

    meta = PngInfo()
    meta.add_text('Source', os.path.relpath(args.source, gc.ROOT))
    meta.add_text('Gradients', str(len(doc['gradients'])))
    meta.add_text('BrightnessPct', str(args.brightness))
    meta.add_text('HighlightKneePct', str(args.knee))
    meta.add_text('ExtraGain256', str(args.gain))
    meta.add_text('RampMode', args.mode)
    meta.add_text('Sampler', 'web/src/config/gradientRamp.js via tools/gradient_samples.js')
    img.save(args.out, pnginfo=meta)
    print('wrote %s (%dx%d), %d gradients at brightness %d%%, rolloff %d%%, gain %d, %s'
          % (args.out, img.width, img.height, len(doc['gradients']),
             args.brightness, args.knee, args.gain, args.mode))
    return 0


if __name__ == '__main__':
    sys.exit(main())
