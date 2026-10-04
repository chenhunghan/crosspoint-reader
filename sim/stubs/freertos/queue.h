#pragma once
#include "FreeRTOS.h"
// Queues are not used by the simulated screens; these never deliver.
inline QueueHandle_t xQueueCreate(UBaseType_t, UBaseType_t) { return new SimSemaphore(); }
inline void vQueueDelete(QueueHandle_t q) { delete q; }
inline BaseType_t xQueueSend(QueueHandle_t, const void*, TickType_t) { return pdTRUE; }
inline BaseType_t xQueueSendToBack(QueueHandle_t, const void*, TickType_t) { return pdTRUE; }
inline BaseType_t xQueueSendFromISR(QueueHandle_t, const void*, BaseType_t*) { return pdTRUE; }
inline BaseType_t xQueueOverwrite(QueueHandle_t, const void*) { return pdTRUE; }
inline BaseType_t xQueueReceive(QueueHandle_t, void*, TickType_t) { return pdFALSE; }
inline BaseType_t xQueuePeek(QueueHandle_t, void*, TickType_t) { return pdTRUE; }
inline UBaseType_t uxQueueMessagesWaiting(QueueHandle_t) { return 0; }
inline BaseType_t xQueueReset(QueueHandle_t) { return pdTRUE; }
