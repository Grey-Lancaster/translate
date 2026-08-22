// Code by Grey and my buddy Claude AI
//
// v0.4.3 - display + touch bring-up: init TFT_eSPI + LVGL, show the boot
// logo, then drop to a blank placeholder screen. WiFi provisioning (via
// TouchWifiProvisioner) lands in v0.4.4.

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>

#include "pins.h"
#include "boot_logo_cyd.h"

TFT_eSPI tft = TFT_eSPI();
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[SCREEN_W * 40];

static void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&color_p->full, w * h, true);
  tft.endWrite();

  lv_disp_flush_ready(disp);
}

static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
  if (!ts.touched()) {
    data->state = LV_INDEV_STATE_REL;
    return;
  }

  TS_Point p = ts.getPoint();

  data->state = LV_INDEV_STATE_PR;
  data->point.x = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, SCREEN_W);
  data->point.y = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_H);
}

// Same pushColors(..., swap=true) call disp_flush() uses for every LVGL
// frame above, just aimed at the raw logo buffer instead of LVGL's draw
// buffer -- reuses a proven-correct call rather than a second, separately-
// verified byte-order convention. Runs before lv_init(), so the tft object
// still owns the screen directly.
static void show_boot_logo() {
  tft.fillScreen(TFT_BLACK);
  int x = (SCREEN_W - CYD_BOOT_LOGO_SIZE) / 2;
  int y = (SCREEN_H - CYD_BOOT_LOGO_SIZE) / 2;

  tft.startWrite();
  tft.setAddrWindow(x, y, CYD_BOOT_LOGO_SIZE, CYD_BOOT_LOGO_SIZE);
  tft.pushColors((uint16_t *)cyd_boot_logo_map, CYD_BOOT_LOGO_SIZE * CYD_BOOT_LOGO_SIZE, true);
  tft.endWrite();

  delay(1500);
}

void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(3);

  SPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin();
  ts.setRotation(3);

  show_boot_logo();

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf1, nullptr, SCREEN_W * 40);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_W;
  disp_drv.ver_res = SCREEN_H;
  disp_drv.flush_cb = disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touchpad_read;
  lv_indev_drv_register(&indev_drv);

  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

  Serial.println("translate firmware v0.4.3 - display + boot logo OK");
}

void loop() {
  lv_timer_handler();
  delay(5);
}
