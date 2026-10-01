#!/usr/bin/env python3
"""Render an icon SVG to an LVGL TRUE_COLOR_ALPHA C array, EEZ Studio's format.

The icons under icons/ are the source of every image in the UI; icons/convert.sh
rasterises them and EEZ Studio turns the PNG into the C files under
src/display/ui/default/eez/images/. That round trip needs Inkscape and the EEZ
GUI, so this is the headless path for adding one more size of an icon that is
already in the tree: ImageMagick rasterises the SVG (checked against the shipped
40x40 play icon: mean alpha error 2 of 255) and this writes the same four
LV_COLOR_DEPTH blocks the studio writes.

    python3 tools/icon_to_lvgl.py icons/play.svg 60 \
        --out src/display/ui/default/images/ui_image_play_60x60.c

Write the result to src/display/ui/default/images/, not into the studio's tree,
and declare it in UiImages.h there. The studio rewrites ../eez/ wholesale on
every export, and its images[] table is indexed by asset number, so an extra
row in it renumbers every asset after it.

The UI draws these icons with img_recolor_opa 255, so only the alpha channel
reaches the panel and the colour bytes are black throughout, as in every image
already in the tree. The colour encodings below follow LVGL 8's converter and
are untested against a colour asset, because there is none here.
"""
import argparse
import os
import subprocess
import sys
import tempfile

try:
    from PIL import Image
except ImportError:  # noqa: BLE001
    sys.exit("needs Pillow: pip install pillow")


def rasterise(svg_path, size):
    """SVG to an RGBA image at size x size, through ImageMagick.

    The density is high before the resize so the path is sampled well above
    the target and downsampled, rather than rendered at 60 px and aliased.
    """
    with tempfile.TemporaryDirectory() as tmp:
        png = os.path.join(tmp, "icon.png")
        subprocess.run(
            ["convert", "-background", "none", "-density", "1200",
             svg_path, "-resize", "%dx%d" % (size, size), png],
            check=True)
        return Image.open(png).convert("RGBA").copy()


def encode(im):
    """The four LV_COLOR_DEPTH blocks of an LV_IMG_CF_TRUE_COLOR_ALPHA map."""
    px = list(im.getdata())
    d8, d16, d16s, d32 = bytearray(), bytearray(), bytearray(), bytearray()
    for r, g, b, a in px:
        d8 += bytes([((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6), a])
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        d16 += bytes([v & 0xFF, v >> 8, a])
        d16s += bytes([v >> 8, v & 0xFF, a])
        d32 += bytes([b, g, r, a])
    return d8, d16, d16s, d32


def emit(name, im):
    upper = name.upper()
    w, h = im.size
    d8, d16, d16s, d32 = encode(im)
    out = []
    add = out.append
    add('#ifdef __has_include\n#if __has_include("lvgl.h")\n'
        "#ifndef LV_LVGL_H_INCLUDE_SIMPLE\n#define LV_LVGL_H_INCLUDE_SIMPLE\n"
        "#endif\n#endif\n#endif\n")
    add("#if defined(LV_LVGL_H_INCLUDE_SIMPLE)\n#include \"lvgl.h\"\n"
        "#else\n#include \"lvgl/lvgl.h\"\n#endif\n")
    add("#ifndef LV_ATTRIBUTE_MEM_ALIGN\n#define LV_ATTRIBUTE_MEM_ALIGN\n#endif\n")
    add("#ifndef LV_ATTRIBUTE_IMG_%s\n#define LV_ATTRIBUTE_IMG_%s\n#endif\n" % (upper, upper))
    add("const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST "
        "LV_ATTRIBUTE_IMG_%s uint8_t %s_map[] = {" % (upper, name))
    # The comment lines, and the absence of one on the 32-bit block, are copied
    # from the studio's own output so a regenerated file diffs cleanly against
    # the ones already in the tree.
    for cond, comment, data in (
            ("LV_COLOR_DEPTH == 1 || LV_COLOR_DEPTH == 8",
             "Pixel format: Alpha 8 bit, Red: 3 bit, Green: 3 bit, Blue: 2 bit", d8),
            ("LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP == 0",
             "Pixel format: Alpha 8 bit, Red: 5 bit, Green: 6 bit, Blue: 5 bit", d16),
            ("LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP != 0",
             "Pixel format: Alpha 8 bit, Red: 5 bit, Green: 6 bit, Blue: 5 bit"
             "  BUT the 2  color bytes are swapped", d16s),
            ("LV_COLOR_DEPTH == 32", None, d32)):
        add("#if %s" % cond)
        if comment is not None:
            add("    /*%s*/" % comment)
        for byte in data:
            add("    0x%02x," % byte)
        add("#endif")
    add("};\n")
    add("const lv_img_dsc_t %s = {" % name)
    add("    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,")
    add("    .header.always_zero = 0,")
    add("    .header.reserved = 0,")
    add("    .header.w = %d," % w)
    add("    .header.h = %d," % h)
    add("    .data_size = %d * LV_IMG_PX_SIZE_ALPHA_BYTE," % (w * h))
    add("    .data = %s_map," % name)
    add("};")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("svg")
    ap.add_argument("size", type=int)
    ap.add_argument("--name", help="C symbol; default img_<stem>_<n>x<n>")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    stem = os.path.splitext(os.path.basename(args.svg))[0].replace("-", "_")
    name = args.name or "img_%s_%dx%d" % (stem, args.size, args.size)
    im = rasterise(args.svg, args.size)
    if im.size != (args.size, args.size):
        # A non-square source keeps its aspect, which would silently change the
        # widget's box; say so rather than emit a header nobody expects.
        print("rendered %dx%d, not %dx%d" % (im.size + (args.size, args.size)))
    with open(args.out, "w", newline="\n") as fh:
        fh.write(emit(name, im))
    print("%s -> %s (%s, %dx%d)" % (args.svg, args.out, name, im.size[0], im.size[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
