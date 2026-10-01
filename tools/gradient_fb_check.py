#!/usr/bin/env python3
"""Compares the device's framebuffer against the gradient sampler (gm-nov3.10).

    python3 tools/gradient_fb_check.py --host 192.168.1.121
    python3 tools/gradient_fb_check.py --host 192.168.1.121 --tone 60,65
    python3 tools/gradient_fb_check.py --host 192.168.1.121 --json out.json
    python3 tools/gradient_fb_check.py --host-test        (no device needed)

Exits 0 when every compared sample matches and non-zero on any mismatch, on a
fixture that will not arm, or on a device whose reported tone is not the tone
that was asked for.

WHAT THIS CHECKS THAT NOTHING ELSE DOES
=======================================

tools/animbench/web/ramp_parity.js proves web/src/config/gradientRamp.js equal
to the firmware's C++ palette arithmetic, entry by entry, on the host. It
cannot see the device. A stored brightness, a stored highlight rolloff, a
gradient reference resolving to something other than the entry under review,
or a compositor dropping bits on the way to the framebuffer would all leave
that proof passing and the framebuffer wrong.

This reads the framebuffer.

WHAT THIS CANNOT ESTABLISH
==========================

It reads framebuffer memory in PSRAM through /api/debug/fb. That is where the
render task leaves its pixels, so every result here is evidence about the
compositor and about nothing downstream of it (gm-nov3.22). A match says the
right colours were written. It does not say they were shown.

Specifically, it is silent about:

  - RGB scan-out: the bounce buffers, the DMA refill and the LCD_CAM
    peripheral that carry those bytes to the panel.
  - panel timing. The pixel-clock divider is not in this path at all, so the
    divider a board happens to store neither qualifies a pass nor explains a
    failure. The bench board reads 7; the result would be the same at 6.
  - the ribbon and the wiring.
  - the controller board, which is a separate device over BLE.
  - the glass. A panel that is dark, torn, mirrored or miswired can still
    give every sample below a match.

Photographs and the scan-out counters (/api/debug/scanout) are the instruments
for those. This one answers a narrower question, and answering it exactly is
the point: when a picture looks wrong, this says whether the compositor put it
there, which is the fork the two families of fix hang off.

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

It also reports which palette is published and which one the strip was drawn
from, which is what says the fixture has caught up with the tone that was asked
for. arm() is the accept condition and says what each step of that is.

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

Both sides are integer arithmetic over the same stops and the framebuffer
holds RGB565, so the expected result is exact equality, not a tolerance. A
non-zero mismatch count means the firmware and gradientRamp.js have diverged,
or something is writing the strip after the fixture does.
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
    """The 256 RGB565 entries gradientRamp.js says the framebuffer will hold."""
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


def arm(host, mode, y0, y1, xoff, btone, ktone, settle_frames, stops=None, timeout_s=15.0,
        getter=None, sleep=time.sleep, now=time.time):
    """Arms the fixture and waits until the strip carries what was asked for.

    stops, when given, is a gradient wire string held on the device through the
    editor's live-preview path for as long as this pass needs it. Waiting for
    the device to report those same stops is what makes a batch safe: the
    preview lapses on a timer, and a pass that sampled after it lapsed would
    otherwise be compared against the gradient that was asked for rather than
    the one that was drawn.

    A requested tone reaches the panel in four steps, and three of them are
    states a sample must not be taken in. gate_problems() below is the whole
    condition; this loop polls until it has nothing left to report, or until
    timeout_s has passed and it says what was still standing.

      1. The HTTP task holds the requested percentages. Nothing else has
         moved. Measured on the bench board (2026-09-13, three tone changes):
         the device reported the new percentages beside the previous integers
         for 4 to 6 consecutive reads, 230 to 394 ms, because the UI task only
         picks the request up on its next pass. That measurement is what the
         percentage-and-conversion half of the gate was written for
         (gm-nov3.22), and it still is.
      2. The UI task converts them and bganim publishes the toned palette.
         Before gm-nov3.27 the firmware could report the converted integers
         with the previous palette still published, so this step was visible
         from the host as a state that satisfied the whole gate. It is not a
         state the firmware can reach now, and the host test keeps scripting it
         so a regression there is refused rather than sampled.
      3. The palette is published. The strip in the framebuffer is not: the
         render task rebuilds the fixture LUT on its next frame.
      4. A whole frame has been drawn from that palette. Only now is a sample
         evidence about anything.

    The count in step 4 is frames the device says it drew, not a delay worked
    out from a frame rate. A device too slow to draw them runs the deadline out
    and the failure names the count it reached, which is the refusal that
    should happen: it must not be waved through by an assumed 22 fps.

    getter, sleep and now are injected so host_test() can drive this loop
    against a scripted device with no board and no waiting.
    """
    get = getter or (lambda path: get_json(host, path))
    q = '/api/debug/gradfix?on=%d&y0=%d&y1=%d&xoff=%d' % (MODES[mode], y0, y1, xoff)
    if btone is not None:
        q += '&btone=%d&ktone=%d' % (btone, ktone)
    if stops is not None:
        q += '&stops=' + stops
    st = get(q)
    if not st['armed']:
        raise RuntimeError('the fixture refused to arm: %s' % json.dumps(st))
    deadline = now() + timeout_s
    problems = ['no reading yet']
    while now() < deadline:
        st = get('/api/debug/gradfix')
        problems = gate_problems(st, btone, ktone, settle_frames, stops)
        if not problems:
            return st
        sleep(0.25)
    raise RuntimeError('the fixture did not settle in %.0f s (%s): %s'
                       % (timeout_s, '; '.join(problems), json.dumps(st)))


def check_tone_conversion(st):
    """The percent-to-integer conversion DefaultUI does, checked on the device.

    The device reports both the percentages and the integers its render task
    ended up with. If they disagree the host is about to sample a tone the
    render task is not drawing, so this is a stop rather than a note."""
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


def tone_problems(st, btone, ktone):
    """Everything still standing between the requested tone and a sample.

    Two conditions, and the second is the one the fixture used to skip: the
    device must report the percentages that were asked for, and the integers
    it reports must be the conversion of the percentages it reports. When
    nothing was asked for (btone None) only the conversion is checked, because
    the stored setting is then whatever it is."""
    problems = []
    if btone is not None and st['brightnessPct'] != btone:
        problems.append('asked for brightness %d%%, device reports %d%%' % (btone, st['brightnessPct']))
    if ktone is not None and st['kneePct'] != ktone:
        problems.append('asked for rolloff %d%%, device reports %d%%' % (ktone, st['kneePct']))
    return problems + check_tone_conversion(st)


# What gm-nov3.27 added to the report. Without them a host cannot tell a strip
# drawn with the published palette from one still drawn with the previous, and
# that is the whole question, so their absence is named rather than tolerated.
FIXTURE_FIELDS = ('applied', 'fixApplied', 'tonedFrames', 'consistent')


def fixture_problems(st):
    """Everything between the published palette and a strip that carries it.

    'applied' is one word naming the palette the device is publishing and the
    tone that went into it; 'fixApplied' is the same word for the palette the
    fixture's lookup table was built from, which is what the pixels in the
    framebuffer came from. Equal means the strip is the published palette.
    Unequal means the render task has not caught up yet, which used to be
    invisible from here."""
    missing = [k for k in FIXTURE_FIELDS if k not in st]
    if missing:
        return ['the device reports no %s, so its firmware predates gm-nov3.27' % ', '.join(missing)]
    problems = []
    if not st['consistent']:
        problems.append('the palette was republished while the report was being built')
    if st['fixApplied'] != st['applied']:
        problems.append('the strip was drawn from palette %d, the published one is %d'
                        % (st['fixApplied'], st['applied']))
    return problems


def gate_problems(st, btone, ktone, settle_frames, stops=None):
    """The whole accept condition, as a list of reasons not to sample yet.

    Empty means a sample taken now is evidence about the tone and the gradient
    that were asked for. Anything else names what is still standing, so a
    timeout says which step the device stopped at rather than only that it
    did."""
    problems = tone_problems(st, btone, ktone) + fixture_problems(st)
    if stops is not None and st.get('stops') != stops:
        problems.append('asked for stops %s, device reports %s' % (stops, st.get('stops')))
    toned = st.get('tonedFrames', 0)
    if toned < settle_frames:
        problems.append('%d of %d whole frames drawn with the published palette' % (toned, settle_frames))
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
            or after['kneePct'] != st['kneePct'] or not after['armed'] or after['xoff'] != st['xoff']
            # The palette itself, and the one the strip came from. A republish
            # during the framebuffer read leaves the pixels part one palette
            # and part the other, and the percentages above would not show it:
            # a preview that lapses and comes back reports the same tone.
            or after.get('applied') != st.get('applied') or after.get('fixApplied') != st.get('fixApplied')):
        raise StaleFixture('%s -> %s' % (json.dumps(st), json.dumps(after)))
    return st, results


# The four steps a requested tone takes to reach the strip, as separate states
# a FakeDevice can be scripted to stop at. See arm() for what each one is.
STAGE_INTEGERS = 'integers'  # the reported integers are the request's conversion
STAGE_PALETTE = 'palette'    # the published palette carries the new tone
STAGE_FRAME = 'frame'        # a whole frame has been drawn from that palette

DEFAULT_STAGES = (STAGE_INTEGERS, STAGE_PALETTE, STAGE_FRAME)


def pack_applied(gen, brightness256, knee):
    """The word the firmware publishes, as BgAnimCommon.cpp packs it."""
    return (brightness256 & 0x1ff) | ((knee & 0xff) << 9) | ((gen & 0x7fff) << 17)


def convert(pct):
    """The percent-to-integer conversion DefaultUI does."""
    return (pct[0] * 256) // 100, (pct[1] * 255) // 100


class FakeDevice:
    """A scripted /api/debug/gradfix, one poll per step of the real publish.

    The device starts settled on its stored tone. The arm request hands it the
    requested percentages, which is step 1, and each poll afterwards applies
    the next entry of `stages`, so a test stops the device wherever it wants to
    and the gate has to refuse everything short of the end.

    STAGE_INTEGERS is deliberately a state the firmware can no longer reach.
    Before gm-nov3.27 setThemeTone() assigned the two integers and only then
    rebuilt the palette, and the endpoint read those integers directly, so the
    device really did report the converted tone beside the previous palette,
    and the gate of the day accepted it. Scripting it keeps the refusal
    testable: run it alone (stages=[STAGE_INTEGERS]) and it is the mutation
    that the old gate passed and this one must fail.

    ignore_request is a device that never picks the request up at all, which is
    what a lost or half-published override looks like from the host.
    arm_applies runs the whole script during the arm, for a device that was
    already showing what was asked for."""

    def __init__(self, stages=DEFAULT_STAGES, stored=(70, 80), frames_per_poll=4,
                 ignore_request=False, consistent=True, arm_applies=False):
        self.stages = list(stages)
        self.step = 0
        self.ignore_request = ignore_request
        self.consistent = consistent
        self.arm_applies = arm_applies
        self.req = None            # the percentages the HTTP task holds
        self.stored = stored
        self.pal = stored          # what the published palette was built from
        self.integers = stored     # what the reported integers convert from
        self.gen = 1
        self.fix = (1, stored)     # the palette the strip in the buffer came from
        self.toned = 0
        self.frames = 0
        self.frames_per_poll = frames_per_poll
        self.polls = 0
        self.arm_state = None
        self.states = []

    def _applied(self):
        return pack_applied(self.gen, *convert(self.integers))

    def _fix_applied(self):
        return pack_applied(self.fix[0], *convert(self.fix[1]))

    def _apply(self, stage):
        if stage == STAGE_INTEGERS:
            self.integers = self.req
        elif stage == STAGE_PALETTE:
            self.pal = self.req
            self.integers = self.req
            self.gen += 1
        elif stage == STAGE_FRAME:
            self.fix = (self.gen, self.pal)
            self.toned = self.frames_per_poll
        else:
            raise AssertionError('unknown stage %r' % stage)

    def _state(self):
        pct = self.req if self.req is not None else self.stored
        b256, knee = convert(self.integers)
        return {'armed': True, 'frames': self.frames, 'stops': 'aabbcc,ddeeff', 'xoff': 0,
                'w': 480, 'h': 480, 'y0': 200, 'y1': 240, 'gain256': 256,
                'brightnessPct': pct[0], 'kneePct': pct[1],
                'brightness256': b256, 'knee': knee,
                'applied': self._applied(), 'fixApplied': self._fix_applied(),
                'tonedFrames': self.toned, 'consistent': self.consistent}

    def get(self, path):
        if 'on=' in path:
            self.frames = 0
            self.toned = 0     # the arm moves the region, so earlier frames say nothing
            if 'btone=' in path and not self.ignore_request:
                q = path.split('?', 1)[1]
                args = dict(p.split('=', 1) for p in q.split('&'))
                self.req = (int(args['btone']), int(args['ktone']))
                self.step = 0
                if self.arm_applies:
                    for stage in self.stages:
                        self._apply(stage)
                    self.step = len(self.stages)
            self.arm_state = self._state()
            return self.arm_state
        self.polls += 1
        self.frames += self.frames_per_poll
        if self.req is not None and self.step < len(self.stages):
            self._apply(self.stages[self.step])
            self.step += 1
        elif self._fix_applied() == self._applied():
            self.toned += self.frames_per_poll
        st = self._state()
        self.states.append(st)
        return st


def host_test():
    """The settle gate, on the host, with no board (gm-nov3.22, gm-nov3.27).

    What it has to prove is that no sample is accepted until the strip in the
    framebuffer was drawn from the palette that carries the tone that was asked
    for. Returns a list of problems, empty when every case held."""
    problems = []

    def refuse(name, dev, expect, settle=4, timeout_s=2.0):
        """Runs the gate against a device that must not be accepted."""
        clock = [0.0]

        def tick(_s):
            clock[0] += 0.25

        try:
            arm('fake', 'ramp', 200, 240, 0, 60, 65, settle_frames=settle, timeout_s=timeout_s,
                getter=dev.get, sleep=tick, now=lambda: clock[0])
            problems.append('%s was accepted' % name)
            return
        except RuntimeError as exc:
            if expect not in str(exc):
                problems.append('%s was refused without naming it (%r): %s' % (name, expect, exc))
            print('host test, %s: refused' % name)

    # 1. The four states, one per poll, and only the last one accepted. The
    #    arm response is state 1 (the request is held, nothing else has moved),
    #    then one poll each for the integers, the palette and the frame.
    dev = FakeDevice()
    st = arm('fake', 'ramp', 200, 240, 0, 60, 65, settle_frames=4,
             getter=dev.get, sleep=lambda _s: None)
    walked = [dev.arm_state] + dev.states
    if len(walked) != 4:
        problems.append('the four states were not walked one per poll: %d states' % len(walked))
    for i, state in enumerate(walked[:-1]):
        if not gate_problems(state, 60, 65, 4):
            problems.append('state %d of the publish was accepted: %s' % (i + 1, json.dumps(state)))
    if gate_problems(walked[-1], 60, 65, 4):
        problems.append('the settled state was refused: %s' % '; '.join(gate_problems(walked[-1], 60, 65, 4)))
    if st['brightnessPct'] != 60 or st['kneePct'] != 65:
        problems.append('the gate returned a state at the wrong percentages: %s' % json.dumps(st))
    if st['brightness256'] != (60 * 256) // 100 or st['knee'] != (65 * 255) // 100:
        problems.append('the gate returned before the tone was published: %s' % json.dumps(st))
    if st['fixApplied'] != st['applied']:
        problems.append('the gate returned before the strip carried the palette: %s' % json.dumps(st))
    print('host test, the four states are walked one per poll: settled after %d polls' % dev.polls)

    # 2. The mutation, and the teeth of the whole bead. Both reported integers
    #    equal the request while the palette is still the previous one, which
    #    is exactly what the firmware did before gm-nov3.27. The old gate
    #    (percentages reported back, integers their conversion, frames painted)
    #    is satisfied on the first poll, so this case is what separates the two.
    mutant = FakeDevice(stages=[STAGE_INTEGERS])
    refuse('a tone reported before the palette changed', mutant,
           'the strip was drawn from palette')
    early = mutant.states[0]
    if tone_problems(early, 60, 65) or early['frames'] < 4:
        problems.append('the mutation did not reproduce the old gate, so it proves nothing')
    if early['fixApplied'] == early['applied']:
        problems.append('the mutation published the palette after all, so it proves nothing')

    # 3. A palette published but not yet drawn. The tone is right everywhere a
    #    host could read it and the strip is still the previous one.
    refuse('a palette the fixture has not drawn yet', FakeDevice(stages=[STAGE_INTEGERS, STAGE_PALETTE]),
           'the strip was drawn from palette')

    # 4. A device that never applies the request is a failure, not a wait
    #    forever and not a silent sample at the wrong tone.
    refuse('a tone that never lands', FakeDevice(stages=[]), 'converts to')

    # 5. The percentages themselves are still checked: a device holding a tone
    #    other than the one asked for never gets sampled, however settled the
    #    rest of it is. This is the override that went missing.
    refuse('a device holding the wrong percentages',
           FakeDevice(stages=[], stored=(100, 100), ignore_request=True), 'asked for brightness')

    # 6. A report taken across a republish pairs one palette's stops with
    #    another's tone, so it is not sampled however settled it looks.
    refuse('a report taken across a republish',
           FakeDevice(stages=[], stored=(60, 65), consistent=False), 'republished')

    # 7. Firmware without the fields cannot be gated at all, and says so
    #    rather than degrading to the old condition.
    old_fw = FakeDevice(stages=[], stored=(60, 65), arm_applies=True)

    def strip(path):
        st = old_fw.get(path)
        return {k: v for k, v in st.items() if k not in FIXTURE_FIELDS}

    class Stripped:
        get = staticmethod(strip)

    refuse('firmware without the fixture fields', Stripped(), 'predates gm-nov3.27')

    # 8. Slow frames are governed by the frames the device says it drew. At one
    #    frame per poll a six frame gate costs six polls, and a device drawing
    #    none runs the deadline out rather than being waved through.
    slow = FakeDevice(stages=[], stored=(60, 65), frames_per_poll=1)
    arm('fake', 'ramp', 200, 240, 0, 60, 65, settle_frames=6, getter=slow.get, sleep=lambda _s: None)
    if slow.polls != 6:
        problems.append('a device at one frame per poll took %d polls to pass a six frame gate' % slow.polls)
    refuse('a device drawing no frames', FakeDevice(stages=[], stored=(60, 65), frames_per_poll=0),
           'of 4 whole frames drawn')

    # 9. A device that is already showing what was asked for costs one poll, so
    #    the wait is a gate and not a fixed delay.
    settled = FakeDevice(stored=(60, 65), arm_applies=True)
    arm('fake', 'ramp', 200, 240, 0, 60, 65, settle_frames=4, getter=settled.get, sleep=lambda _s: None)
    if settled.polls != 1:
        problems.append('a device that was already settled took %d polls' % settled.polls)

    for pr in problems:
        print('host test FAILED: %s' % pr)
    print('host test: %s' % ('FAIL' if problems else 'PASS'))
    return problems


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
    """Every built-in in data/gradients.json, drawn on the device, one at a time.

    This is what gm-nov3.5's appended batch needs: the entries are put on the
    device through the preview path, so nothing stored is written and nothing
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
    ap = argparse.ArgumentParser(description="Compare the device's framebuffer against the gradient sampler")
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
                    help='whole fixture frames drawn from the published palette to wait for before '
                         'reading the framebuffer (raised to one per --buffers entry if lower)')
    ap.add_argument('--buffers', default='0,1', help='framebuffer indices to read (default both)')
    ap.add_argument('--node', default='node')
    ap.add_argument('--json', default=None, help='write the full record here')
    ap.add_argument('--batch', action='store_true',
                    help='walk every gradient in data/gradients.json instead of the device\'s own theme')
    ap.add_argument('--source', default=os.path.join(ROOT, 'data', 'gradients.json'))
    ap.add_argument('--limit', type=int, default=None, help='with --batch, stop after this many gradients')
    ap.add_argument('--selftest', action='store_true',
                    help='also run the two negative controls that prove the comparison can fail')
    ap.add_argument('--host-test', action='store_true',
                    help='run the settle-gate tests against a scripted device and exit; needs no board')
    args = ap.parse_args(argv)
    if args.host_test:
        return 1 if host_test() else 0
    args.buffers = [int(v) for v in args.buffers.split(',')]
    # One rendered frame writes one framebuffer and the pair alternates, so
    # reading both needs at least one whole frame each. The default of 6 covers
    # it; this only stops a hand-picked --settle from accepting a buffer the
    # new palette never reached.
    if args.settle < len(args.buffers):
        print('raising --settle to %d, one whole frame per framebuffer read' % len(args.buffers))
        args.settle = len(args.buffers)

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
    print('framebuffer %dx%d, fixture rows %d..%d, fb step %d'
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
        # failed run does not leave the device drawing a strip.
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
