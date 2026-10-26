#pragma once
#include <Arduino.h>
#include <WiFi.h>   // для wifi_auth_mode_t (WIFI_AUTH_*)

// WiFi в фоновой FreeRTOS-задаче: скан и подключение блокирующие по своей
// природе (2-12 с), поэтому выполняются НЕ в цикле UI — стол и экран не
// замирают. Сохранённая сеть подключается автоматически при старте.

struct WifiNet {
  char ssid[33];
  int8_t rssi;
  uint8_t auth;     // 0 = открытая, иначе нужен пароль
};

void wifiBegin();                       // старт фоновой задачи + авто-подключение
bool wifiIsConnected();
int8_t wifiRssi();
String wifiCurrentSsid();
String wifiIp();

void wifiRequestScan();                 // запрос скана (асинхронно)
bool wifiScanDone();                    // скан завершён
int wifiScanCount();                    // сколько сетей найдено
const WifiNet &wifiNet(int i);

void wifiConnect(const char *ssid, const char *pass);   // асинхронно
bool wifiConnectBusy();                 // подключение в процессе
bool wifiConnectDone();                 // попытка завершена
bool wifiConnectOk();                   // успех
void wifiDisconnect();

String wifiSavedSsid();                 // сохранённая сеть
String wifiSavedPass();
void wifiErase();                       // забыть сеть и отключиться
