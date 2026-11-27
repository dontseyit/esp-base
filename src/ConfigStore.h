// Thin, mutex protected wrapper over Preferences (NVS).
// EspBase uses namespace "espbase" (keys in Keys.h); projects create their own
// instance with a different namespace and get the same API.
#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "Keys.h"

namespace espbase {

class ConfigStore {
 public:
  static constexpr size_t kMaxKeyLen = Keys::kMaxKeyLen;

  ConfigStore();
  ~ConfigStore();

  bool begin(const char* ns = Keys::Namespace, bool readOnly = false);
  void end();
  const char* ns() const { return _ns; }

  String getString(const char* key, const String& def = String());
  bool setString(const char* key, const String& value);
  int32_t getInt(const char* key, int32_t def = 0);
  bool setInt(const char* key, int32_t value);
  uint8_t getUChar(const char* key, uint8_t def = 0);
  bool setUChar(const char* key, uint8_t value);

  bool has(const char* key);
  bool remove(const char* key);
  // Wipes every key in this namespace (factory reset).
  bool erase();
  size_t freeEntries();

 private:
  bool lock();
  void unlock();
  static bool validKey(const char* key);
  // Run fn under the lock when the key is valid and the namespace open.
  // readKey returns def unless the key exists.
  template <typename T, typename Fn>
  T readKey(const char* key, T def, Fn fn);
  template <typename Fn>
  bool withKey(const char* key, Fn fn);

  Preferences _prefs;
  SemaphoreHandle_t _mutex = nullptr;
  char _ns[kMaxKeyLen + 1] = {0};
  bool _open = false;
};

}  // namespace espbase
