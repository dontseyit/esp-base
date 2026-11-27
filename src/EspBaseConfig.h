// User facing configuration for EspBase. Fill in, then pass to EspBase::begin().
#pragma once

#include <Arduino.h>

#include "Log.h"

#define ESPBASE_VERSION "2.0.0"

// The library uses Arduino core 3.x APIs (Network, async disconnect, scan
// timing) and the ESP32Async web stack. Fail here with one line instead of
// pages of errors on an older core or another chip family.
#ifndef ESP32
#error "esp-base targets the ESP32 family"
#endif
#include <esp_arduino_version.h>
#if !defined(ESP_ARDUINO_VERSION_VAL) || ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 0, 0)
#error "esp-base needs Arduino core 3.x (pioarduino platform or arduino-esp32 3.x); see README, Requirements"
#endif

namespace espbase {

// How the radio is used. Persisted as the "wifi_mode" key (sta | ap | apsta).
enum class WifiMode : uint8_t {
  Sta,    // station, AP only as a fallback (default)
  Ap,     // AP only, the station is never used
  ApSta,  // AP always on, station connects next to it
};

struct EspBaseConfig {
  // Identity ---------------------------------------------------------------
  String hostname = "esp-base";     // DHCP hostname and mDNS name (<hostname>.local)
  String apSsidPrefix = "EspBase";  // AP SSID = prefix + "-" + last 4 hex digits of the MAC
  String apPassword = "";           // empty = open AP; otherwise 8..63 characters (WPA2)

  // WiFi behaviour. Persisted keys override these defaults: "wifi_mode",
  // "sta_timeout", "reconn_timeout", "ap_retry" (set from the console or the web UI).
  WifiMode wifiMode = WifiMode::Sta;
  uint32_t staTimeoutMs = 20000;        // per network: give up on one attempt after this (3 s .. 300 s)
  uint32_t reconnectTimeoutMs = 60000;  // after a lost link: retry with backoff this long, then AP fallback; 0 = AP immediately
  uint32_t apRetryIntervalMs = 60000;   // in AP fallback: retry every stored network this often; 0 = never (manual "wifi reconnect")

  // Time ---------------------------------------------------------------------
  String ntpServer = "pool.ntp.org";  // SNTP server, synced whenever the station connects; empty = no time sync
  String timezone = "UTC0";           // POSIX TZ string, e.g. "CET-1CEST,M3.5.0,M10.5.0/3"

  // Hardware -----------------------------------------------------------------
  int statusLedPin = -1;  // -1 = none. The project supplies the pin, never the library.
  bool statusLedActiveLow = false;

  // Web console (compile the web server out with -DESPBASE_NO_WEB) ----------
  uint16_t httpPort = 80;
  uint8_t maxWsClients = 4;  // oldest client is dropped beyond this
  bool webUi = true;         // serve the built-in page at "/"; false = bring your own UI over the same /api and /ws
  String webPassword =
      "";  // page, API and POST /update password (HTTP digest, user "admin"), 8..63 chars; empty = open. Key "web_pass" overrides it.

  // OTA (compile out entirely with -DESPBASE_NO_OTA) ---------------------------
  bool otaEnabled = true;   // runtime switch
  String otaPassword = "";  // used by ArduinoOTA and as HTTP basic auth password for POST /update (user "admin")

  // Logging ------------------------------------------------------------------
  Print* logOutput = nullptr;          // e.g. &Serial after Serial.begin(); nullptr = no serial sink
  size_t logBufferBytes = 4096;        // ring buffer replayed to new console clients
  LogLevel logLevel = LogLevel::Info;  // default; overridden by the persisted "log_lvl" key

  // Version reported by /api/info and the "info" command. Pass -DFW_VERSION=\"1.2.3\".
#ifdef FW_VERSION
  const char* fwVersion = FW_VERSION;
#else
  const char* fwVersion = "0.0.0";
#endif
};

}  // namespace espbase
