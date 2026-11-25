// The access point's name. Pure, so it is tested on the host
// (test/test_wifi_naming).
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace WifiNaming {

// "<prefix>-XXXX": the prefix cut to 27 characters, then the last two bytes
// of the address in hex. 32 characters at most, the limit for an SSID.
inline void apSsid(char* out, size_t size, const char* prefix, const uint8_t mac[6]) {
  snprintf(out, size, "%.27s-%02X%02X", prefix, mac[4], mac[5]);
}

}  // namespace WifiNaming
