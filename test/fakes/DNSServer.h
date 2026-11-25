// Host stand-in for the captive portal's DNS server.
#pragma once

#include <Arduino.h>

class DNSServer {
 public:
  void setTTL(uint32_t) {}
  bool start(uint16_t, const String&, const IPAddress&) { return _up = true; }
  void stop() { _up = false; }
  bool isUp() { return _up; }

 private:
  bool _up = false;
};
