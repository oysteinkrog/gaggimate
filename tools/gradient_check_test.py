#!/usr/bin/env python3
"""Unit tests for tools/gradient_check.py.

Each rule is checked by the specific failure it is supposed to produce, not by
"something failed": a checker that rejects everything for the wrong reason is
worse than no checker, because the next person edits the wrong stop.

    python3 tools/gradient_check_test.py

Exits non-zero on the first failing case. Runs from tools/animbench `make
check`, alongside gradient_check.py itself.
"""

import colorsys
import copy
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import gradient_check as gc  # noqa: E402

FAILURES = []


def load_real():
    with open(gc.SOURCE, encoding='utf-8') as f:
        return json.load(f)


def codes(doc):
    findings, _pairs = gc.check(doc)
    return sorted(f.code for f in findings)


def expect(case, doc, wanted, forbidden=()):
    """`wanted` must appear; anything in `forbidden` must not."""
    got = codes(doc)
    ok = wanted in got and all(f not in got for f in forbidden)
    print('%-4s %-34s -> %s' % ('ok' if ok else 'FAIL', case, ', '.join(got) or '(clean)'))
    if not ok:
        FAILURES.append('%s: wanted %r, got %r' % (case, wanted, got))


def expect_clean(case, doc):
    got = codes(doc)
    ok = not got
    print('%-4s %-34s -> %s' % ('ok' if ok else 'FAIL', case, ', '.join(got) or '(clean)'))
    if not ok:
        FAILURES.append('%s: wanted no findings, got %r' % (case, got))


def base():
    """The real set, so every fixture differs from it in exactly one way."""
    return copy.deepcopy(load_real())


def find(doc, name):
    for g in doc['gradients']:
        if g['name'] == name:
            return g
    raise KeyError(name)


def check_true(case, ok, detail=''):
    """For the cases whose answer is not a finding code."""
    print('%-4s %-34s -> %s' % ('ok' if ok else 'FAIL', case, detail or ('true' if ok else 'false')))
    if not ok:
        FAILURES.append('%s: %s' % (case, detail or 'expected true'))


# --- the adversarial fixture --------------------------------------------------
# A set that the pair gate has nothing to say about and that is crowded anyway.
# It is built rather than written out: a greedy packing that accepts a candidate
# only if it clears the 8.0 floor against everything already accepted lands most
# of its pairs just above the floor, which is the shape the gate cannot see. The
# pool is fixed, so the fixture is the same on every run.

FIXTURE_LEVELS = (0.03, 0.13, 0.28, 0.46, 0.68, 0.90)
FIXTURE_CATEGORIES = ('Warm', 'Green', 'Cool', 'Violet')


def fixture_ramp(hue, sat, gamma):
    """A dark-to-bright ramp of one hue, desaturating toward the highlight, so
    the source rules have nothing to say about it either."""
    stops = []
    for level in FIXTURE_LEVELS:
        lightness = level ** gamma
        s = sat * (1.0 - 0.75 * max(0.0, (lightness - 0.5) / 0.5))
        r, g, b = colorsys.hls_to_rgb((hue % 360) / 360.0, lightness, s)
        stops.append('#%02x%02x%02x' % (round(r * 255), round(g * 255), round(b * 255)))
    return stops


def adversarial_fixture(floor=None, want=24):
    """A document whose every pair is at or above the near-duplicate floor."""
    floor = gc.NEAR_DUPLICATE_DE if floor is None else floor
    pool = [{'name': 'h%03d s%02d g%03d' % (hue, sat * 100, gamma * 100),
             'category': FIXTURE_CATEGORIES[0], 'stops': fixture_ramp(hue, sat, gamma)}
            for hue in range(0, 360, 4)
            for sat in (0.55, 0.8)
            for gamma in (0.9, 1.1)]
    # Drop the candidates the source rules would reject, so the only thing the
    # fixture exercises is the pair gate and the crowding report.
    rejected = {f.name for f in gc.check_source_rules(
        {'categories': list(FIXTURE_CATEGORIES), 'gradients': pool})}
    pool = [g for g in pool if g['name'] not in rejected]

    labs = [gc.sample_labs(r) for r in gc.sample_ramps(pool)]
    chosen = []
    for i in range(len(pool)):
        if all(gc.pair_distance(labs[i], labs[j])[0] > floor for j in chosen):
            chosen.append(i)
        if len(chosen) >= want:
            break

    # Consecutive entries are the closest ones, so chunking in order puts the
    # crowding inside the categories, which is where a user meets it.
    per = (len(chosen) + len(FIXTURE_CATEGORIES) - 1) // len(FIXTURE_CATEGORIES)
    gradients = []
    for n, i in enumerate(chosen):
        g = copy.deepcopy(pool[i])
        g['category'] = FIXTURE_CATEGORIES[min(n // per, len(FIXTURE_CATEGORIES) - 1)]
        gradients.append(g)
    return {'categories': list(FIXTURE_CATEGORIES), 'gradients': gradients}


def check_adversarial_fixture():
    """The gate passes every pair, and the crowding report is still populated.

    The frozen baseline and the exemption list are swapped for the fixture's own
    while this runs: with no exemptions at all, "every pair at or above 8.0" is
    literal rather than "every pair the gate looks at"."""
    doc = adversarial_fixture()
    saved_records = gc.ORIGINAL_18_RECORDS
    saved_names = gc.ORIGINAL_18
    saved_exemptions = gc.NEAR_DUPLICATE_EXEMPTIONS
    try:
        gc.ORIGINAL_18_RECORDS = tuple((g['name'], g['category'], tuple(g['stops']))
                                       for g in doc['gradients'][:len(saved_records)])
        gc.ORIGINAL_18 = tuple(n for n, _c, _s in gc.ORIGINAL_18_RECORDS)
        gc.NEAR_DUPLICATE_EXEMPTIONS = ()
        findings, pairs = gc.check(doc)
        crowding = gc.crowding_report(doc, pairs)
    finally:
        gc.ORIGINAL_18_RECORDS = saved_records
        gc.ORIGINAL_18 = saved_names
        gc.NEAR_DUPLICATE_EXEMPTIONS = saved_exemptions

    entries = len(doc['gradients'])
    closest = min(p[0] for p in pairs) if pairs else 0.0
    under_first_band = crowding['bandCounts'][0]['withinCategory']

    check_true('the fixture is a real set', entries >= len(saved_records),
               '%d entries in %d categories' % (entries, len(doc['categories'])))
    check_true('the fixture clears the pair gate', not findings and closest >= gc.NEAR_DUPLICATE_DE,
               'closest pair %.2f, floor %.1f, %d findings' % (closest, gc.NEAR_DUPLICATE_DE, len(findings)))
    check_true('and the crowding report sees it',
               under_first_band > 0 and len(crowding['review']) > 0,
               '%d within-category pairs under %.1f, %d under %.1f, of %d'
               % (under_first_band, crowding['bands'][0], len(crowding['review']),
                  crowding['bands'][-1], crowding['withinCategoryPairs']))


def check_crowding_on_the_real_set(real):
    """The report has to agree with the pair table it is built from."""
    _findings, pairs = gc.check(real)
    crowding = gc.crowding_report(real, pairs)

    names = [g['name'] for g in real['gradients']]
    check_true('a nearest-neighbour row per gradient',
               [r['name'] for r in crowding['nearest']] == names,
               '%d rows, %d gradients' % (len(crowding['nearest']), len(names)))

    # The report says a name's nearest neighbour; the pair table is the only
    # source for that, so recompute it the long way and compare.
    truth = {}
    for mean, _low, a, b, _exempt in pairs:
        for one, other in ((a, b), (b, a)):
            if one not in truth or mean < truth[one][0]:
                truth[one] = (mean, other)
    wrong = [r['name'] for r in crowding['nearest']
             if truth[r['name']] != (r['mean'], r['neighbour'])]
    check_true('each row is the real nearest', not wrong,
               'mismatched: %s' % ', '.join(wrong) if wrong else 'all %d agree' % len(names))

    bands = crowding['bands']
    counts = [row['all'] for row in crowding['bandCounts']]
    within = [row['withinCategory'] for row in crowding['bandCounts']]
    check_true('the bands are cumulative',
               bands == sorted(bands) and counts == sorted(counts) and within == sorted(within),
               ' '.join('<%.0f: %d of %d' % (b, w, a) for b, a, w in zip(bands, counts, within)))
    check_true('the per-category counts add up',
               sum(c['pairs'] for c in crowding['categories']) == crowding['withinCategoryPairs'],
               '%d within-category pairs over %d categories'
               % (crowding['withinCategoryPairs'], len(crowding['categories'])))
    check_true('the shipped set is crowded', len(crowding['review']) > 0,
               '%d within-category pairs under %.1f' % (len(crowding['review']), bands[-1]))


def main():
    real = base()
    expect_clean('the current set', real)

    doc = base()
    find(doc, 'Mocha')['stops'][2] = '#6B3413'
    expect('an upper-case hex stop', doc, 'stop_hex')

    doc = base()
    find(doc, 'Mocha')['stops'][2] = '#6b341'
    expect('a five-digit hex stop', doc, 'stop_hex')

    doc = base()
    find(doc, 'Mocha')['stops'][2] = 'rebeccapurple'
    expect('a colour name instead of hex', doc, 'stop_hex')

    doc = base()
    del find(doc, 'Mocha')['stops'][2]
    expect('a five-stop entry', doc, 'stop_count')

    doc = base()
    # Stop 4 darker than stop 3: the ramp stops rising, so an animation
    # indexing the palette by intensity draws a dip.
    g = find(doc, 'Mocha')
    g['stops'][3] = '#1a1206'
    expect('a luminance dip', doc, 'luminance_dip')

    doc = base()
    # #141414 is relative luminance 0.00605, over the 0.0040 ceiling.
    find(doc, 'Mocha')['stops'][0] = '#141414'
    expect('an overly bright first stop', doc, 'first_stop_bright',
           forbidden=('luminance_dip',))

    doc = base()
    # A ramp that rises all the way but tops out at #5a6a8e, L* 44.8, well
    # under the 58.0 floor. It rises, so the dip rule has nothing to say: this
    # asserts the endpoint rule on its own.
    find(doc, 'Midnight')['stops'] = ['#01020a', '#0a1030', '#1a2a5a', '#2c3e78', '#45568e', '#5a6a8e']
    expect('an overly dim endpoint', doc, 'endpoint_dim', forbidden=('luminance_dip',))

    doc = base()
    find(doc, 'Mocha')['category'] = 'Condiments'
    expect('an unknown category', doc, 'category_unknown')

    doc = base()
    # A copy of Espresso with one stop nudged by four counts. This is the case
    # the near-duplicate rule exists for, and no exemption covers it because an
    # exemption can only name two of the original 18.
    clone = copy.deepcopy(find(doc, 'Espresso'))
    clone['name'] = 'Espresso Dark'
    clone['stops'][3] = '#b8703e'
    doc['gradients'].append(clone)
    expect('a near copy of an existing entry', doc, 'near_duplicate')

    doc = base()
    clone = copy.deepcopy(find(doc, 'Espresso'))
    clone['name'] = 'Espresso'
    doc['gradients'].append(clone)
    expect('a repeated name', doc, 'duplicate_name')

    # An exemption naming something outside the original 18 is refused, which
    # is the mechanism that keeps the list from growing to admit a new entry.
    doc = base()
    saved = gc.NEAR_DUPLICATE_EXEMPTIONS
    try:
        gc.NEAR_DUPLICATE_EXEMPTIONS = saved + (('Espresso', 'Espresso Dark'),)
        clone = copy.deepcopy(find(doc, 'Espresso'))
        clone['name'] = 'Espresso Dark'
        clone['stops'][3] = '#b8703e'
        doc['gradients'].append(clone)
        expect('an exemption for a new entry', doc, 'exemption_invalid')
    finally:
        gc.NEAR_DUPLICATE_EXEMPTIONS = saved

    # The frozen originals. Each mutation changes one field of one of the first
    # 18 and nothing else, because a checker that fails for the wrong reason
    # teaches the next person to edit the wrong thing. The four together are the
    # only way to know the freeze can fail at all: before this table the check
    # compared names, so three of the four passed it.
    check_true('the baseline is the shipped first 18',
               tuple((g['name'], g['category'], tuple(g['stops']))
                     for g in real['gradients'][:len(gc.ORIGINAL_18_RECORDS)])
               == gc.ORIGINAL_18_RECORDS,
               '%d records frozen' % len(gc.ORIGINAL_18_RECORDS))

    doc = base()
    find(doc, 'Gold')['name'] = 'Golden'
    expect('a renamed original', doc, 'original_name_changed')

    doc = base()
    # Forest and Sunset swap places, so ids 3 and 4 point at each other's
    # gradient on a device that already stored one.
    doc['gradients'][3], doc['gradients'][4] = doc['gradients'][4], doc['gradients'][3]
    expect('a reordered original', doc, 'original_name_changed')

    doc = base()
    find(doc, 'Gold')['category'] = 'Nature'
    expect('a recategorised original', doc, 'original_category_changed',
           forbidden=('category_unknown',))

    doc = base()
    # One count on the blue channel of one stop: too small to move any other
    # rule, which is the point.
    find(doc, 'Gold')['stops'][2] = '#6e5015'
    expect('a changed stop on an original', doc, 'original_stops_changed',
           forbidden=('luminance_dip', 'near_duplicate'))

    doc = base()
    # The same edit outside the frozen 18 is allowed: the freeze is on ids 0 to
    # 17, not on the set.
    find(doc, 'Mocha')['stops'][2] = '#6c3927'
    expect_clean('the same edit on a later entry', doc)

    doc = base()
    # Deleting one shifts every later entry up, so the freeze sees id 17 holding
    # the wrong gradient rather than a short document.
    del doc['gradients'][17]
    expect('a deleted original', doc, 'original_name_changed')

    doc = base()
    doc['gradients'] = doc['gradients'][:10]
    expect('a document shorter than the freeze', doc, 'original_set_short')

    # Criterion six of gm-nov3.24, as a test rather than as a promise: the
    # exemption list is the four grandfathered pairs of the original 18 and
    # nothing has been added to admit a later gradient.
    check_true('the exemption list is unchanged',
               len(gc.NEAR_DUPLICATE_EXEMPTIONS) == 4
               and all(n in gc.ORIGINAL_18 for p in gc.NEAR_DUPLICATE_EXEMPTIONS for n in p),
               '%d pairs, all inside the original %d'
               % (len(gc.NEAR_DUPLICATE_EXEMPTIONS), len(gc.ORIGINAL_18)))

    # The crowding report, which fails nothing and so has to be checked against
    # the pair table it summarises.
    check_crowding_on_the_real_set(real)
    check_adversarial_fixture()

    # The sampling contract itself: the sampler must take the firmware's
    # uniform path for a built-in. Espresso's last ramp entry is 63288 there
    # and 65337 through the positional arithmetic the editor's helpers use.
    ramps = gc.sample_ramps([find(real, 'Espresso')])
    got = ramps[0][255]
    ok = got == 63288
    print('%-4s %-34s -> %d' % ('ok' if ok else 'FAIL', 'Espresso ramp entry 255', got))
    if not ok:
        FAILURES.append('Espresso ramp entry 255: wanted 63288, got %d' % got)

    if FAILURES:
        print('\ngradient_check_test: %d failure%s' % (len(FAILURES), '' if len(FAILURES) == 1 else 's'))
        for f in FAILURES:
            print('  %s' % f)
        return 1
    print('\ngradient_check_test: all cases pass')
    return 0


if __name__ == '__main__':
    sys.exit(main())
