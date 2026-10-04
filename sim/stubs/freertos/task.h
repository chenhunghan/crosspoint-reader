#pragma once
#include "FreeRTOS.h"

typedef enum { eNoAction = 0, eSetBits, eIncrement, eSetValueWithOverwrite, eSetValueWithoutOverwrite } eNotifyAction;

void vTaskDelay(TickType_t ticks);
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return reinterpret_cast<TaskHandle_t>(1); }
// Tasks are never really spawned in the simulator.
inline BaseType_t xTaskCreate(TaskFunction_t, const char*, uint32_t, void*, UBaseType_t, TaskHandle_t* h) {
  if (h) *h = nullptr;
  return pdFAIL;
}
inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t f, const char* n, uint32_t s, void* p, UBaseType_t pr,
                                          TaskHandle_t* h, BaseType_t) {
  return xTaskCreate(f, n, s, p, pr, h);
}
inline void vTaskDelete(TaskHandle_t) {}
inline uint32_t ulTaskNotifyTake(BaseType_t, TickType_t) { return 0; }
inline BaseType_t xTaskNotify(TaskHandle_t, uint32_t, eNotifyAction) { return pdPASS; }
inline BaseType_t xTaskNotifyGive(TaskHandle_t) { return pdPASS; }
inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 4096; }
inline void vTaskSuspend(TaskHandle_t) {}
inline void vTaskResume(TaskHandle_t) {}
