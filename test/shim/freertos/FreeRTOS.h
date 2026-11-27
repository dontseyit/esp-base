// FreeRTOS stand-ins for single threaded host tests: every lock succeeds at once.
#pragma once

#include <cstdint>

typedef void* SemaphoreHandle_t;
typedef void* QueueHandle_t;
typedef int BaseType_t;
typedef uint32_t TickType_t;

#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(ms) (ms)

inline SemaphoreHandle_t xSemaphoreCreateMutex() {
  static int token;
  return &token;
}
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) {
  return pdTRUE;
}
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t) {
  return pdTRUE;
}
inline void vSemaphoreDelete(SemaphoreHandle_t) {}
inline bool xPortInIsrContext() {
  return false;
}
