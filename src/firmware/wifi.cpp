#include "wifi.h"
#include "clock.h"
#include "storage.h"
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <time.h>

namespace {
constexpr int MAX_NETS = 24;

WifiNet nets[MAX_NETS];
volatile int netCount = 0;
volatile bool scanDone = false;
volatile bool wantScan = false;

char wantSsid[33] = "";
char wantPass[65] = "";
volatile bool wantConnect = false, wantDisconnect = false;
volatile bool connectBusy = false, connectDone = false, connectOk = false;

uint32_t lastNtp = 0;

// NTP: получаем UTC и пишем МЕСТНОЕ время в DS3231 — часы остаются
// единственным источником для UI, интернет их только корректирует.
void ntpSync() {
  configTime(clockTzMin() * 60, 0, "pool.ntp.org", "time.nist.gov");
  struct tm tmv;
  if (getLocalTime(&tmv, 8000)) {
    DateTime t;
    t.year = tmv.tm_year + 1900;
    t.mon  = tmv.tm_mon + 1;
    t.day  = tmv.tm_mday;
    t.hour = tmv.tm_hour;
    t.min  = tmv.tm_min;
    t.sec  = tmv.tm_sec;
    t.wday = tmv.tm_wday == 0 ? 7 : tmv.tm_wday;
    clockSet(t);
    Serial0.printf("NTP: время синхронизировано %02d:%02d:%02d\n", t.hour, t.min, t.sec);
  } else {
    Serial0.println("NTP: не удалось получить время");
  }
  lastNtp = millis();
}

void wifiTaskFn(void *) {
  for (;;) {
    if (wantScan) {
      wantScan = false;
      int n = WiFi.scanNetworks();   // ~2-4 с — мы в фоне, UI не блокируется
      netCount = (n > 0) ? min(n, MAX_NETS) : 0;
      for (int i = 0; i < netCount; i++) {
        strncpy(nets[i].ssid, WiFi.SSID(i).c_str(), 32);
        nets[i].ssid[32] = 0;
        nets[i].rssi = WiFi.RSSI(i);
        nets[i].auth = WiFi.encryptionType(i);
      }
      WiFi.scanDelete();
      scanDone = true;
    }

    if (wantConnect) {
      wantConnect = false;
      connectBusy = true;
      connectDone = false;
      WiFi.disconnect();
      WiFi.begin(wantSsid, wantPass);
      uint32_t t0 = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
        delay(100);
      }
      connectOk = (WiFi.status() == WL_CONNECTED);
      if (connectOk) {
        stPutS("wifiSsid", String(wantSsid));
        stPutS("wifiPass", String(wantPass));
        Serial0.printf("WiFi: подключено к %s, IP %s\n", wantSsid, WiFi.localIP().toString().c_str());
        ntpSync();
      } else {
        Serial0.printf("WiFi: не удалось подключиться к %s\n", wantSsid);
      }
      connectBusy = false;
      connectDone = true;
    }

    if (wantDisconnect) {
      wantDisconnect = false;
      WiFi.disconnect(true);
    }

    // периодическая коррекция времени (раз в 6 часов), пока есть сеть
    if (WiFi.status() == WL_CONNECTED && millis() - lastNtp > 6UL * 3600UL * 1000UL) {
      ntpSync();
    }

    delay(100);
  }
}
} // namespace

void wifiBegin() {
  String saved = stGetS("wifiSsid", "");
  if (saved.length() > 0) {
    strncpy(wantSsid, saved.c_str(), 32);
    strncpy(wantPass, stGetS("wifiPass", "").c_str(), 64);
    wantSsid[32] = 0;
    wantPass[64] = 0;
    wantConnect = true;
    Serial0.printf("WiFi: авто-подключение к %s...\n", wantSsid);
  }
  xTaskCreatePinnedToCore(wifiTaskFn, "wifi", 10240, nullptr, 1, nullptr, 0);
}

bool wifiIsConnected() { return WiFi.status() == WL_CONNECTED; }
int8_t wifiRssi() { return (int8_t)WiFi.RSSI(); }
String wifiCurrentSsid() { return WiFi.SSID(); }
String wifiIp() { return WiFi.localIP().toString(); }

void wifiRequestScan() {
  scanDone = false;
  netCount = 0;
  wantScan = true;
}
bool wifiScanDone() { return scanDone; }
int wifiScanCount() { return netCount; }
const WifiNet &wifiNet(int i) { return nets[i]; }

void wifiConnect(const char *ssid, const char *pass) {
  strncpy(wantSsid, ssid, 32);
  strncpy(wantPass, pass, 64);
  wantSsid[32] = 0;
  wantPass[64] = 0;
  connectDone = false;
  connectOk = false;
  wantConnect = true;
}
bool wifiConnectBusy() { return connectBusy; }
bool wifiConnectDone() { return connectDone; }
bool wifiConnectOk() { return connectOk; }
void wifiDisconnect() { wantDisconnect = true; }

String wifiSavedSsid() { return stGetS("wifiSsid", ""); }
String wifiSavedPass() { return stGetS("wifiPass", ""); }
void wifiErase() {
  stPutS("wifiSsid", "");
  stPutS("wifiPass", "");
  wantDisconnect = true;
}
