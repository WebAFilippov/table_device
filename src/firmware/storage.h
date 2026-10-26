#pragma once
#include <Arduino.h>

// Потокобезопасный доступ к NVS (namespace "table").
// Preferences НЕ защищён от одновременного доступа из двух задач —
// моторная задача и UI обращаются к NVS параллельно, поэтому все операции
// идут через один мьютекс.

void storageInit();

float   stGetF(const char* key, float def);
void    stPutF(const char* key, float v);
int64_t stGetI64(const char* key, int64_t def);
void    stPutI64(const char* key, int64_t v);
bool    stGetB(const char* key, bool def);
void    stPutB(const char* key, bool v);
String  stGetS(const char* key, const char* def);
void    stPutS(const char* key, const String &v);
void    stClear();
