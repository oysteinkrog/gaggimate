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
String lvgl_helper_get_fs_filename(String filename);
const char *lvgl_helper_get_fs_filename(const char *filename);
