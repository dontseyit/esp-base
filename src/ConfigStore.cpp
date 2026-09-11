#include "ConfigStore.h"

#include <string.h>

#include "Log.h"

namespace EspBaseKeys {
namespace {
// Slot 1 is "sta_<field>", slots 2.. are "sta<n>_<field>".
const char* slotKey(uint8_t slot, const char* field, char* buf, size_t len) {
  if (slot <= 1) {
    snprintf(buf, len, "sta_%s", field);
  } else {
    snprintf(buf, len, "sta%u_%s", slot, field);
  }
  return buf;
}
}  // namespace

const char* const All[] = {StaSsid,   StaPass,   "sta2_ssid", "sta2_pass", "sta3_ssid",  "sta3_pass",   "sta4_ssid", "sta4_pass", "sta5_ssid",
                           "sta5_pass", Hostname, ApPass,      WebPass,     LogLevelKey, WifiModeKey, StaTimeout, ReconnTimeout, ApRetry,   BootApp, nullptr};

bool isSecret(const char* key) {
  if (key == nullptr) {
    return false;
  }
  const size_t n = strlen(key);
  return n >= 5 && strcmp(key + n - 5, "_pass") == 0;
}

const char* slotSsidKey(uint8_t slot, char* buf, size_t len) {
  return slotKey(slot, "ssid", buf, len);
}

const char* slotPassKey(uint8_t slot, char* buf, size_t len) {
  return slotKey(slot, "pass", buf, len);
}

bool isSlotKey(const char* key, uint8_t& slot, bool& isPass) {
  if (key == nullptr || strncmp(key, "sta", 3) != 0) {
    return false;
  }
  const char* p = key + 3;
  uint8_t s = 1;
  if (isdigit(static_cast<unsigned char>(*p))) {
    s = static_cast<uint8_t>(*p - '0');
    p++;
    if (s < 2 || s > kMaxStaSlots) {
      return false;
    }
  }
  if (strcmp(p, "_ssid") == 0) {
    isPass = false;
  } else if (strcmp(p, "_pass") == 0) {
    isPass = true;
  } else {
    return false;
  }
  slot = s;
  return true;
}
}  // namespace EspBaseKeys

ConfigStore::ConfigStore() : _mutex(xSemaphoreCreateMutex()) {}

ConfigStore::~ConfigStore() {
  end();
  if (_mutex != nullptr) {
    vSemaphoreDelete(_mutex);
  }
}

bool ConfigStore::lock() {
  return _mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE;
}

void ConfigStore::unlock() {
  xSemaphoreGive(_mutex);
}

bool ConfigStore::validKey(const char* key) {
  if (key == nullptr || *key == '\0' || strlen(key) > kMaxKeyLen) {
    LOG_W("config: invalid key '%s'", key ? key : "(null)");
    return false;
  }
  return true;
}

bool ConfigStore::begin(const char* ns, bool readOnly) {
  if (!validKey(ns)) {
    return false;
  }
  if (!lock()) {
    return false;
  }
  if (_open) {
    _prefs.end();
    _open = false;
  }
  strncpy(_ns, ns, kMaxKeyLen);
  _ns[kMaxKeyLen] = '\0';
  _open = _prefs.begin(_ns, readOnly);
  if (!_open) {
    LOG_E("config: cannot open namespace '%s'", _ns);
  }
  unlock();
  return _open;
}

void ConfigStore::end() {
  if (lock()) {
    if (_open) {
      _prefs.end();
      _open = false;
    }
    unlock();
  }
}

template <typename T, typename Fn>
T ConfigStore::readKey(const char* key, T def, Fn fn) {
  T v = def;
  if (validKey(key) && lock()) {
    if (_open && _prefs.isKey(key)) {
      v = fn();
    }
    unlock();
  }
  return v;
}

template <typename Fn>
bool ConfigStore::withKey(const char* key, Fn fn) {
  bool ok = false;
  if (validKey(key) && lock()) {
    ok = _open && fn();
    unlock();
  }
  return ok;
}

String ConfigStore::getString(const char* key, const String& def) {
  return readKey(key, def, [&]() { return _prefs.getString(key, def); });
}

bool ConfigStore::setString(const char* key, const String& value) {
  return withKey(key, [&]() { return _prefs.putString(key, value) == value.length(); });
}

int32_t ConfigStore::getInt(const char* key, int32_t def) {
  return readKey(key, def, [&]() { return _prefs.getInt(key, def); });
}

bool ConfigStore::setInt(const char* key, int32_t value) {
  return withKey(key, [&]() { return _prefs.putInt(key, value) == sizeof(value); });
}

uint8_t ConfigStore::getUChar(const char* key, uint8_t def) {
  return readKey(key, def, [&]() { return _prefs.getUChar(key, def); });
}

bool ConfigStore::setUChar(const char* key, uint8_t value) {
  return withKey(key, [&]() { return _prefs.putUChar(key, value) == sizeof(value); });
}

bool ConfigStore::getBool(const char* key, bool def) {
  return readKey(key, def, [&]() { return _prefs.getBool(key, def); });
}

bool ConfigStore::setBool(const char* key, bool value) {
  return withKey(key, [&]() { return _prefs.putBool(key, value) == sizeof(uint8_t); });
}

bool ConfigStore::has(const char* key) {
  return withKey(key, [&]() { return _prefs.isKey(key); });
}

bool ConfigStore::remove(const char* key) {
  return withKey(key, [&]() { return !_prefs.isKey(key) || _prefs.remove(key); });
}

bool ConfigStore::erase() {
  bool ok = false;
  if (lock()) {
    ok = _open && _prefs.clear();
    unlock();
  }
  return ok;
}

size_t ConfigStore::freeEntries() {
  size_t n = 0;
  if (lock()) {
    n = _open ? _prefs.freeEntries() : 0;
    unlock();
  }
  return n;
}
