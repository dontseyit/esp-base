// Checks on a project's settings: keys and values. Pure, no Arduino includes,
// so it is tested on the host (test/test_settings_check).
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "WebGuard.h"

enum class SettingKind : uint8_t {
  Text,    // shown and edited as text
  Secret,  // write-only: the page and the console say only whether it is set
  Choice,  // one of a fixed list
};

namespace SettingsCheck {

// 1..15 characters of a-z, 0-9 and '_' (15 is the NVS limit for a key).
inline bool validKey(const char* key) {
  if (key == nullptr || *key == '\0') {
    return false;
  }
  size_t n = 0;
  for (const char* p = key; *p != '\0'; ++p, ++n) {
    const char c = *p;
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
      return false;
    }
  }
  return n <= 15;
}

// choices is "a,b,c". True when value is exactly one of the items.
inline bool inChoices(const char* choices, const char* value) {
  if (choices == nullptr || value == nullptr || *value == '\0') {
    return false;
  }
  const size_t len = strlen(value);
  for (const char* p = choices; *p != '\0';) {
    const char* end = strchr(p, ',');
    const size_t n = end != nullptr ? static_cast<size_t>(end - p) : strlen(p);
    if (n == len && memcmp(p, value, len) == 0) {
      return true;
    }
    if (end == nullptr) {
      break;
    }
    p = end + 1;
  }
  return false;
}

// nullptr when the value is fine, else a short reason.
inline const char* valueError(SettingKind kind, const char* choices, uint16_t maxLen, const char* value) {
  if (value == nullptr) {
    return "missing";
  }
  if (strlen(value) > maxLen) {
    return "too long";
  }
  for (const uint8_t* p = reinterpret_cast<const uint8_t*>(value); *p != 0;) {
    const size_t n = *p < 0x80 ? 1 : WebGuard::utf8Length(p);  // utf8Length covers 2..4 byte sequences only
    if (n == 0) {
      return "not valid UTF-8";
    }
    p += n;
  }
  if (kind == SettingKind::Choice && !inChoices(choices, value)) {
    return "not one of the choices";
  }
  return nullptr;
}

}  // namespace SettingsCheck
