// Code by Grey and my buddy Claude AI
//
// Custom LVGL fonts (Montserrat 10/12/14, generated with lv_font_conv from
// the same Montserrat-Medium.ttf LVGL's own built-in fonts use) extending
// the glyph range to include the Latin-1 Supplement block -- LVGL's
// built-in lv_font_montserrat_* fonts only cover ASCII (0x20-0x7F) plus a
// handful of symbol icons, with no umlauts (a/o/u-umlaut, sharp s) at all.
// German translations need those routinely; without this, any umlaut in
// translated text renders as a missing-glyph box.
#pragma once
#include <lvgl.h>

extern lv_font_t lv_font_translate_10;
extern lv_font_t lv_font_translate_12;
extern lv_font_t lv_font_translate_14;
