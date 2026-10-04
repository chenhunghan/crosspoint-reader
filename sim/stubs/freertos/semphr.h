#pragma once
#include "FreeRTOS.h"
#include "queue.h"

inline SemaphoreHandle_t xSemaphoreCreateMutex() {
  auto* s = new SimSemaphore();
  s->count = 1;
  s->isMutex = true;
  return s;
}
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return xSemaphoreCreateMutex(); }
inline SemaphoreHandle_t xSemaphoreCreateBinary() { return new SimSemaphore(); }
inline SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t max, UBaseType_t initial) {
  auto* s = new SimSemaphore();
  s->max = static_cast<int>(max);
  s->count = static_cast<int>(initial);
  return s;
}
inline void vSemaphoreDelete(SemaphoreHandle_t s) { delete s; }
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t) {
  if (s && !s->isMutex && s->count > 0) --s->count;
  return pdTRUE;
}
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t s) {
  if (s && !s->isMutex && s->count < s->max) ++s->count;
  return pdTRUE;
}
inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t s, TickType_t t) { return xSemaphoreTake(s, t); }
inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t s) { return xSemaphoreGive(s); }
inline BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t s, BaseType_t*) { return xSemaphoreGive(s); }
inline TaskHandle_t xSemaphoreGetMutexHolder(SemaphoreHandle_t) { return nullptr; }
