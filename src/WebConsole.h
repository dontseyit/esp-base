// Browser console: AsyncWebServer on cfg.httpPort, AsyncWebSocket at /ws,
// live log streaming, the /api routes, the captive portal handlers and the
// embedded page. Commands go through EspBase::console(), so the same registry
// serves the serial port and the browser. All WebSocket/HTTP callbacks run on
// the async_tcp task and only queue work; commands execute and logs stream
// from loop(). Compiled out entirely with -DESPBASE_NO_WEB.
#pragma once

#ifndef ESPBASE_NO_WEB

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include <deque>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "Console.h"
#include "EspBaseConfig.h"
#include "Log.h"

namespace espbase {

class EspBase;
class WifiManager;

class WebConsole {
 public:
  WebConsole();

  // password: console password (HTTP digest auth, user "admin"); empty = open console.
  void begin(EspBase& base, const String& password);
  void loop();

  // Valid after begin(). Register project routes here; put ones that change state under /api/.
  AsyncWebServer& server() { return *_server; }
  size_t clients() { return _ws.count(); }

  // Request checks, shared with OtaUpdater for uploads (async_tcp task).
  static constexpr const char* kUser = "admin";
  static constexpr const char* kRealm = "esp-base";
  bool authRequired() const { return !_password.isEmpty(); }
  // Same origin, and while no password is set only IP literals or our own
  // host names in the Host header (DNS rebinding).
  bool trustedOrigin(AsyncWebServerRequest* request) const;
  // True when no password is set. Locks a client's logins for a while after repeated failures.
  bool authenticated(AsyncWebServerRequest* request);
  void requestAuth(AsyncWebServerRequest* request);

 private:
  struct WsEvent {
    uint8_t type;  // AwsEventType
    uint32_t id;
  };
  struct PendingCmd {
    uint32_t id;
    String line;
  };
  struct Client {
    bool active = false;
    uint32_t id = 0;
    LogClass::Cursor cursor;
  };
  struct Ticket {
    char value[33];
    uint32_t expires;
  };
  struct Lockout {
    uint32_t ip = 0;
    uint8_t failures = 0;
    uint32_t until = 0;
  };
  static constexpr size_t kMaxTracked = 8;
  static constexpr size_t kMaxPending = 8;
  static constexpr size_t kFrameBudget = 1200;  // bytes of log JSON per WebSocket frame
  static constexpr size_t kMaxLockouts = 4;

  void setupRoutes();
  void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len);
  void handleWsEvent(const WsEvent& e);
  void runPendingCommands();
  void streamLogs();
  bool isCaptiveRequest(AsyncWebServerRequest* request) const;
  void sendCaptiveRedirect(AsyncWebServerRequest* request);
  static void appendLogJson(String& out, const LogClass::Record& r);
  static void appendJsonString(String& out, const char* s);
  // Single-use WebSocket tickets: browsers cannot reliably send HTTP
  // credentials on a WebSocket handshake, so the page fetches a ticket from
  // the (authenticated) API and presents it as /ws?t=<ticket>.
  String issueTicket();
  bool consumeTicket(AsyncWebServerRequest* request);
  Lockout& lockoutFor(uint32_t ip);

  EspBase* _base = nullptr;
  const EspBaseConfig* _cfg = nullptr;
  WifiManager* _wifi = nullptr;
  Console* _console = nullptr;
  AsyncWebServer* _server = nullptr;
  AsyncWebSocket _ws;
  Client _clients[kMaxTracked];
  QueueHandle_t _wsEvents = nullptr;
  SemaphoreHandle_t _pendingMutex = nullptr;
  std::deque<PendingCmd> _pending;
  uint32_t _lastCleanup = 0;
  String _password;
  Ticket _tickets[4] = {};
  Lockout _lockouts[kMaxLockouts];
};

}  // namespace espbase

#endif  // ESPBASE_NO_WEB
