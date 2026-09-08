"""Partial invalidation for lv_meter scale-lines indicators.

Why this exists
---------------
The main screens put three 500x500 lv_meter dials (temperature needle,
full-range temperature, pressure) in the centre of a 480x480 panel, each with
a scale-lines indicator that recolors the tick ring up to the current value.
Upstream LVGL 8.4 invalidates needle and arc indicators partially (inv_line /
inv_arc), but a scale-lines indicator falls into the else-branch of all three
value setters: lv_obj_invalidate(obj) -- the whole meter, which here is the
whole screen. Telemetry streams temperature and pressure continuously, so
every tick invalidated all 230,400 pixels. Measured on the bench rig
(GM_UISTAT): the LVGL task pass ran at ~650 ms instead of ~5 ms, which put
touch feedback at ~750 ms and every widget readout at ~1.5 Hz.

The patch adds gm_inv_scale_lines(): invalidate only the tick-ring sector
between the old and the new value, widened by one tick pitch on each side so
the boundary tick is covered whichever side of it the mapped angle lands, and
by the tick width radially for line caps. Modelled on upstream's own inv_arc.

lv_draw_arc_get_area handles the mapped angles the same way it does for
inv_arc: values above 360 are normalized and a start>end sector takes the
wrapped-quarter path, so gauges whose scale crosses 360 (rotation 300 +
range 120) stay covered.

Needle images take the same route (2026-09-08). Upstream sends a NEEDLE_IMG
indicator through inv_line(), which reads needle_line.r_mod out of the
type_data union; for an image that word is part of the src pointer, so the
"line" is thousands of pixels long and the area clamps to the whole meter:
every telemetry tick that moved a temperature needle repainted the screen
(dirty_recent on /api/debug/anim showed 0,0,479,479 four times a second on
the grind screen). gm_inv_needle_img() invalidates the rotated image's own
bounding box at the old and the new angle, computed the way lv_draw_img
computes it (_lv_img_buf_get_transformed_area about the pivot placed on the
scale centre), plus one pixel of margin.

Idempotent: guarded by a marker string. Anchored on exact upstream text so an
LVGL bump that rewrites these functions fails the build loudly here rather
than silently shipping full-screen invalidation again.

The library lives in .pio/libdeps/<env>/lvgl, so unlike the framework patches
this one resolves the path through the SCons env. A pristine copy is kept at
lv_meter.c.gm-orig beside the patched file.
"""

import os
import sys

Import("env")  # noqa: F821 -- provided by SCons

MARKER = "GM_METER_INV_PATCH"
ELEM_MARKER = "GM_METER_ELEM_PATCH"

HELPER = (
    "/* " + MARKER + ": partial invalidation for scale-lines indicators. The\n"
    " * upstream else-branch below invalidates the whole meter on every value\n"
    " * change; on a meter sized like the screen that is a full-screen redraw\n"
    " * per telemetry tick. Only the tick-ring sector between the old and new\n"
    " * value can change, widened by one tick pitch per side (values land\n"
    " * between ticks, the recolor applies per whole tick) and by the tick\n"
    " * width radially (line caps). See scripts/patch_lvgl_meter_inv.py. */\n"
    "static void gm_inv_scale_lines(lv_obj_t * obj, lv_meter_indicator_t * indic, int32_t v0, int32_t v1)\n"
    "{\n"
    "    if(v0 == v1) return;\n"
    "    lv_area_t scale_area;\n"
    "    lv_obj_get_content_coords(obj, &scale_area);\n"
    "    lv_coord_t r_out = lv_area_get_width(&scale_area) / 2;\n"
    "    lv_point_t scale_center;\n"
    "    scale_center.x = scale_area.x1 + r_out;\n"
    "    scale_center.y = scale_area.y1 + r_out;\n"
    "    lv_meter_scale_t * scale = indic->scale;\n"
    "    int32_t a0 = lv_map(v0, scale->min, scale->max, scale->rotation, scale->rotation + scale->angle_range);\n"
    "    int32_t a1 = lv_map(v1, scale->min, scale->max, scale->rotation, scale->rotation + scale->angle_range);\n"
    "    /* The larger of the two counts, because this application suppresses\n"
    "     * LVGL's builtin tick rendering by zeroing tick_cnt and stashing the\n"
    "     * real count in tick_major_nth (suppressMeterTicks in eez/actions.cpp;\n"
    "     * a draw-event handler paints the ticks itself and applies the\n"
    "     * scale-lines recolor). On a stock meter tick_major_nth is a small\n"
    "     * every-Nth stride, so the max still picks tick_cnt. */\n"
    "    int32_t cnt = scale->tick_cnt > scale->tick_major_nth ? scale->tick_cnt : scale->tick_major_nth;\n"
    "    int32_t pitch = cnt > 1 ? (scale->angle_range + cnt - 2) / (cnt - 1) : scale->angle_range;\n"
    "    /* Clamped so a scale with degenerate counts widens to a bounded band,\n"
    "     * never to the whole angle_range: a multi-quarter sector gets the\n"
    "     * full-circle box from lv_draw_arc_get_area, which is the full-screen\n"
    "     * invalidation this patch exists to remove. The flat 20 degree ceiling\n"
    "     * assumes every real scale's pitch is under it (currently ~5 degrees);\n"
    "     * a scale with fewer than angle_range/20 ticks would be under-widened\n"
    "     * and could leave its boundary tick stale. Revisit if such a scale\n"
    "     * ever exists. */\n"
    "    if(pitch < 2) pitch = 2;\n"
    "    if(pitch > 20) pitch = 20;\n"
    "    int32_t start_angle = LV_MIN(a0, a1) - pitch;\n"
    "    int32_t end_angle = LV_MAX(a0, a1) + pitch;\n"
    "    if(start_angle < 0) start_angle = 0;\n"
    "    lv_coord_t len = scale->tick_length;\n"
    "    if(scale->tick_major_length > len) len = scale->tick_major_length;\n"
    "    lv_coord_t tw = scale->tick_width;\n"
    "    if(scale->tick_major_width > tw) tw = scale->tick_major_width;\n"
    "    lv_area_t a;\n"
    "    lv_draw_arc_get_area(scale_center.x, scale_center.y, r_out + scale->r_mod + tw, start_angle, end_angle,\n"
    "                         len + 2 * tw + 4, false, &a);\n"
    "    lv_obj_invalidate_area(obj, &a);\n"
    "}\n"
    "\n"
)
HELPER_NEEDLE = (
    "/* " + MARKER + ": partial invalidation for needle images. Upstream routes\n"
    " * NEEDLE_IMG through inv_line(), which reads needle_line.r_mod from the\n"
    " * type_data union (part of the image pointer for a needle image), so the\n"
    " * area came out as the whole meter. This is the rotated image's bounding\n"
    " * box, as lv_draw_img places it: pivot on the scale centre, one pixel of\n"
    " * margin. See scripts/patch_lvgl_meter_inv.py. */\n"
    "static void gm_inv_needle_img(lv_obj_t * obj, lv_meter_indicator_t * indic, int32_t value)\n"
    "{\n"
    "    const void * src = indic->type_data.needle_img.src;\n"
    "    if(src == NULL) return;\n"
    "    lv_img_header_t info;\n"
    "    if(lv_img_decoder_get_info(src, &info) != LV_RES_OK) {\n"
    "        lv_obj_invalidate(obj);\n"
    "        return;\n"
    "    }\n"
    "    lv_area_t scale_area;\n"
    "    lv_obj_get_content_coords(obj, &scale_area);\n"
    "    lv_coord_t r_out = lv_area_get_width(&scale_area) / 2;\n"
    "    lv_point_t c;\n"
    "    c.x = scale_area.x1 + r_out;\n"
    "    c.y = scale_area.y1 + r_out;\n"
    "    lv_meter_scale_t * scale = indic->scale;\n"
    "    int32_t angle = lv_map(value, scale->min, scale->max, scale->rotation, scale->rotation + scale->angle_range);\n"
    "    angle = angle * 10;\n"
    "    if(angle > 3600) angle -= 3600;\n"
    "    lv_point_t pivot = indic->type_data.needle_img.pivot;\n"
    "    lv_area_t t;\n"
    "    _lv_img_buf_get_transformed_area(&t, info.w, info.h, (int16_t)angle, LV_IMG_ZOOM_NONE, &pivot);\n"
    "    lv_area_t a;\n"
    "    a.x1 = c.x - pivot.x + t.x1 - 1;\n"
    "    a.y1 = c.y - pivot.y + t.y1 - 1;\n"
    "    a.x2 = c.x - pivot.x + t.x2 + 1;\n"
    "    a.y2 = c.y - pivot.y + t.y2 + 1;\n"
    "    lv_obj_invalidate_area(obj, &a);\n"
    "}\n"
    "\n"
)

SET_VALUE_BANNER = (
    "/*=====================\n"
    " * Set indicator value\n"
    " *====================*/\n"
)

# lv_meter_set_indicator_value: both start and end move to `value`, so the
# changed ticks are the union of the two swept sectors, same shape as the
# upstream ARC branch right above it.
SET_VALUE_OLD = (
    "    else if(indic->type == LV_METER_INDICATOR_TYPE_NEEDLE_IMG || indic->type == LV_METER_INDICATOR_TYPE_NEEDLE_LINE) {\n"
    "        inv_line(obj, indic, old_start);\n"
    "        inv_line(obj, indic, old_end);\n"
    "        inv_line(obj, indic, value);\n"
    "    }\n"
    "    else {\n"
    "        lv_obj_invalidate(obj);\n"
    "    }\n"
)
SET_VALUE_NEW = (
    "    else if(indic->type == LV_METER_INDICATOR_TYPE_NEEDLE_LINE) {\n"
    "        inv_line(obj, indic, old_start);\n"
    "        inv_line(obj, indic, old_end);\n"
    "        inv_line(obj, indic, value);\n"
    "    }\n"
    "    else if(indic->type == LV_METER_INDICATOR_TYPE_NEEDLE_IMG) { /* " + MARKER + " */\n"
    "        gm_inv_needle_img(obj, indic, old_start);\n"
    "        gm_inv_needle_img(obj, indic, old_end);\n"
    "        gm_inv_needle_img(obj, indic, value);\n"
    "    }\n"
    "    else if(indic->type == LV_METER_INDICATOR_TYPE_SCALE_LINES) { /* " + MARKER + " */\n"
    "        if(!lv_obj_has_flag(obj, LV_OBJ_FLAG_USER_1)) { /* " + ELEM_MARKER + ": ring owned by a compositor element */\n"
    "            gm_inv_scale_lines(obj, indic, old_start, value);\n"
    "            gm_inv_scale_lines(obj, indic, old_end, value);\n"
    "        }\n"
    "    }\n"
    "    else {\n"
    "        lv_obj_invalidate(obj);\n"
    "    }\n"
)

# lv_meter_set_indicator_start_value and lv_meter_set_indicator_end_value have
# byte-identical bodies for this region, so the anchor matches exactly twice
# and the replacement applies to both.
SET_BOUND_OLD = (
    "    else if(indic->type == LV_METER_INDICATOR_TYPE_NEEDLE_IMG || indic->type == LV_METER_INDICATOR_TYPE_NEEDLE_LINE) {\n"
    "        inv_line(obj, indic, old_value);\n"
    "        inv_line(obj, indic, value);\n"
    "    }\n"
    "    else {\n"
    "        lv_obj_invalidate(obj);\n"
    "    }\n"
)
SET_BOUND_NEW = (
    "    else if(indic->type == LV_METER_INDICATOR_TYPE_NEEDLE_LINE) {\n"
    "        inv_line(obj, indic, old_value);\n"
    "        inv_line(obj, indic, value);\n"
    "    }\n"
    "    else if(indic->type == LV_METER_INDICATOR_TYPE_NEEDLE_IMG) { /* " + MARKER + " */\n"
    "        gm_inv_needle_img(obj, indic, old_value);\n"
    "        gm_inv_needle_img(obj, indic, value);\n"
    "    }\n"
    "    else if(indic->type == LV_METER_INDICATOR_TYPE_SCALE_LINES) { /* " + MARKER + " */\n"
    "        if(!lv_obj_has_flag(obj, LV_OBJ_FLAG_USER_1)) { /* " + ELEM_MARKER + ": ring owned by a compositor element */\n"
    "            gm_inv_scale_lines(obj, indic, old_value, value);\n"
    "        }\n"
    "    }\n"
    "    else {\n"
    "        lv_obj_invalidate(obj);\n"
    "    }\n"
)

# (anchor, replacement, expected occurrence count)
HUNKS = [
    (SET_VALUE_BANNER, HELPER + HELPER_NEEDLE + SET_VALUE_BANNER, 1),
    (SET_VALUE_OLD, SET_VALUE_NEW, 1),
    (SET_BOUND_OLD, SET_BOUND_NEW, 2),
]

# Second concern (gm-2cl.6), folded into the hunks above: while the animation
# composites the screen, DefaultUI paints a dial's tick ring through a
# compositor element (src/display/ui/default/TickRingElement.h) and marks the
# meter with LV_OBJ_FLAG_USER_1. The setters still record the value (the
# element reads start_value and end_value after every ui_tick) but skip the
# scale-lines invalidation, which is what turned every telemetry tick into an
# LVGL draw, a snapshot and an overlay publish. Only the ring is owned: the
# needle image on the same meter is still LVGL's, so its invalidation must
# still happen (a first version returned from the setter before any branch
# and the owned dials' needles froze, 2026-09-08). ELEM_MARKER is what an
# already-patched tree is recognised by.
ELEM_HUNKS = []


def apply_hunks(path, marker, hunks, text):
    if marker in text:
        print("patch_lvgl_meter_inv: %s already applied (%s)" % (marker, path))
        return text, False
    for old, new, count in hunks:
        found = text.count(old)
        if found != count:
            sys.stderr.write(
                "patch_lvgl_meter_inv: %s anchor found %d times (want %d) in %s; "
                "LVGL was updated and this patch needs review. Anchor begins: %r\n"
                % (marker, found, count, path, old[:80]))
            sys.exit(1)
        text = text.replace(old, new)
    return text, True


def apply(path):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    orig = path + ".gm-orig"
    if not os.path.exists(orig):
        with open(orig, "w", encoding="utf-8") as f:
            f.write(text)
    text, a = apply_hunks(path, MARKER, HUNKS, text)
    text, b = apply_hunks(path, ELEM_MARKER, ELEM_HUNKS, text)
    if not (a or b):
        return
    tmp = path + ".gm-tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(text)
    os.replace(tmp, path)
    print("patch_lvgl_meter_inv: patched %s" % path)


def main():
    path = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"),  # noqa: F821
                        "lvgl", "src", "extra", "widgets", "meter", "lv_meter.c")
    if not os.path.isfile(path):
        print("patch_lvgl_meter_inv: lv_meter.c not found for this env; skipping")
        return
    apply(path)


main()
