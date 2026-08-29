"""Inline the snapshot overlay's per-pixel writer.

Why this exists
---------------
While the background animation owns the panel, widget redraws are snapshotted
into an RGB565+A8 overlay through LVGL's generic set_px_cb path
(lv_disp_drv_use_generic_set_px_cb -> set_px_true_color_alpha). That path pays
a function-pointer call, a display-driver lookup and a full
lv_color_mix_with_alpha per pixel. QEMU attribution (see
data/qemu-snapshot-profile.md) measured the pixel-blend substage at -32% with
this fast path; the blend loops below write RGB565+A8 (3 bytes/px) directly
and keep the exact branch semantics of set_px_true_color_alpha +
lv_color_mix_with_alpha, so the output bytes are identical:

- foreground >= LV_OPA_MAX or background alpha <= LV_OPA_MIN: the mix returns
  the foreground with the foreground's opacity; written without reading the
  background color bytes.
- foreground <= LV_OPA_MIN: the mix returns the background and the generic
  path rewrites the same bytes it read; skipped as a true no-op.
- everything else: the same inline lv_color_mix_with_alpha the generic calls.

The dispatch in lv_draw_sw_blend_basic recognizes the generic callback by
pointer identity (helper appended to lv_hal_disp.c, where the static function
is visible) so any OTHER custom set_px_cb still takes the generic path.

Idempotent (marker-guarded), anchored on exact upstream text so an LVGL bump
fails loudly. Pristine copies kept beside the files as .gm-orig. The library
lives in .pio/libdeps/<env>/lvgl, resolved through the SCons env like
patch_lvgl_meter_inv.py.
"""

import os
import sys

Import("env")  # noqa: F821 -- provided by SCons

MARKER = "GM_SETPX_FASTPATH"

# --- lv_hal_disp.c: pointer-identity recognizer ------------------------------
HAL_ANCHOR = """    buf_px[0] = res_color.ch.blue;
    buf_px[1] = res_color.ch.green;
    buf_px[2] = res_color.ch.red;
#endif

}
"""
HAL_ADD = HAL_ANCHOR + (
    "\n/* " + MARKER + ": lets lv_draw_sw_blend.c recognize \"this driver uses the\n"
    " * generic TRUE_COLOR_ALPHA writer\" by pointer identity, without exporting the\n"
    " * static function itself. See scripts/patch_lvgl_setpx_fast.py. */\n"
    "bool gm_disp_uses_generic_true_color_alpha(const lv_disp_drv_t * drv)\n"
    "{\n"
    "    return drv->set_px_cb == set_px_true_color_alpha;\n"
    "}\n"
)

# --- lv_draw_sw_blend.c: prototypes after map_set_px's prototype -------------
BLEND_PROTO_ANCHOR = """static void map_set_px(lv_color_t * dest_buf, const lv_area_t * dest_area, lv_coord_t dest_stride,
                       const lv_color_t * src_buf, lv_coord_t src_stride, lv_opa_t opa,
                       const lv_opa_t * mask, lv_coord_t mask_stride);
"""
BLEND_PROTO_ADD = BLEND_PROTO_ANCHOR + (
    "\n/* " + MARKER + " (see scripts/patch_lvgl_setpx_fast.py) */\n"
    "extern bool gm_disp_uses_generic_true_color_alpha(const lv_disp_drv_t * drv);\n"
    "#if LV_COLOR_DEPTH == 16\n"
    "static void gm_fill_set_px_rgb565a8(lv_color_t * dest_buf, const lv_area_t * blend_area, lv_coord_t dest_stride,\n"
    "                                    lv_color_t color, lv_opa_t opa, const lv_opa_t * mask, lv_coord_t mask_stide);\n"
    "static void gm_map_set_px_rgb565a8(lv_color_t * dest_buf, const lv_area_t * dest_area, lv_coord_t dest_stride,\n"
    "                                   const lv_color_t * src_buf, lv_coord_t src_stride, lv_opa_t opa,\n"
    "                                   const lv_opa_t * mask, lv_coord_t mask_stride);\n"
    "#endif\n"
)

# --- lv_draw_sw_blend.c: dispatch --------------------------------------------
BLEND_DISPATCH_OLD = """    if(disp->driver->set_px_cb) {
        if(dsc->src_buf == NULL) {
            fill_set_px(dest_buf, &blend_area, dest_stride, dsc->color, dsc->opa, mask, mask_stride);
        }
        else {
            map_set_px(dest_buf, &blend_area, dest_stride, src_buf, src_stride, dsc->opa, mask, mask_stride);
        }
    }
"""
BLEND_DISPATCH_NEW = """    if(disp->driver->set_px_cb) {
#if LV_COLOR_DEPTH == 16
        /* """ + MARKER + """: the generic TRUE_COLOR_ALPHA writer costs a
         * function-pointer call and a driver lookup per pixel; write the
         * RGB565+A8 bytes inline instead. Identical output bytes. */
        if(gm_disp_uses_generic_true_color_alpha(disp->driver)) {
            if(dsc->src_buf == NULL) {
                gm_fill_set_px_rgb565a8(dest_buf, &blend_area, dest_stride, dsc->color, dsc->opa, mask, mask_stride);
            }
            else {
                gm_map_set_px_rgb565a8(dest_buf, &blend_area, dest_stride, src_buf, src_stride, dsc->opa, mask,
                                       mask_stride);
            }
        }
        else
#endif
            if(dsc->src_buf == NULL) {
                fill_set_px(dest_buf, &blend_area, dest_stride, dsc->color, dsc->opa, mask, mask_stride);
            }
            else {
                map_set_px(dest_buf, &blend_area, dest_stride, src_buf, src_stride, dsc->opa, mask, mask_stride);
            }
    }
"""

# --- lv_draw_sw_blend.c: implementations appended at end of file -------------
BLEND_IMPL = (
    "\n/* " + MARKER + ": inline writers for the generic TRUE_COLOR_ALPHA layout\n"
    " * (RGB565 + A8, 3 bytes per pixel). Byte-for-byte the same results as\n"
    " * set_px_true_color_alpha + lv_color_mix_with_alpha, in the same branch\n"
    " * order; the fast cases skip the per-pixel indirect call and the reads\n"
    " * whose written-back result would be the bytes already there. */\n"
    "#if LV_COLOR_DEPTH == 16\n"
    "static inline void gm_write_px_rgb565a8(uint8_t * px, lv_color_t color, lv_opa_t opa)\n"
    "{\n"
    "    lv_opa_t bg_opa = px[2];\n"
    "    if(opa >= LV_OPA_MAX || bg_opa <= LV_OPA_MIN) {\n"
    "        px[0] = color.full & 0xff;\n"
    "        px[1] = (color.full >> 8) & 0xff;\n"
    "        px[2] = opa;\n"
    "        return;\n"
    "    }\n"
    "    if(opa <= LV_OPA_MIN) return; /*the mix returns the background: a no-op rewrite*/\n"
    "    lv_color_t bg_color;\n"
    "    lv_color_t res_color;\n"
    "    bg_color.full = px[0] + (px[1] << 8);\n"
    "    lv_color_mix_with_alpha(bg_color, bg_opa, color, opa, &res_color, &px[2]);\n"
    "    if(px[2] <= LV_OPA_MIN) return;\n"
    "    px[0] = res_color.full & 0xff;\n"
    "    px[1] = (res_color.full >> 8) & 0xff;\n"
    "}\n"
    "\n"
    "static void gm_fill_set_px_rgb565a8(lv_color_t * dest_buf, const lv_area_t * blend_area, lv_coord_t dest_stride,\n"
    "                                    lv_color_t color, lv_opa_t opa, const lv_opa_t * mask, lv_coord_t mask_stide)\n"
    "{\n"
    "    uint8_t * buf8 = (uint8_t *)dest_buf;\n"
    "    int32_t x;\n"
    "    int32_t y;\n"
    "    if(mask == NULL) {\n"
    "        for(y = blend_area->y1; y <= blend_area->y2; y++) {\n"
    "            uint8_t * px = buf8 + ((size_t)y * dest_stride + blend_area->x1) * 3;\n"
    "            for(x = blend_area->x1; x <= blend_area->x2; x++) {\n"
    "                gm_write_px_rgb565a8(px, color, opa);\n"
    "                px += 3;\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    else {\n"
    "        int32_t w = lv_area_get_width(blend_area);\n"
    "        int32_t h = lv_area_get_height(blend_area);\n"
    "        for(y = 0; y < h; y++) {\n"
    "            uint8_t * px = buf8 + ((size_t)(blend_area->y1 + y) * dest_stride + blend_area->x1) * 3;\n"
    "            for(x = 0; x < w; x++) {\n"
    "                if(mask[x]) {\n"
    "                    gm_write_px_rgb565a8(px, color, (lv_opa_t)((uint32_t)((uint32_t)opa * mask[x]) >> 8));\n"
    "                }\n"
    "                px += 3;\n"
    "            }\n"
    "            mask += mask_stide;\n"
    "        }\n"
    "    }\n"
    "}\n"
    "\n"
    "static void gm_map_set_px_rgb565a8(lv_color_t * dest_buf, const lv_area_t * dest_area, lv_coord_t dest_stride,\n"
    "                                   const lv_color_t * src_buf, lv_coord_t src_stride, lv_opa_t opa,\n"
    "                                   const lv_opa_t * mask, lv_coord_t mask_stride)\n"
    "{\n"
    "    uint8_t * buf8 = (uint8_t *)dest_buf;\n"
    "    int32_t w = lv_area_get_width(dest_area);\n"
    "    int32_t h = lv_area_get_height(dest_area);\n"
    "    int32_t x;\n"
    "    int32_t y;\n"
    "    if(mask == NULL) {\n"
    "        for(y = 0; y < h; y++) {\n"
    "            uint8_t * px = buf8 + ((size_t)(dest_area->y1 + y) * dest_stride + dest_area->x1) * 3;\n"
    "            for(x = 0; x < w; x++) {\n"
    "                gm_write_px_rgb565a8(px, src_buf[x], opa);\n"
    "                px += 3;\n"
    "            }\n"
    "            src_buf += src_stride;\n"
    "        }\n"
    "    }\n"
    "    else {\n"
    "        for(y = 0; y < h; y++) {\n"
    "            uint8_t * px = buf8 + ((size_t)(dest_area->y1 + y) * dest_stride + dest_area->x1) * 3;\n"
    "            for(x = 0; x < w; x++) {\n"
    "                if(mask[x]) {\n"
    "                    gm_write_px_rgb565a8(px, src_buf[x], (lv_opa_t)((uint32_t)((uint32_t)opa * mask[x]) >> 8));\n"
    "                }\n"
    "                px += 3;\n"
    "            }\n"
    "            mask += mask_stride;\n"
    "            src_buf += src_stride;\n"
    "        }\n"
    "    }\n"
    "}\n"
    "#endif /*LV_COLOR_DEPTH == 16*/\n"
)


def apply_one(path, hunks, append=None):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    if MARKER in text:
        print("patch_lvgl_setpx_fast: already patched (%s)" % path)
        return
    orig = path + ".gm-orig"
    if not os.path.exists(orig):
        with open(orig, "w", encoding="utf-8") as f:
            f.write(text)
    for old, new in hunks:
        found = text.count(old)
        if found != 1:
            sys.stderr.write(
                "patch_lvgl_setpx_fast: anchor found %d times (want 1) in %s; "
                "LVGL was updated and this patch needs review. Anchor begins: %r\n"
                % (found, path, old[:80]))
            sys.exit(1)
        text = text.replace(old, new)
    if append is not None:
        text += append
    tmp = path + ".gm-tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(text)
    os.replace(tmp, path)
    print("patch_lvgl_setpx_fast: patched %s" % path)


def main():
    base = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"), "lvgl", "src")  # noqa: F821
    hal = os.path.join(base, "hal", "lv_hal_disp.c")
    blend = os.path.join(base, "draw", "sw", "lv_draw_sw_blend.c")
    if not os.path.isfile(hal) or not os.path.isfile(blend):
        print("patch_lvgl_setpx_fast: lvgl not found for this env; skipping")
        return
    apply_one(hal, [(HAL_ANCHOR, HAL_ADD)])
    apply_one(blend, [(BLEND_PROTO_ANCHOR, BLEND_PROTO_ADD),
                      (BLEND_DISPATCH_OLD, BLEND_DISPATCH_NEW)], append=BLEND_IMPL)


main()
