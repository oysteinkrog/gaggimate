# The device half of the gradient browse check (gm-nov3.11): the real web UI,
# served by the firmware out of its embedded bundle, driven in real Chrome.
#
# tools/gradient_browse_check.mjs proves the markup and the handlers under node.
# tools/gradient_browse_browser.py proves layout, Escape and focus in a browser,
# but over a standalone page emitted from the component, so it says nothing
# about the bundle a board actually serves. This script is the missing half: it
# loads http://<board>/settings/display, drives both gradient editors on it,
# opens the browse dialog from each, saves through the page's own Save button
# and reads the stored ref back from GET /api/settings.
#
# It never POSTs to /api/settings. Every write goes through the page, which is
# the only safe writer (CLAUDE.md: the endpoint returns the WiFi password in
# clear text and must not be written by hand).
#
# What it covers, at 1440x900 and 390x844:
#   * the full editor (the global one) and the collapsed per-animation selector
#   * the browse dialog from both, with a non-empty library and with an empty
#     one (emptied in the form only, never saved)
#   * the declared category order, and one built-in from every category saved
#     and read back as its exact decimal ref
#   * saved gradients chosen through the select and through the dialog, saved
#     and read back as their exact c<id> ref
#   * cancelling by Close, Escape and backdrop: no selection, focus back on the
#     Browse button, and the editor keeps the preview it owned
#   * the last swatch reachable at both widths, and zero page errors
#
# Usage, from WSL, with the Windows Playwright venv in C:\work\camshots:
#
#   /mnt/c/work/camshots/pwenv/Scripts/python.exe /mnt/c/work/camshots/pw_run.py \
#       C:\work\camshots\gmdev\log.txt \
#       <this file as a Windows path> --host 192.168.1.121 \
#       --gradients <data/gradients.json as a Windows path>
#
# pw_run.py is required: a Windows Node process started from WSL1 gets an
# unusable inherited stderr, and the Playwright driver is Node.
#
# Exits 0 on PASS, 1 on any failed check.
import argparse
import json
import time
import urllib.request

from playwright.sync_api import sync_playwright

WIDTHS = [("desktop", 1440, 900), ("phone", 390, 844)]

failures = []
total = 0


def check(what, got, want):
    global total
    total += 1
    if got == want:
        print("  ok   %s" % what, flush=True)
    else:
        failures.append(what)
        print("  FAIL %s\n       got  %r\n       want %r" % (what, got, want), flush=True)


def ok(what, cond, detail=""):
    global total
    total += 1
    if cond:
        print("  ok   %s" % what, flush=True)
    else:
        failures.append(what)
        print("  FAIL %s %s" % (what, detail), flush=True)


def settings(host):
    """GET /api/settings, reduced to the gradient fields. The rest of the
    document is never read and never printed."""
    with urllib.request.urlopen("http://%s/api/settings" % host, timeout=20) as r:
        doc = json.load(r)
    return {
        k: doc.get(k)
        for k in ("bgAnimGradientRef", "bgAnimTheme", "bgAnimCustomTheme", "bgAnimThemeMap",
                  "bgAnimGradients", "bgAnimId")
    }


def map_slot(theme_map, anim_id):
    parts = (theme_map or "").split(";")
    return parts[anim_id] if anim_id < len(parts) else ""


# ---------------------------------------------------------------------------
# Page helpers


def mark_browse(page, select_id):
    """Tags the Browse button that belongs to one editor, so the two editors
    on the Display tab can be told apart by their select's id."""
    return page.evaluate(
        """(id) => {
             const sel = document.getElementById(id);
             if (!sel) return false;
             const root = sel.closest('div.rounded-lg');
             const btn = root && root.querySelector("button[aria-haspopup='dialog']");
             if (!btn) return false;
             btn.setAttribute('data-gm', id);
             return true;
           }""",
        select_id,
    )


def groups(page):
    return page.eval_on_selector_all(
        "[role='dialog'] [role='group']", "els => els.map(e => e.getAttribute('aria-label'))"
    )


def refs_in_group(page, label):
    return page.eval_on_selector_all(
        "[role='dialog'] [role='group'][aria-label=%s] button[data-ref]" % json.dumps(label),
        "els => els.map(e => e.getAttribute('data-ref'))",
    )


def open_dialog(page, select_id):
    # Re-tagged every time: choosing a gradient re-renders the editor, and the
    # tag is this script's own attribute rather than something the app keeps.
    mark_browse(page, select_id)
    page.locator("button[data-gm='%s']" % select_id).click()
    page.wait_for_selector("[role='dialog']", timeout=10000)
    # The dialog's node exists as soon as the render commits, but its two
    # effects (the Escape listener, then the focus move) run in the flush
    # after it. A key press sent in that gap is delivered before the listener
    # exists and looks exactly like Escape not working, which is what the
    # first run of this script reported. Waiting for focus to land inside the
    # dialog waits for that flush, because the focus effect is in it.
    page.wait_for_function(
        "() => document.activeElement && document.activeElement.hasAttribute('data-ref')",
        timeout=10000,
    )


def dialog_gone(page):
    return page.locator("[role='dialog']").count() == 0


def focused_label(page):
    return page.evaluate(
        "() => { const a = document.activeElement;"
        " return a ? (a.getAttribute('data-gm') || a.getAttribute('data-ref') ||"
        " (a.textContent || '').trim()) : 'none'; }"
    )


def load_display_tab(page, host, anim_select_id=None):
    page.goto("http://%s/settings/display" % host, wait_until="load", timeout=60000)
    page.wait_for_selector("#bgAnimGradientRef", timeout=30000)
    # Below the md breakpoint the navigation drawer starts open and its
    # backdrop covers the page, so a phone-width run has to dismiss it first,
    # which is what a finger does. Nothing on this page is reachable until it
    # is gone.
    # It is in the DOM at every width (the class `md:hidden` is what hides it
    # on a desktop), so ask whether it is visible, not whether it exists.
    # The backdrop covers the viewport, but the 290 px drawer sits on top of
    # its left half, so the dismissing tap has to land to the right of it.
    drawer = page.locator("div.z-9998").first
    if drawer.count() and drawer.is_visible():
        size = page.viewport_size
        page.mouse.click(size["width"] - 8, size["height"] // 2)
        page.wait_for_selector("div.z-9998", state="hidden", timeout=10000)
    ok("global editor present", mark_browse(page, "bgAnimGradientRef"))
    if anim_select_id:
        page.wait_for_selector("#%s" % anim_select_id, timeout=15000)
        ok("per-animation editor present (%s)" % anim_select_id, mark_browse(page, anim_select_id))


def save(page, host, timeout=60000):
    """Clicks the page's own Save button and waits for its POST. Returns the
    status."""
    with page.expect_response(
        lambda r: r.url.endswith("/api/settings") and r.request.method == "POST", timeout=timeout
    ) as info:
        page.get_by_role("button", name="Save Settings").click()
    return info.value.status


# ---------------------------------------------------------------------------


def run(args):
    board = settings(args.host)
    anim_id = int(board["bgAnimId"])
    anim_select = "bgAnimGradientRef-%d" % anim_id
    data = json.load(open(args.gradients, "r", encoding="utf-8"))
    categories = data["categories"]
    cat_of = {}
    names = []
    for idx, g in enumerate(data["gradients"]):
        cat_of.setdefault(g["category"], []).append(str(idx))
        names.append(g["name"])
    declared = [c for c in categories if c in cat_of]
    print("board: anim=%d (%s slot %r), global ref %r, library %r"
          % (anim_id, anim_select, map_slot(board["bgAnimThemeMap"], anim_id),
             board["bgAnimGradientRef"], board["bgAnimGradients"]), flush=True)

    lib_ids = [e.split("|")[0] for e in (board["bgAnimGradients"] or "").split(";") if e]
    ok("the board has saved gradients to browse", len(lib_ids) >= 2, "ids=%r" % lib_ids)

    with sync_playwright() as p:
        browser = p.chromium.launch(channel="chrome", headless=True)
        for label, w, h in WIDTHS:
            print("\n%s, %dx%d" % (label, w, h), flush=True)
            ctx = browser.new_context(viewport={"width": w, "height": h})
            page = ctx.new_page()
            errors = []
            page.on("pageerror", lambda e: errors.append("pageerror %s" % e))
            page.on("console", lambda m: errors.append("console %s" % m.text) if m.type == "error" else None)
            page.on("requestfailed", lambda r: errors.append("requestfailed %s %s" % (r.url, r.failure)))
            # Preview ownership is a module-level singleton in the page, so no
            # attribute reports it. What it does is observable: the owning
            # editor keeps sending req:bganim:preview on a 5 s keepalive, and
            # whoever gives ownership up sends req:bganim:preview-end. Both
            # editors here preview the same animation index, so the frames
            # cannot say which editor owns it; what they can say is that
            # cancelling the dialog neither ends the preview nor restarts it,
            # which is the rule the dialog is mounted inside the editor for.
            sent = []

            def on_frame(payload):
                sent.append(payload if isinstance(payload, str) else payload.decode("utf-8", "replace"))

            page.on("websocket", lambda ws: ws.on("framesent", on_frame))
            load_display_tab(page, args.host, anim_select)

            wide = "() => document.documentElement.scrollWidth <= document.documentElement.clientWidth + 1"
            ok("the tab starts without a sideways scroll", page.evaluate(wide))

            # ---- the full editor's dialog, non-empty library -------------
            open_dialog(page, "bgAnimGradientRef")
            dialog = page.locator("[role='dialog']")
            ok("the dialog opens from the full editor", dialog.is_visible())
            ok("the page still does not scroll sideways", page.evaluate(wide))
            box = dialog.bounding_box()
            ok("the dialog fits the width", box["width"] <= w, "%r vs %r" % (box["width"], w))
            ok("and the height", box["height"] <= h, "%r vs %r" % (box["height"], h))
            cols = page.evaluate(
                "() => getComputedStyle(document.querySelector(\"[role='dialog'] [role='group'] > div:last-child\"))"
                ".gridTemplateColumns.split(' ').length"
            )
            check("swatches per row", cols, 4 if w >= 1024 else 2)
            check("group order, non-empty library", groups(page), ["My gradients"] + declared)
            for cat in declared:
                check("category %r holds its declared built-ins" % cat, refs_in_group(page, cat), cat_of[cat])
            check("My gradients holds the saved ones", refs_in_group(page, "My gradients"),
                  ["c%s" % i for i in lib_ids])
            check("focus lands on the gradient in force",
                  page.evaluate("() => document.activeElement.getAttribute('data-ref')"),
                  board["bgAnimGradientRef"])

            # The last swatch of the last group, at this width.
            swatches = page.locator("[role='dialog'] button[data-ref]")
            count = swatches.count()
            ok("every gradient has a swatch", count == len(names) + len(lib_ids),
               "%d swatches, %d built-ins + %d saved" % (count, len(names), len(lib_ids)))
            last = swatches.nth(count - 1)
            last.scroll_into_view_if_needed()
            lbox = last.bounding_box()
            ok("the last swatch is a usable target", lbox["width"] >= 60 and lbox["height"] >= 40, repr(lbox))
            ok("and it sits inside the dialog",
               lbox["x"] >= box["x"] - 1 and lbox["x"] + lbox["width"] <= box["x"] + box["width"] + 1,
               "%r in %r" % (lbox, box))

            # ---- cancelling three ways -----------------------------------
            before_sel = page.locator("#bgAnimGradientRef").input_value()
            page.get_by_role("button", name="Close").click()
            ok("Close closes the dialog", dialog_gone(page))
            check("Close returns focus to Browse", focused_label(page), "bgAnimGradientRef")
            check("Close wrote nothing", page.locator("#bgAnimGradientRef").input_value(), before_sel)

            open_dialog(page, "bgAnimGradientRef")
            page.keyboard.press("Escape")
            ok("Escape closes the dialog", dialog_gone(page))
            check("Escape returns focus to Browse", focused_label(page), "bgAnimGradientRef")
            check("Escape wrote nothing", page.locator("#bgAnimGradientRef").input_value(), before_sel)

            open_dialog(page, "bgAnimGradientRef")
            page.mouse.click(int(w / 2), 4)
            ok("a click on the backdrop closes the dialog", dialog_gone(page))
            check("the backdrop wrote nothing", page.locator("#bgAnimGradientRef").input_value(), before_sel)

            # One keepalive period past the three cancels: the preview stream
            # is still running and nothing ended it.
            mark = len(sent)
            page.wait_for_timeout(6000)
            after = [f for f in sent[mark:] if "bganim:preview" in f]
            ok("the preview keeps running after the cancels",
               any('"req:bganim:preview"' in f for f in after), repr(after[:3]))
            ok("no cancel ended the preview",
               not any("preview-end" in f for f in sent), "%d frames" % len(sent))
            # Cancelling must not have handed the preview to the other editor:
            # the dialog lives inside its editor's capture handlers, so the
            # editor that owned the preview still owns it. Observable here as
            # the other editor still being collapsed and unchanged.
            check("the per-animation editor is untouched by the cancels",
                  page.locator("#%s" % anim_select).input_value(), "")

            # ---- the collapsed per-animation selector --------------------
            ok("the per-animation editor is the collapsed form",
               page.locator("#%s" % anim_select).input_value() == "")
            open_dialog(page, anim_select)
            check("group order from the collapsed selector", groups(page), ["Global", "My gradients"] + declared)
            check("the Global group offers the empty ref", refs_in_group(page, "Global"), [""])
            check("focus lands on Global", page.evaluate("() => document.activeElement.getAttribute('data-ref')"), "")
            page.keyboard.press("Escape")
            ok("Escape closes it here too", dialog_gone(page))
            check("focus returns to this editor's Browse", focused_label(page), anim_select)

            # ---- the dialog with an empty library ------------------------
            # Emptied in the form only: the Delete button is a setField, and
            # this page is never saved afterwards. The stored library is read
            # back at the end of the run to prove it.
            page.select_option("#bgAnimGradientRef", "c%s" % lib_ids[0])
            page.get_by_role("button", name="Delete", exact=True).click()
            page.select_option("#bgAnimGradientRef", "c%s" % lib_ids[1])
            page.get_by_role("button", name="Delete", exact=True).click()
            ok("the library is empty in the form",
               page.eval_on_selector_all("#bgAnimGradientRef optgroup",
                                         "els => els.map(e => e.label)").count("My gradients") == 0)
            mark_browse(page, "bgAnimGradientRef")
            open_dialog(page, "bgAnimGradientRef")
            check("group order, empty library", groups(page), declared)
            ok("no saved-gradient group is drawn", "My gradients" not in groups(page))
            last = page.locator("[role='dialog'] button[data-ref]").last
            last.scroll_into_view_if_needed()
            lbox = last.bounding_box()
            ok("the last swatch is still reachable", lbox["width"] >= 60 and lbox["height"] >= 40, repr(lbox))
            page.keyboard.press("Escape")

            check("no page errors", errors, [])
            ctx.close()

        # ---- saving, at the desktop width ------------------------------
        # One built-in from every declared category, then a saved gradient
        # through the select and another through the dialog. Each one is
        # saved, the page reloaded, and the stored ref read back.
        print("\nsaving round trips (1440x900)", flush=True)
        ctx = browser.new_context(viewport={"width": 1440, "height": 900})
        page = ctx.new_page()
        errors = []
        page.on("pageerror", lambda e: errors.append("pageerror %s" % e))
        page.on("console", lambda m: errors.append("console %s" % m.text) if m.type == "error" else None)

        for cat in declared:
            want = cat_of[cat][0]
            load_display_tab(page, args.host)
            open_dialog(page, "bgAnimGradientRef")
            page.locator("[role='dialog'] button[data-ref='%s']" % want).click()
            ok("choosing from %r closes the dialog" % cat, dialog_gone(page))
            check("the select follows the dialog (%s)" % cat,
                  page.locator("#bgAnimGradientRef").input_value(), want)
            check("saving %s returns 200" % names[int(want)], save(page, args.host), 200)
            time.sleep(1.0)
            got = settings(args.host)
            check("stored ref after %s (%s)" % (names[int(want)], cat), got["bgAnimGradientRef"], want)
            check("the built-in is mirrored into bgAnimTheme (%s)" % cat, str(got["bgAnimTheme"]), want)
            check("and bgAnimCustomTheme is cleared (%s)" % cat, got["bgAnimCustomTheme"], "")

        # A saved gradient through the select, on the global ref.
        load_display_tab(page, args.host)
        page.select_option("#bgAnimGradientRef", "c%s" % lib_ids[0])
        check("saving a saved gradient returns 200", save(page, args.host), 200)
        time.sleep(1.0)
        got = settings(args.host)
        check("stored ref is the c<id> from the select", got["bgAnimGradientRef"], "c%s" % lib_ids[0])

        # A saved gradient through the dialog, on the per-animation slot.
        load_display_tab(page, args.host, anim_select)
        open_dialog(page, anim_select)
        page.locator("[role='dialog'] button[data-ref='c%s']" % lib_ids[1]).click()
        ok("choosing a saved gradient closes the dialog", dialog_gone(page))
        check("the per-animation select follows",
              page.locator("#%s" % anim_select).input_value(), "c%s" % lib_ids[1])
        check("saving the per-animation slot returns 200", save(page, args.host), 200)
        time.sleep(1.0)
        got = settings(args.host)
        check("stored map slot is the c<id> from the dialog",
              map_slot(got["bgAnimThemeMap"], anim_id), "c%s" % lib_ids[1])
        check("the library survived the run", got["bgAnimGradients"], board["bgAnimGradients"])

        # ---- put both back ---------------------------------------------
        load_display_tab(page, args.host, anim_select)
        page.select_option("#bgAnimGradientRef", board["bgAnimGradientRef"])
        page.select_option("#%s" % anim_select, map_slot(board["bgAnimThemeMap"], anim_id))
        check("restoring returns 200", save(page, args.host), 200)
        time.sleep(1.0)
        got = settings(args.host)
        check("global ref restored", got["bgAnimGradientRef"], board["bgAnimGradientRef"])
        check("bgAnimTheme restored", str(got["bgAnimTheme"]), str(board["bgAnimTheme"]))
        check("bgAnimCustomTheme restored", got["bgAnimCustomTheme"], board["bgAnimCustomTheme"])
        check("the animation's map slot restored",
              map_slot(got["bgAnimThemeMap"], anim_id), map_slot(board["bgAnimThemeMap"], anim_id))
        check("the whole map restored", got["bgAnimThemeMap"], board["bgAnimThemeMap"])
        check("the library restored", got["bgAnimGradients"], board["bgAnimGradients"])
        check("no page errors while saving", errors, [])
        ctx.close()
        browser.close()

    print("\n%s (%d checks, %d failed)" % ("PASS" if not failures else "FAIL", total, len(failures)),
          flush=True)
    for name in failures:
        print("  failed: %s" % name, flush=True)
    return 0 if not failures else 1


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default="192.168.1.121")
    ap.add_argument("--gradients", required=True, help="path to data/gradients.json")
    raise SystemExit(run(ap.parse_args()))
