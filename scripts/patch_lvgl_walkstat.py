"""Device-side attribution counters for the snapshot redraw walk (GM_WALKSTAT).

Why this exists
---------------
GM_UISTAT's draw= bucket (the lv_obj_redraw wall time of a snapshot pass) runs
64-78 ms per refresh on the device for ~24k dirty pixels, and the meter
tick-draw handler (GM_METERSTAT, eez/actions.cpp) accounts for only ~25% of
it. The remaining ~50 ms is inside LVGL. The QEMU-side attribution
(data/qemu-snapshot-profile.md) split the walk into event dispatch / style
resolution / descriptor init on flat memory, but PSRAM pointer-chasing is
expected to skew the device split, and QEMU percentages have already been
falsified on device once (the style-cache candidate). This patch measures the
split where it matters.

Three wraps, counting only while gm_ws_active is set (DefaultUI sets it
around the snapshot lv_obj_redraw, so the counters partition exactly the
draw= bucket, not general LVGL activity like input processing or layout):

- lv_event_send (lv_event.c): total event-dispatch wall time, depth-guarded
  so nested sends are not double-counted. In LVGL 8 dispatch IS drawing for
  the built-in widgets, so this includes the leaf draw calls.
- lv_obj_get_style_prop (lv_obj_style.c): every style lookup. The
  inheritance walk is a loop, not self-recursion, so no depth guard.
- lv_draw_img (lv_draw_img.c): image blits, i.e. the three rotated meter
  needles, the prime suspect for the unattributed bulk (per-pixel transform
  in the sw renderer).

Everything is inside #ifdef GM_TOUCH_PROBE, so only loadtest builds compile
the counters even though the patch is applied to every env's libdep copy.
Consumed and reset by the GM_UISTAT logger in DefaultUI.cpp.

Idempotent: guarded by a marker string. Anchored on exact upstream text so an
LVGL bump that rewrites these functions fails the build loudly. Pristine
copies are kept at <file>.gm-orig beside each patched file.
"""

import os
import sys

Import("env")  # noqa: F821 -- provided by SCons

MARKER = "GM_WALKSTAT_PATCH"

EVENT_OLD = (
    "lv_res_t lv_event_send(lv_obj_t * obj, lv_event_code_t event_code, void * param)\n"
    "{\n"
    "    if(obj == NULL) return LV_RES_OK;\n"
)
EVENT_NEW = (
    "/* " + MARKER + ": walk attribution counters. Defined here, consumed by the\n"
    " * GM_UISTAT logger in DefaultUI.cpp; gm_ws_active is set there around the\n"
    " * snapshot lv_obj_redraw so these partition exactly the draw= bucket.\n"
    " * See scripts/patch_lvgl_walkstat.py. */\n"
    "#ifdef GM_TOUCH_PROBE\n"
    "#include \"esp_timer.h\"\n"
    "bool gm_ws_active;\n"
    "uint32_t gm_ws_ev_calls; int64_t gm_ws_ev_us;\n"
    "uint32_t gm_ws_style_calls; int64_t gm_ws_style_us;\n"
    "uint32_t gm_ws_img_calls; int64_t gm_ws_img_us;\n"
    "uint32_t gm_ws_rect_calls; int64_t gm_ws_rect_us;\n"
    "uint32_t gm_ws_rectr_calls; int64_t gm_ws_rectr_us; int64_t gm_ws_rect_max_us;\n"
    "uint32_t gm_ws_label_calls; int64_t gm_ws_label_us;\n"
    "uint32_t gm_ws_line_calls; int64_t gm_ws_line_us;\n"
    "uint32_t gm_ws_arc_calls; int64_t gm_ws_arc_us;\n"
    "static int gm_ws_ev_depth;\n"
    "#endif\n"
    "\n"
    "static lv_res_t gm_ws_event_send_inner(lv_obj_t * obj, lv_event_code_t event_code, void * param);\n"
    "\n"
    "lv_res_t lv_event_send(lv_obj_t * obj, lv_event_code_t event_code, void * param)\n"
    "{\n"
    "#ifdef GM_TOUCH_PROBE\n"
    "    if(gm_ws_active && gm_ws_ev_depth++ == 0) {\n"
    "        const int64_t t0 = esp_timer_get_time();\n"
    "        const lv_res_t r = gm_ws_event_send_inner(obj, event_code, param);\n"
    "        gm_ws_ev_us += esp_timer_get_time() - t0;\n"
    "        gm_ws_ev_calls++;\n"
    "        gm_ws_ev_depth--;\n"
    "        return r;\n"
    "    }\n"
    "    if(gm_ws_active) { /* nested: depth was incremented by the test above */\n"
    "        const lv_res_t r = gm_ws_event_send_inner(obj, event_code, param);\n"
    "        gm_ws_ev_depth--;\n"
    "        return r;\n"
    "    }\n"
    "#endif\n"
    "    return gm_ws_event_send_inner(obj, event_code, param);\n"
    "}\n"
    "\n"
    "static lv_res_t gm_ws_event_send_inner(lv_obj_t * obj, lv_event_code_t event_code, void * param)\n"
    "{\n"
    "    if(obj == NULL) return LV_RES_OK;\n"
)

STYLE_OLD = (
    "lv_style_value_t lv_obj_get_style_prop(const lv_obj_t * obj, lv_part_t part, lv_style_prop_t prop)\n"
    "{\n"
    "    lv_style_value_t value_act;\n"
)
STYLE_NEW = (
    "/* " + MARKER + ": see lv_event.c for the counter definitions. */\n"
    "#ifdef GM_TOUCH_PROBE\n"
    "#include \"esp_timer.h\"\n"
    "extern bool gm_ws_active;\n"
    "extern uint32_t gm_ws_style_calls; extern int64_t gm_ws_style_us;\n"
    "#endif\n"
    "\n"
    "static lv_style_value_t gm_ws_get_style_prop_inner(const lv_obj_t * obj, lv_part_t part, lv_style_prop_t prop);\n"
    "\n"
    "lv_style_value_t lv_obj_get_style_prop(const lv_obj_t * obj, lv_part_t part, lv_style_prop_t prop)\n"
    "{\n"
    "#ifdef GM_TOUCH_PROBE\n"
    "    if(gm_ws_active) {\n"
    "        const int64_t t0 = esp_timer_get_time();\n"
    "        const lv_style_value_t v = gm_ws_get_style_prop_inner(obj, part, prop);\n"
    "        gm_ws_style_us += esp_timer_get_time() - t0;\n"
    "        gm_ws_style_calls++;\n"
    "        return v;\n"
    "    }\n"
    "#endif\n"
    "    return gm_ws_get_style_prop_inner(obj, part, prop);\n"
    "}\n"
    "\n"
    "static lv_style_value_t gm_ws_get_style_prop_inner(const lv_obj_t * obj, lv_part_t part, lv_style_prop_t prop)\n"
    "{\n"
    "    lv_style_value_t value_act;\n"
)

IMG_OLD = (
    "void lv_draw_img(lv_draw_ctx_t * draw_ctx, const lv_draw_img_dsc_t * dsc, const lv_area_t * coords, const void * src)\n"
    "{\n"
    "    if(src == NULL) {\n"
)
IMG_NEW = (
    "/* " + MARKER + ": see lv_event.c for the counter definitions. */\n"
    "#ifdef GM_TOUCH_PROBE\n"
    "#include \"esp_timer.h\"\n"
    "extern bool gm_ws_active;\n"
    "extern uint32_t gm_ws_img_calls; extern int64_t gm_ws_img_us;\n"
    "#endif\n"
    "\n"
    "static void gm_ws_draw_img_inner(lv_draw_ctx_t * draw_ctx, const lv_draw_img_dsc_t * dsc, const lv_area_t * coords,\n"
    "                                 const void * src);\n"
    "\n"
    "void lv_draw_img(lv_draw_ctx_t * draw_ctx, const lv_draw_img_dsc_t * dsc, const lv_area_t * coords, const void * src)\n"
    "{\n"
    "#ifdef GM_TOUCH_PROBE\n"
    "    if(gm_ws_active) {\n"
    "        const int64_t t0 = esp_timer_get_time();\n"
    "        gm_ws_draw_img_inner(draw_ctx, dsc, coords, src);\n"
    "        gm_ws_img_us += esp_timer_get_time() - t0;\n"
    "        gm_ws_img_calls++;\n"
    "        return;\n"
    "    }\n"
    "#endif\n"
    "    gm_ws_draw_img_inner(draw_ctx, dsc, coords, src);\n"
    "}\n"
    "\n"
    "static void gm_ws_draw_img_inner(lv_draw_ctx_t * draw_ctx, const lv_draw_img_dsc_t * dsc, const lv_area_t * coords,\n"
    "                                 const void * src)\n"
    "{\n"
    "    if(src == NULL) {\n"
)

def leaf_wrap(orig_name, ret_open, signature_first_line, signature_rest, arg_names, us_var, calls_var,
              body_first_line):
    """Build (old, new) for a void leaf-draw wrap. The anchor is the exact
    signature plus the body's first line; the replacement renames the original
    to an inner and adds a timed wrapper gated on gm_ws_active."""
    sig = signature_first_line + signature_rest
    inner_sig = sig.replace(orig_name + "(", ret_open + "(", 1)
    old = sig + "\n{\n" + body_first_line
    new = (
        "/* " + MARKER + ": see lv_event.c for the counter definitions. */\n"
        "#ifdef GM_TOUCH_PROBE\n"
        "#include \"esp_timer.h\"\n"
        "extern bool gm_ws_active;\n"
        "extern uint32_t " + calls_var + "; extern int64_t " + us_var + ";\n"
        "#endif\n"
        "\n"
        "static " + inner_sig + ";\n"
        "\n"
        + sig + "\n{\n"
        "#ifdef GM_TOUCH_PROBE\n"
        "    if(gm_ws_active) {\n"
        "        const int64_t t0 = esp_timer_get_time();\n"
        "        " + ret_open + "(" + arg_names + ");\n"
        "        " + us_var + " += esp_timer_get_time() - t0;\n"
        "        " + calls_var + "++;\n"
        "        return;\n"
        "    }\n"
        "#endif\n"
        "    " + ret_open + "(" + arg_names + ");\n"
        "}\n"
        "\n"
        "static " + inner_sig + "\n{\n" + body_first_line
    )
    return old, new


# The rect wrap is hand-rolled rather than leaf_wrap: it additionally splits
# time by radius (the mask path suspect) and tracks the worst single call, to
# tell "a few huge circle masks" apart from "many medium background fills".
RECT_OLD = (
    "void lv_draw_rect(lv_draw_ctx_t * draw_ctx, const lv_draw_rect_dsc_t * dsc, const lv_area_t * coords)\n"
    "{\n"
    "    if(lv_area_get_height(coords) < 1 || lv_area_get_width(coords) < 1) return;\n"
)
RECT_NEW = (
    "/* " + MARKER + ": see lv_event.c for the counter definitions. */\n"
    "#ifdef GM_TOUCH_PROBE\n"
    "#include \"esp_timer.h\"\n"
    "#include \"esp_log.h\"\n"
    "extern bool gm_ws_active;\n"
    "extern uint32_t gm_ws_rect_calls; extern int64_t gm_ws_rect_us;\n"
    "extern uint32_t gm_ws_rectr_calls; extern int64_t gm_ws_rectr_us; extern int64_t gm_ws_rect_max_us;\n"
    "#endif\n"
    "\n"
    "static void gm_ws_draw_rect_inner(lv_draw_ctx_t * draw_ctx, const lv_draw_rect_dsc_t * dsc, const lv_area_t * coords);\n"
    "\n"
    "void lv_draw_rect(lv_draw_ctx_t * draw_ctx, const lv_draw_rect_dsc_t * dsc, const lv_area_t * coords)\n"
    "{\n"
    "#ifdef GM_TOUCH_PROBE\n"
    "    if(gm_ws_active) {\n"
    "        const int64_t t0 = esp_timer_get_time();\n"
    "        gm_ws_draw_rect_inner(draw_ctx, dsc, coords);\n"
    "        const int64_t dt = esp_timer_get_time() - t0;\n"
    "        gm_ws_rect_us += dt;\n"
    "        gm_ws_rect_calls++;\n"
    "        if(dsc->radius != 0) {\n"
    "            gm_ws_rectr_us += dt;\n"
    "            gm_ws_rectr_calls++;\n"
    "        }\n"
    "        if(dt > gm_ws_rect_max_us) gm_ws_rect_max_us = dt;\n"
    "        /* Name the heavy hitters: a handful of >2 ms radius-masked rects\n"
    "         * dominate the walk; their coords identify the objects. Rate\n"
    "         * limited so a soak logs the recurring set once, not a stream. */\n"
    "        {\n"
    "            static int gm_ws_big_logged;\n"
    "            if(dt > 2000 && gm_ws_big_logged < 16) {\n"
    "                gm_ws_big_logged++;\n"
    "                ESP_LOGI(\"GM_RECTBIG\", \"dt=%d coords=%d,%d,%d,%d r=%d bgopa=%d\", (int)dt,\n"
    "                         (int)coords->x1, (int)coords->y1, (int)coords->x2, (int)coords->y2,\n"
    "                         (int)dsc->radius, (int)dsc->bg_opa);\n"
    "            }\n"
    "        }\n"
    "        return;\n"
    "    }\n"
    "#endif\n"
    "    gm_ws_draw_rect_inner(draw_ctx, dsc, coords);\n"
    "}\n"
    "\n"
    "static void gm_ws_draw_rect_inner(lv_draw_ctx_t * draw_ctx, const lv_draw_rect_dsc_t * dsc, const lv_area_t * coords)\n"
    "{\n"
    "    if(lv_area_get_height(coords) < 1 || lv_area_get_width(coords) < 1) return;\n"
)

LABEL_OLD, LABEL_NEW = leaf_wrap(
    "lv_draw_label", "gm_ws_draw_label_inner",
    "void LV_ATTRIBUTE_FAST_MEM lv_draw_label(lv_draw_ctx_t * draw_ctx, const lv_draw_label_dsc_t * dsc,",
    "\n                                         const lv_area_t * coords, const char * txt, lv_draw_label_hint_t * hint)",
    "draw_ctx, dsc, coords, txt, hint",
    "gm_ws_label_us", "gm_ws_label_calls",
    "    if(dsc->opa <= LV_OPA_MIN) return;\n")

LINE_OLD, LINE_NEW = leaf_wrap(
    "lv_draw_line", "gm_ws_draw_line_inner",
    "void LV_ATTRIBUTE_FAST_MEM lv_draw_line(struct _lv_draw_ctx_t * draw_ctx, const lv_draw_line_dsc_t * dsc,",
    "\n                                        const lv_point_t * point1, const lv_point_t * point2)",
    "draw_ctx, dsc, point1, point2",
    "gm_ws_line_us", "gm_ws_line_calls",
    "    if(dsc->width == 0) return;\n")

ARC_OLD, ARC_NEW = leaf_wrap(
    "lv_draw_arc", "gm_ws_draw_arc_inner",
    "void lv_draw_arc(lv_draw_ctx_t * draw_ctx, const lv_draw_arc_dsc_t * dsc, const lv_point_t * center, uint16_t radius,",
    "\n                 uint16_t start_angle, uint16_t end_angle)",
    "draw_ctx, dsc, center, radius, start_angle, end_angle",
    "gm_ws_arc_us", "gm_ws_arc_calls",
    "    if(dsc->opa <= LV_OPA_MIN) return;\n")

# path components (under the env's lvgl libdep), hunks as (old, new, count)
FILES = [
    (("src", "core", "lv_event.c"), [(EVENT_OLD, EVENT_NEW, 1)]),
    (("src", "core", "lv_obj_style.c"), [(STYLE_OLD, STYLE_NEW, 1)]),
    (("src", "draw", "lv_draw_img.c"), [(IMG_OLD, IMG_NEW, 1)]),
    (("src", "draw", "lv_draw_rect.c"), [(RECT_OLD, RECT_NEW, 1)]),
    (("src", "draw", "lv_draw_label.c"), [(LABEL_OLD, LABEL_NEW, 1)]),
    (("src", "draw", "lv_draw_line.c"), [(LINE_OLD, LINE_NEW, 1)]),
    (("src", "draw", "lv_draw_arc.c"), [(ARC_OLD, ARC_NEW, 1)]),
]


def apply(path, hunks):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    if MARKER in text:
        print("patch_lvgl_walkstat: already patched (%s)" % path)
        return
    orig = path + ".gm-orig"
    if not os.path.exists(orig):
        with open(orig, "w", encoding="utf-8") as f:
            f.write(text)
    for old, new, count in hunks:
        found = text.count(old)
        if found != count:
            sys.stderr.write(
                "patch_lvgl_walkstat: anchor found %d times (want %d) in %s; "
                "LVGL was updated and this patch needs review. Anchor begins: %r\n"
                % (found, count, path, old[:80]))
            sys.exit(1)
        text = text.replace(old, new)
    tmp = path + ".gm-tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(text)
    os.replace(tmp, path)
    print("patch_lvgl_walkstat: patched %s" % path)


def main():
    base = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"), "lvgl")  # noqa: F821
    if not os.path.isdir(base):
        print("patch_lvgl_walkstat: lvgl libdep not found for this env; skipping")
        return
    for parts, hunks in FILES:
        path = os.path.join(base, *parts)
        if not os.path.isfile(path):
            sys.stderr.write("patch_lvgl_walkstat: %s missing; LVGL layout changed\n" % path)
            sys.exit(1)
        apply(path, hunks)


main()
