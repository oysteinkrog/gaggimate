# Per-animation frame cost on the device, with the animation's own frame()
# call timed separately (framefn_us) from the band kernel (band_us). Runs
# each animation through the web preview (not stored), interlace pinned to
# the value in ILACE (default: the stored one), and reads the fps from the
# anim_frames delta over a window. Puts the interlace pin back at the end.
# usage: ILACE=0|1 WIN=6 python3 tools/framefn_sweep.py [ids...]
# Needs the websocket-client package. The board must run a build with the
# web preview (every display build) and framefn_us (2026-09-11 or later).
# The rows are only comparable with each other within one run: the screen
# the board is on and the radio's state move every stage by up to 2x
# between runs (see "Measuring the display rig" in CLAUDE.md).
import sys, os, time, json, urllib.request, websocket
HOST = "192.168.1.121"
STOPS = "080402,2a1206,6b3413,b8703a,e8b268,f8e6c8"
NAMES = ["plasma","lava","silk","starfield","aurora","ripples","caustics","mandala","orbits","fireflies","steam","ember","nebula","silk2","brushed","horizon","oculus","chevrons","mosaic","saddle","refraction","sundial","crescent","glint","tunnel","kaleido","shafts","weave","lens","tide","truchet","quilt","rain","stripes","ribbon","harmonograph","floor","hills","gyroid","barrel","grid","cells","dimples","cube"]
ids = [int(a) for a in sys.argv[1:]] or list(range(44))
WIN = float(os.environ.get("WIN", "6"))
ILACE = os.environ.get("ILACE", "")

def get(path, tries=3):
    for k in range(tries):
        try:
            return json.load(urllib.request.urlopen("http://%s%s" % (HOST, path), timeout=15))
        except Exception:
            if k == tries - 1: raise
            time.sleep(1.0)

ws = None
def preview(a):
    global ws
    for k in range(3):
        try:
            if ws is None:
                ws = websocket.create_connection("ws://%s/ws" % HOST, timeout=10)
            ws.send(json.dumps({"tp": "req:bganim:preview", "anim": a, "stops": STOPS}))
            return
        except Exception:
            try: ws.close()
            except Exception: pass
            ws = None
            time.sleep(1.0)

if ILACE:
    get("/api/debug/anim?interlace=%s" % ILACE)
d0 = get("/api/debug/anim")
print("interlace=%s fps_cap=%s" % (d0.get("interlace"), d0.get("fps_cap")))
print("%-13s %6s %8s %8s %8s %8s %8s %8s %8s" % ("anim", "fps", "framefn", "band", "blend", "push", "work", "frame", "wait"))
try:
    for a in ids:
        preview(a)
        time.sleep(3.5)
        s0 = get("/api/debug/anim")
        t0 = time.time()
        rows = []
        while time.time() - t0 < WIN:
            time.sleep(1.0)
            if int(time.time() - t0) % 3 == 0: preview(a)
            d = get("/api/debug/anim")
            rows.append(d)
        s1 = rows[-1]
        dt = (s1["uptime_ms"] - s0["uptime_ms"]) / 1000.0
        fps = (s1["anim_frames"] - s0["anim_frames"]) / dt if dt > 0 else 0
        med = lambda k: sorted(r.get(k, 0) for r in rows)[len(rows) // 2] / 1000.0
        print("%-13s %6.1f %8.1f %8.1f %8.1f %8.1f %8.1f %8.1f %8.1f" % (NAMES[a], fps, med("framefn_us"), med("band_us"), med("blend_us"), med("push_us"), med("work_us"), med("frame_us"), med("wait_us")), flush=True)
finally:
    try:
        ws.send(json.dumps({"tp": "req:bganim:preview-end"}))
        ws.close()
    except Exception:
        pass
    if ILACE:
        get("/api/debug/anim?interlace=-1")
