// OTA: ArduinoOTA (espota) plus a browser/curl upload endpoint at POST /update.
// Compiled out entirely with -DESPBASE_NO_OTA.
#pragma once

#ifndef ESPBASE_NO_OTA

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "EspBaseConfig.h"

class WebConsole;

class OtaUpdater {
 public:
  void begin(const EspBaseConfig& cfg, WebConsole& console, const String& hostname);
  // Call when the network is up (STA connected or AP started); idempotent.
  // mdnsRunning: whether MDNS.begin() has been called, so the _arduino._tcp
  // service can be advertised for espota discovery.
  void onNetworkUp(bool mdnsRunning);
  void loop();

  bool enabled() const { return _enabled; }
  bool inProgress() const { return _otaActive || _owner != nullptr; }

 private:
  bool authorized(AsyncWebServerRequest* request);
  void handleUploadChunk(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final);
  void handleUploadDone(AsyncWebServerRequest* request);

  WebConsole* _console = nullptr;
  String _password;
  bool _enabled = false;
  bool _arduinoOtaStarted = false;
  bool _mdnsAdvertised = false;
  bool _otaActive = false;                  // espota transfer running (loop task)
  AsyncWebServerRequest* _owner = nullptr;  // the one web upload allowed to write (async_tcp task)
  bool _webOk = false;
  String _webError;
  uint32_t _rebootAt = 0;
  uint8_t _lastPct = 0;
};

#endif  // ESPBASE_NO_OTA
