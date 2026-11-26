#include "WebConsole.h"

#include <AsyncJson.h>
#include <esp_arduino_version.h>
#include <esp_random.h>
#include <esp_system.h>
#include <string.h>

#include "ChipInfo.h"
#include "DeviceStatus.h"
#include "WebGuard.h"
#include "ProjectSettings.h"
#include "WifiManager.h"
#include "web/index_html_gz.h"

const String CmdArgs::_empty;

namespace {
constexpr size_t kMaxWsMessage = 512;
constexpr uint32_t kCleanupIntervalMs = 1000;
constexpr int kMaxFramesPerLoop = 4;
constexpr size_t kMaxJsonBody = 1024;        // API request bodies
// POST /api/settings: all 24 settings at 255 bytes, keys included, are about
// 6.6 KB. The server allocates the body before the login check runs.
constexpr size_t kMaxSettingsBody = 8192;
constexpr uint32_t kTicketMs = 30000;        // WebSocket ticket lifetime
constexpr uint8_t kMaxAuthFailures = 5;      // then logins are refused for kLockoutMs
constexpr uint32_t kLockoutMs = 30000;

const char* kindName(SettingKind kind) {
  switch (kind) {
    case SettingKind::Secret: return "secret";
    case SettingKind::Choice: return "choice";
    case SettingKind::Number: return "number";
    case SettingKind::Bool: return "bool";
    default: return "text";
  }
}

const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt wdt";
    case ESP_RST_TASK_WDT: return "task wdt";
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    default: return "other";
  }
}

const char* const kCaptiveProbes[] = {
    "/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html", "/connecttest.txt", "/ncsi.txt",
    "/redirect", "/canonical.html", "/success.txt", "/check_network_status.txt",
};
}  // namespace

// ---- CmdArgs ---------------------------------------------------------------

bool CmdArgs::parse(const String& line, String& cmd, CmdArgs& args) {
  args._n = 0;
  args._raw = "";
  cmd = "";
  const size_t n = line.length();
  size_t i = 0;
  auto skipSpace = [&]() {
    while (i < n && isspace(static_cast<unsigned char>(line[i]))) {
      i++;
    }
  };
  auto token = [&](String& out) -> bool {
    skipSpace();
    if (i >= n) {
      return false;
    }
    out = "";
    if (line[i] == '"' || line[i] == '\'') {
      const char q = line[i++];
      while (i < n && line[i] != q) {
        if (line[i] == '\\' && i + 1 < n) {
          i++;
        }
        out += line[i++];
      }
      if (i < n) {
        i++;  // closing quote
      }
    } else {
      while (i < n && !isspace(static_cast<unsigned char>(line[i]))) {
        out += line[i++];
      }
    }
    return true;
  };
  if (!token(cmd)) {
    return false;
  }
  skipSpace();
  args._raw = line.substring(i);
  args._raw.trim();
  String t;
  while (args._n < kMax && token(t)) {
    args._v[args._n++] = t;
  }
  return true;
}

long CmdArgs::toInt(size_t i, long def) const {
  if (i >= _n || _v[i].isEmpty()) {
    return def;
  }
  char* end = nullptr;
  const long v = strtol(_v[i].c_str(), &end, 0);
  return (end != nullptr && *end == '\0') ? v : def;
}

// ---- WebConsole -------------------------------------------------------------

WebConsole::WebConsole() : _ws("/ws") {}

void WebConsole::begin(const EspBaseConfig& cfg, WifiManager& wifi, const String& password) {
  _cfg = &cfg;
  _wifi = &wifi;
  _password = password;
  if (_wsEvents == nullptr) {
    _wsEvents = xQueueCreate(16, sizeof(WsEvent));
  }
  if (_pendingMutex == nullptr) {
    _pendingMutex = xSemaphoreCreateMutex();
  }
  if (_server == nullptr) {
    _server = new AsyncWebServer(cfg.httpPort);
  }
  _ws.onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
    onWsEvent(server, client, type, arg, data, len);
  });
  _ws.handleHandshake([this](AsyncWebServerRequest* request) { return consumeTicket(request); });

  DefaultHeaders::Instance().addHeader("X-Content-Type-Options", "nosniff");
  DefaultHeaders::Instance().addHeader("X-Frame-Options", "DENY");
  DefaultHeaders::Instance().addHeader("Referrer-Policy", "no-referrer");

  // Runs before every handler. Upload bodies arrive before middleware runs,
  // so OtaUpdater repeats these checks itself.
  _server->addMiddleware([this](AsyncWebServerRequest* request, ArMiddlewareNext next) {
    const String& url = request->url();
    const bool api = WebGuard::guardedPath(url.c_str());
    if (api && !trustedOrigin(request)) {
      LOG_W("web: refused %s from %s (cross-origin or foreign host \"%s\")", url.c_str(), request->client()->remoteIP().toString().c_str(),
            request->host().c_str());
      request->send(403, "application/json", "{\"error\":\"cross-origin request refused\"}");
      return;
    }
    // The WebSocket authenticates with a ticket instead (consumeTicket()).
    if ((api || WebGuard::underPath(url.c_str(), _consolePath.c_str())) && url != "/ws" && !authenticated(request)) {
      requestAuth(request);
      return;
    }
    next();
  });

  _appLink = !cfg.appPage.isEmpty() && WebGuard::appPath(cfg.appPage.c_str());
  if (!cfg.appPage.isEmpty() && !_appLink) {
    LOG_E("web: appPage \"%s\" refused: the console login only covers /app/", cfg.appPage.c_str());
  }

  // The page moves or goes, the API and /ws stay: a page of the project's own
  // at "/" uses them, and the captive portal opens on "/". A '*' would make
  // ESPAsyncWebServer match more paths than the login check covers.
  _consolePath = cfg.consolePath;
  if (!_consolePath.isEmpty() &&
      (!_consolePath.startsWith("/") || _consolePath.indexOf('*') >= 0 || WebGuard::guardedPath(_consolePath.c_str()))) {
    LOG_E("web: consolePath \"%s\" refused: it must start with /, have no *, and stay out of /api/, /app/, /ws and /update",
          _consolePath.c_str());
    _consolePath = "";
  }

  setupRoutes();
  _server->addHandler(&_ws);
  _server->begin();
  if (_consolePath.isEmpty()) {
    LOG_I("web: no console page; API and ws at /ws on port %u", cfg.httpPort);
  } else {
    LOG_I("web: console at %s on port %u (page %u bytes gzipped), ws at /ws", _consolePath.c_str(), cfg.httpPort,
          static_cast<unsigned>(INDEX_HTML_GZ_LEN));
  }
  if (!authRequired()) {
    LOG_W("web: no console password, anyone on the network can control this device (cfg.webPassword or \"config set web_pass\")");
  } else if (!EspBaseKeys::validPassword(_password)) {
    LOG_W("web: the console password should be 8..63 chars");
  }
}

void WebConsole::setupRoutes() {
  AsyncWebServer& s = *_server;

  if (!_consolePath.isEmpty()) {
    s.on(_consolePath.c_str(), HTTP_GET, [](AsyncWebServerRequest* request) {
      const AsyncWebHeader* inm = request->getHeader("If-None-Match");
      if (inm != nullptr && inm->value() == INDEX_HTML_ETAG) {
        request->send(304);
        return;
      }
      AsyncWebServerResponse* response = request->beginResponse(200, "text/html", INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
      response->addHeader("Content-Encoding", "gzip");
      response->addHeader("Cache-Control", "no-cache");
      response->addHeader("ETag", INDEX_HTML_ETAG);
      response->addHeader("Content-Security-Policy", INDEX_HTML_CSP);
      request->send(response);
    });
  }

  // The project's own settings (ProjectSettings.h). A Secret never carries a value.
  s.on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest* request) {
    AsyncJsonResponse* response = new AsyncJsonResponse();
    JsonArray list = response->getRoot().to<JsonObject>()["settings"].to<JsonArray>();
    for (size_t i = 0; _settings != nullptr && i < _settings->count(); ++i) {
      const SettingDef& d = *_settings->at(i);
      JsonObject e = list.add<JsonObject>();
      e["key"] = d.key;
      e["label"] = d.label;
      e["kind"] = kindName(d.kind);
      if (d.kind != SettingKind::Secret) {
        e["value"] = _settings->get(d.key);
      }
      e["set"] = _settings->isSet(d.key);
      if (d.kind == SettingKind::Choice) {
        JsonArray choices = e["choices"].to<JsonArray>();
        for (const char* p = d.choices; *p != '\0';) {
          const char* end = strchr(p, ',');
          const size_t n = end != nullptr ? static_cast<size_t>(end - p) : strlen(p);
          String item;
          item.concat(p, n);
          choices.add(item);
          if (end == nullptr) {
            break;
          }
          p = end + 1;
        }
      }
      if (*d.hint != '\0') {
        e["hint"] = d.hint;
      }
      e["maxLen"] = d.maxLen;
      if (d.kind == SettingKind::Number) {
        e["min"] = d.min;
        e["max"] = d.max;
        e["decimals"] = d.decimals;
      }
    }
    response->setLength();
    request->send(response);
  });

  // {"<key>": "<value>" | <number> | true | false | null, ...}: one bad pair
  // refuses the whole request. A number or true/false counts as its JSON text,
  // so a page of the project's own can post {"limit": 21.5, "auto": true}.
  // null clears a value; "" clears a Text or Choice and leaves a Secret as it
  // is (the form sends an untouched secret field empty). This runs on the web
  // server's task: it only checks, then hands the changes to the loop task,
  // which writes them after this answer (ProjectSettings::loop()).
  s.on("/api/settings", HTTP_POST, [this](AsyncWebServerRequest* request, JsonVariant& json) {
    const char* keys[ProjectSettings::kMaxSettings];
    String values[ProjectSettings::kMaxSettings];
    bool nulls[ProjectSettings::kMaxSettings];
    size_t n = 0;
    String err;
    if (_settings == nullptr || !json.is<JsonObject>()) {
      err = "an object of settings is required";
    } else {
      for (JsonPair p : json.as<JsonObject>()) {
        const SettingDef* def = _settings->find(p.key().c_str());
        if (def == nullptr || n >= ProjectSettings::kMaxSettings) {
          err = String(p.key().c_str()) + ": unknown setting";
          break;
        }
        const bool isNull = p.value().isNull();
        String value;
        if (p.value().is<const char*>()) {
          value = p.value().as<const char*>();
        } else if (p.value().is<bool>()) {
          value = p.value().as<bool>() ? "true" : "false";
        } else if (p.value().is<double>()) {
          serializeJson(p.value(), value);
        } else if (!isNull) {
          err = String(def->key) + ": must be text, a number, true or false";
          break;
        }
        if (!isNull && value.isEmpty() && def->kind == SettingKind::Secret) {
          continue;
        }
        const bool clear = isNull || value.isEmpty();
        if (!clear && !_settings->check(def->key, value, err)) {
          break;
        }
        keys[n] = def->key;
        values[n] = value;
        nulls[n] = clear;
        ++n;
      }
    }
    if (!err.isEmpty()) {
      JsonDocument doc;
      doc["ok"] = false;
      doc["error"] = err;
      String body;
      serializeJson(doc, body);
      request->send(400, "application/json", body);
    } else if (n > 0 && !_settings->queue(keys, values, nulls, n)) {
      request->send(503, "application/json", "{\"ok\":false,\"error\":\"busy\"}");
    } else {
      request->send(200, "application/json", "{\"ok\":true}");
    }
  }).setMaxContentLength(kMaxSettingsBody);

  s.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
    AsyncJsonResponse* response = new AsyncJsonResponse();
    JsonObject root = response->getRoot().to<JsonObject>();
    if (_status != nullptr) {
      _status->writeJson(root);
    }
    response->setLength();
    request->send(response);
  });

  s.on("/api/info", HTTP_GET, [this](AsyncWebServerRequest* request) {
    AsyncJsonResponse* response = new AsyncJsonResponse();
    writeInfo(response->getRoot().to<JsonObject>());
    response->setLength();
    request->send(response);
  });

  s.on("/api/log", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncResponseStream* stream = request->beginResponseStream("text/plain");
    Log.dump(*stream);
    request->send(stream);
  });

  s.on("/api/ws-ticket", HTTP_GET, [this](AsyncWebServerRequest* request) {
    String body = "{\"ticket\":\"";
    body += issueTicket();
    body += "\"}";
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", body);
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  });

  s.on("/api/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest* request) {
    String body = "{\"scanning\":";
    body += _wifi->scanRunning() ? "true" : "false";
    body += ",\"age\":";
    body += _wifi->scanCompletedAt() ? static_cast<long>((millis() - _wifi->scanCompletedAt()) / 1000) : -1L;
    body += ",\"networks\":";
    const String nets = _wifi->scanResultJson();
    body += nets.isEmpty() ? "[]" : nets;
    body += '}';
    request->send(200, "application/json", body);
  });

  s.on("/api/wifi/scan", HTTP_POST, [this](AsyncWebServerRequest* request) {
    const bool ok = _wifi->startScan();
    request->send(ok ? 202 : 409, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"scan busy\"}");
  });

  s.on("/api/wifi", HTTP_GET, [this](AsyncWebServerRequest* request) {
    AsyncJsonResponse* response = new AsyncJsonResponse();
    JsonObject o = response->getRoot().to<JsonObject>();
    writeWifiStatus(o);
    const WifiManager::Settings st = _wifi->settings();
    JsonObject settings = o["settings"].to<JsonObject>();
    settings["staTimeoutMs"] = st.staTimeoutMs;
    settings["reconnectTimeoutMs"] = st.reconnectTimeoutMs;
    settings["apRetryIntervalMs"] = st.apRetryIntervalMs;
    o["maxSlots"] = WifiManager::kMaxSlots;
    JsonArray nets = o["networks"].to<JsonArray>();
    WifiManager::Slot slots[WifiManager::kMaxSlots];
    const size_t n = _wifi->listCredentials(slots, WifiManager::kMaxSlots);
    for (size_t i = 0; i < n; ++i) {
      JsonObject e = nets.add<JsonObject>();
      e["slot"] = slots[i].slot;
      e["ssid"] = slots[i].ssid;
      e["hasPassword"] = slots[i].hasPassword;
    }
    response->setLength();
    request->send(response);
  });

  // {"ssid","pass","slot"?,"connect"?}: store (slot 1 by default) and connect;
  // "connect":false stores only (first free slot when no slot is given);
  // {"slot":n} alone starts a round at that stored network.
  s.on("/api/wifi", HTTP_POST, [this](AsyncWebServerRequest* request, JsonVariant& json) {
    const char* ssid = json["ssid"] | "";
    const char* pass = json["pass"] | "";
    const int slot = json["slot"] | 0;
    const bool connect = json["connect"] | true;
    bool ok = false;
    String result = "{\"ok\":true}";
    if (*ssid != '\0') {
      if (slot < 0 || slot > WifiManager::kMaxSlots) {
        ok = false;
      } else if (connect) {
        ok = _wifi->setCredentials(ssid, pass, static_cast<uint8_t>(slot > 0 ? slot : 1));
      } else if (slot > 0) {
        ok = _wifi->storeCredentials(ssid, pass, static_cast<uint8_t>(slot));
      } else {
        const uint8_t used = _wifi->addCredentials(ssid, pass);
        ok = used > 0;
        result = String("{\"ok\":true,\"slot\":") + used + '}';
      }
      if (!ok) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"ssid 1..32 chars, password empty or 8..63 chars, and a free slot\"}");
        return;
      }
    } else if (slot >= 1 && slot <= WifiManager::kMaxSlots) {
      _wifi->reconnect(static_cast<uint8_t>(slot));
    } else {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"ssid or slot required\"}");
      return;
    }
    request->send(200, "application/json", result);
  }).setMaxContentLength(kMaxJsonBody);

  s.on("/api/wifi", HTTP_DELETE, [this](AsyncWebServerRequest* request) {
    const AsyncWebParameter* p = request->getParam("slot");
    const int slot = p ? p->value().toInt() : 0;
    if (slot >= 1 && slot <= WifiManager::kMaxSlots && _wifi->removeCredentials(static_cast<uint8_t>(slot))) {
      request->send(200, "application/json", "{\"ok\":true}");
    } else {
      request->send(400, "application/json", String("{\"ok\":false,\"error\":\"slot 1..") + WifiManager::kMaxSlots + " required\"}");
    }
  });

  // {"mode"?,"staTimeoutMs"?,"reconnectTimeoutMs"?,"apRetryIntervalMs"?}
  s.on("/api/wifi/settings", HTTP_POST, [this](AsyncWebServerRequest* request, JsonVariant& json) {
    struct Field {
      const char* field;
      const char* key;
    };
    static const Field fields[] = {{"mode", EspBaseKeys::WifiModeKey},
                                   {"staTimeoutMs", EspBaseKeys::StaTimeout},
                                   {"reconnectTimeoutMs", EspBaseKeys::ReconnTimeout},
                                   {"apRetryIntervalMs", EspBaseKeys::ApRetry}};
    String err;
    for (const Field& f : fields) {
      if (json[f.field].isNull()) {
        continue;
      }
      const String value = json[f.field].as<String>();
      if (!_wifi->applySetting(f.key, value, err)) {
        String body = "{\"ok\":false,\"error\":";
        appendJsonString(body, err.c_str());
        body += '}';
        request->send(400, "application/json", body);
        return;
      }
    }
    request->send(200, "application/json", "{\"ok\":true}");
  }).setMaxContentLength(kMaxJsonBody);

  for (const char* probe : kCaptiveProbes) {
    s.on(probe, HTTP_ANY, [this](AsyncWebServerRequest* request) {
      if (isCaptiveRequest(request)) {
        sendCaptiveRedirect(request);
      } else {
        request->send(404, "text/plain", "not found");
      }
    });
  }

  s.onNotFound([this](AsyncWebServerRequest* request) {
    if (isCaptiveRequest(request)) {
      sendCaptiveRedirect(request);
      return;
    }
    request->send(404, "application/json", "{\"error\":\"not found\"}");
  });
}

bool WebConsole::isCaptiveRequest(AsyncWebServerRequest* request) const {
  if (_wifi == nullptr || !_wifi->apActive()) {
    return false;
  }
  const IPAddress apIp = _wifi->apIP();
  if (request->client()->localIP() != apIp) {
    return false;  // arrived via the STA interface
  }
  return request->host() != apIp.toString();  // requests addressed to the AP IP itself are served normally
}

void WebConsole::sendCaptiveRedirect(AsyncWebServerRequest* request) {
  String url = "http://";
  url += _wifi->apIP().toString();
  url += '/';
  request->redirect(url);
}

// ---- WebSocket --------------------------------------------------------------

void WebConsole::onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
  // Runs on the async_tcp task while the server holds its client lock: only
  // queue work here, never call back into the server.
  (void)server;
  switch (type) {
    case WS_EVT_CONNECT:
    case WS_EVT_DISCONNECT: {
      WsEvent e{static_cast<uint8_t>(type), client->id()};
      if (_wsEvents != nullptr) {
        xQueueSend(_wsEvents, &e, 0);
      }
      break;
    }
    case WS_EVT_DATA: {
      const AwsFrameInfo* info = static_cast<AwsFrameInfo*>(arg);
      if (!(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)) {
        return;  // fragmented or binary frames are not part of the protocol
      }
      if (len == 0 || len > kMaxWsMessage) {
        return;
      }
      JsonDocument doc;
      if (deserializeJson(doc, data, len) != DeserializationError::Ok) {
        return;
      }
      const char* t = doc["t"] | "";
      const char* c = doc["c"] | "";
      if (strcmp(t, "cmd") != 0 || *c == '\0') {
        return;
      }
      if (_pendingMutex != nullptr && xSemaphoreTake(_pendingMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (_pending.size() < kMaxPending) {
          _pending.push_back(PendingCmd{client->id(), String(c)});
        }
        xSemaphoreGive(_pendingMutex);
      }
      break;
    }
    default:
      break;
  }
}

void WebConsole::handleWsEvent(const WsEvent& e) {
  if (e.type == WS_EVT_CONNECT) {
    Client* slot = nullptr;
    for (auto& c : _clients) {
      if (c.active && c.id == e.id) {
        slot = &c;
        break;
      }
      if (!c.active && slot == nullptr) {
        slot = &c;
      }
    }
    if (slot == nullptr) {
      LOG_W("web: no slot for ws client %lu", static_cast<unsigned long>(e.id));
      return;
    }
    slot->active = true;
    slot->id = e.id;
    slot->cursor = Log.tail();  // replay the ring buffer first
    LOG_I("web: console client %lu connected (%u online)", static_cast<unsigned long>(e.id), static_cast<unsigned>(_ws.count()));
  } else if (e.type == WS_EVT_DISCONNECT) {
    for (auto& c : _clients) {
      if (c.active && c.id == e.id) {
        c.active = false;
      }
    }
    LOG_I("web: console client %lu disconnected", static_cast<unsigned long>(e.id));
  }
}

void WebConsole::loop() {
  WsEvent e;
  while (_wsEvents != nullptr && xQueueReceive(_wsEvents, &e, 0) == pdTRUE) {
    handleWsEvent(e);
  }
  runPendingCommands();
  streamLogs();
  const uint32_t now = millis();
  if (now - _lastCleanup >= kCleanupIntervalMs) {
    _lastCleanup = now;
    const uint16_t limit = (_cfg->maxWsClients == 0 || _cfg->maxWsClients > kMaxTracked) ? kMaxTracked : _cfg->maxWsClients;
    _ws.cleanupClients(limit);  // frees closed clients, drops the oldest beyond the limit
  }
}

void WebConsole::runPendingCommands() {
  for (;;) {
    PendingCmd cmd;
    bool have = false;
    if (_pendingMutex != nullptr && xSemaphoreTake(_pendingMutex, portMAX_DELAY) == pdTRUE) {
      if (!_pending.empty()) {
        cmd = std::move(_pending.front());
        _pending.pop_front();
        have = true;
      }
      xSemaphoreGive(_pendingMutex);
    }
    if (!have) {
      return;
    }
    const int space = cmd.line.indexOf(' ');  // log the command name only: arguments may be passwords
    LOG_D("web: [%lu] > %s", static_cast<unsigned long>(cmd.id), (space < 0 ? cmd.line : cmd.line.substring(0, space)).c_str());
    StringPrint out;
    execute(cmd.line, out);
    String frame;
    frame.reserve(out.buf.length() + 24);
    frame += "{\"t\":\"out\",\"m\":";
    appendJsonString(frame, out.buf.c_str());
    frame += '}';
    if (_ws.hasClient(cmd.id)) {
      _ws.text(cmd.id, frame);
    }
  }
}

void WebConsole::streamLogs() {
  for (auto& c : _clients) {
    if (!c.active) {
      continue;
    }
    for (int frames = 0; frames < kMaxFramesPerLoop && _ws.availableForWrite(c.id); ++frames) {
      String frame;
      frame.reserve(kFrameBudget + 128);
      frame += '[';
      auto separate = [&frame]() {
        if (frame.length() > 1) {
          frame += ',';
        }
      };
      LogClass::Record r;
      uint32_t dropped = 0;
      while (frame.length() < kFrameBudget && Log.read(c.cursor, r, &dropped)) {
        if (dropped > 0) {
          separate();
          frame += "{\"t\":\"log\",\"l\":\"W\",\"ms\":";
          frame += millis();
          frame += ",\"m\":\"... ";
          frame += dropped;
          frame += " log lines dropped (console client too slow)\"}";
        }
        separate();
        appendLogJson(frame, r);
      }
      if (frame.length() == 1) {
        break;  // nothing new
      }
      frame += ']';
      if (!_ws.text(c.id, frame)) {
        break;
      }
    }
  }
}

void WebConsole::appendJsonString(String& out, const char* s) {
  out += '"';
  const auto* p = reinterpret_cast<const uint8_t*>(s);
  while (*p != 0) {
    const uint8_t c = *p;
    if (c >= 0x80) {
      // SSIDs are arbitrary bytes, and browsers drop a WebSocket that carries invalid UTF-8.
      const size_t n = WebGuard::utf8Length(p);
      if (n == 0) {
        out += "\\ufffd";
        p++;
      } else {
        out.concat(reinterpret_cast<const char*>(p), n);
        p += n;
      }
      continue;
    }
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20 || c == 0x7F) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
    p++;
  }
  out += '"';
}

void WebConsole::appendLogJson(String& out, const LogClass::Record& r) {
  out += "{\"t\":\"log\",\"l\":\"";
  out += LogClass::levelChar(r.level);
  out += "\",\"ms\":";
  out += r.ms;
  out += ",\"m\":";
  appendJsonString(out, r.msg);
  out += '}';
}

// ---- Commands ----------------------------------------------------------------

void WebConsole::addCommand(const char* name, CmdHandler handler, const char* help) {
  for (auto& c : _commands) {
    if (c.name.equalsIgnoreCase(name)) {
      c.handler = std::move(handler);
      c.help = help ? help : "";
      return;
    }
  }
  _commands.push_back(Command{String(name), String(help ? help : ""), std::move(handler)});
}

bool WebConsole::execute(const String& line, Print& out) {
  String name;
  CmdArgs args;
  if (!CmdArgs::parse(line, name, args)) {
    return true;  // empty line
  }
  for (auto& c : _commands) {
    if (c.name.equalsIgnoreCase(name)) {
      c.handler(args, out);
      return true;
    }
  }
  out.printf("unknown command \"%s\" (try: help)\r\n", name.c_str());
  return false;
}

void WebConsole::printHelp(Print& out) {
  std::vector<const Command*> sorted;
  sorted.reserve(_commands.size());
  for (auto& c : _commands) {
    sorted.push_back(&c);
  }
  std::sort(sorted.begin(), sorted.end(), [](const Command* a, const Command* b) { return strcmp(a->name.c_str(), b->name.c_str()) < 0; });
  for (const Command* c : sorted) {
    out.printf("  %-14s %s\r\n", c->name.c_str(), c->help.c_str());
  }
}

// ---- Info ---------------------------------------------------------------------

void WebConsole::writeInfo(JsonObject o) {
  o["chip"] = ESP.getChipModel();
  o["target"] = ChipInfo::name();
  o["rev"] = ESP.getChipRevision();
  o["cores"] = ChipInfo::cores();
  o["cpuMhz"] = ESP.getCpuFreqMHz();
  o["flash"] = ESP.getFlashChipSize();
  JsonObject heap = o["heap"].to<JsonObject>();
  heap["free"] = ESP.getFreeHeap();
  heap["min"] = ESP.getMinFreeHeap();
  heap["maxAlloc"] = ESP.getMaxAllocHeap();
  heap["total"] = ESP.getHeapSize();
  if (ESP.getPsramSize() > 0) {
    JsonObject psram = o["psram"].to<JsonObject>();
    psram["size"] = ESP.getPsramSize();
    psram["free"] = ESP.getFreePsram();
  }
  JsonObject sketch = o["sketch"].to<JsonObject>();
  sketch["size"] = ESP.getSketchSize();
  sketch["free"] = ESP.getFreeSketchSpace();
  o["uptime"] = millis() / 1000;
  o["hostname"] = _wifi->hostname();
  o["mac"] = _wifi->macAddress();
  writeWifiStatus(o["wifi"].to<JsonObject>());
  // The tab the page opens on while the device is its own access point.
  const WifiState state = _wifi->state();
  const String& landing = _cfg->apLandingTab;
  if ((state == WifiState::ApFallback || state == WifiState::ApOnly) &&
      (landing == "wifi" || (landing == "settings" && _settings != nullptr && _settings->count() > 0) ||
       (landing == "device" && _status != nullptr && !_status->empty()))) {
    o["landing"] = landing;
  }
  if (_appLink) {  // the project's own page, linked from the tab bar
    JsonObject app = o["app"].to<JsonObject>();
    app["path"] = _cfg->appPage;
    app["title"] = _cfg->appPageTitle.isEmpty() ? String("App") : _cfg->appPageTitle;
  }
  o["fw"] = _cfg->fwVersion;
  o["build"] = __DATE__ " " __TIME__;
  o["lib"] = ESPBASE_VERSION;
  o["core"] = ESP_ARDUINO_VERSION_STR;
  o["idf"] = ESP.getSdkVersion();
  o["reset"] = resetReasonName(esp_reset_reason());
#ifdef ESPBASE_NO_OTA
  o["ota"] = false;
#else
  o["ota"] = _cfg->otaEnabled;
#endif
  o["ws"] = _ws.count();
  o["log"] = LogClass::levelName(Log.level());
}

// Shared by GET /api/wifi and the "wifi" object of /api/info.
void WebConsole::writeWifiStatus(JsonObject o) {
  o["mode"] = WifiManager::modeName(_wifi->settings().mode);
  o["state"] = _wifi->stateName();
  o["ssid"] = _wifi->staSsid();
  o["ip"] = _wifi->isConnected() ? _wifi->localIP().toString() : String();
  o["rssi"] = _wifi->rssi();
  if (_wifi->apActive()) {
    JsonObject ap = o["ap"].to<JsonObject>();
    ap["ssid"] = _wifi->apSsid();
    ap["ip"] = _wifi->apIP().toString();
    ap["clients"] = _wifi->apClients();
  }
}

void WebConsole::printInfo(Print& out) {
  out.printf("chip:     %s rev %u, %d core(s) @ %lu MHz (%s)\r\n", ESP.getChipModel(), ESP.getChipRevision(), ChipInfo::cores(),
             static_cast<unsigned long>(ESP.getCpuFreqMHz()), ChipInfo::name());
  out.printf("flash:    %lu KB\r\n", static_cast<unsigned long>(ESP.getFlashChipSize() / 1024));
  out.printf("heap:     %lu KB free, %lu KB min, %lu KB largest block\r\n", static_cast<unsigned long>(ESP.getFreeHeap() / 1024),
             static_cast<unsigned long>(ESP.getMinFreeHeap() / 1024), static_cast<unsigned long>(ESP.getMaxAllocHeap() / 1024));
  if (ESP.getPsramSize() > 0) {
    out.printf("psram:    %lu KB free of %lu KB\r\n", static_cast<unsigned long>(ESP.getFreePsram() / 1024),
               static_cast<unsigned long>(ESP.getPsramSize() / 1024));
  }
  out.printf("sketch:   %lu KB used, %lu KB free\r\n", static_cast<unsigned long>(ESP.getSketchSize() / 1024),
             static_cast<unsigned long>(ESP.getFreeSketchSpace() / 1024));
  out.printf("uptime:   %lu s\r\n", static_cast<unsigned long>(millis() / 1000));
  out.printf("hostname: %s (%s)\r\n", _wifi->hostname().c_str(), _wifi->macAddress().c_str());
  if (_wifi->isConnected()) {
    out.printf("wifi:     %s, \"%s\", %s, %d dBm\r\n", _wifi->stateName(), _wifi->staSsid().c_str(), _wifi->localIP().toString().c_str(),
               _wifi->rssi());
  } else {
    out.printf("wifi:     %s\r\n", _wifi->stateName());
  }
  if (_wifi->apActive()) {
    out.printf("ap:       \"%s\" at %s, %u client(s)\r\n", _wifi->apSsid().c_str(), _wifi->apIP().toString().c_str(), _wifi->apClients());
  }
  out.printf("firmware: %s, built %s %s\r\n", _cfg->fwVersion, __DATE__, __TIME__);
  out.printf("versions: esp-base %s, arduino %s, idf %s\r\n", ESPBASE_VERSION, ESP_ARDUINO_VERSION_STR, ESP.getSdkVersion());
  out.printf("reset:    %s\r\n", resetReasonName(esp_reset_reason()));
#ifdef ESPBASE_NO_OTA
  out.println("ota:      compiled out");
#else
  out.printf("ota:      %s\r\n", _cfg->otaEnabled ? "enabled" : "disabled");
#endif
  out.printf("console:  %u ws client(s), log level %s\r\n", static_cast<unsigned>(_ws.count()), LogClass::levelName(Log.level()));
}

// ---- Request checks ------------------------------------------------------------

bool WebConsole::trustedOrigin(AsyncWebServerRequest* request) const {
  const String& host = request->host();
  if (!authRequired() && !WebGuard::hostAllowed(host.c_str(), _wifi->hostname().c_str())) {
    return false;  // a foreign name resolving to this device: DNS rebinding
  }
  const AsyncWebHeader* origin = request->getHeader("Origin");
  return WebGuard::originMatches(origin != nullptr ? origin->value().c_str() : nullptr, host.c_str());
}

bool WebConsole::authenticated(AsyncWebServerRequest* request) {
  if (!authRequired()) {
    return true;
  }
  const uint32_t now = millis();
  if (_lockedUntil != 0 && static_cast<int32_t>(now - _lockedUntil) < 0) {
    return false;
  }
  _lockedUntil = 0;
  if (request->authenticate(kUser, _password.c_str(), kRealm)) {
    _authFailures = 0;
    return true;
  }
  if (request->authType() != AsyncAuthType::AUTH_NONE && ++_authFailures >= kMaxAuthFailures) {
    _authFailures = 0;
    _lockedUntil = now + kLockoutMs;
    LOG_W("web: %u failed logins, refusing logins for %lu s", kMaxAuthFailures, static_cast<unsigned long>(kLockoutMs / 1000));
  }
  return false;
}

void WebConsole::requestAuth(AsyncWebServerRequest* request) {
  // Digest keeps the password off the wire; there is no TLS.
  request->requestAuthentication(AsyncAuthType::AUTH_DIGEST, kRealm, "Authentication required");
}

String WebConsole::issueTicket() {
  uint8_t raw[16];
  esp_fill_random(raw, sizeof(raw));
  Ticket* slot = &_tickets[0];  // reuse the slot that expires first
  for (Ticket& t : _tickets) {
    if (static_cast<int32_t>(t.expires - slot->expires) < 0) {
      slot = &t;
    }
  }
  for (size_t i = 0; i < sizeof(raw); ++i) {
    snprintf(slot->value + 2 * i, 3, "%02x", raw[i]);
  }
  slot->expires = millis() + kTicketMs;
  return String(slot->value);
}

bool WebConsole::consumeTicket(AsyncWebServerRequest* request) {
  const AsyncWebParameter* param = request->getParam("t");
  if (param != nullptr && param->value().length() == 32) {
    const uint32_t now = millis();
    for (Ticket& t : _tickets) {
      if (t.value[0] != '\0' && static_cast<int32_t>(t.expires - now) > 0 && param->value() == t.value) {
        t.value[0] = '\0';  // single use
        return true;
      }
    }
  }
  LOG_W("web: WebSocket from %s refused, missing or expired ticket", request->client()->remoteIP().toString().c_str());
  return false;
}
