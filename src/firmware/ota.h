#pragma once
#include <Arduino.h>

// OTA-обновление с GitHub Releases (фоновое, не блокирует стол).
// Прошивка публикуется как релиз с тегом "v1.0" и ассетом firmware.bin.

enum class OtaState : uint8_t {
  IDLE,             // ничего не делаем
  CHECKING,         // спрашиваем GitHub
  NO_UPDATE,        // обновлений нет
  UPDATE_AVAILABLE, // есть новая версия
  NO_WIFI,          // нет сети
  DOWNLOADING,      // качаем .bin (смотри otaProgress)
  INSTALLING,       // пишем во второй раздел
  DONE,             // готово — можно перезагружаться
  ERROR             // не удалось
};

void otaBegin();                 // фоновая задача + авто-проверка после старта
void otaCheck();                 // запросить проверку (UI/CLI)
void otaInstall();               // запросить установку найденной версии (UI)
OtaState otaState();
int otaProgress();               // 0..100 для DOWNLOADING
uint16_t otaAvailVersion();      // версия найденного обновления
uint16_t fwVersion();
const char *fwVersionStr(uint16_t v);   // "1.0"
