// Code by Grey and my buddy Claude AI
//
// LVGL bring-up: same confirmed-working TFT_eSPI init as the plain-text
// test, now driving LVGL's display flush callback and a real lv_label
// instead of raw tft.print(). No touch, no boot logo yet -- next step
// after this is confirmed on hardware.

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>

#include "pins.h"

TFT_eSPI tft = TFT_eSPI();

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[SCREEN_W * 10];

static void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&color_p->full, w * h, true);
  tft.endWrite();

  lv_disp_flush_ready(disp);
}

void setup() {
  Serial.begin(115200);

  lv_init();
  tft.begin();
  tft.setRotation(1);
  lv_disp_draw_buf_init(&draw_buf, buf1, nullptr, SCREEN_W * 10);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_H; // rotated 1 -> landscape, hor_res is the taller native dimension
  disp_drv.ver_res = SCREEN_W;
  disp_drv.flush_cb = disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  lv_obj_t *label = lv_label_create(lv_scr_act());
  lv_label_set_text(label, "Hello Grey");
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

  Serial.println("translate firmware - LVGL Hello Grey OK");
}

void loop() {
  lv_timer_handler();
  delay(5);
}
