#!/usr/bin/env python3
"""Dump every screen's touch targets from a running display and draw them.

Drives /api/debug/touchmap (WebUIPlugin.cpp): for each screen the UI task
loads it, walks its object tree and reports each object's coords, flags,
ext click pad and event-callback count. The hit rectangle LVGL will use is
the object's coords grown by its ext click pad and clipped by every ancestor
that does not have LV_OBJ_FLAG_OVERFLOW_VISIBLE (lv_indev_search_obj only
descends into children when the point is inside the parent's coords), so
padding a button inside a tight container gains nothing; this tool computes
that effective rectangle, which is what the numbers below are.

Writes, per screen, <out>/<name>.json (the raw dump), <out>/<name>.png (the
panel framebuffer at half resolution with the hit rectangles drawn on it)
and prints a table of every target with its effective size in pixels and
millimetres (the LilyGo T-RGB is a 2.1" 480 px round panel: 0.111 mm/px).

usage: touchmap.py [--host 192.168.1.121] [--out touchmap_out] [--screens 1,2,...]
       [--no-load]   (dump the trees without switching screens; the flow-driven
                      HIDDEN flags of inactive screens are then as created)
"""
import argparse, json, os, re, struct, sys, time, urllib.request, zlib

SCREENS = {1: 'standby', 2: 'brew', 3: 'status', 4: 'menu', 5: 'menu_new', 6: 'steam', 7: 'water',
           8: 'profile', 9: 'grind', 10: 'info', 11: 'new_profile'}
MM_PER_PX = 53.3 / 480.0

HERE = os.path.dirname(os.path.abspath(__file__))
SCREENS_H = os.path.join(HERE, '..', 'src', 'display', 'ui', 'default', 'eez', 'screens.h')


def object_names():
    src = open(SCREENS_H).read()
    body = src[src.index('typedef struct _objects_t {'):src.index('} objects_t;')]
    return re.findall(r'lv_obj_t \*(\w+);', body)


def get(host, path, timeout=15):
    return urllib.request.urlopen('http://%s%s' % (host, path), timeout=timeout)


def dump_screen(host, sid, load):
    get(host, '/api/debug/touchmap?screen=%d%s' % (sid, '&load=1' if load else '')).read()
    t0 = time.time()
    while time.time() - t0 < 8:
        time.sleep(0.25)
        d = json.load(get(host, '/api/debug/touchmap'))
        if not d.get('pending') and d.get('screen') == sid:
            return d
    raise SystemExit('touchmap for screen %d did not arrive' % sid)


def grab_fb(host, step=2):
    req = get(host, '/api/debug/fb?step=%d' % step, timeout=60)
    w, h = [int(v) for v in req.headers.get('X-FB-Size', '240x240').split('x')]
    data = req.read()
    if len(data) != w * h * 2:
        return None, w, h
    px = []
    for i in range(0, len(data), 2):
        v = data[i] | (data[i + 1] << 8)
        r = (v >> 11) & 0x1f; g = (v >> 5) & 0x3f; b = v & 0x1f
        px.append([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)])
    return px, w, h


def write_png(path, px, w, h):
    rows = []
    for y in range(h):
        row = bytearray([0])
        for x in range(w):
            row += bytes(px[y * w + x])
        rows.append(bytes(row))
    raw = b''.join(rows)

    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)

    out = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    out += chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
    open(path, 'wb').write(out)


def draw_rect(px, w, h, x1, y1, x2, y2, color, scale):
    x1, y1, x2, y2 = [int(round(v * scale)) for v in (x1, y1, x2, y2)]
    for x in range(max(0, x1), min(w - 1, x2) + 1):
        for y in (y1, y2):
            if 0 <= y < h:
                px[y * w + x] = list(color)
    for y in range(max(0, y1), min(h - 1, y2) + 1):
        for x in (x1, x2):
            if 0 <= x < w:
                px[y * w + x] = list(color)


def effective_targets(d, names):
    objs = {o['i']: o for o in d['objects']}
    out = []
    for o in d['objects']:
        if not o['k'] or o['n'] == 0:
            continue
        # Hidden anywhere up the chain means unreachable.
        hidden = False
        n = o
        while n is not None:
            if n['h']:
                hidden = True
                break
            n = objs.get(n['p'])
        ty = o['ty']
        x1, y1, x2, y2 = o['x1'] - o['e'], o['y1'] - o['e'] + ty, o['x2'] + o['e'], o['y2'] + o['e'] + ty
        # Clip by ancestors that do not let overflow through.
        p = objs.get(o['p'])
        clipped_by = None
        while p is not None:
            if not p['v']:
                nx1, ny1, nx2, ny2 = max(x1, p['x1']), max(y1, p['y1'] + p['ty']), min(x2, p['x2']), min(y2, p['y2'] + p['ty'])
                if (nx1, ny1, nx2, ny2) != (x1, y1, x2, y2):
                    clipped_by = clipped_by or p
                x1, y1, x2, y2 = nx1, ny1, nx2, ny2
            p = objs.get(p['p'])
        name = names[o['o']] if 0 <= o['o'] < len(names) else '(%s #%d)' % (o['c'], o['i'])
        out.append({'name': name, 'cls': o['c'], 'hidden': hidden, 'obj': (o['x1'], o['y1'], o['x2'], o['y2']),
                    'hit': (x1, y1, x2, y2), 'ext': o['e'], 'clipped_by': clipped_by, 'events': o['n']})
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='192.168.1.121')
    ap.add_argument('--out', default='touchmap_out')
    ap.add_argument('--screens', default=','.join(str(k) for k in SCREENS))
    ap.add_argument('--no-load', action='store_true')
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    names = object_names()
    for sid in [int(v) for v in args.screens.split(',') if v]:
        sname = SCREENS[sid]
        d = dump_screen(args.host, sid, not args.no_load)
        json.dump(d, open(os.path.join(args.out, sname + '.json'), 'w'), indent=0)
        targets = effective_targets(d, names)
        print('== %s (%d objects, active=%s)' % (sname, len(d['objects']), d.get('active')))
        print('   %-34s %-7s %-22s %-22s %-9s %s' % ('target', 'class', 'object x1,y1-x2,y2', 'hit x1,y1-x2,y2', 'hit px', 'mm'))
        for t in targets:
            hx1, hy1, hx2, hy2 = t['hit']
            hw, hh = hx2 - hx1 + 1, hy2 - hy1 + 1
            note = ''
            if t['hidden']:
                note = ' hidden'
            if t['clipped_by'] is not None:
                note += ' clipped by %s' % (names[t['clipped_by']['o']] if 0 <= t['clipped_by']['o'] < len(names) else t['clipped_by']['c'])
            if t['ext']:
                note += ' ext=%d' % t['ext']
            print('   %-34s %-7s %-22s %-22s %3dx%-4d %4.1fx%-4.1f%s' % (
                t['name'], t['cls'], '%d,%d-%d,%d' % t['obj'], '%d,%d-%d,%d' % t['hit'], hw, hh,
                hw * MM_PER_PX, hh * MM_PER_PX, note))
        if not args.no_load:
            time.sleep(0.5)
            px, w, h = grab_fb(args.host)
            if px is None:
                print('   (framebuffer grab failed)')
                continue
            scale = w / 480.0
            for t in targets:
                if t['hidden']:
                    continue
                draw_rect(px, w, h, *t['obj'], (255, 220, 0), scale)
                draw_rect(px, w, h, *t['hit'], (0, 255, 80), scale)
            write_png(os.path.join(args.out, sname + '.png'), px, w, h)
    if not args.no_load:
        get(args.host, '/api/debug/touchmap?screen=1&load=1').read()


if __name__ == '__main__':
    main()
