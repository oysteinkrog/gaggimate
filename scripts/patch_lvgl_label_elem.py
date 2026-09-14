"""Labels owned by a compositor Text element (gm-2cl.5).

Why this exists
---------------
While the background animation composites a screen, DefaultUI paints a live
label (a temperature, a weight, a timer) through a Text element
(SleepAnimation::ElementType::Text, glyphs from GlyphAtlas) and marks the
label with LV_OBJ_FLAG_USER_2. From then on LVGL must keep the label as a
normal, visible object (the generated flow code reads and writes its
hidden flag every tick, so hiding it is not an option) but must not draw
it and must not invalidate it when the flow sets its text: that
invalidation is what turns every telemetry value into an LVGL draw, a
snapshot and an overlay publish.

Three hunks in lv_label.c, all guarded by the flag:
- lv_label_set_text skips its leading lv_obj_invalidate;
- lv_label_refr_text skips its closing lv_obj_invalidate (the text is
  still stored and the self size still refreshed, so lv_label_get_text and
  the label's coordinates stay current for the element builder);
- draw_main returns before drawing.

Two more hunks in core/lv_obj_pos.c, same guard: lv_obj_refr_size and
lv_obj_move_to skip their two invalidations (the old area and the new one)
for a flagged object. A content-sized label whose text width changes
resizes and, when centre-aligned, moves, and those were the two rectangles
still invalidating on the brew screen once the labels were owned
(measured 2026-09-08, dirty_recent on /api/debug/anim: 85x49 and 105x49
boxes at the status bar, every value change). The coordinates still
update; only the invalidation is skipped, and the area holds no label
pixels either way.

Idempotent: guarded by a marker string. Anchored on exact upstream text so
an LVGL bump that rewrites these functions fails the build loudly here
rather than silently drawing owned labels twice. Same mechanics as
scripts/patch_lvgl_meter_inv.py: the library lives in
.pio/libdeps/<env>/lvgl, resolved through the SCons env, and a pristine
copy is kept at lv_label.c.gm-orig beside the patched file.

Since gm-2cl.17 LV_OBJ_FLAG_USER_3 on an lv_img means "owned by a layer":
the image draws nothing (lv_img.c) and none of its invalidations reach the
display (lv_obj_pos.c); DefaultUI::serviceIconLayers mirrors its state and
hidden flag into layer sprites instead.
"""

import os
import sys

Import("env")  # noqa: F821 -- provided by SCons

MARKER = "GM_LABEL_ELEM_PATCH"
GUARD = "lv_obj_has_flag(obj, LV_OBJ_FLAG_USER_2)"

SET_TEXT_OLD = (
    "    LV_ASSERT_OBJ(obj, MY_CLASS);\n"
    "    lv_label_t * label = (lv_label_t *)obj;\n"
    "\n"
    "    lv_obj_invalidate(obj);\n"
    "\n"
    "    /*If text is NULL then just refresh with the current text*/\n"
)
SET_TEXT_NEW = (
    "    LV_ASSERT_OBJ(obj, MY_CLASS);\n"
    "    lv_label_t * label = (lv_label_t *)obj;\n"
    "\n"
    "    if(!" + GUARD + ") lv_obj_invalidate(obj); /* " + MARKER + ": owned by a compositor element */\n"
    "\n"
    "    /*If text is NULL then just refresh with the current text*/\n"
)

REFR_OLD = (
    "    else if(label->long_mode == LV_LABEL_LONG_CLIP) {\n"
    "        /*Do nothing*/\n"
    "    }\n"
    "\n"
    "    lv_obj_invalidate(obj);\n"
    "}\n"
)
REFR_NEW = (
    "    else if(label->long_mode == LV_LABEL_LONG_CLIP) {\n"
    "        /*Do nothing*/\n"
    "    }\n"
    "\n"
    "    if(!" + GUARD + ") lv_obj_invalidate(obj); /* " + MARKER + " */\n"
    "}\n"
)

DRAW_OLD = (
    "static void draw_main(lv_event_t * e)\n"
    "{\n"
    "    lv_obj_t * obj = lv_event_get_target(e);\n"
    "    lv_label_t * label = (lv_label_t *)obj;\n"
    "    lv_draw_ctx_t * draw_ctx = lv_event_get_draw_ctx(e);\n"
    "\n"
)
DRAW_NEW = (
    "static void draw_main(lv_event_t * e)\n"
    "{\n"
    "    lv_obj_t * obj = lv_event_get_target(e);\n"
    "    lv_label_t * label = (lv_label_t *)obj;\n"
    "    lv_draw_ctx_t * draw_ctx = lv_event_get_draw_ctx(e);\n"
    "    if(" + GUARD + ") return; /* " + MARKER + ": the Text element paints it */\n"
    "\n"
)

# lv_label_set_text_fmt invalidates on its own, before it reaches
# lv_label_refr_text, so guarding set_text and refr_text is not enough: a caller
# that formats its value (DefaultUI::maintainScaleScreen writes the scale
# readout with set_text_fmt) invalidated the owned label's box on every value.
# On the bench board that was 11.8 invalidations a second on the scale screen,
# each one an overlay publish the owned element had already made unnecessary.
SET_FMT_OLD = (
    "    LV_ASSERT_OBJ(obj, MY_CLASS);\n"
    "    LV_ASSERT_NULL(fmt);\n"
    "\n"
    "    lv_obj_invalidate(obj);\n"
)
SET_FMT_NEW = (
    "    LV_ASSERT_OBJ(obj, MY_CLASS);\n"
    "    LV_ASSERT_NULL(fmt);\n"
    "\n"
    "    if(!" + GUARD + ") lv_obj_invalidate(obj); /* " + MARKER + " */\n"
)

HUNKS = [
    (SET_TEXT_OLD, SET_TEXT_NEW, 1),
    (SET_FMT_OLD, SET_FMT_NEW, 1),
    (REFR_OLD, REFR_NEW, 1),
    (DRAW_OLD, DRAW_NEW, 1),
]

# lv_obj_pos.c: lv_obj_refr_size and lv_obj_move_to share the two comment
# lines, so each anchor matches exactly twice and the replacement applies to
# both functions.
POS_ORIG_OLD = (
    "    /*Invalidate the original area*/\n"
    "    lv_obj_invalidate(obj);\n"
)
POS_ORIG_NEW = (
    "    /*Invalidate the original area*/\n"
    "    if(!" + GUARD + ") lv_obj_invalidate(obj); /* " + MARKER + " */\n"
)
POS_NEW_OLD = (
    "    /*Invalidate the new area*/\n"
    "    lv_obj_invalidate(obj);\n"
)
POS_NEW_NEW = (
    "    /*Invalidate the new area*/\n"
    "    if(!" + GUARD + ") lv_obj_invalidate(obj); /* " + MARKER + " */\n"
)
POS_HUNKS = [
    (POS_ORIG_OLD, POS_ORIG_NEW, 2),
    (POS_NEW_OLD, POS_NEW_NEW, 2),
]

# LV_OBJ_FLAG_USER_3 on an lv_img means "owned by a layer" (gm-2cl.17,
# DefaultUI::serviceIconLayers): the image draws nothing (lv_img.c) and
# none of its invalidations reach the display (lv_obj_pos.c), so the flow
# can toggle its CHECKED state (the dial screens' icons blink by a recolor
# on that state) or its hidden flag without costing an LVGL pass. The UI
# task invalidates the image before setting the flag and after clearing
# it, so the handover to and from the layer still redraws the area.
GUARD3 = "lv_obj_has_flag(obj, LV_OBJ_FLAG_USER_3)"
INV_OLD = (
    "void lv_obj_invalidate_area(const lv_obj_t * obj, const lv_area_t * area)\n"
    "{\n"
    "    LV_ASSERT_OBJ(obj, MY_CLASS);\n"
    "\n"
)
INV_NEW = (
    "void lv_obj_invalidate_area(const lv_obj_t * obj, const lv_area_t * area)\n"
    "{\n"
    "    LV_ASSERT_OBJ(obj, MY_CLASS);\n"
    "    if(" + GUARD3 + ") return; /* " + MARKER + ": owned by a layer */\n"
    "\n"
)
POS_HUNKS.append((INV_OLD, INV_NEW, 1))

IMG_OLD = (
    "    else if(code == LV_EVENT_DRAW_MAIN || code == LV_EVENT_DRAW_POST) {\n"
    "\n"
    "        lv_coord_t obj_w = lv_obj_get_width(obj);\n"
)
IMG_NEW = (
    "    else if(code == LV_EVENT_DRAW_MAIN || code == LV_EVENT_DRAW_POST) {\n"
    "        if(" + GUARD3 + ") return; /* " + MARKER + ": a layer paints it */\n"
    "\n"
    "        lv_coord_t obj_w = lv_obj_get_width(obj);\n"
)
IMG_HUNKS = [
    (IMG_OLD, IMG_NEW, 1),
]


def apply(path, hunks):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    orig = path + ".gm-orig"
    if not os.path.exists(orig):
        with open(orig, "w", encoding="utf-8") as f:
            f.write(text)
    # Per hunk, so a hunk added later still lands in a file the marker is
    # already in.
    changed = 0
    for old, new, count in hunks:
        if new in text:
            continue
        found = text.count(old)
        if found != count:
            sys.stderr.write(
                "patch_lvgl_label_elem: anchor found %d times (want %d) in %s; "
                "LVGL was updated and this patch needs review. Anchor begins: %r\n"
                % (found, count, path, old[:80]))
            sys.exit(1)
        text = text.replace(old, new)
        changed += 1
    if changed == 0:
        print("patch_lvgl_label_elem: already applied (%s)" % path)
        return
    tmp = path + ".gm-tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(text)
    os.replace(tmp, path)
    print("patch_lvgl_label_elem: patched %s (%d hunks)" % (path, changed))


def main():
    root = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"), "lvgl", "src")  # noqa: F821
    for rel, hunks in ((("widgets", "lv_label.c"), HUNKS), (("core", "lv_obj_pos.c"), POS_HUNKS),
                       (("widgets", "lv_img.c"), IMG_HUNKS)):
        path = os.path.join(root, *rel)
        if not os.path.isfile(path):
            print("patch_lvgl_label_elem: %s not found for this env; skipping" % rel[-1])
            continue
        apply(path, hunks)


main()
