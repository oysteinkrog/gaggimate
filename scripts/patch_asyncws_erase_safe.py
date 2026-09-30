"""Make ESPAsyncWebServer's WebSocket broadcast loops survive a client
closing itself mid-iteration.

Why this exists
---------------
WebUIPlugin sets setCloseClientOnQueueFull(true) on every WebSocket client:
a stalled tab must be dropped, not kept alive with a full queue, because
its buffered frames pin WiFi TX memory the rest of the IP stack needs
(the deliberate choice is documented at the WS_EVT_CONNECT handler in
WebUIPlugin.cpp). With that flag, AsyncWebSocketClient::_queueMessage
reacts to a full queue by calling _client->close(), and AsyncTCP runs the
disconnect callback synchronously from inside close():

    AsyncWebSocket::textAll            for (auto &c : _clients) ...
      AsyncWebSocketClient::text
        _queueMessage                  _client->close()
          AsyncClient::_close          _discard_cb(...)
            AsyncWebSocketClient::_onDisconnect
              AsyncWebSocket::_handleDisconnect    _clients.erase(iter)

_clients is a std::list, so erasing the element the range-for is standing
on frees the node it will read its ++ from. The next iteration walks a
dangling pointer: LoadProhibited at EXCVADDR 0x14 (c.status() through the
garbage node) or an IllegalInstruction when the freed memory has already
been handed out. Seen four times on the bench, every one preceded by the
library's own "Too many messages queued: closing connection" line, all in
WebUIPlugin::broadcastJson -> AsyncWebSocket::textAll. It only needs one
browser tab to fall behind the ~10 Hz status broadcast while the internal
heap is tight enough that the TX path stalls, which is exactly the state
the flag exists for.

Two changes, both in AsyncWebSocket.cpp:

1. The four broadcast loops (closeAll, pingAll, textAll, binaryAll) take
   an explicit iterator and advance it before touching the client. On a
   std::list, erasing the element behind the iterator leaves the iterator
   valid, so the loop continues from the right place whether or not the
   client removed itself.

2. _queueMessage logs "Too many messages queued" *before* it calls
   _client->close(). Upstream logs afterwards, through _server and
   _clientId of an object the close just destroyed (the log is a read of
   freed memory; it does not crash on its own, which is why the loop bug
   surfaced first).

Applied through scripts/gm_patch.py: the patched file is always derived
from a hash-verified pristine AsyncWebSocket.cpp (kept beside it as
AsyncWebSocket.cpp.gm-orig) and carries a gm-patch-vN stamp, so a tree
patched by an older version of this script is brought up to date instead
of being taken as done. Anchored on the exact upstream text (v3.10.3, our
fork) so a library bump that rewrites these functions fails the build here
instead of silently shipping the crash again. Upstream fixed the same class
of bug for cleanupClients only (the `break` after erase); the broadcast
loops are still range-for at 3.10.x.

Versions
--------
v1 (2026-10-01, the first stamped version): the four broadcast loops and
the log-before-close in _queueMessage. Bump VERSION whenever a hunk below
changes.
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

OWNER = "patch_asyncws_erase_safe"
VERSION = 1
# sha256 of src/AsyncWebSocket.cpp at the commit platformio.ini pins
# (oysteinkrog/ESPAsyncWebServer 1dc278b9a7915b7d97baa79e0a5449b2eb72b8d4),
# checked against raw.githubusercontent.com on 2026-10-01.
PRISTINE_SHA256 = {"ce6192141fe8d51c0579370a30cc9599c8d06ac598440b60349ca8f93fa05cdb"}

MARKER = "GM_ASYNCWS_ERASE_SAFE_PATCH"

LOOP_NOTE = (
    "  /* " + MARKER + ": the call below may close the client, which erases it from\n"
    "   * _clients synchronously (AsyncTCP runs the disconnect callback inside\n"
    "   * close()). Advance first; erasing behind a std::list iterator is safe.\n"
    "   * See scripts/patch_asyncws_erase_safe.py. */\n"
)

CLOSE_ALL_OLD = (
    "  for (auto &c : _clients) {\n"
    "    if (c.status() == WS_CONNECTED) {\n"
    "      c.close(code, message);\n"
    "    }\n"
    "  }\n"
)
CLOSE_ALL_NEW = (
    LOOP_NOTE +
    "  for (auto it = _clients.begin(); it != _clients.end();) {\n"
    "    AsyncWebSocketClient &c = *it++;\n"
    "    if (c.status() == WS_CONNECTED) {\n"
    "      c.close(code, message);\n"
    "    }\n"
    "  }\n"
)

PING_ALL_OLD = (
    "  for (auto &c : _clients) {\n"
    "    if (c.status() == WS_CONNECTED && c.ping(data, len)) {\n"
)
PING_ALL_NEW = (
    LOOP_NOTE +
    "  for (auto it = _clients.begin(); it != _clients.end();) {\n"
    "    AsyncWebSocketClient &c = *it++;\n"
    "    if (c.status() == WS_CONNECTED && c.ping(data, len)) {\n"
)

TEXT_ALL_OLD = (
    "  for (auto &c : _clients) {\n"
    "    if (c.status() == WS_CONNECTED && c.text(buffer)) {\n"
)
TEXT_ALL_NEW = (
    LOOP_NOTE +
    "  for (auto it = _clients.begin(); it != _clients.end();) {\n"
    "    AsyncWebSocketClient &c = *it++;\n"
    "    if (c.status() == WS_CONNECTED && c.text(buffer)) {\n"
)

BINARY_ALL_OLD = (
    "  for (auto &c : _clients) {\n"
    "    if (c.status() == WS_CONNECTED && c.binary(buffer)) {\n"
)
BINARY_ALL_NEW = (
    LOOP_NOTE +
    "  for (auto it = _clients.begin(); it != _clients.end();) {\n"
    "    AsyncWebSocketClient &c = *it++;\n"
    "    if (c.status() == WS_CONNECTED && c.binary(buffer)) {\n"
)

QUEUE_FULL_OLD = (
    "    if (closeWhenFull) {\n"
    "      _status = WS_DISCONNECTED;\n"
    "\n"
    "      if (_client) {\n"
    "#ifdef ESP32\n"
    "        /*\n"
    "          Unlocking has to be called before return execution otherwise std::unique_lock ::~unique_lock() will get an exception pthread_mutex_unlock.\n"
    "          Due to _client->close() shall call the callback function _onDisconnect()\n"
    "          The calling flow _onDisconnect() --> _handleDisconnect() --> ~AsyncWebSocketClient()\n"
    "        */\n"
    "        lock.unlock();\n"
    "#endif\n"
    "        _client->close();\n"
    "      }\n"
    "\n"
    "      async_ws_log_w(\"[%s][%\" PRIu32 \"] Too many messages queued: closing connection\", _server->url(), _clientId);\n"
    "\n"
    "    } else {\n"
)
QUEUE_FULL_NEW = (
    "    if (closeWhenFull) {\n"
    "      _status = WS_DISCONNECTED;\n"
    "\n"
    "      /* " + MARKER + ": logged before the close, which destroys this object\n"
    "       * (upstream read _server and _clientId after it). */\n"
    "      async_ws_log_w(\"[%s][%\" PRIu32 \"] Too many messages queued: closing connection\", _server->url(), _clientId);\n"
    "\n"
    "      if (_client) {\n"
    "#ifdef ESP32\n"
    "        /*\n"
    "          Unlocking has to be called before return execution otherwise std::unique_lock ::~unique_lock() will get an exception pthread_mutex_unlock.\n"
    "          Due to _client->close() shall call the callback function _onDisconnect()\n"
    "          The calling flow _onDisconnect() --> _handleDisconnect() --> ~AsyncWebSocketClient()\n"
    "        */\n"
    "        lock.unlock();\n"
    "#endif\n"
    "        _client->close();\n"
    "      }\n"
    "\n"
    "    } else {\n"
)

# (anchor, replacement, expected occurrence count)
HUNKS = [
    (CLOSE_ALL_OLD, CLOSE_ALL_NEW, 1),
    (PING_ALL_OLD, PING_ALL_NEW, 1),
    (TEXT_ALL_OLD, TEXT_ALL_NEW, 1),
    (BINARY_ALL_OLD, BINARY_ALL_NEW, 1),
    (QUEUE_FULL_OLD, QUEUE_FULL_NEW, 1),
]


PATCHES = [gm_patch.Patch(MARKER, VERSION, HUNKS)]


def main():
    root = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"))  # noqa: F821
    paths = sorted(glob.glob(os.path.join(root, "ESPAsyncWebServer*", "src", "AsyncWebSocket.cpp")))
    if not paths:
        print("gm-patch: %s: ESPAsyncWebServer not present for this env; skipping" % OWNER)
        return
    for path in paths:
        gm_patch.run(OWNER, path, PRISTINE_SHA256, PATCHES)


if env is not None:
    main()
