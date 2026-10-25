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

// --- экраны ---
static lv_obj_t *scrMain = nullptr;
static lv_obj_t *scrSettings = nullptr;
static lv_obj_t *scrWizard = nullptr;

// --- главный экран ---
static lv_obj_t *lblHeight = nullptr;
static lv_obj_t *lblState = nullptr;
static lv_obj_t *btnPreset[4] = {nullptr, nullptr, nullptr, nullptr};
static lv_obj_t *lblPreset[4] = {nullptr, nullptr, nullptr, nullptr};

// --- настройки ---
static lv_obj_t *lblBrightnessVal = nullptr;
static lv_obj_t *sliderBrightness = nullptr;
static lv_obj_t *ddSleep = nullptr;

// --- визард калибровки ---
static lv_obj_t *lblWizTitle = nullptr;
static lv_obj_t *lblWizSub = nullptr;
static lv_obj_t *btnWizPrimary = nullptr;
static lv_obj_t *btnWizCancel = nullptr;
static lv_obj_t *lblWizH1 = nullptr;
static lv_obj_t *lblWizH2 = nullptr;

static String wizBuf1, wizBuf2;

// --- яркость и сон ---
static uint8_t brightPct = 60;      // 0..100 %
static uint8_t sleepMin = 0;        // 0 = сон выключен
static bool backlightOn = true;
static uint32_t lastActivity = 0;
static const uint8_t sleepOptions[] = {0, 1, 5, 15, 30};

static uint32_t noticeUntil = 0;

static const uint8_t FT_ADDR = 0x38;
static lv_coord_t touchX = 0, touchY = 0;

// предварительные объявления колбэков
static void upCb(lv_event_t *e);
static void downCb(lv_event_t *e);
static void presetCb(lv_event_t *e);
static void calCb(lv_event_t *e);
static void settingsOpenCb(lv_event_t *e);
static void settingsBackCb(lv_event_t *e);
static void brightnessCb(lv_event_t *e);
static void sleepCb(lv_event_t *e);
static void wizPrimaryCb(lv_event_t *e);
static void wizCancelCb(lv_event_t *e);
static void wizKeyCb(lv_event_t *e);

// ================= тач FT6336U =================

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

// ================= драйверы LVGL =================

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
    lastActivity = millis();
    // пробуждение по тачу
    if (!backlightOn) {
      backlightOn = true;
      ledcWrite(Pin::LCD_BL, (uint32_t)(brightPct * 255 / 100));
    }
    uint8_t b[4];
    Wire.beginTransmission(FT_ADDR);
    Wire.write(0x03);                     // P1_XH ... P1_YL
    Wire.endTransmission();
    if (Wire.requestFrom((uint16_t)FT_ADDR, (uint8_t)4) == 4) {
      for (int i = 0; i < 4; i++) b[i] = Wire.read();
      int rawX = ((b[0] & 0x0F) << 8) | b[1];   // 0..479 (длинная сторона)
      int rawY = ((b[2] & 0x0F) << 8) | b[3];   // 0..319
      // Ландшафт 480x320: ось X тача = короткая сторона, Y = длинная
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

// ================= яркость / сон =================

static void setBrightness(uint8_t pct) {
  brightPct = constrain((int)pct, 0, 100);
  if (backlightOn) ledcWrite(Pin::LCD_BL, (uint32_t)(brightPct * 255 / 100));
}

static void setSleepMin(uint8_t m) {
  sleepMin = m;
  stPutF("sleepMin", (float)m);
}

// ================= уведомления =================

void uiNotice(const char *msg, uint32_t ms) {
  if (lv_scr_act() == scrWizard) {
    lv_label_set_text(lblWizSub, msg);
  } else {
    lv_label_set_text(lblState, msg);
    lv_obj_set_style_text_color(lblState, lv_palette_main(LV_PALETTE_ORANGE), 0);
  }
  noticeUntil = millis() + ms;
}

// ================= визард калибровки =================

enum WizStep : uint8_t {
  WIZ_INTRO,     // предупреждение
  WIZ_DOWN,      // едем вниз
  WIZ_PAUSE,     // нижний упор, пауза
  WIZ_UP,        // едем вверх
  WIZ_VERIFY,    // контрольный спуск вниз
  WIZ_CONFIRM,   // вычисленные значения: подтверждение
  WIZ_INPUT1,    // ручной ввод нижней высоты (запасной режим)
  WIZ_INPUT2,    // ручной ввод верхней высоты
  WIZ_EDITMIN,   // правка нижней высоты (умная калибровка)
  WIZ_DONE,      // готово
  WIZ_FAIL       // проверка не прошла
};
static WizStep wizStep = WIZ_INTRO;

static lv_obj_t *wizKeys[12] = {nullptr};
static lv_obj_t *btnWizEdit = nullptr;
static lv_obj_t *btnWizManual = nullptr;
static float wizHmin = HMIN_DEFAULT_MM;   // нижняя высота (из NVS, правится)
static float wizTravel = 0.0f;            // вычисленный ход, мм

static void wizRefreshFields() {
  char b[40];
  snprintf(b, sizeof(b), "Hmin: %s", wizBuf1.length() ? wizBuf1.c_str() : "--");
  lv_label_set_text(lblWizH1, b);
  snprintf(b, sizeof(b), "Hmax: %s", wizBuf2.length() ? wizBuf2.c_str() : "--");
  lv_label_set_text(lblWizH2, b);
}

static void wizShow(WizStep s) {
  wizStep = s;
  lv_obj_add_flag(btnWizPrimary, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(btnWizEdit, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(btnWizManual, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(lblWizH1, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(lblWizH2, LV_OBJ_FLAG_HIDDEN);

  bool keys = (s == WIZ_INPUT1 || s == WIZ_INPUT2 || s == WIZ_EDITMIN);
  for (int i = 0; i < 12; i++) {
    if (keys) lv_obj_clear_flag(wizKeys[i], LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(wizKeys[i], LV_OBJ_FLAG_HIDDEN);
  }

  switch (s) {
    case WIZ_INTRO:
      lv_label_set_text(lblWizTitle, "Calibration");
      lv_label_set_text(lblWizSub,
                        "Clear the table and the space\nbelow it. The table will move\ndown and up to the stops.");
      lv_obj_clear_flag(btnWizPrimary, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(btnWizPrimary, "START");
      break;
    case WIZ_DOWN:
      lv_label_set_text(lblWizSub, "Going down to the bottom stop...");
      break;
    case WIZ_PAUSE:
      lv_label_set_text(lblWizSub, "Bottom reached.\nGoing up in 3 s...");
      break;
    case WIZ_UP:
      lv_label_set_text(lblWizSub, "Going up to the top stop...");
      break;
    case WIZ_VERIFY:
      lv_label_set_text(lblWizSub, "Top reached.\nChecking: going down...");
      break;
    case WIZ_CONFIRM: {
      char b[96];
      snprintf(b, sizeof(b), "Bottom: %.0f mm\nTravel: %.0f mm\nTop: %.0f mm",
               wizHmin, wizTravel, wizHmin + wizTravel);
      lv_label_set_text(lblWizSub, b);
      lv_obj_clear_flag(btnWizPrimary, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(btnWizPrimary, "OK");
      lv_obj_clear_flag(btnWizEdit, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(btnWizEdit, "EDIT");
      lv_obj_clear_flag(btnWizManual, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(btnWizManual, "MANUAL");
      break;
    }
    case WIZ_INPUT1:
      lv_label_set_text(lblWizSub, "Enter BOTTOM height (mm):");
      lv_obj_clear_flag(lblWizH1, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(btnWizPrimary, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(btnWizPrimary, "NEXT");
      break;
    case WIZ_INPUT2:
      lv_label_set_text(lblWizSub, "Enter TOP height (mm):");
      lv_obj_clear_flag(lblWizH1, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(lblWizH2, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(btnWizPrimary, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(btnWizPrimary, "SAVE");
      break;
    case WIZ_EDITMIN:
      lv_label_set_text(lblWizSub, "Bottom height (mm):");
      lv_obj_clear_flag(lblWizH1, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(btnWizPrimary, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(btnWizPrimary, "OK");
      break;
    case WIZ_DONE:
      lv_label_set_text(lblWizSub, "Calibration complete!");
      lv_obj_clear_flag(btnWizPrimary, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(btnWizPrimary, "OK");
      break;
    case WIZ_FAIL:
      lv_label_set_text(lblWizSub,
                        "Check failed!\nEncoder or mechanics problem.\nCheck wiring and try again.");
      lv_obj_clear_flag(btnWizPrimary, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(btnWizPrimary, "RETRY");
      break;
  }
  wizRefreshFields();
}

static void wizStart() {
  wizBuf1 = "";
  wizBuf2 = "";
  wizHmin = stGetF("hminDef", HMIN_DEFAULT_MM);
  wizTravel = 0.0f;
  wizShow(WIZ_INTRO);
  lv_scr_load(scrWizard);
}

static void uiShowMain() {
  lv_scr_load(scrMain);
}

// ================= колбэки главного экрана =================

static void upCb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    if (!motor.calibrated()) { uiNotice("Calibrate first (CAL)", 2000); return; }
    motor.cmdManual(true);
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    motor.cmdStop();
  }
}

static void downCb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    if (!motor.calibrated()) { uiNotice("Calibrate first (CAL)", 2000); return; }
    motor.cmdManual(false);
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    motor.cmdStop();
  }
}

static void presetCb(lv_event_t *e) {
  int n = (int)(intptr_t)lv_event_get_user_data(e);
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_LONG_PRESSED) {
    if (motor.calibrated()) {
      stPutF(("p" + String(n)).c_str(), motor.posMm());
      char b[64];
      snprintf(b, sizeof(b), "Saved P%d: %.1f cm", n, motor.posMm() / 10.0f);
      uiNotice(b, 2500);
    }
  } else if (code == LV_EVENT_CLICKED) {
    float mm = stGetF(("p" + String(n)).c_str(), NAN);
    if (!isnan(mm) && motor.calibrated()) {
      motor.cmdGotoMm(mm);
    } else {
      uiNotice("Preset empty", 2000);
    }
  }
}

static void calCb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) wizStart();
}

static void settingsOpenCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_scr_load(scrSettings);
}

static void settingsBackCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  uiShowMain();
}

// ================= колбэки настроек =================

static void brightnessCb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  int v = lv_slider_get_value(sliderBrightness);
  if (code == LV_EVENT_VALUE_CHANGED) {
    setBrightness((uint8_t)v);
    char b[16];
    snprintf(b, sizeof(b), "%d%%", (int)brightPct);
    lv_label_set_text(lblBrightnessVal, b);
  } else if (code == LV_EVENT_RELEASED) {
    stPutF("bright", (float)brightPct);   // сохраняем один раз, а не на каждый шаг слайдера
  }
}

static void sleepCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  int idx = lv_dropdown_get_selected(ddSleep);
  if (idx >= 0 && idx < 5) setSleepMin(sleepOptions[idx]);
}

// ================= колбэки визарда =================

static void wizPrimaryCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

  switch (wizStep) {
    case WIZ_INTRO:
      motor.cmdCalibrate();
      wizShow(WIZ_DOWN);
      break;

    case WIZ_CONFIRM:
      // умная калибровка: подтверждаем вычисленные значения
      motor.setHeightsMm(wizHmin, wizHmin + wizTravel);
      wizShow(WIZ_DONE);
      break;

    case WIZ_INPUT1:
      if (wizBuf1.length() == 0) { uiNotice("Enter bottom height first", 2000); return; }
      wizShow(WIZ_INPUT2);
      break;

    case WIZ_INPUT2: {
      if (wizBuf2.length() == 0) { uiNotice("Enter top height first", 2000); return; }
      float h1 = wizBuf1.toFloat();
      float h2 = wizBuf2.toFloat();

      // Проверка 1: верхняя высота должна быть больше нижней
      if (h2 <= h1) { uiNotice("Top must be > bottom!", 3000); return; }

      // Проверка 2: правдоподобность хода относительно пройденных тиков
      int64_t range = motor.ticksMax() - motor.ticksMin();
      if (range <= 0) { uiNotice("No stop data", 3000); return; }
      float expected = (float)range * motor.mmPerTickValue();
      float travel = h2 - h1;
      if (travel < expected * 0.7f || travel > expected * 1.3f) {
        char b[80];
        snprintf(b, sizeof(b), "Travel %.0f mm,\nexpected ~%.0f mm.\nCheck input!", travel, expected);
        uiNotice(b, 4000);
        return;
      }

      motor.setHeightsMm(h1, h2);
      wizShow(WIZ_DONE);
      break;
    }

    case WIZ_EDITMIN: {
      float h = wizBuf1.toFloat();
      if (h < 300.0f || h > 2000.0f) { uiNotice("Bottom must be 300..2000 mm", 3000); return; }
      wizHmin = h;
      stPutF("hminDef", h);          // запоминаем нижнюю высоту
      wizShow(WIZ_CONFIRM);
      break;
    }

    case WIZ_DONE:
      uiShowMain();
      break;

    case WIZ_FAIL:
      motor.cmdCalibrate();          // повтор: снова вниз-вверх-вниз
      wizShow(WIZ_DOWN);
      break;

    default:
      break;
  }
}

static void wizManualCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  wizBuf1 = "";
  wizBuf2 = "";
  wizShow(WIZ_INPUT1);
}

static void wizEditCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  wizBuf1 = String((int)wizHmin);
  wizShow(WIZ_EDITMIN);
}

static void wizCancelCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  motor.cmdStop();          // останавливает и отменяет отложенный переход калибровки
  uiShowMain();
}

static void wizKeyCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const char *k = (const char *)lv_event_get_user_data(e);
  if (k == nullptr) return;

  String *buf = (wizStep == WIZ_INPUT2) ? &wizBuf2 : &wizBuf1;

  if (strcmp(k, "BS") == 0) {
    if (buf->length() > 0) buf->remove(buf->length() - 1);
  } else if (buf->length() < 5) {
    if (strcmp(k, ".") == 0 && buf->indexOf('.') >= 0) {
      // вторая точка не нужна
    } else {
      *buf += k;
    }
  }
  wizRefreshFields();
}

// ================= сборка экранов =================

static lv_obj_t *mkBtn(lv_obj_t *parent, int x, int y, int w, int h,
                       const char *txt, lv_event_cb_t cb, const void *ud) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)ud);
  lv_obj_t *l = lv_label_create(b);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
  lv_label_set_text(l, txt);
  lv_obj_center(l);
  return b;
}

static void buildMain() {
  scrMain = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scrMain, lv_color_hex(0x101418), 0);

  // Высота крупно (в сантиметрах)
  lblHeight = lv_label_create(scrMain);
  lv_obj_set_style_text_font(lblHeight, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(lblHeight, lv_color_white(), 0);
  lv_obj_align(lblHeight, LV_ALIGN_TOP_MID, 0, 6);
  lv_label_set_text(lblHeight, "-- cm");

  // Статусная строка
  lblState = lv_label_create(scrMain);
  lv_obj_set_style_text_font(lblState, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lblState, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_align(lblState, LV_ALIGN_TOP_MID, 0, 82);
  lv_label_set_text(lblState, "Ready");

  // Настройки (шестерёнка)
  mkBtn(scrMain, 8, 8, 80, 50, LV_SYMBOL_SETTINGS, settingsOpenCb, nullptr);

  // Калибровка
  lv_obj_t *btnCal = lv_btn_create(scrMain);
  lv_obj_set_size(btnCal, 80, 50);
  lv_obj_align(btnCal, LV_ALIGN_TOP_RIGHT, -8, 8);
  lv_obj_set_style_bg_color(btnCal, lv_palette_main(LV_PALETTE_ORANGE), 0);
  lv_obj_add_event_cb(btnCal, calCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lblCal = lv_label_create(btnCal);
  lv_obj_set_style_text_font(lblCal, &lv_font_montserrat_20, 0);
  lv_label_set_text(lblCal, "CAL");
  lv_obj_center(lblCal);

  // 4 пресета
  for (int i = 0; i < 4; i++) {
    btnPreset[i] = lv_btn_create(scrMain);
    lv_obj_set_size(btnPreset[i], 106, 70);
    lv_obj_align(btnPreset[i], LV_ALIGN_TOP_LEFT, 10 + i * 116, 118);
    lv_obj_add_event_cb(btnPreset[i], presetCb, LV_EVENT_ALL, (void *)(intptr_t)(i + 1));
    lblPreset[i] = lv_label_create(btnPreset[i]);
    lv_obj_set_style_text_font(lblPreset[i], &lv_font_montserrat_20, 0);
    lv_label_set_text(lblPreset[i], "...");
    lv_obj_center(lblPreset[i]);
  }

  // Вверх / Вниз (удержание)
  lv_obj_t *btnUp = lv_btn_create(scrMain);
  lv_obj_set_size(btnUp, 210, 100);
  lv_obj_align(btnUp, LV_ALIGN_BOTTOM_LEFT, 15, -15);
  lv_obj_set_style_bg_color(btnUp, lv_palette_main(LV_PALETTE_GREEN), 0);
  lv_obj_add_event_cb(btnUp, upCb, LV_EVENT_ALL, nullptr);
  lv_obj_t *lblUp = lv_label_create(btnUp);
  lv_obj_set_style_text_font(lblUp, &lv_font_montserrat_28, 0);
  lv_label_set_text(lblUp, LV_SYMBOL_UP "\nUP");
  lv_obj_center(lblUp);

  lv_obj_t *btnDown = lv_btn_create(scrMain);
  lv_obj_set_size(btnDown, 210, 100);
  lv_obj_align(btnDown, LV_ALIGN_BOTTOM_RIGHT, -15, -15);
  lv_obj_set_style_bg_color(btnDown, lv_palette_main(LV_PALETTE_BLUE), 0);
  lv_obj_add_event_cb(btnDown, downCb, LV_EVENT_ALL, nullptr);
  lv_obj_t *lblDown = lv_label_create(btnDown);
  lv_obj_set_style_text_font(lblDown, &lv_font_montserrat_28, 0);
  lv_label_set_text(lblDown, LV_SYMBOL_DOWN "\nDOWN");
  lv_obj_center(lblDown);
}

static void buildSettings() {
  scrSettings = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scrSettings, lv_color_hex(0x101418), 0);

  lv_obj_t *t = lv_label_create(scrSettings);
  lv_label_set_text(t, "Settings");
  lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(t, lv_color_white(), 0);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 10);

  // Яркость
  lv_obj_t *lb = lv_label_create(scrSettings);
  lv_label_set_text(lb, "Brightness:");
  lv_obj_set_style_text_font(lb, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lb, lv_color_white(), 0);
  lv_obj_align(lb, LV_ALIGN_TOP_LEFT, 20, 70);

  sliderBrightness = lv_slider_create(scrSettings);
  lv_obj_set_size(sliderBrightness, 240, 10);
  lv_obj_align(sliderBrightness, LV_ALIGN_TOP_LEFT, 180, 76);
  lv_slider_set_range(sliderBrightness, 0, 100);
  lv_slider_set_value(sliderBrightness, brightPct, LV_ANIM_OFF);
  lv_obj_add_event_cb(sliderBrightness, brightnessCb, LV_EVENT_ALL, nullptr);

  lblBrightnessVal = lv_label_create(scrSettings);
  lv_obj_set_style_text_font(lblBrightnessVal, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lblBrightnessVal, lv_color_white(), 0);
  lv_obj_align(lblBrightnessVal, LV_ALIGN_TOP_RIGHT, -20, 70);
  char b[16];
  snprintf(b, sizeof(b), "%d%%", (int)brightPct);
  lv_label_set_text(lblBrightnessVal, b);

  // Таймер сна
  lv_obj_t *ls = lv_label_create(scrSettings);
  lv_label_set_text(ls, "Sleep:");
  lv_obj_set_style_text_font(ls, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(ls, lv_color_white(), 0);
  lv_obj_align(ls, LV_ALIGN_TOP_LEFT, 20, 130);

  ddSleep = lv_dropdown_create(scrSettings);
  lv_dropdown_set_options(ddSleep, "Off\n1 min\n5 min\n15 min\n30 min");
  lv_obj_align(ddSleep, LV_ALIGN_TOP_LEFT, 180, 120);
  lv_obj_set_size(ddSleep, 180, 45);
  lv_obj_set_style_text_font(ddSleep, &lv_font_montserrat_20, 0);
  int sel = 0;
  for (int i = 0; i < 5; i++) if (sleepOptions[i] == sleepMin) sel = i;
  lv_dropdown_set_selected(ddSleep, sel);
  lv_obj_add_event_cb(ddSleep, sleepCb, LV_EVENT_VALUE_CHANGED, nullptr);

  // Назад
  mkBtn(scrSettings, 20, 260, 160, 44, LV_SYMBOL_LEFT " Back", settingsBackCb, nullptr);
}

static void buildWizard() {
  scrWizard = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scrWizard, lv_color_hex(0x101418), 0);

  lblWizTitle = lv_label_create(scrWizard);
  lv_obj_set_style_text_font(lblWizTitle, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(lblWizTitle, lv_color_white(), 0);
  lv_obj_align(lblWizTitle, LV_ALIGN_TOP_MID, 0, 6);

  lblWizSub = lv_label_create(scrWizard);
  lv_obj_set_style_text_font(lblWizSub, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lblWizSub, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_width(lblWizSub, 440);
  lv_label_set_long_mode(lblWizSub, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(lblWizSub, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lblWizSub, LV_ALIGN_TOP_MID, 0, 48);

  lblWizH1 = lv_label_create(scrWizard);
  lv_obj_set_style_text_font(lblWizH1, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lblWizH1, lv_color_white(), 0);
  lv_obj_align(lblWizH1, LV_ALIGN_TOP_MID, 0, 118);

  lblWizH2 = lv_label_create(scrWizard);
  lv_obj_set_style_text_font(lblWizH2, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lblWizH2, lv_color_white(), 0);
  lv_obj_align(lblWizH2, LV_ALIGN_TOP_MID, 0, 142);

  // Цифровая клавиатура 4x3
  const char *keys[4][3] = {
    {"1", "2", "3"},
    {"4", "5", "6"},
    {"7", "8", "9"},
    {".", "0", "BS"},
  };
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 3; c++) {
      wizKeys[r * 3 + c] =
        mkBtn(scrWizard, 40 + c * 140, 170 + r * 40, 120, 36, keys[r][c], wizKeyCb, keys[r][c]);
    }
  }

  btnWizManual = mkBtn(scrWizard, 40, 276, 100, 36, "MANUAL", wizManualCb, nullptr);
  btnWizPrimary = mkBtn(scrWizard, 170, 276, 140, 36, "START", wizPrimaryCb, nullptr);
  btnWizEdit = mkBtn(scrWizard, 320, 276, 80, 36, "EDIT", wizEditCb, nullptr);
  btnWizCancel = mkBtn(scrWizard, 410, 276, 60, 36, "X", wizCancelCb, nullptr);
}

// ================= публичное API =================

void uiBegin() {
  // Настройки яркости/сна из NVS
  brightPct = (uint8_t)stGetF("bright", 60.0f);
  sleepMin = (uint8_t)stGetF("sleepMin", 0.0f);

  ledcAttach(Pin::LCD_BL, 5000, 8);
  ledcWrite(Pin::LCD_BL, (uint32_t)(brightPct * 255 / 100));

  tft.init();
  tft.setRotation(1);          // ландшафт 480x320

  Wire.begin(Pin::CTP_SDA, Pin::CTP_SCL);
  touchReset();

  lv_init();

  // Два частичных буфера по 480x60 строк — UI отзывчивый
  const uint32_t bufPx = 480 * 60;
  buf1 = (lv_color_t *)ps_malloc(bufPx * sizeof(lv_color_t));
  buf2 = (lv_color_t *)ps_malloc(bufPx * sizeof(lv_color_t));
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, bufPx);
  Serial0.printf("UI: psramFound=%d buf1=%p buf2=%p\n",
                 psramFound() ? 1 : 0, (void *)buf1, (void *)buf2);

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

  buildMain();
  buildSettings();
  buildWizard();
  Serial0.println("UI: init done");

  // Без калибровки сразу открываем визард
  if (motor.calibrated()) {
    uiShowMain();
  } else {
    wizStart();
  }
}

void uiEvent(MotorEvent e) {
  bool wizActive = (lv_scr_act() == scrWizard);
  switch (e) {
    case MotorEvent::OBSTACLE:
      uiNotice("Pinch! Backing off", 3000);
      break;
    case MotorEvent::ENC_STALL:
      uiNotice("Encoder silent!", 5000);
      break;
    case MotorEvent::CAL_HIT_MIN:
      if (wizActive) wizShow(WIZ_PAUSE);
      uiNotice("Bottom reached. Going up in 3 s...", 4000);
      break;
    case MotorEvent::CAL_HIT_MAX:
      if (wizActive) wizShow(WIZ_VERIFY);
      uiNotice("Top reached. Checking...", 5000);
      break;
    case MotorEvent::CAL_VERIFIED:
      if (wizActive) {
        wizHmin = stGetF("hminDef", HMIN_DEFAULT_MM);
        int64_t range = motor.ticksMax() - motor.ticksMin();
        wizTravel = (float)range * motor.mmPerTickValue();
        // правдоподобность хода: 100..1500 мм
        if (range <= 0 || wizTravel < 100.0f || wizTravel > 1500.0f) {
          wizShow(WIZ_FAIL);
          uiNotice("Unreasonable travel. Check!", 6000);
        } else {
          wizShow(WIZ_CONFIRM);
        }
      }
      break;
    case MotorEvent::CAL_VERIFY_FAIL:
      if (wizActive) wizShow(WIZ_FAIL);
      uiNotice("Check failed! Encoder or mechanics", 6000);
      break;
    case MotorEvent::CAL_TIMEOUT:
      uiNotice("Calibration failed", 5000);
      break;
    case MotorEvent::END_LIMIT:
      uiNotice("End of travel", 2000);
      break;
    case MotorEvent::REACHED_TARGET:
      uiNotice("Done", 1500);
      break;
    default:
      break;
  }
}

void uiTask() {
  lv_timer_handler();

  // Сон: гасим подсветку после таймаута без касаний (только когда мотор стоит)
  if (sleepMin > 0 && backlightOn &&
      motor.state() == MotorState::IDLE &&
      (millis() - lastActivity > (uint32_t)sleepMin * 60000UL)) {
    backlightOn = false;
    ledcWrite(Pin::LCD_BL, 0);
  }

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

  // Статус (если нет активного уведомления и не визард)
  if (millis() >= noticeUntil && lv_scr_act() == scrMain) {
    lv_obj_set_style_text_color(lblState, lv_palette_main(LV_PALETTE_GREY), 0);
    switch (motor.state()) {
      case MotorState::IDLE:      lv_label_set_text(lblState, "Ready"); break;
      case MotorState::MANUAL:    lv_label_set_text(lblState, "Manual"); break;
      case MotorState::MOVE_TO:   lv_label_set_text(lblState, "Moving..."); break;
      case MotorState::CAL_DOWN:
      case MotorState::CAL_UP:
      case MotorState::CAL_VERIFY: lv_label_set_text(lblState, "Calibrating..."); break;
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
