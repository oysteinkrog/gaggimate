#!/usr/bin/env python3
"""Compares the panel's own pixels against the gradient sampler (gm-nov3.10).

    python3 tools/gradient_fb_check.py --host 192.168.1.121
    python3 tools/gradient_fb_check.py --host 192.168.1.121 --tone 60,65
    python3 tools/gradient_fb_check.py --host 192.168.1.121 --json out.json

Exits 0 when every compared sample matches and non-zero on any mismatch, on a
fixture that will not arm, or on a device whose reported tone is not the tone
that was asked for.

WHAT THIS CHECKS THAT NOTHING ELSE DOES
=======================================

tools/animbench/web/ramp_parity.js proves web/src/config/gradientRamp.js equal
to the firmware's C++ palette arithmetic, entry by entry, on the host. It
cannot see the panel. A stored brightness, a stored highlight rolloff, a
gradient reference resolving to something other than the entry under review,
or a driver dropping bits on the way to the framebuffer would all leave that
proof passing and the panel wrong.

This reads the framebuffer.

WHY IT NEEDS A FIXTURE
======================

An animated frame cannot be compared against a linear strip. An animation maps
the palette through its own pattern, its own gain and, for Plasma, a wheel
rather than a ramp, so an arbitrary moving frame has no known relation to a
ramp. A comparison that cannot say which ramp index each pixel came from is
not a comparison.

So the firmware paints a known one. /api/debug/gradfix?on=1 arms a bench-only
fixture (GM_TOUCH_PROBE builds) that writes rows [y0, y1) of every band with
the active theme's 256-entry ramp, after every compositing stage and before
the push, so no overlay, layer, element or scrim can reach those rows. The
same request reports the stops before tone, the interpolation mode, both tone
percentages, the gain and the region, and this script feeds those back into
tools/gradient_samples.js rather than assuming any of them.

THE SAMPLE MAPPING
==================

Column x of a fixture row carries ramp index

    ((x + xoff) * 255) / (w - 1)      integer division, clamped to 255

with w = 480, so at xoff 0 index 0 is column 0 and index 255 is column 479.

/api/debug/fb delivers the framebuffer subsampled, and this check reads it at
step 2, which hands back even columns and even rows only. Even columns alone
never include 479, so the fixture is read twice, at xoff 0 and xoff 1, and the
second pass puts the index that sat on column 479 onto column 478. The two
passes together cover the index of every column, ramp index 255 included.

Every row of the strip is painted identically, so the rows are compared
individually rather than averaged: two rows that disagree is itself a finding
and the report names it.

WHAT A MISMATCH MEANS
=====================

Both sides are integer arithmetic over the same stops and the panel stores
RGB565, so the expected result is exact equality, not a tolerance. A non-zero
mismatch count means the firmware and gradientRamp.js have diverged, or
something is writing the strip after the fixture does.
"""

import argparse
import json
import os
import subprocess
import sys
import time
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SAMPLER = os.path.join(ROOT, 'tools', 'gradient_samples.js')

# Modes the fixture takes, and the name gradient_samples.js knows them by.
MODES = {'ramp': 1, 'reversed': 2, 'wheel': 3}


# A request can still time out under the render task's own load, which is a
# transport fault and not a finding, so every read is retried. A short frame is
# no longer among the faults worth tolerating: the endpoint used to end a
# chunked response early whenever the remaining TCP window was smaller than one
# output row, and this check used to accept whatever arrived as long as it
# reached the fixture rows. That is fixed in the firmware (gm-6ivh), so a short
# frame now means something is wrong and the read is retried and then reported.
RETRIES = 8


def _retry(what, fn):
    last = None
    for attempt in range(RETRIES):
        try:
            return fn()
        except Exception as exc:  # noqa: BLE001 - any transport fault is worth retrying
            last = exc
            time.sleep(0.5 * (attempt + 1))
    raise RuntimeError('%s failed %d times, last: %s' % (what, RETRIES, last))


def get_json(host, path, timeout=20):
    def once():
        with urllib.request.urlopen('http://%s%s' % (host, path), timeout=timeout) as resp:
            return json.loads(resp.read().decode('utf-8'))

    return _retry('GET ' + path, once)


def get_fb(host, step, n=0, timeout=90):
    """The whole framebuffer dump, or an error.

    The rows arrive top of frame first, so a short read is a prefix of the real
    frame, which is why this check used to accept one that reached the fixture
    rows. It does not any more: the endpoint delivers the whole frame, and a
    frame that arrives short says the transport or the firmware has a problem
    the comparison below would hide."""

    def once():
        with urllib.request.urlopen('http://%s/api/debug/fb?n=%d&step=%d' % (host, n, step), timeout=timeout) as resp:
            size = resp.headers.get('X-FB-Size', '')
            data = resp.read()
        w, h = (int(v) for v in size.split('x'))
        rows = len(data) // (w * 2)
        if rows != h:
            raise RuntimeError('fb step %d: %d of %d rows' % (step, rows, h))
        return w, h, rows, data

    w, h, rows, data = _retry('GET /api/debug/fb?n=%d&step=%d' % (n, step), once)
    px = [data[i] | (data[i + 1] << 8) for i in range(0, rows * w * 2, 2)]
    return w, rows, px


def sample_ramp(wire, brightness_pct, knee_pct, gain256, mode, node='node'):
    """The 256 RGB565 entries gradientRamp.js says the panel will store."""
    req = {
        'brightnessPct': brightness_pct,
        'kneePct': knee_pct,
        'gain256': gain256,
        'mode': mode,
        'gradients': [wire],
    }
    out = subprocess.run([node, SAMPLER], input=json.dumps(req).encode('utf-8'),
                         stdout=subprocess.PIPE, check=True).stdout
    ramps = json.loads(out.decode('utf-8'))['ramps']
    if ramps[0] is None:
        raise RuntimeError('the sampler rejects the stops the device reported: %r' % wire)
    return ramps[0]


def arm(host, mode, y0, y1, xoff, btone, ktone, settle_frames, stops=None, timeout_s=15.0):
    """Arms the fixture and waits until it has actually painted frames.

    stops, when given, is a gradient wire string held on the panel through the
    editor's live-preview path for as long as this pass needs it. Waiting for
    the device to report those same stops is what makes a batch safe: the
    preview lapses on a timer, and a pass that sampled after it lapsed would
    otherwise be compared against the gradient that was asked for rather than
    the one that was drawn."""
    q = '/api/debug/gradfix?on=%d&y0=%d&y1=%d&xoff=%d' % (MODES[mode], y0, y1, xoff)
    if btone is not None:
        q += '&btone=%d&ktone=%d' % (btone, ktone)
    if stops is not None:
        q += '&stops=' + stops
    st = get_json(host, q)
    if not st['armed']:
        raise RuntimeError('the fixture refused to arm: %s' % json.dumps(st))
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        st = get_json(host, '/api/debug/gradfix')
        tone_ok = btone is None or (st['brightnessPct'] == btone and st['kneePct'] == ktone)
        stops_ok = stops is None or st['stops'] == stops
        if st['frames'] >= settle_frames and tone_ok and stops_ok:
            return st
        time.sleep(0.25)
    raise RuntimeError('the fixture did not settle in %.0f s: %s' % (timeout_s, json.dumps(st)))


def check_tone_conversion(st):
    """The percent-to-integer conversion DefaultUI does, checked on the device.

    The device reports both the percentages and the integers its render task
    ended up with. If they disagree the host is about to sample a tone the
    panel is not drawing, so this is a stop rather than a note."""
    want_b = (st['brightnessPct'] * 256) // 100
    want_k = (st['kneePct'] * 255) // 100
    problems = []
    if want_b != st['brightness256']:
        problems.append('brightness %d%% converts to %d, device reports %d'
                        % (st['brightnessPct'], want_b, st['brightness256']))
    if want_k != st['knee']:
        problems.append('rolloff %d%% converts to %d, device reports %d'
                        % (st['kneePct'], want_k, st['knee']))
    return problems


def compare(st, ramp, px, fbw, step):
    """Every sampled pixel of the strip against the ramp entry its column names."""
    w = st['w']
    y0, y1, xoff = st['y0'], st['y1'], st['xoff']
    compared = 0
    # Counted in full; only the first few are kept, because a wholly wrong
    # strip is 9,600 of them and a capped count would read as a small fault.
    bad = 0
    examples = []
    indices = set()
    rows_seen = 0
    for oy in range(y0 // step + (1 if y0 % step else 0), (y1 + step - 1) // step):
        y = oy * step
        if y < y0 or y >= y1:
            continue
        rows_seen += 1
        for ox in range(fbw):
            x = ox * step
            idx = min(((x + xoff) * 255) // (w - 1), 255)
            got = px[oy * fbw + ox]
            want = ramp[idx]
            compared += 1
            indices.add(idx)
            if got != want:
                bad += 1
                if len(examples) < 20:
                    examples.append({'x': x, 'y': y, 'index': idx, 'got': got, 'want': want})
    return compared, rows_seen, sorted(indices), bad, examples


class StaleFixture(Exception):
    """The device's fixture state moved between arming it and reading it."""


def run_pass(host, args, mode, xoff, btone, ktone, stops=None):
    """One armed strip, read back and compared.

    The pass is retried when the device's state moves under it. It can: a
    gradient held through the preview path lapses on a timer, and a framebuffer
    read that needed several attempts can outlast it, which would compare the
    gradient that was asked for against a strip painted with the one that came
    back. So the state is read again after the pixels and the pass is discarded
    if it moved."""
    for attempt in range(3):
        try:
            return run_pass_once(host, args, mode, xoff, btone, ktone, stops)
        except StaleFixture as exc:
            if attempt == 2:
                raise RuntimeError('the device kept changing under the read: %s' % exc)
    raise AssertionError('unreachable')


def run_pass_once(host, args, mode, xoff, btone, ktone, stops=None):
    st = arm(host, mode, args.y0, args.y1, xoff, btone, ktone, args.settle, stops=stops)
    problems = check_tone_conversion(st)
    if problems:
        raise RuntimeError('the device\'s tone does not match its own percentages: ' + '; '.join(problems))
    ramp = sample_ramp(st['stops'], st['brightnessPct'], st['kneePct'], st['gain256'], mode, node=args.node)
    results = []
    for n in args.buffers:
        # Both framebuffers, because the fixture vetoes interlacing precisely so
        # that both carry the strip; a stale second buffer would say the veto is
        # not working.
        fbw, fbrows, px = get_fb(host, args.step, n)
        compared, rows, indices, bad, examples = compare(st, ramp, px, fbw, args.step)
        results.append({'buffer': n, 'compared': compared, 'rows': rows, 'fb_rows': fbrows,
                        'indices': len(indices), 'index_max': indices[-1] if indices else None,
                        'mismatches': bad, 'first': examples[:5]})
    after = get_json(host, '/api/debug/gradfix')
    if (after['stops'] != st['stops'] or after['brightnessPct'] != st['brightnessPct']
            or after['kneePct'] != st['kneePct'] or not after['armed'] or after['xoff'] != st['xoff']):
        raise StaleFixture('%s -> %s' % (json.dumps(st), json.dumps(after)))
    return st, results


def load_gradients(path):
    """data/gradients.json as the firmware's parser reads it: bare hex stops,
    comma separated, no positions, so the uniform path."""
    with open(path, encoding='utf-8') as f:
        doc = json.load(f)
    out = []
    for i, g in enumerate(doc['gradients']):
        wire = ','.join(h.lstrip('#').lower() for h in g['stops'])
        out.append((i, g['name'], wire))
    return out


def run_batch(host, args, btone, ktone, record):
    """Every built-in in data/gradients.json, on the panel, one at a time.

    This is what gm-nov3.5's appended batch needs: the entries are put on the
    panel through the preview path, so nothing stored is written and nothing
    has to be put back."""
    failed = False
    for index, name, wire in load_gradients(args.source)[:args.limit]:
        for xoff in (0, 1):
            st, results = run_pass(host, args, args.mode, xoff, btone, ktone, stops=wire)
            bad = sum(r['mismatches'] for r in results)
            total = sum(r['compared'] for r in results)
            failed = failed or bad != 0
            print('%3d %-16s xoff %d: %d samples, %d mismatches' % (index, name, xoff, total, bad))
            record['passes'].append({'gradient': name, 'index': index, 'stops': st['stops'],
                                     'brightnessPct': st['brightnessPct'], 'kneePct': st['kneePct'],
                                     'xoff': xoff, 'results': results})
    get_json(host, '/api/debug/gradfix?stops=')
    return failed


def self_test(host, args):
    """Proves the comparison can fail, on this board, in this run.

    A check that only ever passes is not evidence. Two controls, both of which
    must produce mismatches: the same armed strip read against a ramp sampled
    at a tone the device is not drawing, and the correct ramp read against a
    framebuffer with the fixture switched off (so the region holds the
    animation). Returns a list of problems, empty when both controls fired."""
    problems = []
    st = arm(host, args.mode, args.y0, args.y1, 0, 100, 100, args.settle)
    right = sample_ramp(st['stops'], 100, 100, st['gain256'], args.mode, node=args.node)
    wrong = sample_ramp(st['stops'], 50, 50, st['gain256'], args.mode, node=args.node)
    fbw, _, px = get_fb(host, args.step, 0)
    _, _, _, bad_right, _ = compare(st, right, px, fbw, args.step)
    _, _, _, bad_wrong, _ = compare(st, wrong, px, fbw, args.step)
    print('self test, armed strip against its own ramp: %d mismatches' % bad_right)
    print('self test, same strip against a 50/50 ramp:  %d mismatches' % bad_wrong)
    if bad_right != 0:
        problems.append('the armed strip did not match its own ramp')
    if bad_wrong == 0:
        problems.append('a deliberately wrong tone still matched, so the comparison proves nothing')
    get_json(host, '/api/debug/gradfix?on=0&btone=-1&ktone=-1')
    time.sleep(2.0)
    fbw, _, px = get_fb(host, args.step, 0)
    _, _, _, bad_off, _ = compare(st, right, px, fbw, args.step)
    print('self test, fixture off against the ramp:     %d mismatches' % bad_off)
    if bad_off == 0:
        problems.append('the region matched the ramp with the fixture off, so the strip is not what was read')
    return problems


def main(argv=None):
    ap = argparse.ArgumentParser(description='Compare the panel against the gradient sampler')
    ap.add_argument('--host', default='192.168.1.121')
    ap.add_argument('--mode', default='ramp', choices=sorted(MODES))
    ap.add_argument('--y0', type=int, default=200, help='first fixture row (default 200)')
    ap.add_argument('--y1', type=int, default=240, help='one past the last fixture row (default 240)')
    ap.add_argument('--step', type=int, default=2,
                    help='the /api/debug/fb subsampling step (default 2, which needs the two xoff passes '
                         'to cover every column; step 1 reads every column in one pass)')
    ap.add_argument('--tone', action='append', default=None,
                    help='a brightness,rolloff percentage pair to hold while sampling; '
                         'repeatable. Omitted, the device keeps its stored tone.')
    ap.add_argument('--settle', type=int, default=6,
                    help='fixture frames to wait for before reading the framebuffer')
    ap.add_argument('--buffers', default='0,1', help='framebuffer indices to read (default both)')
    ap.add_argument('--node', default='node')
    ap.add_argument('--json', default=None, help='write the full record here')
    ap.add_argument('--batch', action='store_true',
                    help='walk every gradient in data/gradients.json instead of the device\'s own theme')
    ap.add_argument('--source', default=os.path.join(ROOT, 'data', 'gradients.json'))
    ap.add_argument('--limit', type=int, default=None, help='with --batch, stop after this many gradients')
    ap.add_argument('--selftest', action='store_true',
                    help='also run the two negative controls that prove the comparison can fail')
    args = ap.parse_args(argv)
    args.buffers = [int(v) for v in args.buffers.split(',')]

    tones = []
    if args.tone:
        for t in args.tone:
            parts = t.split(',')
            if len(parts) != 2:
                sys.stderr.write('gradient_fb_check: --tone takes brightness,rolloff in percent\n')
                return 2
            tones.append((int(parts[0]), int(parts[1])))
    else:
        tones.append((None, None))

    before = get_json(args.host, '/api/debug/gradfix')
    print('firmware %s built %s' % (before['fw'], before['built']))
    print('panel %dx%d, fixture rows %d..%d, fb step %d'
          % (before['w'], before['h'], args.y0, args.y1, args.step))

    record = {'host': args.host, 'fw': before['fw'], 'built': before['built'],
              'mode': args.mode, 'step': args.step, 'passes': []}
    failed = False
    try:
        for btone, ktone in tones:
            if args.batch:
                failed = run_batch(args.host, args, btone, ktone, record) or failed
                continue
            for xoff in (0, 1):
                st, results = run_pass(args.host, args, args.mode, xoff, btone, ktone)
                total = sum(r['compared'] for r in results)
                bad = sum(r['mismatches'] for r in results)
                failed = failed or bad != 0
                print('tone %d/%d, xoff %d, stops %s%s: %d samples, %d ramp indices up to %d, %d mismatches'
                      % (st['brightnessPct'], st['kneePct'], xoff, st['stops'],
                         '' if st['uniform'] else ' (positional)',
                         total, results[0]['indices'], results[0]['index_max'], bad))
                for r in results:
                    if r['mismatches']:
                        print('  buffer %d: %s' % (r['buffer'], json.dumps(r['first'])))
                record['passes'].append({'brightnessPct': st['brightnessPct'], 'kneePct': st['kneePct'],
                                         'brightness256': st['brightness256'], 'knee': st['knee'],
                                         'gain256': st['gain256'], 'stops': st['stops'],
                                         'uniform': st['uniform'], 'themeGen': st['themeGen'],
                                         'xoff': xoff, 'y0': st['y0'], 'y1': st['y1'],
                                         'map': st['map'], 'results': results})
    finally:
        # Disarm and release the tone override whatever happened above, so a
        # failed run does not leave the panel showing a strip.
        off = get_json(args.host, '/api/debug/gradfix?on=0&btone=-1&ktone=-1&stops=')
        print('fixture off (armed=%s, tone %d/%d)' % (off['armed'], off['brightnessPct'], off['kneePct']))
        record['after'] = off

    if args.selftest:
        problems = self_test(args.host, args)
        record['selftest'] = problems
        for pr in problems:
            print('self test FAILED: %s' % pr)
        failed = failed or bool(problems)
        get_json(args.host, '/api/debug/gradfix?on=0&btone=-1&ktone=-1')
    record['mismatches'] = sum(r['mismatches'] for p in record['passes'] for r in p['results'])
    record['compared'] = sum(r['compared'] for p in record['passes'] for r in p['results'])
    if args.json:
        with open(args.json, 'w', encoding='utf-8') as f:
            json.dump(record, f, indent=2)
        print('wrote %s' % args.json)
    print('%s: %d samples compared, %d mismatches'
          % ('FAIL' if failed else 'PASS', record['compared'], record['mismatches']))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
