#include "clock.h"
#include "storage.h"
#include <Wire.h>
#include <time.h>

namespace {
constexpr uint8_t DS_ADDR = 0x68;

uint8_t dec2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
uint8_t bcd2dec(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }

bool rtcRead(uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(DS_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((uint16_t)DS_ADDR, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

bool rtcWrite(uint8_t reg, const uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(DS_ADDR);
  Wire.write(reg);
  for (uint8_t i = 0; i < len; i++) Wire.write(buf[i]);
  return Wire.endTransmission() == 0;
}
} // namespace

void clockBegin() {
  if (clockPresent()) {
    Serial0.println("RTC DS3231: найден (0x68)");
    if (clockOscStopped()) {
      Serial0.println("RTC: ВНИМАНИЕ — время сбито (был перерыв питания/батарейки).");
      Serial0.println("     Задай время: setTime ЧЧ:ММ[:СС] [ДД.ММ.ГГГГ]");
    }
  } else {
    Serial0.println("RTC DS3231: НЕ найден — проверь VCC/GND/SDA/SCL");
  }
}

bool clockPresent() {
  Wire.beginTransmission(DS_ADDR);
  return Wire.endTransmission() == 0;
}

bool clockGet(DateTime &t) {
  uint8_t b[7];
  if (!rtcRead(0x00, b, 7)) return false;
  t.sec  = bcd2dec(b[0] & 0x7F);
  t.min  = bcd2dec(b[1] & 0x7F);
  t.hour = bcd2dec(b[2] & 0x3F);   // 24-часовой режим
  t.wday = b[3];
  t.day  = bcd2dec(b[4] & 0x3F);
  t.mon  = bcd2dec(b[5] & 0x1F);
  t.year = bcd2dec(b[6]) + 2000;
  return true;
}

bool clockSet(const DateTime &t) {
  uint8_t b[7];
  b[0] = dec2bcd((uint8_t)(t.sec % 60));
  b[1] = dec2bcd((uint8_t)(t.min % 60));
  b[2] = dec2bcd((uint8_t)(t.hour % 24));   // бит 6 = 0 → 24-часовой формат
  b[3] = (uint8_t)((t.wday >= 1 && t.wday <= 7) ? t.wday : 1);
  b[4] = dec2bcd((uint8_t)constrain(t.day, 1, 31));
  b[5] = dec2bcd((uint8_t)constrain(t.mon, 1, 12));
  b[6] = dec2bcd((uint8_t)(t.year % 100));
  if (!rtcWrite(0x00, b, 7)) return false;
  // Сброс флага OSF («время сбито») в регистре статуса
  uint8_t s = 0x00;
  rtcWrite(0x0F, &s, 1);
  return true;
}

bool clockOscStopped() {
  uint8_t s;
  if (!rtcRead(0x0F, &s, 1)) return false;
  return (s & 0x80) != 0;
}

int clockTzMin() {
  return (int)stGetF("tzMin", 420.0f);   // +7 часов по умолчанию (Красноярск)
}

const char *clockWdayName(int y, int m, int d) {
  // формула Зеллера: 0 = суббота
  if (m < 3) { m += 12; y -= 1; }
  int k = y % 100, j = y / 100;
  int h = (d + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
  static const char *nm[7] = {"Sat", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri"};
  return nm[h];
}

void clockSetTz(int tzMin) {
  tzMin = constrain(tzMin, -12 * 60, 14 * 60);
  int old = clockTzMin();
  if (tzMin == old) return;

  DateTime t;
  if (clockGet(t)) {
    // пересчитываем местное время в RTC на дельту пояса
    struct tm tmv = {};
    tmv.tm_year = t.year - 1900;
    tmv.tm_mon  = t.mon - 1;
    tmv.tm_mday = t.day;
    tmv.tm_hour = t.hour;
    tmv.tm_min  = t.min;
    tmv.tm_sec  = t.sec;
    time_t tt = mktime(&tmv);
    tt += (time_t)(tzMin - old) * 60;
    struct tm *l = localtime(&tt);
    t.year = l->tm_year + 1900;
    t.mon  = l->tm_mon + 1;
    t.day  = l->tm_mday;
    t.hour = l->tm_hour;
    t.min  = l->tm_min;
    t.sec  = l->tm_sec;
    t.wday = l->tm_wday == 0 ? 7 : l->tm_wday;
    clockSet(t);
  }
  stPutF("tzMin", (float)tzMin);
}
