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


COMPARING THIS AGAINST THE PANEL
================================

Not done, and the design is written down here so the next person does not
invent a worse one. As of 2026-09-12 nothing has compared these strips against
a real panel.

What will not work: grabbing /api/debug/fb while an animation runs and holding
it next to a strip. An animation maps the palette through its own pattern, its
own gain and, for Plasma, a wheel rather than a ramp, so an arbitrary moving
frame has no known relation to a horizontal ramp. A comparison that does not
say which ramp index each pixel came from is not a comparison.

What will work is a controlled fixture, on a bench build only:

  - a probe on /api/debug/anim, under GM_TOUCH_PROBE, that stops the animation
    (the existing animoff=1 path), paints rows 200 to 239 of the framebuffer
    with the currently resolved theme's buildThemeRamp output, column x taking
    ramp index (x * 255) / 479, and reports in its JSON the stops it resolved,
    the two tone percentages and the gain it used;
  - read the region back with /api/debug/fb, average each column over the 40
    rows (they are identical, so a disagreement is itself a finding), and
    compare against this tool's samples generated at the settings the probe
    reported, not at this tool's defaults;
  - pass is every column equal, since both sides are the same integer
    arithmetic on the same stops and the panel stores RGB565.

The matched settings are the whole point: the device applies its own stored
brightness, rolloff and gradient, so the host side has to be told what those
were rather than assuming the defaults. Reading them from the probe's own
report keeps the check away from /api/settings, which returns the WiFi
password in clear text.
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
