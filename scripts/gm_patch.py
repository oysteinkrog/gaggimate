"""Shared machinery for the scripts/patch_*.py pre-build patches.

Why this exists
---------------
The patch scripts used to decide "already patched" by finding a marker string
in the target. A file patched by an older version of a script carries the same
marker, so it stayed old: the production lv_meter.c on the bench machine had
the scale-lines hunks but not the element guard added later, and nothing said
so. This module replaces the marker test with a derivation:

1. Find a pristine baseline. The target itself, if its sha256 is one of the
   script's known pristine hashes; otherwise the ``.gm-orig`` copy beside it,
   if that hash is known. Anything else fails the build: a ``.gm-orig`` can
   itself be patched (it was once written from whatever the file held), and a
   baseline nobody verified is how a stale patch survives.
2. Apply every patch set the script owns to that baseline, in order, each
   anchor matched exactly as often as it declares. An empty hunk list is an
   error, not a success.
3. Prepend one stamp line per patch set:
   ``/* gm-patch-v<N> <NAME> <digest> by scripts/<owner>.py ... */``. The
   digest is taken over the hunks themselves, so a hunk edited without a
   version bump still changes the stamp.
4. Compare with the target. Equal: log "already vN". Different: write the
   derived text atomically and log "applied vN".

Because the result is always derived from a verified pristine file, a fresh
install and an old patched tree end byte-identical.

Shared targets
--------------
Restoring a baseline erases every hunk in the file, including another
script's. So a target that carries a stamp from a different owner is refused
with an error rather than rewritten. None of today's targets is shared; a
script that needs to patch one another script already patches must first move
both patch sets into one owner.

Concurrency
-----------
The IDF package is shared by every env and by the Windows-side install, so two
builds can patch the same file at once. Each target is patched under a
directory lock (``<target>.gm-lock``, created with mkdir, which is atomic on
every filesystem this runs on). A lock older than LOCK_STALE_S is taken to be
left by a killed build and is broken.
"""

import hashlib
import os
import re
import shutil
import sys
import time

STAMP_TAG = "gm-patch-v"
STAMP_RE = re.compile(r"gm-patch-v(\d+) (\S+) ([0-9a-f]+) by scripts/(\S+)\.py")

LOCK_WAIT_S = 120.0
LOCK_STALE_S = 300.0

_logged_idf = set()


class PatchError(Exception):
    pass


class Patch:
    """One versioned patch set: a name, a version and its hunks.

    hunks is a list of (anchor, replacement, expected_count). Bump version
    whenever the hunks change; the digest catches a forgotten bump, the version
    is what a person reads in the build log.
    """

    def __init__(self, name, version, hunks):
        if not hunks:
            raise PatchError("patch %s v%d has no hunks" % (name, version))
        for hunk in hunks:
            if len(hunk) != 3 or not hunk[0] or hunk[2] < 1:
                raise PatchError("patch %s has a malformed hunk: %r" % (name, hunk[:1]))
        self.name = name
        self.version = int(version)
        self.hunks = hunks

    def digest(self):
        h = hashlib.sha256()
        for old, new, count in self.hunks:
            for part in (old, new, str(count)):
                data = part.encode("utf-8")
                h.update(str(len(data)).encode("ascii") + b":" + data)
        return h.hexdigest()[:12]

    def stamp(self, owner):
        return "/* %s%d %s %s by scripts/%s.py; pristine copy beside this file as .gm-orig */\n" % (
            STAMP_TAG, self.version, self.name, self.digest(), owner)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def render(baseline, owner, patches, path="<text>"):
    """Apply patches to a pristine text and prepend their stamps."""
    text = baseline
    for patch in patches:
        for index, (old, new, count) in enumerate(patch.hunks, start=1):
            found = text.count(old)
            if found != count:
                raise PatchError(
                    "%s: %s hunk %d anchor found %d times (want %d) in %s. The upstream "
                    "file changed: re-derive the hunk against the new source rather than "
                    "loosening this check. Anchor begins: %r"
                    % (owner, patch.name, index, found, count, path, old[:80]))
            text = text.replace(old, new)
    return "".join(p.stamp(owner) for p in patches) + text


def stamps_in(text):
    """(version, name, digest, owner) for every stamp line in text."""
    return [(int(m.group(1)), m.group(2), m.group(3), m.group(4)) for m in STAMP_RE.finditer(text)]


def _read(path):
    with open(path, "rb") as handle:
        return handle.read()


def _write_atomic(path, data, mode_from=None):
    tmp = path + ".gm-tmp"
    with open(tmp, "wb") as handle:
        handle.write(data)
    if mode_from and os.path.exists(mode_from):
        shutil.copymode(mode_from, tmp)
    os.replace(tmp, path)


class _Lock:
    def __init__(self, path):
        self.dir = path + ".gm-lock"

    def __enter__(self):
        deadline = time.time() + LOCK_WAIT_S
        while True:
            try:
                os.mkdir(self.dir)
                return self
            except FileExistsError:
                try:
                    age = time.time() - os.stat(self.dir).st_mtime
                except FileNotFoundError:
                    continue
                if age > LOCK_STALE_S:
                    try:
                        os.rmdir(self.dir)
                    except OSError:
                        pass
                    continue
                if time.time() > deadline:
                    raise PatchError("timed out waiting for lock %s" % self.dir)
                time.sleep(0.1)

    def __exit__(self, *exc):
        try:
            os.rmdir(self.dir)
        except OSError:
            pass
        return False


def ensure(owner, path, pristine_sha256, patches, log=print):
    """Make path equal to render(pristine, owner, patches). Returns the actions taken.

    owner is the script's base name (e.g. "patch_lvgl_meter_inv"), pristine_sha256
    the set of known upstream hashes for the target, patches a list of Patch.
    Raises PatchError on an unknown baseline, an anchor mismatch or a target
    another script also stamps.
    """
    orig = path + ".gm-orig"
    with _Lock(path):
        cur_bytes = _read(path)
        cur = cur_bytes.decode("utf-8")
        foreign = sorted({s[3] for s in stamps_in(cur) if s[3] != owner})
        if foreign:
            raise PatchError(
                "%s: %s also carries hunks from %s. Re-deriving it from the pristine "
                "copy would erase them, so this is refused; move both patch sets into "
                "one script." % (owner, path, ", ".join("scripts/%s.py" % f for f in foreign)))

        orig_bytes = _read(orig) if os.path.exists(orig) else None
        actions = []
        if sha256(cur_bytes) in pristine_sha256:
            baseline = cur_bytes
            if orig_bytes != baseline:
                _write_atomic(orig, baseline, mode_from=path)
                actions.append("wrote .gm-orig" if orig_bytes is None
                               else "replaced a .gm-orig that was not pristine")
        elif orig_bytes is not None and sha256(orig_bytes) in pristine_sha256:
            baseline = orig_bytes
        else:
            raise PatchError(
                "%s: no pristine baseline for %s. The file (sha256 %s) and its .gm-orig "
                "(%s) match none of the known upstream hashes %s. Restore the upstream "
                "file (delete .pio/libdeps/<env>/<lib> or reinstall the framework "
                "package) and build again; if upstream itself changed, re-derive the "
                "hunks and add the new hash to the script."
                % (owner, path, sha256(cur_bytes),
                   sha256(orig_bytes) if orig_bytes is not None else "missing",
                   ", ".join(sorted(pristine_sha256))))

        target = render(baseline.decode("utf-8"), owner, patches, path).encode("utf-8")
        state = "already" if cur_bytes == target else "applied"
        if state == "applied":
            _write_atomic(path, target, mode_from=path)
        for patch in patches:
            log("gm-patch: %s: %s %s v%d (%s)" % (owner, patch.name, state, patch.version, path))
        for action in actions:
            log("gm-patch: %s: %s (%s)" % (owner, action, orig))
        return [state] + actions


def run(owner, path, pristine_sha256, patches):
    """ensure() for a pre-build script: a PatchError fails the build."""
    try:
        ensure(owner, path, pristine_sha256, patches)
    except PatchError as err:
        sys.stderr.write("gm-patch: ERROR: %s\n" % err)
        sys.exit(1)


def idf_dir(env):
    """The framework-espidf package directory PlatformIO builds this env from.

    The one way the IDF patches locate the framework: through the platform's own
    package manager, not IDF_PATH or a guessed ~/.platformio. Logged once per
    build.
    """
    path = env.PioPlatform().get_package_dir("framework-espidf")
    if not path or not os.path.isdir(path):
        sys.stderr.write("gm-patch: ERROR: framework-espidf package not found for env %s\n"
                         % env.subst("$PIOENV"))
        sys.exit(1)
    if path not in _logged_idf:
        _logged_idf.add(path)
        print("gm-patch: IDF at %s" % path)
    return path


def idf_file(env, *rel):
    path = os.path.join(idf_dir(env), *rel)
    if not os.path.isfile(path):
        sys.stderr.write("gm-patch: ERROR: %s not found in the IDF package\n" % path)
        sys.exit(1)
    return path

