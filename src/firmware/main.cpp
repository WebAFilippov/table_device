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
    Serial0.println("save <1..4> | goP <1..4> | setOc <ампер> | resetcal");
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
    Serial0.println("NVS очищено. Перезагрузи плату и сделай cal заново.");
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


  // Диагностика PSRAM (важно для LVGL)
  Serial0.printf("PSRAM: found=%d size=%d KB\n",
                 psramFound() ? 1 : 0, (int)(ESP.getPsramSize() / 1024));

  xTaskCreatePinnedToCore(motorTask, "motor", 8192, nullptr, 12, nullptr, 0);

  Serial0.println("== Стол: прошивка v0.1 (CLI) ==");
  Serial0.println("help — список команд");
  printStatus();
}

void loop() {
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
