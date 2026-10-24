/**
 * ТЕСТ 2: энкодер (квадратура) через аппаратный PCNT
 *
 * Сборка: PlatformIO, окружение test02
 * Библиотека: ESP32Encoder (madhephaestus/ESP32Encoder)
 *
 * Пины: A (зелёный)=15, B (синий)=16
 *
 * ВАЖНО: выходы энкодера качаются до ~4.8 В, а GPIO ESP32-S3 держат
 * максимум ~3.6 В. На КАЖДЫЙ сигнал обязателен делитель:
 *   энкодер A -> [1 кОм] -> GPIO15, и от GPIO15 [1.5 кОм] -> GND.
 *   энкодер B -> [1 кОм] -> GPIO16, и от GPIO16 [1.5 кОм] -> GND.
 */
#include <Arduino.h>
#include <ESP32Encoder.h>

ESP32Encoder enc;

void setup() {
  Serial0.begin(115200);
  delay(300);

  enc.attachFullQuad(15, 16);   // квадратура, счёт x4
  enc.setCount(0);

  Serial0.println("== Тест энкодера ==");
  Serial0.println("Мотор обесточен. Крути ВАЛ МОТОРА вручную.");
  Serial0.println("1 оборот вала мотора:     показание / 4 = PPR энкодера");
  Serial0.println("1 оборот выходного вала:  показание / 4 = PPR * передаточное число");
  Serial0.println("Команда 'r' — сброс счётчика.");
}

void loop() {
  static int64_t last = 0;
  int64_t c = enc.getCount();
  if (c != last) {
    Serial0.printf("count=%lld\n", (long long)c);
    last = c;
  }

  while (Serial0.available()) {
    if (Serial0.read() == 'r') {
      enc.setCount(0);
      last = 0;
      Serial0.println("reset");
    }
  }
  delay(10);
}
