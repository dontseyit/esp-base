#include "EspBase.h"

#include <esp_arduino_version.h>

namespace {
bool knownKey(const String& key) {
  for (const char* const* k = EspBaseKeys::All; *k != nullptr; ++k) {
    if (key == *k) {
      return true;
    }
  }
  return false;
}

// Network slot (1..) of a sta*_ssid / sta*_pass key, 0 for any other key.
uint8_t slotOf(const char* key) {
  uint8_t slot = 0;
  bool isPass = false;
  return EspBaseKeys::isSlotKey(key, slot, isPass) ? slot : 0;
}

// RFC 1123 label: the hostname becomes the DHCP and mDNS name and is matched against Host headers.
bool validHostname(const String& value) {
  const size_t n = value.length();
  if (n == 0 || n > 32 || value[0] == '-' || value[n - 1] == '-') {
    return false;
  }
  for (size_t i = 0; i < n; ++i) {
    if (!isalnum(static_cast<unsigned char>(value[i])) && value[i] != '-') {
      return false;
    }
  }
  return true;
}
}  // namespace

bool EspBase::begin(const EspBaseConfig& cfg) {
  if (_begun) {
    return true;
  }
  const uint32_t t0 = millis();
  _cfg = cfg;

  Log.begin(_cfg.logBufferBytes, _cfg.logOutput);
  Log.setLevel(_cfg.logLevel);
  _store.begin(EspBaseKeys::Namespace);
  if (_store.has(EspBaseKeys::LogLevelKey)) {
    const uint8_t lvl = _store.getUChar(EspBaseKeys::LogLevelKey, static_cast<uint8_t>(_cfg.logLevel));
    if (lvl <= static_cast<uint8_t>(LogLevel::Debug)) {
      Log.setLevel(static_cast<LogLevel>(lvl));
    }
  }

  LOG_I("esp-base %s | %s rev %u, %d core(s) @ %lu MHz | fw %s built %s", ESPBASE_VERSION, ESP.getChipModel(), ESP.getChipRevision(),
        ChipInfo::cores(), static_cast<unsigned long>(ESP.getCpuFreqMHz()), _cfg.fwVersion, __DATE__);
  LOG_I("arduino %s, idf %s | flash %lu KB, heap %lu KB free, psram %lu KB", ESP_ARDUINO_VERSION_STR, ESP.getSdkVersion(),
        static_cast<unsigned long>(ESP.getFlashChipSize() / 1024), static_cast<unsigned long>(ESP.getFreeHeap() / 1024),
        static_cast<unsigned long>(ESP.getPsramSize() / 1024));

  if (_cfg.statusLedPin >= 0) {
    pinMode(_cfg.statusLedPin, OUTPUT);
    digitalWrite(_cfg.statusLedPin, _cfg.statusLedActiveLow ? HIGH : LOW);
  }

  _wifi.begin(_cfg, _store);
  _console.begin(_cfg, _wifi, _store.getString(EspBaseKeys::WebPass, _cfg.webPassword));
#ifndef ESPBASE_NO_OTA
  _ota.begin(_cfg, _console, _wifi.hostname());
  _wifi.onConnected([this]() { _ota.onNetworkUp(_wifi.mdnsRunning()); });
  _wifi.onApStarted([this]() { _ota.onNetworkUp(_wifi.mdnsRunning()); });
#endif
  registerBuiltins();
  _apps.attach(*this);

  _begun = true;
  LOG_I("esp-base: ready in %lu ms", static_cast<unsigned long>(millis() - t0));
  return true;
}

void EspBase::loop() {
  if (!_begun) {
    return;
  }
  _wifi.loop();
  _console.loop();
  _apps.loop();
#ifndef ESPBASE_NO_OTA
  _ota.loop();
#endif
  updateStatusLed();
  if (_rebootAt != 0 && static_cast<int32_t>(millis() - _rebootAt) >= 0) {
    _rebootAt = 0;
    LOG_I("esp-base: rebooting");
    delay(100);  // one-off: let the serial sink drain before restart
    ESP.restart();
  }
}

void EspBase::reboot(uint32_t delayMs) {
  _rebootAt = millis() + (delayMs == 0 ? 1 : delayMs);
}

void EspBase::factoryReset() {
  LOG_W("esp-base: factory reset");
  _store.erase();
  reboot(500);
}

void EspBase::updateStatusLed() {
  if (_cfg.statusLedPin < 0) {
    return;
  }
  uint32_t period = 200, onMs = 100;  // connecting / reconnecting: fast blink
  switch (_wifi.state()) {
    case WifiState::StaConnected:
      period = 3000;  // short blink every 3 s
      onMs = 50;
      break;
    case WifiState::ApFallback:
    case WifiState::ApOnly:
      period = 1000;  // slow blink
      onMs = 500;
      break;
    case WifiState::Off:
      onMs = 0;  // dark
      break;
    default:
      break;
  }
  const bool on = (millis() % period) < onMs;
  if (on != _ledOn) {
    _ledOn = on;
    digitalWrite(_cfg.statusLedPin, (on != _cfg.statusLedActiveLow) ? HIGH : LOW);
  }
}

void EspBase::registerBuiltins() {
  _console.addCommand(
      "help", [this](const CmdArgs&, Print& out) { _console.printHelp(out); }, "list commands");

  _console.addCommand(
      "info", [this](const CmdArgs&, Print& out) { _console.printInfo(out); }, "chip, memory, network and version summary");

  _console.addCommand(
      "heap",
      [](const CmdArgs&, Print& out) {
        out.printf("heap:  free %lu, min %lu, largest block %lu, total %lu\r\n", static_cast<unsigned long>(ESP.getFreeHeap()),
                   static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()),
                   static_cast<unsigned long>(ESP.getHeapSize()));
        if (ESP.getPsramSize() > 0) {
          out.printf("psram: free %lu, total %lu\r\n", static_cast<unsigned long>(ESP.getFreePsram()),
                     static_cast<unsigned long>(ESP.getPsramSize()));
        }
      },
      "heap statistics");

  _console.addCommand(
      "reboot",
      [this](const CmdArgs&, Print& out) {
        out.println("rebooting");
        reboot(500);
      },
      "restart the device");

  _console.addCommand(
      "factory-reset",
      [this](const CmdArgs&, Print& out) {
        out.println("erasing stored settings and rebooting");
        factoryReset();
      },
      "erase WiFi credentials and settings, then reboot");

  _console.addCommand(
      "wifi", [this](const CmdArgs& a, Print& out) { wifiCommand(a, out); }, "wifi status|list|scan|set|add|remove|forget|reconnect|mode|on|off");

  _console.addCommand(
      "config", [this](const CmdArgs& a, Print& out) { configCommand(a, out); }, "config list|get <key>|set <key> <value>");

  _console.addCommand(
      "log",
      [this](const CmdArgs& a, Print& out) {
        if (a.is(0, "level")) {
          if (!a.has(1)) {
            out.printf("log level: %s\r\n", LogClass::levelName(Log.level()));
            return;
          }
          LogLevel lvl;
          if (!LogClass::parseLevel(a[1].c_str(), lvl)) {
            out.println("usage: log level e|w|i|d");
            return;
          }
          Log.setLevel(lvl);
          _store.setUChar(EspBaseKeys::LogLevelKey, static_cast<uint8_t>(lvl));
          out.printf("log level set to %s (persisted)\r\n", LogClass::levelName(lvl));
        } else if (a.is(0, "dump")) {
          Log.dump(out);
        } else {
          out.println("usage: log level [e|w|i|d] | log dump");
        }
      },
      "log level [e|w|i|d] | log dump");
}

void EspBase::wifiCommand(const CmdArgs& a, Print& out) {
  if (!a.has(0) || a.is(0, "status")) {
    _wifi.printStatus(out);
  } else if (a.is(0, "list")) {
    _wifi.printNetworks(out);
  } else if (a.is(0, "scan")) {
    out.println(_wifi.startScan() ? "scan started, results appear in the log" : "scan not started (busy or already running)");
  } else if (a.is(0, "set")) {
    if (!a.has(1)) {
      out.println("usage: wifi set <ssid> [password]   (quote names with spaces; stores slot 1 and connects)");
    } else if (_wifi.setCredentials(a[1], a[2], 1)) {
      out.printf("slot 1 set to \"%s\", connecting\r\n", a[1].c_str());
    } else {
      out.println("rejected: ssid 1..32 chars, password empty or 8..63 chars");
    }
  } else if (a.is(0, "add")) {
    if (!a.has(1)) {
      out.printf("usage: wifi add <ssid> [password]   (stores in the first free of %u slots)\r\n", WifiManager::kMaxSlots);
      return;
    }
    const uint8_t slot = _wifi.addCredentials(a[1], a[2]);
    if (slot > 0) {
      out.printf("\"%s\" stored in slot %u\r\n", a[1].c_str(), slot);
    } else {
      out.printf("not stored: all %u slots in use, or ssid/password length invalid\r\n", WifiManager::kMaxSlots);
    }
  } else if (a.is(0, "remove")) {
    const long slot = a.toInt(1, 0);
    if (slot >= 1 && slot <= WifiManager::kMaxSlots && _wifi.removeCredentials(static_cast<uint8_t>(slot))) {
      out.printf("slot %ld removed\r\n", slot);
    } else {
      out.printf("usage: wifi remove <slot 1..%u>\r\n", WifiManager::kMaxSlots);
    }
  } else if (a.is(0, "forget")) {
    _wifi.forgetCredentials();
    out.println("all stored networks removed, switching to AP mode");
  } else if (a.is(0, "reconnect")) {
    const long slot = a.toInt(1, 0);
    _wifi.reconnect(static_cast<uint8_t>(slot > 0 && slot <= WifiManager::kMaxSlots ? slot : 0));
    out.println("reconnecting");
  } else if (a.is(0, "mode")) {
    String err;
    if (!a.has(1)) {
      out.printf("wifi mode: %s   (sta = station with AP fallback, ap = AP only, apsta = AP always on)\r\n",
                 WifiManager::modeName(_wifi.settings().mode));
    } else if (_wifi.applySetting(EspBaseKeys::WifiModeKey, a[1], err)) {
      out.printf("wifi mode set to %s\r\n", a[1].c_str());
    } else {
      out.println(err);
    }
  } else if (a.is(0, "off")) {
    // Over the network this is the last thing the console hears: say so first.
    out.println("radio off until \"wifi on\" (serial) or a reboot; this console goes with it");
    _wifi.setEnabled(false);
  } else if (a.is(0, "on")) {
    _wifi.setEnabled(true);
    out.println("radio on");
  } else {
    out.println("usage: wifi status|list|scan|set <ssid> [pass]|add <ssid> [pass]|remove <slot>|forget|reconnect [slot]|mode [sta|ap|apsta]|on|off");
  }
}

void EspBase::showConfigKey(const char* key, bool skipUnset, Print& out) {
  if (!_store.has(key)) {
    if (!skipUnset) {
      out.printf("  %-14s (unset)\r\n", key);
    }
  } else if (strcmp(key, EspBaseKeys::LogLevelKey) == 0) {
    out.printf("  %-14s %s\r\n", key, LogClass::levelName(static_cast<LogLevel>(_store.getUChar(key))));
  } else if (strcmp(key, EspBaseKeys::StaTimeout) == 0 || strcmp(key, EspBaseKeys::ReconnTimeout) == 0 || strcmp(key, EspBaseKeys::ApRetry) == 0) {
    out.printf("  %-14s %ld ms\r\n", key, static_cast<long>(_store.getInt(key)));
  } else if (EspBaseKeys::isSecret(key)) {
    out.printf("  %-14s ******** (%u chars)\r\n", key, static_cast<unsigned>(_store.getString(key).length()));
  } else {
    out.printf("  %-14s %s\r\n", key, _store.getString(key).c_str());
  }
}

void EspBase::configCommand(const CmdArgs& a, Print& out) {
  if (!a.has(0) || a.is(0, "list")) {
    for (const char* const* k = EspBaseKeys::All; *k != nullptr; ++k) {
      showConfigKey(*k, slotOf(*k) > 1, out);  // unset backup slots are noise
    }
    out.printf("  (namespace \"%s\", %u free entries)\r\n", _store.ns(), static_cast<unsigned>(_store.freeEntries()));
    return;
  }
  if (!(a.is(0, "get") || a.is(0, "set")) || !a.has(1)) {
    out.println("usage: config list | config get <key> | config set <key> <value>");
    return;
  }
  const String& key = a[1];
  if (!knownKey(key)) {
    out.println("unknown key (see: config list)");
    return;
  }
  if (a.is(0, "get")) {
    showConfigKey(key.c_str(), false, out);
    return;
  }

  const String& value = a[2];
  if (key == EspBaseKeys::LogLevelKey) {
    LogLevel lvl;
    if (LogClass::parseLevel(value.c_str(), lvl)) {
      Log.setLevel(lvl);
      _store.setUChar(EspBaseKeys::LogLevelKey, static_cast<uint8_t>(lvl));
      out.printf("log level set to %s\r\n", LogClass::levelName(lvl));
    } else {
      out.println("log_lvl must be one of e w i d");
    }
  } else if (key == EspBaseKeys::Hostname) {
    if (!validHostname(value)) {
      out.println("hostname must be 1..32 letters, digits or '-', not starting or ending with '-'");
    } else if (_store.setString(key.c_str(), value)) {
      out.println("hostname saved (takes effect after reboot)");
    } else {
      out.println("write failed");
    }
  } else if (key == EspBaseKeys::ApPass || key == EspBaseKeys::WebPass) {
    if (!value.isEmpty() && (value.length() < 8 || value.length() > 63)) {
      out.printf("%s must be empty or 8..63 chars\r\n", key.c_str());
    } else if (_store.setString(key.c_str(), value)) {
      out.printf("%s saved (takes effect after reboot)\r\n", key.c_str());
    } else {
      out.println("write failed");
    }
  } else {
    // wifi_mode, timeouts and network slots: validated and applied live by WifiManager
    String err;
    if (_wifi.applySetting(key.c_str(), value, err)) {
      out.printf("%s saved%s\r\n", key.c_str(), slotOf(key.c_str()) > 0 ? " (use \"wifi reconnect\" or \"wifi set\" to connect now)" : "");
    } else {
      out.println(err);
    }
  }
}
