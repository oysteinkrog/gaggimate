#!/usr/bin/env python3
"""Unit tests for tools/gradient_check.py.

Each rule is checked by the specific failure it is supposed to produce, not by
"something failed": a checker that rejects everything for the wrong reason is
worse than no checker, because the next person edits the wrong stop.

    python3 tools/gradient_check_test.py

Exits non-zero on the first failing case. Runs from tools/animbench `make
check`, alongside gradient_check.py itself.
"""

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


def main():
    real = base()
    expect_clean('the current set', real)

    doc = base()
    find(doc, 'Gold')['stops'][2] = '#6B3413'
    expect('an upper-case hex stop', doc, 'stop_hex')

    doc = base()
    find(doc, 'Gold')['stops'][2] = '#6b341'
    expect('a five-digit hex stop', doc, 'stop_hex')

    doc = base()
    find(doc, 'Gold')['stops'][2] = 'rebeccapurple'
    expect('a colour name instead of hex', doc, 'stop_hex')

    doc = base()
    del find(doc, 'Gold')['stops'][2]
    expect('a five-stop entry', doc, 'stop_count')

    doc = base()
    # Stop 4 darker than stop 3: the ramp stops rising, so an animation
    # indexing the palette by intensity draws a dip.
    g = find(doc, 'Gold')
    g['stops'][3] = '#1a1206'
    expect('a luminance dip', doc, 'luminance_dip')

    doc = base()
    # #141414 is relative luminance 0.00605, over the 0.0040 ceiling.
    find(doc, 'Gold')['stops'][0] = '#141414'
    expect('an overly bright first stop', doc, 'first_stop_bright',
           forbidden=('luminance_dip',))

    doc = base()
    # A ramp that rises all the way but tops out at #5a6a8e, L* 44.8, well
    # under the 58.0 floor. It rises, so the dip rule has nothing to say: this
    # asserts the endpoint rule on its own.
    find(doc, 'Deep Space')['stops'] = ['#01020a', '#0a1030', '#1a2a5a', '#2c3e78', '#45568e', '#5a6a8e']
    expect('an overly dim endpoint', doc, 'endpoint_dim', forbidden=('luminance_dip',))

    doc = base()
    find(doc, 'Gold')['category'] = 'Condiments'
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
