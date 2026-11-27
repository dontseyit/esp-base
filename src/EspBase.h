// EspBase: the facade. Owns Log, ConfigStore, WifiManager, Console and, unless
// compiled out, WebConsole (-DESPBASE_NO_WEB), OtaUpdater (-DESPBASE_NO_OTA)
// and AppManager (-DESPBASE_NO_APPS), and wires them together.
//
//   #include <EspBase.h>
//   using namespace espbase;
//   EspBaseConfig cfg;  cfg.hostname = "my-device";
//   EspBase base;
//   void setup() { Serial.begin(115200); cfg.logOutput = &Serial; base.begin(cfg); }
//   void loop()  { base.loop(); base.console().poll(Serial); }
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "ChipInfo.h"
#include "ConfigStore.h"
#include "Console.h"
#include "EspBaseConfig.h"
#include "Keys.h"
#include "Log.h"
#include "WifiManager.h"
#ifndef ESPBASE_NO_WEB
#include "WebConsole.h"
#endif
#ifndef ESPBASE_NO_OTA
#include "OtaUpdater.h"
#endif
#ifndef ESPBASE_NO_APPS
#include "AppManager.h"
#endif

namespace espbase {

class EspBase {
 public:
  // Starts everything and returns without waiting for the network.
  bool begin(const EspBaseConfig& cfg);
  // Call from loop(); never blocks.
  void loop();

  Console& console() { return _console; }
  WifiManager& wifi() { return _wifi; }
  ConfigStore& config() { return _store; }
#ifndef ESPBASE_NO_WEB
  WebConsole& webConsole() { return _web; }
  AsyncWebServer& web() { return _web.server(); }  // valid after begin()
#endif
#ifndef ESPBASE_NO_APPS
  AppManager& apps() { return _apps; }  // call apps().start(...) after begin() to run the registered apps
#endif
#ifndef ESPBASE_NO_OTA
  OtaUpdater& ota() { return _ota; }
#endif
  const EspBaseConfig& cfg() const { return _cfg; }
  static const char* version() { return ESPBASE_VERSION; }

  // Device summary shared by the "info" command and GET /api/info.
  void writeInfo(JsonObject o);

  // Schedules a restart from loop() (gives responses time to flush).
  void reboot(uint32_t delayMs = 500);
  // Erases the "espbase" namespace and reboots.
  void factoryReset();

 private:
  void registerBuiltins();
  void wifiCommand(const CmdArgs& a, Print& out);
  void configCommand(const CmdArgs& a, Print& out);
  void showConfigKey(const char* key, bool skipUnset, Print& out);
  void updateStatusLed();

  EspBaseConfig _cfg;
  ConfigStore _store;
  Console _console;
  WifiManager _wifi;
#ifndef ESPBASE_NO_WEB
  WebConsole _web;
#endif
#ifndef ESPBASE_NO_APPS
  AppManager _apps;
#endif
#ifndef ESPBASE_NO_OTA
  OtaUpdater _ota;
#endif
  bool _begun = false;
  uint32_t _rebootAt = 0;
  bool _ledOn = false;
};

}  // namespace espbase
