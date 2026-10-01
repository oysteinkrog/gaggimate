# Sets one settings field on a board through its own web UI and saves
# (gm-nov3.11).
#
# The device checks must not POST to /api/settings by hand: the endpoint
# returns the WiFi password in clear text and the web UI is the only safe
# writer (CLAUDE.md). Some device checks still need a stored value changed for
# the duration of a run, the standby timeout being the case this was written
# for: a real standby transition cannot be waited out at the 900 s the bench
# board stores.
#
# It types into the input with the given id on the given settings tab, clicks
# Save Settings, and reads the value back from GET /api/settings. It prints the
# value before and after, so the caller can put it back with a second run.
#
# Usage, from WSL, with the Windows Playwright venv in C:\work\camshots:
#
#   /mnt/c/work/camshots/pwenv/Scripts/python.exe C:\work\camshots\pw_run.py \
#       <log> <this file as a Windows path> --host 192.168.1.121 \
#       --tab general --field standbyTimeout --value 60
#
# Exits 0 when the field reads back as the wanted value.
import argparse
import json
import sys
import time
import urllib.request

from playwright.sync_api import sync_playwright


def read_field(host, field):
    with urllib.request.urlopen("http://%s/api/settings" % host, timeout=20) as r:
        return json.load(r).get(field)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default="192.168.1.121")
    ap.add_argument("--tab", default="general", help="the settings tab the field lives on")
    ap.add_argument("--field", required=True)
    ap.add_argument("--value", required=True)
    args = ap.parse_args()

    before = read_field(args.host, args.field)
    print("%s before: %r" % (args.field, before), flush=True)

    with sync_playwright() as p:
        browser = p.chromium.launch(channel="chrome", headless=True)
        page = browser.new_context(viewport={"width": 1440, "height": 900}).new_page()
        errors = []
        page.on("pageerror", lambda e: errors.append(str(e)))
        page.goto("http://%s/settings/%s" % (args.host, args.tab), wait_until="load", timeout=60000)
        page.wait_for_selector("#%s" % args.field, timeout=30000)
        page.fill("#%s" % args.field, args.value)
        with page.expect_response(
            lambda r: r.url.endswith("/api/settings") and r.request.method == "POST", timeout=60000
        ) as info:
            page.get_by_role("button", name="Save Settings").click()
        status = info.value.status
        print("save returned %d" % status, flush=True)
        browser.close()

    time.sleep(1.0)
    after = read_field(args.host, args.field)
    print("%s after: %r" % (args.field, after), flush=True)
    if errors:
        print("page errors: %r" % errors, flush=True)
    ok = status == 200 and str(after) == str(args.value) and not errors
    print("OK" if ok else "NOT OK", flush=True)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
