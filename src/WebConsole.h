// Browser console: AsyncWebServer on cfg.httpPort, AsyncWebSocket at /ws,
// command registry, live log streaming, /api/info and the captive portal
// handlers. All WebSocket/HTTP callbacks run on the async_tcp task and only
// queue work; commands execute and logs stream from loop().
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include <deque>
#include <functional>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "EspBaseConfig.h"
#include "Log.h"

class WifiManager;

// Parsed command arguments (everything after the command name).
// Supports "quoted strings" and up to kMax tokens.
class CmdArgs {
 public:
  static constexpr size_t kMax = 8;

  // Splits `line` into the command name and its arguments. Returns false for an empty line.
  static bool parse(const String& line, String& cmd, CmdArgs& args);

  size_t size() const { return _n; }
  bool has(size_t i) const { return i < _n; }
  const String& operator[](size_t i) const { return i < _n ? _v[i] : _empty; }
  const String& get(size_t i) const { return (*this)[i]; }
  bool is(size_t i, const char* word) const { return i < _n && _v[i].equalsIgnoreCase(word); }
  long toInt(size_t i, long def = 0) const;
  // The raw text after the command name, untouched.
  const String& raw() const { return _raw; }

 private:
  String _v[kMax];
  size_t _n = 0;
  String _raw;
  static const String _empty;
};

using CmdHandler = std::function<void(const CmdArgs& args, Print& out)>;

// A Print that collects into a String (used for command output).
class StringPrint : public Print {
 public:
  size_t write(uint8_t c) override {
    buf += static_cast<char>(c);
    return 1;
  }
  size_t write(const uint8_t* data, size_t len) override {
    buf.concat(reinterpret_cast<const char*>(data), len);
    return len;
  }
  String buf;
};

class WebConsole {
 public:
  WebConsole();

  // password: console password (HTTP digest auth, user "admin"); empty = open console.
  void begin(const EspBaseConfig& cfg, WifiManager& wifi, const String& password);
  void loop();

  // Valid after begin().
  AsyncWebServer& server() { return *_server; }
  AsyncWebSocket& ws() { return _ws; }

  void addCommand(const char* name, CmdHandler handler, const char* help = "");
  // Executes one command line synchronously (used for the WebSocket, and by
  // projects that read commands from Serial). Returns false if unknown.
  bool execute(const String& line, Print& out);
  void printHelp(Print& out);

  // Device information (shared by GET /api/info and the "info" command).
  void writeInfo(JsonObject obj);
  void printInfo(Print& out);

  size_t wsClientCount() { return _ws.count(); }

  // Request checks, shared with OtaUpdater for uploads (async_tcp task).
  static constexpr const char* kUser = "admin";
  static constexpr const char* kRealm = "esp-base";
  bool authRequired() const { return !_password.isEmpty(); }
  // Same origin, and while no password is set only IP literals or our own
  // host names in the Host header (DNS rebinding).
  bool trustedOrigin(AsyncWebServerRequest* request) const;
  // True when no password is set. Locks logins for a while after repeated failures.
  bool authenticated(AsyncWebServerRequest* request);
  void requestAuth(AsyncWebServerRequest* request);

 private:
  struct Command {
    String name;
    String help;
    CmdHandler handler;
  };
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
  static constexpr size_t kMaxTracked = 8;
  static constexpr size_t kMaxPending = 8;
  static constexpr size_t kFrameBudget = 1200;  // bytes of log JSON per WebSocket frame

  void setupRoutes();
  void writeWifiStatus(JsonObject obj);
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

  const EspBaseConfig* _cfg = nullptr;
  WifiManager* _wifi = nullptr;
  AsyncWebServer* _server = nullptr;
  AsyncWebSocket _ws;
  std::vector<Command> _commands;
  Client _clients[kMaxTracked];
  QueueHandle_t _wsEvents = nullptr;
  SemaphoreHandle_t _pendingMutex = nullptr;
  std::deque<PendingCmd> _pending;
  uint32_t _lastCleanup = 0;
  String _password;
  struct Ticket {
    char value[33];
    uint32_t expires;
  };
  Ticket _tickets[4] = {};
  uint8_t _authFailures = 0;
  uint32_t _lockedUntil = 0;
};
