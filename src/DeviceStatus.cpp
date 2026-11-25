#include "DeviceStatus.h"

#include <string.h>

#include "Log.h"

void DeviceStatus::begin() {
  if (_mutex == nullptr) {
    _mutex = xSemaphoreCreateMutex();
  }
}

bool DeviceStatus::lock() {
  if (_mutex == nullptr) {
    LOG_E("status: used before EspBase::begin()");
    return false;
  }
  return xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE;
}

bool DeviceStatus::add(const char* key, const char* label) {
  if (key == nullptr || *key == '\0' || label == nullptr) {
    LOG_E("status: a value needs a key and a label");
    return false;
  }
  if (!lock()) {
    return false;
  }
  const char* why = _values.size() >= kMaxValues ? "too many values" : nullptr;
  for (const Value& v : _values) {
    if (strcmp(v.key, key) == 0) {
      why = "the key is already defined";
    }
  }
  if (why == nullptr) {
    _values.push_back({key, *label != '\0' ? label : key, String()});
  }
  unlock();
  if (why != nullptr) {
    LOG_E("status: \"%s\" refused: %s", key, why);
  }
  return why == nullptr;
}

bool DeviceStatus::addButton(const char* label, const char* command) {
  if (label == nullptr || *label == '\0' || command == nullptr || *command == '\0') {
    LOG_E("status: a button needs a label and a command");
    return false;
  }
  if (!lock()) {
    return false;
  }
  const bool ok = _buttons.size() < kMaxButtons;
  if (ok) {
    _buttons.push_back({label, command});
  }
  unlock();
  if (!ok) {
    LOG_E("status: button \"%s\" refused: too many buttons", label);
  }
  return ok;
}

bool DeviceStatus::set(const char* key, const String& value) {
  if (key == nullptr || !lock()) {
    return false;
  }
  bool found = false;
  for (Value& v : _values) {
    if (strcmp(v.key, key) == 0) {
      size_t n = value.length();
      if (n > kMaxValueLen) {
        n = kMaxValueLen;
        while (n > 0 && (static_cast<uint8_t>(value[n]) & 0xC0) == 0x80) {
          --n;  // never cut a UTF-8 sequence in half
        }
      }
      v.value = n == value.length() ? value : value.substring(0, n);
      found = true;
      break;
    }
  }
  unlock();
  return found;
}

bool DeviceStatus::empty() {
  if (_mutex == nullptr || !lock()) {
    return true;
  }
  const bool none = _values.empty() && _buttons.empty();
  unlock();
  return none;
}

void DeviceStatus::writeJson(JsonObject obj) {
  JsonArray values = obj["values"].to<JsonArray>();
  JsonArray buttons = obj["buttons"].to<JsonArray>();
  if (_mutex == nullptr || !lock()) {
    return;
  }
  for (const Value& v : _values) {
    JsonObject e = values.add<JsonObject>();
    e["key"] = v.key;
    e["label"] = v.label;
    e["value"] = v.value;
  }
  for (const Button& b : _buttons) {
    JsonObject e = buttons.add<JsonObject>();
    e["label"] = b.label;
    e["cmd"] = b.command;
  }
  unlock();
}
