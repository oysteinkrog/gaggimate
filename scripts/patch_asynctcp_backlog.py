"""Raise AsyncTCP's hardcoded listen backlog from 5 to 16.

ESPAsyncWebServer sends Connection: close on every response, so a browser
page load is a burst of parallel fresh TCP connections (html, css, js,
API fetch, WebSocket), and two concurrent tabs roughly double it. AsyncTCP
listens with a hardcoded backlog of 5 (a function-local in
AsyncServer::begin); connections past it get their SYN silently dropped,
and the client stalls into retransmit for seconds or fails outright.
Measured on the bench: two simultaneous headless-Chrome page loads against
backlog 5 both timed out at the bare index.html while two concurrent curl
downloads of the same 430KB bundle finished in 2.3s each -- the server has
the bandwidth, the listen queue just refuses the burst.

Queued-but-unaccepted connections draw from the same lwIP pcb pool as
everything else (CONFIG_LWIP_MAX_ACTIVE_TCP=16), so a backlog of 16 makes
the listen queue a non-constraint: bursts are bounded by the pool, whose
TIME_WAIT pressure CONFIG_LWIP_TCP_MSL=5000 already relieved. No extra
memory is reserved by the backlog number itself.

PlatformIO copies the library into .pio/libdeps/<env>/ twice (plain and
@src-<hash>); the Library Dependency Finder compiles one of them, which
one has changed across pio versions, so both copies are patched. The two
are different releases: AsyncTCP@src-<hash> is the v3.4.10 platformio.ini
pins by git tag (the one the display build compiles, lib7da on 2026-10-01),
and the plain AsyncTCP is 3.5.0 from the registry, pulled in by
ESPAsyncWebServer's own dependency. Both upstream hashes are pinned below.

Applied through scripts/gm_patch.py: the patched file is always derived from
a hash-verified pristine AsyncTCP.cpp (kept beside it as AsyncTCP.cpp.gm-orig)
and carries a gm-patch-vN stamp, so a tree patched by an older version of
this script is brought up to date instead of being taken as done. Anchored
on the exact upstream line so an AsyncTCP update that moves it fails the
build loudly here.

Versions
--------
v1 (2026-10-01, the first stamped version): backlog 5 to 16.
Bump VERSION whenever the hunk below changes.
"""

import glob
import os
import sys

try:
    Import("env")  # noqa: F821 -- provided by SCons
except NameError:  # imported by scripts/test_gm_patch.py
    env = None
if env is not None:
    sys.path.insert(0, os.path.join(env.subst("$PROJECT_DIR"), "scripts"))
import gm_patch  # noqa: E402

OWNER = "patch_asynctcp_backlog"
VERSION = 1
# sha256 of upstream src/AsyncTCP.cpp at ESP32Async/AsyncTCP tags v3.4.10 (the
# git pin in platformio.ini) and v3.5.0 (the registry copy), checked against
# raw.githubusercontent.com on 2026-10-01.
PRISTINE_SHA256 = {
    "10b7124eea6b9b0b8734d058d480219f81e2663ddfd958eb6be27241bac048da",  # v3.4.10
    "27e3e22a6030cc5c9c9f32f28d0a3a36189b1ea6a5fa270777b2d2790a89c365",  # v3.5.0
}

MARKER = "GM_ASYNCTCP_BACKLOG_PATCH"

OLD = "  static uint8_t backlog = 5;\n"
NEW = (
    "  /* " + MARKER + ": 5 drops SYNs whenever two tabs load at once, because\n"
    "   * every response is Connection: close and a page load is a burst of\n"
    "   * parallel fresh connections. 16 defers the limit to the lwIP pcb pool\n"
    "   * (CONFIG_LWIP_MAX_ACTIVE_TCP). See scripts/patch_asynctcp_backlog.py. */\n"
    "  static uint8_t backlog = 16;\n"
)

PATCHES = [gm_patch.Patch(MARKER, VERSION, [(OLD, NEW, 1)])]


def main():
    root = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"))  # noqa: F821
    paths = sorted(glob.glob(os.path.join(root, "AsyncTCP*", "src", "AsyncTCP.cpp")))
    if not paths:
        print("gm-patch: %s: AsyncTCP not present for this env; skipping" % OWNER)
        return
    for path in paths:
        gm_patch.run(OWNER, path, PRISTINE_SHA256, PATCHES)


if env is not None:
    main()
