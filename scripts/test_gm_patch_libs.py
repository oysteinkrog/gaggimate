"""Tests for the library patch scripts built on scripts/gm_patch.py.

Run: python3 scripts/test_gm_patch_libs.py            (unit tests)
     python3 scripts/test_gm_patch_libs.py --installed (report whether each
                                                        installed target equals
                                                        what its script derives)

Covers patch_asynctcp_backlog, patch_asyncws_erase_safe and patch_ble_scan_duty
(gm-bzu.81). It sits beside scripts/test_gm_patch.py rather than inside it
because that file's targets() was being edited by another bead at the same
time; targets() below has the same shape, so its entries can move there as
they are.

Unlike test_gm_patch.py, the pristine copy is looked up per target file, not
per script: AsyncTCP is installed twice per env at two different releases
(3.4.10 by git pin, 3.5.0 from the registry), so one script has two baselines.
"""

import glob
import hashlib
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import gm_patch  # noqa: E402
from test_gm_patch import load_script, quiet  # noqa: E402

LIBDEPS = os.path.join(ROOT, ".pio", "libdeps")


def libdeps(*rel):
    return sorted(glob.glob(os.path.join(LIBDEPS, "*", *rel)))


# (script, [target paths]); same shape as test_gm_patch.targets().
def targets():
    return [
        ("patch_asynctcp_backlog", libdeps("AsyncTCP*", "src", "AsyncTCP.cpp")),
        ("patch_asyncws_erase_safe", libdeps("ESPAsyncWebServer*", "src", "AsyncWebSocket.cpp")),
        ("patch_ble_scan_duty", libdeps("esp-arduino-ble-scales", "src", "remote_scales.cpp")),
    ]


def pristine_for(module, path):
    """The pinned-hash pristine text for one target, or None.

    Tries the .gm-orig, the file itself, and for a script that has one, the
    file with its marker-based legacy patch reversed.
    """
    for candidate in (path + ".gm-orig", path):
        if not os.path.isfile(candidate):
            continue
        with open(candidate, "rb") as f:
            data = f.read()
        if hashlib.sha256(data).hexdigest() in module.PRISTINE_SHA256:
            return data.decode("utf-8")
    unpatch = getattr(module, "unpatch_legacy", None)
    if unpatch and os.path.isfile(path):
        with open(path, "rb") as f:
            text = unpatch(f.read().decode("utf-8"))
        if text is not None and gm_patch.sha256(text.encode("utf-8")) in module.PRISTINE_SHA256:
            return text
    return None


def pristines(module, paths):
    """One pristine text per distinct pinned hash found among paths."""
    found = {}
    for path in paths:
        text = pristine_for(module, path)
        if text is not None:
            found.setdefault(gm_patch.sha256(text.encode("utf-8")), text)
    return found


class ScriptTests(unittest.TestCase):
    def test_scripts_import_without_scons(self):
        for name, _paths in targets():
            with self.subTest(script=name):
                module = load_script(name)
                self.assertEqual(module.OWNER, name)
                self.assertTrue(module.PATCHES)
                self.assertTrue(module.PRISTINE_SHA256)

    def test_scripts_render_against_upstream(self):
        for name, paths in targets():
            module = load_script(name)
            found = pristines(module, paths)
            if not found:
                with self.subTest(script=name):
                    self.skipTest("no pristine copy of %s's target on this machine" % name)
            for sha, pristine in found.items():
                with self.subTest(script=name, pristine=sha[:12]):
                    out = gm_patch.render(pristine, module.OWNER, module.PATCHES)
                    self.assertEqual(out.count(gm_patch.STAMP_TAG), len(module.PATCHES))
                    for patch in module.PATCHES:
                        self.assertIn(patch.name, out[out.index("\n"):])

    def test_asynctcp_pins_both_installed_releases(self):
        self.assertEqual(len(load_script("patch_asynctcp_backlog").PRISTINE_SHA256), 2)


class BleLegacyTests(unittest.TestCase):
    """The marker-based BLE script left patched files with no .gm-orig."""

    def setUp(self):
        self.module = load_script("patch_ble_scan_duty")
        paths = dict(targets())["patch_ble_scan_duty"]
        found = pristines(self.module, paths)
        if not found:
            self.skipTest("no pristine copy of remote_scales.cpp on this machine")
        self.pristine = next(iter(found.values()))
        self.tmp = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.tmp.name, "remote_scales.cpp")

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, text):
        with open(self.path, "w", encoding="utf-8", newline="") as f:
            f.write(text)

    def read(self, path):
        with open(path, encoding="utf-8", newline="") as f:
            return f.read()

    def legacy(self):
        text = self.pristine
        for old, new in self.module.LEGACY_HUNKS:
            text = text.replace(old, new)
        return text

    def ensure(self):
        return gm_patch.ensure(self.module.OWNER, self.path, self.module.PRISTINE_SHA256,
                               self.module.PATCHES, log=quiet)

    def test_legacy_tree_is_recovered_and_rederived(self):
        self.write(self.legacy())
        self.assertTrue(self.module.recover_gm_orig(self.path, log=quiet))
        self.assertEqual(self.read(self.path + ".gm-orig"), self.pristine)
        self.assertEqual(self.ensure(), ["applied"])
        want = gm_patch.render(self.pristine, self.module.OWNER, self.module.PATCHES)
        self.assertEqual(self.read(self.path), want)
        self.assertEqual(self.ensure(), ["already"])

    def test_fresh_install_matches_recovered_legacy(self):
        self.write(self.pristine)
        self.assertFalse(self.module.recover_gm_orig(self.path, log=quiet))
        self.ensure()
        fresh = self.read(self.path)
        self.write(self.legacy())
        os.remove(self.path + ".gm-orig")
        self.module.recover_gm_orig(self.path, log=quiet)
        self.ensure()
        self.assertEqual(self.read(self.path), fresh)

    def test_tampered_legacy_tree_is_not_recovered(self):
        self.write(self.legacy().replace("NimBLEDevice", "NimBLEDevic3", 1))
        self.assertFalse(self.module.recover_gm_orig(self.path, log=quiet))
        self.assertFalse(os.path.exists(self.path + ".gm-orig"))
        with self.assertRaisesRegex(gm_patch.PatchError, "no pristine baseline"):
            self.ensure()

    def test_existing_gm_orig_is_never_overwritten(self):
        self.write(self.legacy())
        with open(self.path + ".gm-orig", "w", encoding="utf-8", newline="") as f:
            f.write("not pristine\n")
        self.assertFalse(self.module.recover_gm_orig(self.path, log=quiet))
        self.assertEqual(self.read(self.path + ".gm-orig"), "not pristine\n")

    def test_stamped_file_is_not_treated_as_legacy(self):
        stamped = gm_patch.render(self.pristine, self.module.OWNER, self.module.PATCHES)
        self.assertIsNone(self.module.unpatch_legacy(stamped))


def report_installed():
    """Print, per installed target, whether it equals the script's derivation."""
    status = 0
    for name, paths in targets():
        module = load_script(name)
        for path in paths:
            with open(path, encoding="utf-8", newline="") as f:
                cur = f.read()
            pristine = pristine_for(module, path)
            if pristine is None:
                print("UNKNOWN  %s (no pristine copy found) %s" % (name, path))
                status = 1
                continue
            want = gm_patch.render(pristine, module.OWNER, module.PATCHES)
            if cur == want:
                state = "CURRENT "
            elif cur == pristine:
                state = "PRISTINE"
            else:
                state = "STALE   "
                status = 1
            print("%s %s stamps=%d want=%d %s" % (state, name, cur.count(gm_patch.STAMP_TAG),
                                                  len(module.PATCHES), path))
    return status


if __name__ == "__main__":
    if "--installed" in sys.argv:
        sys.exit(report_installed())
    unittest.main()
