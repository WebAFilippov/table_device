#include "motor.h"
#include "config.h"
#include "storage.h"

namespace {
ESP32Encoder enc;
constexpr uint8_t EVQ_SIZE = 8;
}

void Motor::begin() {
  // Пока стартуем — всё прижато к нулю, чтобы мотор не дёргался
  pinMode(Pin::RPWM, OUTPUT); digitalWrite(Pin::RPWM, LOW);
  pinMode(Pin::LPWM, OUTPUT); digitalWrite(Pin::LPWM, LOW);
  pinMode(Pin::REN, OUTPUT);  digitalWrite(Pin::REN, LOW);
  pinMode(Pin::LEN, OUTPUT);  digitalWrite(Pin::LEN, LOW);

  if (!ledcAttach(Pin::RPWM, PWM_FREQ, PWM_BITS) ||
      !ledcAttach(Pin::LPWM, PWM_FREQ, PWM_BITS)) {
    Serial0.println("!! ОШИБКА: ledcAttach не удался");
  }
  ledcWrite(Pin::RPWM, 0);
  ledcWrite(Pin::LPWM, 0);

  enc.attachFullQuad(Pin::ENC_A, Pin::ENC_B);
  enc.setCount(0);
  pos = 0;
  lastPos = 0;

  loadCalib();
  if (calib.valid) {
    enc.setCount(calib.savedPos);   // восстанавливаем позицию (винт самотормозящий)
    pos = calib.savedPos;
    lastPos = pos;
  }

  // Разрешаем драйвер только теперь
  digitalWrite(Pin::REN, HIGH);
  digitalWrite(Pin::LEN, HIGH);
  tPrev = millis();
}

void Motor::task() {
  uint32_t now = millis();
  float dt = (now - tPrev) / 1000.0f;
  tPrev = now;
  if (dt <= 0.001f) dt = 0.001f;
  if (dt > 0.100f) dt = 0.010f;

  pos = enc.getCount();

  // --- скорость (тики/с), вверх = отрицательные тики ---
  float vRaw = (float)(pos - lastPos) / dt;
  lastPos = pos;
  vFilt += V_FILT_A * (vRaw - vFilt);
  vMeas = vFilt;
  vMmS = -vMeas * mmPerTick;   // вверх = положительная

  // --- ток ---
  int r = analogRead(Pin::IS_R);
  int l = analogRead(Pin::IS_L);
  float rawMax = (float)((r > l) ? r : l);
  curFilt += CUR_FILT_A * (rawMax - curFilt);
  curA = curFilt * RAW_TO_AMPS;

  // --- сторож: мотор включён, а позиция замерла ---
  // Два смысла: (1) механический упор/препятствие, (2) мёртвый энкодер.
  // Различаем по тому, ехал ли мотор с начала команды.
  // ВАЖНО: детекция по ЗАМОРОЗКЕ позиции, а не по току — БП 5 А не тянет
  // стопорный ток мотора (14.7 А) и уходит в циклическую перезагрузку!
  bool inCal = (st == MotorState::CAL_DOWN || st == MotorState::CAL_UP);
  bool dutyOn = (activePin >= 0);
  if (dutyOn) {
    if (llabs(pos - stallPos) >= 2) {
      stallPos = pos;
      stallSince = now;
      movedSinceCmd = true;
    } else if (now - stallSince >= STALL_DETECT_MS) {
      bool jamPushed = (curFilt > (float)INSTANT_JAM_RAW);
      if (inCal) {
        // упор найден по заморозке позиции
        stopNow();
        if (st == MotorState::CAL_DOWN) {
          calib.tMax = pos;              // низ = максимум тиков
          postEvent(MotorEvent::CAL_HIT_MIN);
          pendSt = MotorState::CAL_UP;
          pendAt = now + CAL_PAUSE_MS;
        } else {
          calib.tMin = pos;              // верх = минимум тиков
          saveCalib();
          postEvent(MotorEvent::CAL_HIT_MAX);
          pendSt = MotorState::IDLE;
          pendAt = now + 300;
        }
        st = MotorState::IDLE;
      } else if (movedSinceCmd || jamPushed) {
        // застряли в пути: стоп + отъезд в ПРОТИВОПОЛОЖНУЮ сторону.
        // ВАЖНО: знак берём ДО stopNow() — он обнуляет vSet!
        float s = (vSet >= 0.0f) ? -1.0f : 1.0f;
        stopNow();
        target = pos + (int64_t)(REVERSE_MM * ticksPerMm() * s);
        profileSpeed = PRESET_SPEED_MM_S;
        st = MotorState::REVERSING;
        movedSinceCmd = false;
        postEvent(MotorEvent::OBSTACLE);
      } else {
        stopNow();
        st = MotorState::ERROR;
        postEvent(MotorEvent::ENC_STALL);
      }
      return;
    }
  } else {
    stallPos = pos;
    stallSince = now;
  }

  // --- перегрузка по току ---
  if (curFilt > ocRaw) {
    if (!ocActive) { ocActive = true; ocSince = now; }
  } else {
    ocActive = false;
  }

  if (ocActive && (now - ocSince >= OC_HOLD_MS)) {
    ocActive = false;
    if (inCal) {
      stopNow();
      if (st == MotorState::CAL_DOWN) {
        calib.tMax = pos;              // низ = максимум тиков
        postEvent(MotorEvent::CAL_HIT_MIN);
        pendSt = MotorState::CAL_UP;
        pendAt = now + CAL_PAUSE_MS;
      } else {
        calib.tMin = pos;              // верх = минимум тиков
        saveCalib();
        postEvent(MotorEvent::CAL_HIT_MAX);
        pendSt = MotorState::IDLE;
        pendAt = now + 300;
      }
      st = MotorState::IDLE;
    } else {
      bool nearEnd = calib.valid &&
        ((pos > calib.tMax - END_MARGIN_TICKS) ||
         (pos < calib.tMin + END_MARGIN_TICKS));
      if (nearEnd) {
        stopNow();
        st = MotorState::IDLE;
        postEvent(MotorEvent::END_LIMIT);
      } else {
        // препятствие/зажали: стоп + отъезд в противоположную сторону.
        // ВАЖНО: знак берём ДО stopNow() — он обнуляет vSet!
        float s = (vSet >= 0.0f) ? -1.0f : 1.0f;
        stopNow();
        target = pos + (int64_t)(REVERSE_MM * ticksPerMm() * s);
        profileSpeed = PRESET_SPEED_MM_S;
        st = MotorState::REVERSING;
        postEvent(MotorEvent::OBSTACLE);
      }
    }
  }

  // --- таймаут калибровки ---
  if ((st == MotorState::CAL_DOWN || st == MotorState::CAL_UP) &&
      (now - calStartMs > CAL_TIMEOUT_MS)) {
    stopNow();
    st = MotorState::ERROR;
    postEvent(MotorEvent::CAL_TIMEOUT);
  }

  // --- отложенный переход фаз калибровки ---
  if (pendAt != 0 && now >= pendAt) {
    st = pendSt;
    pendAt = 0;
    pendSt = MotorState::IDLE;
    if (st == MotorState::CAL_DOWN || st == MotorState::CAL_UP) {
      calStartMs = now;
    }
  }

  // --- контур по состоянию ---
  float vCmd = 0.0f;
  switch (st) {
    case MotorState::IDLE:
    case MotorState::ERROR:
      vCmd = 0.0f;
      break;

    case MotorState::MANUAL:
      vCmd = manualUp ? -manualV : manualV;
      break;

    case MotorState::MOVE_TO:
    case MotorState::REVERSING: {
      int64_t errT = target - pos;
      if (llabs(errT) <= 4 && fabsf(vMeas) < 3.0f) {
        stopNow();
        postEvent(MotorEvent::REACHED_TARGET);
        st = MotorState::IDLE;
        vCmd = 0.0f;
      } else {
        vCmd = profileVel(errT);
      }
      break;
    }

    case MotorState::CAL_DOWN:
      vCmd = CAL_SPEED_MM_S * ticksPerMm();
      break;
    case MotorState::CAL_UP:
      vCmd = -CAL_SPEED_MM_S * ticksPerMm();
      break;
  }

  // --- мягкие границы хода (если калибровано) ---
  if (calib.valid && (st == MotorState::MANUAL || st == MotorState::MOVE_TO)) {
    if (vCmd > 0.0f && pos > calib.tMax - END_MARGIN_TICKS) {
      vCmd = 0.0f;
      if (!endLimNotified) { postEvent(MotorEvent::END_LIMIT); endLimNotified = true; }
    }
    if (vCmd < 0.0f && pos < calib.tMin + END_MARGIN_TICKS) {
      vCmd = 0.0f;
      if (!endLimNotified) { postEvent(MotorEvent::END_LIMIT); endLimNotified = true; }
    }
    if ((vCmd > 0.0f && pos <= calib.tMax - END_MARGIN_TICKS) ||
        (vCmd < 0.0f && pos >= calib.tMin + END_MARGIN_TICKS)) {
      endLimNotified = false;
    }
  }

  setPwm(vCmd);
}

// S-профиль по оставшейся дистанции: v = min(vmax, sqrt(2*a*d))
float Motor::profileVel(int64_t errTicks) const {
  float vmaxT = profileSpeed * ticksPerMm();
  float aT = ACCEL_MM_S2 * ticksPerMm();
  float rem = (float)llabs(errTicks);
  if (rem < 1.0f) rem = 1.0f;
  float vCap = (vmaxT < sqrtf(2.0f * aT * rem)) ? vmaxT : sqrtf(2.0f * aT * rem);
  return (errTicks >= 0) ? vCap : -vCap;
}

void Motor::setPwm(float vCmd) {
  // ограничение ускорения (трапеция)
  float aT = ACCEL_MM_S2 * ticksPerMm();
  float dvMax = aT * 0.01f;
  vSet += constrain(vCmd - vSet, -dvMax, dvMax);

  // ПИД по скорости + форвард
  float errV = vSet - vMeas;
  integ += errV * 0.01f * KI_V;
  integ = constrain(integ, -I_MAX, I_MAX);
  float ff = 0.0f;
  if (fabsf(vSet) > 1.0f) {
    float ffm = FF_START + fabsf(vSet) * FF_K;
    if (ffm > 0.85f) ffm = 0.85f;
    ff = (vSet > 0.0f) ? ffm : -ffm;
  }
  float d = ff + KP_V * errV + integ;
  d = constrain(d, -dutyCap, dutyCap);

  // полная остановка: зачищаем всё
  if (fabsf(vCmd) < 0.5f && fabsf(vSet) < 2.0f && fabsf(vMeas) < 2.0f) {
    d = 0.0f;
    integ = 0.0f;
    vSet = 0.0f;
  }

  dOut = d;
  applyDuty(d);
}

void Motor::applyDuty(float d) {
  uint32_t now = millis();
  int want = (d > 0.005f) ? Pin::RPWM : (d < -0.005f) ? Pin::LPWM : -1;

  // смена направления — только через паузу
  if (want >= 0 && activePin >= 0 && want != activePin) {
    ledcWrite(Pin::RPWM, 0);
    ledcWrite(Pin::LPWM, 0);
    activePin = -1;
    stopUntil = now + (uint32_t)DEAD_TIME_MS;
  }

  if (want >= 0 && activePin < 0 && now < stopUntil) return; // ждём паузу

  if (want < 0) {
    ledcWrite(Pin::RPWM, 0);
    ledcWrite(Pin::LPWM, 0);
    activePin = -1;
    return;
  }

  uint32_t u = (uint32_t)(fabsf(d) * (float)PWM_MAX);
  if (want == Pin::RPWM) { ledcWrite(Pin::RPWM, u); ledcWrite(Pin::LPWM, 0); }
  else                   { ledcWrite(Pin::LPWM, u); ledcWrite(Pin::RPWM, 0); }
  activePin = want;
}

void Motor::stopNow() {
  ledcWrite(Pin::RPWM, 0);
  ledcWrite(Pin::LPWM, 0);
  activePin = -1;
  dOut = 0.0f;
  vSet = 0.0f;
  vFilt = 0.0f;
  integ = 0.0f;
  saveCalib();   // сохраняем позицию (rate-limited)
}

// ---------- команды ----------

void Motor::cmdManual(bool up) {
  if (!calib.valid) return;   // без калибровки свободно кататься нельзя
  stopNow();
  manualUp = up;
  manualV = MANUAL_SPEED_MM_S * ticksPerMm();
  st = MotorState::MANUAL;
  dutyCap = NORM_DUTY_CAP;
  endLimNotified = false;
  movedSinceCmd = false;
  pendAt = 0;                 // снимаем отложенный переход калибровки
  pendSt = MotorState::IDLE;
}

void Motor::cmdStop() {
  stopNow();
  st = MotorState::IDLE;
  movedSinceCmd = false;
  pendAt = 0;                 // отмена: отменяем и отложенный переход калибровки
  pendSt = MotorState::IDLE;
}

void Motor::cmdGotoMm(float mm) {
  if (!calib.valid) return;
  float range = calib.hMax - calib.hMin;
  if (range <= 0.0f) return;
  mm = constrain(mm, calib.hMin, calib.hMax);
  target = calib.tMax - (int64_t)((mm - calib.hMin) / mmPerTick);
  profileSpeed = PRESET_SPEED_MM_S;
  st = MotorState::MOVE_TO;
  dutyCap = NORM_DUTY_CAP;
  endLimNotified = false;
  movedSinceCmd = false;
  pendAt = 0;
  pendSt = MotorState::IDLE;
}

void Motor::cmdCalibrate() {
  if (st == MotorState::CAL_DOWN || st == MotorState::CAL_UP) return;
  stopNow();
  calib.valid = false;   // старые значения недействительны до конца калибровки
  calStartMs = millis();
  dutyCap = CAL_DUTY_CAP;   // не даём БП 5А уйти в защиту
  endLimNotified = false;
  movedSinceCmd = false;
  st = MotorState::CAL_DOWN;
}

// ---------- события ----------

void Motor::postEvent(MotorEvent e) {
  if (e == MotorEvent::NONE) return;
  uint8_t next = (uint8_t)((evHead + 1) % EVQ_SIZE);
  if (next != evTail) { evQ[evHead] = e; evHead = next; }
}

MotorEvent Motor::pollEvent() {
  if (evHead == evTail) return MotorEvent::NONE;
  MotorEvent e = evQ[evTail];
  evTail = (uint8_t)((evTail + 1) % EVQ_SIZE);
  return e;
}

// ---------- доступ ----------

float Motor::posMm() const {
  if (!calib.valid) return NAN;
  return calib.hMin + (float)(calib.tMax - pos) * mmPerTick;
}

const char* Motor::stateName() const {
  switch (st) {
    case MotorState::IDLE:      return "IDLE";
    case MotorState::MANUAL:    return "MANUAL";
    case MotorState::MOVE_TO:   return "MOVE_TO";
    case MotorState::CAL_DOWN:  return "CAL_DOWN";
    case MotorState::CAL_UP:    return "CAL_UP";
    case MotorState::REVERSING: return "REVERSING";
    case MotorState::ERROR:     return "ERROR";
  }
  return "?";
}

void Motor::setHeightsMm(float hMin, float hMax) {
  calib.hMin = hMin;
  calib.hMax = hMax;
  // Калибровка считается завершённой только после ввода высот
  if (calib.tMax > calib.tMin && hMax > hMin) {
    mmPerTick = (hMax - hMin) / (float)(calib.tMax - calib.tMin);
    calib.valid = true;
  }
  saveCalib();
}

void Motor::setOcAmps(float a) {
  a = constrain(a, 1.0f, 8.0f);
  ocRaw = a / RAW_TO_AMPS;
  lastSave = 0;   // форсируем запись в NVS сразу
  saveCalib();
}

// ---------- NVS ----------

void Motor::loadCalib() {
  calib.valid   = stGetB("cvalid", false);
  calib.tMin    = stGetI64("tMin", 0);
  calib.tMax    = stGetI64("tMax", 0);
  calib.hMin    = stGetF("hMin", 0);
  calib.hMax    = stGetF("hMax", 0);
  mmPerTick     = stGetF("mpt", MM_PER_TICK_DEFAULT);
  calib.savedPos = stGetI64("pos", 0);
  ocRaw          = stGetF("oc", (float)OC_RAW);
}

void Motor::saveCalib() {
  uint32_t now = millis();
  if (now - lastSave < 2000) return;   // не молотим флеш
  lastSave = now;
  calib.savedPos = pos;
  stPutB("cvalid", calib.valid);
  stPutI64("tMin", calib.tMin);
  stPutI64("tMax", calib.tMax);
  stPutF("hMin", calib.hMin);
  stPutF("hMax", calib.hMax);
  stPutF("mpt", mmPerTick);
  stPutI64("pos", pos);
  stPutF("oc", ocRaw);
}
