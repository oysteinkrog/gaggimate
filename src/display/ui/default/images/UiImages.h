#ifndef GM_UI_IMAGES_H
#define GM_UI_IMAGES_H

// Icons this UI needs that the EEZ Studio project does not carry.
//
// Everything under ../eez/ is written by the studio, including its own
// images.h and the images[] table the flow engine indexes by asset number, so
// an icon added there would be lost on the next export and an extra row in
// that table would renumber every asset after it. These two live outside that
// tree instead: the C arrays beside this header are produced from the same
// icons/*.svg sources by tools/icon_to_lvgl.py, in the studio's own format,
// and are referenced by pointer from DefaultUI rather than by asset name.
//
// They are the 60x60 play and pause, the start control on the brew, water and
// grind screens and the pause on the status screen. The 40x40 originals are
// still in the studio's tree and still used elsewhere; lv_imgbtn cannot scale
// its source (lv_obj_init_draw_img_dsc pins zoom to LV_IMG_ZOOM_NONE), so a
// larger button needs a larger image.

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_img_dsc_t img_play_60x60;
extern const lv_img_dsc_t img_pause_60x60;

#ifdef __cplusplus
}
#endif

#endif /* GM_UI_IMAGES_H */
