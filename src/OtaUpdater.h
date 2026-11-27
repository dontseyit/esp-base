// OTA: ArduinoOTA (espota) plus, with the web console, a browser/curl upload
// endpoint at POST /update. Compiled out entirely with -DESPBASE_NO_OTA.
#pragma once

#ifndef ESPBASE_NO_OTA

#include <Arduino.h>
#ifndef ESPBASE_NO_WEB
#include <ESPAsyncWebServer.h>
#endif

#include "EspBaseConfig.h"

namespace espbase {

class WebConsole;

class OtaUpdater {
 public:
  void begin(const EspBaseConfig& cfg, const String& hostname);
#ifndef ESPBASE_NO_WEB
  // Registers POST /update on the console's server; its password protects the uploads.
  void serveUploads(WebConsole& web);
#endif
  // Call when the network is up (STA connected or AP started); idempotent.
  // mdnsRunning: whether MDNS.begin() has been called, so the _arduino._tcp
  // service can be advertised for espota discovery.
  void onNetworkUp(bool mdnsRunning);
  void loop();

  bool enabled() const { return _enabled; }
  bool inProgress() const { return _otaActive || _owner != nullptr; }

 private:
  String _password;
  bool _enabled = false;
  bool _arduinoOtaStarted = false;
  bool _mdnsAdvertised = false;
  bool _otaActive = false;  // espota transfer running (loop task)
  uint32_t _rebootAt = 0;
  uint8_t _lastPct = 0;
  void* _owner = nullptr;  // the one web upload allowed to write (async_tcp task); void* so the type exists without the web stack
#ifndef ESPBASE_NO_WEB
  bool authorized(AsyncWebServerRequest* request);
  void handleUploadChunk(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final);
  void handleUploadDone(AsyncWebServerRequest* request);
  WebConsole* _web = nullptr;
  bool _webOk = false;
  String _webError;
#endif
};

}  // namespace espbase

#endif  // ESPBASE_NO_OTA
