#ifndef ESPBASE_NO_OTA

#include "OtaUpdater.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Update.h>

#include "Log.h"
#include "WebConsole.h"

namespace {
constexpr uint16_t kOtaPort = 3232;
constexpr uint32_t kRebootDelayMs = 1000;

const char* otaErrorName(ota_error_t err) {
  switch (err) {
    case OTA_AUTH_ERROR: return "auth";
    case OTA_BEGIN_ERROR: return "begin";
    case OTA_CONNECT_ERROR: return "connect";
    case OTA_RECEIVE_ERROR: return "receive";
    case OTA_END_ERROR: return "end";
    default: return "unknown";
  }
}
}  // namespace

void OtaUpdater::begin(const EspBaseConfig& cfg, WebConsole& console, const String& hostname) {
  _console = &console;
  _password = cfg.otaPassword;
  _enabled = cfg.otaEnabled;
  if (!_enabled) {
    LOG_I("ota: disabled");
    return;
  }

  ArduinoOTA.setPort(kOtaPort);
  ArduinoOTA.setHostname(hostname.c_str());
  ArduinoOTA.setMdnsEnabled(false);  // mDNS is owned by WifiManager; the service record is added in onNetworkUp()
  ArduinoOTA.setRebootOnSuccess(true);
  if (!_password.isEmpty()) {
    ArduinoOTA.setPassword(_password.c_str());
  }
  ArduinoOTA.onStart([this]() {
    _otaActive = true;
    _lastPct = 0;
    LOG_I("ota: espota %s update started", ArduinoOTA.getCommand() == U_FLASH ? "firmware" : "filesystem");
  });
  ArduinoOTA.onProgress([this](unsigned int done, unsigned int total) {
    if (total == 0) {
      return;
    }
    const uint8_t pct = static_cast<uint8_t>((static_cast<uint64_t>(done) * 100U) / total);
    if (pct >= _lastPct + 10 || pct == 100) {
      _lastPct = pct;
      LOG_I("ota: %u%% (%u/%u)", pct, done, total);
    }
  });
  ArduinoOTA.onEnd([this]() {
    _otaActive = false;
    LOG_I("ota: espota update complete, rebooting");
  });
  ArduinoOTA.onError([this](ota_error_t err) {
    _otaActive = false;
    LOG_E("ota: espota error %d (%s)", static_cast<int>(err), otaErrorName(err));
  });

  console.server().on(
      "/update", HTTP_POST, [this](AsyncWebServerRequest* request) { handleUploadDone(request); },
      [this](AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
        handleUploadChunk(request, filename, index, data, len, final);
      });

  LOG_I("ota: enabled (espota port %u, POST /update)", kOtaPort);
  if (_password.isEmpty()) {
    LOG_W("ota: no password, anyone on the network can flash firmware (espota%s)", console.authRequired() ? "" : " and POST /update");
  }
}

void OtaUpdater::onNetworkUp(bool mdnsRunning) {
  if (!_enabled) {
    return;
  }
  if (!_arduinoOtaStarted) {
    ArduinoOTA.begin();  // binds UDP 3232 on every interface, AP included
    _arduinoOtaStarted = true;
    LOG_D("ota: espota listening on port %u", kOtaPort);
  }
  if (mdnsRunning && !_mdnsAdvertised) {
    MDNS.enableArduino(kOtaPort, !_password.isEmpty());  // _arduino._tcp for espota / IDE discovery
    _mdnsAdvertised = true;
  }
}

void OtaUpdater::loop() {
  if (!_enabled) {
    return;
  }
  if (_arduinoOtaStarted) {
    ArduinoOTA.handle();
  }
  if (_rebootAt != 0 && static_cast<int32_t>(millis() - _rebootAt) >= 0) {
    _rebootAt = 0;
    LOG_I("ota: rebooting into new firmware");
    delay(100);  // let the serial sink drain; nothing else is pending at this point
    ESP.restart();
  }
}

// Same origin rules as the console. With a console password that password
// protects uploads; otherwise the OTA password does (HTTP basic auth).
bool OtaUpdater::authorized(AsyncWebServerRequest* request) {
  if (!_console->trustedOrigin(request)) {
    return false;
  }
  if (_console->authRequired()) {
    return _console->authenticated(request);
  }
  return _password.isEmpty() || request->authenticate(WebConsole::kUser, _password.c_str(), WebConsole::kRealm);
}

void OtaUpdater::handleUploadChunk(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
  // Runs on the async_tcp task before any middleware, so the checks happen
  // here, before the first byte reaches flash. Only one request (the owner)
  // may write; chunks of any other request are ignored.
  if (index == 0) {
    if (_owner != nullptr || _otaActive) {
      return;  // busy: the done handler answers 409
    }
    if (!authorized(request)) {
      if (request->authType() != AsyncAuthType::AUTH_NONE) {
        LOG_W("ota: web upload from %s rejected (bad credentials)", request->client()->remoteIP().toString().c_str());
      }
      return;
    }
    _owner = request;
    _webOk = false;
    _webError = "";
    _lastPct = 0;
    request->onDisconnect([this, request]() {
      if (_owner == request) {  // the client went away before the done handler ran
        if (Update.isRunning()) {
          Update.abort();
        }
        _owner = nullptr;
        LOG_W("ota: web upload aborted, client disconnected");
      }
    });
    LOG_I("ota: web upload \"%s\" from %s", filename.c_str(), request->client()->remoteIP().toString().c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      _webError = Update.errorString();
      LOG_E("ota: begin failed: %s", _webError.c_str());
      return;
    }
  }
  if (request != _owner || !_webError.isEmpty()) {
    return;
  }
  if (len > 0 && Update.write(data, len) != len) {
    _webError = Update.errorString();
    Update.abort();
    LOG_E("ota: write failed: %s", _webError.c_str());
    return;
  }
  const size_t total = request->contentLength();
  if (total > 0) {
    const uint8_t pct = static_cast<uint8_t>((static_cast<uint64_t>(index + len) * 100U) / total);
    if (pct >= _lastPct + 10) {
      _lastPct = pct;
      LOG_I("ota: %u%%", pct);
    }
  }
  if (final) {
    if (Update.end(true)) {
      _webOk = true;
      LOG_I("ota: web upload complete, %u bytes", static_cast<unsigned>(index + len));
    } else {
      _webError = Update.errorString();
      LOG_E("ota: end failed: %s", _webError.c_str());
    }
  }
}

void OtaUpdater::handleUploadDone(AsyncWebServerRequest* request) {
  if (request != _owner) {
    // Foreign origins and missing console credentials were refused by the
    // console middleware already; what is left is the OTA password case.
    if (!authorized(request)) {
      request->requestAuthentication(AsyncAuthType::AUTH_BASIC, WebConsole::kRealm, "Authentication required");
    } else if (_owner != nullptr || _otaActive) {
      request->send(409, "application/json", "{\"ok\":false,\"error\":\"another update is in progress\"}");
    } else {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"no firmware received\"}");
    }
    return;
  }
  _owner = nullptr;
  if (_webOk) {
    _webOk = false;
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    response->addHeader("Connection", "close");
    request->send(response);
    _rebootAt = millis() + kRebootDelayMs;
    return;
  }
  String body = "{\"ok\":false,\"error\":\"";
  body += _webError.isEmpty() ? "incomplete upload" : _webError;  // Update.errorString() values are fixed ASCII
  body += "\"}";
  request->send(500, "application/json", body);
}

#endif  // ESPBASE_NO_OTA
