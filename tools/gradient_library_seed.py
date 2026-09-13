# Brings a board's saved-gradient library to a wanted size through the web UI
# (gm-nov3.11), so a device run can exercise the picker with a full library of
# twelve and then put the board back as it was.
#
# It exists because the device checks must not POST to /api/settings by hand
# (CLAUDE.md: the web UI is the only safe writer, and the endpoint returns the
# WiFi password in clear text). Growing the library means clicking "Copy to my
# gradients" on the Display tab; shrinking it means selecting an entry and
# clicking Delete. Both go through the page, and the page's own Save button
# writes.
#
# Growing copies a different built-in each time, so the entries get distinct
# names rather than "X copy copy copy".
#
# Restoring is not guaranteed to reproduce the original string byte for byte:
# ids are handed out by the page and the entries you delete are the ones it
# put there, so the entries that were already stored come back unchanged while
# anything else is gone. The script prints the library before and after; the
# caller compares.
#
# Usage, from WSL, with the Windows Playwright venv in C:\work\camshots:
#
#   /mnt/c/work/camshots/pwenv/Scripts/python.exe C:\work\camshots\pw_run.py \
#       <log> <this file as a Windows path> --host 192.168.1.121 --count 12
#       [--ref 10]
#
# Exits 0 when the library reached the wanted size and the save landed.
import argparse
import json
import sys
import time
import urllib.request

from playwright.sync_api import sync_playwright


def library_string(host):
    with urllib.request.urlopen("http://%s/api/settings" % host, timeout=20) as r:
        doc = json.load(r)
    return doc.get("bgAnimGradients") or "", doc.get("bgAnimGradientRef") or ""


def entries(packed):
    return [e for e in (packed or "").split(";") if e]


def load(page, host):
    page.goto("http://%s/settings/display" % host, wait_until="load", timeout=60000)
    page.wait_for_selector("#bgAnimGradientRef", timeout=30000)
    drawer = page.locator("div.z-9998").first
    if drawer.count() and drawer.is_visible():
        size = page.viewport_size
        page.mouse.click(size["width"] - 8, size["height"] // 2)
        page.wait_for_selector("div.z-9998", state="hidden", timeout=10000)


def lib_options(page):
    """The c<id> values the select offers under My gradients, in page order."""
    return page.eval_on_selector_all(
        "#bgAnimGradientRef optgroup[label='My gradients'] option",
        "els => els.map(e => e.value)",
    )


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default="192.168.1.121")
    ap.add_argument("--count", type=int, required=True, help="how many saved gradients to end with")
    ap.add_argument("--ref", default=None, help="what to leave bgAnimGradientRef set to")
    args = ap.parse_args()

    before, ref_before = library_string(args.host)
    print("library before (%d entries): %s" % (len(entries(before)), before), flush=True)
    print("global ref before: %r" % ref_before, flush=True)

    with sync_playwright() as p:
        browser = p.chromium.launch(channel="chrome", headless=True)
        page = browser.new_context(viewport={"width": 1440, "height": 900}).new_page()
        errors = []
        page.on("pageerror", lambda e: errors.append(str(e)))
        load(page, args.host)

        have = lib_options(page)
        print("the page shows %d saved: %r" % (len(have), have), flush=True)

        # Grow: copy a different built-in each time so the names differ.
        builtins = page.eval_on_selector_all(
            "#bgAnimGradientRef optgroup:not([label='My gradients']) option",
            "els => els.map(e => e.value)",
        )
        i = 0
        while len(lib_options(page)) < args.count:
            page.select_option("#bgAnimGradientRef", builtins[i % len(builtins)])
            i += 1
            btn = page.get_by_role("button", name="Copy to my gradients")
            if btn.count() == 0 or btn.is_disabled():
                print("cannot copy any further (library full or the button is gone)", flush=True)
                break
            btn.click()
            page.wait_for_timeout(150)

        # Shrink: delete from the end of the list.
        while len(lib_options(page)) > args.count:
            last = lib_options(page)[-1]
            page.select_option("#bgAnimGradientRef", last)
            page.wait_for_timeout(150)
            btn = page.get_by_role("button", name="Delete", exact=True)
            if btn.count() == 0:
                print("no Delete button for %s; stopping" % last, flush=True)
                break
            btn.click()
            page.wait_for_timeout(150)

        if args.ref is not None:
            page.select_option("#bgAnimGradientRef", args.ref)

        with page.expect_response(
            lambda r: r.url.endswith("/api/settings") and r.request.method == "POST", timeout=60000
        ) as info:
            page.get_by_role("button", name="Save Settings").click()
        status = info.value.status
        print("save returned %d" % status, flush=True)
        browser.close()

    time.sleep(1.0)
    after, ref_after = library_string(args.host)
    print("library after (%d entries): %s" % (len(entries(after)), after), flush=True)
    print("global ref after: %r" % ref_after, flush=True)
    if errors:
        print("page errors: %r" % errors, flush=True)
    ok = status == 200 and len(entries(after)) == args.count and not errors
    if args.ref is not None:
        ok = ok and ref_after == args.ref
    print("OK" if ok else "NOT OK", flush=True)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
