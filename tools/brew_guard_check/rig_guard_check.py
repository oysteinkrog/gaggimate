#!/usr/bin/env python3
"""Host check for the settings runner's gm-warz input guards.

Drives tools/settings_ui_tests/rig.py and the runner's controller-link
preflight against a fake device on 127.0.0.1, because the simulator has no
controller and its gate always reads false, so no simulator run can show a
refusal. Run from the repo root (`make -C tools/brew_guard_check check` does).
A non-zero exit names the failed case.

What it proves: a 409 naming controller_linked halts the rig and is never
retried; a halted rig sends nothing more; with guard_cover on, a cover that
closed under the run halts before the tap is sent; allow_closed and the
simulator default (guard off) still tap; the runner refuses a device that
reports a link, or that cannot say.
"""

import json
import os
import sys
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(os.path.dirname(HERE)))

import tools.settings_ui_test as runner  # noqa: E402
from tools.settings_ui_tests import Rig, RigHalt  # noqa: E402

LINKED_BODY = {"error": "controller_linked", "reason": "test"}


class Fake:
    """The device's state, and a log of every request path it answered."""

    def __init__(self):
        self.linked = False
        self.cover_open = True
        self.report_link_field = True
        self.wifi = None  # dict for /api/debug/wifi, or None for absent
        self.busy_left = 0
        self.paths = []
        self.lock = threading.Lock()

    def taps(self):
        return [p for p in self.paths if p.startswith("/api/debug/tap?")]


def make_handler(fake):
    class H(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def reply(self, code, obj):
            body = json.dumps(obj).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            with fake.lock:
                fake.paths.append(self.path)
            u = urlparse(self.path)
            if u.path == "/api/debug/tap":
                if u.query:
                    if fake.linked:
                        return self.reply(409, LINKED_BODY)
                    return self.reply(200, {"queued": True})
                return self.reply(200, {"active": False, "remaining_ms": 0, "pressed_at_ms": 1, "released_at_ms": 2})
            if u.path == "/api/debug/settingsui":
                if u.query:
                    if fake.linked:
                        return self.reply(409, LINKED_BODY)
                    if fake.busy_left > 0:
                        fake.busy_left -= 1
                        return self.reply(409, {"error": "busy"})
                    return self.reply(200, {"seq": 1, "accepted": True})
                st = {"seq": 1, "open": fake.cover_open, "depth": 1}
                if fake.report_link_field:
                    st["controller_linked"] = fake.linked
                return self.reply(200, st)
            if u.path == "/api/debug/wifi" and fake.wifi is not None:
                return self.reply(200, fake.wifi)
            self.send_response(404)
            self.end_headers()

    return H


class Server:
    def __enter__(self):
        self.fake = Fake()
        self.httpd = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(self.fake))
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()
        self.rig = Rig("127.0.0.1:%d" % self.httpd.server_address[1], timeout=5)
        return self

    def __exit__(self, *a):
        self.httpd.shutdown()
        self.httpd.server_close()


FAILURES = []
CHECKS = [0]


def check(name, cond, detail=""):
    CHECKS[0] += 1
    if not cond:
        FAILURES.append(name)
        print("FAIL %s %s" % (name, detail), file=sys.stderr)


def raises_halt(fn):
    try:
        fn()
    except RigHalt as e:
        return str(e)
    return None


def case_tap_409_halts_and_latches():
    with Server() as s:
        s.fake.linked = True
        check("tap_409_halts", raises_halt(lambda: s.rig.tap(10, 10)) is not None)
        sent = len(s.fake.paths)
        s.fake.linked = False  # even if the link goes away, the rig stays halted
        check("halt_latched_tap", raises_halt(lambda: s.rig.tap(10, 10)) is not None)
        check("halt_latched_swipe", raises_halt(lambda: s.rig.swipe(1, 1, 2, 2)) is not None)
        check("halt_latched_settingsui", raises_halt(lambda: s.rig.settingsui(open=1)) is not None)
        check("halted_rig_sends_nothing", len(s.fake.paths) == sent, str(s.fake.paths[sent:]))


def case_settingsui_409_linked_not_retried():
    with Server() as s:
        s.fake.linked = True
        check("settingsui_409_halts", raises_halt(lambda: s.rig.settingsui(open=1)) is not None)
        cmds = [p for p in s.fake.paths if p.startswith("/api/debug/settingsui?")]
        check("settingsui_409_sent_once", len(cmds) == 1, str(cmds))


def case_settingsui_busy_still_retried():
    with Server() as s:
        s.fake.busy_left = 2
        reply = s.rig.settingsui(open=1)
        check("busy_retried_then_accepted", reply.get("accepted") is True, str(reply))


def case_state_read_halts_on_link():
    with Server() as s:
        s.fake.linked = True
        check("state_read_halts", raises_halt(s.rig.settingsui_state) is not None)


def case_cover_guard():
    with Server() as s:
        s.rig.guard_cover = True
        s.rig.tap(100, 100)  # cover open: tap goes through and records "open"
        check("guard_open_tap_sent", len(s.fake.taps()) == 1)
        s.fake.cover_open = False  # the cover closes under the run
        msg = raises_halt(lambda: s.rig.tap(100, 100))
        check("guard_closed_halts", msg is not None and "cover closed" in msg, str(msg))
        check("guard_closed_tap_not_sent", len(s.fake.taps()) == 1, str(s.fake.taps()))


def case_cover_guard_allow_closed():
    with Server() as s:
        s.rig.guard_cover = True
        s.rig.tap(100, 100)
        s.fake.cover_open = False
        s.rig.tap(100, 100, allow_closed=True)
        check("allow_closed_tap_sent", len(s.fake.taps()) == 2)
        s.rig.tap(100, 100)  # closed was last seen: no transition, no halt
        check("closed_to_closed_tap_sent", len(s.fake.taps()) == 3)


def case_cover_guard_after_close_command():
    with Server() as s:
        s.rig.guard_cover = True
        s.rig.tap(100, 100)
        s.rig.settingsui(close=1)  # the run closed it on purpose
        s.fake.cover_open = False
        s.rig.tap(100, 100)
        check("tap_after_close_command_sent", len(s.fake.taps()) == 2)


def case_guard_off_by_default():
    with Server() as s:
        s.rig.tap(100, 100)
        s.fake.cover_open = False
        s.rig.tap(100, 100)
        check("sim_default_no_state_reads",
              not any(p == "/api/debug/settingsui" for p in s.fake.paths), str(s.fake.paths))
        check("sim_default_taps_sent", len(s.fake.taps()) == 2)


class StubVenue:
    is_device = True


def refuse(fake_setup):
    with Server() as s:
        fake_setup(s.fake)
        with tempfile.TemporaryDirectory() as d:
            report = runner.Report(d)
            code = runner.refuse_if_controller_linked(s.rig, report, StubVenue())
            return code, [v.get("kind") for v in report.violations], s.fake


def case_runner_refuses():
    code, kinds, fake = refuse(lambda f: setattr(f, "linked", True))
    check("runner_refuses_linked", code == 1 and "CONTROLLER LINKED" in kinds, "%s %s" % (code, kinds))
    check("runner_refusal_sends_no_input", not fake.taps())

    code, kinds, _ = refuse(lambda f: None)
    check("runner_runs_unlinked", code is None, "%s %s" % (code, kinds))

    def old_build(f):
        f.report_link_field = False
        f.wifi = {"ble_connected": True}
    code, kinds, _ = refuse(old_build)
    check("runner_refuses_linked_via_wifi", code == 1, "%s %s" % (code, kinds))

    def unknown(f):
        f.report_link_field = False
    code, kinds, _ = refuse(unknown)
    check("runner_refuses_unknown", code == 1 and "CONTROLLER LINK UNKNOWN" in kinds, "%s %s" % (code, kinds))


def main():
    for case in (case_tap_409_halts_and_latches, case_settingsui_409_linked_not_retried,
                 case_settingsui_busy_still_retried, case_state_read_halts_on_link, case_cover_guard,
                 case_cover_guard_allow_closed, case_cover_guard_after_close_command, case_guard_off_by_default,
                 case_runner_refuses):
        case()
    print("rig_guard_check: %d checks, %d failed" % (CHECKS[0], len(FAILURES)))
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main())
