// Pure request checks used by WebConsole. No Arduino dependency, so they are
// unit tested on the host: pio test -e native.
#pragma once

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

namespace WebGuard {

// Length of the valid UTF-8 sequence that starts at p (2 to 4 bytes), or 0
// when it is invalid: truncated, overlong, a surrogate or beyond U+10FFFF.
inline size_t utf8Length(const uint8_t* p) {
  size_t n = 0;
  uint32_t min = 0;
  if ((p[0] & 0xE0) == 0xC0) {
    n = 2;
    min = 0x80;
  } else if ((p[0] & 0xF0) == 0xE0) {
    n = 3;
    min = 0x800;
  } else if ((p[0] & 0xF8) == 0xF0) {
    n = 4;
    min = 0x10000;
  } else {
    return 0;
  }
  uint32_t cp = p[0] & (0x7F >> n);
  for (size_t i = 1; i < n; ++i) {
    if ((p[i] & 0xC0) != 0x80) {
      return 0;  // also stops at a terminating NUL
    }
    cp = (cp << 6) | (p[i] & 0x3F);
  }
  if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
    return 0;
  }
  return n;
}

// True when a Host header value names this device: an IP literal, `name` or
// `name.local`, with an optional port. Anything else reaching the device is a
// DNS rebinding attempt. An absent Host (non-browser clients) is allowed.
inline bool hostAllowed(const char* host, const char* name) {
  if (host == nullptr || *host == '\0') {
    return true;
  }
  if (*host == '[') {
    return strchr(host, ']') != nullptr;  // IPv6 literal
  }
  const char* colon = strchr(host, ':');
  const size_t n = colon != nullptr ? static_cast<size_t>(colon - host) : strlen(host);
  size_t dots = 0;
  bool ipv4 = n > 0;
  for (size_t i = 0; i < n && ipv4; ++i) {
    if (host[i] == '.') {
      dots++;
    } else if (!isdigit(static_cast<unsigned char>(host[i]))) {
      ipv4 = false;
    }
  }
  if (ipv4 && dots == 3) {
    return true;
  }
  const size_t len = strlen(name);
  if (len == 0 || strncasecmp(host, name, len) != 0) {
    return false;
  }
  return n == len || (n == len + 6 && strncasecmp(host + len, ".local", 6) == 0);
}

// True when an Origin header (null when absent) names the same host and port
// as the Host header. Browsers send Origin on cross-site requests and on every
// WebSocket handshake; same-origin GETs and non-browser clients send none.
inline bool originMatches(const char* origin, const char* host) {
  if (origin == nullptr) {
    return true;
  }
  const char* sep = strstr(origin, "://");
  return sep != nullptr && host != nullptr && strcasecmp(sep + 3, host) == 0;
}

}  // namespace WebGuard
