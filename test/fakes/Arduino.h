// Host stand-ins for the Arduino core, enough for library sources to compile
// unchanged in `pio test -e native` (test/test_wifi_manager). They do the least
// the tests need; nothing here is used on a device.
#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <algorithm>
#include <string>
#include <type_traits>

#if defined(__GLIBC__) && (__GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 38))
inline size_t strlcpy(char* dst, const char* src, size_t size) {  // glibc has it from 2.38 on
  const size_t n = strlen(src);
  if (size > 0) {
    const size_t k = n < size - 1 ? n : size - 1;
    memcpy(dst, src, k);
    dst[k] = '\0';
  }
  return n;
}
#endif

inline uint32_t fakeMillis = 0;  // the tests move the clock
inline uint32_t millis() { return fakeMillis; }

class String {
 public:
  String() = default;
  String(const char* s) : _s(s != nullptr ? s : "") {}
  template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
  explicit String(T v) : _s(std::to_string(v)) {}

  const char* c_str() const { return _s.c_str(); }
  size_t length() const { return _s.size(); }
  bool isEmpty() const { return _s.empty(); }
  bool equalsIgnoreCase(const String& o) const { return strcasecmp(c_str(), o.c_str()) == 0; }
  bool concat(const char* s) { _s += s; return true; }
  // What ArduinoJson writes through.
  void clear() { _s.clear(); }
  void push_back(char c) { _s.push_back(c); }
  String& append(const char* s) { _s.append(s); return *this; }
  String& append(const char* s, size_t n) { _s.append(s, n); return *this; }

  bool operator==(const String& o) const { return _s == o._s; }
  bool operator==(const char* o) const { return _s == (o != nullptr ? o : ""); }
  bool operator!=(const String& o) const { return !(*this == o); }
  String& operator+=(const String& o) { _s += o._s; return *this; }
  friend String operator+(String a, const String& b) { return a += b; }

 private:
  std::string _s;
};

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
      write(p[i]);
    }
    return n;
  }
  size_t printf(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return n < 0 ? 0 : write(reinterpret_cast<const uint8_t*>(buf), std::min(static_cast<size_t>(n), sizeof(buf) - 1));
  }
  size_t print(const char* s) { return write(reinterpret_cast<const uint8_t*>(s), strlen(s)); }
  size_t println(const char* s = "") { return print(s) + print("\r\n"); }
  size_t println(const String& s) { return println(s.c_str()); }
};

class IPAddress {
 public:
  IPAddress() = default;
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : _a{a, b, c, d} {}
  String toString() const {
    char s[16];
    snprintf(s, sizeof(s), "%u.%u.%u.%u", _a[0], _a[1], _a[2], _a[3]);
    return String(s);
  }

 private:
  uint8_t _a[4] = {0, 0, 0, 0};
};
