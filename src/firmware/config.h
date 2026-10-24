#pragma once
#include <Arduino.h>

// ================= Пины =================
namespace Pin {
  constexpr int ENC_A = 15, ENC_B = 16;   // энкодер (через делители!)
  constexpr int RPWM = 38, LPWM = 39;     // ШИМ драйвера
  constexpr int REN  = 40, LEN  = 41;     // разрешение драйвера
  constexpr int IS_R = 1, IS_L = 2;       // датчики тока (R_IS/L_IS через 1к на GND)
}

// ================= ШИМ =================
// LEDC в ядре 3.3.x тактируется от XTAL 40 МГц.
// 10 кГц x 11 бит = 20.5 МГц -> делитель 1, реально ~19.5 кГц.
// (12 бит не влезает: 10кГц x 4096 = 41 МГц > 40 МГц — ledcAttach падает!)
constexpr uint32_t PWM_FREQ = 10000;
constexpr int      PWM_BITS = 11;
constexpr uint32_t PWM_MAX  = (1u << PWM_BITS) - 1;   // 2047

// ================= Кинематика =================
// Измерено: 1500 тиков = 152 мм (повторяемо)
constexpr float MM_PER_TICK_DEFAULT = 152.0f / 1500.0f;   // ~0.1013
constexpr float TICKS_PER_MM_DEFAULT = 1500.0f / 152.0f;  // ~9.87

constexpr float MANUAL_SPEED_MM_S = 8.0f;    // ручной режим (удержание)
constexpr float PRESET_SPEED_MM_S = 15.0f;   // поездка к пресету
constexpr float CAL_SPEED_MM_S    = 12.0f;   // калибровка: достаточно быстро, чтобы на упоре
                                             // регулятор выжал ~65% ШИМ и ток пробил порог
constexpr float ACCEL_MM_S2       = 60.0f;   // ускорение — главное для «ничего не падает»
constexpr float REVERSE_MM        = 15.0f;   // отъезд после срабатывания «зажали»
constexpr float DEAD_TIME_MS      = 200.0f;  // пауза при смене направления

// ================= Ток =================
constexpr float ADC_FS_V    = 3.1f;                       // полная шкала ADC (11 дБ)
constexpr float RAW_TO_AMPS = ADC_FS_V / 4095.0f * 8.5f;  // *kILIS(8500)/1000 Ом
constexpr float OC_THRESH_A = 4.5f;                       // порог перегрузки
constexpr int   OC_RAW      = (int)(OC_THRESH_A / RAW_TO_AMPS); // ~700
constexpr uint32_t OC_HOLD_MS     = 250;    // держать ток выше порога, чтобы не ловить пики
constexpr int   END_MARGIN_TICKS  = 200;    // ~20 мм от упора = «у края»
constexpr uint32_t STALL_DETECT_MS  = 500;    // мотор включён, позиция стоит столько = упор/застревание
constexpr uint32_t CAL_TIMEOUT_MS = 120000; // калибровка: не нашли упор за это время
constexpr uint32_t CAL_PAUSE_MS   = 10000;  // пауза на нижнем упоре: успеть замерить высоту линейкой

// Ограничение ШИМ: БП 5 А не тянет стопорный ток (14.7 А на 100% ШИМ) и
// уходит в защиту. Калибровка едет на 30% (стопор ~4.4 А < 5 А).
constexpr float CAL_DUTY_CAP  = 0.45f;
constexpr float NORM_DUTY_CAP = 0.90f;
constexpr int   INSTANT_JAM_RAW = 390;      // ~2.5 А: мотор «давит», но не едет с места

// ================= Регулятор (крутить здесь) =================
constexpr float FF_START = 0.08f;    // мёртвая зона (трение червяка)
constexpr float FF_K     = 0.0026f;  // заполнение на (тик/с) сверх мёртвой зоны
constexpr float KP_V     = 0.0008f;  // P по скорости
constexpr float KI_V     = 0.05f;    // I по скорости (1/с)
constexpr float I_MAX    = 0.25f;    // анти-накопление интеграла (потолок добавки ШИМ)
constexpr float V_FILT_A = 0.35f;    // EMA скорости (10 мс)
constexpr float CUR_FILT_A = 0.3f;   // EMA тока (10 мс)
