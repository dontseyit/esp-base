#include "ProjectSettings.h"

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

const char* errorOf(const SettingDef& d, const char* value) {
  return SettingsCheck::valueError(d.kind, d.choices, d.maxLen, value, d.min, d.max, d.decimals);
}
}  // namespace

void ProjectSettings::begin(ConfigStore& store) {
  _store = &store;
  if (_mutex == nullptr) {
    _mutex = xSemaphoreCreateMutex();
  }
}

bool ProjectSettings::add(const SettingDef& def) {
  SettingDef d = def;
  if (d.kind == SettingKind::Bool && d.def != nullptr && *d.def == '\0') {
    d.def = "false";
  }
  const char* why = nullptr;
  const char* badDefault = nullptr;
  if (d.label == nullptr || d.def == nullptr || d.choices == nullptr || d.hint == nullptr) {
    why = "label, def, choices and hint must not be null";
  } else if (!SettingsCheck::validKey(d.key)) {
    why = "the key must be 1..15 of a-z, 0-9 and '_'";
  } else if (libraryKey(d.key)) {
    why = "the key is one of the library's own";
  } else if (find(d.key) != nullptr) {
    why = "the key is already defined";
  } else if (_count >= kMaxSettings) {
    why = "too many settings";
  } else if (d.maxLen < 1 || d.maxLen > 255) {
    why = "maxLen must be 1..255";
  } else if (d.kind == SettingKind::Choice && *d.choices == '\0') {
    why = "a Choice needs choices";
  } else if (d.kind == SettingKind::Number && !(d.min <= d.max)) {
    why = "a Number needs min <= max";
  } else if (d.kind == SettingKind::Number && d.decimals > 6) {
    why = "decimals must be 0..6";
  } else if ((d.kind != SettingKind::Text && d.kind != SettingKind::Secret) || *d.def != '\0') {
    badDefault = errorOf(d, d.def);  // only Text and Secret may have an empty default
  }
  if (why != nullptr || badDefault != nullptr) {
    if (badDefault != nullptr) {
      LOG_E("settings: \"%s\" refused: the default \"%s\": %s", d.key, d.def, badDefault);
    } else {
      LOG_E("settings: \"%s\" refused: %s", d.key != nullptr ? d.key : "", why);
    }
    return false;
  }
  _defs[_count] = d;
  ++_count;  // atomic: published after the copy
  return true;
}

const SettingDef* ProjectSettings::find(const char* key) const {
  if (key == nullptr) {
    return nullptr;
  }
  const size_t n = _count;
  for (size_t i = 0; i < n; ++i) {
    if (strcmp(_defs[i].key, key) == 0) {
      return &_defs[i];
    }
  }
  return nullptr;
}

String ProjectSettings::get(const char* key) {
  const SettingDef* def = find(key);
  if (def == nullptr || _store == nullptr) {
    return String();
  }
  String value = _store->getString(def->key, def->def);
  if (errorOf(*def, value.c_str()) != nullptr) {
    return String(def->def);  // stored by a firmware whose definition allowed it
  }
  return value;
}

bool ProjectSettings::isSet(const char* key) {
  const SettingDef* def = find(key);
  return def != nullptr && _store != nullptr && _store->has(def->key);
}

bool ProjectSettings::check(const char* key, const String& value, String& err) const {
  const SettingDef* def = find(key);
  if (def == nullptr) {
    err = String(key != nullptr ? key : "") + ": unknown setting";
    return false;
  }
  const char* why = errorOf(*def, value.c_str());
  if (why != nullptr) {
    err = String(def->key) + ": " + why;
    if (def->kind == SettingKind::Number) {
      err += " (" + String(def->min, static_cast<unsigned int>(def->decimals)) + " to " + String(def->max, static_cast<unsigned int>(def->decimals)) + ")";
    }
    return false;
  }
  String detail;
  if (def->validate != nullptr && !def->validate(value, detail)) {
    err = String(def->key) + ": " + (detail.isEmpty() ? String("not valid") : detail);
    return false;
  }
  return true;
}

bool ProjectSettings::set(const char* key, const String& value, String& err) {
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

bool ProjectSettings::clear(const char* key) {
  const SettingDef* def = find(key);
  if (def == nullptr || _store == nullptr) {
    return false;
  }
  if (_store->has(def->key)) {
    if (!_store->remove(def->key)) {
      return false;  // still stored: no callback
    }
    notify(def->key);
  }
  return true;
}

bool ProjectSettings::queue(const char* const* keys, const String* values, const bool* isNull, size_t n) {
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

void ProjectSettings::loop() {
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
        if (!clear(c.def->key)) {
          LOG_W("settings: %s: clear failed", c.def->key);
        }
      } else if (!set(c.def->key, c.value, err)) {
        LOG_W("settings: %s", err.c_str());  // it passed check() when it was queued
      }
    }
  }
}

void ProjectSettings::notify(const char* key) {
  for (const auto& cb : _onChange) {
    cb(key);
  }
}
