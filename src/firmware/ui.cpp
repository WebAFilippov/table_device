#include "ui.h"
#include "config.h"
#include "storage.h"
#include "clock.h"
#include "wifi.h"
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
static lv_obj_t *sliderSpeed = nullptr;
static lv_obj_t *lblSpeedVal = nullptr;
static lv_obj_t *ddTz = nullptr;

// --- часы (DS3231) ---
static lv_obj_t *lblClock = nullptr;

// --- вибромотор ---
static bool vibEnabled = true;
static bool vibOn = false;
static int vibTaps = 0;            // сколько ещё включений после текущего
static uint32_t vibPhaseEnd = 0;   // конец текущей фазы
static uint32_t vibOnMs = 120, vibOffMs = 100;
static float vibLvl = 1.0f;        // 0.7..1.0 (слайдер)
static uint32_t vibDuty = 1023;    // заполнение LEDC
static lv_obj_t *vibSwitch = nullptr;

// --- зуммер (мелодии) ---
static bool sndEnabled = true;
static float sndLvl = 20.0f;       // 10..100 % (слайдер)
static uint32_t sndDuty = 200;     // заполнение LEDC
static lv_obj_t *sndSwitch = nullptr;
struct SndNote { uint16_t freq; uint16_t dur; };
static SndNote sndQueue[8];
static uint8_t sndN = 0;
static bool sndPlaying = false;
static uint32_t sndNoteEnd = 0;

// --- экран Feel & sound ---
static lv_obj_t *scrFeedback = nullptr;
static lv_obj_t *sliderVibLvl = nullptr, *sliderSndLvl = nullptr;
static lv_obj_t *lblVibLvlVal = nullptr, *lblSndLvlVal = nullptr;

// --- экраны Wi-Fi ---
static lv_obj_t *scrWifi = nullptr;
static lv_obj_t *scrWifiPass = nullptr;
static lv_obj_t *lblWifiStatus = nullptr;
static lv_obj_t *lstWifi = nullptr;
static lv_obj_t *lblWifiSsid = nullptr;
static lv_obj_t *taWifiPass = nullptr;
static lv_obj_t *kbWifiPass = nullptr;
static WifiNet uiNets[24];
static int uiNetCount = 0;
static char uiSelSsid[33] = "";
static bool scanWaiting = false;

// --- экран установки даты/времени ---
static lv_obj_t *scrClockSet = nullptr;
static lv_obj_t *lblClkSub = nullptr;
static lv_obj_t *lblClkField = nullptr;
static lv_obj_t *btnClkPrimary = nullptr;
static lv_obj_t *btnClkCancel = nullptr;
static lv_obj_t *clkKeys[12] = {nullptr};
enum ClkStep : uint8_t { CLK_DATE, CLK_TIME };
static ClkStep clkStep = CLK_DATE;
static String clkBuf1, clkBuf2;           // цифры: дата 8, время 4

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
static void manualSpeedCb(lv_event_t *e);
static void timezoneCb(lv_event_t *e);
static void vibCb(lv_event_t *e);
static void sndCb(lv_event_t *e);
static void feedbackOpenCb(lv_event_t *e);
static void feedbackBackCb(lv_event_t *e);
static void vibLvlCb(lv_event_t *e);
static void sndLvlCb(lv_event_t *e);
static void vibTestCb(lv_event_t *e);
static void sndTestCb(lv_event_t *e);
static void wifiOpenCb(lv_event_t *e);
static void wifiBackCb(lv_event_t *e);
static void wifiScanCb(lv_event_t *e);
static void wifiForgetCb(lv_event_t *e);
static void wifiItemCb(lv_event_t *e);
static void wifiPassConnectCb(lv_event_t *e);
static void wifiPassCancelCb(lv_event_t *e);
static void wizPrimaryCb(lv_event_t *e);
static void wizCancelCb(lv_event_t *e);
static void wizKeyCb(lv_event_t *e);
static void clockSetOpenCb(lv_event_t *e);
static void clkPrimaryCb(lv_event_t *e);
static void clkCancelCb(lv_event_t *e);
static void clkKeyCb(lv_event_t *e);

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

// ================= вибромотор =================

static void vibPulse(uint32_t ms) {
  if (!vibEnabled) return;
  ledcWrite(Pin::VIB, vibDuty);
  vibOn = true;
  vibTaps = 0;
  vibPhaseEnd = millis() + ms;
}

static void vibPattern(int taps, uint32_t onMs, uint32_t offMs) {
  if (!vibEnabled || taps < 1) return;
  vibOnMs = onMs;
  vibOffMs = offMs;
  vibPulse(onMs);
  vibTaps = taps - 1;
}

// Неблокирующий «проигрыватель» паттернов: зовётся из uiTask
static void vibTask() {
  if (!vibOn && vibTaps == 0) return;
  uint32_t now = millis();
  if (vibOn && now >= vibPhaseEnd) {
    ledcWrite(Pin::VIB, 0);
    vibOn = false;
    vibPhaseEnd = (vibTaps > 0) ? (now + vibOffMs) : 0;
  } else if (!vibOn && vibTaps > 0 && now >= vibPhaseEnd) {
    ledcWrite(Pin::VIB, vibDuty);
    vibOn = true;
    vibPhaseEnd = now + vibOnMs;
    vibTaps--;
  }
}

static void vibCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  vibEnabled = lv_obj_has_state(vibSwitch, LV_STATE_CHECKED);
  stPutF("vibEn", vibEnabled ? 1.0f : 0.0f);
  if (vibEnabled) vibPulse(80);   // подтверждение, что включили
}

// ================= зуммер (мягкие мелодии) =================

// Ноты мягкие и короткие: {частота, длительность}. Частота 0 = пауза.
static const SndNote M_DONE[]    = {{659, 80}, {0, 60}, {784, 110}};   // «ти-там»: доехали
static const SndNote M_SAVED[]   = {{659, 90}};                        // сохранено
static const SndNote M_STEP[]    = {{880, 70}};                        // упор найден
static const SndNote M_CALDONE[] = {{523, 90}, {659, 90}, {784, 110}}; // калибровка готова
static const SndNote M_LIMIT[]   = {{784, 80}, {0, 50}, {659, 120}};   // граница хода
static const SndNote M_WARN[]    = {{784, 90}, {0, 60}, {523, 140}};   // мягкий «спад»: ошибка

static void sndEnqueue(uint16_t f, uint16_t d) {
  if (sndN < 8) { sndQueue[sndN].freq = f; sndQueue[sndN].dur = d; sndN++; }
}

static void sndPlay(const SndNote *notes, int n) {
  if (!sndEnabled) return;
  for (int i = 0; i < n && sndN < 8; i++) sndEnqueue(notes[i].freq, notes[i].dur);
}

// Неблокирующий проигрыватель: зовётся из uiTask
static void sndTask() {
  if (!sndPlaying && sndN == 0) return;
  if (!sndPlaying) {
    sndPlaying = true;
    ledcChangeFrequency(Pin::BUZZER, sndQueue[0].freq ? sndQueue[0].freq : 4000, SND_BITS);
    ledcWrite(Pin::BUZZER, sndQueue[0].freq ? sndDuty : 0);
    sndNoteEnd = millis() + sndQueue[0].dur;
  } else if (millis() >= sndNoteEnd) {
    for (int i = 0; i < (int)sndN - 1; i++) sndQueue[i] = sndQueue[i + 1];
    sndN--;
    if (sndN == 0) {
      sndPlaying = false;
      ledcWrite(Pin::BUZZER, 0);
    } else {
      ledcChangeFrequency(Pin::BUZZER, sndQueue[0].freq ? sndQueue[0].freq : 4000, SND_BITS);
      ledcWrite(Pin::BUZZER, sndQueue[0].freq ? sndDuty : 0);
      sndNoteEnd = millis() + sndQueue[0].dur;
    }
  }
}

static void sndCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  sndEnabled = lv_obj_has_state(sndSwitch, LV_STATE_CHECKED);
  stPutF("sndEn", sndEnabled ? 1.0f : 0.0f);
  if (sndEnabled) sndPlay(M_SAVED, 1);   // подтверждение, что включили
}

// ================= экран Feel & sound =================

static void vibLvlCb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  int v = lv_slider_get_value(sliderVibLvl);
  if (code == LV_EVENT_VALUE_CHANGED) {
    vibLvl = v / 100.0f;
    vibDuty = (uint32_t)(vibLvl * 1023.0f);
    char b[16];
    snprintf(b, sizeof(b), "%d%%", v);
    lv_label_set_text(lblVibLvlVal, b);
    if (vibEnabled) vibPulse(200);   // живой отклик при движении слайдера
  } else if (code == LV_EVENT_RELEASED) {
    stPutF("vibLvl", vibLvl);
  }
}

static void sndLvlCb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  int v = lv_slider_get_value(sliderSndLvl);
  if (code == LV_EVENT_VALUE_CHANGED) {
    sndLvl = (float)v;
    sndDuty = (uint32_t)(v * 1023.0f / 100.0f);
    char b[16];
    snprintf(b, sizeof(b), "%d%%", v);
    lv_label_set_text(lblSndLvlVal, b);
    if (sndEnabled) {
      sndN = 0;                     // отменяем очередь, играем короткий отклик
      sndPlaying = false;
      ledcWrite(Pin::BUZZER, 0);
      sndPlay(M_SAVED, 1);
    }
  } else if (code == LV_EVENT_RELEASED) {
    stPutF("sndLvl", sndLvl);
  }
}

static void vibTestCb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) vibPulse(400);
}

static void sndTestCb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) sndPlay(M_CALDONE, 3);
}

static void feedbackOpenCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_scr_load(scrFeedback);
}

static void feedbackBackCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_scr_load(scrSettings);
}

// ================= Wi-Fi =================

static const char *authName(uint8_t a) {
  switch (a) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "802.1X";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA3";
    default: return "WAPI";
  }
}

static void wifiPopulateList() {
  if (lstWifi == nullptr) return;
  lv_obj_clean(lstWifi);
  for (int i = 0; i < uiNetCount; i++) {
    const WifiNet &n = uiNets[i];
    char b[96];
    // «замочек» — текстом (в шрифте нет иконки замка): open / WPA2 / WPA3...
    // уровень — в dBm; у подключённой сети спереди галочка
    char pre[8] = "";
    if (wifiIsConnected() && wifiCurrentSsid() == String(n.ssid)) {
      strcpy(pre, LV_SYMBOL_OK " ");
    }
    snprintf(b, sizeof(b), "%s%s    %d dBm  %s", pre, n.ssid, (int)n.rssi, authName(n.auth));
    lv_obj_t *btn = lv_list_add_btn(lstWifi, nullptr, b);
    lv_obj_add_event_cb(btn, wifiItemCb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }
}

// Обновление экранов Wi-Fi: зовётся из uiTask каждые 500 мс
static void wifiUiPoll() {
  if (lv_scr_act() != scrWifi && lv_scr_act() != scrWifiPass) return;

  // Смена состояния подключения → перерисовываем список (галочка у сети)
  static bool lastWifiConnected = false;
  bool conn = wifiIsConnected();
  if (conn != lastWifiConnected) {
    lastWifiConnected = conn;
    wifiPopulateList();
  }

  if (scanWaiting && wifiScanDone()) {
    scanWaiting = false;
    uiNetCount = min(wifiScanCount(), 24);
    for (int i = 0; i < uiNetCount; i++) uiNets[i] = wifiNet(i);
    wifiPopulateList();
    if (uiNetCount == 0) lv_label_set_text(lblWifiStatus, "No networks found");
    else lv_label_set_text(lblWifiStatus, "Select a network");
  }

  if (wifiConnectDone()) {
    if (wifiConnectOk()) {
      char b[96];
      snprintf(b, sizeof(b), LV_SYMBOL_OK " Connected, IP %s", wifiIp().c_str());
      lv_label_set_text(lblWifiStatus, b);
    } else {
      lv_label_set_text(lblWifiStatus, "Connection failed");
    }
  } else if (wifiConnectBusy()) {
    lv_label_set_text(lblWifiStatus, "Connecting...");
  } else if (wifiIsConnected()) {
    char b[96];
    snprintf(b, sizeof(b), LV_SYMBOL_WIFI " %s  %d dBm  %s",
             wifiCurrentSsid().c_str(), (int)wifiRssi(), wifiIp().c_str());
    lv_label_set_text(lblWifiStatus, b);
  }
}

static void wifiOpenCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (wifiIsConnected()) {
    char b[96];
    snprintf(b, sizeof(b), LV_SYMBOL_WIFI " %s  %d dBm", wifiCurrentSsid().c_str(), (int)wifiRssi());
    lv_label_set_text(lblWifiStatus, b);
  } else {
    lv_label_set_text(lblWifiStatus, "Scanning...");
  }
  wifiRequestScan();
  scanWaiting = true;
  lv_scr_load(scrWifi);
}

static void wifiBackCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_scr_load(scrSettings);
}

static void wifiScanCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_label_set_text(lblWifiStatus, "Scanning...");
  wifiRequestScan();
  scanWaiting = true;
}

static void wifiForgetCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  wifiErase();
  lv_label_set_text(lblWifiStatus, "Network forgotten");
}

static void wifiItemCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i < 0 || i >= uiNetCount) return;
  const WifiNet &n = uiNets[i];
  strncpy(uiSelSsid, n.ssid, 32);
  uiSelSsid[32] = 0;

  if (n.auth == WIFI_AUTH_OPEN) {
    wifiConnect(uiSelSsid, "");
    lv_label_set_text(lblWifiStatus, "Connecting...");
  } else {
    lv_label_set_text(lblWifiSsid, uiSelSsid);
    String savedPass = (wifiSavedSsid() == String(uiSelSsid)) ? wifiSavedPass() : "";
    lv_textarea_set_text(taWifiPass, savedPass.c_str());
    lv_scr_load(scrWifiPass);
  }
}

static void wifiPassConnectCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const char *pass = lv_textarea_get_text(taWifiPass);
  wifiConnect(uiSelSsid, pass);
  lv_scr_load(scrWifi);
  lv_label_set_text(lblWifiStatus, "Connecting...");
}

static void wifiPassCancelCb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) lv_scr_load(scrWifi);
}

// ================= уведомления =================

void uiNotice(const char *msg, uint32_t ms) {
  if (lv_scr_act() == scrWizard) {
    lv_label_set_text(lblWizSub, msg);
  } else if (lv_scr_act() == scrClockSet) {
    lv_label_set_text(lblClkSub, msg);
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

// Подпись кнопки — её дочерняя метка (первый ребёнок).
// ВАЖНО: lv_label_set_text нельзя вызывать на самой кнопке — LVGL трактует
// память кнопки как структуру метки, читает мусорный указатель и портит память.
static lv_obj_t *btnLabelOf(lv_obj_t *btn) {
  return lv_obj_get_child(btn, 0);
}

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
      lv_label_set_text(btnLabelOf(btnWizPrimary), "START");
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
      lv_label_set_text(btnLabelOf(btnWizPrimary), "OK");
      lv_obj_clear_flag(btnWizEdit, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(btnLabelOf(btnWizEdit), "EDIT");
      lv_obj_clear_flag(btnWizManual, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(btnLabelOf(btnWizManual), "MANUAL");
      break;
    }
    case WIZ_INPUT1:
      lv_label_set_text(lblWizSub, "Enter BOTTOM height (mm):");
      lv_obj_clear_flag(lblWizH1, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(btnWizPrimary, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(btnLabelOf(btnWizPrimary), "NEXT");
      break;
    case WIZ_INPUT2:
      lv_label_set_text(lblWizSub, "Enter TOP height (mm):");
      lv_obj_clear_flag(lblWizH1, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(lblWizH2, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(btnWizPrimary, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(btnLabelOf(btnWizPrimary), "SAVE");
      break;
    case WIZ_EDITMIN:
      lv_label_set_text(lblWizSub, "Bottom height (mm):");
      lv_obj_clear_flag(lblWizH1, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(btnWizPrimary, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(btnLabelOf(btnWizPrimary), "OK");
      break;
    case WIZ_DONE:
      lv_label_set_text(lblWizSub, "Calibration complete!");
      lv_obj_clear_flag(btnWizPrimary, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(btnLabelOf(btnWizPrimary), "OK");
      break;
    case WIZ_FAIL:
      lv_label_set_text(lblWizSub,
                        "Check failed!\nEncoder or mechanics problem.\nCheck wiring and try again.");
      lv_obj_clear_flag(btnWizPrimary, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(btnLabelOf(btnWizPrimary), "RETRY");
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
      vibPulse(120);
      sndPlay(M_SAVED, 1);
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

static void manualSpeedCb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  int v = lv_slider_get_value(sliderSpeed);
  if (code == LV_EVENT_VALUE_CHANGED) {
    motor.setManualSpeedMmS((float)v);
    char b[24];
    snprintf(b, sizeof(b), "%d mm/s", v);
    lv_label_set_text(lblSpeedVal, b);
  } else if (code == LV_EVENT_RELEASED) {
    stPutF("manSpd", motor.manualSpeedMmS());   // сохраняем один раз
  }
}

static void timezoneCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  int idx = lv_dropdown_get_selected(ddTz);
  if (idx >= 0 && idx <= 10) clockSetTz((idx + 2) * 60);   // UTC+2 .. UTC+12
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
      vibPattern(2, 120, 120);
      sndPlay(M_CALDONE, 3);
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
      vibPattern(2, 120, 120);
      sndPlay(M_CALDONE, 3);
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

// ================= экран установки даты/времени =================

static void fmtDate(const String &d, char *out, size_t n) {
  out[0] = 0;
  for (unsigned i = 0; i < d.length() && i < 8; i++) {
    if (i == 2 || i == 4) strlcat(out, ".", n);
    char c[2] = {d[i], 0};
    strlcat(out, c, n);
  }
}

static void fmtTime(const String &d, char *out, size_t n) {
  out[0] = 0;
  for (unsigned i = 0; i < d.length() && i < 4; i++) {
    if (i == 2) strlcat(out, ":", n);
    char c[2] = {d[i], 0};
    strlcat(out, c, n);
  }
}

static void clkRefresh() {
  char b[24];
  if (clkStep == CLK_DATE) {
    if (clkBuf1.length() == 0) lv_label_set_text(lblClkField, "--.--.----");
    else { fmtDate(clkBuf1, b, sizeof(b)); lv_label_set_text(lblClkField, b); }
  } else {
    if (clkBuf2.length() == 0) lv_label_set_text(lblClkField, "--:--");
    else { fmtTime(clkBuf2, b, sizeof(b)); lv_label_set_text(lblClkField, b); }
  }
}

static void clkShow(ClkStep s) {
  clkStep = s;
  if (s == CLK_DATE) {
    lv_label_set_text(lblClkSub, "Enter date: DD.MM.YYYY");
    lv_label_set_text(btnLabelOf(btnClkPrimary), "NEXT");
  } else {
    lv_label_set_text(lblClkSub, "Enter time: HH:MM");
    lv_label_set_text(btnLabelOf(btnClkPrimary), "SAVE");
  }
  clkRefresh();
}

static void clockSetOpenCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  clkBuf1 = "";
  clkBuf2 = "";
  clkShow(CLK_DATE);
  lv_scr_load(scrClockSet);
}

static void clkPrimaryCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

  if (clkStep == CLK_DATE) {
    if (clkBuf1.length() != 8) { uiNotice("Date: 8 digits DDMMYYYY", 2500); return; }
    int d = clkBuf1.substring(0, 2).toInt();
    int m = clkBuf1.substring(2, 4).toInt();
    int y = clkBuf1.substring(4).toInt();
    if (d < 1 || d > 31 || m < 1 || m > 12 || y < 2000 || y > 2099) {
      uiNotice("Bad date, check digits", 2500);
      return;
    }
    clkShow(CLK_TIME);
  } else {
    if (clkBuf2.length() != 4) { uiNotice("Time: 4 digits HHMM", 2500); return; }
    int hh = clkBuf2.substring(0, 2).toInt();
    int mm = clkBuf2.substring(2).toInt();
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) { uiNotice("Bad time, check digits", 2500); return; }
    DateTime t;
    if (!clockGet(t)) t = DateTime();
    t.day = clkBuf1.substring(0, 2).toInt();
    t.mon = clkBuf1.substring(2, 4).toInt();
    t.year = clkBuf1.substring(4).toInt();
    t.hour = hh;
    t.min = mm;
    t.sec = 0;
    clockSet(t);
    uiShowMain();
    uiNotice("Date & time saved", 2500);
  }
}

static void clkCancelCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  uiShowMain();
}

static void clkKeyCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const char *k = (const char *)lv_event_get_user_data(e);
  if (k == nullptr) return;

  String &buf = (clkStep == CLK_DATE) ? clkBuf1 : clkBuf2;
  size_t maxLen = (clkStep == CLK_DATE) ? 8 : 4;
  if (strcmp(k, "BS") == 0) {
    if (buf.length()) buf.remove(buf.length() - 1);
  } else if (strcmp(k, "C") == 0) {
    buf = "";
  } else if (buf.length() < maxLen && k[0] >= '0' && k[0] <= '9') {
    buf += k;
  }
  clkRefresh();
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

  // Часы сверху (DS3231)
  lblClock = lv_label_create(scrMain);
  lv_obj_set_style_text_font(lblClock, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lblClock, lv_color_white(), 0);
  lv_obj_align(lblClock, LV_ALIGN_TOP_MID, 0, 2);
  lv_label_set_text(lblClock, "--:--");

  // Высота крупно (в сантиметрах)
  lblHeight = lv_label_create(scrMain);
  lv_obj_set_style_text_font(lblHeight, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(lblHeight, lv_color_white(), 0);
  lv_obj_align(lblHeight, LV_ALIGN_TOP_MID, 0, 26);
  lv_label_set_text(lblHeight, "-- cm");

  // Статусная строка
  lblState = lv_label_create(scrMain);
  lv_obj_set_style_text_font(lblState, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lblState, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_align(lblState, LV_ALIGN_TOP_MID, 0, 88);
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

  // Ручная скорость (10..25 мм/с)
  lv_obj_t *lm = lv_label_create(scrSettings);
  lv_label_set_text(lm, "Manual speed:");
  lv_obj_set_style_text_font(lm, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lm, lv_color_white(), 0);
  lv_obj_align(lm, LV_ALIGN_TOP_LEFT, 20, 180);

  sliderSpeed = lv_slider_create(scrSettings);
  lv_obj_set_size(sliderSpeed, 240, 10);
  lv_obj_align(sliderSpeed, LV_ALIGN_TOP_LEFT, 180, 186);
  lv_slider_set_range(sliderSpeed, (int32_t)MANUAL_SPEED_MIN_MM_S, (int32_t)MANUAL_SPEED_MAX_MM_S);
  lv_slider_set_value(sliderSpeed, (int32_t)motor.manualSpeedMmS(), LV_ANIM_OFF);
  lv_obj_add_event_cb(sliderSpeed, manualSpeedCb, LV_EVENT_ALL, nullptr);

  lblSpeedVal = lv_label_create(scrSettings);
  lv_obj_set_style_text_font(lblSpeedVal, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lblSpeedVal, lv_color_white(), 0);
  lv_obj_align(lblSpeedVal, LV_ALIGN_TOP_RIGHT, -20, 180);
  {
    char b[24];
    snprintf(b, sizeof(b), "%d mm/s", (int)motor.manualSpeedMmS());
    lv_label_set_text(lblSpeedVal, b);
  }

  // Часовой пояс (UTC+2..+12)
  lv_obj_t *ltz = lv_label_create(scrSettings);
  lv_label_set_text(ltz, "Time zone:");
  lv_obj_set_style_text_font(ltz, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(ltz, lv_color_white(), 0);
  lv_obj_align(ltz, LV_ALIGN_TOP_LEFT, 20, 226);

  ddTz = lv_dropdown_create(scrSettings);
  lv_dropdown_set_options(ddTz,
    "UTC+2\nUTC+3\nUTC+4\nUTC+5\nUTC+6\nUTC+7\nUTC+8\nUTC+9\nUTC+10\nUTC+11\nUTC+12");
  lv_obj_align(ddTz, LV_ALIGN_TOP_LEFT, 180, 220);
  lv_obj_set_size(ddTz, 140, 45);
  lv_obj_set_style_text_font(ddTz, &lv_font_montserrat_20, 0);
  int tzIdx = constrain(clockTzMin() / 60 - 2, 0, 10);
  lv_dropdown_set_selected(ddTz, tzIdx);
  lv_obj_add_event_cb(ddTz, timezoneCb, LV_EVENT_VALUE_CHANGED, nullptr);

  // Вибрация вкл/выкл
  lv_obj_t *lvb = lv_label_create(scrSettings);
  lv_label_set_text(lvb, "Vib:");
  lv_obj_set_style_text_font(lvb, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lvb, lv_color_white(), 0);
  lv_obj_align(lvb, LV_ALIGN_TOP_LEFT, 330, 226);

  vibSwitch = lv_switch_create(scrSettings);
  lv_obj_align(vibSwitch, LV_ALIGN_TOP_LEFT, 380, 220);
  lv_obj_set_size(vibSwitch, 60, 30);
  if (vibEnabled) lv_obj_add_state(vibSwitch, LV_STATE_CHECKED);
  lv_obj_add_event_cb(vibSwitch, vibCb, LV_EVENT_VALUE_CHANGED, nullptr);

  // Назад + Wi-Fi + дата/время + отклик
  mkBtn(scrSettings, 20, 260, 100, 44, LV_SYMBOL_LEFT " Back", settingsBackCb, nullptr);
  mkBtn(scrSettings, 130, 260, 100, 44, LV_SYMBOL_WIFI " WiFi", wifiOpenCb, nullptr);
  mkBtn(scrSettings, 240, 260, 110, 44, "Clock...", clockSetOpenCb, nullptr);
  mkBtn(scrSettings, 360, 260, 100, 44, "Feel", feedbackOpenCb, nullptr);
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

static void buildClockSet() {
  scrClockSet = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scrClockSet, lv_color_hex(0x101418), 0);

  lv_obj_t *title = lv_label_create(scrClockSet);
  lv_label_set_text(title, "Set date & time");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

  lblClkSub = lv_label_create(scrClockSet);
  lv_obj_set_style_text_font(lblClkSub, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lblClkSub, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_align(lblClkSub, LV_ALIGN_TOP_MID, 0, 48);

  lblClkField = lv_label_create(scrClockSet);
  lv_obj_set_style_text_font(lblClkField, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(lblClkField, lv_color_white(), 0);
  lv_obj_align(lblClkField, LV_ALIGN_TOP_MID, 0, 100);

  // Цифровая клавиатура (только цифры: разделители подставляются сами)
  const char *keys[4][3] = {
    {"1", "2", "3"},
    {"4", "5", "6"},
    {"7", "8", "9"},
    {"BS", "0", "C"},
  };
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 3; c++) {
      clkKeys[r * 3 + c] =
        mkBtn(scrClockSet, 40 + c * 140, 170 + r * 40, 120, 36, keys[r][c], clkKeyCb, keys[r][c]);
    }
  }

  btnClkPrimary = mkBtn(scrClockSet, 170, 276, 140, 36, "NEXT", clkPrimaryCb, nullptr);
  btnClkCancel = mkBtn(scrClockSet, 410, 276, 60, 36, "X", clkCancelCb, nullptr);
}

static void buildFeedback() {
  scrFeedback = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scrFeedback, lv_color_hex(0x101418), 0);

  lv_obj_t *t = lv_label_create(scrFeedback);
  lv_label_set_text(t, "Feel & sound");
  lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(t, lv_color_white(), 0);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 6);

  // Звук: вкл/выкл + тест
  lv_obj_t *ls = lv_label_create(scrFeedback);
  lv_label_set_text(ls, "Sound:");
  lv_obj_set_style_text_font(ls, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(ls, lv_color_white(), 0);
  lv_obj_align(ls, LV_ALIGN_TOP_LEFT, 20, 56);

  sndSwitch = lv_switch_create(scrFeedback);
  lv_obj_align(sndSwitch, LV_ALIGN_TOP_LEFT, 110, 52);
  lv_obj_set_size(sndSwitch, 60, 30);
  if (sndEnabled) lv_obj_add_state(sndSwitch, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sndSwitch, sndCb, LV_EVENT_VALUE_CHANGED, nullptr);

  mkBtn(scrFeedback, 190, 52, 90, 36, "Test", sndTestCb, nullptr);

  // Уровень звука
  lv_obj_t *lsl = lv_label_create(scrFeedback);
  lv_label_set_text(lsl, "Level:");
  lv_obj_set_style_text_font(lsl, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lsl, lv_color_white(), 0);
  lv_obj_align(lsl, LV_ALIGN_TOP_LEFT, 20, 106);

  sliderSndLvl = lv_slider_create(scrFeedback);
  lv_obj_set_size(sliderSndLvl, 240, 10);
  lv_obj_align(sliderSndLvl, LV_ALIGN_TOP_LEFT, 110, 112);
  lv_slider_set_range(sliderSndLvl, 10, 100);
  lv_slider_set_value(sliderSndLvl, (int32_t)sndLvl, LV_ANIM_OFF);
  lv_obj_add_event_cb(sliderSndLvl, sndLvlCb, LV_EVENT_ALL, nullptr);

  lblSndLvlVal = lv_label_create(scrFeedback);
  lv_obj_set_style_text_font(lblSndLvlVal, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lblSndLvlVal, lv_color_white(), 0);
  lv_obj_align(lblSndLvlVal, LV_ALIGN_TOP_RIGHT, -20, 106);
  {
    char b[16];
    snprintf(b, sizeof(b), "%d%%", (int)sndLvl);
    lv_label_set_text(lblSndLvlVal, b);
  }

  // Уровень вибрации (70..100%: ниже ~74% мотор может не стартовать)
  lv_obj_t *lvl = lv_label_create(scrFeedback);
  lv_label_set_text(lvl, "Vib level:");
  lv_obj_set_style_text_font(lvl, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lvl, lv_color_white(), 0);
  lv_obj_align(lvl, LV_ALIGN_TOP_LEFT, 20, 156);

  sliderVibLvl = lv_slider_create(scrFeedback);
  lv_obj_set_size(sliderVibLvl, 240, 10);
  lv_obj_align(sliderVibLvl, LV_ALIGN_TOP_LEFT, 110, 162);
  lv_slider_set_range(sliderVibLvl, 70, 100);
  lv_slider_set_value(sliderVibLvl, (int32_t)(vibLvl * 100.0f), LV_ANIM_OFF);
  lv_obj_add_event_cb(sliderVibLvl, vibLvlCb, LV_EVENT_ALL, nullptr);

  lblVibLvlVal = lv_label_create(scrFeedback);
  lv_obj_set_style_text_font(lblVibLvlVal, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lblVibLvlVal, lv_color_white(), 0);
  lv_obj_align(lblVibLvlVal, LV_ALIGN_TOP_RIGHT, -20, 156);
  {
    char b[16];
    snprintf(b, sizeof(b), "%d%%", (int)(vibLvl * 100.0f));
    lv_label_set_text(lblVibLvlVal, b);
  }

  mkBtn(scrFeedback, 370, 158, 90, 36, "Test", vibTestCb, nullptr);

  // Назад
  mkBtn(scrFeedback, 20, 262, 160, 44, LV_SYMBOL_LEFT " Back", feedbackBackCb, nullptr);
}

static void buildWifi() {
  scrWifi = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scrWifi, lv_color_hex(0x101418), 0);

  lv_obj_t *t = lv_label_create(scrWifi);
  lv_label_set_text(t, "Wi-Fi");
  lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(t, lv_color_white(), 0);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 6);

  lblWifiStatus = lv_label_create(scrWifi);
  lv_obj_set_style_text_font(lblWifiStatus, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lblWifiStatus, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_width(lblWifiStatus, 440);
  lv_label_set_long_mode(lblWifiStatus, LV_LABEL_LONG_DOT);
  lv_obj_align(lblWifiStatus, LV_ALIGN_TOP_MID, 0, 46);
  lv_label_set_text(lblWifiStatus, "Scanning...");

  lstWifi = lv_list_create(scrWifi);
  lv_obj_set_size(lstWifi, 440, 152);
  lv_obj_align(lstWifi, LV_ALIGN_TOP_MID, 0, 78);
  lv_obj_set_style_text_font(lstWifi, &lv_font_montserrat_20, 0);

  mkBtn(scrWifi, 20, 262, 130, 44, LV_SYMBOL_REFRESH " Scan", wifiScanCb, nullptr);
  mkBtn(scrWifi, 170, 262, 120, 44, "Forget", wifiForgetCb, nullptr);
  mkBtn(scrWifi, 310, 262, 150, 44, LV_SYMBOL_LEFT " Back", wifiBackCb, nullptr);
}

static void buildWifiPass() {
  scrWifiPass = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scrWifiPass, lv_color_hex(0x101418), 0);

  lv_obj_t *t = lv_label_create(scrWifiPass);
  lv_label_set_text(t, "Password");
  lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(t, lv_color_white(), 0);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 6);

  lblWifiSsid = lv_label_create(scrWifiPass);
  lv_obj_set_style_text_font(lblWifiSsid, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lblWifiSsid, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_width(lblWifiSsid, 440);
  lv_label_set_long_mode(lblWifiSsid, LV_LABEL_LONG_DOT);
  lv_obj_align(lblWifiSsid, LV_ALIGN_TOP_MID, 0, 44);
  lv_label_set_text(lblWifiSsid, "");

  taWifiPass = lv_textarea_create(scrWifiPass);
  lv_obj_set_size(taWifiPass, 440, 44);
  lv_obj_align(taWifiPass, LV_ALIGN_TOP_MID, 0, 74);
  lv_textarea_set_one_line(taWifiPass, true);
  lv_textarea_set_password_mode(taWifiPass, true);
  lv_textarea_set_max_length(taWifiPass, 63);
  lv_obj_set_style_text_font(taWifiPass, &lv_font_montserrat_20, 0);

  kbWifiPass = lv_keyboard_create(scrWifiPass);
  lv_keyboard_set_textarea(kbWifiPass, taWifiPass);
  lv_obj_align(kbWifiPass, LV_ALIGN_TOP_MID, 0, 128);

  mkBtn(scrWifiPass, 170, 276, 140, 36, "CONNECT", wifiPassConnectCb, nullptr);
  mkBtn(scrWifiPass, 410, 276, 60, 36, "X", wifiPassCancelCb, nullptr);
}

// ================= публичное API =================

void uiBegin() {
  // Настройки яркости/сна из NVS
  brightPct = (uint8_t)stGetF("bright", 60.0f);
  sleepMin = (uint8_t)stGetF("sleepMin", 0.0f);

  // Вибромотор (LEDC — уровень вибрации регулируется заполнением)
  ledcAttach(Pin::VIB, 1000, 10);
  ledcWrite(Pin::VIB, 0);
  vibEnabled = stGetF("vibEn", 1.0f) >= 0.5f;
  vibLvl = constrain(stGetF("vibLvl", 1.0f), 0.7f, 1.0f);
  vibDuty = (uint32_t)(vibLvl * 1023.0f);

  // Зуммер
  ledcAttach(Pin::BUZZER, 4000, SND_BITS);
  ledcWrite(Pin::BUZZER, 0);
  sndEnabled = stGetF("sndEn", 1.0f) >= 0.5f;
  sndLvl = constrain(stGetF("sndLvl", 20.0f), 10.0f, 100.0f);
  sndDuty = (uint32_t)(sndLvl * 1023.0f / 100.0f);

  ledcAttach(Pin::LCD_BL, 5000, 8);
  ledcWrite(Pin::LCD_BL, (uint32_t)(brightPct * 255 / 100));

  tft.init();
  tft.setRotation(1);          // ландшафт 480x320

  Wire.begin(Pin::CTP_SDA, Pin::CTP_SCL);
  Wire.setTimeOut(5);          // зависшая шина I2C не должна блокировать UI > 5 мс
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
  buildClockSet();
  buildFeedback();
  buildWifi();
  buildWifiPass();
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
      vibPattern(3, 100, 120);
      sndPlay(M_WARN, 3);
      break;
    case MotorEvent::ENC_STALL:
      uiNotice("Encoder silent!", 5000);
      vibPattern(3, 100, 120);
      sndPlay(M_WARN, 3);
      break;
    case MotorEvent::CAL_HIT_MIN:
      if (wizActive) wizShow(WIZ_PAUSE);
      uiNotice("Bottom reached. Going up in 3 s...", 4000);
      vibPulse(80);
      sndPlay(M_STEP, 1);
      break;
    case MotorEvent::CAL_HIT_MAX:
      if (wizActive) wizShow(WIZ_VERIFY);
      uiNotice("Top reached. Checking...", 5000);
      vibPulse(80);
      sndPlay(M_STEP, 1);
      break;
    case MotorEvent::CAL_VERIFIED:
      vibPulse(120);
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
      vibPattern(3, 100, 120);
      sndPlay(M_WARN, 3);
      break;
    case MotorEvent::CAL_TIMEOUT:
      uiNotice("Calibration failed", 5000);
      vibPattern(3, 100, 120);
      sndPlay(M_WARN, 3);
      break;
    case MotorEvent::END_LIMIT:
      uiNotice("End of travel", 2000);
      vibPattern(2, 100, 120);
      sndPlay(M_LIMIT, 3);
      break;
    case MotorEvent::REACHED_TARGET:
      uiNotice("Done", 1500);
      vibPulse(150);
      sndPlay(M_DONE, 3);
      break;
    default:
      break;
  }
}

void uiTask() {
  vibTask();   // проигрывание паттернов вибрации
  sndTask();   // проигрывание мелодий

  // ДИАГНОСТИКА: если обработчик LVGL тормозит >20 мс — печатаем максимум
  uint32_t t0 = millis();
  lv_timer_handler();
  uint32_t dt = millis() - t0;
  static uint32_t maxDt = 0, lastRep = 0;
  if (dt > maxDt) maxDt = dt;
  if (millis() - lastRep > 5000) {
    if (maxDt > 20) {
      Serial0.printf("UI: slow lv_timer_handler %lu ms\n", (unsigned long)maxDt);
    }
    maxDt = 0;
    lastRep = millis();
  }

  // Сон: гасим подсветку после таймаута без касаний (только когда мотор стоит)
  if (sleepMin > 0 && backlightOn &&
      motor.state() == MotorState::IDLE &&
      (millis() - lastActivity > (uint32_t)sleepMin * 60000UL)) {
    backlightOn = false;
    ledcWrite(Pin::LCD_BL, 0);
  }

  // Часы DS3231: читаем дважды в секунду, метку обновляем при смене минуты.
  // Значок Wi-Fi рядом с часами появляется/пропадает при смене подключения.
  static uint32_t lastClockRead = 0;
  static int lastMin = -1;
  static bool lastWifiShown = false;
  if (millis() - lastClockRead > 500) {
    lastClockRead = millis();
    wifiUiPoll();   // обновление экрана Wi-Fi (скан/статус подключения)
    DateTime t;
    bool conn = wifiIsConnected();
    if (clockPresent() && clockGet(t) && !clockOscStopped()) {
      if (t.min != lastMin || conn != lastWifiShown) {
        lastMin = t.min;
        lastWifiShown = conn;
        char b[48];
        snprintf(b, sizeof(b), "%02d:%02d  %s %02d.%02d%s",
                 t.hour, t.min, clockWdayName(t.year, t.mon, t.day), t.day, t.mon,
                 conn ? "  " LV_SYMBOL_WIFI : "");
        lv_label_set_text(lblClock, b);
      }
    } else if (lastMin != -2) {
      lastMin = -2;
      lv_label_set_text(lblClock, "--:--");
    }
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
