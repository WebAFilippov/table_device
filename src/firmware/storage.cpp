#include "storage.h"
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static Preferences prefs;
static SemaphoreHandle_t sMutex = nullptr;

void storageInit() {
  if (sMutex == nullptr) {
    sMutex = xSemaphoreCreateMutex();
  }
}

static void lock() {
  if (sMutex) xSemaphoreTake(sMutex, portMAX_DELAY);
}

static void unlock() {
  if (sMutex) xSemaphoreGive(sMutex);
}

float stGetF(const char* key, float def) {
  lock();
  prefs.begin("table", true);
  float v = prefs.getFloat(key, def);
  prefs.end();
  unlock();
  return v;
}

void stPutF(const char* key, float v) {
  lock();
  prefs.begin("table", false);
  prefs.putFloat(key, v);
  prefs.end();
  unlock();
}

int64_t stGetI64(const char* key, int64_t def) {
  lock();
  prefs.begin("table", true);
  int64_t v = prefs.getLong64(key, def);
  prefs.end();
  unlock();
  return v;
}

void stPutI64(const char* key, int64_t v) {
  lock();
  prefs.begin("table", false);
  prefs.putLong64(key, v);
  prefs.end();
  unlock();
}

bool stGetB(const char* key, bool def) {
  lock();
  prefs.begin("table", true);
  bool v = prefs.getBool(key, def);
  prefs.end();
  unlock();
  return v;
}

void stPutB(const char* key, bool v) {
  lock();
  prefs.begin("table", false);
  prefs.putBool(key, v);
  prefs.end();
  unlock();
}

String stGetS(const char* key, const char* def) {
  lock();
  prefs.begin("table", true);
  String v = prefs.getString(key, def);
  prefs.end();
  unlock();
  return v;
}

void stPutS(const char* key, const String &v) {
  lock();
  prefs.begin("table", false);
  prefs.putString(key, v);
  prefs.end();
  unlock();
}

void stClear() {
  lock();
  prefs.begin("table", false);
  prefs.clear();
  prefs.end();
  unlock();
}
