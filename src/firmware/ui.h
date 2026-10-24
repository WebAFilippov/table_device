#pragma once
#include "motor.h"

void uiBegin();                          // инициализация дисплея + LVGL + экраны
void uiTask();                           // вызывать из loop() (lv_timer_handler + обновление)
void uiNotice(const char* msg, uint32_t ms);  // временное сообщение в статусной строке
void uiEvent(MotorEvent e);              // реакция на события мотора
