#include "Settings.h"

#include <string.h>

#include "Log.h"

namespace {
bool libraryKey(const char* key) {
  for (const char* const* k = EspBaseKeys::All; *k != nullptr; ++k) {
    if (strcmp(key, *k) == 0) {
      return true;
    }
  }
  return false;
}
}  // namespace

void Settings::begin(ConfigStore& store) {
  _store = &store;
  if (_mutex == nullptr) {
    _mutex = xSemaphoreCreateMutex();
  }
}

bool Settings::add(const SettingDef& def) {
  const char* why = nullptr;
  if (def.label == nullptr || def.def == nullptr || def.choices == nullptr || def.hint == nullptr) {
    why = "label, def, choices and hint must not be null";
  } else if (!SettingsCheck::validKey(def.key)) {
    why = "the key must be 1..15 of a-z, 0-9 and '_'";
  } else if (libraryKey(def.key)) {
    why = "the key is one of the library's own";
  } else if (find(def.key) != nullptr) {
    why = "the key is already defined";
  } else if (_count >= kMaxSettings) {
    why = "too many settings";
  } else if (def.maxLen < 1 || def.maxLen > 255) {
    why = "maxLen must be 1..255";
  } else if (def.kind == SettingKind::Choice && (def.choices == nullptr || *def.choices == '\0')) {
    why = "a Choice needs choices";
  } else if (def.kind == SettingKind::Choice || *def.def != '\0') {
    why = SettingsCheck::valueError(def.kind, def.choices, def.maxLen, def.def);  // an empty default is fine, but for a Choice
  }
  if (why != nullptr) {
    LOG_E("settings: \"%s\" refused: %s", def.key != nullptr ? def.key : "", why);
    return false;
  }
  _defs[_count] = def;
  ++_count;  // after the copy: readers on other tasks never see half a definition
  return true;
}

const SettingDef* Settings::find(const char* key) const {
  if (key == nullptr) {
    return nullptr;
  }
  for (size_t i = 0; i < _count; ++i) {
    if (strcmp(_defs[i].key, key) == 0) {
      return &_defs[i];
    }
  }
  return nullptr;
}

String Settings::get(const char* key) {
  const SettingDef* def = find(key);
  if (def == nullptr || _store == nullptr) {
    return String();
  }
  return _store->getString(def->key, def->def);
}

bool Settings::isSet(const char* key) {
  const SettingDef* def = find(key);
  return def != nullptr && _store != nullptr && _store->has(def->key);
}

bool Settings::check(const char* key, const String& value, String& err) const {
  const SettingDef* def = find(key);
  if (def == nullptr) {
    err = String(key != nullptr ? key : "") + ": unknown setting";
    return false;
  }
  const char* why = SettingsCheck::valueError(def->kind, def->choices, def->maxLen, value.c_str());
  if (why != nullptr) {
    err = String(def->key) + ": " + why;
    return false;
  }
  String detail;
  if (def->validate != nullptr && !def->validate(value, detail)) {
    err = String(def->key) + ": " + (detail.isEmpty() ? String("not valid") : detail);
    return false;
  }
  return true;
}

bool Settings::set(const char* key, const String& value, String& err) {
  if (!check(key, value, err)) {
    return false;
  }
  if (_store == nullptr) {
    err = String(key) + ": not started";
    return false;
  }
  if (_store->has(key) && _store->getString(key) == value) {
    return true;  // nothing changed
  }
  if (!_store->setString(key, value)) {
    err = String(key) + ": write failed";
    return false;
  }
  notify(key);
  return true;
}

bool Settings::clear(const char* key) {
  const SettingDef* def = find(key);
  if (def == nullptr || _store == nullptr) {
    return false;
  }
  if (_store->has(def->key)) {
    _store->remove(def->key);
    notify(def->key);
  }
  return true;
}

bool Settings::queue(const char* const* keys, const String* values, const bool* isNull, size_t n) {
  std::vector<Change> request;
  request.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    const SettingDef* def = find(keys[i]);
    if (def == nullptr) {
      return false;
    }
    request.push_back(Change{def, isNull[i] ? String() : values[i], isNull[i]});
  }
  if (_mutex == nullptr || xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    return false;
  }
  const bool room = _queue.size() < kMaxQueued;
  if (room) {
    _queue.push_back(std::move(request));
  }
  xSemaphoreGive(_mutex);
  return room;
}

void Settings::loop() {
  if (_mutex == nullptr || xSemaphoreTake(_mutex, 0) != pdTRUE) {
    return;  // a request is being handed over right now: next pass
  }
  std::vector<std::vector<Change>> pending;
  pending.swap(_queue);
  xSemaphoreGive(_mutex);

  for (const std::vector<Change>& request : pending) {
    for (const Change& c : request) {
      String err;
      if (c.clear) {
        clear(c.def->key);
      } else if (!set(c.def->key, c.value, err)) {
        LOG_W("settings: %s", err.c_str());  // it passed check() when it was queued
      }
    }
  }
}

void Settings::notify(const char* key) {
  for (const auto& cb : _onChange) {
    cb(key);
  }
}
