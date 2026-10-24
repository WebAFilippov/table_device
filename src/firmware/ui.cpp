#include "ui.h"
#include "config.h"
#include "storage.h"
#include <TFT_eSPI.h>
#include <Wire.h>
#include <esp32-hal-psram.h>
#include <lvgl.h>

extern Motor motor;

static TFT_eSPI tft = TFT_eSPI();

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

static lv_obj_t *lblHeight = nullptr;
static lv_obj_t *lblState = nullptr;
static lv_obj_t *btnPreset[4] = {nullptr, nullptr, nullptr, nullptr};
static lv_obj_t *lblPreset[4] = {nullptr, nullptr, nullptr, nullptr};

static uint32_t noticeUntil = 0;

static const uint8_t FT_ADDR = 0x38;
static lv_coord_t touchX = 0, touchY = 0;

// ---------- тач FT6336U ----------

static uint8_t ftRead(uint8_t reg) {
  Wire.beginTransmission(FT_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return 0xFF;
  if (Wire.requestFrom((uint16_t)FT_ADDR, (uint8_t)1) != 1) return 0xFF;
  return Wire.read();
}

static void touchReset() {
  pinMode(Pin::CTP_RST, OUTPUT);
  digitalWrite(Pin::CTP_RST, LOW);
  delay(20);
  digitalWrite(Pin::CTP_RST, HIGH);
  delay(300);
}

// ---------- драйверы LVGL ----------

static void dispFlush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
  uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)color_p, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(drv);
}

static void touchRead(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  (void)drv;
  uint8_t status = ftRead(0x02);
  bool pressed = (status != 0xFF) && ((status & 0x0F) > 0);
  if (pressed) {
    uint8_t b[4];
    Wire.beginTransmission(FT_ADDR);
    Wire.write(0x03);                     // P1_XH ... P1_YL
    Wire.endTransmission();
    if (Wire.requestFrom((uint16_t)FT_ADDR, (uint8_t)4) == 4) {
      for (int i = 0; i < 4; i++) b[i] = Wire.read();
      int rawX = ((b[0] & 0x0F) << 8) | b[1];   // 0..479 (длинная сторона)
      int rawY = ((b[2] & 0x0F) << 8) | b[3];   // 0..319
      // Ландшафт 480x320: ось X тача = короткая сторона, Y = длинная.
      // (выведено экспериментально в test01)
      touchX = rawY;
      touchY = 319 - rawX;
      // Если нажатия зеркалят: touchX = 479 - rawY; / touchY = rawX;
    }
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touchX;
    data->point.y = touchY;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

// ---------- колбэки кнопок ----------

static void upCb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    motor.cmdManual(true);
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    motor.cmdStop();
  }
}

static void downCb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    motor.cmdManual(false);
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    motor.cmdStop();
  }
}

static void calCb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    motor.cmdCalibrate();
    uiNotice("Calibrating: going down...", 5000);
  }
}

static void presetCb(lv_event_t *e) {
  int n = (int)(intptr_t)lv_event_get_user_data(e);
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_LONG_PRESSED) {
    // долгое нажатие = сохранить текущую высоту в пресет
    if (motor.calibrated()) {
      stPutF(("p" + String(n)).c_str(), motor.posMm());
      char b[64];
      snprintf(b, sizeof(b), "Saved P%d: %.1f cm", n, motor.posMm() / 10.0f);
      uiNotice(b, 2500);
    }
  } else if (code == LV_EVENT_CLICKED) {
    // тап = поехать к пресету
    float mm = stGetF(("p" + String(n)).c_str(), NAN);
    if (!isnan(mm) && motor.calibrated()) {
      motor.cmdGotoMm(mm);
    } else {
      uiNotice("Preset empty", 2000);
    }
  }
}

// ---------- экран ----------

static void createUi() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);

  // Высота крупно (в сантиметрах)
  lblHeight = lv_label_create(scr);
  lv_obj_set_style_text_font(lblHeight, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(lblHeight, lv_color_white(), 0);
  lv_obj_align(lblHeight, LV_ALIGN_TOP_MID, 0, 6);
  lv_label_set_text(lblHeight, "-- cm");

  // Статусная строка
  lblState = lv_label_create(scr);
  lv_obj_set_style_text_font(lblState, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lblState, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_align(lblState, LV_ALIGN_TOP_MID, 0, 82);
  lv_label_set_text(lblState, "Ready");

  // Кнопка калибровки
  lv_obj_t *btnCal = lv_btn_create(scr);
  lv_obj_set_size(btnCal, 80, 50);
  lv_obj_align(btnCal, LV_ALIGN_TOP_RIGHT, -8, 8);
  lv_obj_set_style_bg_color(btnCal, lv_palette_main(LV_PALETTE_ORANGE), 0);
  lv_obj_add_event_cb(btnCal, calCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lblCal = lv_label_create(btnCal);
  lv_label_set_text(lblCal, "CAL");
  lv_obj_center(lblCal);

  // 4 пресета
  for (int i = 0; i < 4; i++) {
    btnPreset[i] = lv_btn_create(scr);
    lv_obj_set_size(btnPreset[i], 106, 70);
    lv_obj_align(btnPreset[i], LV_ALIGN_TOP_LEFT, 10 + i * 116, 118);
    lv_obj_add_event_cb(btnPreset[i], presetCb, LV_EVENT_ALL, (void *)(intptr_t)(i + 1));
    lblPreset[i] = lv_label_create(btnPreset[i]);
    lv_obj_set_style_text_font(lblPreset[i], &lv_font_montserrat_20, 0);
    lv_label_set_text(lblPreset[i], "...");
    lv_obj_center(lblPreset[i]);
  }

  // Вверх / Вниз (удержание)
  lv_obj_t *btnUp = lv_btn_create(scr);
  lv_obj_set_size(btnUp, 210, 100);
  lv_obj_align(btnUp, LV_ALIGN_BOTTOM_LEFT, 15, -15);
  lv_obj_set_style_bg_color(btnUp, lv_palette_main(LV_PALETTE_GREEN), 0);
  lv_obj_add_event_cb(btnUp, upCb, LV_EVENT_ALL, nullptr);
  lv_obj_t *lblUp = lv_label_create(btnUp);
  lv_obj_set_style_text_font(lblUp, &lv_font_montserrat_28, 0);
  lv_label_set_text(lblUp, LV_SYMBOL_UP "\nUP");
  lv_obj_center(lblUp);

  lv_obj_t *btnDown = lv_btn_create(scr);
  lv_obj_set_size(btnDown, 210, 100);
  lv_obj_align(btnDown, LV_ALIGN_BOTTOM_RIGHT, -15, -15);
  lv_obj_set_style_bg_color(btnDown, lv_palette_main(LV_PALETTE_BLUE), 0);
  lv_obj_add_event_cb(btnDown, downCb, LV_EVENT_ALL, nullptr);
  lv_obj_t *lblDown = lv_label_create(btnDown);
  lv_obj_set_style_text_font(lblDown, &lv_font_montserrat_28, 0);
  lv_label_set_text(lblDown, LV_SYMBOL_DOWN "\nDOWN");
  lv_obj_center(lblDown);
}

// ---------- публичное API ----------

void uiBegin() {
  // Подсветка ~60%
  ledcAttach(Pin::LCD_BL, 5000, 8);
  ledcWrite(Pin::LCD_BL, 150);

  tft.init();
  tft.setRotation(1);          // ландшафт 480x320

  Wire.begin(Pin::CTP_SDA, Pin::CTP_SCL);
  touchReset();

  lv_init();

  // Два частичных буфера по 480x60 строк — UI остаётся отзывчивым,
  // а не перерисовывает весь кадр разом (это и вызывало «зависание»).
  const uint32_t bufPx = 480 * 60;
  buf1 = (lv_color_t *)ps_malloc(bufPx * sizeof(lv_color_t));
  buf2 = (lv_color_t *)ps_malloc(bufPx * sizeof(lv_color_t));
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, bufPx);
  Serial0.printf("UI: psramFound=%d buf1=%p buf2=%p\n",
                 psramFound() ? 1 : 0, (void*)buf1, (void*)buf2);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = 480;
  disp_drv.ver_res = 320;
  disp_drv.flush_cb = dispFlush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touchRead;
  lv_indev_drv_register(&indev_drv);

  createUi();
  Serial0.println("UI: init done");
}

void uiNotice(const char *msg, uint32_t ms) {
  lv_label_set_text(lblState, msg);
  lv_obj_set_style_text_color(lblState, lv_palette_main(LV_PALETTE_ORANGE), 0);
  noticeUntil = millis() + ms;
}

void uiEvent(MotorEvent e) {
  switch (e) {
    case MotorEvent::OBSTACLE:      uiNotice("Pinch! Backing off", 3000); break;
    case MotorEvent::ENC_STALL:     uiNotice("Encoder silent!", 5000); break;
    case MotorEvent::CAL_HIT_MIN:   uiNotice("Measure height! 10 s", 9000); break;
    case MotorEvent::CAL_HIT_MAX:   uiNotice("Top reached. Enter setH", 10000); break;
    case MotorEvent::CAL_TIMEOUT:   uiNotice("Calibration failed", 5000); break;
    case MotorEvent::END_LIMIT:     uiNotice("End of travel", 2000); break;
    case MotorEvent::REACHED_TARGET: uiNotice("Done", 1500); break;
    default: break;
  }
}

void uiTask() {
  lv_timer_handler();

  static uint32_t last = 0;
  if (millis() - last < 250) return;
  last = millis();

  // Высота в сантиметрах
  if (motor.calibrated()) {
    char b[16];
    snprintf(b, sizeof(b), "%.1f cm", motor.posMm() / 10.0f);
    lv_label_set_text(lblHeight, b);
  } else {
    lv_label_set_text(lblHeight, "-- cm");
  }

  // Статус (если нет активного уведомления)
  if (millis() >= noticeUntil) {
    lv_obj_set_style_text_color(lblState, lv_palette_main(LV_PALETTE_GREY), 0);
    switch (motor.state()) {
      case MotorState::IDLE:      lv_label_set_text(lblState, "Ready"); break;
      case MotorState::MANUAL:    lv_label_set_text(lblState, "Manual"); break;
      case MotorState::MOVE_TO:   lv_label_set_text(lblState, "Moving..."); break;
      case MotorState::CAL_DOWN:
      case MotorState::CAL_UP:    lv_label_set_text(lblState, "Calibrating..."); break;
      case MotorState::REVERSING: lv_label_set_text(lblState, "Backing off..."); break;
      case MotorState::ERROR:     lv_label_set_text(lblState, "ERROR"); break;
    }
  }

  // Пресеты (в см)
  for (int i = 0; i < 4; i++) {
    float mm = stGetF(("p" + String(i + 1)).c_str(), NAN);
    char b[20];
    if (isnan(mm)) snprintf(b, sizeof(b), "%d\n--", i + 1);
    else           snprintf(b, sizeof(b), "%d\n%.1f", i + 1, mm / 10.0f);
    lv_label_set_text(lblPreset[i], b);
  }
}
