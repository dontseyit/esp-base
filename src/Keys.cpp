#include "Keys.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace espbase {
namespace Keys {
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

const char* const All[] = {StaSsid,     StaPass,     "sta2_ssid",   "sta2_pass", "sta3_ssid", "sta3_pass", "sta4_ssid",
                           "sta4_pass", "sta5_ssid", "sta5_pass",   Hostname,    ApPass,      WebPass,     LogLevelKey,
                           WifiModeKey, StaTimeout,  ReconnTimeout, ApRetry,     BootApp,     nullptr};

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

}  // namespace Keys
}  // namespace espbase
