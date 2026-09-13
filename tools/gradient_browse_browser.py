# The browser half of the gradient browse check: layout at two widths, Escape,
# where focus lands, and what a real click writes.
#
# tools/gradient_browse_check.mjs proves the markup and the handlers under node.
# It cannot prove that the dialog fits a phone, that Escape closes it or that
# focus comes back, because none of that exists without a browser. This drives
# the real editor in real Chrome over a standalone page with the application's
# own stylesheet.
#
# It is not the device check. The firmware serves the same bundle, so a device
# run still has to happen against firmware built through scripts/build_webui.sh.
#
# Usage, from WSL, with the Windows Playwright venv in C:\work\camshots:
#
#   cd web && npm run build && cd ..
#   node tools/gradient_browse_check.mjs --emit-page /mnt/c/work/camshots/gmbrowse/index.html
#   /mnt/c/work/camshots/pwenv/Scripts/python.exe /mnt/c/work/camshots/pw_run.py \
#       /mnt/c/work/camshots/gmbrowse/log.txt \
#       <this file, as a Windows path> C:\work\camshots\gmbrowse\index.html
#
# pw_run.py is required: a Windows Node process started from WSL1 gets an
# unusable inherited stderr, and the Playwright driver is Node.
#
# Exits 0 on PASS, 1 on any failed check.
import sys
from playwright.sync_api import sync_playwright

HTML = sys.argv[1] if len(sys.argv) > 1 else r"C:\work\camshots\gmbrowse\index.html"
WIDTHS = [("desktop", 1440, 900), ("phone", 390, 844)]

failures = []


def check(what, got, want):
    if got == want:
        print("  ok   %s" % what, flush=True)
    else:
        failures.append(what)
        print("  FAIL %s\n       got  %r\n       want %r" % (what, got, want), flush=True)


def ok(what, cond):
    check(what, bool(cond), True)


def focused(page):
    return page.evaluate(
        "() => { const a = document.activeElement;"
        " return a ? (a.getAttribute('aria-haspopup') || '') + ':' + (a.textContent || '').trim() : 'none'; }"
    )


THEMES = ["light", "dark", "coffee", "nord"]

# The heading colour and the panel colour, both as the browser resolved them,
# composited on a canvas rather than parsed. The class is an opacity modifier,
# so the computed colour is a color-mix() whose serialisation is Chrome's
# business; painting it over the panel colour and reading the pixel back gives
# the sRGB the eye gets, whatever the string said.
PAINT = """
(sel) => {
  const el = document.querySelector(sel);
  const panel = document.querySelector("[role='dialog']");
  const c = document.createElement('canvas');
  c.width = c.height = 1;
  const x = c.getContext('2d', { willReadFrequently: true });
  const paint = v => {
    x.clearRect(0, 0, 1, 1);
    x.fillStyle = getComputedStyle(panel).backgroundColor;
    x.fillRect(0, 0, 1, 1);
    if (v) { x.fillStyle = v; x.fillRect(0, 0, 1, 1); }
    return Array.from(x.getImageData(0, 0, 1, 1).data).slice(0, 3);
  };
  return [paint(null), paint(getComputedStyle(el).color)];
}
"""


def luminance(rgb):
    def ch(v):
        v /= 255.0
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4

    r, g, b = (ch(v) for v in rgb)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast(page, selector):
    bg, fg = page.evaluate(PAINT, selector)
    hi, lo = sorted((luminance(bg), luminance(fg)), reverse=True)
    return (hi + 0.05) / (lo + 0.05)


with sync_playwright() as p:
    browser = p.chromium.launch(channel="chrome", headless=True)
    for label, w, h in WIDTHS:
        print("%s, %dx%d" % (label, w, h), flush=True)
        page = browser.new_page(viewport={"width": w, "height": h})
        errors = []
        page.on("pageerror", lambda e: errors.append("pageerror %s" % e))
        page.on("console", lambda m: errors.append("console %s" % m.text) if m.type == "error" else None)
        page.goto("file:///" + HTML.replace("\\", "/").lstrip("/"))
        page.wait_for_selector("#fields")

        browse = page.locator("button[aria-haspopup='dialog']")
        check("one Browse button", browse.count(), 1)
        # What the page is before the dialog exists, so the comparison below is
        # about the dialog and not about the harness around it.
        wide = "() => document.documentElement.scrollWidth <= document.documentElement.clientWidth"
        ok("the page starts without a sideways scroll", page.evaluate(wide))
        browse.click()
        dialog = page.locator("[role='dialog']")
        ok("the dialog opens", dialog.is_visible())

        # It has to fit the viewport it was opened in.
        ok("the page still does not scroll sideways", page.evaluate(wide))
        box = dialog.bounding_box()
        ok("the dialog fits the width", box["width"] <= w)
        ok("and the height", box["height"] <= h)
        cols = page.evaluate(
            "() => getComputedStyle(document.querySelector(\"[role='dialog'] [role='group'] > div:last-child\"))"
            ".gridTemplateColumns.split(' ').length"
        )
        check("swatches per row", cols, 4 if w >= 1024 else 2)

        # The category headings, in every theme the WebUI ships. Small text, so
        # the floor is 4.5:1. At text-base-content/60 three of the four were
        # under it (gm-nov3.20).
        heading = "[role='dialog'] [role='group'] > div:first-child"
        for theme in THEMES:
            page.evaluate("t => document.documentElement.setAttribute('data-theme', t)", theme)
            ratio = contrast(page, heading)
            ok("category headings reach 4.5:1 on %s (%.2f:1)" % (theme, ratio), ratio >= 4.5)
        page.evaluate("() => document.documentElement.removeAttribute('data-theme')")
        # And the heading belongs to its group: one line, inside the group box.
        fit = page.evaluate(
            "sel => { const h = document.querySelector(sel);"
            " const g = h.parentElement; const a = h.getBoundingClientRect();"
            " const b = g.getBoundingClientRect();"
            " return [h.scrollWidth <= h.clientWidth + 1,"
            " a.left >= b.left - 1 && a.right <= b.right + 1]; }",
            heading,
        )
        ok("a heading fits on one line", fit[0])
        ok("and sits inside its group", fit[1])

        # Every swatch is a real target, and the last one can be reached.
        swatches = page.locator("[role='dialog'] button[data-ref]")
        ok("every gradient has a swatch", swatches.count() >= 18)
        last = swatches.nth(swatches.count() - 1)
        last.scroll_into_view_if_needed()
        lbox = last.bounding_box()
        ok("the last swatch is a usable target", lbox["width"] >= 60 and lbox["height"] >= 40)
        ok(
            "and it sits inside the dialog",
            lbox["x"] >= box["x"] - 1 and lbox["x"] + lbox["width"] <= box["x"] + box["width"] + 1,
        )

        # Opening moves focus into the dialog, at the current gradient.
        check(
            "focus lands on the current gradient",
            page.evaluate("() => document.activeElement.getAttribute('data-ref')"),
            "0",
        )

        # Escape closes it and hands focus back.
        before = page.locator("#fields").inner_text()
        page.keyboard.press("Escape")
        ok("Escape closes the dialog", dialog.count() == 0)
        check("focus returns to the Browse button", focused(page), "dialog:Browse")
        check("and nothing was written", page.locator("#fields").inner_text(), before)

        # The backdrop closes it too, and still writes nothing.
        browse.click()
        page.mouse.click(int(w / 2), 4)
        ok("a click outside closes the dialog", dialog.count() == 0)
        check("still nothing written", page.locator("#fields").inner_text(), before)

        # Choosing one writes the ref and the select follows.
        browse.click()
        page.locator("[role='dialog'] button[data-ref='4']").click()
        ok("choosing closes the dialog", dialog.count() == 0)
        fields = page.locator("#fields").inner_text()
        ok("the ref is written", '"bgAnimGradientRef":"4"' in fields)
        check("the select shows it", page.locator("#bgAnimGradientRef").input_value(), "4")
        check("focus returns after choosing", focused(page), "dialog:Browse")

        check("no page errors", errors, [])
        page.close()
    browser.close()

print("PASS" if not failures else "FAIL (%d)" % len(failures), flush=True)
sys.exit(0 if not failures else 1)
