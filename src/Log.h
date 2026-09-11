// Structured logging: printf style macros, millis + level prefix, a serial
// sink (any Print*), and a ring buffer that the WebConsole replays and streams.
//
//   LOG_E("bad thing %d", code);
//   LOG_I("up: %s", WiFi.localIP().toString().c_str());
//
// Thread safe (FreeRTOS mutex), callable from any task, never from an ISR.
// Compile out levels with -DESPBASE_LOG_MAX_LEVEL=<0..4> (default 4 = debug).
#pragma once

#include <Arduino.h>
#include <stdarg.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

enum class LogLevel : uint8_t { None = 0, Error = 1, Warn = 2, Info = 3, Debug = 4 };

#ifndef ESPBASE_LOG_MAX_LEVEL
#define ESPBASE_LOG_MAX_LEVEL 4
#endif

class LogClass {
 public:
  static constexpr size_t kMaxMessage = 240;  // longer messages are truncated
  static constexpr size_t kMinRing = 512;
  static constexpr size_t kDefaultRing = 4096;  // used when logging starts before begin()

  // A reader's position in the ring buffer. Readers keep their own cursor.
  struct Cursor {
    uint32_t seq = 0;
    size_t offset = 0;
  };

  struct Record {
    uint32_t ms = 0;
    LogLevel level = LogLevel::Info;
    uint16_t len = 0;
    char msg[kMaxMessage + 1] = {0};
  };

  LogClass();

  // Allocates the ring buffer (re-allocates only if the size changes, so
  // messages logged before begin() survive) and sets the optional serial sink.
  // Safe to call more than once.
  bool begin(size_t ringBytes = 4096, Print* output = nullptr);

  void setOutput(Print* output);
  Print* output() const { return _out; }

  void setLevel(LogLevel level) { _level = level; }
  LogLevel level() const { return _level; }

  static char levelChar(LogLevel level);
  static const char* levelName(LogLevel level);
  // Accepts "e|w|i|d|n", "error|warn|info|debug|none" (case insensitive) or "0".."4".
  static bool parseLevel(const char* text, LogLevel& out);

  void printf(LogLevel level, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
  void vprintf(LogLevel level, const char* fmt, va_list ap);
  // Pre-formatted message (no trailing newline needed).
  void write(LogLevel level, const char* msg, size_t len);

  // ---- Ring buffer readers ----------------------------------------------
  Cursor tail();  // oldest stored record
  Cursor head();  // one past the newest record
  // Copies the record at `c` into `out` and advances `c`. Returns false when
  // nothing new is available. If the reader fell behind and records were
  // overwritten, `c` is moved to the oldest record and *dropped is set.
  bool read(Cursor& c, Record& out, uint32_t* dropped = nullptr);
  size_t pending(const Cursor& c);  // records between c and head
  uint32_t sequence();              // total records written since boot
  size_t ringBytes() const { return _cap; }
  // Prints the whole ring buffer as text, oldest first.
  void dump(Print& out);

  // Number of records dropped by the ring since boot (for diagnostics).
  uint32_t overwritten();

 private:
  static constexpr size_t kHeader = 4 + 1 + 2;  // ms, level, len
  static constexpr size_t kLenOffset = 4 + 1;

  bool enabled(LogLevel level) const { return level != LogLevel::None && level <= _level; }
  bool lock();
  void unlock();
  void dropOldest();
  void putBytes(const void* src, size_t n);
  void getBytes(size_t offset, void* dst, size_t n) const;
  uint16_t recordLen(size_t offset) const;

  uint8_t* _buf = nullptr;
  size_t _cap = 0;
  size_t _head = 0;   // write position
  size_t _tail = 0;   // oldest record
  size_t _used = 0;   // bytes in use
  uint32_t _seqHead = 0;
  uint32_t _seqTail = 0;
  uint32_t _overwritten = 0;
  Print* _out = nullptr;
  LogLevel _level = LogLevel::Info;
  SemaphoreHandle_t _mutex = nullptr;
};

extern LogClass Log;

// Compiled-out levels keep their arguments type checked (and used) but never evaluate them.
#define ESPBASE_LOG_OFF(lvl, fmt, ...) do { if (0) Log.printf(lvl, fmt, ##__VA_ARGS__); } while (0)

#if ESPBASE_LOG_MAX_LEVEL >= 1
#define LOG_E(fmt, ...) Log.printf(LogLevel::Error, fmt, ##__VA_ARGS__)
#else
#define LOG_E(fmt, ...) ESPBASE_LOG_OFF(LogLevel::Error, fmt, ##__VA_ARGS__)
#endif
#if ESPBASE_LOG_MAX_LEVEL >= 2
#define LOG_W(fmt, ...) Log.printf(LogLevel::Warn, fmt, ##__VA_ARGS__)
#else
#define LOG_W(fmt, ...) ESPBASE_LOG_OFF(LogLevel::Warn, fmt, ##__VA_ARGS__)
#endif
#if ESPBASE_LOG_MAX_LEVEL >= 3
#define LOG_I(fmt, ...) Log.printf(LogLevel::Info, fmt, ##__VA_ARGS__)
#else
#define LOG_I(fmt, ...) ESPBASE_LOG_OFF(LogLevel::Info, fmt, ##__VA_ARGS__)
#endif
#if ESPBASE_LOG_MAX_LEVEL >= 4
#define LOG_D(fmt, ...) Log.printf(LogLevel::Debug, fmt, ##__VA_ARGS__)
#else
#define LOG_D(fmt, ...) ESPBASE_LOG_OFF(LogLevel::Debug, fmt, ##__VA_ARGS__)
#endif
