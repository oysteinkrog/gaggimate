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
one has changed across pio versions, so both copies are patched. A
pristine copy is kept at AsyncTCP.cpp.gm-orig beside each patched file.
Idempotent; anchored on the exact upstream line so an AsyncTCP update that
moves it fails the build loudly here.
"""

import glob
import os
import sys

Import("env")  # noqa: F821 -- provided by SCons

MARKER = "GM_ASYNCTCP_BACKLOG_PATCH"

OLD = "  static uint8_t backlog = 5;\n"
NEW = (
    "  /* " + MARKER + ": 5 drops SYNs whenever two tabs load at once, because\n"
    "   * every response is Connection: close and a page load is a burst of\n"
    "   * parallel fresh connections. 16 defers the limit to the lwIP pcb pool\n"
    "   * (CONFIG_LWIP_MAX_ACTIVE_TCP). See scripts/patch_asynctcp_backlog.py. */\n"
    "  static uint8_t backlog = 16;\n"
)


def apply(path):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    if MARKER in text:
        print("patch_asynctcp_backlog: already patched (%s)" % path)
        return
    found = text.count(OLD)
    if found != 1:
        sys.stderr.write(
            "patch_asynctcp_backlog: anchor found %d times (want 1) in %s; "
            "AsyncTCP was updated and this patch needs review.\n" % (found, path))
        sys.exit(1)
    orig = path + ".gm-orig"
    if not os.path.exists(orig):
        with open(orig, "w", encoding="utf-8") as f:
            f.write(text)
    text = text.replace(OLD, NEW)
    tmp = path + ".gm-tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(text)
    os.replace(tmp, path)
    print("patch_asynctcp_backlog: patched %s" % path)


def main():
    root = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"))  # noqa: F821
    paths = glob.glob(os.path.join(root, "AsyncTCP*", "src", "AsyncTCP.cpp"))
    if not paths:
        print("patch_asynctcp_backlog: AsyncTCP not present for this env; skipping")
        return
    for path in paths:
        apply(path)


main()
