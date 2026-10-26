/**
 * СТОЛ С РЕГУЛИРОВКОЙ ВЫСОТЫ — прошивка v0.1 (управление по Serial)
 *
 * Сборка: PlatformIO, окружение firmware
 *
 * Команды (Serial-монитор, 115200):
 *   help                 — список команд
 *   status               — позиция/скорость/ток
 *   up | down | stop     — ручной режим
 *   cal                  — калибровка: вниз до упора, пауза, вверх до упора
 *   setH <Hмин> <Hмакс>  — ввести измеренные линейкой высоты (мм от пола)
 *   go <мм>              — поехать на высоту
 *   save <1..4>          — сохранить текущую высоту в пресет
 *   goP <1..4>           — поехать к пресету
 *   setOc <ампер>        — порог перегрузки по току (сохраняется в NVS)
 *   resetcal             — сбросить калибровку/пресеты (NVS)
 *
 * Логика:
 *   - S-кривая + ПИД по скорости, 10 мс цикл, LEDC ~19.5 кГц
 *   - ток R_IS/L_IS: порог 4.5 А x 250 мс. У края хода = «дошли»,
 *     в середине = «зажали» → стоп + реверс 15 мм
 *   - позиция/калибровка/пресеты хранятся в NVS, позиция восстанавливается
 *     после включения (червяк + винт самотормозящие)
 */
#include <Arduino.h>
#include <esp32-hal-psram.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config.h"
#include "motor.h"
#include "storage.h"
#include "ui.h"
#include "clock.h"
#include "wifi.h"
#include "ota.h"
#include <esp_ota_ops.h>

Motor motor;

// Задача управления мотором: 10 мс, ядро 0, высокий приоритет
void motorTask(void*) {
  for (;;) {
    motor.task();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ---------- печать ----------

static void printStatus() {
  String h = motor.calibrated() ? (String(motor.posMm(), 1) + " мм")
                                : String("-- (нет калибровки)");
  Serial0.printf("[%s] высота=%s | ticks=%lld | v=%+.1f мм/с | duty=%3.0f%% | I=%.2f A | OC=%.1f A\n",
                 motor.stateName(), h.c_str(),
                 (long long)motor.posTicks(),
                 motor.speedMmS(),
                 motor.dutyFrac() * 100.0f,
                 motor.currentA(),
                 motor.ocAmps());
}

static void printEvent(MotorEvent e) {
  switch (e) {
    case MotorEvent::REACHED_TARGET:
      Serial0.printf("EVENT: доехали, высота %.1f мм\n", motor.posMm());
      break;
    case MotorEvent::CAL_HIT_MIN:
      Serial0.printf("EVENT: нижний упор найден (ticks=%lld).\n", (long long)motor.posTicks());
      Serial0.println("       Пауза 3 с, потом вверх...");
      break;
    case MotorEvent::CAL_HIT_MAX:
      Serial0.printf("EVENT: верхний упор найден (ticks=%lld).\n", (long long)motor.posTicks());
      Serial0.println("       Контрольный спуск вниз...");
      break;
    case MotorEvent::CAL_VERIFIED:
      Serial0.println("EVENT: круговая проверка пройдена — низ подтверждён");
      break;
    case MotorEvent::CAL_VERIFY_FAIL:
      Serial0.println("EVENT: круговая проверка НЕ пройдена! Проверь энкодер/механику");
      break;
    case MotorEvent::END_LIMIT:
      Serial0.println("EVENT: мягкая граница хода");
      break;
    case MotorEvent::OBSTACLE:
      Serial0.printf("EVENT: ПЕРЕГРУЗКА %.2f A — стоп + отъезд 15 мм\n", motor.currentA());
      break;
    case MotorEvent::CAL_TIMEOUT:
      Serial0.println("EVENT: калибровка — упор не найден за 2 мин. Проверь ток/механику.");
      break;
    case MotorEvent::ENC_STALL:
      Serial0.println("EVENT: мотор едет, а энкодер молчит — проверь кабель энкодера!");
      break;
    default:
      break;
  }
}

// ---------- CLI ----------

static void handleLine(String s) {
  s.trim();
  if (s.length() == 0) return;
  int sp = s.indexOf(' ');
  String cmd = (sp < 0) ? s : s.substring(0, sp);
  String arg = (sp < 0) ? "" : s.substring(sp + 1);
  cmd.toLowerCase();

  if (cmd == "help") {
    Serial0.println("help | status | up | down | stop | go <мм> | cal | setH <Hмин> <Hмакс>");
    Serial0.println("save <1..4> | goP <1..4> | setOc <ампер> | setV <мм/с 10..25> | resetcal");
    Serial0.println("time | setTime ЧЧ:ММ[:СС] [ДД.ММ.ГГГГ] | setTz <+7> | wifi | ota | otaf");
  } else if (cmd == "status" || cmd == "pos") {
    printStatus();
    Serial0.printf("калибровка: %s | ticks[min=%lld, max=%lld] | mm/тик=%.5f\n",
                   motor.calibrated() ? "OK" : "нет",
                   (long long)motor.ticksMin(), (long long)motor.ticksMax(),
                   motor.mmPerTickValue());
  } else if (cmd == "up") {
    motor.cmdManual(true);
  } else if (cmd == "down") {
    motor.cmdManual(false);
  } else if (cmd == "stop") {
    motor.cmdStop();
  } else if (cmd == "go") {
    if (!motor.calibrated()) {
      Serial0.println("Сначала калибровка: cal, затем setH");
    } else {
      float mm = arg.toFloat();
      Serial0.printf("Едем на %.1f мм...\n", mm);
      motor.cmdGotoMm(mm);
    }
  } else if (cmd == "cal") {
    Serial0.println("Калибровка: освободи место над/под столом! Едем вниз...");
    motor.cmdCalibrate();
  } else if (cmd == "seth") {
    float h1 = 0, h2 = 0;
    if (sscanf(arg.c_str(), "%f %f", &h1, &h2) == 2 && h2 > h1) {
      motor.setHeightsMm(h1, h2);
      Serial0.printf("Готово: mm/тик = %.5f. Текущая высота %.1f мм\n",
                     motor.mmPerTickValue(), motor.posMm());
    } else {
      Serial0.println("Формат: setH <Hмин_мм> <Hмакс_мм>");
    }
  } else if (cmd == "setoc") {
    float a = arg.toFloat();
    if (a >= 1.0f && a <= 8.0f) {
      motor.setOcAmps(a);
      Serial0.printf("Порог перегрузки установлен: %.1f A\n", motor.ocAmps());
    } else {
      Serial0.println("Формат: setOc <ампер 1..8>");
    }
  } else if (cmd == "setv") {
    float v = arg.toFloat();
    if (v >= MANUAL_SPEED_MIN_MM_S && v <= MANUAL_SPEED_MAX_MM_S) {
      motor.setManualSpeedMmS(v);
      stPutF("manSpd", motor.manualSpeedMmS());
      Serial0.printf("Ручная скорость: %.0f мм/с\n", motor.manualSpeedMmS());
    } else {
      Serial0.println("Формат: setV <мм/с 10..25>");
    }
  } else if (cmd == "time") {
    DateTime t;
    if (clockGet(t)) {
      Serial0.printf("Время: %02d:%02d:%02d  %02d.%02d.%04d (UTC%+d)\n",
                     t.hour, t.min, t.sec, t.day, t.mon, t.year, clockTzMin() / 60);
      if (clockOscStopped()) {
        Serial0.println("ВНИМАНИЕ: DS3231 терял питание — время сбито, задай setTime");
      }
    } else {
      Serial0.println("DS3231 не найден на шине I2C");
    }
  } else if (cmd == "settime") {
    int hh = -1, mm = -1, ss = 0, dd = 0, mo = 0, yyyy = 0;
    int n = sscanf(arg.c_str(), "%d:%d:%d %d.%d.%d", &hh, &mm, &ss, &dd, &mo, &yyyy);
    if (n >= 2 && hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59 && ss >= 0 && ss <= 59) {
      DateTime t;
      bool haveDate = (n >= 5 && dd >= 1 && dd <= 31 && mo >= 1 && mo <= 12 && yyyy >= 2000);
      if (!haveDate && !clockGet(t)) t = DateTime();
      t.hour = hh;
      t.min = mm;
      t.sec = ss;
      if (haveDate) { t.day = dd; t.mon = mo; t.year = yyyy; t.wday = 1; }
      clockSet(t);
      Serial0.printf("Записано: %02d:%02d:%02d %02d.%02d.%04d\n",
                     t.hour, t.min, t.sec, t.day, t.mon, t.year);
    } else {
      Serial0.println("Формат: setTime ЧЧ:ММ[:СС] [ДД.ММ.ГГГГ]");
    }
  } else if (cmd == "settz") {
    int tzMin = 0;
    if (arg.length() > 0 && (arg[0] == '+' || arg[0] == '-')) {
      tzMin = arg.toInt() * 60;
    } else {
      tzMin = arg.toInt();
    }
    if (tzMin >= -720 && tzMin <= 840) {
      clockSetTz(tzMin);
      Serial0.printf("Часовой пояс: UTC%+d\n", clockTzMin() / 60);
    } else {
      Serial0.println("Формат: setTz <+7> (или минуты, напр. 420)");
    }
  } else if (cmd == "wifi") {
    if (wifiIsConnected()) {
      Serial0.printf("WiFi: %s, IP %s, RSSI %d dBm\n",
                     wifiCurrentSsid().c_str(), wifiIp().c_str(), (int)wifiRssi());
    } else {
      Serial0.printf("WiFi: не подключено (сохранено: %s)\n",
                     wifiSavedSsid().length() ? wifiSavedSsid().c_str() : "нет");
    }
  } else if (cmd == "ota") {
    Serial0.printf("Прошивка: v%s. Проверяю обновления на GitHub...\n", fwVersionStr(fwVersion()));
    otaCheck();
  } else if (cmd == "otaf") {
    // принудительная установка последнего релиза (для отладки/переустановки)
    Serial0.println("OTA: принудительная установка последнего релиза...");
    otaCheck();
    otaInstall();
  } else if (cmd == "save") {
    int n = arg.toInt();
    if (n >= 1 && n <= 4 && motor.calibrated()) {
      stPutF(("p" + String(n)).c_str(), motor.posMm());
      Serial0.printf("Пресет %d сохранён: %.1f мм\n", n, motor.posMm());
    } else {
      Serial0.println("save <1..4> (нужна калибровка)");
    }
  } else if (cmd == "gop") {
    int n = arg.toInt();
    if (n >= 1 && n <= 4 && motor.calibrated()) {
      float mm = stGetF(("p" + String(n)).c_str(), NAN);
      if (!isnan(mm)) {
        Serial0.printf("Едем к пресету %d: %.1f мм\n", n, mm);
        motor.cmdGotoMm(mm);
      } else {
        Serial0.println("Пресет пуст");
      }
    } else {
      Serial0.println("goP <1..4> (нужна калибровка)");
    }
  } else if (cmd == "resetcal") {
    stClear();
    // Перезагрузка нужна, потому что мотор/UI держат калибровку и пресеты
    // в оперативной памяти (загружены из NVS при старте) — очистка NVS их
    // не обновляет. Перезагружаемся сами, кнопка не нужна: после рестарта
    // откроется визард калибровки.
    Serial0.println("NVS очищено. Перезагружаюсь...");
    Serial0.flush();
    delay(200);
    ESP.restart();
  } else {
    Serial0.println("Неизвестная команда. help");
  }
}

// ---------- setup/loop ----------

void setup() {
  Serial0.begin(115200);
  delay(300);

  storageInit();
  motor.begin();

  uiBegin();
  clockBegin();   // DS3231: после uiBegin (Wire уже поднят на GPIO8/9)
  wifiBegin();    // фоновый WiFi: авто-подключение сохранённой сети + NTP
  otaBegin();     // фоновое OTA: авто-проверка обновлений через 30 с


  // Диагностика PSRAM (важно для LVGL)
  Serial0.printf("PSRAM: found=%d size=%d KB\n",
                 psramFound() ? 1 : 0, (int)(ESP.getPsramSize() / 1024));

  xTaskCreatePinnedToCore(motorTask, "motor", 8192, nullptr, 12, nullptr, 0);

  Serial0.printf("== Стол: прошивка v%s (CLI) ==\n", fwVersionStr(fwVersion()));
  Serial0.println("help — список команд");
  printStatus();
}

void loop() {
  // Подтверждение работоспособности прошивки через 30 с после старта:
  // если это была OTA-установка и она не дожила — загрузчик откатится сам
  static bool rollbackOk = false;
  if (!rollbackOk && millis() > 30000) {
    rollbackOk = true;
    esp_ota_mark_app_valid_cancel_rollback();
    Serial0.println("OTA: прошивка подтверждена (откат отменён)");
  }

  MotorEvent e = motor.pollEvent();
  if (e != MotorEvent::NONE) {
    printEvent(e);
    uiEvent(e);
  }

  uiTask();

  static String buf;
  while (Serial0.available()) {
    char c = (char)Serial0.read();
    if (c == '\n' || c == '\r') {
      if (buf.length() > 0) { handleLine(buf); buf = ""; }
    } else if (buf.length() < 60) {
      buf += c;
    }
  }

  static uint32_t last = 0;
  if (millis() - last > 1000) {
    last = millis();
    if (motor.state() != MotorState::IDLE) printStatus();
  }
  delay(5);
}
