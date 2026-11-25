// A host stand-in for NVS: one map for every namespace, which outlives the
// objects that use it, as flash outlives a reboot. The tests clear it.
#pragma once

#include <Arduino.h>

#include <map>

inline std::map<std::string, std::string> fakeNvs;

class Preferences {
 public:
  bool begin(const char* ns, bool = false) {
    _ns = std::string(ns) + "/";
    return true;
  }
  void end() {}
  bool isKey(const char* key) { return fakeNvs.count(_ns + key) != 0; }
  bool remove(const char* key) { return fakeNvs.erase(_ns + key) != 0; }
  bool clear() {
    for (auto it = fakeNvs.begin(); it != fakeNvs.end();) {
      it = it->first.rfind(_ns, 0) == 0 ? fakeNvs.erase(it) : std::next(it);
    }
    return true;
  }
  size_t freeEntries() { return 100; }

  String getString(const char* key, const String& def = String()) {
    const auto it = fakeNvs.find(_ns + key);
    return it == fakeNvs.end() ? def : String(it->second.c_str());
  }
  size_t putString(const char* key, const String& value) {
    fakeNvs[_ns + key] = value.c_str();
    return value.length();
  }
  int32_t getInt(const char* key, int32_t def = 0) {
    const auto it = fakeNvs.find(_ns + key);
    return it == fakeNvs.end() ? def : static_cast<int32_t>(strtol(it->second.c_str(), nullptr, 10));
  }
  size_t putInt(const char* key, int32_t value) {
    fakeNvs[_ns + key] = std::to_string(value);
    return sizeof(value);
  }
  uint8_t getUChar(const char* key, uint8_t def = 0) { return static_cast<uint8_t>(getInt(key, def)); }
  size_t putUChar(const char* key, uint8_t value) { return putInt(key, value) > 0 ? sizeof(value) : 0; }
  bool getBool(const char* key, bool def = false) { return getInt(key, def) != 0; }
  size_t putBool(const char* key, bool value) { return putInt(key, value) > 0 ? 1 : 0; }

 private:
  std::string _ns;
};
