// ProjectSettings: a project's own settings, beside the library's. Reached
// through base.settings().
//
// The project names them once, in setup() after EspBase::begin():
//
//   static const SettingDef kMapKey = {.key = "map_key", .label = "Map key", .kind = SettingKind::Secret};
//   base.settings().add(kMapKey);
//   base.settings().onChange([](const char* key) { /* loop task */ });
//   String key = base.settings().get("map_key");
//
// The library stores the values through ConfigStore, shows them in a tab of
// the web page (GET/POST /api/settings) and in the "settings" and "config"
// commands. A Secret is write-only everywhere: only whether it is set is told.
#pragma once

#include <Arduino.h>

#include <functional>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "ConfigStore.h"
#include "SettingsCheck.h"

struct SettingDef {
  const char* key = "";      // ConfigStore key: 1..15 of a-z, 0-9, '_', and not one of the library's own
  const char* label = "";    // shown on the page
  SettingKind kind = SettingKind::Text;
  const char* def = "";      // the value while nothing is stored
  const char* choices = "";  // Choice only: "a,b,c"
  const char* hint = "";     // placeholder on the page
  uint16_t maxLen = 63;      // bytes, 1..255
  // Optional extra check. It must be pure: it runs on the loop task and on the
  // web server's task.
  bool (*validate)(const String& value, String& err) = nullptr;
};

class ProjectSettings {
 public:
  static constexpr size_t kMaxSettings = 24;
  static constexpr size_t kMaxQueued = 8;  // requests waiting for loop()

  void begin(ConfigStore& store);
  // Applies the changes handed over by queue(), then runs the change callbacks.
  void loop();

  // Loop task. The definition's strings must outlive this object (literals do).
  // false, with an error in the log, for a bad, duplicate or surplus definition.
  bool add(const SettingDef& def);

  size_t count() const { return _count; }
  const SettingDef* at(size_t i) const { return i < _count ? &_defs[i] : nullptr; }
  const SettingDef* find(const char* key) const;

  // The stored value, else the default; "" for an unknown key. Any task.
  String get(const char* key);
  bool isSet(const char* key);

  // Any task. err becomes "<key>: <reason>".
  bool check(const char* key, const String& value, String& err) const;
  // Loop task: check, store, run the callbacks. Storing the value that is
  // already stored changes nothing and runs no callback.
  bool set(const char* key, const String& value, String& err);
  // Loop task: back to the default.
  bool clear(const char* key);

  // Any task: hands checked changes over to loop(), all of them or none. A
  // true in isNull clears that key. false for an unknown key or a full queue.
  bool queue(const char* const* keys, const String* values, const bool* isNull, size_t n);

  // Loop task, after a value changed.
  void onChange(std::function<void(const char* key)> cb) { _onChange.push_back(std::move(cb)); }

 private:
  struct Change {
    const SettingDef* def;
    String value;
    bool clear;
  };

  void notify(const char* key);

  ConfigStore* _store = nullptr;
  SettingDef _defs[kMaxSettings];
  size_t _count = 0;
  SemaphoreHandle_t _mutex = nullptr;
  std::vector<std::vector<Change>> _queue;  // guarded by _mutex
  std::vector<std::function<void(const char*)>> _onChange;
};
