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

Applied through scripts/gm_patch.py, like patch_lvgl_meter_inv.py: each
patched file is derived from a hash-verified pristine copy (kept beside it as
<file>.gm-orig) and carries one gm-patch-vN stamp per patch set, so a tree
patched by an older version of this script is brought up to date instead of
being taken as done. The old marker test left the loadtest
lv_draw_sw_blend.c without the planar writer's row tracking and opaque fill.
Anchored on exact upstream text so an LVGL bump fails loudly. The library
lives in .pio/libdeps/<env>/lvgl, resolved through the SCons env.

Versions
--------
v1 (2026-10-01, the first stamped version) of both patch sets:
GM_SETPX_FASTPATH (lv_hal_disp.c recognizer, RGB565+A8 writers in
lv_draw_sw_blend.c) and GM_SETPX_PLANAR (planar overlay writers with row
extent tracking and the opaque fill). Bump VERSION whenever a hunk changes.
"""

import os
import sys

try:
    Import("env")  # noqa: F821 -- provided by SCons
except NameError:  # imported by scripts/test_gm_patch.py
    env = None
if env is not None:
    sys.path.insert(0, os.path.join(env.subst("$PROJECT_DIR"), "scripts"))
import gm_patch  # noqa: E402

OWNER = "patch_lvgl_setpx_fast"
VERSION = 1
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


# --- GM_SETPX_PLANAR: the animation overlay's planar layout ------------------
# The overlay is an RGB565 plane followed by a 16-bit alpha plane (255 stored
# as 256), gm_overlay_plane_px pixels each; see src/display/drivers/common/
# LV_Helper.h. LVGL renders into it through gm_set_px_planar, and this hunk
# inlines that writer the way the one above inlines the generic one: same
# branch order, same lv_color_mix_with_alpha, recognised by pointer identity.
MARKER2 = "GM_SETPX_PLANAR"
PLANAR_PROTO_ANCHOR = """extern bool gm_disp_uses_generic_true_color_alpha(const lv_disp_drv_t * drv);
"""
PLANAR_PROTO_ADD = PLANAR_PROTO_ANCHOR + (
    "/* " + MARKER2 + " (see scripts/patch_lvgl_setpx_fast.py) */\n"
    "extern bool gm_disp_uses_planar_writer(const lv_disp_drv_t * drv);\n"
    "extern uint32_t gm_overlay_plane_px;\n"
    "#define GM_SNAP_ROWS 512\n"
    "extern uint8_t gm_snap_row_x0[GM_SNAP_ROWS]; /* 4-pixel granules, see LV_Helper.h */\n"
    "extern uint8_t gm_snap_row_x1[GM_SNAP_ROWS];\n"
    "#if LV_COLOR_DEPTH == 16\n"
    "static void gm_fill_set_px_planar(lv_color_t * dest_buf, const lv_area_t * blend_area, lv_coord_t dest_stride,\n"
    "                                  lv_color_t color, lv_opa_t opa, const lv_opa_t * mask, lv_coord_t mask_stide);\n"
    "static void gm_map_set_px_planar(lv_color_t * dest_buf, const lv_area_t * dest_area, lv_coord_t dest_stride,\n"
    "                                 const lv_color_t * src_buf, lv_coord_t src_stride, lv_opa_t opa,\n"
    "                                 const lv_opa_t * mask, lv_coord_t mask_stride);\n"
    "#endif\n"
)
PLANAR_DISPATCH_OLD = """        if(gm_disp_uses_generic_true_color_alpha(disp->driver)) {
"""
PLANAR_DISPATCH_NEW = """        if(gm_disp_uses_planar_writer(disp->driver)) { /* """ + MARKER2 + """ */
            if(dsc->src_buf == NULL) {
                gm_fill_set_px_planar(dest_buf, &blend_area, dest_stride, dsc->color, dsc->opa, mask, mask_stride);
            }
            else {
                gm_map_set_px_planar(dest_buf, &blend_area, dest_stride, src_buf, src_stride, dsc->opa, mask,
                                     mask_stride);
            }
        }
        else if(gm_disp_uses_generic_true_color_alpha(disp->driver)) {
"""
PLANAR_IMPL = (
    "\n/* " + MARKER2 + ": inline writers for the planar overlay (RGB565 plane, then a\n"
    " * 16-bit alpha plane with 255 stored as 256, gm_overlay_plane_px pixels each).\n"
    " * Same branch order and mix as gm_write_px_rgb565a8 above. */\n"
    "#if LV_COLOR_DEPTH == 16\n"
    "static inline void gm_write_px_planar(uint16_t * col, uint16_t * a16, lv_color_t color, lv_opa_t opa)\n"
    "{\n"
    "    uint16_t bg16 = *a16;\n"
    "    lv_opa_t bg_opa = bg16 > 255 ? 255 : (lv_opa_t)bg16;\n"
    "    if(opa >= LV_OPA_MAX || bg_opa <= LV_OPA_MIN) {\n"
    "        *col = color.full;\n"
    "        *a16 = opa == 255 ? 256 : opa;\n"
    "        return;\n"
    "    }\n"
    "    if(opa <= LV_OPA_MIN) return;\n"
    "    lv_color_t bg_color;\n"
    "    lv_color_t res_color;\n"
    "    lv_opa_t res_a;\n"
    "    bg_color.full = *col;\n"
    "    lv_color_mix_with_alpha(bg_color, bg_opa, color, opa, &res_color, &res_a);\n"
    "    *a16 = res_a == 255 ? 256 : res_a;\n"
    "    if(res_a <= LV_OPA_MIN) return;\n"
    "    *col = res_color.full;\n"
    "}\n"
    "\n"
    "/* Row extent bookkeeping (LV_Helper.h): true when nothing has been written\n"
    " * to [x1, x2] of buffer row y in this snapshot, so its alpha is zero and a\n"
    " * write there needs no read. Always grows the extent afterwards. */\n"
    "static inline uint8_t gm_snap_gran(int32_t x)\n"
    "{\n"
    "    if(x < 0) x = 0;\n"
    "    x >>= 2;\n"
    "    return x > 254 ? 254 : (uint8_t)x;\n"
    "}\n"
    "static inline int gm_snap_row_fresh(int32_t y, int32_t x1, int32_t x2)\n"
    "{\n"
    "    if(y < 0 || y >= GM_SNAP_ROWS) return 0;\n"
    "    return gm_snap_gran(x2) < gm_snap_row_x0[y] || gm_snap_gran(x1) > gm_snap_row_x1[y];\n"
    "}\n"
    "static inline void gm_snap_row_note(int32_t y, int32_t x1, int32_t x2)\n"
    "{\n"
    "    uint8_t g1;\n"
    "    uint8_t g2;\n"
    "    if(y < 0 || y >= GM_SNAP_ROWS) return;\n"
    "    g1 = gm_snap_gran(x1);\n"
    "    g2 = gm_snap_gran(x2);\n"
    "    if(g1 < gm_snap_row_x0[y]) gm_snap_row_x0[y] = g1;\n"
    "    if(g2 > gm_snap_row_x1[y]) gm_snap_row_x1[y] = g2;\n"
    "}\n"
    "\n"
    "static void gm_fill_set_px_planar(lv_color_t * dest_buf, const lv_area_t * blend_area, lv_coord_t dest_stride,\n"
    "                                  lv_color_t color, lv_opa_t opa, const lv_opa_t * mask, lv_coord_t mask_stide)\n"
    "{\n"
    "    uint16_t * colp = (uint16_t *)dest_buf;\n"
    "    uint16_t * a16p = colp + gm_overlay_plane_px;\n"
    "    int32_t x;\n"
    "    int32_t y;\n"
    "    if(mask == NULL && opa > LV_OPA_MIN && opa < LV_OPA_MAX) {\n"
    "        /* Translucent fill: rows nothing has touched yet take colour and\n"
    "         * alpha straight, without reading the alpha plane. */\n"
    "        uint16_t a16v = opa;\n"
    "        for(y = blend_area->y1; y <= blend_area->y2; y++) {\n"
    "            size_t i = (size_t)y * dest_stride + blend_area->x1;\n"
    "            if(gm_snap_row_fresh(y, blend_area->x1, blend_area->x2)) {\n"
    "                for(x = blend_area->x1; x <= blend_area->x2; x++, i++) {\n"
    "                    colp[i] = color.full;\n"
    "                    a16p[i] = a16v;\n"
    "                }\n"
    "            }\n"
    "            else {\n"
    "                for(x = blend_area->x1; x <= blend_area->x2; x++, i++) {\n"
    "                    gm_write_px_planar(colp + i, a16p + i, color, opa);\n"
    "                }\n"
    "            }\n"
    "            gm_snap_row_note(y, blend_area->x1, blend_area->x2);\n"
    "        }\n"
    "    }\n"
    "    else if(mask == NULL && opa >= LV_OPA_MAX) {\n"
    "        /* Opaque fill: nothing to read, every pixel is colour and alpha 256.\n"
    "         * Two pixels per 32-bit store where the row is 4-byte aligned. */\n"
    "        uint32_t col2 = ((uint32_t)color.full << 16) | color.full;\n"
    "        uint16_t a1 = opa == 255 ? 256 : opa; /* the writer's store rule for opa >= LV_OPA_MAX */\n"
    "        uint32_t a2 = ((uint32_t)a1 << 16) | a1;\n"
    "        int32_t w = blend_area->x2 - blend_area->x1 + 1;\n"
    "        for(y = blend_area->y1; y <= blend_area->y2; y++) {\n"
    "            size_t i = (size_t)y * dest_stride + blend_area->x1;\n"
    "            uint16_t * c = colp + i;\n"
    "            uint16_t * a = a16p + i;\n"
    "            int32_t n = w;\n"
    "            if(((uintptr_t)c & 2) && n > 0) {\n"
    "                *c++ = color.full;\n"
    "                *a++ = a1;\n"
    "                n--;\n"
    "            }\n"
    "            uint32_t * c32 = (uint32_t *)c;\n"
    "            uint32_t * a32 = (uint32_t *)a;\n"
    "            for(; n >= 2; n -= 2) {\n"
    "                *c32++ = col2;\n"
    "                *a32++ = a2;\n"
    "            }\n"
    "            if(n) {\n"
    "                *(uint16_t *)c32 = color.full;\n"
    "                *(uint16_t *)a32 = a1;\n"
    "            }\n"
    "            gm_snap_row_note(y, blend_area->x1, blend_area->x2);\n"
    "        }\n"
    "    }\n"
    "    else if(mask == NULL) {\n"
    "        /* opa <= LV_OPA_MIN: the writer stores nothing. */\n"
    "    }\n"
    "    else {\n"
    "        int32_t w = lv_area_get_width(blend_area);\n"
    "        int32_t h = lv_area_get_height(blend_area);\n"
    "        for(y = 0; y < h; y++) {\n"
    "            int32_t by = blend_area->y1 + y;\n"
    "            size_t i = (size_t)by * dest_stride + blend_area->x1;\n"
    "            if(gm_snap_row_fresh(by, blend_area->x1, blend_area->x2)) {\n"
    "                /* Untouched row: masked coverage lands straight, the same\n"
    "                 * (opa * mask) >> 8 the writer would store over alpha 0. */\n"
    "                for(x = 0; x < w; x++, i++) {\n"
    "                    if(mask[x]) {\n"
    "                        lv_opa_t a = (lv_opa_t)((uint32_t)((uint32_t)opa * mask[x]) >> 8);\n"
    "                        colp[i] = color.full;\n"
    "                        a16p[i] = a == 255 ? 256 : a;\n"
    "                    }\n"
    "                }\n"
    "            }\n"
    "            else {\n"
    "                for(x = 0; x < w; x++, i++) {\n"
    "                    if(mask[x]) {\n"
    "                        gm_write_px_planar(colp + i, a16p + i, color, (lv_opa_t)((uint32_t)((uint32_t)opa * mask[x]) >> 8));\n"
    "                    }\n"
    "                }\n"
    "            }\n"
    "            gm_snap_row_note(by, blend_area->x1, blend_area->x2);\n"
    "            mask += mask_stide;\n"
    "        }\n"
    "    }\n"
    "}\n"
    "\n"
    "static void gm_map_set_px_planar(lv_color_t * dest_buf, const lv_area_t * dest_area, lv_coord_t dest_stride,\n"
    "                                 const lv_color_t * src_buf, lv_coord_t src_stride, lv_opa_t opa,\n"
    "                                 const lv_opa_t * mask, lv_coord_t mask_stride)\n"
    "{\n"
    "    uint16_t * colp = (uint16_t *)dest_buf;\n"
    "    uint16_t * a16p = colp + gm_overlay_plane_px;\n"
    "    int32_t w = lv_area_get_width(dest_area);\n"
    "    int32_t h = lv_area_get_height(dest_area);\n"
    "    int32_t x;\n"
    "    int32_t y;\n"
    "    if(mask == NULL) {\n"
    "        for(y = 0; y < h; y++) {\n"
    "            size_t i = (size_t)(dest_area->y1 + y) * dest_stride + dest_area->x1;\n"
    "            if(opa >= LV_OPA_MAX) {\n"
    "                /* Opaque image row: a straight copy, alpha as the writer stores it. */\n"
    "                uint16_t a1 = opa == 255 ? 256 : opa;\n"
    "                lv_memcpy(colp + i, src_buf, (size_t)w * 2);\n"
    "                for(x = 0; x < w; x++) a16p[i + x] = a1;\n"
    "            }\n"
    "            else {\n"
    "                for(x = 0; x < w; x++, i++) {\n"
    "                    gm_write_px_planar(colp + i, a16p + i, src_buf[x], opa);\n"
    "                }\n"
    "            }\n"
    "            gm_snap_row_note(dest_area->y1 + y, dest_area->x1, dest_area->x2);\n"
    "            src_buf += src_stride;\n"
    "        }\n"
    "    }\n"
    "    else {\n"
    "        for(y = 0; y < h; y++) {\n"
    "            size_t i = (size_t)(dest_area->y1 + y) * dest_stride + dest_area->x1;\n"
    "            for(x = 0; x < w; x++, i++) {\n"
    "                if(mask[x]) {\n"
    "                    gm_write_px_planar(colp + i, a16p + i, src_buf[x], (lv_opa_t)((uint32_t)((uint32_t)opa * mask[x]) >> 8));\n"
    "                }\n"
    "            }\n"
    "            gm_snap_row_note(dest_area->y1 + y, dest_area->x1, dest_area->x2);\n"
    "            mask += mask_stride;\n"
    "            src_buf += src_stride;\n"
    "        }\n"
    "    }\n"
    "}\n"
    "#endif /*LV_COLOR_DEPTH == 16*/\n"
)
# The two writer blocks go at the end of the file. gm_patch only replaces
# anchored text, so the fast-path block anchors on upstream's last lines and
# the planar block anchors on the fast-path block, which keeps the order the
# old appending script produced.
BLEND_TAIL = """    if(opa == LV_OPA_COVER) return fg;

    return lv_color_mix(fg, bg, opa);
}

#endif
"""

HAL_PATCH = gm_patch.Patch(MARKER, VERSION, [(HAL_ANCHOR, HAL_ADD, 1)])
BLEND_PATCHES = [
    gm_patch.Patch(MARKER, VERSION, [
        (BLEND_PROTO_ANCHOR, BLEND_PROTO_ADD, 1),
        (BLEND_DISPATCH_OLD, BLEND_DISPATCH_NEW, 1),
        (BLEND_TAIL, BLEND_TAIL + BLEND_IMPL, 1),
    ]),
    gm_patch.Patch(MARKER2, VERSION, [
        (PLANAR_PROTO_ANCHOR, PLANAR_PROTO_ADD, 1),
        (PLANAR_DISPATCH_OLD, PLANAR_DISPATCH_NEW, 1),
        (BLEND_IMPL, BLEND_IMPL + PLANAR_IMPL, 1),
    ]),
]

# (path under the env's lvgl libdep, sha256 of the upstream LVGL 8.4.0 file,
# patch sets in order)
TARGETS = [
    (("src", "hal", "lv_hal_disp.c"),
     {"042607aab514f55fc69e54bb6c02187ead284cd8bb13e686fe11635c7b14c0d5"}, [HAL_PATCH]),
    (("src", "draw", "sw", "lv_draw_sw_blend.c"),
     {"fca53b473937735d0d1af866eb149b105c6f241d67e3f06c3f1fcfdf1352ca7d"}, BLEND_PATCHES),
]


def main():
    base = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"), "lvgl")  # noqa: F821
    paths = [(os.path.join(base, *rel), sha, patches) for rel, sha, patches in TARGETS]
    if not all(os.path.isfile(path) for path, _sha, _patches in paths):
        print("gm-patch: %s: lvgl not found for this env; skipping" % OWNER)
        return
    for path, sha, patches in paths:
        gm_patch.run(OWNER, path, sha, patches)


if env is not None:
    main()
