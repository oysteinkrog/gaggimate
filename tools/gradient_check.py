#!/usr/bin/env python3
"""Set-quality check for the built-in gradients in data/gradients.json.

Run from tools/animbench `make check`. Exits non-zero on a failing input.

    python3 tools/gradient_check.py            check data/gradients.json
    python3 tools/gradient_check.py --json out.json
    python3 tools/gradient_check.py --source other.json

Why a checker at all: the set is going from 18 gradients to about 60, one
gradient is indexed by a stored id, and the failure mode is not a crash but a
picker full of entries a user cannot tell apart. Two of the rules below are
about what a gradient is (a dark-to-bright ramp), and one is about whether the
new one already exists.


THE SAMPLING CONTRACT
=====================

Every rendered number in this file is measured under one fixed set of
conditions. They are fixed on purpose: a gate whose answer depends on a
device's stored settings, or on whichever contact sheet was generated last, is
not a gate. Change any of these and the recorded margins below stop meaning
anything.

  Source of truth   data/gradients.json, the stops exactly as written.

  Samples           The 256-entry RGB565 palette the firmware builds
                    (buildThemeRamp), sampled at 32 positions
                    t_k = k * 255 // 31 for k in 0..31, which includes both
                    ends. Not the six stops: what a user sees is the ramp,
                    and two ramps can share stops and differ between them.

  Who computes them web/src/config/gradientRamp.js, reached through
                    tools/gradient_samples.js. That module is a transcription
                    of the firmware's arithmetic, and
                    tools/animbench/web/ramp_parity.js proves it equal to the
                    real C++ entry by entry (tools/animbench/ramp_dump.cpp
                    links BgAnimCommon.cpp itself). This file contains no tone
                    curve of its own, and must not grow one: the editor's
                    helpers in bgAnimations.js already drifted from the
                    firmware, and a third copy would drift again.

  Tone              Animation brightness 100%, highlight rolloff 100%. These
                    are the shipped defaults, and at them the tone transform
                    is the identity, so the gate measures the gradient rather
                    than a taste setting. It is not degenerate: nothing is
                    crushed, no channel collapses. At brightness 0 every
                    gradient is black and every pair is a duplicate, which is
                    exactly why the setting is pinned here.

  Extra gain        256, meaning none. An animation may apply its own palette
                    gain, reverse the ramp, or wrap it into a wheel (Plasma).
                    The review contract is the plain forward ramp: it
                    represents the gradient, not every later use of it.

  RGB565 to colour  Bit replication, r8 = (r5 << 3) | (r5 >> 2) and so on,
                    which is what the panel shows. Quantization is part of the
                    contract, not noise to be averaged away: two gradients
                    that differ only below the quantizer are the same picture.

  Colour metric     CIEDE2000 in CIELAB, sRGB primaries, D65 white.

  Aggregation       The mean over the 32 sample positions. The minimum is
                    reported as well, because two ramps often cross, but the
                    gate is on the mean: one shared colour does not make two
                    gradients the same.

Source design rules are checked on the source stops and never on the rendered
samples, because a rendered sample cannot tell a bad ramp from a dim setting.
Quantization can make adjacent ramp entries equal, and that is not a fault.


THRESHOLDS, AND WHERE THEY CAME FROM
====================================

Measured over the original 18 on 2026-09-12 under the contract above.

  First stop ceiling 0.0040 relative luminance. The brightest first stop in
                     the set is Teal Reef at 0.002838, so the ceiling is about
                     40% above it. In neutral terms it is around #0e0e0e: a
                     ramp has to start near black, because the first stop is
                     what most of a dark animation's area is painted with.

  Endpoint floor     L* 58.0 at the last stop. This is not the set's current
                     minimum (Ember Coal, L* 74.89, which has 16.9 of margin).
                     It is chosen from what a highlight can be: a fully
                     saturated hue is dark in L* and still reads as a
                     highlight, and the darkest such endpoints worth allowing
                     are magenta #ff00ff at 60.3 and orange #ff6a1e at 63.1,
                     so the floor sits below them. It stays above a mid tone:
                     neutral grey #808080 is 53.6, and a ramp that stops at a
                     mid grey has no highlight at all.

  Near duplicate     Mean CIEDE2000 of 8.0. Under this contract the original
                     18 make 153 pairs with a median of 29.01 and a maximum of
                     67.48. Four pairs fall below 8.0 and are named exemptions
                     below; the closest pair that is not exempt is Sunset and
                     Fire at 8.96, so the gate has 0.96 of margin against the
                     set it grandfathers. For scale, a copy of Espresso with
                     one stop moved from #b8703a to #b8703e measures 0.125.

Exemptions are named pairs, and both names must be in ORIGINAL_18 below. That
is the whole mechanism that stops the list from growing to admit a new
gradient: a pair involving anything added later cannot be exempted, so a new
entry has to clear 8.0 against everything, exempt pairs included.


WHAT THE PAIR GATE CANNOT SEE
=============================

The 8.0 gate is per pair. It asks whether two named gradients are the same
picture and nothing else, so it cannot see how dense the set is as a whole: the
shipped 60 have 36 pairs under 10.0 and 133 under 12.0, and the gate passes
every one of them. A floor that each pair clears still allows a set that is
crowded everywhere. So this file also prints a crowding report. The report is
advisory. It fails nothing, and it is not a reason to move the 8.0 or to add an
exemption.

Raising the floor is the wrong lever, and gm-2tqj measured why. Two readers
looked at the contact sheet of the 60 independently and called sixteen pairs
hard to tell apart. Scored with this file's own metric and ranked against all
1770 pairs, nine of the sixteen land in the closest 100, so the metric does
track a reader over the crowded end. The other seven sit between rank 181 and
480, and the furthest of them, Sandstone and Graphite, scores 19.52, more than
twice the gate. No floor the shipped set could clear would catch those.

What does separate them is the category. All sixteen pairs those readers flagged
are pairs inside one category, and within-category pairs are 196 of the 1770. So
the report's review list is the within-category pairs, closest first: 11% of the
pair table, holding every pair a reader flagged. Fifteen of the sixteen are in
the closest 89 of those 196 and the last is at 125. This is a lens, not a score.
A user opens one category and compares what is in front of them, and two ramps
that travel the same hues at different rates read alike as whole pictures while
scoring far apart.

Two more reads were taken at the picker's own swatch size for gm-nov3.24, one of
them by a different model, and they hold the lens up. Across the three reads, 27
distinct pairs have been called hard to tell apart, every one of them inside a
category, and 26 of the 27 are in the review list. The exception is Gold and
Basalt at 20.99, named once, which is where the list ends rather than a number
to move. The bead records all 27 with their ranks. Whether an entry looks like
its name is a separate question and belongs to gm-2tqj: this file measures
distance and has no opinion about a name.

  Review bands      10.0, 16.0 and 20.0 mean dE2000, taken from those tables.
                    Of the 27 pairs, 8 are under 10.0, 24 under 16.0 and 26
                    under 20.0, and only the one pair above it was ever named.
                    The counts are printed per category because that is where a
                    reader's eye works.

  Review size       The settings picker paints a 96 by 30 pixel swatch from 96
                    ramp samples (kSettingsRowSwatchSamples and kSwatchH in
                    src/display/ui/default/settings/SettingsRows.cpp). A visual
                    review of the set belongs at that size, not at whatever
                    size a contact sheet happens to use.

Two limits of this file, stated so nobody reads more into a pass than is there.
It measures distance, never whether a gradient looks like its name or belongs
in its category. And its ranking predicts a reader only over the crowded end:
above roughly the closest hundred pairs of the full table, the order stops
meaning anything about what a person can tell apart.


THE FROZEN ORIGINALS
====================

The first 18 entries are frozen by full record, not by name. ORIGINAL_18_RECORDS
below holds the name, the category and all six stops of each as they stood when
the thresholds above were measured. A change to any of the three fails, and so
does a reordering, because a record is compared at the index it is frozen at.
Every margin quoted above is measured against that set and a device's stored
gradient id points into it, so it is not a set this file may quietly
re-measure.
"""

import argparse
import itertools
import json
import math
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, 'data', 'gradients.json')
SAMPLER = os.path.join(ROOT, 'tools', 'gradient_samples.js')

STOPS_PER_GRADIENT = 6

# --- the contract, as constants the other tools import ----------------------

TONE_BRIGHTNESS_PCT = 100
TONE_KNEE_PCT = 100
TONE_GAIN_256 = 256
TONE_MODE = 'ramp'
SAMPLE_COUNT = 32
SAMPLE_POSITIONS = [(k * 255) // (SAMPLE_COUNT - 1) for k in range(SAMPLE_COUNT)]

FIRST_STOP_LUMINANCE_CEILING = 0.0040
ENDPOINT_LSTAR_FLOOR = 58.0
NEAR_DUPLICATE_DE = 8.0

# The review bands the crowding report counts against. Advisory: nothing here
# rejects a gradient. See WHAT THE PAIR GATE CANNOT SEE above for where the
# three numbers come from.
REVIEW_BANDS = (10.0, 16.0, 20.0)

# The set as it stood when these thresholds were measured, complete: name,
# category and every stop, at the index each is frozen at. This is the trusted
# baseline that check_original_set compares against, and it is a copy of
# data/gradients.json as of commit 8f61e73a, before the set grew past 18. An
# exemption may only name two of these.
ORIGINAL_18_RECORDS = (
    ('Espresso', 'Coffee',
     ('#080402', '#2a1206', '#6b3413', '#b8703a', '#e8b268', '#f8e6c8')),
    ('Ocean', 'Water and Ice',
     ('#02060c', '#06284a', '#0a5276', '#2596be', '#66d3e8', '#d8f6ff')),
    ('Violet Dusk', 'Night Sky',
     ('#0a0512', '#2a1050', '#5c2a94', '#9a5ad4', '#d09af0', '#f4e2ff')),
    ('Forest', 'Nature',
     ('#020803', '#0c2c12', '#1e5c28', '#46963c', '#8cd464', '#e6ffc8')),
    ('Sunset', 'Fire and Heat',
     ('#0c0410', '#4a1030', '#952038', '#d4542c', '#f89c3c', '#ffe8a0')),
    ('Fire', 'Fire and Heat',
     ('#0a0200', '#401004', '#8c2808', '#d85c10', '#f8a428', '#ffe8b0')),
    ('Ice', 'Water and Ice',
     ('#020408', '#10203c', '#2c4a74', '#5486b4', '#9cc8e4', '#eafaff')),
    ('Mono', 'Metal and Stone',
     ('#000000', '#202020', '#484848', '#808080', '#c0c0c0', '#ffffff')),
    ('Rose', 'Pastel',
     ('#0e0407', '#3c1020', '#7a2440', '#c04868', '#ee8ca4', '#ffdce6')),
    ('Gold', 'Metal and Stone',
     ('#060402', '#2e2008', '#6e5014', '#b48c24', '#e8c453', '#fff0b8')),
    ('Aurora', 'Night Sky',
     ('#010806', '#063020', '#0c6444', '#14a878', '#48e0b0', '#c8ffec')),
    ('Cyber', 'Neon',
     ('#050008', '#240448', '#501090', '#9018d8', '#e030f8', '#ff9cf0')),
    ('Ember Coal', 'Fire and Heat',
     ('#0a0604', '#2b0a06', '#6b1a08', '#b8420f', '#e2751f', '#f4a94a')),
    ('Deep Space', 'Night Sky',
     ('#05050f', '#150a28', '#341840', '#6b2f5e', '#b3477d', '#e6b3d6')),
    ('Teal Reef', 'Water and Ice',
     ('#050a0f', '#0a1c28', '#123a44', '#1f6b6e', '#3fb3a8', '#bdeee0')),
    ('Sakura', 'Pastel',
     ('#0c060a', '#341828', '#6e3050', '#b45c80', '#e896b0', '#ffe0ec')),
    ('Lime', 'Nature',
     ('#040802', '#16300a', '#326016', '#5ea024', '#9ee44c', '#eaffc0')),
    ('Arctic Night', 'Water and Ice',
     ('#020206', '#0a1424', '#1a3048', '#34587c', '#6c94bc', '#c4e4f8')),
)

ORIGINAL_18 = tuple(name for name, _category, _stops in ORIGINAL_18_RECORDS)

# Pairs in the original 18 that are closer than the threshold. The measured
# mean dE2000 on 2026-09-12 is recorded with each, so a later change to the
# sampling contract shows up as a mismatch between this comment and the
# printed table rather than as silence.
NEAR_DUPLICATE_EXEMPTIONS = (
    ('Forest', 'Lime'),        # 4.71
    ('Rose', 'Sakura'),        # 5.45
    ('Ocean', 'Ice'),          # 6.99
    ('Espresso', 'Fire'),      # 7.88
)


class Finding:
    """One rule violation. `code` is what the unit tests assert on."""

    def __init__(self, code, name, detail):
        self.code = code
        self.name = name
        self.detail = detail

    def __str__(self):
        where = self.name if self.name else '(document)'
        return '%-18s %-28s %s' % (self.code, where, self.detail)


# --- colour ------------------------------------------------------------------
# A colour difference metric, not a tone curve. The tone curve lives in the
# firmware and is reached through the JS sampler; see the contract above.

def _srgb_to_linear(c):
    c = c / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def relative_luminance(rgb):
    r, g, b = (_srgb_to_linear(v) for v in rgb)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def to_lab(rgb):
    r, g, b = (_srgb_to_linear(v) for v in rgb)
    x = (0.4124564 * r + 0.3575761 * g + 0.1804375 * b) / 0.95047
    y = 0.2126729 * r + 0.7151522 * g + 0.0721750 * b
    z = (0.0193339 * r + 0.1191920 * g + 0.9503041 * b) / 1.08883

    def f(t):
        return t ** (1.0 / 3.0) if t > 216.0 / 24389.0 else (841.0 / 108.0) * t + 4.0 / 29.0

    fx, fy, fz = f(x), f(y), f(z)
    return (116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz))


def ciede2000(lab1, lab2):
    l1, a1, b1 = lab1
    l2, a2, b2 = lab2
    c1 = math.hypot(a1, b1)
    c2 = math.hypot(a2, b2)
    cbar = (c1 + c2) / 2.0
    g = 0.5 * (1 - math.sqrt(cbar ** 7 / (cbar ** 7 + 25.0 ** 7))) if cbar > 0 else 0.0
    a1p = (1 + g) * a1
    a2p = (1 + g) * a2
    c1p = math.hypot(a1p, b1)
    c2p = math.hypot(a2p, b2)
    h1p = math.degrees(math.atan2(b1, a1p)) % 360 if (a1p or b1) else 0.0
    h2p = math.degrees(math.atan2(b2, a2p)) % 360 if (a2p or b2) else 0.0
    dlp = l2 - l1
    dcp = c2p - c1p
    if c1p * c2p == 0:
        dhp = 0.0
    elif abs(h2p - h1p) <= 180:
        dhp = h2p - h1p
    elif h2p - h1p > 180:
        dhp = h2p - h1p - 360
    else:
        dhp = h2p - h1p + 360
    dbighp = 2 * math.sqrt(c1p * c2p) * math.sin(math.radians(dhp) / 2)
    lbar = (l1 + l2) / 2.0
    cbarp = (c1p + c2p) / 2.0
    if c1p * c2p == 0:
        hbarp = h1p + h2p
    elif abs(h1p - h2p) <= 180:
        hbarp = (h1p + h2p) / 2.0
    elif h1p + h2p < 360:
        hbarp = (h1p + h2p + 360) / 2.0
    else:
        hbarp = (h1p + h2p - 360) / 2.0
    t = (1 - 0.17 * math.cos(math.radians(hbarp - 30))
         + 0.24 * math.cos(math.radians(2 * hbarp))
         + 0.32 * math.cos(math.radians(3 * hbarp + 6))
         - 0.20 * math.cos(math.radians(4 * hbarp - 63)))
    dtheta = 30 * math.exp(-(((hbarp - 275) / 25.0) ** 2))
    rc = 2 * math.sqrt(cbarp ** 7 / (cbarp ** 7 + 25.0 ** 7)) if cbarp > 0 else 0.0
    sl = 1 + (0.015 * (lbar - 50) ** 2) / math.sqrt(20 + (lbar - 50) ** 2)
    sc = 1 + 0.045 * cbarp
    sh = 1 + 0.015 * cbarp * t
    rt = -math.sin(math.radians(2 * dtheta)) * rc
    return math.sqrt((dlp / sl) ** 2 + (dcp / sc) ** 2 + (dbighp / sh) ** 2
                     + rt * (dcp / sc) * (dbighp / sh))


def expand_rgb565(v):
    """RGB565 back to 8 bits a channel by replication, as the panel shows it."""
    r = (v >> 11) & 0x1F
    g = (v >> 5) & 0x3F
    b = v & 0x1F
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def hex_to_rgb(text):
    s = text.lstrip('#')
    return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16))


def valid_hex(text):
    if not isinstance(text, str) or len(text) != 7 or text[0] != '#':
        return False
    return all(c in '0123456789abcdef' for c in text[1:])


# --- sampling ----------------------------------------------------------------

def wire_string(stops):
    """The gradient as the settings would hold it: no positions, so the
    firmware takes its uniform path, which is what a built-in gets.

    Anything that is not a string is dropped rather than raising, so a
    malformed document is reported by the rules below instead of crashing the
    tool that was run to find out what is wrong with it."""
    if not isinstance(stops, list):
        return ''
    return ','.join(s.lstrip('#') for s in stops if isinstance(s, str))


def sample_ramps(gradients, brightness_pct=TONE_BRIGHTNESS_PCT, knee_pct=TONE_KNEE_PCT,
                 gain256=TONE_GAIN_256, mode=TONE_MODE, node='node'):
    """256-entry RGB565 ramps, one per gradient, from the verified JS sampler."""
    request = json.dumps({
        'brightnessPct': brightness_pct,
        'kneePct': knee_pct,
        'gain256': gain256,
        'mode': mode,
        'gradients': [wire_string(g.get('stops')) for g in gradients],
    })
    try:
        out = subprocess.run([node, SAMPLER], input=request, capture_output=True,
                             text=True, check=True)
    except FileNotFoundError:
        die('node not found. Both gradient tools sample through '
            'web/src/config/gradientRamp.js and need it.')
    except subprocess.CalledProcessError as exc:
        die('the sampler failed:\n%s' % exc.stderr.strip())
    return json.loads(out.stdout)['ramps']


def sample_labs(ramp, positions=SAMPLE_POSITIONS):
    return [to_lab(expand_rgb565(ramp[p])) for p in positions]


def pair_distance(labs_a, labs_b):
    """(mean, min) CIEDE2000 over the contract's sample positions."""
    ds = [ciede2000(a, b) for a, b in zip(labs_a, labs_b)]
    return (sum(ds) / len(ds), min(ds))


# --- rules -------------------------------------------------------------------

def check_source_rules(doc):
    """Rules about what a gradient is. Source stops only, no rendering."""
    findings = []
    categories = doc.get('categories')
    if not isinstance(categories, list) or not categories:
        findings.append(Finding('categories', '', 'the document has no category list'))
        categories = []
    seen_names = {}
    for index, g in enumerate(doc.get('gradients', [])):
        name = g.get('name', '<unnamed %d>' % index)
        if name in seen_names:
            findings.append(Finding('duplicate_name', name,
                                    'already used at index %d' % seen_names[name]))
        seen_names[name] = index

        if g.get('category') not in categories:
            findings.append(Finding('category_unknown', name,
                                    'category %r is not in the declared list' % g.get('category')))

        stops = g.get('stops')
        if not isinstance(stops, list) or len(stops) != STOPS_PER_GRADIENT:
            findings.append(Finding('stop_count', name,
                                    'needs exactly %d stops, has %s'
                                    % (STOPS_PER_GRADIENT,
                                       len(stops) if isinstance(stops, list) else 'none')))
            continue
        bad_hex = [s for s in stops if not valid_hex(s)]
        if bad_hex:
            findings.append(Finding('stop_hex', name,
                                    'not lower-case #rrggbb: %s' % ', '.join(repr(s) for s in bad_hex)))
            continue

        rgbs = [hex_to_rgb(s) for s in stops]
        lums = [relative_luminance(c) for c in rgbs]
        for i in range(1, len(lums)):
            if lums[i] <= lums[i - 1]:
                findings.append(Finding('luminance_dip', name,
                                        'stop %d (%s, %.5f) is not brighter than stop %d (%s, %.5f)'
                                        % (i + 1, stops[i], lums[i], i, stops[i - 1], lums[i - 1])))
                break
        if lums[0] > FIRST_STOP_LUMINANCE_CEILING:
            findings.append(Finding('first_stop_bright', name,
                                    'first stop %s has relative luminance %.5f, over the %.4f ceiling'
                                    % (stops[0], lums[0], FIRST_STOP_LUMINANCE_CEILING)))
        endpoint_l = to_lab(rgbs[-1])[0]
        if endpoint_l < ENDPOINT_LSTAR_FLOOR:
            findings.append(Finding('endpoint_dim', name,
                                    'last stop %s is L* %.1f, under the %.1f floor'
                                    % (stops[-1], endpoint_l, ENDPOINT_LSTAR_FLOOR)))
    return findings


def check_original_set(doc):
    """Ids 0 to 17 are what the thresholds were measured against and what
    devices already store. If any of them changes, every recorded margin in
    this file is about a different set and a device's stored id points at a
    different picture.

    The whole record is compared, not the name: a stop edit or a category move
    leaves the names in place and is exactly the change that would go unnoticed.
    Comparing at the index also catches a reordering, because a swap puts each
    name where another one is frozen."""
    findings = []
    gradients = doc.get('gradients', [])
    if len(gradients) < len(ORIGINAL_18_RECORDS):
        findings.append(Finding('original_set_short', '',
                                'the document has %d gradients; the first %d are frozen '
                                'and the list is append only'
                                % (len(gradients), len(ORIGINAL_18_RECORDS))))
    for index, (name, category, stops) in enumerate(ORIGINAL_18_RECORDS):
        if index >= len(gradients):
            break
        g = gradients[index]
        got_name = g.get('name')
        if got_name != name:
            findings.append(Finding('original_name_changed', str(got_name),
                                    'id %d is frozen as %r; the first %d entries are '
                                    'append only and may not be renamed or reordered'
                                    % (index, name, len(ORIGINAL_18_RECORDS))))
            # The rest of the record belongs to a different gradient now, so
            # comparing its category and stops would only add noise.
            continue
        if g.get('category') != category:
            findings.append(Finding('original_category_changed', name,
                                    'id %d is frozen in %r, the document says %r'
                                    % (index, category, g.get('category'))))
        got_stops = g.get('stops')
        got_stops = tuple(got_stops) if isinstance(got_stops, list) else got_stops
        if got_stops != stops:
            # A malformed document reaches here too, so the stops are printed
            # through repr unless they really are a list of strings.
            shown = (', '.join(got_stops)
                     if isinstance(got_stops, tuple) and all(isinstance(s, str) for s in got_stops)
                     else repr(got_stops))
            findings.append(Finding('original_stops_changed', name,
                                    'id %d is frozen as %s, the document says %s'
                                    % (index, ', '.join(stops), shown)))
    return findings


def check_exemptions(doc):
    """The exemption list can name only the original 18. This is what stops it
    growing to admit a new gradient."""
    findings = []
    for a, b in NEAR_DUPLICATE_EXEMPTIONS:
        outsiders = [n for n in (a, b) if n not in ORIGINAL_18]
        if outsiders:
            findings.append(Finding('exemption_invalid', '%s / %s' % (a, b),
                                    'exemptions may only name the original 18; %s is not one'
                                    % ', '.join(outsiders)))
    return findings


def check_near_duplicates(doc, ramps):
    """The rendered rule: are two gradients the same picture?"""
    gradients = doc.get('gradients', [])
    usable = [(i, g) for i, g in enumerate(gradients)
              if isinstance(g.get('stops'), list) and len(g['stops']) == STOPS_PER_GRADIENT
              and all(valid_hex(s) for s in g['stops']) and ramps[i] is not None]
    labs = {i: sample_labs(ramps[i]) for i, _ in usable}
    exempt = {frozenset(p) for p in NEAR_DUPLICATE_EXEMPTIONS}

    pairs = []
    findings = []
    for (i, ga), (j, gb) in itertools.combinations(usable, 2):
        mean, low = pair_distance(labs[i], labs[j])
        is_exempt = frozenset((ga['name'], gb['name'])) in exempt
        pairs.append((mean, low, ga['name'], gb['name'], is_exempt))
        if mean < NEAR_DUPLICATE_DE and not is_exempt:
            findings.append(Finding('near_duplicate', '%s / %s' % (ga['name'], gb['name']),
                                    'mean dE2000 %.2f over %d ramp samples, under the %.1f threshold'
                                    % (mean, SAMPLE_COUNT, NEAR_DUPLICATE_DE)))
    pairs.sort(key=lambda p: p[0])
    return findings, pairs


# --- the crowding report -----------------------------------------------------
# Beside the gate, never inside it. Nothing below returns a Finding and nothing
# below can fail a run. See WHAT THE PAIR GATE CANNOT SEE at the top for why the
# review list is the within-category pairs and where the bands come from.

def category_order(doc):
    """The declared categories first, then any category a gradient claims that
    the document did not declare. The second group is a fault the source rules
    already report; the report still has to put those entries somewhere."""
    declared = doc.get('categories')
    order = list(declared) if isinstance(declared, list) else []
    extra = set()
    for g in doc.get('gradients', []):
        c = g.get('category')
        if isinstance(c, str) and c not in order:
            extra.add(c)
    return order + sorted(extra)


def crowding_report(doc, pairs):
    """How dense the set is, as a table a reviewer can act on.

    `pairs` is what check_near_duplicates returns, sorted closest first. The
    answer has three parts: how many pairs sit under each review band, the same
    counts per category, and every gradient's nearest neighbour. A set can pass
    the 8.0 gate on every pair and still be crowded, and this is what says so.
    """
    category = {}
    for g in doc.get('gradients', []):
        name = g.get('name')
        if isinstance(name, str) and name not in category:
            category[name] = g.get('category')

    def same_category(a, b):
        ca = category.get(a)
        return ca is not None and ca == category.get(b)

    within = [p for p in pairs if same_category(p[2], p[3])]

    # pairs is sorted, so the first time a name appears is its nearest.
    nearest = {}
    nearest_in_category = {}
    for mean, _low, a, b, _exempt in pairs:
        for one, other in ((a, b), (b, a)):
            nearest.setdefault(one, (mean, other))
            if same_category(one, other):
                nearest_in_category.setdefault(one, (mean, other))

    def counts(subset):
        return [sum(1 for p in subset if p[0] < band) for band in REVIEW_BANDS]

    categories = []
    for cname in category_order(doc):
        members = [n for n, c in category.items() if c == cname]
        cpairs = [p for p in within if category.get(p[2]) == cname]
        categories.append({
            'name': cname,
            'entries': len(members),
            'pairs': len(cpairs),
            'counts': counts(cpairs),
            'closest': ({'mean': cpairs[0][0], 'min': cpairs[0][1],
                         'a': cpairs[0][2], 'b': cpairs[0][3], 'exempt': cpairs[0][4]}
                        if cpairs else None),
        })

    rows = []
    for g in doc.get('gradients', []):
        name = g.get('name')
        if not isinstance(name, str) or name not in nearest:
            continue
        mean, other = nearest[name]
        cmean, cother = nearest_in_category.get(name, (None, None))
        rows.append({
            'name': name,
            'category': category.get(name),
            'neighbour': other,
            'mean': mean,
            'sameCategory': same_category(name, other),
            'categoryNeighbour': cother,
            'categoryMean': cmean,
        })

    widest = REVIEW_BANDS[-1]
    review = [{'mean': m, 'min': lo, 'a': a, 'b': b, 'exempt': ex,
               'category': category.get(a)}
              for m, lo, a, b, ex in within if m < widest]

    return {
        'bands': list(REVIEW_BANDS),
        'pairs': len(pairs),
        'withinCategoryPairs': len(within),
        'bandCounts': [{'band': band, 'all': a, 'withinCategory': w}
                       for band, a, w in zip(REVIEW_BANDS, counts(pairs), counts(within))],
        'categories': categories,
        'nearest': rows,
        'review': review,
    }


def check(doc, ramps=None, node='node'):
    """Every rule. Returns (findings, pairs)."""
    findings = check_source_rules(doc) + check_original_set(doc) + check_exemptions(doc)
    gradients = doc.get('gradients', [])
    if ramps is None:
        ramps = sample_ramps(gradients, node=node)
    for i, g in enumerate(gradients):
        if i < len(ramps) and ramps[i] is None:
            findings.append(Finding('parse_failed', g.get('name', '<unnamed>'),
                                    'the firmware parser would reject %r' % wire_string(g.get('stops', []))))
    dup_findings, pairs = check_near_duplicates(doc, ramps)
    return findings + dup_findings, pairs


def die(message):
    sys.stderr.write('gradient_check: %s\n' % message)
    sys.exit(2)


def report_crowding(crowding, show_review=15):
    """Print the advisory half. Reads, never decides."""
    bands = crowding['bands']
    print('\n  crowding, advisory: %d of %d pairs are inside one category'
          % (crowding['withinCategoryPairs'], crowding['pairs']))
    print('    %-10s %8s %18s' % ('band', 'all pairs', 'within a category'))
    for row in crowding['bandCounts']:
        print('    under %-4.1f %8d %18d' % (row['band'], row['all'], row['withinCategory']))

    print('\n    per category, pairs inside it under each band:')
    print('      %-18s %7s %6s %s   %s'
          % ('category', 'entries', 'pairs',
             ' '.join('%6s' % ('<%.0f' % b) for b in bands), 'closest pair inside it'))
    for c in crowding['categories']:
        closest = ('%6.2f  %s / %s%s'
                   % (c['closest']['mean'], c['closest']['a'], c['closest']['b'],
                      ' [exempt]' if c['closest']['exempt'] else '')) if c['closest'] else '-'
        print('      %-18s %7d %6d %s   %s'
              % (c['name'], c['entries'], c['pairs'],
                 ' '.join('%6d' % n for n in c['counts']), closest))

    review = crowding['review']
    if show_review and review:
        print('\n    review list: the closest %d of %d within-category pairs under %.1f'
              % (min(show_review, len(review)), len(review), bands[-1]))
        for r in review[:show_review]:
            print('      %6.2f  (min %5.2f)  %s / %s  [%s]%s'
                  % (r['mean'], r['min'], r['a'], r['b'], r['category'],
                     ' [exempt]' if r['exempt'] else ''))

    print('\n    each gradient and its nearest neighbour (and its nearest inside its own category):')
    for row in crowding['nearest']:
        if row['categoryMean'] is None:
            inside = '%6s  %s' % ('-', '(alone in its category)')
        else:
            inside = '%6.2f  %s' % (row['categoryMean'], row['categoryNeighbour'])
        print('      %-16s %6.2f  %-16s   %s'
              % (row['name'], row['mean'], row['neighbour'], inside))


def report(doc, findings, pairs, show_pairs=10, crowding=None, show_review=15):
    print('gradient set check: %d gradients, %d categories'
          % (len(doc.get('gradients', [])), len(doc.get('categories', []))))
    print('  contract: brightness %d%%, rolloff %d%%, gain %d, %s, %d ramp samples, '
          'mean CIEDE2000 on RGB565'
          % (TONE_BRIGHTNESS_PCT, TONE_KNEE_PCT, TONE_GAIN_256, TONE_MODE, SAMPLE_COUNT))
    print('  thresholds: first stop <= %.4f relative luminance, endpoint L* >= %.1f, '
          'pairs >= %.1f dE2000'
          % (FIRST_STOP_LUMINANCE_CEILING, ENDPOINT_LSTAR_FLOOR, NEAR_DUPLICATE_DE))

    if pairs:
        print('\n  closest %d of %d pairs:' % (min(show_pairs, len(pairs)), len(pairs)))
        for mean, low, a, b, is_exempt in pairs[:show_pairs]:
            margin = mean - NEAR_DUPLICATE_DE
            tag = ' [exempt]' if is_exempt else ''
            print('    %6.2f  (min %5.2f, margin %+6.2f)  %s / %s%s'
                  % (mean, low, margin, a, b, tag))
        free = [p for p in pairs if not p[4]]
        if free:
            print('    closest pair that is not exempt: %.2f (%s / %s), margin %+.2f'
                  % (free[0][0], free[0][2], free[0][3], free[0][0] - NEAR_DUPLICATE_DE))

    # The margins on the two source thresholds, so a reader can see how much
    # room the set has without running anything else.
    grads = [g for g in doc.get('gradients', [])
             if isinstance(g.get('stops'), list) and len(g['stops']) == STOPS_PER_GRADIENT
             and all(valid_hex(s) for s in g['stops'])]
    if grads:
        first = max((relative_luminance(hex_to_rgb(g['stops'][0])), g['name']) for g in grads)
        last = min((to_lab(hex_to_rgb(g['stops'][-1]))[0], g['name']) for g in grads)
        print('\n  brightest first stop: %s at %.5f (ceiling %.4f, margin %.5f)'
              % (first[1], first[0], FIRST_STOP_LUMINANCE_CEILING,
                 FIRST_STOP_LUMINANCE_CEILING - first[0]))
        print('  dimmest endpoint:     %s at L* %.2f (floor %.1f, margin %.2f)'
              % (last[1], last[0], ENDPOINT_LSTAR_FLOOR, last[0] - ENDPOINT_LSTAR_FLOOR))

    if crowding:
        report_crowding(crowding, show_review=show_review)

    if findings:
        print('\n  FAIL: %d finding%s' % (len(findings), '' if len(findings) == 1 else 's'))
        for f in findings:
            print('    %s' % f)
    else:
        print('\n  OK: every rule passes')


def main(argv=None):
    ap = argparse.ArgumentParser(description='Set-quality check for data/gradients.json')
    ap.add_argument('--source', default=SOURCE, help='gradient document to check')
    ap.add_argument('--json', dest='json_out', help='write the pair table and findings here')
    ap.add_argument('--pairs', type=int, default=10, help='how many closest pairs to print')
    ap.add_argument('--review', type=int, default=15,
                    help='how many within-category pairs to list in the crowding report')
    ap.add_argument('--no-crowding', dest='crowding', action='store_false',
                    help='skip the advisory crowding report')
    ap.add_argument('--node', default='node', help='node executable')
    args = ap.parse_args(argv)

    try:
        with open(args.source, encoding='utf-8') as f:
            doc = json.load(f)
    except (OSError, ValueError) as exc:
        die('cannot read %s: %s' % (args.source, exc))

    findings, pairs = check(doc, node=args.node)
    crowding = crowding_report(doc, pairs) if args.crowding else None
    report(doc, findings, pairs, show_pairs=args.pairs, crowding=crowding,
           show_review=args.review)

    if args.json_out:
        with open(args.json_out, 'w', encoding='utf-8') as f:
            json.dump({
                'contract': {
                    'brightnessPct': TONE_BRIGHTNESS_PCT,
                    'kneePct': TONE_KNEE_PCT,
                    'gain256': TONE_GAIN_256,
                    'mode': TONE_MODE,
                    'positions': SAMPLE_POSITIONS,
                    'metric': 'mean CIEDE2000 over the sample positions, RGB565 expanded by replication',
                },
                'thresholds': {
                    'firstStopLuminanceCeiling': FIRST_STOP_LUMINANCE_CEILING,
                    'endpointLstarFloor': ENDPOINT_LSTAR_FLOOR,
                    'nearDuplicateDE': NEAR_DUPLICATE_DE,
                    'exemptions': [list(p) for p in NEAR_DUPLICATE_EXEMPTIONS],
                },
                'pairs': [{'mean': m, 'min': lo, 'a': a, 'b': b, 'exempt': ex}
                          for m, lo, a, b, ex in pairs],
                'findings': [{'code': f.code, 'name': f.name, 'detail': f.detail}
                             for f in findings],
                'crowding': crowding if crowding else crowding_report(doc, pairs),
            }, f, indent=1)

    return 1 if findings else 0


if __name__ == '__main__':
    sys.exit(main())
