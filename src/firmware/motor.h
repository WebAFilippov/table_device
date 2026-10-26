#pragma once
#include <Arduino.h>
#include <ESP32Encoder.h>
#include "config.h"

enum class MotorState : uint8_t {
  IDLE,        // стоим
  MANUAL,      // удержание кнопки
  MOVE_TO,     // едем к целевой высоте (пресет)
  CAL_DOWN,    // калибровка: вниз до упора
  CAL_UP,      // калибровка: вверх до упора
  CAL_VERIFY,  // калибровка: контрольный спуск вниз (круговая проверка)
  REVERSING,   // отъезд после «зажали»
  ERROR        // ошибка, требуется вмешательство
};

enum class MotorEvent : uint8_t {
  NONE,
  REACHED_TARGET,  // доехали
  CAL_HIT_MIN,     // калибровка: нижний упор найден
  CAL_HIT_MAX,     // калибровка: верхний упор найден
  CAL_VERIFIED,    // круговая проверка пройдена (низ подтверждён)
  CAL_VERIFY_FAIL, // круговая проверка НЕ пройдена
  END_LIMIT,       // мягкая граница хода
  OBSTACLE,        // перегрузка в середине хода → стоп + реверс
  CAL_TIMEOUT,     // калибровка: упор не найден
  ENC_STALL        // мотор едет, энкодер молчит
};

class Motor {
public:
  void begin();
  void task();                 // вызывать каждые 10 мс

  void cmdManual(bool up);     // вверх = true
  void cmdStop();
  void cmdGotoMm(float mm);    // абсолютная высота, мм (нужна калибровка)
  void cmdCalibrate();

  MotorEvent pollEvent();

  MotorState state() const { return st; }
  const char* stateName() const;
  int64_t posTicks() const { return pos; }
  float posMm() const;         // NaN, если не калибровано
  float speedMmS() const { return vMmS; }     // + = вверх
  float dutyFrac() const { return dOut; }     // 0..1
  float currentA() const { return curA; }
  bool calibrated() const { return calib.valid; }

  void setHeightsMm(float hMin, float hMax);
  void setMmPerTick(float v) { mmPerTick = v; }
  float mmPerTickValue() const { return mmPerTick; }
  int64_t ticksMin() const { return calib.tMin; }
  int64_t ticksMax() const { return calib.tMax; }

  void setOcAmps(float a);                       // порог перегрузки, А (в NVS)
  float ocAmps() const { return ocRaw * RAW_TO_AMPS; }

private:
  float ticksPerMm() const { return 1.0f / mmPerTick; }
  void setPwm(float vCmd);                    // vCmd: тики/с, + = вверх
  float profileVel(int64_t errTicks) const;   // S-профиль по дистанции
  void applyDuty(float d);                    // d: + = вверх (RPWM), - = вниз (LPWM)
  void stopNow();
  void postEvent(MotorEvent e);
  void loadCalib();
  void saveCalib();

  struct Calib {
    bool valid = false;
    int64_t tMin = 0, tMax = 0;   // тики: низ = минимум, верх = максимум
    int64_t savedPos = 0;         // позиция на момент выключения
    float hMin = 0, hMax = 0;     // высоты в мм (от пола)
  } calib;

  MotorState st = MotorState::IDLE;
  MotorEvent evQ[8];
  uint8_t evHead = 0, evTail = 0;

  int64_t pos = 0, lastPos = 0, target = 0;
  float vSet = 0, vMeas = 0, vMmS = 0, vFilt = 0;
  float integ = 0, dOut = 0;
  float curA = 0, curFilt = 0;
  float ocRaw = (float)OC_RAW;   // порог перегрузки в raw-отсчётах (меняется setOc)
  float mmPerTick = MM_PER_TICK_DEFAULT;
  float profileSpeed = PRESET_SPEED_MM_S;
  float manualV = 0;
  bool manualUp = false;

  bool ocActive = false;
  uint32_t ocSince = 0;
  int64_t stallPos = 0;
  uint32_t stallSince = 0;
  bool movedSinceCmd = false;
  float dutyCap = NORM_DUTY_CAP;
  uint32_t calStartMs = 0;
  uint32_t tPrev = 0;
  uint32_t stopUntil = 0;
  uint32_t lastSave = 0;
  uint32_t pendAt = 0;
  MotorState pendSt = MotorState::IDLE;
  bool endLimNotified = false;
  int activePin = -1;   // какой из RPWM/LPWM активен (-1 = стоп)
};
