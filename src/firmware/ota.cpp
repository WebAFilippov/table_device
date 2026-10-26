#include "ota.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
volatile OtaState gState = OtaState::IDLE;
volatile int gProgress = 0;
volatile uint16_t gAvail = 0;
volatile bool wantCheck = false, wantInstall = false;
char dlUrl[256] = "";
uint32_t lastAutoCheck = 0;
int gLastPct = -1;

HTTPUpdate httpUpdate;

void otaProgressCb(int cur, int total) {
  gProgress = (total > 0) ? (int)((long)cur * 100 / total) : 0;
  if (gProgress / 10 != gLastPct / 10) {
    gLastPct = gProgress;
    Serial0.printf("OTA: %d%% (%d из %d)\n", gProgress, cur, total);
  }
}

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
  Serial0.printf("OTA: проверяю %s\n", apiUrl());
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.begin(client, apiUrl());
  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    String body = http.getString();
    http.end();
    bool ok = parseRelease(body, ver, url, urlSize);
    if (ok) {
      Serial0.printf("OTA: релиз v%d.%d, ассет: %s\n", (int)(ver / 100), (int)(ver % 100), url);
    } else {
      Serial0.println("OTA: не удалось разобрать JSON релиза");
    }
    return ok;
  }
  http.end();
  Serial0.printf("OTA: проверка не удалась, HTTP %d\n", code);
  return false;
}

bool doInstall(const char *url) {
  Serial0.printf("OTA: качаю %s\n", url);
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30);   // 30 с на чтение сокета

  // HTTPUpdate сам ходит по 302-редиректам GitHub и пишет в OTA-раздел
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(false);
  httpUpdate.onProgress(otaProgressCb);
  gProgress = 0;
  gLastPct = -1;

  HTTPUpdateResult ret = httpUpdate.update(client, url);
  if (ret == HTTP_UPDATE_OK) {
    Serial0.println("OTA: записано успешно");
    return true;
  }
  Serial0.printf("OTA: ошибка (%d): %s\n",
                 httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
  return false;
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
      if (!ok) Serial0.println("OTA: установка не удалась (детали выше)");
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
  // Стек 16 КБ: TLS-скачивание прошивки ест много стека (был stack canary
  // panic при 8 КБ)
  xTaskCreatePinnedToCore(otaTaskFn, "ota", 16384, nullptr, 1, nullptr, 0);
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
