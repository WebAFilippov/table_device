/**
 * LVGL 8.3 — конфигурация для стола (ESP32-S3 N16R8, PSRAM, TFT_eSPI)
 * Собирается с флагом -DLV_CONF_INCLUDE_SIMPLE
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0   // если красный/синий поменялись местами — поставь 1

// Вся память LVGL — из PSRAM (8 МБ, хватит на всё)
#define LV_MEM_CUSTOM 1
#if LV_MEM_CUSTOM
  #define LV_MEM_CUSTOM_INCLUDE <esp32-hal-psram.h>
  #include <esp_heap_caps.h>   // heap_caps_free / MALLOC_CAP_SPIRAM для макроса ниже
  #define LV_MEM_CUSTOM_ALLOC   ps_malloc
  // ВАЖНО: free() не освобождает указатели из PSRAM (heap не default-capable)
  // → heap_caps_free assert. heap_caps_free сам находит кучу по указателю.
  #define LV_MEM_CUSTOM_FREE(p) heap_caps_free((p))
  #define LV_MEM_CUSTOM_REALLOC ps_realloc
#endif

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

#define LV_USE_THEME_DEFAULT 1

#define LV_MEM_SIZE (64U * 1024U)   // не используется при LV_MEM_CUSTOM
#define LV_DPI_DEF 130
#define LV_DISP_DEF_REFR_PERIOD 30
#define LV_INDEV_DEF_READ_PERIOD 30

#endif
