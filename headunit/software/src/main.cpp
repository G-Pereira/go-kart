#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <FT62XXTouchScreen.h>
#include "ui/ui.h"

FT62XXTouchScreen ts = FT62XXTouchScreen(TFT_WIDTH, PIN_SDA, PIN_SCL);

/*LVGL draw into this buffer, 1/10 screen size usually works well. The size is in bytes*/
void *draw_buf_1;
unsigned long lastTickMillis = 0;

static lv_display_t *disp;
lv_obj_t *screen;

#define DRAW_BUF_SIZE (TFT_WIDTH * TFT_HEIGHT / 10 * (LV_COLOR_DEPTH / 8))

void my_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data) {
    TouchPoint p = ts.read();
  if (p.touched) {

    data->point.x = p.yPos;
    data->point.y = TFT_HEIGHT - p.xPos;
    Serial.printf("x:%d, y:%d\n", data->point.x, data->point.y);
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void setup() {
  Serial.begin(115200);

  lv_init();

  // Start TouchScreen
  ts.begin();

  draw_buf_1 = heap_caps_malloc(DRAW_BUF_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  disp = lv_tft_espi_create(TFT_WIDTH, TFT_HEIGHT, draw_buf_1, DRAW_BUF_SIZE);
  lv_disp_set_rotation(disp, LV_DISPLAY_ROTATION_270);

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER); // Touchpad should have POINTER type
  lv_indev_set_read_cb(indev, my_touchpad_read);

  ui_init();

  // Turn on backlight
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
}

void loop() {
  lv_timer_handler();
  ui_tick();

  // LVGL Tick Interface
  unsigned int tickPeriod = millis() - lastTickMillis;
  lv_tick_inc(tickPeriod);
  lastTickMillis = millis();

  // LVGL Task Handler
  lv_task_handler();
}