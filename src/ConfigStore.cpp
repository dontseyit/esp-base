#include "ConfigStore.h"

#include <string.h>

#include "Log.h"

namespace espbase {

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

}  // namespace espbase
