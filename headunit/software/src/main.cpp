#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <FT62XXTouchScreen.h>
#include "ui/ui.h"
#include "ui/vars.h"
#include "ui/actions.h"

#include <CAN.h>
#include <can/VESC.h>

#define POLE_PAIRS 3.0
#define WHEEL_CIRCUMFERENCE 2.0*PI*0.15 // meters
#define GEAR_RATIO 9.0/64.0

typedef struct {
  double voltage;
  double soc;
} VoltageSoCPoint;

const VoltageSoCPoint voltage_soc_table[] = {
  {54.6, 100}, {53.3, 90}, {52.0, 80}, {50.7, 70}, {49.4, 60},
  {48.1, 50}, {46.8, 40}, {45.5, 30}, {44.2, 20}, {42.9, 10}, {41.6, 5}, {39.0, 0}
};

#define TX_GPIO_NUM   26
#define RX_GPIO_NUM   35
//#define TEST_MODE

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
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

double estimate_soc_from_voltage(double voltage) {
  const int n_points = sizeof(voltage_soc_table) / sizeof(voltage_soc_table[0]);

  if (voltage >= voltage_soc_table[0].voltage) return 100.0;
  if (voltage <= voltage_soc_table[n_points - 1].voltage) return 0.0;

  for (int i = 0; i < n_points - 1; i++) {
    if (voltage <= voltage_soc_table[i].voltage && voltage >= voltage_soc_table[i+1].voltage) {
      double v1 = voltage_soc_table[i].voltage, s1 = voltage_soc_table[i].soc;
      double v2 = voltage_soc_table[i+1].voltage, s2 = voltage_soc_table[i+1].soc;
      return s1 + ((voltage - v1) * (s2 - s1) / (v2 - v1));
    }
  }

  return 0.0; // fallback, shouldn't get here
}

static uint64_t u64_from_can_msg(const uint8_t m[8]) {
	return ((uint64_t)m[7] << 56) | ((uint64_t)m[6] << 48) | ((uint64_t)m[5] << 40) | ((uint64_t)m[4] << 32) 
		| ((uint64_t)m[3] << 24) | ((uint64_t)m[2] << 16) | ((uint64_t)m[1] << 8) | ((uint64_t)m[0] << 0);
}

static void u64_to_can_msg(const uint64_t u, uint8_t m[8]) {
	m[7] = u >> 56;
	m[6] = u >> 48;
	m[5] = u >> 40;
	m[4] = u >> 32;
	m[3] = u >> 24;
	m[2] = u >> 16;
	m[1] = u >>  8;
	m[0] = u >>  0;
}

void on_receive_can_msg(int packet_size){
  if(CAN.packetRtr() || packet_size <= 0){
    return;
  }

  uint32_t id = CAN.packetId();

  uint8_t dlc = CAN.packetDlc();
  uint8_t can_message_raw[8];
  dbcc_time_stamp_t ts = 0;
  
  for(uint8_t i = 0; i < packet_size; i++){
    int b = CAN.read();
    if(b < 0){
      return;
    }
    can_message_raw[i] = b;
  }
  
  uint64_t can_message_u64 = 0;
  can_message_u64 = u64_from_can_msg(can_message_raw);
  
  can_obj_vesc_h_t unpacked_msg;
  int un_ret = unpack_message(&unpacked_msg, id, can_message_u64, dlc, ts);
  if ( un_ret < 0) {
    return;
  }
  
  switch (id) {
  case 0x1b01:
    double data_bat = 0;
    if (decode_can_0x1b01_Status_InputVoltage_V1(&unpacked_msg, &data_bat)) {
      return;
    }
    set_var_bat_lvl(estimate_soc_from_voltage(data_bat));
    int32_t data_speed = 0;
    if (decode_can_0x1b01_Status_Tachometer_V1(&unpacked_msg, &data_speed)) {
      return;
    }
    double mech_rpm = (double)data_speed * GEAR_RATIO / POLE_PAIRS;
    double speed_kph = (mech_rpm * WHEEL_CIRCUMFERENCE) / 60.0 * 3.6;
    set_var_speed(speed_kph);
    break;
  }
}

void setup() {
  lv_init();

  // Start TouchScreen
  ts.begin();

  draw_buf_1 = heap_caps_malloc(DRAW_BUF_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  disp = lv_tft_espi_create(TFT_WIDTH, TFT_HEIGHT, draw_buf_1, DRAW_BUF_SIZE);
  lv_disp_set_rotation(disp, LV_DISPLAY_ROTATION_90);

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER); // Touchpad should have POINTER type
  lv_indev_set_read_cb(indev, my_touchpad_read);

  ui_init();

  // Turn on backlight
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  CAN.setPins(RX_GPIO_NUM, TX_GPIO_NUM);
  CAN.begin(500E3);
  CAN.onReceive(on_receive_can_msg);
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

  #ifdef TEST_MODE
    if (millis() % 100 == 0) {
      if (get_var_speed() < 40) {
        set_var_speed(get_var_speed() + 1);
      }
      else {
        set_var_speed(0);
      }

      if (get_var_bat_lvl() < 100){
        set_var_bat_lvl(get_var_bat_lvl() + 1);
      }
      else {
        set_var_bat_lvl(0);
      }
    }
#endif
}