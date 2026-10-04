#pragma once
// CrossPoint simulator: FreeRTOS surface. The simulator is single-threaded
// (the render "task" runs synchronously from the main loop), so mutexes and
// semaphores are bookkeeping-only and always succeed.

#include <cstddef>
#include <cstdint>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef void* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);

struct SimSemaphore {
  int count = 0;
  int max = 1;
  bool isMutex = false;
};
typedef SimSemaphore* SemaphoreHandle_t;
typedef SimSemaphore* QueueHandle_t;

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define errQUEUE_FULL 0
#define portMAX_DELAY 0xffffffffUL
#define portTICK_PERIOD_MS 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define configTICK_RATE_HZ 1000
#define tskIDLE_PRIORITY 0
#define tskNO_AFFINITY 0x7FFFFFFF

typedef struct {
  int unused;
} portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {0}
#define taskENTER_CRITICAL(m) ((void)(m))
#define taskEXIT_CRITICAL(m) ((void)(m))
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
#define taskYIELD() ((void)0)
#define portYIELD_FROM_ISR() ((void)0)

TickType_t xTaskGetTickCount();
