// User facing configuration for EspBase. Fill in, then pass to EspBase::begin().
#pragma once

#include <Arduino.h>

#include "Log.h"

#define ESPBASE_VERSION "1.2.0"

#ifndef FW_VERSION
#define FW_VERSION "0.0.0"
#endif

// How the radio is used. Persisted as the "wifi_mode" key (sta | ap | apsta).
enum class WifiMode : uint8_t {
  Sta,    // station, AP only as a fallback (default)
  Ap,     // AP only, the station is never used
  ApSta,  // AP always on, station connects next to it
};

struct EspBaseConfig {
  // Identity ---------------------------------------------------------------
  String hostname = "esp-base";      // DHCP hostname and mDNS name (<hostname>.local)
  String apSsidPrefix = "EspBase";   // AP SSID = prefix + "-" + last 4 hex digits of the MAC
  String apPassword = "";            // empty = open AP; otherwise 8..63 characters (WPA2)

  // WiFi behaviour. Persisted keys override these defaults: "wifi_mode",
  // "sta_timeout", "reconn_timeout", "ap_retry" (set from the console or the web UI).
  WifiMode wifiMode = WifiMode::Sta;
  uint32_t staTimeoutMs = 20000;        // per network: give up on one attempt after this (3 s .. 300 s)
  uint32_t reconnectTimeoutMs = 60000;  // after a lost link: retry with backoff this long, then AP fallback; 0 = AP immediately
  uint32_t apRetryIntervalMs = 60000;   // in AP fallback: retry every stored network this often; 0 = never (manual "wifi reconnect")

  // Hardware -----------------------------------------------------------------
  int statusLedPin = -1;             // -1 = none. The project supplies the pin, never the library.
  bool statusLedActiveLow = false;

  // Web console ------------------------------------------------------------
  uint16_t httpPort = 80;
  uint8_t maxWsClients = 4;          // oldest client is dropped beyond this
  String webPassword = "";           // console, API and POST /update password (HTTP digest, user "admin"), 8..63 chars; empty = open. Key "web_pass" overrides it.

  // OTA ----------------------------------------------------------------------
  bool otaEnabled = true;            // runtime switch; compile out entirely with -DESPBASE_NO_OTA
  String otaPassword = "";           // used by ArduinoOTA and as HTTP basic auth password for POST /update (user "admin")

  // Logging ------------------------------------------------------------------
  Print* logOutput = nullptr;        // e.g. &Serial after Serial.begin(); nullptr = no serial sink
  size_t logBufferBytes = 4096;      // ring buffer replayed to new console clients
  LogLevel logLevel = LogLevel::Info;  // default; overridden by the persisted "log_lvl" key

  // Version reported by /api/info and the "info" command.
  const char* fwVersion = FW_VERSION;
};
