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

HOW IT DECIDES WHAT WAS COMPARED
================================

An animation whose kernel is compiled out is worse than untested: band() is
then the portable code again, so the device compares a function against
itself and reports 0 differing pixels, as it must. That is noncoverage, not
parity, and telling the two apart is the whole of this tool's source
inventory.

It is not decided by a naming convention. Until gm-nov3.40 the inventory
scanned for literal `#define GM_BGANIM_<X>_ASM n` lines and matched each
animation to one by uppercasing its id, and neither of those is what the
compiler reads. A guard changed to `#if 0`, a guard moved to a differently
named macro, a band() body edited down to a call to the reference, or a -D on
the build line all left the old scan believing the kernel was live.

What it reads instead, per animation, is the condition at the guard:

  1. The `const BgAnimation bg_anim_<id> = {...}` initialiser says which
     function is band() and which is the reference. Both are fields, not
     names: Steam calls its reference bandPortable.
  2. The file is evaluated the way the preprocessor would evaluate it for the
     target build (below), so only the branch that really compiles is read.
  3. The kernel is dormant when band() is the reference by any of four
     routes: the two fields name the same function, band()'s body is one call
     to the reference, band()'s body is token for token the reference's body
     (Hills dispatches through render<true> and render<false>), or no inline
     assembly is reachable from band() at all through the file's own call
     graph (Steam dispatches through renderRow<Asm>).
  4. Which macros select the kernel is measured, not assumed: every macro the
     file's own conditions mention is forced on, forced to 0 and removed in
     turn, and the ones that change the classification are this animation's
     switches. Eleven animations (Aurora, Caustics, Ember, Fireflies,
     Mandala, Nebula, Orbits, Plasma, Ripples, Starfield, Steam) come back
     with nothing but the fleet-wide __XTENSA__ and GM_BGANIM_NO_ASM, so
     there is no build-time way to turn one of them off on its own for an
     A/B. /api/debug/anim?useref=1 is the only isolated A/B for those.

The target build is the production `display` environment: __XTENSA__ and
ESP_PLATFORM, plus every -D and -U in that environment's build_flags in
platformio.ini, with ${section.key} references expanded. `--pio-env` picks a
different one. Every run prints the environment, the overrides that reach a
kernel switch, and the blind spots below.

WHAT THIS CANNOT ESTABLISH
==========================

It compares band() against bandRef() as the running firmware compiled them.
It says nothing about a path the running firmware did not compile, and
nothing about a board running firmware built from another checkout.

Three ways a define can reach the compiler without appearing in
platformio.ini, all of which would make this inventory wrong. The run refuses
to start when it finds one rather than reporting a pass it cannot support:

  - PLATFORMIO_BUILD_FLAGS or PLATFORMIO_BUILD_UNFLAGS in the environment.
  - an extra_script of the target environment that touches CPPDEFINES or
    BUILD_FLAGS. None does today, and the check is run rather than asserted.
  - a build driven by hand with extra -D arguments, which leaves no trace
    anywhere the tool can read. That one it cannot detect, so it is printed
    on every run as a standing limit.

DORMANT below names every animation whose kernel is compiled out. The run
refuses to start if the sources disagree with it, and the summary counts
those animations separately and never as a pass. Nothing reachable from the
board can test a dormant kernel; the flag has to be turned on first, and then
the ladder in CLAUDE.md applies (host goldens, the real compiler's
disassembly, QEMU bit-exactness, and only then this tool).

WHAT --host-test DOES NOT COVER
===============================

The host cases drive check_fleet() and run_one() against FakeDevice, whose
get() is called directly, and main() through a stub urlopen. So they do cover
main()'s exit codes and the JSON decoding faults that stub can raise, and
they do not cover:

  - make_getter()'s retry and exhaustion loop against a real socket: its
    timeout path, its 1.5 s backoff, and a transport fault that recovers on
    the second try. Only the 404 and non-JSON branches are driven, through a
    substituted urlopen.
  - a real HTTP response: chunked transfer, a truncated body, a wrong
    content type, a body that is JSON but not an object arriving from the
    firmware rather than from a stub.
  - run_one()'s `pending` branch. FakeDevice publishes complete results and
    never answers pending true, so the poll that waits out a render-task
    window in progress is not exercised.
  - main()'s unavailable-pclk fallback. FakeDevice always answers
    /api/debug/pclk, so the ResultError path that prints "pclk: unavailable"
    and carries on is not reached.

A --host-test pass is a statement about the gate's logic, not about talking
to a board.
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
    "silk": "band() is one line that calls bandRef() in the build this "
            "inventory read. The kernel lost to the compiler on the chip "
            "(2026-09-04) and stays in AnimSilk.cpp behind a switch that "
            "defaults to 0, for the next attempt.",
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


def blank_comments(text):
    """C++ comments replaced by spaces, string literals and line count kept.

    strip_comments() above deletes comment text outright, which is right for
    the registry regex and wrong here: this scan matches `#` at the start of a
    line and brace-matches function bodies, so it needs the file's line
    structure and offsets left alone."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            quote = c
            out.append(c)
            i += 1
            while i < n:
                out.append(text[i])
                if text[i] == '\\' and i + 1 < n:
                    out.append(text[i + 1])
                    i += 2
                    continue
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            while i < n and text[i] != '\n':
                out.append(' ')
                i += 1
            continue
        if c == '/' and i + 1 < n and text[i + 1] == '*':
            out.append('  ')
            i += 2
            while i + 1 < n and not (text[i] == '*' and text[i + 1] == '/'):
                out.append('\n' if text[i] == '\n' else ' ')
                i += 1
            out.append('  ')
            i += 2
            continue
        out.append(c)
        i += 1
    return ''.join(out)


# ------------------------------------------------- the preprocessor's view

PP_TOKEN = re.compile(r'0[xX][0-9a-fA-F]+[uUlL]*|\d+[uUlL]*|[A-Za-z_]\w*'
                      r'|&&|\|\||<<|>>|[<>=!]=|[-+*/%&|^~!()<>,?:]')
IDENT = re.compile(r'[A-Za-z_]\w*')


def pp_tokens(expr):
    return PP_TOKEN.findall(expr)


def pp_names(expr):
    """Every macro name an #if condition mentions, `defined` itself aside."""
    return set(t for t in pp_tokens(expr) if IDENT.match(t) and t != 'defined')


class PPExpr:
    """The subset of the #if grammar these sources use.

    An undefined name is 0 and `defined(X)` is 1 or 0, as in the standard. A
    defined name with a value is its value re-evaluated as an expression, so
    `#define HARMO_STAMP_ASM 1` then `#if HARMO_STAMP_ASM` works. Function-like
    macros evaluate to 0 rather than being expanded: none of these conditions
    calls one, and guessing at one would be worse than the honest zero, which
    can only ever make a branch look inactive."""

    def __init__(self, tokens, macros, depth=0):
        self.t = tokens
        self.i = 0
        self.m = macros
        self.depth = depth

    def peek(self):
        return self.t[self.i] if self.i < len(self.t) else None

    def take(self):
        v = self.peek()
        self.i += 1
        return v

    def parse(self):
        return self.cond()

    def cond(self):
        v = self.binary(0)
        if self.peek() == '?':
            self.take()
            a = self.cond()
            if self.peek() == ':':
                self.take()
            b = self.cond()
            return a if v else b
        return v

    LEVELS = (('||',), ('&&',), ('|',), ('^',), ('&',), ('==', '!='),
              ('<', '>', '<=', '>='), ('<<', '>>'), ('+', '-'), ('*', '/', '%'))

    def binary(self, level):
        if level >= len(self.LEVELS):
            return self.unary()
        v = self.binary(level + 1)
        while self.peek() in self.LEVELS[level]:
            op = self.take()
            r = self.binary(level + 1)
            v = self.apply(op, v, r)
        return v

    @staticmethod
    def apply(op, a, b):
        if op == '||':
            return 1 if (a or b) else 0
        if op == '&&':
            return 1 if (a and b) else 0
        if op == '|':
            return a | b
        if op == '^':
            return a ^ b
        if op == '&':
            return a & b
        if op == '==':
            return int(a == b)
        if op == '!=':
            return int(a != b)
        if op == '<':
            return int(a < b)
        if op == '>':
            return int(a > b)
        if op == '<=':
            return int(a <= b)
        if op == '>=':
            return int(a >= b)
        if op == '<<':
            return a << min(b, 64) if b >= 0 else 0
        if op == '>>':
            return a >> min(b, 64) if b >= 0 else 0
        if op == '+':
            return a + b
        if op == '-':
            return a - b
        if op == '*':
            return a * b
        if b == 0:
            return 0
        q = int(a / b)
        return q if op == '/' else a - b * q

    def unary(self):
        p = self.peek()
        if p == '!':
            self.take()
            return 0 if self.unary() else 1
        if p == '~':
            self.take()
            return ~self.unary()
        if p == '-':
            self.take()
            return -self.unary()
        if p == '+':
            self.take()
            return self.unary()
        return self.primary()

    def primary(self):
        p = self.take()
        if p is None:
            return 0
        if p == '(':
            v = self.cond()
            if self.peek() == ')':
                self.take()
            return v
        if p == 'defined':
            name = self.take()
            if name == '(':
                name = self.take()
                if self.peek() == ')':
                    self.take()
            return 1 if name in self.m else 0
        if p[0].isdigit():
            return int(p.rstrip('uUlL'), 0)
        if p in self.m and self.depth < 16:
            value = self.m[p]
            if not value:
                return 0
            return PPExpr(pp_tokens(value), self.m, self.depth + 1).parse()
        return 0


def pp_eval(expr, macros):
    return PPExpr(pp_tokens(expr), macros).parse()


PP_DIRECTIVE = re.compile(r'^\s*#\s*(\w+)(.*)$')
PP_KEYWORDS = ('if', 'ifdef', 'ifndef', 'elif', 'else', 'endif', 'define', 'undef')

_BLANKED = {}


def _blanked(text):
    """blank_comments() memoised. It does not depend on the macros, and the
    inventory preprocesses each source about twenty times to find out which
    macros select its kernel."""
    key = hash(text)
    hit = _BLANKED.get(key)
    if hit is None or hit[0] != text:
        hit = (text, blank_comments(text))
        _BLANKED[key] = hit
    return hit[1]


def expand_macros(line, macros, hidden=(), depth=0):
    """Object-like macros substituted, string and character literals left alone.

    Only object-like macros with a replacement, and never one already being
    expanded, which is the standard's own rule against recursion. Crescent
    dispatches through `#define CR_OUTER crescentOuterAsm` inside band()'s own
    body (2026-09-13), so without this the call graph stops at CR_OUTER and the
    kernel looks unreachable."""
    if depth > 16 or not macros:
        return line
    if not any(w in macros for w in IDENT.findall(line)):
        return line
    out, i, n, changed = [], 0, len(line), False
    while i < n:
        c = line[i]
        if c == '"' or c == "'":
            quote = c
            out.append(c)
            i += 1
            while i < n:
                out.append(line[i])
                if line[i] == '\\' and i + 1 < n:
                    out.append(line[i + 1])
                    i += 2
                    continue
                if line[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if c.isalpha() or c == '_':
            j = i
            while j < n and (line[j].isalnum() or line[j] == '_'):
                j += 1
            word = line[i:j]
            value = macros.get(word)
            if value and word not in hidden:
                out.append(expand_macros(value, macros, tuple(hidden) + (word,), depth + 1))
                changed = True
            else:
                out.append(word)
            i = j
            continue
        out.append(c)
        i += 1
    text = ''.join(out)
    return expand_macros(text, macros, hidden, depth + 1) if changed else text


def preprocess(text, macros):
    """The file as the compiler would see it, minus macro expansion.

    Every line that a conditional excluded comes back empty, and every
    directive line comes back empty, so offsets and line numbers still line up
    with the original. #define and #undef are obeyed only inside a live
    branch, which is what makes the `#ifndef X / #define X 1 / #endif` idiom
    yield to a -D on the build line.

    Returns (text, macros after the file, names every condition mentioned)."""
    macros = dict(macros)
    raw = _blanked(text).split('\n')
    lines, pending = [], None
    for line in raw:
        if pending is not None:
            pending = pending.rstrip()[:-1] + ' ' + line
            lines.append('')
        else:
            pending = line
        if pending.rstrip().endswith('\\'):
            continue
        lines.append(pending)
        pending = None
    if pending is not None:
        lines.append(pending)

    out, stack, mentioned = [], [], set()
    for line in lines:
        active = all(frame[2] for frame in stack)
        m = PP_DIRECTIVE.match(line)
        if m and m.group(1) in PP_KEYWORDS:
            kw, rest = m.group(1), m.group(2).strip()
            if kw in ('if', 'ifdef', 'ifndef'):
                if kw == 'if':
                    expr = rest
                elif not rest.split():
                    expr = '0'
                elif kw == 'ifdef':
                    expr = 'defined(%s)' % rest.split()[0]
                else:
                    expr = '!defined(%s)' % rest.split()[0]
                mentioned |= pp_names(expr)
                taken = bool(pp_eval(expr, macros)) if active else False
                # [parent was live, some branch has been taken, this one is live]
                stack.append([active, taken, active and taken])
            elif kw == 'elif':
                if not stack:
                    raise InventoryError('#elif outside a conditional')
                frame = stack[-1]
                mentioned |= pp_names(rest)
                if frame[1] or not frame[0]:
                    frame[2] = False
                else:
                    taken = bool(pp_eval(rest, macros))
                    frame[1] = taken
                    frame[2] = taken
            elif kw == 'else':
                if not stack:
                    raise InventoryError('#else outside a conditional')
                frame = stack[-1]
                frame[2] = frame[0] and not frame[1]
                frame[1] = True
            elif kw == 'endif':
                if not stack:
                    raise InventoryError('#endif outside a conditional')
                stack.pop()
            elif kw == 'define' and active:
                d = re.match(r'(\w+)(\([^)]*\))?\s*(.*)$', rest)
                if d:
                    # None marks a function-like macro: defined, so defined()
                    # is 1 and #if reads it as 0, but never substituted, since
                    # expanding one needs its arguments.
                    macros[d.group(1)] = None if d.group(2) else d.group(3).strip()
            elif kw == 'undef' and active:
                if rest.split():
                    macros.pop(rest.split()[0], None)
            out.append('')
        else:
            out.append(expand_macros(line, macros) if active else '')
    if stack:
        raise InventoryError('a conditional is never closed')
    return '\n'.join(out), macros, mentioned


# -------------------------------------------------- what band() dispatches to

BGANIM_REG = re.compile(r'\bconst\s+BgAnimation\s+bg_anim_(\w+)\s*=\s*\{')
# The field order of BgAnimation (BgAnim.h): id, name, params, init, frame,
# band, release, bandRef. The last is optional and absent means nullptr.
FIELD_ID, FIELD_BAND, FIELD_REF = 0, 5, 7

NOT_A_CALL = {'if', 'for', 'while', 'switch', 'do', 'else', 'catch', 'return',
              'sizeof', 'static_cast', 'reinterpret_cast', 'const_cast',
              'dynamic_cast', 'decltype', 'alignas', 'alignof', 'noexcept',
              'throw', 'new', 'delete', 'and', 'or', 'not', 'constexpr'}
ASM_KEYWORD = re.compile(r'\b(?:asm|__asm|__asm__)\b')
BODY_TOKEN = re.compile(r'[A-Za-z_]\w*|\d+\.?\d*[a-zA-Z]*|\S')
TYPE_WORDS = {'int', 'char', 'void', 'float', 'double', 'unsigned', 'signed',
              'short', 'long', 'bool', 'const', 'restrict', '__restrict'}


def match_brace(text, i):
    """The index of the `}` that closes the `{` at i, strings skipped."""
    depth, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            quote = c
            i += 1
            while i < n:
                if text[i] == '\\':
                    i += 2
                    continue
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise InventoryError('a brace is never closed')


def split_fields(text):
    """A braced initialiser's top-level comma-separated fields."""
    out, depth, cur = [], 0, []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            quote = c
            cur.append(c)
            i += 1
            while i < n:
                cur.append(text[i])
                if text[i] == '\\' and i + 1 < n:
                    cur.append(text[i + 1])
                    i += 2
                    continue
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if c in '{([':
            depth += 1
        elif c in '})]':
            depth -= 1
        if c == ',' and depth == 0:
            out.append(''.join(cur).strip())
            cur = []
            i += 1
            continue
        cur.append(c)
        i += 1
    if ''.join(cur).strip():
        out.append(''.join(cur).strip())
    return out


def registrations(pptext):
    """{symbol suffix: [field, ...]} for every BgAnimation initialiser."""
    out = {}
    for m in BGANIM_REG.finditer(pptext):
        end = match_brace(pptext, m.end() - 1)
        out[m.group(1)] = split_fields(pptext[m.end():end])
    return out


def _back(text, i):
    while i >= 0 and text[i] in ' \t\n':
        i -= 1
    return i


def function_bodies(pptext):
    """{name: [(params, body), ...]} for every function defined in the text.

    Found by walking back from each `{` rather than by matching a return type,
    because the definitions here carry GM_ANIM_IRAM, template headers and
    trailing const in every combination. A `{` that is not preceded by an
    identifier and a parenthesised list, or whose identifier is a control
    keyword, is not a definition."""
    out = {}
    for m in re.finditer(r'\{', pptext):
        brace = m.start()
        i = _back(pptext, brace - 1)
        for word in ('noexcept', 'const', 'override', 'final'):
            while i >= len(word) - 1 and pptext[i - len(word) + 1:i + 1] == word:
                i = _back(pptext, i - len(word))
        if i < 0 or pptext[i] != ')':
            continue
        depth, open_at = 0, -1
        j = i
        while j >= 0:
            if pptext[j] == ')':
                depth += 1
            elif pptext[j] == '(':
                depth -= 1
                if depth == 0:
                    open_at = j
                    break
            j -= 1
        if open_at < 0:
            continue
        end_name = _back(pptext, open_at - 1)
        start_name = end_name
        while start_name >= 0 and (pptext[start_name].isalnum() or pptext[start_name] == '_'):
            start_name -= 1
        name = pptext[start_name + 1:end_name + 1]
        if not name or not IDENT.fullmatch(name) or name in NOT_A_CALL:
            continue
        if start_name >= 0 and pptext[start_name] in '.=':
            continue
        if start_name >= 1 and pptext[start_name - 1:start_name + 1] == '->':
            continue
        try:
            close = match_brace(pptext, brace)
        except InventoryError:
            continue
        out.setdefault(name, []).append((pptext[open_at + 1:i], pptext[brace + 1:close]))
    return out


def reaches_asm(bodies, start, seen=None):
    """Whether inline assembly is reachable from `start` in this translation
    unit, following calls by name. A kernel that the preprocessor removed took
    its asm with it, so this is what separates a real dispatch from one that
    landed back on the portable code through a template argument."""
    if seen is None:
        seen = set()
    if start in seen or start not in bodies:
        return False
    seen.add(start)
    for _params, body in bodies[start]:
        if ASM_KEYWORD.search(body):
            return True
        for token in set(IDENT.findall(body)):
            if token in bodies and token not in seen and reaches_asm(bodies, token, seen):
                return True
    return False


def param_names(params):
    """The declared name of each parameter, None where it has none."""
    out = []
    for p in params.split(','):
        m = re.search(r'([A-Za-z_]\w*)\s*$', p.strip())
        if m and m.group(1) not in TYPE_WORDS and not m.group(1).endswith('_t'):
            out.append(m.group(1))
        else:
            out.append(None)
    return out


def normalised_body(params, body):
    """The body's tokens with each parameter renamed to its position, so two
    functions that differ only in what they call their arguments compare
    equal."""
    renamed = {n: '@%d' % k for k, n in enumerate(param_names(params)) if n}
    return tuple(renamed.get(t, t) for t in BODY_TOKEN.findall(body))


LIVE = 'live'


def classify_kernel(band_sym, ref_sym, bodies):
    """LIVE, or the reason the device would be comparing the reference with
    itself. Four routes, because the dispatch takes four shapes across the
    fleet (see the module docstring)."""
    if ref_sym == 'nullptr' or not IDENT.fullmatch(ref_sym or ''):
        return "the registration's reference field is %s" % (ref_sym or 'absent')
    if band_sym == 'nullptr' or not IDENT.fullmatch(band_sym or ''):
        return "the registration's band field is %s" % (band_sym or 'absent')
    if band_sym == ref_sym:
        return 'band() and the reference are both %s()' % band_sym
    band = bodies.get(band_sym, [])
    ref = bodies.get(ref_sym, [])
    if len(band) != 1 or len(ref) != 1:
        raise InventoryError(
            'this build defines %s() %d times and %s() %d times, so which one '
            'band() dispatches to cannot be read from the source'
            % (band_sym, len(band), ref_sym, len(ref)))
    if re.match(r'^\s*' + re.escape(ref_sym) + r'\s*\([^;{}]*\)\s*;\s*$', band[0][1].strip()):
        return 'band() is one call to %s()' % ref_sym
    if normalised_body(*band[0]) == normalised_body(*ref[0]):
        return 'band() and %s() have the same body in this build' % ref_sym
    if not reaches_asm(bodies, band_sym):
        return 'no assembly is reachable from band() in this build'
    return LIVE


def kernel_macros(path, sym, state, mentioned, macros):
    """Which of the macros this file's conditions mention actually select the
    kernel, measured by forcing each one on, to 0 and away in turn.

    This is the answer to "what could turn this kernel off on its own", and it
    is measured rather than derived from the name, because the compiler does
    not read the name."""
    out = []
    for macro in sorted(mentioned):
        for probe in (None, '0', '1'):
            probed = dict(macros)
            if probe is None:
                probed.pop(macro, None)
            else:
                probed[macro] = probe
            if probed == macros:
                continue
            try:
                other, _mentioned = read_animations(path, probed)
            except InventoryError:
                out.append(macro)
                break
            if sym not in other:
                # The animation is not registered at all under that macro
                # (GAGGIMATE_SIM drops the whole file), which is a question
                # about whether the file compiles, not about the kernel.
                continue
            if other[sym][2] != state:
                out.append(macro)
                break
    return out


_ANIM_CACHE = {}


def read_animations(path, macros):
    """({symbol: (id, band symbol, kernel state)}, macros the conditions
    mention) for one animation source, read as the target build's
    preprocessor would see it."""
    text = read_source(path)
    key = (path, hash(text), tuple(sorted(macros.items())))
    hit = _ANIM_CACHE.get(key)
    if hit is not None:
        return hit
    pptext, _macros, mentioned = preprocess(text, macros)
    bodies = function_bodies(pptext)
    out = {}
    for sym, fields in registrations(pptext).items():
        ident = fields[FIELD_ID].strip('"') if fields else ''
        band = fields[FIELD_BAND] if len(fields) > FIELD_BAND else 'nullptr'
        ref = fields[FIELD_REF] if len(fields) > FIELD_REF else 'nullptr'
        out[sym] = (ident, band, classify_kernel(band, ref, bodies))
    _ANIM_CACHE[key] = (out, mentioned)
    return out, mentioned


def scan_animations(bganim_dir, macros):
    """{symbol: (id, file, kernel state, selecting macros)} for the directory."""
    found = {}
    for name in sorted(os.listdir(bganim_dir)):
        if not name.endswith('.cpp'):
            continue
        path = os.path.join(bganim_dir, name)
        anims, mentioned = read_animations(path, macros)
        for sym, (ident, _band, state) in anims.items():
            found[sym] = (ident, name, state,
                          kernel_macros(path, sym, state, mentioned, macros))
    return found


# ------------------------------------------------ what the build line says

# The macros the toolchain defines for the panel target. Everything else that
# can change a kernel comes off the build line and is read from platformio.ini.
TARGET_MACROS = {'__XTENSA__': '1', 'ESP_PLATFORM': '1'}

# Turning either of these off changes the whole fleet at once, so an animation
# whose only selecting macros are these has no isolated build-time A/B.
FLEET_MACROS = ('__XTENSA__', 'GM_BGANIM_NO_ASM')

PIO_SECTION = re.compile(r'^\[([^\]]+)\]\s*$')
PIO_KEY = re.compile(r'^([A-Za-z_][\w.\-]*)\s*=(.*)$')
PIO_REF = re.compile(r'\$\{([^}]*)\}')
PIO_DEFINE = re.compile(r'-D\s*([A-Za-z_]\w*)(?:=(\S+))?')
PIO_UNDEF = re.compile(r'-U\s*([A-Za-z_]\w*)')


def pio_read(path):
    """platformio.ini as {section: {key: [value line, ...]}}.

    Its own format, not configparser's: a continuation line is any indented
    line, and a line whose first non-blank character is `;` is a comment even
    inside a multi-line value, which is most of this file."""
    out, section, key = {}, None, None
    try:
        with open(path, encoding='utf-8') as f:
            lines = f.readlines()
    except OSError as exc:
        raise InventoryError('cannot read %s: %s' % (path, exc))
    for raw in lines:
        line = raw.rstrip('\n')
        stripped = line.strip()
        if not stripped or stripped[0] in ';#':
            continue
        m = PIO_SECTION.match(stripped)
        if m:
            section = m.group(1)
            out.setdefault(section, {})
            key = None
            continue
        if section is None:
            continue
        if line[:1] in (' ', '\t') and key is not None:
            out[section][key].append(stripped)
            continue
        m = PIO_KEY.match(stripped)
        if m:
            key = m.group(1)
            out[section][key] = [m.group(2).strip()] if m.group(2).strip() else []
    return out


def pio_value(ini, section, key, depth=0):
    """The value lines of [section] key, with ${a.b} expanded and `extends`
    followed. Returns (lines, references it could not expand)."""
    if depth > 12:
        raise InventoryError('%s.%s references itself' % (section, key))
    values, opaque = None, set()
    seen = set()
    while section is not None and section not in seen:
        seen.add(section)
        if key in ini.get(section, {}):
            values = ini[section][key]
            break
        parent = ini.get(section, {}).get('extends')
        section = parent[0].strip() if parent else None
    if values is None:
        return [], opaque

    def expand_in(line):
        def expand(m):
            ref = m.group(1)
            if '.' in ref:
                rsec, rkey = ref.rsplit('.', 1)
                if rsec not in ('platformio', 'sysenv', 'this'):
                    lines, more = pio_value(ini, rsec, rkey, depth + 1)
                    opaque.update(more)
                    return ' '.join(lines)
            # PlatformIO substitutes this one and this scan cannot. Whether
            # that matters depends on where it sits: inside a token the scan
            # has already read (`-DLV_CONF_PATH="${platformio.src_dir}/..."`)
            # it cannot introduce a flag, while a reference standing on its own
            # is a whole flag list of unknown content.
            alone = ((m.start() == 0 or line[m.start() - 1].isspace()) and
                     (m.end() >= len(line) or line[m.end()].isspace()))
            opaque.add((ref, alone))
            return ''
        return PIO_REF.sub(expand, line)

    return [expand_in(line) for line in values], opaque


def pio_defines(ini_path, env):
    """({macro: value}, [undefined macro], [(reference, stands alone)]) for one
    environment's build_flags, with [env]'s shared flags underneath."""
    ini = pio_read(ini_path)
    if ('env:' + env) not in ini:
        raise InventoryError('%s has no [env:%s]; --pio-env names the build '
                             'this inventory is for' % (ini_path, env))
    shared, opaque = pio_value(ini, 'env', 'build_flags')
    own, more = pio_value(ini, 'env:' + env, 'build_flags')
    opaque |= more
    defines, undefined = {}, []
    for line in shared + own:
        for m in PIO_DEFINE.finditer(line):
            defines[m.group(1)] = m.group(2) if m.group(2) is not None else ''
        for m in PIO_UNDEF.finditer(line):
            undefined.append(m.group(1))
            defines.pop(m.group(1), None)
    return defines, undefined, sorted(opaque)


def pio_hidden_defines(ini_path, env, root):
    """Reasons this inventory cannot see every -D that reaches the compiler.

    An extra_script runs as SCons and can append to CPPDEFINES or rewrite
    BUILD_FLAGS, which would not show up in build_flags at all. None of this
    project's does, and this runs the check rather than repeating the claim."""
    ini = pio_read(ini_path)
    scripts, _opaque = pio_value(ini, 'env:' + env, 'extra_scripts')
    reasons = []
    for line in scripts:
        for name in line.replace(',', ' ').split():
            name = name.strip()
            if name.startswith('pre:') or name.startswith('post:'):
                name = name.split(':', 1)[1]
            if not name.endswith('.py'):
                continue
            path = name if os.path.isabs(name) else os.path.join(root, name)
            try:
                text = read_source(path)
            except OSError:
                reasons.append('extra_script %s cannot be read, so whether it '
                               'adds a -D is unknown' % name)
                continue
            if 'CPPDEFINES' in text or 'BUILD_FLAGS' in text:
                reasons.append('extra_script %s touches CPPDEFINES or '
                               'BUILD_FLAGS, so the build line is not what '
                               'platformio.ini says' % name)
    return reasons


class BuildConfig:
    """The macro environment one firmware build compiles the animations with."""

    def __init__(self, env, macros, defines, undefined, opaque, hidden):
        self.env = env              # the platformio environment it came from
        self.macros = macros        # what the preprocessor is run with
        self.defines = defines      # the -D flags read out of platformio.ini
        self.undefined = undefined  # the -U flags
        self.opaque = opaque        # ${...} references left unexpanded
        self.hidden = hidden        # reasons a -D could be invisible here


def build_config(ini_path, env, root=ROOT, environ=None):
    """The target build's macros, and every reason they might be incomplete."""
    environ = os.environ if environ is None else environ
    defines, undefined, opaque = pio_defines(ini_path, env)
    hidden = pio_hidden_defines(ini_path, env, root)
    for ref, alone in opaque:
        if alone:
            hidden.append('build_flags carries ${%s} as a flag of its own, and '
                          'PlatformIO substitutes it, so what it adds to the '
                          'build line cannot be read here' % ref)
    for var in ('PLATFORMIO_BUILD_FLAGS', 'PLATFORMIO_BUILD_UNFLAGS'):
        if environ.get(var):
            hidden.append('%s is set in the environment, so the build line '
                          'carries flags platformio.ini does not: %r'
                          % (var, environ[var]))
    macros = dict(TARGET_MACROS)
    macros.update(defines)
    for name in undefined:
        macros.pop(name, None)
    return BuildConfig(env, macros, defines, undefined, opaque, hidden)


class Inventory:
    """The fleet as the firmware sources describe it, for one build."""

    def __init__(self, names, dormant, unswitched, switches, config, overrides):
        self.names = names
        self.dormant = dormant        # {id: why the kernel is not in the build}
        self.unswitched = unswitched  # ids with no isolated switch, so no A/B
        self.switches = switches      # {id: [macro that selects this kernel]}
        self.config = config          # the BuildConfig it was all read under
        self.overrides = overrides    # build-line defines that reach a switch


def build_inventory(bganim_dir=BGANIM_DIR, expect_names=None, expect_dormant=None,
                    ini_path=None, pio_env='display', environ=None, root=ROOT):
    """Reads the registry and each animation's real kernel guard, and checks
    both against what this tool was written for.

    Raises InventoryError, which aborts the run, when the sources have moved.
    That is the point: appending a registry entry, or letting a kernel go
    dormant, has to fail rather than shrink what a passing run covered."""
    expect_names = NAMES if expect_names is None else expect_names
    expect_dormant = DORMANT if expect_dormant is None else expect_dormant
    ini_path = os.path.join(root, 'platformio.ini') if ini_path is None else ini_path

    config = build_config(ini_path, pio_env, root=root, environ=environ)
    if config.hidden:
        # A define this scan cannot see could turn any kernel into its own
        # reference, and the run would then report a pass over a comparison
        # that never happened. There is nothing to fall back on, so stop.
        raise InventoryError(
            'the build line cannot be read from the sources, so which kernels '
            'are compiled in is unknown:\n  %s\n'
            'Clear it and run again, or say which build this is with --pio-env.'
            % '\n  '.join(config.hidden))

    syms = registry_symbols(bganim_dir)
    sources = scan_animations(bganim_dir, config.macros)
    names = []
    for sym in syms:
        if sym not in sources:
            raise InventoryError(
                'REGISTRY[] names bg_anim_%s and no source defines it in the '
                '[env:%s] build. Every animation file opens with #ifndef '
                'GAGGIMATE_SIM, so an environment that defines it compiles none '
                'of them, and /api/debug/animtest is absent from that firmware '
                'anyway: name a panel build with --pio-env.' % (sym, config.env))
        ident, src, _state, _macros = sources[sym]
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

    # Dormancy is the classification of the band() the target build compiles,
    # not a switch value and not a naming convention: see the module docstring.
    dormant, unswitched, switches = {}, [], {}
    for ident in names:
        _id, src, state, macros = sources[ident]
        isolated = [m for m in macros if m not in FLEET_MACROS]
        switches[ident] = macros
        if not isolated:
            unswitched.append(ident)
        if state is not LIVE:
            dormant[ident] = '%s (%s)' % (state, src)

    overrides = sorted((n, v) for n, v in config.defines.items()
                       if any(n in macros for macros in switches.values()))

    undeclared = sorted(set(dormant) - set(expect_dormant))
    if undeclared:
        raise InventoryError(
            'a kernel is compiled out of the shipped build and is not declared '
            'noncoverage: %s.\n  %s\n'
            'The device compares bandRef() against itself for these, so a run '
            'that counted them as passes would be claiming work it did not do. '
            'Add them to DORMANT with the reason, or turn the kernel back on.'
            % (', '.join(undeclared), '\n  '.join(dormant[d] for d in undeclared)))

    revived = sorted(set(expect_dormant) - set(dormant))
    if revived:
        raise InventoryError(
            'DORMANT declares %s noncoverage and the sources no longer agree. '
            'If the kernel is back in the build, drop the entry so the run '
            'counts the animation as compared.' % ', '.join(revived))

    return Inventory(names, dormant, unswitched, switches, config, overrides)


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


def run_one(get, anim, frames, deadline_s=120.0, sleep=None, now=None):
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
    # Resolved here, not in the signature: a default bound at import time is
    # still the real time.sleep after a caller has replaced time.sleep, which
    # is what made the end-to-end host cases wait 0.7 s per animation for a
    # scripted board that answers at once.
    sleep = time.sleep if sleep is None else sleep
    now = time.time if now is None else now
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
                sleep=None, now=None):
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
        print('no macro of their own selects the kernel, so no isolated '
              'build-time A/B (%d): %s'
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


class _Scratch:
    """A throwaway copy of everything build_inventory() reads.

    Built unmutated, so a mutation case can read the inventory before and
    after its own edit on one tree. A mutation that only ever reports the
    failure proves nothing: the tree it ran against might have been broken to
    begin with."""

    def __init__(self, root, bganim, ini):
        self.root = root
        self.bganim = bganim
        self.ini = ini

    def inventory(self, **kw):
        kw.setdefault('bganim_dir', self.bganim)
        kw.setdefault('ini_path', self.ini)
        kw.setdefault('root', self.root)
        kw.setdefault('environ', {})
        return build_inventory(**kw)

    def edit(self, name, old, new):
        """One text substitution in one copied source.

        The old text has to be there: a mutation that silently matched nothing
        would leave the case checking unmutated sources and passing."""
        path = os.path.join(self.bganim, name)
        text = read_source(path)
        if old not in text:
            raise InventoryError('the scratch edit of %s matched nothing: %r' % (name, old[:60]))
        with open(path, 'w', encoding='utf-8') as f:
            f.write(text.replace(old, new))

    def rewrite_band(self, name, body):
        """band()'s body replaced, brace-matched rather than pattern-matched.

        The mutation it serves is "band() calls only the reference with its
        switch still on", and an animation kernel is edited often enough that
        a literal anchor on its current body would rot into a mutation that
        matched nothing."""
        path = os.path.join(self.bganim, name)
        text = read_source(path)
        m = re.search(r'^(?:GM_ANIM_IRAM\s+)?void\s+band\s*\([^)]*\)\s*\{', text, re.M)
        if m is None:
            raise InventoryError('%s has no band() to rewrite' % path)
        close = match_brace(text, m.end() - 1)
        with open(path, 'w', encoding='utf-8') as f:
            f.write(text[:m.end()] + '\n    ' + body + '\n' + text[close:])

    def add_build_flags(self, flags):
        """Flags appended to [env:display]'s build_flags, which is what a -D
        override of a kernel switch would look like in the real file."""
        text = read_source(self.ini)
        marker = '[env:display]\n'
        if marker not in text:
            raise InventoryError('the scratch platformio.ini has no [env:display]')
        head, tail = text.split(marker, 1)
        key = 'build_flags =\n'
        if key not in tail:
            raise InventoryError('[env:display] has no build_flags to append to')
        before, after = tail.split(key, 1)
        with open(self.ini, 'w', encoding='utf-8') as f:
            f.write(head + marker + before + key + ''.join('\t%s\n' % x for x in flags) + after)

    def append_to_script(self, relpath, text):
        """Text appended to one copied extra_script, which is how a build line
        grows a -D that platformio.ini never mentions."""
        path = os.path.join(self.root, relpath)
        if not os.path.exists(path):
            raise InventoryError('the scratch tree has no %s' % relpath)
        with open(path, 'a', encoding='utf-8') as f:
            f.write(text)

    def append_registration(self, sym, ident):
        """One more entry in REGISTRY[], and a source that defines it."""
        reg = os.path.join(self.bganim, 'BgAnimRegistry.cpp')
        text = read_source(reg).replace('    &bg_anim_cube,\n',
                                        '    &bg_anim_cube,\n    &bg_anim_%s,\n' % sym)
        with open(reg, 'w', encoding='utf-8') as f:
            f.write(text)
        with open(os.path.join(self.bganim, 'AnimZZTest.cpp'), 'w', encoding='utf-8') as f:
            f.write('const BgAnimation bg_anim_%s = {\n    "%s",\n    "ZZ",\n};\n' % (sym, ident))


def _scratch_sources(tmp):
    """An untouched copy of the sources and the build file the inventory reads.

    Every mutation below is an edit to a file this checkout shares with other
    agents, and a file put back "for a moment" is a file another agent's
    pathspec can capture, so they are made on this copy instead. The scripts
    directory comes along because the inventory reads the target environment's
    extra_scripts to check none of them adds a -D."""
    import shutil
    root = os.path.join(tmp, 'tree')
    dst = os.path.join(root, 'bganim')
    os.makedirs(dst, exist_ok=True)
    os.makedirs(os.path.join(root, 'scripts'), exist_ok=True)
    for name in os.listdir(BGANIM_DIR):
        if name.endswith('.cpp') or name.endswith('.h'):
            shutil.copy2(os.path.join(BGANIM_DIR, name), os.path.join(dst, name))
    for name in os.listdir(os.path.join(ROOT, 'scripts')):
        if name.endswith('.py'):
            shutil.copy2(os.path.join(ROOT, 'scripts', name),
                         os.path.join(root, 'scripts', name))
    ini = os.path.join(root, 'platformio.ini')
    shutil.copy2(os.path.join(ROOT, 'platformio.ini'), ini)
    return _Scratch(root, dst, ini)


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

    def expect_live(name, scratch):
        """The inventory of an unmutated scratch tree, with Tide's kernel in
        the build. Every guard mutation below runs this on its own tree first,
        so its abort is the mutation's doing and not a broken copy's."""
        try:
            got = scratch.inventory()
        except InventoryError as exc:
            problems.append('%s: the tree does not pass before the mutation: %s' % (name, exc))
            return None
        if 'tide' in got.dormant:
            problems.append('%s: tide is dormant before the mutation' % name)
            return None
        named = [m for m in got.switches.get('tide', []) if m not in FLEET_MACROS]
        print('host test, %s: before, tide compares a kernel, selected by %s'
              % (name, ', '.join(named) or 'nothing of its own'), file=out)
        return got

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
        grown = _scratch_sources(tmp)
        grown.append_registration('zztest', 'zztest')
        expect_abort('mutation 1, an animation appended to the registry',
                     grown.inventory, 'the registry has moved under this tool')
        # And the count really did grow, so the abort is about the right thing.
        syms = registry_symbols(grown.bganim)
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
        dulled = _scratch_sources(tmp)
        expect_live('mutation 4b, a second kernel compiled out of the build', dulled)
        dulled.edit('AnimTide.cpp', '#define GM_BGANIM_TIDE_ASM 1',
                    '#define GM_BGANIM_TIDE_ASM 0')
        expect_abort('mutation 4b, a second kernel compiled out of the build',
                     dulled.inventory, 'is not declared')
        # And a declaration that no longer matches the sources is refused too,
        # so DORMANT cannot rot into a blanket excuse.
        expect_abort('a stale noncoverage declaration',
                     lambda: build_inventory(expect_dormant={'silk': 'x', 'cube': 'y'}),
                     'the sources no longer agree')

    # 4c. The four ways a kernel leaves the build that the switch scan this
    #     replaced could not see (gm-nov3.40). Each is built in a scratch copy
    #     of Tide, whose conventional `#define GM_BGANIM_TIDE_ASM 1` is left
    #     enabled in all four, and each is read once before its own edit, so
    #     the abort is the mutation's and not the tree's.
    TIDE_GUARD = '#if GM_BGANIM_TIDE_ASM && defined(__XTENSA__) && !defined(GM_BGANIM_NO_ASM)'

    with tempfile.TemporaryDirectory() as tmp:
        name = 'mutation 5a, a kernel guard changed to #if 0 with its conventional ' \
               'define left enabled'
        blanked = _scratch_sources(tmp)
        expect_live(name, blanked)
        blanked.edit('AnimTide.cpp', TIDE_GUARD, '#if 0')
        expect_abort(name, blanked.inventory, 'is not declared')

    with tempfile.TemporaryDirectory() as tmp:
        name = 'mutation 5b, a kernel guard moved to a macro nothing defines'
        renamed = _scratch_sources(tmp)
        expect_live(name, renamed)
        renamed.edit('AnimTide.cpp', TIDE_GUARD,
                     TIDE_GUARD.replace('GM_BGANIM_TIDE_ASM', 'GM_BGANIM_TIDE_KERNEL'))
        expect_abort(name, renamed.inventory, 'is not declared')

    with tempfile.TemporaryDirectory() as tmp:
        name = 'mutation 5c, band() rewritten to one call to bandRef() with its switch still on'
        hollow = _scratch_sources(tmp)
        expect_live(name, hollow)
        hollow.rewrite_band('AnimTide.cpp', 'bandRef(dst, y0, rows, w, tMs, p);')
        expect_abort(name, hollow.inventory, 'is not declared')

    with tempfile.TemporaryDirectory() as tmp:
        name = 'mutation 5d, -DGM_BGANIM_TIDE_ASM=0 added to platformio.ini'
        overridden = _scratch_sources(tmp)
        before = expect_live(name, overridden)
        if before is not None and before.overrides:
            problems.append('mutation 5d: the unmutated tree already reports build-line '
                            'overrides: %r' % (before.overrides,))
        overridden.add_build_flags(['-DGM_BGANIM_TIDE_ASM=0'])
        expect_abort(name, overridden.inventory, 'is not declared')

    with tempfile.TemporaryDirectory() as tmp:
        # A build_flags line that is nothing but a reference PlatformIO
        # expands. Its content is unknown here and could be any -D, so the run
        # stops. The one the real file carries is inside a -D already read
        # (-DLV_CONF_PATH="${platformio.src_dir}/...") and does not stop it,
        # which is what every passing case above shows.
        name = 'mutation 5h, a build_flags line that is only ${sysenv.EXTRA_FLAGS}'
        vague = _scratch_sources(tmp)
        expect_live(name, vague)
        vague.add_build_flags(['${sysenv.EXTRA_FLAGS}'])
        expect_abort(name, vague.inventory, 'as a flag of its own')

    with tempfile.TemporaryDirectory() as tmp:
        # An extra_script that appends to CPPDEFINES. None of this project's
        # does, and that is checked on every run rather than asserted here.
        name = 'mutation 5i, an extra_script that appends to CPPDEFINES'
        scripted = _scratch_sources(tmp)
        expect_live(name, scripted)
        scripted.append_to_script('scripts/pioarduino_env.py',
                                  '\nenv.Append(CPPDEFINES=["GM_BGANIM_TIDE_ASM=0"])\n')
        expect_abort(name, scripted.inventory, 'touches CPPDEFINES')

    # 4d. A switch named against the convention. The scan this replaced matched
    #     GM_BGANIM_<ID>_ASM to the animation by uppercasing its id, so a switch
    #     under any other name was invisible: on, the animation looked
    #     unswitched, and off, the run reported a pass over a comparison of
    #     bandRef with itself. Neither happens now, because nothing reads the
    #     name.
    with tempfile.TemporaryDirectory() as tmp:
        name = 'mutation 5e, a switch named against the convention'
        odd = _scratch_sources(tmp)
        base = expect_live(name, odd)
        odd.edit('AnimTide.cpp', 'GM_BGANIM_TIDE_ASM', 'TIDE_WANTS_THE_KERNEL')
        after = expect_live(name + ', still on', odd)
        if base is not None and after is not None:
            named = [m for m in after.switches.get('tide', []) if m not in FLEET_MACROS]
            if named != ['TIDE_WANTS_THE_KERNEL']:
                problems.append('%s: the scan names tide\'s switches %r, not the one '
                                'the file uses' % (name, named))
            elif 'tide' in after.unswitched:
                problems.append('%s: tide is reported as having no switch' % name)
            elif set(after.dormant) != set(base.dormant):
                problems.append('%s: renaming the switch changed the dormant set: %r'
                                % (name, sorted(after.dormant)))
            else:
                print('host test, %s: found anyway, and the run passes' % name, file=out)
        odd.edit('AnimTide.cpp', '#define TIDE_WANTS_THE_KERNEL 1',
                 '#define TIDE_WANTS_THE_KERNEL 0')
        expect_abort('mutation 5f, a switch named against the convention, turned off',
                     odd.inventory, 'is not declared')

    # 4e. A -D that reaches the compiler from outside platformio.ini. The tool
    #     cannot evaluate it, so it refuses rather than reporting an inventory
    #     it cannot support.
    expect_abort('mutation 5g, PLATFORMIO_BUILD_FLAGS set in the environment',
                 lambda: build_inventory(environ={'PLATFORMIO_BUILD_FLAGS': '-DGM_BGANIM_TIDE_ASM=0'}),
                 'the build line cannot be read from the sources')

    # 4f. And the standing limits are printed, not left in a comment.
    banner = io.StringIO()
    print_build(inv, out=banner)
    for want in ('[env:display]', 'not visible from any source'):
        if want not in banner.getvalue():
            problems.append('the build banner does not say %r: %r' % (want, banner.getvalue()))

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

def print_build(inv, out=None):
    """What build this inventory was read for, and what it could not see.

    Printed on every run rather than written down in a comment, because the
    answer moves with platformio.ini and with the environment the run was
    started in."""
    out = sys.stdout if out is None else out
    print('build: [env:%s] of platformio.ini, __XTENSA__ and ESP_PLATFORM plus '
          'its build_flags' % inv.config.env, file=out, flush=True)
    if inv.overrides:
        print('  build-line overrides that select a kernel: %s'
              % ', '.join('%s=%s' % (n, v or '(empty)') for n, v in inv.overrides),
              file=out, flush=True)
    else:
        print('  no -D in that environment names a macro that selects a kernel',
              file=out, flush=True)
    if inv.config.undefined:
        print('  -U: %s' % ', '.join(inv.config.undefined), file=out, flush=True)
    if inv.config.opaque:
        print('  references PlatformIO expands and this scan does not, each '
              'inside a flag already read, so none of them can add a -D: %s'
              % ', '.join('${%s}' % ref for ref, _alone in sorted(inv.config.opaque)),
              file=out, flush=True)
    print('  not visible from any source: a build driven by hand with extra -D '
          'arguments. PLATFORMIO_BUILD_FLAGS and an extra_script that touches '
          'CPPDEFINES are checked and would have stopped the run.',
          file=out, flush=True)


def main(argv=None):
    ap = argparse.ArgumentParser(description='band() against bandRef() for every animation')
    ap.add_argument('ids', nargs='*', type=int, help='animation ids (default: the whole registry)')
    ap.add_argument('--host', default=os.environ.get('HOST', '192.168.1.121'))
    ap.add_argument('--frames', type=int, default=int(os.environ.get('FRAMES', '8')))
    ap.add_argument('--pio-env', default=os.environ.get('PIOENV', 'display'),
                    help='the platformio environment whose build_flags decide which '
                         'kernels are compiled in (default: display)')
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
        inv = build_inventory(pio_env=args.pio_env)
    except InventoryError as exc:
        print('ABORT: %s' % exc, file=sys.stderr)
        return 2

    get = make_getter(args.host)
    try:
        print('pclk: %s' % json.dumps(get('/api/debug/pclk')), flush=True)
    except ResultError as exc:
        print('pclk: unavailable (%s)' % exc, flush=True)
    print_build(inv)
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
