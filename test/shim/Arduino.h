// Minimal Arduino shim for host unit tests: String, Print, Stream, millis().
// Only what the library code under test uses.
#pragma once

#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

class String {
 public:
  String() = default;
  String(const char* s) : _s(s ? s : "") {}  // NOLINT: implicit like Arduino's
  String(const std::string& s) : _s(s) {}
  String(char c) : _s(1, c) {}
  explicit String(int v) : _s(std::to_string(v)) {}
  explicit String(unsigned v) : _s(std::to_string(v)) {}
  explicit String(long v) : _s(std::to_string(v)) {}
  explicit String(unsigned long v) : _s(std::to_string(v)) {}

  size_t length() const { return _s.size(); }
  bool isEmpty() const { return _s.empty(); }
  const char* c_str() const { return _s.c_str(); }
  char operator[](size_t i) const { return i < _s.size() ? _s[i] : '\0'; }
  String substring(size_t from) const { return from < _s.size() ? String(_s.substr(from)) : String(); }
  String substring(size_t from, size_t to) const { return from < _s.size() ? String(_s.substr(from, to - from)) : String(); }
  void trim() {
    size_t b = 0, e = _s.size();
    while (b < e && isspace(static_cast<unsigned char>(_s[b])))
      b++;
    while (e > b && isspace(static_cast<unsigned char>(_s[e - 1])))
      e--;
    _s = _s.substr(b, e - b);
  }
  bool equalsIgnoreCase(const char* o) const { return o != nullptr && strcasecmp(_s.c_str(), o) == 0; }
  bool equalsIgnoreCase(const String& o) const { return equalsIgnoreCase(o.c_str()); }
  int indexOf(char c) const {
    const size_t p = _s.find(c);
    return p == std::string::npos ? -1 : static_cast<int>(p);
  }
  void reserve(size_t n) { _s.reserve(n); }
  long toInt() const { return strtol(_s.c_str(), nullptr, 10); }
  void concat(const char* p, size_t n) { _s.append(p, n); }

  String& operator+=(char c) {
    _s += c;
    return *this;
  }
  String& operator+=(const char* s) {
    _s += s;
    return *this;
  }
  String& operator+=(const String& s) {
    _s += s._s;
    return *this;
  }
  String& operator+=(unsigned long v) {
    _s += std::to_string(v);
    return *this;
  }
  String& operator+=(uint32_t v) {
    _s += std::to_string(v);
    return *this;
  }
  bool operator==(const char* o) const { return o != nullptr && _s == o; }
  bool operator==(const String& o) const { return _s == o._s; }
  bool operator!=(const char* o) const { return !(*this == o); }
  bool operator!=(const String& o) const { return !(*this == o); }
  friend String operator+(const String& a, const char* b) { return String(a._s + b); }
  friend String operator+(const String& a, const String& b) { return String(a._s + b._s); }
  friend String operator+(const String& a, char b) { return String(a._s + b); }

 private:
  std::string _s;
};

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i)
      write(data[i]);
    return len;
  }
  size_t print(const char* s) { return write(reinterpret_cast<const uint8_t*>(s), strlen(s)); }
  size_t print(const String& s) { return print(s.c_str()); }
  size_t println(const char* s) { return print(s) + print("\r\n"); }
  size_t println(const String& s) { return println(s.c_str()); }
  size_t println() { return print("\r\n"); }
  size_t printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0)
      return 0;
    return write(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(n) < sizeof(buf) ? static_cast<size_t>(n) : sizeof(buf) - 1);
  }
};

class Stream : public Print {
 public:
  virtual int available() = 0;
  virtual int read() = 0;
};

// Test hooks: the clock is whatever the test says it is.
inline uint32_t& shimMillis() {
  static uint32_t ms = 0;
  return ms;
}
inline uint32_t millis() {
  return shimMillis();
}
inline uint32_t micros() {
  return shimMillis() * 1000;
}
