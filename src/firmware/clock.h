#pragma once
#include <Arduino.h>

// Часы DS3231 (I2C 0x68, та же шина, что тач на GPIO8/9).
// В RTC хранится МЕСТНОЕ время; пояс (минуты, +420 = Красноярск) — в NVS.

struct DateTime {
  int year = 2024, mon = 1, day = 1, wday = 1;
  int hour = 0, min = 0, sec = 0;
};

void clockBegin();              // проверить наличие DS3231, печать статуса
bool clockPresent();            // чип отвечает на шине?
bool clockGet(DateTime &t);     // прочитать местное время
bool clockSet(const DateTime &t); // записать (24-часовой режим)
bool clockOscStopped();         // DS3231 терял питание → время сбито
int  clockTzMin();              // пояс из NVS (минуты)
void clockSetTz(int tzMin);     // сменить пояс: пересчёт времени в RTC + NVS
