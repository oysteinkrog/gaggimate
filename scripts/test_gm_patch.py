"""Tests for scripts/gm_patch.py and the patch scripts built on it.

Run: python3 scripts/test_gm_patch.py            (unit tests)
     python3 scripts/test_gm_patch.py --installed (report whether each installed
                                                   target equals what the scripts
                                                   derive from its pristine copy)

The synthetic tests cover each input a target can be found in: pristine,
patched by an older version, already current, a contaminated .gm-orig, an
unknown baseline, and a file another script also stamps. The script tests
render each real patch against the real upstream file when a copy with the
pinned hash is on this machine, and skip otherwise.
"""

import glob
import hashlib
import importlib.util
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import gm_patch  # noqa: E402

PRISTINE = "alpha\nbeta\ngamma\nbeta\n"
PRISTINE_SHA = {hashlib.sha256(PRISTINE.encode()).hexdigest()}


def v1():
    return [gm_patch.Patch("GM_TEST_PATCH", 1, [("alpha\n", "alpha GM_TEST_PATCH\n", 1)])]


def v2():
    return [gm_patch.Patch("GM_TEST_PATCH", 2, [("alpha\n", "alpha GM_TEST_PATCH\n", 1),
                                                ("beta\n", "beta2\n", 2)])]


def quiet(*_args):
    pass


class HelperTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.tmp.name, "target.c")
        self.write(PRISTINE)

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, text, path=None):
        with open(path or self.path, "w", encoding="utf-8", newline="") as f:
            f.write(text)

    def read(self, path=None):
        with open(path or self.path, encoding="utf-8", newline="") as f:
            return f.read()

    def ensure(self, patches, owner="patch_test"):
        return gm_patch.ensure(owner, self.path, PRISTINE_SHA, patches, log=quiet)

    def test_pristine_is_applied_and_gm_orig_written(self):
        self.assertEqual(self.ensure(v1()), ["applied", "wrote .gm-orig"])
        self.assertEqual(self.read(), gm_patch.render(PRISTINE, "patch_test", v1()))
        self.assertEqual(self.read(self.path + ".gm-orig"), PRISTINE)
        self.assertEqual(self.read().count(gm_patch.STAMP_TAG), 1)

    def test_current_is_left_alone(self):
        self.ensure(v1())
        before = os.stat(self.path).st_mtime_ns
        self.assertEqual(self.ensure(v1()), ["already"])
        self.assertEqual(os.stat(self.path).st_mtime_ns, before)

    def test_older_version_is_rederived_byte_identical(self):
        self.ensure(v1())
        self.assertEqual(self.ensure(v2()), ["applied"])
        fresh = gm_patch.render(PRISTINE, "patch_test", v2())
        self.assertEqual(self.read(), fresh)
        self.assertIn("gm-patch-v2 GM_TEST_PATCH", self.read())
        self.assertNotIn("gm-patch-v1", self.read())

    def test_unstamped_old_patch_is_rederived_from_gm_orig(self):
        # What the marker-based scripts left behind: patched, no stamp.
        self.write(PRISTINE, self.path + ".gm-orig")
        self.write(PRISTINE.replace("alpha\n", "alpha GM_TEST_PATCH\n"))
        self.assertEqual(self.ensure(v2()), ["applied"])
        self.assertEqual(self.read(), gm_patch.render(PRISTINE, "patch_test", v2()))

    def test_hunk_edit_without_version_bump_changes_stamp(self):
        self.ensure(v1())
        edited = [gm_patch.Patch("GM_TEST_PATCH", 1, [("alpha\n", "alpha GM_TEST_PATCH edited\n", 1)])]
        self.assertEqual(self.ensure(edited), ["applied"])
        self.assertIn("edited", self.read())

    def test_contaminated_gm_orig_is_replaced_when_file_is_pristine(self):
        self.write("alpha GM_TEST_PATCH\nbeta\ngamma\nbeta\n", self.path + ".gm-orig")
        self.assertEqual(self.ensure(v1()), ["applied", "replaced a .gm-orig that was not pristine"])
        self.assertEqual(self.read(self.path + ".gm-orig"), PRISTINE)

    def test_contaminated_gm_orig_with_patched_file_fails(self):
        self.write("alpha GM_TEST_PATCH\nbeta\ngamma\nbeta\n", self.path + ".gm-orig")
        self.write("alpha GM_TEST_PATCH\nbeta\ngamma\nbeta\n")
        with self.assertRaisesRegex(gm_patch.PatchError, "no pristine baseline"):
            self.ensure(v1())

    def test_unknown_baseline_fails(self):
        self.write("something else entirely\n")
        with self.assertRaisesRegex(gm_patch.PatchError, "no pristine baseline"):
            self.ensure(v1())

    def test_anchor_count_mismatch_fails(self):
        bad = [gm_patch.Patch("GM_TEST_PATCH", 1, [("beta\n", "b\n", 1)])]
        with self.assertRaisesRegex(gm_patch.PatchError, "found 2 times"):
            self.ensure(bad)
        self.assertEqual(self.read(), PRISTINE)

    def test_empty_hunk_list_is_an_error(self):
        with self.assertRaises(gm_patch.PatchError):
            gm_patch.Patch("GM_EMPTY", 1, [])

    def test_foreign_stamp_is_refused(self):
        self.ensure(v1(), owner="patch_other")
        with self.assertRaisesRegex(gm_patch.PatchError, "patch_other"):
            self.ensure(v1())

    def test_stale_lock_is_broken(self):
        lock = self.path + ".gm-lock"
        os.mkdir(lock)
        old = os.stat(lock).st_mtime - gm_patch.LOCK_STALE_S - 10
        os.utime(lock, (old, old))
        self.assertEqual(self.ensure(v1())[0], "applied")
        self.assertFalse(os.path.exists(lock))


def load_script(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def idf_root():
    base = os.environ.get("PLATFORMIO_CORE_DIR") or os.path.expanduser("~/.platformio")
    return os.path.join(base, "packages", "framework-espidf")


# (script, [candidate target paths]); every candidate and its .gm-orig is
# searched for a copy with the pinned pristine hash.
def targets():
    return [
        ("patch_esp_lcd_rgb", [os.path.join(idf_root(), "components", "esp_lcd", "rgb", "esp_lcd_panel_rgb.c")]),
        ("patch_flash_cache_flag", [os.path.join(idf_root(), "components", "spi_flash", "cache_utils.c")]),
        ("patch_lvgl_meter_inv", sorted(glob.glob(os.path.join(
            ROOT, ".pio", "libdeps", "*", "lvgl", "src", "extra", "widgets", "meter", "lv_meter.c")))),
    ]


def find_pristine(module, paths):
    for path in paths:
        for candidate in (path + ".gm-orig", path):
            if os.path.isfile(candidate):
                with open(candidate, "rb") as f:
                    data = f.read()
                if hashlib.sha256(data).hexdigest() in module.PRISTINE_SHA256:
                    return data.decode("utf-8")
    return None


class ScriptTests(unittest.TestCase):
    def test_scripts_render_against_upstream(self):
        for name, paths in targets():
            with self.subTest(script=name):
                module = load_script(name)
                pristine = find_pristine(module, paths)
                if pristine is None:
                    self.skipTest("no pristine copy of %s's target on this machine" % name)
                out = gm_patch.render(pristine, module.OWNER, module.PATCHES)
                self.assertEqual(out.count(gm_patch.STAMP_TAG), len(module.PATCHES))
                for patch in module.PATCHES:
                    self.assertIn(patch.name, out[out.index("\n"):])

    def test_meter_keeps_element_guard_and_needle_fix(self):
        module = load_script("patch_lvgl_meter_inv")
        text = "".join(new for _old, new, _n in module.PATCHES[0].hunks)
        self.assertIn("GM_METER_ELEM_PATCH", text)
        self.assertIn("gm_inv_needle_img", text)


def report_installed():
    """Print, per installed target, whether it equals the scripts' derivation."""
    status = 0
    for name, paths in targets():
        module = load_script(name)
        pristine = find_pristine(module, paths)
        for path in paths:
            if not os.path.isfile(path):
                continue
            with open(path, encoding="utf-8", newline="") as f:
                cur = f.read()
            if pristine is None:
                print("UNKNOWN  %s (no pristine copy found)" % path)
                status = 1
                continue
            want = gm_patch.render(pristine, module.OWNER, module.PATCHES)
            stamps = cur.count(gm_patch.STAMP_TAG)
            if cur == want:
                state = "CURRENT "
            elif hashlib.sha256(cur.encode()).hexdigest() in module.PRISTINE_SHA256:
                state = "PRISTINE"
            else:
                state = "STALE   "
                status = 1
            print("%s %s stamps=%d want=%d %s" % (state, name, stamps, len(module.PATCHES), path))
    return status


if __name__ == "__main__":
    if "--installed" in sys.argv:
        sys.exit(report_installed())
    unittest.main()
