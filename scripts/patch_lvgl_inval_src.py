"""Records which object asked for a near-whole-screen invalidation.

dirty_recent on /api/debug/anim says a whole-screen redraw happened. It cannot
say who asked for it, and that is the question when a telemetry readout steps
instead of counting: one widget invalidating all 480x480 on every value turns
each overlay publish into a whole-page snapshot, which on the bench board cost
a median of 147 ms and held the readout to 3.7 updates a second.

lv_obj_invalidate_area is the single place every object-driven invalidation
goes through, so one call at its head names the object, its class and both
rectangles. The recorder (gm_record_inval_src, LV_Helper.cpp) keeps only areas
of at least 240x240, so a label redrawing its own digits does not push the
interesting entries out of the ring.

The call sits on the last line of lv_obj_invalidate_area, so it records what
actually reached the display list: an invalidation a layer swallows, or one
LVGL drops because the object is hidden or off-screen, costs nothing and is not
recorded. A second hunk parks lv_obj_invalidate's own return address in
gm_inval_caller for the length of the call, which is the only thing that
separates a hidden-flag change from a style state change from a widget setter.
patch_lvgl_label_elem.py patches the same file; both are hunk-marked, so they
apply in either order and re-apply cleanly.

The library lives in .pio/libdeps/<env>/lvgl, so like the other LVGL patches
this one resolves the path through the SCons env. A pristine copy is kept at
lv_obj_pos.c.gm-orig beside the patched file.
"""

import os
import shutil

Import("env")  # noqa: F821 -- provided by SCons

MARKER = "GM_INVAL_SRC_PATCH"

DECL = (
    "/* " + MARKER + " */\n"
    "void gm_record_inval_src(const void * obj, const void * cls, int ox1, int oy1, int ox2, int oy2,\n"
    "                         int ax1, int ay1, int ax2, int ay2);\n"
    "extern const void * gm_inval_caller;\n"
)

# The record sits on the last line of lv_obj_invalidate_area, after LVGL has
# dropped the invalidations that cost nothing (a layer-owned object, a display
# with invalidation off, a hidden or off-screen object) and after the area has
# been truncated to what is actually visible. Recording at the head of the
# function instead reports work that never happens, which is how a permanently
# hidden widget reads as the culprit. area_tmp is the truncated area.
OLD_REC = "    _lv_inv_area(lv_obj_get_disp(obj),  &area_tmp);\n"
NEW_REC = (
    "    { /* " + MARKER + " */\n"
    "        lv_area_t gm_oc;\n"
    "        lv_obj_get_coords(obj, &gm_oc);\n"
    "        gm_record_inval_src(obj, obj->class_p, gm_oc.x1, gm_oc.y1, gm_oc.x2, gm_oc.y2,\n"
    "                            area_tmp.x1, area_tmp.y1, area_tmp.x2, area_tmp.y2);\n"
    "    }\n"
    "    _lv_inv_area(lv_obj_get_disp(obj),  &area_tmp);\n"
)

# lv_obj_invalidate is the whole-object path and the expensive one. Its own
# return address names the caller, which the object cannot: LVGL invalidates a
# whole object from a hidden-flag change, a style state change and a dozen
# widget setters. __builtin_return_address(0) is what GCC gives on Xtensa, so
# the value carries the windowed ABI's call-size bits; the reader unwinds them.
OLD_CALLER = (
    "    lv_obj_invalidate_area(obj, &obj_coords);\n"
)
NEW_CALLER = (
    "    gm_inval_caller = __builtin_return_address(0); /* " + MARKER + " */\n"
    "    lv_obj_invalidate_area(obj, &obj_coords);\n"
    "    gm_inval_caller = NULL; /* " + MARKER + " */\n"
)

OLD_DECL = "void lv_obj_invalidate_area(const lv_obj_t * obj, const lv_area_t * area)\n"
NEW_DECL = DECL + OLD_DECL

HUNKS = [(OLD_DECL, NEW_DECL, 1), (OLD_REC, NEW_REC, 1), (OLD_CALLER, NEW_CALLER, 1)]


def apply(path, hunks):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    orig = path + ".gm-orig"
    if not os.path.exists(orig):
        shutil.copyfile(path, orig)
    if MARKER in text:
        print("patch_lvgl_inval_src: already applied (%s)" % path)
        return
    changed = 0
    for old, new, count in hunks:
        found = text.count(old)
        if found != count:
            raise SystemExit("patch_lvgl_inval_src: expected %d of a hunk in %s, found %d\n  %s"
                             % (count, path, found, old.splitlines()[0]))
        text = text.replace(old, new, count)
        changed += 1
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("patch_lvgl_inval_src: patched %s (%d hunks)" % (path, changed))


def main():
    path = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"),  # noqa: F821
                        "lvgl", "src", "core", "lv_obj_pos.c")
    if not os.path.isfile(path):
        print("patch_lvgl_inval_src: lv_obj_pos.c not found for this env; skipping")
        return
    apply(path, HUNKS)


main()
