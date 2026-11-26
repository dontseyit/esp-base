// Thin, mutex protected wrapper over Preferences (NVS).
// EspBase uses namespace "espbase"; projects create their own instance with a
// different namespace and get the same API.
#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace EspBaseKeys {
constexpr const char* Namespace = "espbase";
constexpr const char* StaSsid = "sta_ssid";  // slot 1 (primary network)
constexpr const char* StaPass = "sta_pass";
constexpr const char* Hostname = "hostname";
constexpr const char* ApPass = "ap_pass";
constexpr const char* WebPass = "web_pass";           // console password, applied after reboot
constexpr const char* LogLevelKey = "log_lvl";
constexpr const char* WifiModeKey = "wifi_mode";      // sta | ap | apsta
constexpr const char* StaTimeout = "sta_timeout";     // ms per connect attempt
constexpr const char* ReconnTimeout = "reconn_timeout";  // ms of retries after link loss before AP fallback, 0 = AP immediately
constexpr const char* ApRetry = "ap_retry";           // ms between retry rounds in AP mode, 0 = never
constexpr const char* BootApp = "boot_app";           // root app id restored by AppManager::start()
// Stored networks live in slots 1..kMaxStaSlots. Slot 1 uses sta_ssid/sta_pass,
// slots 2.. use sta<n>_ssid/sta<n>_pass (e.g. sta2_ssid). Tried in slot order.
constexpr uint8_t kMaxStaSlots = 5;
const char* slotSsidKey(uint8_t slot, char* buf, size_t len);
const char* slotPassKey(uint8_t slot, char* buf, size_t len);
// Recognises slot keys; sets slot (1..kMaxStaSlots) and whether it is the password key.
bool isSlotKey(const char* key, uint8_t& slot, bool& isPass);

// The library's keys, one entry each in Defs[]. "config list|get|set" go by
// it, and a project's own setting may not take one of these keys.
enum class Type : uint8_t {
  Text,
  Secret,  // config shows it masked
  Ms,      // int32_t milliseconds
  Level,   // a LogLevel, stored as a uint8_t
};
// The module whose applySetting() checks, stores and applies "config set".
enum class Owner : uint8_t { Base, Wifi, Apps };
struct Def {
  const char* key;
  Type type;
  Owner owner;
  bool atReboot = false;  // a new value applies after a reboot
};
// In "config list" order; the last entry's key is nullptr.
extern const Def Defs[];
// The key's entry, nullptr when the key is not one of the library's.
const Def* find(const char* key);

// The rule for every *_pass key: empty, or 8..63 characters (WPA2).
bool validPassword(const String& value);
}  // namespace EspBaseKeys

class ConfigStore {
 public:
  static constexpr size_t kMaxKeyLen = 15;  // NVS limit for keys and namespaces

  ConfigStore();
  ~ConfigStore();

  bool begin(const char* ns = EspBaseKeys::Namespace, bool readOnly = false);
  void end();
  bool isOpen() const { return _open; }
  const char* ns() const { return _ns; }

  String getString(const char* key, const String& def = String());
  bool setString(const char* key, const String& value);
  int32_t getInt(const char* key, int32_t def = 0);
  bool setInt(const char* key, int32_t value);
  uint8_t getUChar(const char* key, uint8_t def = 0);
  bool setUChar(const char* key, uint8_t value);
  bool getBool(const char* key, bool def = false);
  bool setBool(const char* key, bool value);

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
