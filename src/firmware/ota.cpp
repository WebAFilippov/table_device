#include "ota.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
volatile OtaState gState = OtaState::IDLE;
volatile int gProgress = 0;
volatile uint16_t gAvail = 0;
volatile bool wantCheck = false, wantInstall = false;
char dlUrl[256] = "";
uint32_t lastAutoCheck = 0;

const char *apiUrl() {
  static char u[160];
  snprintf(u, sizeof(u), "https://api.github.com/repos/%s/%s/releases/latest",
           GITHUB_OWNER, GITHUB_REPO);
  return u;
}

// Мини-парсер JSON без зависимостей: ищем tag_name и ассет firmware.bin
bool parseRelease(const String &json, uint16_t &ver, char *url, size_t urlSize) {
  int ti = json.indexOf("\"tag_name\"");
  if (ti < 0) return false;
  int c1 = json.indexOf('"', ti + 11) + 1;
  int c2 = json.indexOf('"', c1);
  String tag = json.substring(c1, c2);      // напр. "v1.0"
  int maj = 0, min = 0;
  sscanf(tag.c_str(), "v%d.%d", &maj, &min);
  ver = (uint16_t)(maj * 100 + min);
  if (ver <= 0) return false;

  // в релизе ищём именно ассет firmware.bin (source.zip нам не нужен)
  int ai = json.indexOf("firmware.bin");
  if (ai < 0) return false;
  int ui = json.indexOf("\"browser_download_url\"", ai);
  if (ui < 0) return false;
  int u1 = json.indexOf('"', ui + 23) + 1;
  int u2 = json.indexOf('"', u1);
  if (u2 - u1 >= (int)urlSize) return false;
  json.substring(u1, u2).toCharArray(url, urlSize);
  return true;
}

bool doCheck(uint16_t &ver, char *url, size_t urlSize) {
  WiFiClientSecure client;
  client.setInsecure();   // пропускаем проверку сертификата (для простоты)
  HTTPClient http;
  http.setTimeout(15000);
  http.begin(client, apiUrl());
  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    String body = http.getString();
    http.end();
    return parseRelease(body, ver, url, urlSize);
  }
  http.end();
  return false;
}

bool doInstall(const char *url) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(20000);
  http.begin(client, url);
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return false; }
  int len = http.getSize();
  if (len <= 0 || !Update.begin((size_t)len)) { http.end(); return false; }

  gProgress = 0;
  WiFiClient *stream = http.getStreamPtr();
  uint8_t buf[4096];
  int read = 0, total = 0;
  while ((read = stream->read(buf, sizeof(buf))) > 0) {
    Update.write(buf, (size_t)read);
    total += read;
    gProgress = (int)((long)total * 100 / len);
  }
  http.end();
  if (total != len) { Update.abort(); return false; }
  return Update.end();   // при успехе новый раздел станет загрузочным
}

void otaTaskFn(void *) {
  for (;;) {
    if (wantCheck) {
      wantCheck = false;
      gState = OtaState::CHECKING;
      gAvail = 0;
      if (WiFi.status() != WL_CONNECTED) {
        gState = OtaState::NO_WIFI;
      } else {
        uint16_t ver = 0;
        if (doCheck(ver, dlUrl, sizeof(dlUrl))) {
          if (ver > FW_VERSION) {
            gAvail = ver;
            gState = OtaState::UPDATE_AVAILABLE;
          } else {
            gState = OtaState::NO_UPDATE;
          }
        } else {
          gState = OtaState::ERROR;
        }
      }
    }

    if (wantInstall) {
      wantInstall = false;
      gState = OtaState::DOWNLOADING;
      bool ok = (WiFi.status() == WL_CONNECTED) && doInstall(dlUrl);
      gState = ok ? OtaState::DONE : OtaState::ERROR;
    }

    // авто-проверка раз в сутки (первая — через ~30 с после старта)
    if (WiFi.status() == WL_CONNECTED && gState == OtaState::IDLE &&
        millis() - lastAutoCheck > 24UL * 3600UL * 1000UL) {
      lastAutoCheck = millis();
      wantCheck = true;
    }

    delay(200);
  }
}
} // namespace

void otaBegin() {
  // первая проверка через 30 с (чтобы WiFi успел подключиться)
  lastAutoCheck = millis() - (24UL * 3600UL * 1000UL) + 30000UL;
  xTaskCreatePinnedToCore(otaTaskFn, "ota", 8192, nullptr, 1, nullptr, 0);
}

void otaCheck() { wantCheck = true; }
void otaInstall() { wantInstall = true; }
OtaState otaState() { return (OtaState)gState; }
int otaProgress() { return gProgress; }
uint16_t otaAvailVersion() { return gAvail; }
uint16_t fwVersion() { return FW_VERSION; }

const char *fwVersionStr(uint16_t v) {
  static char b[12];
  snprintf(b, sizeof(b), "%d.%d", (int)(v / 100), (int)(v % 100));
  return b;
}
