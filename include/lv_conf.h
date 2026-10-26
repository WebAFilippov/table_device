/**
 * LVGL 8.3 — конфигурация для стола (ESP32-S3 N16R8, PSRAM, TFT_eSPI)
 * Собирается с флагом -DLV_CONF_INCLUDE_SIMPLE
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0   // если красный/синий поменялись местами — поставь 1

// Куча LVGL — во ВНУТРЕННЕЙ RAM (статический массив 64 КБ).
// ВАЖНО: в этой конфигурации ядра (PSRAM без SPIRAM_USE_MALLOC) указатели
// из PSRAM нельзя освобождать: и free(), и heap_caps_free() падают с assert
// "outside heap areas". А LVGL постоянно выделяет/освобождает объекты и
// тексты меток — поэтому его куча живёт во внутренней памяти.
// В PSRAM только буферы кадра: выделяются один раз и не освобождаются.
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (64U * 1024U)

// Тик — из millis(), без отдельного таймера
#define LV_TICK_CUSTOM 1
#if LV_TICK_CUSTOM
  #define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
  #define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#endif

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 0
#define LV_USE_ASSERT_MALLOC 0
#define LV_USE_PERF_MONITOR 0

// Шрифты, которые использует интерфейс
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1

// Для ввода пароля Wi-Fi
#define LV_USE_TEXTAREA 1
#define LV_USE_KEYBOARD 1

#define LV_USE_THEME_DEFAULT 1

#define LV_DPI_DEF 130
#define LV_DISP_DEF_REFR_PERIOD 30
#define LV_INDEV_DEF_READ_PERIOD 50   // тач 20 раз/с: меньше шансов словить глюк I2C при езде

#endif
