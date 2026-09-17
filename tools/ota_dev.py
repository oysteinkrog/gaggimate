#!/usr/bin/env python3
"""Build the display firmware and deploy it to a board over WiFi.

For a machine that is plumbed in, where the USB port is not reachable. The
board pulls the image from a one-shot HTTP server this script runs, which is
the same path a release update takes, so the panel stop, the progress bar and
the reboot all behave the way they do for a real update.

    python3 tools/ota_dev.py                      # build, then deploy to the bench board
    python3 tools/ota_dev.py --no-build           # deploy what is already built
    python3 tools/ota_dev.py --host 192.168.1.50  # another board
    python3 tools/ota_dev.py --env display-loadtest

What it checks, in order, so a failure says which step failed rather than
"update failed":

  1. The board answers /api/ota/info, is not mid-shot, and reports a second
     app slot. A single-slot build refuses OTA in firmware; this says so
     before spending three minutes on a build.
  2. The board actually fetches the image. A download that never starts is
     nearly always the host firewall blocking the inbound connection, which
     looks identical to a dead board from the device end, so the wait for the
     first byte is reported separately from the flash itself.
  3. The image that comes back up is the one that was sent, by comparing
     esp_app_get_elf_sha256 before and after. BUILD_GIT_VERSION does not move
     between two builds of the same dirty tree, so it cannot answer this.

The flash writes take about a minute, during which the panel is deliberately
blank: DefaultUI stops scan-out for a display OTA because the RGB peripheral
and the download contend for the same memory bus.
"""
import argparse
import json
import os
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIO = os.path.expanduser("~/.local/bin/pio")
DEFAULT_HOST = "192.168.1.121"


def api(host, path, method="GET", timeout=10):
    req = urllib.request.Request("http://%s%s" % (host, path), method=method)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        body = r.read().decode("utf-8", "replace")
    return json.loads(body) if body.strip().startswith("{") else {"raw": body}


def local_ip_towards(host):
    """The address of the interface that routes to the board.

    Asking the routing table rather than the hostname: this repo's dev host is
    WSL1, whose hostname resolves to an address the board cannot reach, while
    the Windows LAN address it shares is the one that works.
    """
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect((host, 80))
        return s.getsockname()[0]
    finally:
        s.close()


class Serve(BaseHTTPRequestHandler):
    """Serves exactly one file, at any path, and records that it was fetched."""

    payload = b""
    first_get = threading.Event()
    served = []

    def do_GET(self):
        Serve.first_get.set()
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(Serve.payload)))
        self.end_headers()
        try:
            self.wfile.write(Serve.payload)
            Serve.served.append(len(Serve.payload))
        except (BrokenPipeError, ConnectionResetError) as e:
            Serve.served.append(-1)
            print("  the board dropped the connection mid-download: %s" % e)

    def do_HEAD(self):
        self.send_response(200)
        self.send_header("Content-Length", str(len(Serve.payload)))
        self.end_headers()

    def log_message(self, fmt, *args):  # quiet; the script prints its own steps
        pass


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=os.environ.get("GM_OTA_HOST", DEFAULT_HOST),
                    help="board address (default %s)" % DEFAULT_HOST)
    ap.add_argument("--env", default="display", help="PlatformIO env to build and send")
    ap.add_argument("--no-build", action="store_true", help="send what is already built")
    ap.add_argument("--no-web", action="store_true",
                    help="skip the web UI build; the embedded bundle stays as it is")
    ap.add_argument("--bin", default=None, help="an image to send instead of the env's own")
    ap.add_argument("--serve-port", type=int, default=0, help="0 picks a free port")
    ap.add_argument("--flash-timeout", type=int, default=300,
                    help="seconds to wait for the board to come back (default 300)")
    args = ap.parse_args()

    fw = args.bin or os.path.join(REPO_ROOT, ".pio", "build", args.env, "firmware.bin")

    # 1. Ask the board before building, so a board that cannot take an OTA at
    #    all costs seconds rather than minutes.
    print("checking %s" % args.host)
    try:
        before = api(args.host, "/api/ota/info")
    except (urllib.error.URLError, socket.timeout, TimeoutError) as e:
        print("  cannot reach the board: %s" % e)
        return 2
    print("  running %s  sha %s  slot %s -> %s" %
          (before.get("version"), before.get("sha"), before.get("slot"), before.get("next")))
    if not before.get("dev_ota", False):
        print("  this build has no dev OTA endpoint (built with -DGM_DEV_OTA=0)")
        return 2
    if not before.get("ota", False):
        print("  this build has a single app slot; it can only be flashed over USB")
        return 2
    if before.get("busy", False):
        print("  the board is busy (a process is running, or an update is pending)")
        return 2

    if not args.no_build:
        if not args.no_web:
            print("building the web UI")
            r = subprocess.run(["bash", os.path.join(REPO_ROOT, "scripts", "build_webui.sh")],
                               cwd=REPO_ROOT)
            if r.returncode != 0:
                return r.returncode
        print("building %s" % args.env)
        r = subprocess.run([PIO, "run", "-e", args.env], cwd=REPO_ROOT)
        if r.returncode != 0:
            return r.returncode

    if not os.path.exists(fw):
        print("no image at %s" % fw)
        return 2
    with open(fw, "rb") as f:
        Serve.payload = f.read()
    if not Serve.payload.startswith(b"\xe9"):
        print("%s does not start with the ESP image magic 0xE9" % fw)
        return 2
    print("image %s (%d bytes)" % (fw, len(Serve.payload)))

    # 2. Serve it on the interface that routes to the board.
    ip = local_ip_towards(args.host)
    httpd = ThreadingHTTPServer(("0.0.0.0", args.serve_port), Serve)
    port = httpd.server_address[1]
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    url = "http://%s:%d/firmware.bin" % (ip, port)
    print("serving %s" % url)

    try:
        started = time.time()
        r = api(args.host, "/api/ota/dev?url=%s" % url, method="POST")
        if not r.get("ok"):
            print("  the board refused it: %s" % r)
            return 2
        print("  queued; waiting for the board to fetch it")

        # The board does the fetch from its display task, one loop() later.
        if not Serve.first_get.wait(timeout=45):
            print("  the board never connected.")
            print("  That is almost always the host firewall blocking inbound TCP %d." % port)
            print("  Allow it, or run this from a host the board can reach, and try again.")
            return 3
        print("  downloading (%.0fs in)" % (time.time() - started))

        # 3. Wait for the reboot. The board goes away partway through the
        #    flash, so the first sign of success is that it answers again.
        deadline = time.time() + args.flash_timeout
        after = None
        while time.time() < deadline:
            time.sleep(3)
            try:
                after = api(args.host, "/api/ota/info", timeout=4)
            except Exception:  # noqa: BLE001 - every network error means "not back yet"
                continue
            if after.get("uptime_ms", 1 << 30) < 60000 and after.get("sha") != before.get("sha"):
                break
            after = None
        if after is None:
            print("  the board did not come back within %ds." % args.flash_timeout)
            print("  Served %s. If the download finished, it may still be writing;"
                  % (Serve.served or "nothing"))
            print("  check /api/ota/info yourself before reflashing over USB.")
            return 3
    finally:
        httpd.shutdown()

    print("deployed in %.0fs" % (time.time() - started))
    print("  now running %s  sha %s  slot %s  up %.1fs" %
          (after.get("version"), after.get("sha"), after.get("slot"),
           after.get("uptime_ms", 0) / 1000.0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
