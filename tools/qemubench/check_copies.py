#!/usr/bin/env python3
"""Diff a tools/qemubench test's hand-copied asm kernel against the real
`asm volatile`/`__asm__ volatile` block it claims to transcribe, in src/ (or,
for blend_row, in tools/animbench/kernels-blend/).

Why this exists: every test under tools/qemubench/tests/anim_* says, in its
own header comment, that its kernel body is "transcribed verbatim" from a
named function in src/display/ui/default/bganim/Anim<X>.cpp, because the
freestanding QEMU harness has no libc/ESP-IDF to link the real function
against. That comment is a promise nothing used to check: if the kernel in
src/ changed (a fixed instruction, a new clamp, a reordered store) and the
test's hand copy did not, the test kept passing against its own stale copy,
proving nothing about the file that actually ships. This script is the
check: extract the asm block from both sides, normalise away comments and
incidental whitespace (nothing else), and diff.

What "normalise" means here, precisely:
  - `asm` and `__asm__` are the same GCC keyword; spelled either way.
  - `//` line comments and `/* */` block comments are stripped. Every
    comment on either side is about THAT file's copy, not about the
    instructions, and the two sides are free to explain the same
    instruction differently (a source file's `// fieldRow[x+2]` and a
    test's `/* fieldRow[x+2] */` are not a divergence worth reporting).
    This assumes no asm string literal in this codebase contains a
    `//` or `/*` sequence as data -- true today (checked by hand) and
    worth re-checking if a kernel's dither/format strings ever change
    shape.
  - Runs of whitespace (including newlines) collapse to single spaces,
    and leading/trailing whitespace per line is trimmed. The assembler
    does not care about column alignment; a diff should not either.
  - Everything else -- mnemonics, operand names, immediates, the
    output/input/clobber lists, their order -- must match exactly.

What this does NOT check: that the toolchain compiling the *_pie_kernel.cpp
included into blend_group8 hasn't drifted (it's #included directly, so it
can't drift from itself), or that the transcribed copy still executes
correctly (run.sh under QEMU proves that, this script only proves the two
texts are the same instructions).

Usage:
    check_copies.py                    # check every mapped test, exit 1 on any mismatch
    check_copies.py anim_lava           # check just one test
    check_copies.py --repo-root DIR ... # point at a different checkout (for
                                         # reproducing a failure against a
                                         # scratch copy with one source line
                                         # changed, without touching the real
                                         # checkout)
    check_copies.py --tests-root DIR    # point at a different tests/ dir
                                         # directly (same purpose)

Exit status: 0 if every named test's kernel copy matches its source, or the
test is explicitly declared to have no source correspondence to check.
1 if any copy has diverged, or if a test directory exists that this script
has not classified into MAPPING or NO_CORRESPONDENCE -- an unclassified
test is refused rather than silently skipped, so a new test must be
triaged into one bucket or the other before it can build.
"""
import argparse
import difflib
import re
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_REPO_ROOT = SCRIPT_DIR.parents[1]

# test directory name -> list of (source file, source function name, test
# function name). Test function name is None when it is spelled the same
# as the source function (the common case); a few tests rename the
# transcribed copy (e.g. anim_fireflies calls its copy of AnimFireflies.cpp's
# fillRowPie "fillRowPieAsm", to keep it distinct from the test's own
# fillRowRef/fillRowPie-adjacent helpers).
#
# Building this table is itself the main way a stale mapping gets caught:
# every entry below was checked by hand against both files (function
# signatures, "verbatim transcription" comments) when this script was
# written (gm-bzu.35, 2026-09-30). A function with two definitions under the
# same name (a device asm body plus a portable C++ twin used off-Xtensa,
# e.g. AnimLava's and AnimSilk's *Asm functions, AnimSilk2's
# silk2PairRowAsm) is handled by picking whichever definition's body
# actually contains an asm block -- see find_function_asm_body() below --
# not by the mapping table.
MAPPING = {
    "anim_aurora": [
        ("src/display/ui/default/bganim/AnimAurora.cpp", "auroraPixelsAsm", None),
    ],
    "anim_caustics": [
        ("src/display/ui/default/bganim/AnimCaustics.cpp", "causticsRowKernel", None),
    ],
    "anim_ember": [
        # emberFlickerFieldRow (also asm in AnimEmber.cpp) is not
        # transcribed by this test and is intentionally left unchecked.
        ("src/display/ui/default/bganim/AnimEmber.cpp", "emberIdxRowPie", None),
        ("src/display/ui/default/bganim/AnimEmber.cpp", "emberFinalizeRow", None),
    ],
    "anim_fireflies": [
        ("src/display/ui/default/bganim/AnimFireflies.cpp", "fillRowPie", "fillRowPieAsm"),
    ],
    "anim_lava": [
        ("src/display/ui/default/bganim/AnimLava.cpp", "lavaFinalizeQuadAsm", None),
        ("src/display/ui/default/bganim/AnimLava.cpp", "lavaFieldGatherAsm", None),
    ],
    "anim_mandala": [
        ("src/display/ui/default/bganim/AnimMandala.cpp", "interpPairKernel", None),
    ],
    "anim_nebula": [
        # lerpShiftRowPie (also asm in AnimNebula.cpp) is not transcribed by
        # this test and is intentionally left unchecked.
        ("src/display/ui/default/bganim/AnimNebula.cpp", "nebulaFieldPie", None),
        ("src/display/ui/default/bganim/AnimNebula.cpp", "nebulaGatherScalar", None),
    ],
    "anim_orbits": [
        ("src/display/ui/default/bganim/AnimOrbits.cpp", "fillBgPie", None),
    ],
    "anim_plasma": [
        ("src/display/ui/default/bganim/AnimPlasma.cpp", "plasmaRowAsm", None),
    ],
    "anim_ripples": [
        ("src/display/ui/default/bganim/AnimRipples.cpp", "fillTileSpanPie", None),
        ("src/display/ui/default/bganim/AnimRipples.cpp", "accumulateBandAsm", None),
    ],
    "anim_silk": [
        ("src/display/ui/default/bganim/AnimSilk.cpp", "silkFastCell16Asm", None),
        ("src/display/ui/default/bganim/AnimSilk.cpp", "silkExactCell16Asm", None),
    ],
    "anim_silk2": [
        ("src/display/ui/default/bganim/AnimSilk2.cpp", "silk2PairRowAsm", None),
    ],
    "anim_starfield": [
        ("src/display/ui/default/bganim/AnimStarfield.cpp", "starfieldVigRowAsm", None),
    ],
    "anim_steam": [
        ("src/display/ui/default/bganim/AnimSteam.cpp", "fillRowPie", None),
    ],
    "blend_row": [
        # Transcribed from blend-asm-lead's kernel, not from src/display/.
        ("tools/animbench/kernels-blend/blend_pie_kernel.cpp", "blendGroup8General", None),
    ],
}

# Tests with no src/ (or tools/animbench/) original to diff against, and
# why each one is exempt rather than merely unclassified:
#   - pie_smoke, pie_smoke_noabi, pie_smoke_full, probe_vadds_s8: standalone
#     PIE/QEMU capability probes (the first two predate the harness and
#     build standalone, see build.sh's own header comment). None of them
#     claims to transcribe a kernel from anywhere; there is nothing to diff.
#   - blend_group8: #includes tools/animbench/kernels-blend/blend_pie_kernel.cpp
#     directly rather than copying it by hand, so it is the same text as
#     its source by construction and cannot drift from it.
NO_CORRESPONDENCE = {"pie_smoke", "pie_smoke_noabi", "pie_smoke_full", "probe_vadds_s8", "blend_group8"}


def find_matching(text: str, open_pos: int, open_ch: str, close_ch: str) -> int:
    """Return the index of the close_ch matching the open_ch at open_pos,
    skipping over string/char literals and // and /* */ comments so a
    brace or paren inside one of those is not miscounted."""
    depth = 0
    i = open_pos
    n = len(text)
    in_string = in_char = in_line_comment = in_block_comment = False
    while i < n:
        c = text[i]
        if in_line_comment:
            if c == "\n":
                in_line_comment = False
            i += 1
            continue
        if in_block_comment:
            if c == "*" and text[i + 1 : i + 2] == "/":
                in_block_comment = False
                i += 2
                continue
            i += 1
            continue
        if in_string:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                in_string = False
            i += 1
            continue
        if in_char:
            if c == "\\":
                i += 2
                continue
            if c == "'":
                in_char = False
            i += 1
            continue
        if c == "/" and text[i + 1 : i + 2] == "/":
            in_line_comment = True
            i += 2
            continue
        if c == "/" and text[i + 1 : i + 2] == "*":
            in_block_comment = True
            i += 2
            continue
        if c == '"':
            in_string = True
            i += 1
            continue
        if c == "'":
            in_char = True
            i += 1
            continue
        if c == open_ch:
            depth += 1
        elif c == close_ch:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise ValueError(f"no matching {close_ch!r} for {open_ch!r} at offset {open_pos}")


_ASM_RE = re.compile(r"\b(?:__asm__|asm)\b\s*(?:volatile\b\s*)?\(")


def extract_asm_blocks(body: str) -> list[str]:
    """Return the full text (keyword through matching close paren) of every
    asm volatile(...)/__asm__ volatile(...) call found directly in body."""
    blocks = []
    for m in _ASM_RE.finditer(body):
        open_paren = m.end() - 1
        close_paren = find_matching(body, open_paren, "(", ")")
        blocks.append(body[m.start() : close_paren + 1])
    return blocks


def _skip_definition_gap(text: str, pos: int) -> int:
    """From just after a function's parameter-list close paren, skip past
    const/noexcept/override/whitespace and return the position of the next
    significant character (expected to be '{' for a definition or ';' for
    a declaration/call)."""
    pattern = re.compile(r"\s*(?:const\b|noexcept\b(?:\([^)]*\))?|override\b|final\b)*\s*")
    m = pattern.match(text, pos)
    return m.end()


def find_function_asm_body(source: str, func_name: str, source_label: str) -> str:
    """Find func_name's definition(s) in source and return the text of the
    one whose body contains an asm block. Raises with a clear message if
    there is no definition, or no definition contains an asm block, or
    more than one does (ambiguous -- the mapping table or the source
    changed shape in a way this script no longer understands)."""
    name_re = re.compile(r"\b" + re.escape(func_name) + r"\s*\(")
    candidates = []
    for m in name_re.finditer(source):
        paren_open = m.end() - 1
        try:
            paren_close = find_matching(source, paren_open, "(", ")")
        except ValueError:
            continue
        gap_end = _skip_definition_gap(source, paren_close + 1)
        if gap_end < len(source) and source[gap_end] == "{":
            try:
                body_end = find_matching(source, gap_end, "{", "}")
            except ValueError:
                continue
            candidates.append(source[gap_end : body_end + 1])

    if not candidates:
        raise LookupError(f"no definition of {func_name}() found in {source_label}")

    with_asm = [c for c in candidates if _ASM_RE.search(c)]
    if not with_asm:
        raise LookupError(
            f"found {len(candidates)} definition(s) of {func_name}() in {source_label}, "
            "but none contain an asm volatile/__asm__ volatile block"
        )
    if len(with_asm) > 1:
        raise LookupError(
            f"found {len(with_asm)} definitions of {func_name}() in {source_label} that each "
            "contain an asm block -- ambiguous, update check_copies.py to disambiguate"
        )
    return with_asm[0]


_STRING_LITERAL_RE = re.compile(r'"(?:[^"\\]|\\.)*"')


def normalize(asm_text: str) -> list[str]:
    """Strip comments, fold asm/__asm__ to one spelling, then split into
    diff-friendly lines: each asm template string (a quoted literal whose
    content contains a `\\n` escape -- in this codebase's convention, one
    instruction per literal) becomes its own line, and everything between
    template strings (the `asm volatile (`, the commas and casts and `"+r"`
    / `"=&r"` constraint strings of the operand lists, the closing `)`)
    collapses into whitespace-normalised lines of its own.

    This is deliberately NOT a plain split on the source file's physical
    newlines: this codebase sometimes writes `asm volatile(\\n"...` and
    sometimes `asm volatile("...` on the same line, and wraps a long
    operand list across one physical line or several, purely as
    prettifying -- none of that is a difference in the instructions, so a
    line-for-line diff must not be sensitive to it. Splitting on the
    template strings instead means only the file's choice of comment
    wording and physical wrapping is ignored; a real instruction, operand,
    immediate or clobber difference still lands on its own diff line.
    """
    text = re.sub(r"/\*.*?\*/", "", asm_text, flags=re.S)
    text = re.sub(r"\b__asm__\b", "asm", text)
    text = "\n".join(re.sub(r"//.*$", "", raw_line) for raw_line in text.split("\n"))

    lines = []
    buf = ""
    pos = 0
    for m in _STRING_LITERAL_RE.finditer(text):
        gap = text[pos : m.start()]
        literal = m.group(0)
        if "\\n" in literal:
            buf += gap
            gap_norm = " ".join(buf.split())
            if gap_norm:
                lines.append(gap_norm)
            lines.append(" ".join(literal.split()))
            buf = ""
        else:
            # A short constraint string ("+r", "=&r", "r", "memory", ...):
            # keep it with the surrounding operand-list code rather than
            # giving it its own line.
            buf += gap + literal
        pos = m.end()
    buf += text[pos:]
    tail_norm = " ".join(buf.split())
    if tail_norm:
        lines.append(tail_norm)
    return lines


def check_one(repo_root: Path, test_name: str, mapping_entries) -> list[str]:
    """Return a list of failure-message blocks (each already containing its
    own diff) for one test; empty list means every mapped function matched."""
    tests_dir_candidates = [repo_root / "tools/qemubench/tests" / test_name]
    test_dir = None
    for cand in tests_dir_candidates:
        if cand.is_dir():
            test_dir = cand
            break
    if test_dir is None:
        return [f"{test_name}: no such test directory under tools/qemubench/tests/"]

    test_main = None
    for name in ("main.cpp", "main.c"):
        p = test_dir / name
        if p.exists():
            test_main = p
            break
    if test_main is None:
        return [f"{test_name}: no main.c/main.cpp in {test_dir}"]

    test_source = test_main.read_text()

    failures = []
    src_cache: dict[str, str] = {}
    for src_relpath, src_func, test_func in mapping_entries:
        test_func = test_func or src_func
        src_path = repo_root / src_relpath
        if src_relpath not in src_cache:
            if not src_path.is_file():
                failures.append(f"{test_name}: mapped source file does not exist: {src_relpath}")
                src_cache[src_relpath] = ""
                continue
            src_cache[src_relpath] = src_path.read_text()
        src_text = src_cache[src_relpath]
        if not src_text:
            continue

        try:
            src_body = find_function_asm_body(src_text, src_func, str(src_path))
        except LookupError as e:
            failures.append(f"{test_name}: {e}")
            continue
        try:
            test_body = find_function_asm_body(test_source, test_func, str(test_main))
        except LookupError as e:
            failures.append(f"{test_name}: {e}")
            continue

        src_blocks = extract_asm_blocks(src_body)
        test_blocks = extract_asm_blocks(test_body)
        if len(src_blocks) != len(test_blocks):
            failures.append(
                f"{test_name}: {src_func}() has {len(src_blocks)} asm block(s) in {src_relpath} "
                f"but {test_func}() has {len(test_blocks)} in {test_main.relative_to(repo_root)}"
            )
            continue

        for idx, (src_block, test_block) in enumerate(zip(src_blocks, test_blocks)):
            src_lines = normalize(src_block)
            test_lines = normalize(test_block)
            if src_lines != test_lines:
                suffix = f" (block {idx})" if len(src_blocks) > 1 else ""
                diff = "\n".join(
                    difflib.unified_diff(
                        src_lines,
                        test_lines,
                        fromfile=f"{src_relpath}:{src_func}{suffix}",
                        tofile=f"{test_main.relative_to(repo_root)}:{test_func}{suffix}",
                        lineterm="",
                    )
                )
                failures.append(
                    f"{test_name}: {test_func}() in {test_main.relative_to(repo_root)} has drifted "
                    f"from {src_func}() in {src_relpath}{suffix}:\n{diff}"
                )

    return failures


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo-root", default=str(DEFAULT_REPO_ROOT), help="checkout root (default: this repo)")
    ap.add_argument(
        "tests",
        nargs="*",
        help="test directory name(s) to check (default: every mapped test)",
    )
    args = ap.parse_args()
    repo_root = Path(args.repo_root).resolve()

    names = args.tests or sorted(MAPPING.keys())

    any_failure = False
    checked = 0
    for name in names:
        if name in NO_CORRESPONDENCE:
            print(f"check_copies: {name}: no source correspondence to check (declared exempt)")
            continue
        if name not in MAPPING:
            print(
                f"check_copies: {name}: not classified in check_copies.py's MAPPING or "
                "NO_CORRESPONDENCE -- refusing (add it to one or the other)",
                file=sys.stderr,
            )
            any_failure = True
            continue
        failures = check_one(repo_root, name, MAPPING[name])
        checked += 1
        if failures:
            any_failure = True
            for f in failures:
                print(f, file=sys.stderr)
        else:
            fn_count = len(MAPPING[name])
            print(f"check_copies: {name}: OK ({fn_count} function(s) match src/ verbatim)")

    if any_failure:
        print("check_copies: FAIL -- a kernel copy has drifted from its source (see above)", file=sys.stderr)
        return 1
    print(f"check_copies: PASS -- {checked} test(s) verified against source")
    return 0


if __name__ == "__main__":
    sys.exit(main())
