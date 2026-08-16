/**
 * @file      LV_Helper.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  Shenzhen Xin Yuan Electronic Technology Co., Ltd
 * @date      2024-01-22
 *
 */
#pragma once
#include "Display.h"
#include <Arduino.h>
#include <lvgl.h>

void enable_amoled_black_theme_override(lv_disp_t *disp);
void beginLvglHelper(Display &board, bool debug = false);
// While true, LVGL flushes are dropped (rendered to the draw buffer but never
// pushed to the panel). Used while the sleep animation task owns the panel so
// widget updates can't race the plasma frames on screen.
void lvgl_helper_suppress_flush(bool suppress);

// Union of the areas LVGL re-rendered while flushing was suppressed, in screen
// coordinates. Returns false and leaves *out untouched when nothing has been
// redrawn since the last call; taking the value clears the accumulator.
//
// The suppressed flush is the exact hook for this: LVGL calls it once per
// redrawn area, so what it is handed IS the invalidated region, already merged
// and clipped by LVGL's own refresh logic. Both this and the caller run on the
// UI task (lv_timer_handler), so the accumulator needs no locking.
bool lvgl_helper_take_dirty(lv_area_t *out);
String lvgl_helper_get_fs_filename(String filename);
const char *lvgl_helper_get_fs_filename(const char *filename);
