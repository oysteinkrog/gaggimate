#!/usr/bin/env python3
"""Verify the animation parameter tables agree between the firmware registry
and the web UI mirror.

Why this exists: bg_parse_params() fills a param slot from the registry only
when its key is non-null, and zeroes it otherwise. So deleting a param entry
from an animation's BgAnimation struct does not fail to build and does not
warn — it silently feeds 0 where the intended default belonged. That is how
an optimization pass once dropped caustics' "contrast" param: threshold fell
from 0.44 to 0.14, the near-black background lifted, and only a golden-frame
diff caught it.

Checks:
  1. Registry order matches the web UI order — the index IS the persisted
     setting value, so a reorder silently repoints everyone's saved choice.
  2. Every animation's param keys and defaults match between firmware and web.
  3. The bench page (tools/animbench/web/anim_bench.html) carries the same
     order, keys and defaults: the page is the spec the ports are written
     from, and since gm-3vj every parameter is implemented there too.
  4. The simulator's copies of the roster agree (gen_sim_mirror.py --check).

Run from anywhere:  python3 tools/animbench/check-params.py
Exits non-zero on any mismatch.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
ANIM_DIR = ROOT / "src/display/ui/default/bganim"
WEB_CONFIG = ROOT / "web/src/config/bgAnimations.js"
PAGE = ROOT / "tools/animbench/web/anim_bench.html"
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import gen_sim_mirror  # noqa: E402

STRUCT_RE = re.compile(r"const BgAnimation bg_anim_(\w+)\s*=\s*\{(.*?)\n\};", re.S)
PARAM_RE = re.compile(r'\{\s*"([^"]+)"\s*,\s*"[^"]+"\s*,\s*(\d+)\s*\}')
WEB_ENTRY_RE = re.compile(r"\{\s*id:\s*'([^']+)'.*?params:\s*\[(.*?)\]\s*,?\s*\}", re.S)


def firmware_tables():
    registry = (ANIM_DIR / "BgAnimRegistry.cpp").read_text(encoding="utf-8")
    order, seen = [], set()
    for symbol in re.findall(r"bg_anim_(\w+)", registry):
        if symbol not in seen:
            seen.add(symbol)
            order.append(symbol)

    by_symbol = {}
    for path in ANIM_DIR.glob("Anim*.cpp"):
        match = STRUCT_RE.search(path.read_text(encoding="utf-8"))
        if match is None:
            print(f"cannot parse BgAnimation struct in {path.name}")
            sys.exit(2)
        body = match.group(2)
        # The animation's id/name are the bare strings before the params brace.
        head = body.split("{", 1)[0]
        anim_id = re.findall(r'"([^"]*)"', head)[0]
        params = [(k, int(v)) for k, v in PARAM_RE.findall(body)]
        by_symbol[match.group(1)] = (anim_id, params)

    return [by_symbol[s] for s in order if s in by_symbol]


def web_tables():
    js = WEB_CONFIG.read_text(encoding="utf-8")
    out = []
    for anim_id, block in WEB_ENTRY_RE.findall(js):
        keys = re.findall(r"key:\s*'([^']+)'", block)
        defs = re.findall(r"def:\s*(\d+)", block)
        out.append((anim_id, [(k, int(d)) for k, d in zip(keys, defs)]))
    return out


def page_tables():
    html = PAGE.read_text(encoding="utf-8")
    out = []
    for chunk in html.split("ANIMS.push({")[1:]:
        m = re.match(r"\s*id:\s*'([^']+)'", chunk)
        if m is None:
            continue
        start = chunk.find("params:")
        if start < 0:
            out.append((m.group(1), []))
            continue
        i = chunk.index("[", start)
        depth, j = 0, i
        while True:
            c = chunk[j]
            if c == "[":
                depth += 1
            elif c == "]":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        block = chunk[i : j + 1]
        keys = re.findall(r"key:\s*'([^']+)'", block)
        defs = re.findall(r"def:\s*(\d+)", block)
        out.append((m.group(1), [(k, int(d)) for k, d in zip(keys, defs)]))
    return out


def compare(fw, other, what):
    problems = 0
    fw_ids = [a for a, _ in fw]
    other_ids = [a for a, _ in other]
    if fw_ids != other_ids:
        problems += 1
        print(f"ORDER MISMATCH against the {what}")
        print(f"  firmware: {', '.join(fw_ids)}")
        print(f"  {what}: {', '.join(other_ids)}")
    by_id = dict(other)
    for anim_id, fw_params in fw:
        params = by_id.get(anim_id)
        if params is None:
            problems += 1
            print(f"{anim_id}: present in firmware, missing from the {what}")
        elif params != fw_params:
            problems += 1
            print(f"{anim_id}: parameter mismatch against the {what}")
            print(f"  firmware: {fw_params}")
            print(f"  {what}: {params}")
    return problems


def main():
    fw = firmware_tables()
    web = web_tables()
    problems = 0
    problems += compare(fw, page_tables(), "page")
    (cat_old, cat_new), (test_old, test_new), _ = gen_sim_mirror.render()
    if cat_old != cat_new or test_old != test_new:
        problems += 1
        print("simulator mirror stale: run python3 tools/animbench/gen_sim_mirror.py")

    fw_ids = [a for a, _ in fw]
    web_ids = [a for a, _ in web]
    if fw_ids != web_ids:
        problems += 1
        print("ORDER MISMATCH — the registry index is the persisted setting value,")
        print("so reordering silently repoints saved animation choices.")
        print(f"  firmware: {', '.join(fw_ids)}")
        print(f"  web:      {', '.join(web_ids)}")

    web_by_id = dict(web)
    for anim_id, fw_params in fw:
        web_params = web_by_id.get(anim_id)
        if web_params is None:
            problems += 1
            print(f"{anim_id}: present in firmware, missing from the web config")
        elif web_params != fw_params:
            problems += 1
            print(f"{anim_id}: parameter mismatch")
            print(f"  firmware: {fw_params}")
            print(f"  web:      {web_params}")

    if problems == 0:
        print(f"ok — {len(fw)} animations, parameter tables agree (firmware, web, page, simulator mirror)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
