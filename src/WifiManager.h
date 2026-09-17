// WiFi station with AP fallback and captive portal DNS.
//
//   BOOT -> STA_CONNECTING -> STA_CONNECTED
//               | round failed        | lost
//               v                     v
//           AP_FALLBACK <---- STA_RECONNECTING (backoff 1, 2, 4 .. 30 s)
//
// Up to EspBaseKeys::kMaxStaSlots stored networks are tried in slot order
// (a "round"); hidden networks work because no scan is involved. When a round
// fails the AP comes up and rounds repeat every apRetryIntervalMs.
// Modes: Sta (above), Ap (AP only, station never used), ApSta (AP always on).
// setEnabled(false) stops the radio from any state (Off); setEnabled(true)
// starts over from Boot.
//
// Driven by WiFi.onEvent(); the event task only queues events, the state
// machine runs from loop(). Nothing here blocks.
#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>

#include <functional>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "ConfigStore.h"
#include "EspBaseConfig.h"

enum class WifiState : uint8_t { Boot, StaConnecting, StaConnected, StaReconnecting, ApFallback, ApOnly, Off };

class WifiManager {
 public:
  using Callback = std::function<void()>;
  static constexpr uint8_t kMaxSlots = EspBaseKeys::kMaxStaSlots;

  struct Settings {
    WifiMode mode = WifiMode::Sta;
    uint32_t staTimeoutMs = 20000;
    uint32_t reconnectTimeoutMs = 60000;
    uint32_t apRetryIntervalMs = 60000;
  };
  struct Slot {
    uint8_t slot = 0;
    String ssid;
    bool hasPassword = false;
  };

  void begin(const EspBaseConfig& cfg, ConfigStore& store);
  void loop();

  WifiState state() const { return _state; }
  static const char* stateName(WifiState s);
  const char* stateName() const { return stateName(_state); }
  static const char* modeName(WifiMode m);
  static bool parseMode(const char* text, WifiMode& out);
  Settings settings() const { return _settings; }
  bool isConnected() const { return _state == WifiState::StaConnected; }
  bool apActive() const { return _apActive; }
  bool hasCredentials() const { return _candCount > 0; }
  bool mdnsRunning() const { return _mdnsStarted; }

  // Safe to call from any task: copies of fixed buffers or driver queries.
  String hostname() const { return String(_hostname); }
  String staSsid() const;  // connected SSID, else the network being tried
  String apSsid() const { return String(_apSsid); }
  IPAddress localIP() const;
  IPAddress apIP() const;
  int8_t rssi() const;
  uint8_t apClients() const;
  String macAddress() const;
  uint32_t reconnectCount() const { return _reconnects; }

  // ---- Stored networks (slot 1 = primary). Callable from any task; the
  // state machine picks the change up in loop().
  // Stores and connects to that slot right away.
  bool setCredentials(const String& ssid, const String& pass, uint8_t slot = 1);
  // Stores without touching the current connection.
  bool storeCredentials(const String& ssid, const String& pass, uint8_t slot);
  // Stores in the first free slot; returns the slot or 0 when full/invalid.
  uint8_t addCredentials(const String& ssid, const String& pass);
  bool removeCredentials(uint8_t slot);
  bool forgetCredentials();  // all slots; drops to AP
  size_t listCredentials(Slot* out, size_t max);
  // Starts a new round now (slot 0 = from the first stored network).
  void reconnect(uint8_t slot = 0);

  // ---- Radio. Off drops the station and the AP and stops the WiFi driver, for
  // a project that needs the one radio for BLE or the battery for longer; on
  // starts over as after boot. Not persisted: WiFi is on after every boot.
  // While off, credentials and settings are stored only and scans are refused.
  // Callable from any task; applied in loop().
  void setEnabled(bool on) { _wantEnabled = on; }
  bool enabled() const { return _state != WifiState::Off; }

  // ---- Settings: validates, persists (key = EspBaseKeys::*) and applies from
  // loop(). Handles wifi_mode, sta_timeout, reconn_timeout, ap_retry and the
  // slot keys. Returns false with a message in `error`.
  bool applySetting(const char* key, const String& value, String& error);

  // ---- Scan (asynchronous)
  bool startScan();
  bool scanRunning() const { return _scanRunning; }
  String scanResultJson();  // [{"ssid":"x","rssi":-60,"ch":6,"enc":true}, ...] sorted by RSSI
  uint32_t scanCompletedAt() const { return _scanCompletedAt; }

  void onConnected(Callback cb) { _onConnected.push_back(std::move(cb)); }
  void onDisconnected(Callback cb) { _onDisconnected.push_back(std::move(cb)); }
  void onApStarted(Callback cb) { _onApStarted.push_back(std::move(cb)); }

  void printStatus(Print& out);
  void printNetworks(Print& out);

 private:
  struct Event {
    int32_t id;
    uint8_t reason;
  };
  struct Candidate {
    uint8_t slot = 0;
    char ssid[33] = {0};
    char pass[65] = {0};
  };

  void queueEvent(arduino_event_id_t id, const arduino_event_info_t& info);
  void handleEvent(const Event& e);
  void enterState(WifiState s);
  void startDriver();
  void radioOff();
  void radioOn();
  void loadSettings();
  void applySettingsNow();
  void loadCredentials();
  size_t indexOfSlot(uint8_t slot) const;
  size_t indexOfSsid(const String& ssid) const;
  bool nextCandidate();
  void startRound();
  void startStation();
  void staConnect();
  bool serviceAttempt(uint32_t now);
  void attemptFailed(const char* why);
  void abortAttempt();
  bool disconnectStation();
  void scheduleRetry(uint32_t delayMs);
  void startAp();
  void stopAp();
  void startMdns();
  void pollScan();
  void notify(std::vector<Callback>& list);
  static bool timeReached(uint32_t now, uint32_t at) { return static_cast<int32_t>(now - at) >= 0; }

  const EspBaseConfig* _cfg = nullptr;
  ConfigStore* _store = nullptr;
  DNSServer _dns;
  QueueHandle_t _events = nullptr;
  SemaphoreHandle_t _scanMutex = nullptr;

  Settings _settings;
  bool _apPersistent = false;

  WifiState _state = WifiState::Boot;
  uint32_t _stateSince = 0;
  bool _retryPending = false;
  uint32_t _retryAt = 0;
  bool _awaitingDisconnect = false;  // we asked for a disconnect; the event must not count as a failed attempt
  bool _connectCallFailed = false;
  uint32_t _backoffMs = 0;
  bool _staAttemptActive = false;
  uint32_t _attemptStart = 0;
  uint32_t _nextRoundAt = 0;
  bool _apActive = false;
  uint32_t _apStopAt = 0;
  bool _mdnsStarted = false;
  uint32_t _reconnects = 0;

  Candidate _cands[kMaxSlots];
  size_t _candCount = 0;
  size_t _candIdx = 0;      // network of the current/next attempt
  size_t _roundTried = 0;   // attempts made in the current round
  bool _roundActive = false;
  size_t _connectedIdx = 0;

  volatile bool _credentialsChanged = false;
  volatile bool _listChanged = false;
  volatile bool _forgetRequested = false;
  volatile bool _reconnectRequested = false;
  volatile bool _settingsChanged = false;
  volatile bool _wantEnabled = true;
  volatile uint8_t _preferredSlot = 0;

  bool _scanRunning = false;
  uint32_t _scanCompletedAt = 0;
  String _scanJson;

  char _hostname[33] = {0};
  char _apSsid[33] = {0};
  char _apPass[65] = {0};

  std::vector<Callback> _onConnected;
  std::vector<Callback> _onDisconnected;
  std::vector<Callback> _onApStarted;
};
