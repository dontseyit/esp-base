// Host stand-ins for the FreeRTOS calls esp-base makes. One task only, so a
// mutex always succeeds and a queue is a deque.
#pragma once

#include <stdint.h>
#include <string.h>

#include <deque>
#include <vector>

using BaseType_t = int;
using TickType_t = uint32_t;
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(ms) (ms)

struct FakeQueue {
  size_t itemSize;
  size_t capacity;
  std::deque<std::vector<uint8_t>> items;
};
using QueueHandle_t = FakeQueue*;
using SemaphoreHandle_t = void*;

inline QueueHandle_t xQueueCreate(size_t length, size_t itemSize) { return new FakeQueue{itemSize, length, {}}; }
inline BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t) {
  if (q->items.size() >= q->capacity) {
    return pdFALSE;
  }
  const uint8_t* p = static_cast<const uint8_t*>(item);
  q->items.emplace_back(p, p + q->itemSize);
  return pdTRUE;
}
inline BaseType_t xQueueReceive(QueueHandle_t q, void* out, TickType_t) {
  if (q->items.empty()) {
    return pdFALSE;
  }
  memcpy(out, q->items.front().data(), q->itemSize);
  q->items.pop_front();
  return pdTRUE;
}
inline BaseType_t xQueueReset(QueueHandle_t q) {
  q->items.clear();
  return pdTRUE;
}

inline SemaphoreHandle_t xSemaphoreCreateMutex() {
  static int mutex;
  return &mutex;
}
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t) { return pdTRUE; }
inline void vSemaphoreDelete(SemaphoreHandle_t) {}
inline BaseType_t xPortInIsrContext() { return pdFALSE; }
