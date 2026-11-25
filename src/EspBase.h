// EspBase: the facade. Owns Log, ConfigStore, WifiManager, WebConsole and
// OtaUpdater, and wires them together.
//
//   #include <EspBase.h>
//   EspBaseConfig cfg;  cfg.hostname = "my-device";
//   EspBase base;
//   void setup() { Serial.begin(115200); cfg.logOutput = &Serial; base.begin(cfg); }
//   void loop()  { base.loop(); }
#pragma once

#include <Arduino.h>

#ifndef ESPBASE_NO_APPS
#include "AppManager.h"
#endif
#include "ChipInfo.h"
#include "ConfigStore.h"
#include "DeviceStatus.h"
#include "EspBaseConfig.h"
#include "Log.h"
#include "ProjectSettings.h"
#include "WebConsole.h"
#include "WifiManager.h"
#ifndef ESPBASE_NO_OTA
#include "OtaUpdater.h"
#endif

class EspBase {
 public:
  // Starts everything and returns without waiting for the network.
  bool begin(const EspBaseConfig& cfg);
  // Call from loop(); never blocks.
  void loop();

  WebConsole& console() { return _console; }
  AsyncWebServer& web() { return _console.server(); }  // valid after begin()
  WifiManager& wifi() { return _wifi; }
  ConfigStore& config() { return _store; }
  ProjectSettings& settings() { return _settings; }  // the project's own settings; add them after begin()
  DeviceStatus& status() { return _status; }  // the project's values and buttons in the Device tab; add them after begin()
#ifndef ESPBASE_NO_APPS
  AppManager& apps() { return _apps; }  // call apps().start(...) after begin() to run the registered apps
#endif
#ifndef ESPBASE_NO_OTA
  OtaUpdater& ota() { return _ota; }
#endif
  const EspBaseConfig& cfg() const { return _cfg; }
  static const char* version() { return ESPBASE_VERSION; }

  // Schedules a restart from loop() (gives responses time to flush).
  void reboot(uint32_t delayMs = 500);
  // Erases the "espbase" namespace and reboots.
  void factoryReset();

 private:
  void registerBuiltins();
  void wifiCommand(const CmdArgs& a, Print& out);
  void configCommand(const CmdArgs& a, Print& out);
  void showConfigKey(const char* key, bool skipUnset, Print& out);
  void showSetting(const SettingDef& def, Print& out);
  void updateStatusLed();

  EspBaseConfig _cfg;
  ConfigStore _store;
  ProjectSettings _settings;
  DeviceStatus _status;
  WifiManager _wifi;
  WebConsole _console;
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
