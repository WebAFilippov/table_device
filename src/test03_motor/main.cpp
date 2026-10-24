/**
 * ТЕСТ 3: драйвер BTS7960 + мотор 24В + энкодер
 *
 * Сборка: PlatformIO, окружение test03
 *
 * Пины:
 *   RPWM=38, LPWM=39, R_EN=40, L_EN=41
 *   Токи: R_IS -> [1 кОм] -> GND, средняя точка -> GPIO1 (ADC1_CH0)
 *         L_IS -> [1 кОм] -> GND, средняя точка -> GPIO2 (ADC1_CH1)
 *   Энкодер: A (зелёный) -> [R1] -> GPIO15, GPIO15 -> [R2] -> GND
 *            B (синий)   -> [R1] -> GPIO16, GPIO16 -> [R2] -> GND
 *            (делители ОБЯЗАТЕЛЬНЫ: выходы энкодера до ~4.8 В;
 *             подойдут 1к/2к, 2.2к/3.3к, 4.7к/10к и т.п.)
 *
 * Оценка тока: I ~= V_IS / 1000 * 8500  (kILIS ~ 8500, точность ~±20%)
 *
 * Команды (Serial-монитор):
 *   f / r — вперёд / назад на текущей скорости
 *   1..9  — скорость (1 ~= 11% ШИМ, 9 = 100%)
 *   s / 0 — стоп
 *   z     — обнулить счётчик энкодера
 *   i     — показать токи
 *   m     — ЗАМЕР: ехать вперёд MEASURE_TICKS тиков и остановиться
 *   n     — ЗАМЕР: то же назад
 *
 *   После замера прошивка напечатает фактический delta (тики).
 *   Измерь пройденное расстояние линейкой: mm/тик = мм / delta.
 *
 * БЕЗОПАСНОСТЬ:
 *   - сначала ручной режим '1' короткими импульсами, определи направление!
 *   - НЕ упирайся в концы хода
 *   - холостой ток ~1 А (паспорт); увидел >5 А — сразу 's'
 */
#include <Arduino.h>
#include <ESP32Encoder.h>

static const int RPWM = 38, LPWM = 39, REN = 40, LEN = 41;
static const int IS_R = 1, IS_L = 2;
static const int ENC_A = 15, ENC_B = 16;

static const uint32_t PWM_FREQ = 10000;         // LEDC на XTAL 40 МГц: 10 кГц x 11 бит -> div=1, реально ~19.5 кГц
                                                // (12 бит не влезает: 10кГц x 4096 = 41 МГц > 40 МГц XTAL — ledcAttach падал!)
static const int PWM_BITS = 11;
static const int PWM_MAX = (1 << PWM_BITS) - 1; // 4095

// --- параметры режима замера (правь под себя) ---
static const int64_t MEASURE_TICKS = 1500;      // сколько тиков проехать
static const uint32_t MEASURE_DUTY = 500;       // ~24% ШИМ на замере (шкала 11 бит: 500/2047)

ESP32Encoder enc;

static bool running = false;
static bool fwd = true;
static uint32_t duty = 0;

static bool measureMode = false;
static bool measureFwd = true;
static int64_t measureStart = 0;

static void pwmSetup(int pin) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  if (!ledcAttach(pin, PWM_FREQ, PWM_BITS)) {
    Serial0.printf("ERROR: ledcAttach(pin=%d, %lu Hz, %d bit) failed!\n",
                   pin, (unsigned long)PWM_FREQ, PWM_BITS);
  }
#else
  static int ch = 0;
  ledcSetup(ch, PWM_FREQ, PWM_BITS);
  ledcAttachPin(pin, ch);
  ch++;
#endif
}

static void pwmWritePin(int pin, uint32_t d) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(pin, d);
#else
  ledcWrite((pin == RPWM) ? 0 : 1, d);
#endif
}

static void motorStop() {
  pwmWritePin(RPWM, 0);
  pwmWritePin(LPWM, 0);
  running = false;
}

static void motorRun(bool forward, uint32_t d) {
  static bool lastFwd = true;
  if (running && forward != lastFwd) {
    motorStop();
    delay(250);                 // пауза перед сменой направления
  }
  lastFwd = forward;
  fwd = forward;
  duty = d;
  running = true;
  if (forward) { pwmWritePin(RPWM, d); pwmWritePin(LPWM, 0); }
  else         { pwmWritePin(RPWM, 0); pwmWritePin(LPWM, d); }
}

static void printCurrents() {
  int r = analogRead(IS_R);
  int l = analogRead(IS_L);
  float iR = r * 3.1f / 4095.0f * 8.5f;   // *8500/1000 Ом
  float iL = l * 3.1f / 4095.0f * 8.5f;
  float i = (iR > iL) ? iR : iL;
  Serial0.printf("count=%lld duty=%u raw(R=%d,L=%d) I~=%.2f A\n",
                 (long long)enc.getCount(),
                 running ? (unsigned)duty : 0, r, l, i);
}

static void startMeasure(bool forward) {
  measureMode = true;
  measureFwd = forward;
  measureStart = enc.getCount();
  motorRun(forward, MEASURE_DUTY);
  Serial0.printf("MEASURE: едем %s %lld тиков...\n",
                 forward ? "вперёд" : "назад", (long long)MEASURE_TICKS);
}

void setup() {
  Serial0.begin(115200);
  delay(300);

  // Пока прошивка стартует — прижимаем ВСЁ к нулю, чтобы мотор не дёргался
  pinMode(RPWM, OUTPUT);
  pinMode(LPWM, OUTPUT);
  digitalWrite(RPWM, LOW);
  digitalWrite(LPWM, LOW);
  pinMode(REN, OUTPUT);
  pinMode(LEN, OUTPUT);
  digitalWrite(REN, LOW);
  digitalWrite(LEN, LOW);

  pwmSetup(RPWM);
  pwmSetup(LPWM);
  motorStop();

  enc.attachFullQuad(ENC_A, ENC_B);   // квадратура x4
  enc.setCount(0);

  // Драйвер разрешаем только теперь
  digitalWrite(REN, HIGH);
  digitalWrite(LEN, HIGH);

  Serial0.println("== Тест BTS7960 + мотор + энкодер ==");
  Serial0.println("f/r — вперёд/назад | 1..9 — скорость | s — стоп");
  Serial0.println("z — ноль энкодера | i — токи | m/n — замер (вперёд/назад)");
  Serial0.println("Начинай с '1' и коротких импульсов!");
}

void loop() {
  while (Serial0.available()) {
    char c = (char)Serial0.read();
    if (c == 'f' || c == 'r') {
      measureMode = false;
      motorRun(c == 'f', duty ? duty : 400);
    } else if (c == 's' || c == '0') {
      measureMode = false;
      motorStop();
    } else if (c == 'z') {
      enc.setCount(0);
      Serial0.println("encoder=0");
    } else if (c == 'i') {
      printCurrents();
    } else if (c == 'm' || c == 'n') {
      startMeasure(c == 'm');
    } else if (c >= '1' && c <= '9') {
      measureMode = false;
      motorRun(fwd, (uint32_t)(c - '0') * PWM_MAX / 9);
    }
  }

  // Контроль режима замера
  if (measureMode) {
    int64_t d = enc.getCount() - measureStart;
    bool reached = measureFwd ? (d >= MEASURE_TICKS) : (d <= -MEASURE_TICKS);
    if (reached) {
      motorStop();
      delay(400);                       // дать мотору полностью остановиться
      int64_t delta = enc.getCount() - measureStart;
      measureMode = false;
      Serial0.printf("MEASURE done: delta=%lld тиков\n", (long long)delta);
      Serial0.printf("Измерь расстояние линейкой. mm/тик = мм / %lld\n", (long long)delta);
    }
  }

  static uint32_t last = 0;
  if (running && millis() - last > 250) {
    last = millis();
    printCurrents();
  }
}
