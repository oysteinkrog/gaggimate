#!/usr/bin/env python3
"""Device parity for every animation kernel: band() against its portable twin.

    python3 tools/anim_parity.py                    every animation in the registry
    python3 tools/anim_parity.py 14 15              two of them
    FRAMES=8 HOST=192.168.1.121 python3 tools/anim_parity.py
    python3 tools/anim_parity.py --host-test        the gate itself, no board needed

Every animation whose band() dispatches to a hand-written Xtensa kernel keeps
the portable C++ it replaced as bandRef(). This drives /api/debug/animtest,
which renders every band of several frames and three parameter sets through
both paths back to back, alternating which runs first band by band, and
reports the first differing pixel. The run exits 0 only when every animation
in the registry was asked for, answered with a structurally complete result,
and compared clean.

WHY THE BOARD AND NOT THE HOST
==============================

The host cannot show this class of fault. glibc malloc is 16-byte aligned, so
a kernel whose vector stores run off the front of a 4-byte aligned ps_malloc
block passes the goldens, the ASan fuzz, the lifecycle check and QEMU, and
still corrupts the device heap (Truchet's blendLast, 2026-09-12). This
endpoint is what found that one.

WHERE THE ENDPOINT LIVES
========================

/api/debug/animtest is registered in WebUIPluginDebug.cpp inside the real
panel block, which opens with

    #if !defined(GAGGIMATE_HEADLESS) && !defined(GAGGIMATE_SIM)

and is not inside the GM_TOUCH_PROBE or GM_ANIM_BENCH blocks that sit near it.
So it ships in production firmware on the T-RGB panel, and is absent from the
simulator and the headless build. An earlier version of this header said
"loadtest and bench builds only". That was wrong.

WHAT THIS CANNOT ESTABLISH
==========================

It compares band() against bandRef() as the running firmware compiled them.
It says nothing about a path the running firmware did not compile.

Eleven animations (Aurora, Caustics, Ember, Fireflies, Mandala, Nebula,
Orbits, Plasma, Ripples, Starfield, Steam) have no named GM_BGANIM_<NAME>_ASM
switch: their Xtensa path is guarded only by the fleet-wide GM_BGANIM_NO_ASM.
Their kernels are compared here like any other, but there is no build-time way
to turn one of them off on its own for an A/B, because GM_BGANIM_NO_ASM
changes the whole fleet at once. The runtime /api/debug/anim?useref=1 path is
the only isolated A/B for those eleven unless a named switch is added. The
tool prints the current set at the end of a run rather than trusting this
paragraph.

An animation whose named switch defaults to 0 is worse than untested: its
band() is one line that calls bandRef(), so the device compares a function
against itself and reports 0 differing pixels, as it must. That is noncoverage,
not parity. DORMANT below names every such animation, the run refuses to start
if the sources disagree with it, and the summary counts those animations
separately and never as a pass. Nothing reachable from the board can test a
dormant kernel; the flag has to be turned on first, and then the ladder in
CLAUDE.md applies (host goldens, the real compiler's disassembly, QEMU
bit-exactness, and only then this tool).
"""

import argparse
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BGANIM_DIR = os.path.join(ROOT, 'src', 'display', 'ui', 'default', 'bganim')

# The registry this tool was written against, in registration order, which is
# the persisted animation id. This is a snapshot, not the authority: every run
# re-reads BgAnimRegistry.cpp and refuses to start if the two disagree, so
# appending an animation without touching this list fails the run instead of
# quietly checking the first 44.
NAMES = [
    "plasma", "lava", "silk", "starfield", "aurora", "ripples", "caustics", "mandala",
    "orbits", "fireflies", "steam", "ember", "nebula", "silk2", "brushed", "horizon",
    "oculus", "chevrons", "mosaic", "saddle", "refraction", "sundial", "crescent", "glint",
    "tunnel", "kaleido", "shafts", "weave", "lens", "tide", "truchet", "quilt",
    "rain", "stripes", "ribbon", "harmonograph", "floor", "hills", "gyroid", "barrel",
    "grid", "cells", "dimples", "cube",
]

# Animations whose Xtensa kernel is compiled out of the shipped build, so the
# device has nothing independent to compare. The key is the animation id, the
# value is why. Re-derived from the sources on every run: an animation that
# becomes dormant without being declared here, or is declared here and is no
# longer dormant, fails the run.
DORMANT = {
    "silk": "GM_BGANIM_SILK_ASM defaults to 0 (AnimSilk.cpp), and under that "
            "default band() is one line that calls bandRef(). The kernel lost "
            "to the compiler on the chip (2026-09-04) and stays in the file "
            "for the next attempt.",
}

# The fields a result must carry before it means anything. Their types are
# checked too: a string where a count belongs is not a count.
REQUIRED_FIELDS = ("anim", "id", "frames", "has_ref", "init_failed", "bands", "mismatch_px")

# requestAnimTest() clamps the frame count into this range, so a request
# outside it comes back echoing a different number and would fail the echo
# check for no useful reason.
FRAMES_MIN, FRAMES_MAX = 1, 64

# Three parameter sets per run (defaults, all zero, all 100), so the band
# count is always a positive multiple of this times the frame count.
PSETS = 3


class InventoryError(Exception):
    """The tool and the firmware sources disagree about the fleet."""


class ResultError(Exception):
    """The device did not answer with a usable result."""


# ---------------------------------------------------------------- inventory

def strip_comments(text):
    """C++ comments out, string literals left alone.

    The registry and the animation sources carry long prose comments, some of
    them holding braces and ampersands, so a regex over the raw file finds
    registrations that are not there."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            quote = c
            out.append(c)
            i += 1
            while i < n:
                out.append(text[i])
                if text[i] == '\\':
                    if i + 1 < n:
                        out.append(text[i + 1])
                        i += 2
                        continue
                elif text[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            while i < n and text[i] != '\n':
                i += 1
            continue
        if c == '/' and i + 1 < n and text[i + 1] == '*':
            i += 2
            while i + 1 < n and not (text[i] == '*' and text[i + 1] == '/'):
                i += 1
            i += 2
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def read_source(path):
    with open(path, encoding='utf-8') as f:
        return f.read()


def registry_symbols(bganim_dir):
    """The bg_anim_* symbols of REGISTRY[], in registration order."""
    path = os.path.join(bganim_dir, 'BgAnimRegistry.cpp')
    try:
        text = strip_comments(read_source(path))
    except OSError as exc:
        raise InventoryError('cannot read the registry: %s' % exc)
    m = re.search(r'REGISTRY\s*\[\s*\]\s*=\s*\{(.*?)\}\s*;', text, re.S)
    if m is None:
        raise InventoryError('%s does not hold a REGISTRY[] initialiser' % path)
    syms = re.findall(r'&\s*bg_anim_(\w+)', m.group(1))
    if not syms:
        raise InventoryError('REGISTRY[] in %s registers nothing' % path)
    return syms


def animation_ids(bganim_dir):
    """Every `const BgAnimation bg_anim_X = {"id", ...}` in the directory.

    Returns {symbol suffix: (id string, file name)}."""
    found = {}
    for name in sorted(os.listdir(bganim_dir)):
        if not name.endswith('.cpp'):
            continue
        text = strip_comments(read_source(os.path.join(bganim_dir, name)))
        for sym, ident in re.findall(
                r'\bconst\s+BgAnimation\s+bg_anim_(\w+)\s*=\s*\{\s*"([^"]*)"', text):
            found[sym] = (ident, name)
    return found


def asm_switches(bganim_dir):
    """{ANIMATION_NAME: (value, file)} for every `#define GM_BGANIM_<X>_ASM n`.

    The default lives in the animation's own source behind an #ifndef, so a
    -D on the build line overrides it and this scan does not see that. A build
    that turns a kernel on or off from platformio.ini would therefore have to
    say so here; none does today, and `grep GM_BGANIM platformio.ini` is the
    check."""
    out = {}
    for name in sorted(os.listdir(bganim_dir)):
        if not name.endswith('.cpp'):
            continue
        text = strip_comments(read_source(os.path.join(bganim_dir, name)))
        for flag, value in re.findall(r'#\s*define\s+GM_BGANIM_([A-Z0-9_]+)_ASM\s+(\d+)', text):
            out[flag] = (int(value), name)
    return out


class Inventory:
    """The fleet as the firmware sources describe it."""

    def __init__(self, names, dormant, unswitched):
        self.names = names
        self.dormant = dormant        # {id: flag text} compiled out of the build
        self.unswitched = unswitched  # ids with no named switch, so no isolated A/B


def build_inventory(bganim_dir=BGANIM_DIR, expect_names=None, expect_dormant=None):
    """Reads the registry and the per-animation switches, and checks both
    against what this tool was written for.

    Raises InventoryError, which aborts the run, when the sources have moved.
    That is the point: appending a registry entry, or letting a kernel go
    dormant, has to fail rather than shrink what a passing run covered."""
    expect_names = NAMES if expect_names is None else expect_names
    expect_dormant = DORMANT if expect_dormant is None else expect_dormant

    syms = registry_symbols(bganim_dir)
    defs = animation_ids(bganim_dir)
    names = []
    for sym in syms:
        if sym not in defs:
            raise InventoryError('REGISTRY[] names bg_anim_%s and no source defines it' % sym)
        ident, src = defs[sym]
        if ident != sym:
            # The device echoes the id string, and this tool compares it
            # against the name it derived. If the two ever part company the
            # comparison is against the wrong label, so stop instead.
            raise InventoryError('bg_anim_%s in %s carries id "%s"; symbol and id must agree'
                                 % (sym, src, ident))
        names.append(ident)

    if names != list(expect_names):
        raise InventoryError(
            'the registry has moved under this tool: %d animations in %s, %d in NAMES.\n'
            '  registry: %s\n  NAMES:    %s\n'
            'Update NAMES in tools/anim_parity.py to the registry order and run again.'
            % (len(names), os.path.join(bganim_dir, 'BgAnimRegistry.cpp'), len(expect_names),
               ', '.join(names), ', '.join(expect_names)))

    switches = asm_switches(bganim_dir)
    dormant, unswitched = {}, []
    # The switch for animation "foo" is GM_BGANIM_FOO_ASM by convention, and
    # this match is the convention, not a fact the compiler enforces. An
    # animation that named its switch something else would be listed as having
    # none, which understates what can be A/B'd and never overstates what was
    # compared. Today the eleven this finds are exactly the eleven that have
    # only the fleet-wide GM_BGANIM_NO_ASM guard.
    for ident in names:
        flag = ident.upper()
        if flag not in switches:
            unswitched.append(ident)
            continue
        value, src = switches[flag]
        if value == 0:
            dormant[ident] = 'GM_BGANIM_%s_ASM is 0 in %s' % (flag, src)

    undeclared = sorted(set(dormant) - set(expect_dormant))
    if undeclared:
        raise InventoryError(
            'a kernel is compiled out of the shipped build and is not declared '
            'noncoverage: %s.\n  %s\n'
            'The device compares bandRef() against itself for these, so a run '
            'that counted them as passes would be claiming work it did not do. '
            'Add them to DORMANT with the reason, or turn the switch back on.'
            % (', '.join(undeclared), '\n  '.join(dormant[d] for d in undeclared)))

    revived = sorted(set(expect_dormant) - set(dormant))
    if revived:
        raise InventoryError(
            'DORMANT declares %s noncoverage and the sources no longer agree. '
            'If the switch is back on, drop the entry so the run counts the '
            'animation as compared.' % ', '.join(revived))

    return Inventory(names, dormant, unswitched)


# ------------------------------------------------------------------- device

def make_getter(host, tries=4, timeout=30, sleep=time.sleep):
    """A GET that parses JSON and retries transport faults.

    A request can time out under the render task's own load, which is not a
    finding. An HTTP status, a body that is not JSON, and a body that is JSON
    but not an object are all findings and are not retried away: they are what
    a missing route, the wrong firmware or a truncated response look like."""

    def get(path):
        url = 'http://%s%s' % (host, path)
        last = None
        for k in range(tries):
            try:
                with urllib.request.urlopen(url, timeout=timeout) as resp:
                    body = resp.read()
            except urllib.error.HTTPError as exc:
                raise ResultError('GET %s: HTTP %s' % (path, exc.code))
            except Exception as exc:  # noqa: BLE001 - any transport fault is worth retrying
                last = exc
                if k == tries - 1:
                    raise ResultError('GET %s failed %d times, last: %s' % (path, tries, exc))
                sleep(1.5)
                continue
            try:
                r = json.loads(body.decode('utf-8'))
            except Exception as exc:  # noqa: BLE001 - a non-JSON body is a finding
                raise ResultError('GET %s: the body is not JSON (%s): %r' % (path, exc, body[:120]))
            if not isinstance(r, dict):
                raise ResultError('GET %s: the body is not a JSON object: %r' % (path, body[:120]))
            return r
        raise ResultError('GET %s failed %d times, last: %s' % (path, tries, last))

    return get


def run_one(get, anim, frames, deadline_s=120.0, sleep=time.sleep, now=time.time):
    """Queues one run and returns the result it published.

    The render task runs the test between frames, so the result arrives a few
    frames later. seq advances by two per run and is left odd while the result
    is being written, so this run's publish is the first even seq at least two
    past the one read before the request. Two past, not merely different:
    reading seq0 inside another run's publish window gives an odd S-1, and
    that run then lands on S, which "different and even" would accept as ours.

    Which animation the result names is a checked field, not part of the
    accept condition. A result for another animation means the board answered
    the wrong question, and the run has to say that rather than keep polling
    until the deadline and report a timeout."""
    before = get('/api/debug/animtest')
    seq0 = before.get('seq', 0)
    get('/api/debug/animtest?anim=%d&frames=%d' % (anim, frames))
    end = now() + deadline_s
    while now() < end:
        sleep(0.7)
        r = get('/api/debug/animtest')
        if r.get('pending'):
            continue
        seq = r.get('seq')
        if not isinstance(seq, int) or seq % 2 != 0 or seq < seq0 + 2:
            continue
        return r
    raise ResultError('anim %d: no result within %g s' % (anim, deadline_s))


def check_result(r, anim, frames, name):
    """Everything that has to hold before a row counts as work done.

    Returns a list of problems, empty when the result is complete and clean.
    A field that is absent defaults to nothing here: the whole point is that
    an incomplete result is a failure rather than a pass built out of zeros."""
    problems = []
    missing = [f for f in REQUIRED_FIELDS if f not in r]
    if missing:
        return ['the result omits %s' % ', '.join(missing)]

    if r['anim'] != anim:
        problems.append('the result is for animation %r, not %d' % (r['anim'], anim))
    if r['id'] != name:
        problems.append('the board calls animation %d %r, the registry calls it %r'
                        % (anim, r['id'], name))
    if r['frames'] != frames:
        problems.append('asked for %d frames, the result reports %r' % (frames, r['frames']))

    if not isinstance(r['has_ref'], bool):
        problems.append('has_ref is %r, not a boolean' % (r['has_ref'],))
    elif not r['has_ref']:
        problems.append('no reference path: bandRef is null, so nothing was compared')
    if not isinstance(r['init_failed'], bool):
        problems.append('init_failed is %r, not a boolean' % (r['init_failed'],))
    elif r['init_failed']:
        problems.append('init() failed, so nothing was rendered')

    bands = r['bands']
    if not isinstance(bands, int) or isinstance(bands, bool):
        problems.append('bands is %r, not a count' % (bands,))
    elif bands <= 0:
        problems.append('bands is %d: no band was compared' % bands)
    elif bands % (PSETS * frames) != 0:
        problems.append('bands is %d, not a multiple of %d frames x %d parameter sets'
                        % (bands, frames, PSETS))

    mism = r['mismatch_px']
    if not isinstance(mism, int) or isinstance(mism, bool):
        problems.append('mismatch_px is %r, not a count' % (mism,))
    elif mism != 0:
        problems.append('%d differing pixels: %s' % (mism, first_pixel(r)))
    return problems


def first_pixel(r):
    """Where the two paths first parted, as the endpoint reported it.

    Only reached when mismatch_px is non-zero, and every field is printed with
    %s rather than a numeric format: this is the one place the tool formats
    values it has not type-checked, and a board answering with a string there
    should not turn a real mismatch into a traceback."""
    f = r.get('first')
    if not isinstance(f, dict):
        return 'the result carries no first differing pixel'
    return 'f%s/p%s %s,%s %s!=%s' % (
        f.get('frame', '?'), f.get('pset', '?'), f.get('x', '?'), f.get('y', '?'),
        _hex(f.get('got')), _hex(f.get('want')))


def _hex(v):
    return ('%04x' % v) if isinstance(v, int) and not isinstance(v, bool) else repr(v)


ROW = '%-3s %-13s %-9s %-7s %-7s %-9s %-9s %-6s %s'


def check_fleet(get, inv, ids, frames, out=None, deadline_s=120.0,
                sleep=time.sleep, now=time.time):
    """Drives the board over `ids` and returns the list of problems.

    `out` is resolved here rather than in the signature: a default bound at
    import time keeps writing to the real stdout after a caller has replaced
    it, which is how the host test's own output used to carry a full fleet
    report from a case it was running under capture."""
    out = sys.stdout if out is None else out
    problems, compared, skipped = [], [], []
    print(ROW % ('id', 'name', 'coverage', 'bands', 'mism', 'band_us', 'ref_us', 'ratio', 'note'),
          file=out, flush=True)
    for anim in ids:
        if anim < 0 or anim >= len(inv.names):
            problems.append('animation %d is outside the registry (0..%d)' % (anim, len(inv.names) - 1))
            continue
        name = inv.names[anim]
        r = run_one(get, anim, frames, deadline_s=deadline_s, sleep=sleep, now=now)
        bad = check_result(r, anim, frames, name)
        dormant = name in inv.dormant
        bu = r.get('band_us') if isinstance(r.get('band_us'), int) else 0
        ru = r.get('ref_us') if isinstance(r.get('ref_us'), int) else 0
        if bad:
            note = '; '.join(bad)
            coverage = 'FAIL'
        elif dormant:
            note = 'band() is bandRef(): %s' % inv.dormant[name]
            coverage = 'NONE'
        else:
            note = ''
            coverage = 'compared'
        print(ROW % (anim, name, coverage, r.get('bands', '-'), r.get('mismatch_px', '-'),
                     bu, ru, ('%.2f' % (ru / bu)) if bu else '-', note), file=out, flush=True)
        if bad:
            problems.append('%d %s: %s' % (anim, name, '; '.join(bad)))
        elif dormant:
            skipped.append(name)
        else:
            compared.append(name)

    print('', file=out, flush=True)
    if inv.unswitched:
        print('no named GM_BGANIM_<NAME>_ASM switch, so no isolated build-time A/B (%d): %s'
              % (len(inv.unswitched), ', '.join(inv.unswitched)), file=out, flush=True)
    if skipped:
        print('NOT COMPARED (%d): %s' % (len(skipped), ', '.join(skipped)), file=out, flush=True)
        for name in skipped:
            print('  %s: %s The board compared bandRef() with itself, so its 0 '
                  'differing pixels prove nothing about the kernel.'
                  % (name, inv.dormant[name]), file=out, flush=True)
    if not problems and not compared:
        # Every animation asked for was noncoverage, so the run demonstrated
        # nothing. Reporting that as success would let `anim_parity.py 2`
        # stand in for a proof about Silk's kernel, which is the fault this
        # whole gate exists to close.
        problems.append('nothing was compared: every animation asked for has no '
                        'independent path in this build')
    if problems:
        for p in problems:
            print('FAIL: %s' % p, file=out, flush=True)
        print('FAIL: %d of %d asked for compared clean, %d not compared, %d failed'
              % (len(compared), len(ids), len(skipped), len(problems)), file=out, flush=True)
    else:
        print('PASS: %d of %d animations compared band() against an independent '
              'bandRef() with 0 differing pixels, %d not compared'
              % (len(compared), len(ids), len(skipped)), file=out, flush=True)
    return problems


# ---------------------------------------------------------------- host test

class FakeDevice:
    """A scripted /api/debug/animtest, one publish per request.

    Defaults answer the way a healthy board does. Every keyword is a way to
    break one thing and nothing else, so a mutation test says which fault it
    is testing rather than hand-writing a whole response.

      count          how many animations the firmware knows. A request past it
                     gets HTTP 400, which is what an unflashed board does when
                     the registry in the checkout has grown.
      ids            the id strings the firmware reports, defaulting to names.
      drop           fields to leave out of the published result.
      no_ref         animations to report has_ref false for.
      init_failed    animations to report init_failed true for.
      mismatch       {anim: pixel count} to report as differing.
      bands          {anim: count} to override the band count.
      route          'ok', 'missing' (404 on every animtest request) or
                     'garbage' (a body that is not JSON).
      stuck          never advance seq, so the poll runs its deadline out.
      wrong_anim     answer every request with this animation id instead.
      stale_anim     a board caught mid-publish: the read before the request
                     returns the odd transient seq, and the run that was
                     already in flight lands one poll later, for this
                     animation, before ours does.
    """

    def __init__(self, names, count=None, ids=None, drop=(), no_ref=(), init_failed=(),
                 mismatch=None, bands=None, route='ok', stuck=False, wrong_anim=None,
                 stale_anim=None, rows=480, band_h=2):
        self.names = list(names)
        self.count = len(self.names) if count is None else count
        self.ids = list(ids) if ids is not None else list(self.names)
        self.drop = set(drop)
        self.no_ref = set(no_ref)
        self.init_failed = set(init_failed)
        self.mismatch = dict(mismatch or {})
        self.bands = dict(bands or {})
        self.route = route
        self.stuck = stuck
        self.wrong_anim = wrong_anim
        self.bands_per_frame = (rows + band_h - 1) // band_h
        self.seq = 0
        self.result = {'pending': False, 'seq': 0, 'anim': -1, 'id': '', 'frames': 0,
                       'has_ref': False, 'init_failed': False, 'bands': 0, 'mismatch_px': 0,
                       'first': {'frame': -1, 'pset': -1, 'x': -1, 'y': -1, 'got': 0, 'want': 0},
                       'band_us': 0, 'ref_us': 0}
        self.requests = []
        self.stale_anim = stale_anim
        self._stale_due = False
        if stale_anim is not None:
            # seq 1 is the odd value the publish leaves while it writes the
            # struct; the run doing the writing lands on 2, and ours on 4.
            self.seq = 2
            self._publish_into(self.result, stale_anim, 8, 1)

    def _publish(self, anim, frames):
        if not self.stuck:
            self.seq += 2
        r = {}
        self._publish_into(r, anim, frames, self.seq)
        self.result = r

    def _publish_into(self, into, anim, frames, seq):
        name = self.ids[anim] if anim < len(self.ids) else ''
        r = {'pending': False, 'seq': seq, 'anim': anim, 'id': name, 'frames': frames,
             'has_ref': anim not in self.no_ref,
             'init_failed': anim in self.init_failed,
             'bands': self.bands.get(anim, self.bands_per_frame * frames * PSETS),
             'mismatch_px': self.mismatch.get(anim, 0),
             'first': {'frame': 0, 'pset': 0, 'x': 3, 'y': 4, 'got': 0x1234, 'want': 0x5678},
             'band_us': 4000, 'ref_us': 5200}
        if not r['has_ref']:
            # The firmware publishes and returns before rendering anything.
            r['bands'] = 0
            r['mismatch_px'] = 0
        for f in self.drop:
            r.pop(f, None)
        into.clear()
        into.update(r)

    def get(self, path):
        if path.startswith('/api/debug/pclk'):
            return {'div': 8, 'hz': 10000000}
        if not path.startswith('/api/debug/animtest'):
            raise ResultError('GET %s: HTTP 404' % path)
        if self.route == 'missing':
            raise ResultError('GET %s: HTTP 404' % path)
        if self.route == 'garbage':
            raise ResultError('GET %s: the body is not JSON' % path)
        if '?' in path:
            args = dict(p.split('=', 1) for p in path.split('?', 1)[1].split('&'))
            anim = int(args['anim'])
            frames = int(args.get('frames', 8))
            self.requests.append(anim)
            if anim < 0 or anim >= self.count:
                raise ResultError('GET %s: HTTP 400' % path)
            if self.stale_anim is not None:
                self._stale_due = True
            self._publish(self.wrong_anim if self.wrong_anim is not None else anim, frames)
            return dict(self.result)
        if self._stale_due:
            self._stale_due = False
            stale = {}
            self._publish_into(stale, self.stale_anim, 8, self.seq - 2)
            return stale
        return dict(self.result)


def _scratch_sources(tmp, extra_registration=None, dormant_flag=None):
    """A copy of the bganim sources with one thing changed.

    Mutation 1 needs a registry that has grown since this tool was last
    touched, and the noncoverage mutation needs a second kernel to have gone
    dormant. Both are source edits, so they are made in a throwaway tree: this
    checkout is shared with other agents and a file put back "for a moment" is
    a file another agent's pathspec can capture."""
    import shutil
    dst = os.path.join(tmp, 'bganim')
    os.makedirs(dst, exist_ok=True)
    for name in os.listdir(BGANIM_DIR):
        if name.endswith('.cpp') or name.endswith('.h'):
            shutil.copy2(os.path.join(BGANIM_DIR, name), os.path.join(dst, name))
    if extra_registration is not None:
        sym, ident = extra_registration
        reg = os.path.join(dst, 'BgAnimRegistry.cpp')
        text = read_source(reg).replace('    &bg_anim_cube,\n',
                                        '    &bg_anim_cube,\n    &bg_anim_%s,\n' % sym)
        with open(reg, 'w', encoding='utf-8') as f:
            f.write(text)
        with open(os.path.join(dst, 'AnimZZTest.cpp'), 'w', encoding='utf-8') as f:
            f.write('const BgAnimation bg_anim_%s = {\n    "%s",\n    "ZZ",\n};\n' % (sym, ident))
    if dormant_flag is not None:
        flag, src = dormant_flag
        path = os.path.join(dst, src)
        text = read_source(path).replace('#define GM_BGANIM_%s_ASM 1' % flag,
                                         '#define GM_BGANIM_%s_ASM 0' % flag)
        with open(path, 'w', encoding='utf-8') as f:
            f.write(text)
    return dst


def host_test(out=None):
    """The gate, on the host, with no board (gm-nov3.35).

    What it has to prove is that the run cannot exit 0 without having compared
    something. Each case below is a way the previous version of this tool
    reported a pass over work it had not done. Returns the list of problems,
    empty when every case held."""
    import io
    import tempfile

    out = sys.stdout if out is None else out

    problems = []
    frames = 8

    def expect_fail(name, dev, inv, expect, ids=None):
        buf = io.StringIO()
        found = check_fleet(dev.get, inv, ids if ids is not None else list(range(len(inv.names))),
                            frames, out=buf, sleep=lambda _s: None)
        if not found:
            problems.append('%s: the run passed' % name)
            return buf.getvalue()
        if not any(expect in p for p in found):
            problems.append('%s: refused without naming it (%r): %s' % (name, expect, found))
        print('host test, %s: the run fails' % name, file=out)
        return buf.getvalue()

    def expect_abort(name, fn, expect):
        try:
            fn()
        except (InventoryError, ResultError) as exc:
            if expect not in str(exc):
                problems.append('%s: aborted without naming it (%r): %s' % (name, expect, exc))
            print('host test, %s: the run aborts' % name, file=out)
            return
        problems.append('%s: did not abort' % name)

    # 0. The baseline. The real sources, a healthy board, and a pass that says
    #    what it covered and what it did not.
    inv = build_inventory()
    if inv.names != NAMES:
        problems.append('the checked-in NAMES is not the registry order')
    if set(inv.dormant) != set(DORMANT):
        problems.append('the dormant set moved: %s' % sorted(inv.dormant))
    dev = FakeDevice(inv.names)
    buf = io.StringIO()
    found = check_fleet(dev.get, inv, list(range(len(inv.names))), frames, out=buf,
                        sleep=lambda _s: None)
    if found:
        problems.append('the healthy board did not pass: %s' % found)
    report = buf.getvalue()
    n_dormant = len(inv.dormant)
    want = 'PASS: %d of %d animations' % (len(inv.names) - n_dormant, len(inv.names))
    if want not in report:
        problems.append('the pass line does not count the compared animations: %r'
                        % report.splitlines()[-1:])
    print('host test, a healthy board: %s' % report.strip().splitlines()[-1], file=out)

    # 1. A registry entry appended after Cube, with NAMES left alone. The old
    #    tool checked range(len(NAMES)) and exited PASS over a fleet it no
    #    longer covered. The inventory is now re-read every run, so the two
    #    disagree before a single request goes out.
    with tempfile.TemporaryDirectory() as tmp:
        grown = _scratch_sources(tmp, extra_registration=('zztest', 'zztest'))
        expect_abort('mutation 1, an animation appended to the registry',
                     lambda: build_inventory(grown), 'the registry has moved under this tool')
        # And the count really did grow, so the abort is about the right thing.
        syms = registry_symbols(grown)
        if len(syms) != len(NAMES) + 1:
            problems.append('mutation 1 did not grow the registry: %d symbols' % len(syms))

    # 2. bandRef set to null. The old tool printed `has_ref NO`, recorded the
    #    name and exited 0.
    expect_fail('mutation 2, an animation with a null bandRef',
                FakeDevice(inv.names, no_ref=[9]), inv, 'no reference path')

    # 3. A successful, advancing result that omits the two fields the whole
    #    comparison rests on. Both used to default to zero and read as a pass.
    expect_fail('mutation 3, a result without bands or mismatch_px',
                FakeDevice(inv.names, drop=['bands', 'mismatch_px']), inv, 'omits bands')
    expect_fail('mutation 3a, a result without has_ref',
                FakeDevice(inv.names, drop=['has_ref']), inv, 'omits has_ref')
    expect_fail('mutation 3b, a result whose band count is zero',
                FakeDevice(inv.names, bands={5: 0}), inv, 'no band was compared')
    expect_fail('mutation 3c, a result with a token band count',
                FakeDevice(inv.names, bands={5: 1}), inv, 'not a multiple of')

    # 4. Silk, shipped. GM_BGANIM_SILK_ASM is 0, so the device compares
    #    bandRef() with a band() that calls it, and reports 0 differing pixels
    #    because it must. The old tool counted that as one of its 44 passes.
    silk = inv.names.index('silk')
    buf = io.StringIO()
    check_fleet(dev.get, inv, [silk], frames, out=buf, sleep=lambda _s: None)
    report = buf.getvalue()
    if 'NOT COMPARED (1): silk' not in report:
        problems.append('mutation 4: silk is not reported as noncoverage: %r' % report)
    if '0 of 1 asked for compared clean' not in report:
        problems.append('mutation 4: silk still counts as a comparison: %r' % report)
    if 'nothing was compared' not in report:
        problems.append('mutation 4: a run that compared only silk reported success')
    if 'prove nothing about the kernel' not in report:
        problems.append('mutation 4: the report does not say why silk proves nothing')
    print('host test, mutation 4, the shipped Silk: reported as noncoverage, not parity', file=out)

    # 4b. The same fault arriving in a second animation. A kernel that goes
    #     dormant without being declared is what mutation 4 looks like the
    #     next time, and that the run does refuse.
    with tempfile.TemporaryDirectory() as tmp:
        dulled = _scratch_sources(tmp, dormant_flag=('TIDE', 'AnimTide.cpp'))
        expect_abort('mutation 4b, a second kernel compiled out of the build',
                     lambda: build_inventory(dulled), 'is not declared')
        # And a declaration that no longer matches the sources is refused too,
        # so DORMANT cannot rot into a blanket excuse.
        expect_abort('a stale noncoverage declaration',
                     lambda: build_inventory(expect_dormant={'silk': 'x', 'cube': 'y'}),
                     'the sources no longer agree')

    # 5. The three cases that already aborted, which must keep aborting.
    inv_get = make_getter('fake', tries=2, sleep=lambda _s: None)
    expect_abort('a missing endpoint',
                 lambda: run_one(FakeDevice(inv.names, route='missing').get, 0, frames,
                                 sleep=lambda _s: None),
                 'HTTP 404')
    expect_abort('a body that is not JSON',
                 lambda: run_one(FakeDevice(inv.names, route='garbage').get, 0, frames,
                                 sleep=lambda _s: None),
                 'not JSON')
    expect_abort('an out-of-range animation id',
                 lambda: run_one(FakeDevice(inv.names).get, len(inv.names) + 3, frames,
                                 sleep=lambda _s: None),
                 'HTTP 400')
    # The real getter turns each of those into the same abort, so the fake's
    # exceptions are not doing the work on their own.
    for name, body, expect in (('a 404 from urllib', urllib.error.HTTPError('u', 404, 'x', None, None), 'HTTP 404'),
                               ('a non-JSON body from urllib', b'<html>not json', 'not JSON')):
        class _Resp:
            def __init__(self, data):
                self.data = data

            def read(self):
                return self.data

            def __enter__(self):
                return self

            def __exit__(self, *a):
                return False

        def opener(url, timeout=0, _b=body):
            if isinstance(_b, Exception):
                raise _b
            return _Resp(_b)

        saved = urllib.request.urlopen
        urllib.request.urlopen = opener
        try:
            expect_abort(name, lambda: inv_get('/api/debug/animtest'), expect)
        finally:
            urllib.request.urlopen = saved

    # 6. A board that answers about the wrong animation, or calls it something
    #    else, is a finding rather than a poll that waits out its deadline.
    expect_fail('a result for the wrong animation',
                FakeDevice(inv.names, wrong_anim=0), inv, 'is for animation 0',
                ids=[1])
    ids_shifted = list(inv.names)
    ids_shifted[7] = 'not-mandala'
    expect_fail('a board whose registry order differs from the checkout',
                FakeDevice(inv.names, ids=ids_shifted), inv, 'the registry calls it')

    # 6b. A board caught mid-publish. The read before the request returns the
    #     odd transient seq, so the run already in flight lands on an even seq
    #     that differs from it. Accepting "different and even" would take that
    #     stranger's result as the answer to our request.
    racing = FakeDevice(inv.names, stale_anim=0)
    r = run_one(racing.get, 5, frames, sleep=lambda _s: None)
    if r.get('anim') != 5:
        problems.append('a board caught mid-publish handed back animation %r' % (r.get('anim'),))
    else:
        print('host test, a board caught mid-publish: the in-flight result is not taken as ours',
              file=out)

    # 7. A board that never publishes runs its deadline out.
    clock = [0.0]
    expect_abort('a board that never publishes a result',
                 lambda: run_one(FakeDevice(inv.names, stuck=True).get, 0, frames,
                                 deadline_s=5.0, sleep=lambda _s: clock.__setitem__(0, clock[0] + 1),
                                 now=lambda: clock[0]),
                 'no result within')

    # 8. The ordinary faults still fail: a differing pixel and a failed init.
    expect_fail('a differing pixel', FakeDevice(inv.names, mismatch={30: 17}), inv,
                '17 differing pixels')
    expect_fail('an animation whose init failed', FakeDevice(inv.names, init_failed=[12]), inv,
                'init() failed')

    # 9. The process exit code, end to end through main(), because everything
    #    above reads the problem list and a caller reads $?.
    def exit_code(dev, argv):
        class _Resp:
            def __init__(self, data):
                self.data = data

            def read(self, *a):
                return self.data

            def __enter__(self):
                return self

            def __exit__(self, *a):
                return False

        def opener(url, timeout=0):
            path = '/' + url.split('/', 3)[3]
            return _Resp(json.dumps(dev.get(path)).encode('utf-8'))

        saved_open, saved_sleep = urllib.request.urlopen, time.sleep
        saved_stdout, saved_stderr = sys.stdout, sys.stderr
        urllib.request.urlopen = opener
        time.sleep = lambda _s: None
        sys.stdout = sys.stderr = io.StringIO()
        try:
            return main(argv)
        except ResultError:
            return 'raised'
        finally:
            urllib.request.urlopen, time.sleep = saved_open, saved_sleep
            sys.stdout, sys.stderr = saved_stdout, saved_stderr

    for label, dev, argv, want in (
            ('a healthy board', FakeDevice(inv.names), ['--host', 'fake'], 0),
            ('a null bandRef', FakeDevice(inv.names, no_ref=[9]), ['--host', 'fake'], 1),
            ('a result missing fields', FakeDevice(inv.names, drop=['bands']), ['--host', 'fake'], 1),
            ('the shipped Silk alone, which compares nothing', FakeDevice(inv.names),
             ['--host', 'fake', str(silk)], 1),
            ('a missing endpoint', FakeDevice(inv.names, route='missing'), ['--host', 'fake'], 2),
            ('a frame count the firmware would clamp', FakeDevice(inv.names),
             ['--host', 'fake', '--frames', '100'], 2)):
        got = exit_code(dev, argv)
        if got != want:
            problems.append('main() for %s exited %r, wanted %r' % (label, got, want))
        else:
            print('host test, main() for %s exits %d' % (label, want), file=out)

    for p in problems:
        print('host test FAILED: %s' % p, file=out)
    print('host test: %s' % ('FAIL' if problems else 'PASS'), file=out)
    return problems


# ------------------------------------------------------------------- driver

def main(argv=None):
    ap = argparse.ArgumentParser(description='band() against bandRef() for every animation')
    ap.add_argument('ids', nargs='*', type=int, help='animation ids (default: the whole registry)')
    ap.add_argument('--host', default=os.environ.get('HOST', '192.168.1.121'))
    ap.add_argument('--frames', type=int, default=int(os.environ.get('FRAMES', '8')))
    ap.add_argument('--host-test', action='store_true',
                    help='run the gate against a scripted device and exit; needs no board')
    args = ap.parse_args(argv)

    if args.host_test:
        return 1 if host_test() else 0

    if not (FRAMES_MIN <= args.frames <= FRAMES_MAX):
        print('frames must be %d..%d: the firmware clamps it and the echo check '
              'would then fail for no reason' % (FRAMES_MIN, FRAMES_MAX), file=sys.stderr)
        return 2

    try:
        inv = build_inventory()
    except InventoryError as exc:
        print('ABORT: %s' % exc, file=sys.stderr)
        return 2

    get = make_getter(args.host)
    try:
        print('pclk: %s' % json.dumps(get('/api/debug/pclk')), flush=True)
    except ResultError as exc:
        print('pclk: unavailable (%s)' % exc, flush=True)
    print('registry: %d animations, %d not compared (%s)'
          % (len(inv.names), len(inv.dormant), ', '.join(sorted(inv.dormant)) or 'none'), flush=True)

    ids = args.ids or list(range(len(inv.names)))
    try:
        problems = check_fleet(get, inv, ids, args.frames)
    except ResultError as exc:
        print('ABORT: %s' % exc, file=sys.stderr)
        return 2
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
