// Lifecycle, scheduling and navigation for the apps in AppRegistry. Runs from
// EspBase::loop(); see App.h for the app side of the contract.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include <functional>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "App.h"

class EspBase;

class AppManager {
 public:
  using PresentFn = std::function<void(Frame&)>;
  static constexpr size_t kMaxDepth = 8;

  // Called by EspBase::begin(): registers the "app" console command and /api/apps.
  void attach(EspBase& base);

  // Sorts the registry, calls App::begin() on every app, opens the hidden
  // services and the root app: the persisted "boot_app" if it exists, else
  // homeId, else the first visible app. frame/present may be null on headless
  // devices: apps still tick, draw() is never called. Returns false when no
  // visible app could be opened.
  bool start(Frame* frame = nullptr, PresentFn present = nullptr, const char* homeId = nullptr);
  bool started() const { return _started; }
  void loop();

  // Navigation. Requests are validated now and applied at the end of the
  // current loop iteration, one per iteration. Callable from any task.
  bool push(const char* id, const String& args = String());
  bool pop(int result = 0);                 // the root app cannot be popped
  bool replace(const char* id, const String& args = String());  // swaps the foreground app; may replace the root
  bool popToRoot();

  App* foreground() const { return _depth > 0 ? _stack[_depth - 1] : nullptr; }
  App* root() const { return _depth > 0 ? _stack[0] : nullptr; }
  size_t depth() const { return _depth; }
  App* stackAt(size_t i) const { return i < _depth ? _stack[i] : nullptr; }
  bool onStack(const App& app) const;

  // Input from the project (buttons, encoder, touch). Queued and delivered to
  // the foreground app from loop(); an unconsumed Back press pops.
  bool input(const InputEvent& ev);
  bool inputFromISR(const InputEvent& ev);
  bool key(uint8_t code, int32_t value = 0);  // convenience: a Press event

  void setIdleTimeout(uint32_t ms) { _idleTimeoutMs = ms; }  // pop to root after this long without input; 0 = off
  void setTickBudget(uint32_t us) { _tickBudgetUs = us; }    // slower ticks count as overruns and are logged
  uint32_t loopsPerSecond() const { return _loopHz; }
  Frame* frame() const { return _frame; }

  void printList(Print& out);
  void printStack(Print& out);
  void printStats(Print& out);
  void writeJson(JsonObject obj);

 private:
  enum Kind : uint8_t { None = 0, Push, Pop, Replace, PopToRoot };
  struct Transition {
    Kind kind = None;
    App* app = nullptr;
    String args;
    int result = 0;
  };

  App* chooseRoot(const char* homeId) const;
  App* resolve(const char* id, const char* what) const;
  bool queue(Transition&& t, const char* what);
  void apply(Transition& t);
  void openOnStack(App& app, const String& args);
  void closeTop();
  void handleInput(const InputEvent& ev);
  void runTicks(uint32_t now);
  void runDraw(uint32_t now);
  static const char* stateOf(const App& app, const App* fg);
  static bool timeReached(uint32_t now, uint32_t at) { return static_cast<int32_t>(now - at) >= 0; }

  EspBase* _base = nullptr;
  Frame* _frame = nullptr;
  PresentFn _present;
  bool _started = false;
  App* _stack[kMaxDepth] = {};
  size_t _depth = 0;
  Transition _pending;
  bool _hasPending = false;
  SemaphoreHandle_t _mutex = nullptr;
  QueueHandle_t _inputs = nullptr;
  uint32_t _idleTimeoutMs = 0;
  uint32_t _lastInputAt = 0;
  uint32_t _tickBudgetUs = 20000;
  uint32_t _loopCount = 0;
  uint32_t _loopHz = 0;
  uint32_t _loopWindowStart = 0;
  uint32_t _presentLastUs = 0;
  uint32_t _presentMaxUs = 0;
};
