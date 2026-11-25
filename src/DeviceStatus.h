// DeviceStatus: the project's own values and buttons, shown in the page's
// Device tab (GET /api/status). Reached through base.status().
//
//   base.status().add("temp", "Temperature");             // setup(), after begin()
//   base.status().addButton("Calibrate", "calibrate");    // runs that console command
//   base.status().set("temp", String(t, 1) + " C");       // any task, when it changes
//
// The project pushes values and the web server reads the stored copies, so no
// project code runs on the web server's task.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class DeviceStatus {
 public:
  static constexpr size_t kMaxValues = 16;
  static constexpr size_t kMaxButtons = 8;
  static constexpr size_t kMaxValueLen = 127;  // bytes; a longer value is cut

  void begin();

  // After EspBase::begin(). The strings must outlive this object (literals do).
  // false, with an error in the log, for an empty or duplicate key, or past the limit.
  bool add(const char* key, const char* label);
  // A button that runs `command` as if it were typed in the console. Its output
  // shows under the buttons.
  bool addButton(const char* label, const char* command);
  // Any task, not an ISR. false for a key add() never named.
  bool set(const char* key, const String& value);

  bool empty();
  // {"values":[{"key","label","value"}],"buttons":[{"label","cmd"}]}
  void writeJson(JsonObject obj);

 private:
  struct Value {
    const char* key;
    const char* label;
    String value;
  };
  struct Button {
    const char* label;
    const char* command;
  };

  bool lock();
  void unlock() { xSemaphoreGive(_mutex); }

  SemaphoreHandle_t _mutex = nullptr;
  std::vector<Value> _values;  // guarded by _mutex
  std::vector<Button> _buttons;
};
