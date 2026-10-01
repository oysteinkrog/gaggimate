#!/usr/bin/env python3
"""Scenario for gm-nov3.26: a settings save that carries a malformed field
stores every valid field, refuses the malformed ones, and says which.

Before the fix the handler skipped a field that failed validation and
answered 200 with the settings, so a caller was told its save worked
while the device kept the old value. Now the answer is HTTP 422 with
{"error": "invalid fields", "code": "invalid_fields", "fields": [...]}.

What this checks, each with a direct POST to /api/settings:

- A malformed gradient library (an id of 100000, which the five digit
  parser cannot read), a malformed theme map (the reference "c100000"),
  a schedule list with a 25:00 entry and a 5 character access point
  password, sent with one valid field. The answer is 422 and names the
  four fields; the valid field is stored; the four keep their old values.
- The library and the map each sent alone with a valid field: the same
  422, naming only that field, and the valid field stored.
- A valid library and map: answered 200 and stored, so the refusal is
  about the value and not the field.

Every value it changes is put back with a 200 save at the end.

Simulator only: it POSTs /api/settings by hand, which is never done
against the bench board (CLAUDE.md).

Usage:
    python3 tools/settings_ui_tests/test_reject.py
        [--program PATH/to/.pio/build/display-sim/program]
        [--workdir DIR] [--port N]
"""
import argparse
import json
import os
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, REPO_ROOT)

from tools.settings_ui_tests import Sim  # noqa: E402

DEFAULT_PROGRAM = os.path.join(REPO_ROOT, ".pio", "build", "display-sim", "program")

BAD_LIBRARY = "100000|Bad|ff0000,00ff00"
BAD_MAP = "0;c100000"
BAD_SCHEDULES = "25:00|1111111"
BAD_AP_PASSWORD = "short"
GOOD_LIBRARY = "1|Test|ff0000,0000ff"
GOOD_MAP = "0;c1"

FAILURES = []
TOTAL = 0


def check(rig, name, cond, detail=""):
    global TOTAL
    TOTAL += 1
    rig.log("check", name=name, ok=int(bool(cond)), detail=detail)
    if not cond:
        FAILURES.append((name, detail))
    return cond


def _is_sim(rig):
    return rig.host.split(":", 1)[0] in ("127.0.0.1", "localhost")


def post(rig, fields):
    """POST only the given fields to /api/settings (a partial save, which the
    handler supports) and return (status, parsed JSON body or None)."""
    if not _is_sim(rig):
        raise RuntimeError("test_reject refuses to POST /api/settings against %r (simulator only)" % rig.host)
    data = urllib.parse.urlencode(fields).encode("utf-8")
    req = urllib.request.Request(rig.base + "/api/settings", data=data, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=rig.timeout) as resp:
            status, body = resp.status, resp.read()
    except urllib.error.HTTPError as e:
        status, body = e.code, e.read()
    try:
        return status, json.loads(body)
    except ValueError:
        return status, None


def _brief(body):
    """The refusal's own keys only. A 200 answer is the whole settings
    document, which carries the WiFi password, so it is never logged."""
    if not isinstance(body, dict):
        return repr(body)
    return repr({k: body[k] for k in ("error", "code", "fields") if k in body})


def _fields(body):
    if not isinstance(body, dict) or not isinstance(body.get("fields"), list):
        return None
    return sorted(body["fields"])


def check_refused(rig, name, status, body, expected_fields):
    check(rig, name + "_status_422", status == 422, "status=%r" % status)
    check(rig, name + "_error", isinstance(body, dict) and body.get("error") == "invalid fields", _brief(body))
    check(rig, name + "_fields", _fields(body) == sorted(expected_fields), _brief(body))


def _sequence(rig):
    s0 = rig.settings()
    keys = ("bgAnimGradients", "bgAnimThemeMap", "autowakeupSchedules", "apPassword", "targetSteamTemp")
    before = {k: s0[k] for k in keys}
    steam = int(before["targetSteamTemp"])
    try:
        # All four malformed fields with one valid one.
        status, body = post(rig, {
            "targetSteamTemp": str(steam + 1),
            "bgAnimGradients": BAD_LIBRARY,
            "bgAnimThemeMap": BAD_MAP,
            "autowakeupSchedules": BAD_SCHEDULES,
            "apPassword": BAD_AP_PASSWORD,
        })
        check_refused(rig, "all_four", status, body,
                      ["bgAnimGradients", "bgAnimThemeMap", "autowakeupSchedules", "apPassword"])
        s1 = rig.settings()
        check(rig, "all_four_valid_field_stored", int(s1["targetSteamTemp"]) == steam + 1, repr(s1["targetSteamTemp"]))
        for k in ("bgAnimGradients", "bgAnimThemeMap", "autowakeupSchedules", "apPassword"):
            check(rig, "all_four_kept_" + k, s1[k] == before[k], "%r -> %r" % (before[k], s1[k]))

        # Each gradient field alone behaves the same way.
        for i, (key, bad) in enumerate((("bgAnimGradients", BAD_LIBRARY), ("bgAnimThemeMap", BAD_MAP))):
            status, body = post(rig, {"targetSteamTemp": str(steam + 2 + i), key: bad})
            check_refused(rig, "alone_" + key, status, body, [key])
            s2 = rig.settings()
            check(rig, "alone_%s_valid_field_stored" % key, int(s2["targetSteamTemp"]) == steam + 2 + i,
                  repr(s2["targetSteamTemp"]))
            check(rig, "alone_%s_kept" % key, s2[key] == before[key], "%r -> %r" % (before[key], s2[key]))

        # Valid values for the same two fields are stored and answered 200.
        status, body = post(rig, {"bgAnimGradients": GOOD_LIBRARY, "bgAnimThemeMap": GOOD_MAP})
        check(rig, "valid_status_200", status == 200, "status=%r body=%s" % (status, _brief(body)))
        s3 = rig.settings()
        check(rig, "valid_library_stored", s3["bgAnimGradients"] == GOOD_LIBRARY, repr(s3["bgAnimGradients"]))
        check(rig, "valid_map_stored", s3["bgAnimThemeMap"] == GOOD_MAP, repr(s3["bgAnimThemeMap"]))
    finally:
        restore = {k: str(v) for k, v in before.items()}
        status, body = post(rig, restore)
        after = rig.settings()
        restored = all(str(after[k]) == str(before[k]) for k in keys)
        check(rig, "restored", status == 200 and restored,
              "status=%r %r" % (status, {k: after[k] for k in keys if str(after[k]) != str(before[k])}))


def run(rig, report, venue):
    """Entry point for the end-to-end runner (tools/settings_ui_test.py)."""
    if venue.is_device or not _is_sim(rig):
        report.step("scenario_skipped", scenario="reject", reason="simulator only: posts /api/settings by hand")
        return
    first_fail, first_total = len(FAILURES), TOTAL
    _sequence(rig)
    report.step("scenario_checks", scenario="reject", checks=TOTAL - first_total, failed=len(FAILURES) - first_fail)
    new_failures = FAILURES[first_fail:]
    if new_failures:
        raise AssertionError("; ".join("%s: %s" % (n, d) for n, d in new_failures))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--program", default=DEFAULT_PROGRAM)
    ap.add_argument("--workdir", default=os.path.join(tempfile.gettempdir(), "gm_settings_ui_tests", "test_reject"))
    ap.add_argument("--port", type=int, default=int(os.environ.get("GM_SIM_PORT", "8094")))
    args = ap.parse_args()

    if not os.path.isfile(args.program):
        print("simulator binary not found at %r; build it first: pio run -e display-sim" % args.program, file=sys.stderr)
        return 1
    os.makedirs(args.workdir, exist_ok=True)
    with Sim(args.program, os.path.join(args.workdir, "sim_data"), port=args.port) as sim:
        rig = sim.rig
        rig.log("boot", program=args.program, port=args.port)
        _sequence(rig)

    print("%d checks, %d failed" % (TOTAL, len(FAILURES)))
    for name, detail in FAILURES:
        print("FAIL %s: %s" % (name, detail))
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
