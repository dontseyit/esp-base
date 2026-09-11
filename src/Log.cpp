#include "Log.h"

#include <string.h>

LogClass Log;

LogClass::LogClass() : _mutex(xSemaphoreCreateMutex()) {}

bool LogClass::lock() {
  if (_mutex == nullptr) {
    _mutex = xSemaphoreCreateMutex();  // retry if the constructor could not create it
  }
  return _mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE;
}

void LogClass::unlock() {
  xSemaphoreGive(_mutex);
}

bool LogClass::begin(size_t ringBytes, Print* output) {
  if (ringBytes < kMinRing) {
    ringBytes = kMinRing;
  }
  if (!lock()) {
    return false;
  }
  _out = output;
  if (_buf == nullptr || _cap != ringBytes) {
    uint8_t* fresh = static_cast<uint8_t*>(malloc(ringBytes));
    if (fresh == nullptr) {
      unlock();
      return false;
    }
    free(_buf);
    _buf = fresh;
    _cap = ringBytes;
    _head = _tail = _used = 0;
    _seqTail = _seqHead;  // everything before is gone
  }
  unlock();
  return true;
}

void LogClass::setOutput(Print* output) {
  if (lock()) {
    _out = output;
    unlock();
  }
}

char LogClass::levelChar(LogLevel level) {
  switch (level) {
    case LogLevel::Error: return 'E';
    case LogLevel::Warn: return 'W';
    case LogLevel::Info: return 'I';
    case LogLevel::Debug: return 'D';
    default: return 'N';
  }
}

const char* LogClass::levelName(LogLevel level) {
  switch (level) {
    case LogLevel::Error: return "error";
    case LogLevel::Warn: return "warn";
    case LogLevel::Info: return "info";
    case LogLevel::Debug: return "debug";
    default: return "none";
  }
}

bool LogClass::parseLevel(const char* text, LogLevel& out) {
  if (text == nullptr || *text == '\0') {
    return false;
  }
  switch (tolower(static_cast<unsigned char>(text[0]))) {
    case 'e': case '1': out = LogLevel::Error; return true;
    case 'w': case '2': out = LogLevel::Warn; return true;
    case 'i': case '3': out = LogLevel::Info; return true;
    case 'd': case '4': out = LogLevel::Debug; return true;
    case 'n': case '0': out = LogLevel::None; return true;
    default: return false;
  }
}

void LogClass::printf(LogLevel level, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(level, fmt, ap);
  va_end(ap);
}

void LogClass::vprintf(LogLevel level, const char* fmt, va_list ap) {
  if (!enabled(level)) {
    return;  // skip formatting as well
  }
  char buf[kMaxMessage + 1];
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  if (n < 0) {
    return;
  }
  size_t len = static_cast<size_t>(n);
  if (len > kMaxMessage) {
    len = kMaxMessage;
    memcpy(buf + kMaxMessage - 3, "...", 3);
  }
  write(level, buf, len);
}

void LogClass::write(LogLevel level, const char* msg, size_t len) {
  if (xPortInIsrContext()) {
    return;  // never from an ISR
  }
  if (!enabled(level)) {
    return;
  }
  if (len > kMaxMessage) {
    len = kMaxMessage;
  }
  while (len > 0 && (msg[len - 1] == '\n' || msg[len - 1] == '\r')) {
    len--;
  }
  const uint32_t ms = millis();
  // Prefix, message and CRLF in one buffer, so the serial sink gets one write
  // per line. Control characters (SSIDs, command input) are replaced so they
  // cannot forge log lines or inject terminal escape sequences.
  char line[kMaxMessage + 32];
  const size_t prefix = static_cast<size_t>(snprintf(line, sizeof(line), "[%8lu][%c] ", static_cast<unsigned long>(ms), levelChar(level)));
  char* text = line + prefix;
  for (size_t i = 0; i < len; ++i) {
    const uint8_t c = static_cast<uint8_t>(msg[i]);
    text[i] = (c < 0x20 || c == 0x7F) ? '?' : static_cast<char>(c);
  }
  text[len] = '\r';
  text[len + 1] = '\n';
  if (!lock()) {
    return;
  }
  if (_out != nullptr) {
    _out->write(reinterpret_cast<const uint8_t*>(line), prefix + len + 2);
  }
  if (_buf == nullptr) {
    // First use before begin(): keep early messages in a default sized ring.
    _buf = static_cast<uint8_t*>(malloc(kDefaultRing));
    if (_buf != nullptr) {
      _cap = kDefaultRing;
      _head = _tail = _used = 0;
    }
  }
  const size_t need = kHeader + len;
  if (_buf != nullptr && need <= _cap) {
    while (_cap - _used < need) {
      dropOldest();
    }
    const uint16_t len16 = static_cast<uint16_t>(len);
    const uint8_t lvl = static_cast<uint8_t>(level);
    putBytes(&ms, sizeof(ms));
    putBytes(&lvl, sizeof(lvl));
    putBytes(&len16, sizeof(len16));
    putBytes(text, len);
    _used += need;
    _seqHead++;
  }
  unlock();
}

void LogClass::dropOldest() {
  if (_used == 0) {
    return;
  }
  const size_t rec = kHeader + recordLen(_tail);
  _tail = (_tail + rec) % _cap;
  _used -= rec;
  _seqTail++;
  _overwritten++;
}

void LogClass::putBytes(const void* src, size_t n) {
  const uint8_t* s = static_cast<const uint8_t*>(src);
  const size_t first = (n <= _cap - _head) ? n : (_cap - _head);
  memcpy(_buf + _head, s, first);
  if (first < n) {
    memcpy(_buf, s + first, n - first);
  }
  _head = (_head + n) % _cap;
}

void LogClass::getBytes(size_t offset, void* dst, size_t n) const {
  uint8_t* d = static_cast<uint8_t*>(dst);
  const size_t first = (n <= _cap - offset) ? n : (_cap - offset);
  memcpy(d, _buf + offset, first);
  if (first < n) {
    memcpy(d + first, _buf, n - first);
  }
}

uint16_t LogClass::recordLen(size_t offset) const {
  uint16_t len = 0;
  getBytes((offset + kLenOffset) % _cap, &len, sizeof(len));
  return len;
}

LogClass::Cursor LogClass::tail() {
  Cursor c;
  if (lock()) {
    c.seq = _seqTail;
    c.offset = _tail;
    unlock();
  }
  return c;
}

LogClass::Cursor LogClass::head() {
  Cursor c;
  if (lock()) {
    c.seq = _seqHead;
    c.offset = _head;
    unlock();
  }
  return c;
}

uint32_t LogClass::sequence() {
  uint32_t s = 0;
  if (lock()) {
    s = _seqHead;
    unlock();
  }
  return s;
}

uint32_t LogClass::overwritten() {
  uint32_t s = 0;
  if (lock()) {
    s = _overwritten;
    unlock();
  }
  return s;
}

size_t LogClass::pending(const Cursor& c) {
  size_t n = 0;
  if (lock()) {
    const uint32_t seq = (c.seq < _seqTail) ? _seqTail : c.seq;
    n = (seq < _seqHead) ? (_seqHead - seq) : 0;
    unlock();
  }
  return n;
}

bool LogClass::read(Cursor& c, Record& out, uint32_t* dropped) {
  if (dropped != nullptr) {
    *dropped = 0;
  }
  if (!lock()) {
    return false;
  }
  if (_buf == nullptr) {
    unlock();
    return false;
  }
  if (c.seq < _seqTail) {
    if (dropped != nullptr) {
      *dropped = _seqTail - c.seq;
    }
    c.seq = _seqTail;
    c.offset = _tail;
  }
  if (c.seq >= _seqHead) {
    unlock();
    return false;
  }
  uint8_t lvl = 0;
  getBytes(c.offset, &out.ms, sizeof(out.ms));
  getBytes((c.offset + sizeof(out.ms)) % _cap, &lvl, sizeof(lvl));
  const uint16_t len = recordLen(c.offset);
  const size_t copy = (len <= kMaxMessage) ? len : kMaxMessage;
  getBytes((c.offset + kHeader) % _cap, out.msg, copy);
  out.msg[copy] = '\0';
  out.len = static_cast<uint16_t>(copy);
  out.level = static_cast<LogLevel>(lvl);
  c.offset = (c.offset + kHeader + len) % _cap;
  c.seq++;
  unlock();
  return true;
}

void LogClass::dump(Print& out) {
  Cursor c = tail();
  Record r;
  while (read(c, r)) {
    out.printf("[%8lu][%c] %s\r\n", static_cast<unsigned long>(r.ms), levelChar(r.level), r.msg);
  }
}
