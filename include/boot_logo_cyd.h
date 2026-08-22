// Code by Grey and my buddy Claude AI
// Reused verbatim from Grey-Lancaster/Claude_usage common/boot_logo_cyd.h
// (DBN One Page project logo, resized for the CYD-style 240x320 ILI9341 panel).
#pragma once
#include <lvgl.h>
#include <stdint.h>

#define CYD_BOOT_LOGO_SIZE 180

extern const uint8_t cyd_boot_logo_map[];
extern const lv_img_dsc_t cyd_boot_logo;
