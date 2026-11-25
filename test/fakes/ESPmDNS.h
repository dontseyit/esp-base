// Host stand-in for mDNS.
#pragma once

#include <Arduino.h>

struct FakeMDNS {
  bool begin(const char*) { return true; }
  void addService(const char*, const char*, uint16_t) {}
};
inline FakeMDNS MDNS;
