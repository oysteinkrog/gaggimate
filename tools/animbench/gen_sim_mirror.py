#!/usr/bin/env python3
"""Regenerate the simulator's copies of the animation roster from the source.

The registry (BgAnimRegistry.cpp and the Anim*.cpp structs) does not compile
into the desktop simulator, so two files carry a copy of it by hand:

  src/display/ui/default/settings/CatAnimation.cpp   kSimAnims[] (name and
                                                     the eight slot table)
  tools/settings_ui_tests/test_animation.py          ANIM_NAMES

Both drifted before this script existed (24 and 14 names against 44, eight
of them pre-rename). Run it after any change to a BgAnimation struct:

  python3 tools/animbench/gen_sim_mirror.py          rewrite both copies
  python3 tools/animbench/gen_sim_mirror.py --check  exit 1 if either differs

check-params.py runs the --check form, so a stale copy fails the same gate
that catches a firmware and web mismatch.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
ANIM_DIR = ROOT / "src/display/ui/default/bganim"
CAT = ROOT / "src/display/ui/default/settings/CatAnimation.cpp"
TEST = ROOT / "tools/settings_ui_tests/test_animation.py"

STRUCT_RE = re.compile(r"const BgAnimation bg_anim_(\w+)\s*=\s*\{(.*?)\n\};", re.S)
PARAM_RE = re.compile(r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*(\d+)\s*\}')
SLOTS = 8


def roster():
    registry = (ANIM_DIR / "BgAnimRegistry.cpp").read_text(encoding="utf-8")
    order, seen = [], set()
    for symbol in re.findall(r"bg_anim_(\w+)", registry):
        if symbol not in seen:
            seen.add(symbol)
            order.append(symbol)
    by_symbol = {}
    for path in ANIM_DIR.glob("Anim*.cpp"):
        m = STRUCT_RE.search(path.read_text(encoding="utf-8"))
        if m is None:
            raise SystemExit(f"cannot parse BgAnimation struct in {path.name}")
        body = m.group(2)
        head = body.split("{", 1)[0]
        strings = re.findall(r'"([^"]*)"', head)
        name = strings[1]
        params = [(k, l, int(d)) for k, l, d in PARAM_RE.findall(body)]
        if len(params) > SLOTS:
            raise SystemExit(f"{path.name}: {len(params)} params, cap is {SLOTS}")
        by_symbol[m.group(1)] = (name, params)
    return [by_symbol[s] for s in order if s in by_symbol]


def cpp_block(entries):
    lines = ["constexpr SimAnim kSimAnims[] = {"]
    for name, params in entries:
        cells = [f'{{"{k}", "{l}", {d}}}' for k, l, d in params]
        cells += ["{nullptr, nullptr, 0}"] * (SLOTS - len(params))
        lines.append(f'    {{"{name}", {{{", ".join(cells)}}}}},')
    lines.append("};")
    return "\n".join(lines)


def py_block(entries):
    names = [name for name, _ in entries]
    lines, row = ["ANIM_NAMES = ["], "   "
    for n in names:
        cell = f' "{n}",'
        if len(row) + len(cell) > 100:
            lines.append(row)
            row = "   "
        row += cell
    lines.append(row)
    lines.append("]")
    return "\n".join(lines)


CPP_RE = re.compile(r"constexpr SimAnim kSimAnims\[\] = \{\n.*?\n\};", re.S)
PY_RE = re.compile(r"ANIM_NAMES = \[\n.*?\n\]", re.S)


def render():
    entries = roster()
    cat = CAT.read_text(encoding="utf-8")
    test = TEST.read_text(encoding="utf-8")
    if not CPP_RE.search(cat):
        raise SystemExit("kSimAnims block not found in CatAnimation.cpp")
    if not PY_RE.search(test):
        raise SystemExit("ANIM_NAMES block not found in test_animation.py")
    new_cat = CPP_RE.sub(lambda _: cpp_block(entries), cat)
    new_test = PY_RE.sub(lambda _: py_block(entries), test)
    return (cat, new_cat), (test, new_test), len(entries)


def main():
    check = "--check" in sys.argv[1:]
    (cat, new_cat), (test, new_test), n = render()
    stale = [p.name for p, old, new in ((CAT, cat, new_cat), (TEST, test, new_test)) if old != new]
    if check:
        if stale:
            print(f"simulator mirror stale: {', '.join(stale)} (run tools/animbench/gen_sim_mirror.py)")
            return 1
        print(f"ok — simulator mirror agrees, {n} animations")
        return 0
    if new_cat != cat:
        CAT.write_text(new_cat, encoding="utf-8")
    if new_test != test:
        TEST.write_text(new_test, encoding="utf-8")
    print(f"{'rewrote ' + ', '.join(stale) if stale else 'already current'}: {n} animations")
    return 0


if __name__ == "__main__":
    sys.exit(main())
