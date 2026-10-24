/**
 * ТЕСТ 1: дисплей ST7796 (320x480 SPI) + ёмкостный тач FT6336U (I2C)
 *
 * Сборка: PlatformIO, окружение test01 (зависимости подтянутся сами)
 *
 * Пины:
 *   MOSI=11, SCK=12, MISO=13 (можно не подключать), CS=10, DC(RS)=4, RST=5
 *   Тач: SDA=8, SCL=9, CTP_RST=6, CTP_INT=14 (не используется — поллинг)
 *   Подсветка LED=7 (ШИМ)
 *
 * Тач читаем напрямую по I2C (адрес 0x38) без сторонних библиотек.
 */
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>

TFT_eSPI tft = TFT_eSPI();

static const int CTP_RST = 6;
static const int LED_PIN = 7;
static const uint8_t FT_ADDR = 0x38;

static uint8_t ftRead(uint8_t reg) {
  Wire.beginTransmission(FT_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return 0xFF;
  if (Wire.requestFrom((uint16_t)FT_ADDR, (uint8_t)1) != 1) return 0xFF;
  return Wire.read();
}

void setup() {
  Serial0.begin(115200);
  delay(300);

  // --- Подсветка ~47% (не нагружаем 3.3В; позже поднимем) ---
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(LED_PIN, 5000, 8);
  ledcWrite(LED_PIN, 120);
#else
  ledcSetup(0, 5000, 8);
  ledcAttachPin(LED_PIN, 0);
  ledcWrite(0, 120);
#endif

  // --- Дисплей ---
  tft.init();
  tft.setRotation(1);        // 480x320
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Display OK: ST7796");
  tft.setCursor(10, 40);
  tft.println("480 x 320");
  const uint16_t cols[6] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_YELLOW, TFT_CYAN, TFT_MAGENTA};
  for (int i = 0; i < 6; i++) tft.fillRect(0, 80 + i * 36, 480, 36, cols[i]);
  tft.setTextSize(2);
  tft.setCursor(10, 300);
  tft.print("Touch: ");

  // --- Тач ---
  Wire.begin(8, 9);              // SDA=8, SCL=9
  pinMode(CTP_RST, OUTPUT);
  digitalWrite(CTP_RST, LOW);
  delay(20);
  digitalWrite(CTP_RST, HIGH);
  delay(300);

  uint8_t vendor = ftRead(0xA8); // vendor id
  uint8_t chip = ftRead(0xA3);   // chip id
  Serial0.printf("Touch: vendor=0x%02X chip=0x%02X\n", vendor, chip);
  if (vendor == 0xFF || chip == 0xFF) {
    Serial0.println("!!! Тач НЕ отвечает: проверь SDA/SCL (8/9), питание 3.3В, RST (6).");
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.println("NO I2C");
  } else {
    Serial0.println("Touch: I2C OK. Веди пальцем по экрану.");
    tft.println("OK");
  }
}

void loop() {
  uint8_t status = ftRead(0x02); // TD_STATUS: число касаний
  bool pressed = (status != 0xFF) && ((status & 0x0F) > 0);

  if (pressed) {
    uint8_t buf[4];
    Wire.beginTransmission(FT_ADDR);
    Wire.write(0x03);            // P1_XH ... P1_YL
    Wire.endTransmission();
    if (Wire.requestFrom((uint16_t)FT_ADDR, (uint8_t)4) == 4) {
      for (int i = 0; i < 4; i++) buf[i] = Wire.read();
      int rawX = ((buf[0] & 0x0F) << 8) | buf[1]; // 0..479 (длинная сторона)
      int rawY = ((buf[2] & 0x0F) << 8) | buf[3]; // 0..319

      // FT6336U на этом модуле выдаёт rawX = 0..319 (короткая сторона,
      // 0 — у НИЖНЕГО края), rawY = 0..479 (длинная сторона, 0 — у ЛЕВОГО края).
      // В ландшафте 480x320:
      int16_t dx = rawY;        // длинная сторона -> ширина экрана (0..479)
      int16_t dy = 319 - rawX;  // короткая сторона -> высота экрана (0..319)
      // Если кружок зеркалит по горизонтали: dx = 479 - rawY;
      // Если по вертикали:                   dy = rawX;

      dx = constrain((int)dx, 0, 479);
      dy = constrain((int)dy, 0, 319);

      tft.fillCircle(dx, dy, 18, TFT_WHITE);
      Serial0.printf("raw=(%d,%d) disp=(%d,%d)\n", rawX, rawY, dx, dy);
    }
  }
  delay(30);
}
