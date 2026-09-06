"""Rig(host) and Sim(...): the shared client and process launcher the settings
UI scenario scripts and the runner (gm-flw.13) are built on. Everything here
talks to the same debug routes tools/touchmap.py already drives
(/api/debug/tap, /api/debug/touchmap, /api/debug/fb, /api/settings) plus the
ones this epic adds (/api/debug/settingsui, /api/debug/anim, /api/debug/heap,
/api/debug/synth). It works unmodified against the desktop simulator
(pio run -e display-sim, GAGGIMATE_SIM) or the bench device
(display-loadtest, GM_TOUCH_PROBE): both expose the same routes, and Rig
never assumes which venue it is talking to.

Effective hit-rectangle arithmetic (grow by ext click pad, clip by ancestors
without LV_OBJ_FLAG_OVERFLOW_VISIBLE) mirrors tools/touchmap.py's
effective_targets(): that script is the reference this rig's touchmap()
reimplements as a library call instead of a one-shot CLI dump.
"""

import json
import math
import os
import re
import socket
import struct
import subprocess
import time
import urllib.error
import urllib.request
import zlib
from datetime import datetime, timezone

HERE = os.path.dirname(os.path.abspath(__file__))
SCREENS_H = os.path.join(HERE, "..", "..", "src", "display", "ui", "default", "eez", "screens.h")

PANEL_SIZE = 480
PANEL_CENTER = (240.0, 240.0)
PANEL_EDGE_RADIUS = 240.0
MIN_TARGET_SIZE = 56
EDGE_MARGIN = 12

_object_names_cache = None


def _object_names():
    """EEZ Studio object names in objects_t order (screens.h), so a touchmap
    dump's "o" index (into that struct) can be reported by name. Cached: the
    header does not change while a rig runs, and this is called once per
    audit()/baseline dump, not per object."""
    global _object_names_cache
    if _object_names_cache is None:
        src = open(SCREENS_H, encoding="utf-8").read()
        body = src[src.index("typedef struct _objects_t {") : src.index("} objects_t;")]
        _object_names_cache = re.findall(r"lv_obj_t \*(\w+);", body)
    return _object_names_cache


def object_name(obj):
    """The EEZ name for a touchmap object, or a class#id fallback for a
    runtime-built object (the settings shell's rows are not in objects_t)."""
    names = _object_names()
    oi = obj.get("o", -1)
    if 0 <= oi < len(names):
        return names[oi]
    return "(%s #%d)" % (obj.get("c", "?"), obj.get("i", -1))


def tag_role(obj):
    """The role half of a SettingsDebugTag ("row/role"), or None for an
    object outside the open settings shell's cover (DefaultUI.cpp's
    touchMapNode only exports "tag" for descendants of the cover)."""
    tag = obj.get("tag")
    if not tag:
        return None
    return tag.rsplit("/", 1)[-1]


def tag_row(obj):
    """The row half of a SettingsDebugTag, or None."""
    tag = obj.get("tag")
    if not tag:
        return None
    return tag.rsplit("/", 1)[0]


def _annotate(dump):
    """Adds "hit" (the effective hit rectangle: coords grown by the ext click
    pad, clipped by every ancestor without the overflow-visible flag) and
    "hidden" (true anywhere up the parent chain) to every object in place.
    Same arithmetic as tools/touchmap.py's effective_targets(); "ty"
    (translate_y) is diagnostic only there and here, since x1/y1/x2/y2 in the
    dump already include any translate (DefaultUI.cpp's touchMapNode
    comment)."""
    objs = {o["i"]: o for o in dump["objects"]}
    for o in dump["objects"]:
        x1, y1, x2, y2 = o["x1"] - o["e"], o["y1"] - o["e"], o["x2"] + o["e"], o["y2"] + o["e"]
        p = objs.get(o["p"])
        while p is not None:
            if not p["v"]:
                x1, y1, x2, y2 = max(x1, p["x1"]), max(y1, p["y1"]), min(x2, p["x2"]), min(y2, p["y2"])
            p = objs.get(p["p"])
        o["hit"] = (x1, y1, x2, y2)
        hidden = False
        n = o
        while n is not None:
            if n["h"]:
                hidden = True
                break
            n = objs.get(n["p"])
        o["hidden"] = hidden
    return dump


def find_tag(dump, row, role):
    """The object tagged "row/role" in dump, or None. Used directly for a
    one-off lookup, and by row_value()/rows_on_page() below."""
    target = "%s/%s" % (row, role)
    for o in dump["objects"]:
        if o.get("tag") == target:
            return o
    return None


def row_value(dump, row):
    """The canonical value of a settings row: the "val" of its "row/value"
    object (the row-owned string SettingsDebugTag carries; DefaultUI.cpp's
    touchMapNode only ever emits "val" when that pointer is non-null), or
    the "t" of the same object when "val" was not emitted. Raises with the
    visible row names when the row has no value object (a typo'd row name,
    or a row on a page that is not the one showing).

    Note: the bead text calls this row_value(row); every sibling lookup
    below (rows_on_page, targets, audit) takes dump explicitly, so this does
    too, for one consistent signature rather than hidden last-dump state on
    Rig. Rig.row_value is a staticmethod, so rig.row_value(dump, row) reads
    the same either way.
    """
    obj = find_tag(dump, row, "value")
    if obj is None:
        rows = sorted({tag_row(o) for o in dump["objects"] if tag_row(o)})
        raise ValueError("no value object for row %r; visible rows: %s" % (row, ", ".join(rows) if rows else "(none)"))
    val = obj.get("val")
    return val if val is not None else obj.get("t")


ROW_CONTAINER_ROLES = ("row", "toggle", "action", "confirm")


def rows_on_page(dump):
    """Settings row names on the dumped page, top to bottom by hit rect. A
    row widget's outer container is tagged with role "row" (stepper, choice,
    locked, info) or with the whole-row target role it doubles as ("toggle",
    "action", "confirm"); the shell's five slot containers are role "slot"
    and never listed. Row widgets are 320x56, five to a page, so this is at
    most 5 entries for a settings category page; for a generated screen (no
    rows tagged) it is empty."""
    by_y = {}
    for o in dump["objects"]:
        if tag_role(o) not in ROW_CONTAINER_ROLES:
            continue
        row = tag_row(o)
        y = o["hit"][1] if "hit" in o else o["y1"]
        by_y.setdefault(row, y)
    return [row for row, _ in sorted(by_y.items(), key=lambda kv: kv[1])]


def targets(dump, include_hidden=False):
    """Clickable objects with an event callback (o["k"] and o["n"] > 0),
    excluding SettingsDebugTag role "cover" (the settings shell's full-screen
    cover: clickable so activity tracking sees every touch, never itself a
    target) and any object whose hit rect covers at least 90 percent of the
    panel (a generated screen's own tap-anywhere-returns-to-standby
    background). Hidden objects (unreachable behind the flow's own HIDDEN
    flag) are excluded by default; pass include_hidden=True to see them."""
    scr_area = PANEL_SIZE * PANEL_SIZE
    out = []
    for o in dump["objects"]:
        if not o.get("k") or not o.get("n"):
            continue
        if tag_role(o) == "cover":
            continue
        if not include_hidden and o.get("hidden"):
            continue
        x1, y1, x2, y2 = o.get("hit", (o["x1"], o["y1"], o["x2"], o["y2"]))
        area = max(0, x2 - x1 + 1) * max(0, y2 - y1 + 1)
        if area >= 0.9 * scr_area:
            continue
        out.append(o)
    return out


def _max_corner_dist(x1, y1, x2, y2, cx, cy):
    return max(math.hypot(px - cx, py - cy) for px, py in ((x1, y1), (x1, y2), (x2, y1), (x2, y2)))


def audit(dump, exempt_roles=("exit",), exempt_names=("standby_btn",)):
    """Geometry checks for every tappable target on the dumped screen
    (CLAUDE.md / the settings epic's shared contract): each hit rect is at
    least 56x56 px, no two overlap, and none reaches within 12 px of the
    panel's edge circle (radius 240 around 240,240). The exit chevron (role
    "exit") and the generated screens' bottom power button ("standby_btn",
    by EEZ name) are known exceptions to the size and edge rules by design
    (the shared contract calls both out by name); they are reported under
    "exempt", never counted as violations. Overlap has no named exemption:
    it applies to every target regardless of role or name.

    Returns {"violations": [...], "exempt": [...]}, each entry
    {"target", "tag", "reason", "detail"} ("reason" is "size", "edge" or
    "overlap"; an "overlap" entry also carries "other").
    """
    ts = targets(dump)
    violations = []
    exempt = []
    for o in ts:
        x1, y1, x2, y2 = o["hit"]
        w, h = x2 - x1 + 1, y2 - y1 + 1
        label = object_name(o)
        role = tag_role(o)
        is_exempt = role in exempt_roles or label in exempt_names
        bucket = exempt if is_exempt else violations
        if w < MIN_TARGET_SIZE or h < MIN_TARGET_SIZE:
            bucket.append({"target": label, "tag": o.get("tag"), "reason": "size", "detail": "%dx%d" % (w, h)})
        max_d = _max_corner_dist(x1, y1, x2, y2, *PANEL_CENTER)
        if max_d > PANEL_EDGE_RADIUS - EDGE_MARGIN:
            bucket.append({"target": label, "tag": o.get("tag"), "reason": "edge", "detail": "%.1fpx from centre" % max_d})
    for i in range(len(ts)):
        ax1, ay1, ax2, ay2 = ts[i]["hit"]
        for j in range(i + 1, len(ts)):
            bx1, by1, bx2, by2 = ts[j]["hit"]
            ox1, oy1, ox2, oy2 = max(ax1, bx1), max(ay1, by1), min(ax2, bx2), min(ay2, by2)
            if ox1 <= ox2 and oy1 <= oy2:
                violations.append(
                    {
                        "target": object_name(ts[i]),
                        "tag": ts[i].get("tag"),
                        "reason": "overlap",
                        "other": object_name(ts[j]),
                        "detail": "%dx%d" % (ox2 - ox1 + 1, oy2 - oy1 + 1),
                    }
                )
    return {"violations": violations, "exempt": exempt}


def seconds(v):
    """standbyTimeout / standbyBrightnessTimeout: the wire value is already
    seconds (Settings stores milliseconds; handleSettings divides by 1000
    before serializing), so this is just an int coercion for a caller that
    got the value as a JSON number or a string."""
    return int(v)


def num(v):
    """temperatureOffset / pressureScaling: the device serializes these as
    JSON strings (String(settings.getX()) in WebUIPlugin.cpp), not numbers."""
    return float(v)


def color_hex(v):
    """bgAnimPlateColor / elementTintColor / touchDimColor: "#rrggbb" ->
    (r, g, b)."""
    n = int(v.lstrip("#"), 16)
    return ((n >> 16) & 0xFF, (n >> 8) & 0xFF, n & 0xFF)


def schedules(s):
    """autowakeupSchedules: "HH:MM|ddddddd;HH:MM|ddddddd;..." (days
    Mon..Sun) -> [{"time": "HH:MM", "days": [bool] * 7}, ...]."""
    out = []
    for part in s.split(";"):
        if not part:
            continue
        time_str, _, days_str = part.partition("|")
        days = [c == "1" for c in days_str] if len(days_str) == 7 else [False] * 7
        out.append({"time": time_str, "days": days})
    return out


class RigHTTPError(Exception):
    """Every HTTP failure raises this, with the URL and status (or the
    underlying socket error, when the request never got a status) in the
    message."""


def _rgb565_to_rgb888(data):
    px = []
    for i in range(0, len(data), 2):
        v = data[i] | (data[i + 1] << 8)
        r = (v >> 11) & 0x1F
        g = (v >> 5) & 0x3F
        b = v & 0x1F
        px.append([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)])
    return px


def _draw_rect(px, w, h, x1, y1, x2, y2, color, scale):
    x1, y1, x2, y2 = [int(round(v * scale)) for v in (x1, y1, x2, y2)]
    for x in range(max(0, x1), min(w - 1, x2) + 1):
        for y in (y1, y2):
            if 0 <= y < h:
                px[y * w + x] = list(color)
    for y in range(max(0, y1), min(h - 1, y2) + 1):
        for x in (x1, x2):
            if 0 <= x < w:
                px[y * w + x] = list(color)


def _write_png(path, px, w, h):
    rows = []
    for y in range(h):
        row = bytearray([0])
        for x in range(w):
            row += bytes(px[y * w + x])
        rows.append(bytes(row))
    raw = b"".join(rows)

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    out += chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
    open(path, "wb").write(out)


class Rig:
    """HTTP client for one venue (the desktop simulator or the bench
    device): every method here is a thin wrapper over one of the debug
    routes and works unmodified against either. host is "ip[:port]",
    e.g. "192.168.1.121" (device, default port 80) or "127.0.0.1:8084"
    (a simulator started with GM_SIM_PORT=8084)."""

    def __init__(self, host, timeout=15):
        self.host = host
        self.base = "http://%s" % host
        self.timeout = timeout

    def _url(self, path):
        return self.base + path if path.startswith("/") else self.base + "/" + path

    def _open(self, path, timeout=None):
        url = self._url(path)
        try:
            return urllib.request.urlopen(url, timeout=timeout or self.timeout)
        except urllib.error.HTTPError as e:
            raise RigHTTPError("%s -> HTTP %d" % (url, e.code)) from e
        except OSError as e:
            raise RigHTTPError("%s -> %s" % (url, e)) from e

    def get_json(self, path, timeout=None):
        """GET path and parse the body as JSON. Raises RigHTTPError (with
        the URL, status and Content-Type) rather than a bare json/unicode
        exception when the body is not JSON: a debug route this build does
        not compile falls through to the static handler and answers 200
        with the gzip-compressed web UI bundle, not a 404, and that must
        not look like a JSON parse bug in the caller's traceback."""
        with self._open(path, timeout) as resp:
            ctype = resp.headers.get("Content-Type", "")
            data = resp.read()
        try:
            return json.loads(data)
        except (ValueError, UnicodeDecodeError) as e:
            raise RigHTTPError("%s: not JSON (Content-Type=%r): %s" % (self._url(path), ctype, e)) from e

    def get_bytes(self, path, timeout=None):
        with self._open(path, timeout) as resp:
            return resp.read()

    def tap(self, x, y, ms=80):
        """Queues one synthetic tap (/api/debug/tap) and polls the state GET
        until it reports the release, then waits 150 ms (the same margin
        tools/touchmap.py's do_tap uses) for the UI task to act on it before
        returning the final state."""
        self.get_json("/api/debug/tap?x=%d&y=%d&ms=%d" % (x, y, ms))
        t0 = time.time()
        deadline = t0 + ms / 1000.0 + 10
        state = None
        while time.time() < deadline:
            state = self.get_json("/api/debug/tap")
            if state.get("released_at_ms"):
                break
            time.sleep(0.02)
        else:
            raise TimeoutError("tap at (%d,%d) did not release within timeout" % (x, y))
        time.sleep(0.15)
        return state

    def tap_target(self, target, ms=80):
        """Taps the centre of target's effective hit rect (a dict from
        touchmap()/targets()/find_tag(), carrying "hit")."""
        x1, y1, x2, y2 = target["hit"]
        return self.tap((x1 + x2) // 2, (y1 + y2) // 2, ms)

    def touchmap(self, screen=0, load=False, timeout=8):
        """Dumps a screen's object tree (/api/debug/touchmap) and annotates
        every object with its effective hit rect and hidden state (see
        _annotate). screen=0 is the active screen (no load, no settle
        delay); other ids load that screen first unless load=False, in
        which case the last dump for that id is returned as-is.

        The poll loop sleeps before every read, including the first: a
        second request that lands on the simulator's single cooperative
        thread with no gap after the queueing request can arrive while
        serviceTouchMap's changeScreen() is mid-rebuild and kill the whole
        process (reproduced: identical request, only a 0.25s gap before it
        separates a clean "pending" reply from the simulator dying with no
        log line). tools/touchmap.py's dump_screen() already sleeps first
        for this reason; this matches it rather than polling immediately."""
        q = "/api/debug/touchmap?screen=%d" % screen
        if load and screen != 0:
            q += "&load=1"
        self.get_json(q)
        t0 = time.time()
        while time.time() - t0 < timeout:
            time.sleep(0.25)
            d = self.get_json("/api/debug/touchmap")
            if not d.get("pending") and d.get("screen") == screen:
                return _annotate(d)
        raise TimeoutError("touchmap for screen %d did not arrive within %.1fs" % (screen, timeout))

    def wait_dump_change(self, prev_seq, timeout=8, screen=0, load=False):
        """Polls touchmap(screen) until its "seq" differs from prev_seq (a
        UI-task pass happened since the dump prev_seq came from)."""
        t0 = time.time()
        while time.time() - t0 < timeout:
            d = self.touchmap(screen=screen, load=load)
            if d.get("seq") != prev_seq:
                return d
            time.sleep(0.1)
        raise TimeoutError("touchmap seq did not move on from %r within %.1fs" % (prev_seq, timeout))

    find_tag = staticmethod(find_tag)
    row_value = staticmethod(row_value)
    rows_on_page = staticmethod(rows_on_page)
    targets = staticmethod(targets)
    audit = staticmethod(audit)

    def settings(self):
        """GET /api/settings with the WiFi password deleted before the dict
        is returned; the raw body is never logged (CLAUDE.md: never dump or
        print that field)."""
        data = self.get_json("/api/settings")
        data.pop("wifiPassword", None)  # never expose the WiFi password; this is the only line in this package that may name that key
        return data

    def settings_value(self, key):
        """The raw wire-format value of one /api/settings field; pass it to
        seconds()/num()/color_hex()/schedules() to parse it."""
        return self.settings()[key]

    def settingsui(self, **args):
        """/api/debug/settingsui with one command (open=1, close=1, cat=N,
        page=N or pop=1). Not yet backed by a route (gm-flw.6 adds it): this
        method matches the route's documented query parameters and response
        shape ({"seq":n,"accepted":true}) so callers do not change when it
        lands."""
        allowed = {"open", "close", "cat", "page", "pop"}
        bad = set(args) - allowed
        if bad:
            raise ValueError("unknown settingsui argument(s): %s" % ", ".join(sorted(bad)))
        q = "&".join("%s=%s" % (k, v) for k, v in args.items())
        return self.get_json("/api/debug/settingsui" + ("?" + q if q else ""))

    def settingsui_state(self):
        """GET /api/debug/settingsui with no arguments: the shell's last
        published State plus the Fixture counters and the last completed
        command's seq. Not yet backed by a route; see settingsui()."""
        return self.get_json("/api/debug/settingsui")

    def heap(self):
        return self.get_json("/api/debug/heap")

    def anim(self, **args):
        q = "&".join("%s=%s" % (k, v) for k, v in args.items())
        return self.get_json("/api/debug/anim" + ("?" + q if q else ""))

    def synth(self, brew=None):
        """/api/debug/synth: brew=None just reads state; True/False (or
        1/0) sets the load rig's synthetic brew handshake. Bench/loadtest
        and simulator builds only (GM_SYNTH_HANDSHAKE)."""
        path = "/api/debug/synth"
        if brew is not None:
            path += "?brew=%d" % (1 if brew else 0)
        return self.get_json(path)

    def fb_png(self, path, step=2, hit_rects=None):
        """Writes /api/debug/fb (RGB565) to path as a PNG, at 1/step
        resolution. hit_rects, if given, is an iterable of (x1,y1,x2,y2) or
        (x1,y1,x2,y2,color) in full-panel (480x480) coordinates, drawn on
        the image scaled to its actual size."""
        with self._open("/api/debug/fb?step=%d" % step, timeout=60) as resp:
            size = resp.headers.get("X-FB-Size", "")
            data = resp.read()
        if "x" not in size:
            raise RigHTTPError("/api/debug/fb: missing X-FB-Size header")
        w, h = (int(v) for v in size.split("x"))
        if len(data) != w * h * 2:
            raise RigHTTPError("/api/debug/fb: expected %d bytes for %s, got %d" % (w * h * 2, size, len(data)))
        px = _rgb565_to_rgb888(data)
        if hit_rects:
            scale = w / float(PANEL_SIZE)
            for rect in hit_rects:
                color = rect[4] if len(rect) > 4 else (0, 255, 80)
                _draw_rect(px, w, h, rect[0], rect[1], rect[2], rect[3], color, scale)
        _write_png(path, px, w, h)
        return w, h

    def wait_until(self, pred, timeout, every=0.2):
        """Polls pred() until it is truthy or timeout elapses; raises
        TimeoutError with pred's last (falsy) result otherwise."""
        t0 = time.time()
        last = None
        while time.time() - t0 < timeout:
            last = pred()
            if last:
                return last
            time.sleep(every)
        raise TimeoutError("condition not met within %.1fs (last result: %r)" % (timeout, last))

    def log(self, step, **kv):
        """One line to stdout: ISO-8601 UTC timestamp, step, key=value
        pairs, e.g. "2026-09-06T12:00:00.000Z tap x=240 y=450"."""
        ts = datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")
        parts = " ".join("%s=%s" % (k, v) for k, v in kv.items())
        line = ("%s %s %s" % (ts, step, parts)).rstrip()
        print(line, flush=True)
        return line


class SimError(Exception):
    """Sim() launch or readiness failure; the message carries what the
    simulator's own stdout/stderr said, where there was any to carry."""


class Sim:
    """Launches the simulator binary (pio run -e display-sim's
    .pio/build/display-sim/program) headless and tears it down on exit.
    data_dir must be a directory literally named "sim_data" (the sim's
    preferences_shim.cpp and fs_shim.cpp hard-code that relative path), and
    the program is launched with data_dir's parent as its working
    directory so that path resolves to data_dir. Use as a context manager:

        with Sim(program_path, os.path.join(workdir, "sim_data"), port=8084) as sim:
            sim.rig.settings()
    """

    READY_BANNER = "Started webserver"
    # Extra settle time after the banner, before Sim is handed to the caller.
    # Measured (2026-09-06): a /api/debug/touchmap?screen=N&load=1 request
    # (any screen id, any N) that reaches serviceTouchMap's changeScreen()
    # while the process is under ~1s old kills the simulator outright, no
    # log line, connection then refused; a plain GET (settings, heap, the
    # queueing request itself) is safe at any age, and a load=1 request past
    # ~1.2s old was safe in every trial (10 trials at 1.0s, 0 failures; 10
    # trials at 0.5s, 10 failures). The banner alone prints at ~0.2s, well
    # inside the unsafe window, so a caller that starts driving the shell
    # immediately after __enter__ returns without this would hit it on its
    # first screen load. Root cause is unconfirmed (something the UI task's
    # early passes have not finished, not a race with this rig's request
    # cadence: see touchmap()'s own comment) and is in sim/ or src/display/,
    # outside this bead's file list; flagged to the leader rather than fixed
    # here.
    BOOT_SETTLE_S = 1.5

    def __init__(self, program_path, data_dir, port=8080, log_path=None):
        if os.path.basename(os.path.normpath(data_dir)) != "sim_data":
            raise ValueError('data_dir must be a directory named "sim_data", got %r' % data_dir)
        # Absolute before any working-directory change: subprocess.Popen
        # resolves a relative executable path against the child's new cwd
        # (set via the cwd= argument below), not the parent's, so a relative
        # program_path would silently look for the binary under data_dir's
        # parent instead of where the caller meant.
        self.program_path = os.path.abspath(program_path)
        self.data_dir = data_dir
        self.workdir = os.path.dirname(os.path.abspath(data_dir))
        self.port = port
        self.log_path = log_path or os.path.join(self.workdir, "sim.log")
        self.rig = Rig("127.0.0.1:%d" % port)
        self.proc = None

    def _check_port_free(self):
        """Connects rather than binds: on WSL1 (this dev machine; see
        CLAUDE.md's WSL1/wslfs note) a bare bind() with SO_REUSEADDR
        succeeds even when another process already has the exact same
        address:port bound and listening (verified: two plain Python
        sockets, both SO_REUSEADDR, both bind 127.0.0.1 to the same port
        with no error; real Linux refuses the second). A bind-based check
        is silently useless here. Connecting is also simply the more
        direct question: is something already answering on this address,
        which is what would actually go wrong."""
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(0.5)
        try:
            s.connect(("127.0.0.1", self.port))
        except OSError:
            return  # nothing answering: the port is free
        finally:
            s.close()
        raise SimError("127.0.0.1:%d is already answering connections (another process is using this port)" % self.port)

    def _launch(self):
        self._check_port_free()
        os.makedirs(self.workdir, exist_ok=True)
        env = dict(os.environ)
        env["SDL_VIDEODRIVER"] = "dummy"
        env["GM_SIM_PORT"] = str(self.port)
        with open(self.log_path, "wb") as log_fh:
            self.proc = subprocess.Popen(
                [self.program_path], cwd=self.workdir, env=env, stdout=log_fh, stderr=subprocess.STDOUT
            )
        self._wait_ready()

    def _tail(self):
        try:
            with open(self.log_path, "r", encoding="utf-8", errors="replace") as f:
                return f.read()
        except OSError:
            return ""

    def _wait_ready(self, timeout=30):
        t0 = time.time()
        while time.time() - t0 < timeout:
            if self.READY_BANNER in self._tail():
                break
            if self.proc.poll() is not None:
                raise SimError(
                    "simulator exited early (code %s) before %r; output:\n%s"
                    % (self.proc.returncode, self.READY_BANNER, self._tail())
                )
            time.sleep(0.1)
        else:
            raise SimError('timed out waiting for %r; output:\n%s' % (self.READY_BANNER, self._tail()))
        # The banner means the listener is up; confirm the route handlers
        # answer too before calling the simulator ready (GAGGIMATE_SIM's
        # webserver start-up races the rest of the UI task's first pass).
        try:
            self.rig.wait_until(lambda: self._settings_answers(), timeout=10)
        except TimeoutError as e:
            raise SimError("simulator did not answer GET /api/settings within 10s: %s" % e) from e
        # See BOOT_SETTLE_S: a screen-changing touchmap request too soon
        # after boot kills the process outright. remaining, not a flat
        # sleep, so a slow machine (banner + settings check already past
        # the margin) does not pay it twice.
        remaining = self.BOOT_SETTLE_S - (time.time() - t0)
        if remaining > 0:
            time.sleep(remaining)

    def _settings_answers(self):
        try:
            self.rig.get_json("/api/settings")
            return True
        except RigHTTPError:
            return False

    def stop(self):
        if self.proc is not None and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=5)

    def restart(self):
        """Kills and relaunches the simulator with the same data_dir, so a
        setting written to sim_data/nvs/ before the call is still there
        after (the persistence check this exists for; see test_rig.py)."""
        self.stop()
        self._launch()

    def __enter__(self):
        self._launch()
        return self

    def __exit__(self, exc_type, exc, tb):
        self.stop()
        return False
