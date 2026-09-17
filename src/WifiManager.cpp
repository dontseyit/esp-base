#include "WifiManager.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <esp_mac.h>
#include <string.h>

#include <algorithm>

#include "Log.h"

namespace {
constexpr uint32_t kInitialRetryMs = 1000;
constexpr uint32_t kDeferredConnectMs = 3000;  // fallback if no disconnect event follows disconnectAsync()
constexpr uint32_t kMaxBackoffMs = 30000;
constexpr uint32_t kApGraceAfterStaMs = 3000;  // keep AP up briefly after STA connects
constexpr size_t kMaxScanLogged = 20;
constexpr uint8_t kApMaxClients = 4;
constexpr uint32_t kMinStaTimeoutMs = 3000;
constexpr uint32_t kMaxStaTimeoutMs = 300000;
constexpr uint32_t kMaxReconnectTimeoutMs = 3600000;
constexpr uint32_t kMinApRetryMs = 5000;
constexpr uint32_t kMaxApRetryMs = 3600000;
constexpr uint32_t kScanActiveMinMs = 100;       // the core's default wait on every channel
constexpr uint32_t kScanWaitMs = 250;            // how often a connection attempt looks whether the scan is over
constexpr uint32_t kMaxScanMsPerChannel = 1500;  // above this a connected station may lose its AP (ESP-IDF)

const char* authName(wifi_auth_mode_t m) {
  switch (m) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-EAP";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    default: return "other";
  }
}

// Validation messages, shared by the credential setters and applySetting().
const char* ssidError(const String& ssid) {
  return (ssid.isEmpty() || ssid.length() > 32) ? "ssid must be 1..32 chars" : nullptr;
}

const char* passError(const String& pass) {
  return (!pass.isEmpty() && (pass.length() < 8 || pass.length() > 63)) ? "password must be empty or 8..63 chars" : nullptr;
}

bool validCredentials(const String& ssid, const String& pass) {
  const char* err = ssidError(ssid);
  if (err == nullptr) {
    err = passError(pass);
  }
  if (err != nullptr) {
    LOG_W("wifi: %s", err);
    return false;
  }
  return true;
}

// NVS key names of one stored network slot.
struct SlotKeys {
  explicit SlotKeys(uint8_t slot) {
    EspBaseKeys::slotSsidKey(slot, ssid, sizeof(ssid));
    EspBaseKeys::slotPassKey(slot, pass, sizeof(pass));
  }
  char ssid[ConfigStore::kMaxKeyLen + 1];
  char pass[ConfigStore::kMaxKeyLen + 1];
};

bool parseMs(const String& text, uint32_t& out) {
  if (text.isEmpty()) {
    return false;
  }
  char* end = nullptr;
  const long v = strtol(text.c_str(), &end, 10);
  if (end == nullptr || *end != '\0' || v < 0) {
    return false;
  }
  out = static_cast<uint32_t>(v);
  return true;
}
}  // namespace

const char* WifiManager::stateName(WifiState s) {
  switch (s) {
    case WifiState::Boot: return "boot";
    case WifiState::StaConnecting: return "sta_connecting";
    case WifiState::StaConnected: return "sta_connected";
    case WifiState::StaReconnecting: return "sta_reconnecting";
    case WifiState::ApFallback: return "ap_fallback";
    case WifiState::ApOnly: return "ap_only";
    case WifiState::Off: return "off";
    default: return "?";
  }
}

const char* WifiManager::modeName(WifiMode m) {
  switch (m) {
    case WifiMode::Ap: return "ap";
    case WifiMode::ApSta: return "apsta";
    default: return "sta";
  }
}

bool WifiManager::parseMode(const char* text, WifiMode& out) {
  if (text == nullptr) {
    return false;
  }
  const String t(text);
  if (t.equalsIgnoreCase("sta")) {
    out = WifiMode::Sta;
  } else if (t.equalsIgnoreCase("ap")) {
    out = WifiMode::Ap;
  } else if (t.equalsIgnoreCase("apsta") || t.equalsIgnoreCase("ap_sta")) {
    out = WifiMode::ApSta;
  } else {
    return false;
  }
  return true;
}

// ---- Setup ---------------------------------------------------------------------

void WifiManager::begin(const EspBaseConfig& cfg, ConfigStore& store) {
  _cfg = &cfg;
  _store = &store;
  if (_events == nullptr) {
    _events = xQueueCreate(16, sizeof(Event));
  }
  if (_scanMutex == nullptr) {
    _scanMutex = xSemaphoreCreateMutex();
  }

  // Identity: persisted hostname wins over the config default.
  String host = store.getString(EspBaseKeys::Hostname, cfg.hostname);
  if (host.isEmpty()) {
    host = "esp-base";
  }
  strncpy(_hostname, host.c_str(), sizeof(_hostname) - 1);

  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(_apSsid, sizeof(_apSsid), "%.27s-%02X%02X", cfg.apSsidPrefix.c_str(), mac[4], mac[5]);

  String apPass = store.getString(EspBaseKeys::ApPass, cfg.apPassword);
  if (passError(apPass) != nullptr) {
    LOG_W("wifi: AP password must be 8..63 chars, using an open AP");
    apPass = "";
  }
  strncpy(_apPass, apPass.c_str(), sizeof(_apPass) - 1);

  loadSettings();
  loadCredentials();

  // lwIP and the event loop must exist before WebConsole binds its listening
  // socket in begin(): AsyncTCP asserts on the missing lwIP core lock. A few
  // ms, no radio; the driver still starts from the first loop() call.
  Network.begin();
  WiFi.persistent(false);        // credentials live in ConfigStore, not in the SDK's NVS
  WiFi.setAutoReconnect(false);  // reconnects are scheduled here, with backoff
  WiFi.setHostname(_hostname);   // must precede WiFi.mode()
  WiFi.onEvent([this](arduino_event_id_t id, arduino_event_info_t info) { queueEvent(id, info); });

  LOG_I("wifi: hostname %s, mac %02x:%02x:%02x:%02x:%02x:%02x, ap ssid %s, mode %s, %u stored network(s)", _hostname, mac[0], mac[1], mac[2],
        mac[3], mac[4], mac[5], _apSsid, modeName(_settings.mode), static_cast<unsigned>(_candCount));
  LOG_D("wifi: sta timeout %lu ms, reconnect timeout %lu ms, ap retry %lu ms", static_cast<unsigned long>(_settings.staTimeoutMs),
        static_cast<unsigned long>(_settings.reconnectTimeoutMs), static_cast<unsigned long>(_settings.apRetryIntervalMs));
  // The driver starts from the first loop() call (see startDriver()), which
  // keeps begin() well under 100 ms.
  _state = WifiState::Boot;
}

void WifiManager::loadSettings() {
  const EspBaseConfig& c = *_cfg;
  WifiMode mode = c.wifiMode;
  const String stored = _store->getString(EspBaseKeys::WifiModeKey);
  if (!stored.isEmpty() && !parseMode(stored.c_str(), mode)) {
    LOG_W("wifi: ignoring invalid wifi_mode \"%s\"", stored.c_str());
    mode = c.wifiMode;
  }
  auto readMs = [this](const char* key, uint32_t def) {
    const int32_t v = _store->getInt(key, static_cast<int32_t>(def));
    return v < 0 ? 0U : static_cast<uint32_t>(v);
  };
  _settings.mode = mode;
  _settings.staTimeoutMs = std::min(std::max(readMs(EspBaseKeys::StaTimeout, c.staTimeoutMs), kMinStaTimeoutMs), kMaxStaTimeoutMs);
  _settings.reconnectTimeoutMs = std::min(readMs(EspBaseKeys::ReconnTimeout, c.reconnectTimeoutMs), kMaxReconnectTimeoutMs);
  const uint32_t apRetry = readMs(EspBaseKeys::ApRetry, c.apRetryIntervalMs);
  _settings.apRetryIntervalMs = (apRetry == 0) ? 0 : std::min(std::max(apRetry, kMinApRetryMs), kMaxApRetryMs);
  _apPersistent = (mode != WifiMode::Sta);
}

void WifiManager::loadCredentials() {
  _candCount = 0;
  for (uint8_t slot = 1; slot <= kMaxSlots; ++slot) {
    const SlotKeys keys(slot);
    const String ssid = _store->getString(keys.ssid);
    if (ssid.isEmpty()) {
      continue;
    }
    Candidate& c = _cands[_candCount++];
    c.slot = slot;
    strlcpy(c.ssid, ssid.c_str(), sizeof(c.ssid));
    strlcpy(c.pass, _store->getString(keys.pass).c_str(), sizeof(c.pass));
  }
  if (_candIdx >= _candCount) {
    _candIdx = 0;
  }
  if (_connectedIdx >= _candCount) {
    _connectedIdx = 0;
  }
}

size_t WifiManager::indexOfSlot(uint8_t slot) const {
  for (size_t i = 0; i < _candCount; ++i) {
    if (_cands[i].slot == slot) {
      return i;
    }
  }
  return 0;
}

size_t WifiManager::indexOfSsid(const String& ssid) const {
  for (size_t i = 0; i < _candCount; ++i) {
    if (ssid == _cands[i].ssid) {
      return i;
    }
  }
  return _candIdx < _candCount ? _candIdx : 0;
}

void WifiManager::startDriver() {
  if (_settings.mode == WifiMode::Ap) {
    startAp();
    enterState(WifiState::ApOnly);
    return;
  }
  WiFi.mode(WIFI_STA);  // starts the WiFi driver, typically 50..200 ms
  if (_apPersistent) {
    startAp();
  }
  if (!hasCredentials()) {
    LOG_I("wifi: no stored network");
  }
  startStation();
}

// Starts a round from the first stored network, or falls back to the AP.
void WifiManager::startStation() {
  if (hasCredentials()) {
    _candIdx = 0;
    enterState(WifiState::StaConnecting);
  } else {
    enterState(WifiState::ApFallback);
  }
}

// ---- State machine ------------------------------------------------------------

void WifiManager::enterState(WifiState s) {
  const uint32_t now = millis();
  if (s != _state) {
    LOG_D("wifi: %s -> %s", stateName(_state), stateName(s));
  }
  _state = s;
  _stateSince = now;
  _retryPending = false;
  switch (s) {
    case WifiState::StaConnecting:
      _backoffMs = 0;
      _roundTried = 0;
      _roundActive = true;
      if (_awaitingDisconnect) {
        // A disconnect is in flight; the STA_DISCONNECTED event pulls the
        // connect forward, the timer is only a safety net.
        scheduleRetry(kDeferredConnectMs);
      } else {
        staConnect();
      }
      break;
    case WifiState::StaReconnecting:
      _backoffMs = kInitialRetryMs;
      _roundActive = false;
      _candIdx = _connectedIdx;  // the network that was up is the most likely to return
      scheduleRetry(_backoffMs);
      break;
    case WifiState::ApFallback:
      _roundActive = false;
      _nextRoundAt = now + _settings.apRetryIntervalMs;
      startAp();
      break;
    case WifiState::StaConnected:
      _backoffMs = 0;
      _staAttemptActive = false;
      _roundActive = false;
      _awaitingDisconnect = false;
      if (_apActive && !_apPersistent) {
        _apStopAt = now + kApGraceAfterStaMs;
      }
      break;
    case WifiState::ApOnly:
    case WifiState::Off:
      _roundActive = false;
      _staAttemptActive = false;
      break;
    default:
      break;
  }
}

// Stops the driver from whatever state it is in. Events still queued belong to
// the radio that is going away; loop() discards them while Off.
void WifiManager::radioOff() {
  LOG_I("wifi: radio off");
  disconnectStation();
  _dns.stop();
  if (!WiFi.mode(WIFI_OFF)) {  // stops the driver, tens of ms; netifs and the event hook stay
    LOG_W("wifi: the driver did not stop");
  }
  _apActive = false;
  _apStopAt = 0;
  _awaitingDisconnect = false;
  _connectCallFailed = false;
  enterState(WifiState::Off);
}

// Back to Boot with whatever was stored while off; startDriver() runs from the same loop() call.
void WifiManager::radioOn() {
  LOG_I("wifi: radio on");
  _credentialsChanged = false;
  _listChanged = false;
  _forgetRequested = false;
  _reconnectRequested = false;
  _settingsChanged = false;
  _preferredSlot = 0;
  loadSettings();
  loadCredentials();
  _state = WifiState::Boot;
}

bool WifiManager::nextCandidate() {
  _roundTried++;
  if (_roundTried >= _candCount) {
    return false;
  }
  _candIdx = (_candIdx + 1) % _candCount;
  return true;
}

void WifiManager::startRound() {
  _candIdx = 0;
  _roundTried = 0;
  _roundActive = true;
  staConnect();
}

void WifiManager::staConnect() {
  if (!hasCredentials()) {
    return;
  }
  if (_scanPhase == ScanPhase::Running) {
    scheduleRetry(kScanWaitMs);  // esp_wifi_connect() would abort the scan: the attempt waits for it
    return;
  }
  if (_candIdx >= _candCount) {
    _candIdx = 0;
  }
  const Candidate& c = _cands[_candIdx];
  _staAttemptActive = true;
  _attemptStart = millis();
  LOG_I("wifi: connecting to \"%s\" (slot %u, %u of %u)", c.ssid, c.slot, static_cast<unsigned>(_roundTried + 1), static_cast<unsigned>(_candCount));
  if (WiFi.begin(c.ssid, c.pass[0] ? c.pass : nullptr) == WL_CONNECT_FAILED) {
    // No event will follow a failed call; treat it as a failed attempt from loop().
    _staAttemptActive = false;
    _connectCallFailed = true;
  }
}

void WifiManager::scheduleRetry(uint32_t delayMs) {
  _retryPending = true;
  _retryAt = millis() + delayMs;
}

// Fires a due retry or gives up on an attempt that timed out; true when it did either.
bool WifiManager::serviceAttempt(uint32_t now) {
  if (_retryPending && timeReached(now, _retryAt)) {
    _retryPending = false;
    _awaitingDisconnect = false;
    staConnect();
    return true;
  }
  if (_staAttemptActive && now - _attemptStart >= _settings.staTimeoutMs) {
    abortAttempt();
    return true;
  }
  return false;
}

// Drops the station link or the attempt in flight (asynchronously); true when a
// disconnect was requested. Callers decide whether to wait for its event.
bool WifiManager::disconnectStation() {
  if (_state == WifiState::StaConnected) {
    notify(_onDisconnected);
  }
  const bool active = (_state == WifiState::StaConnected) || _staAttemptActive;
  if (active) {
    WiFi.disconnectAsync(false, false);
  }
  _staAttemptActive = false;
  return active;
}

// Gives up on the attempt in flight (no answer within staTimeoutMs).
void WifiManager::abortAttempt() {
  LOG_W("wifi: \"%s\" did not answer within %lu ms", _candIdx < _candCount ? _cands[_candIdx].ssid : "?",
        static_cast<unsigned long>(_settings.staTimeoutMs));
  WiFi.disconnectAsync(false, false);
  _awaitingDisconnect = true;
  attemptFailed("timeout");
}

void WifiManager::attemptFailed(const char* why) {
  _staAttemptActive = false;
  const uint32_t now = millis();
  switch (_state) {
    case WifiState::StaConnecting:
      if (nextCandidate()) {
        LOG_I("wifi: %s, trying next network", why);
        scheduleRetry(kInitialRetryMs);
      } else {
        LOG_W("wifi: none of %u stored network(s) reachable (%s), starting AP", static_cast<unsigned>(_candCount), why);
        enterState(WifiState::ApFallback);
      }
      break;
    case WifiState::StaReconnecting:
      _backoffMs = std::min<uint32_t>(_backoffMs * 2, kMaxBackoffMs);
      if (_candCount > 1) {
        _candIdx = (_candIdx + 1) % _candCount;
      }
      LOG_D("wifi: reconnect failed (%s), next try in %lu ms", why, static_cast<unsigned long>(_backoffMs));
      scheduleRetry(_backoffMs);
      break;
    case WifiState::ApFallback:
      if (!_roundActive) {
        LOG_D("wifi: sta attempt failed (%s)", why);
        break;
      }
      if (nextCandidate()) {
        LOG_I("wifi: %s, trying next network", why);
        scheduleRetry(kInitialRetryMs);
      } else {
        _roundActive = false;
        _nextRoundAt = now + _settings.apRetryIntervalMs;
        if (_settings.apRetryIntervalMs > 0) {
          LOG_I("wifi: no stored network reachable, next round in %lu s", static_cast<unsigned long>(_settings.apRetryIntervalMs / 1000));
        } else {
          LOG_I("wifi: no stored network reachable, retries disabled (use \"wifi reconnect\")");
        }
      }
      break;
    default:
      break;
  }
}

void WifiManager::startAp() {
  if (_apActive) {
    return;
  }
  WiFi.mode(WIFI_AP_STA);  // STA stays enabled for retries and scans
  const bool ok = WiFi.softAP(_apSsid, _apPass[0] ? _apPass : nullptr, 1, 0, kApMaxClients);
  if (!ok) {
    LOG_E("wifi: softAP failed");
  }
}

void WifiManager::stopAp() {
  _apStopAt = 0;
  if (!_apActive) {
    return;
  }
  LOG_I("wifi: stopping AP");
  _dns.stop();
  WiFi.softAPdisconnect(false);
  WiFi.mode(WIFI_STA);
  _apActive = false;
}

void WifiManager::startMdns() {
  if (_mdnsStarted) {
    return;
  }
  if (MDNS.begin(_hostname)) {
    MDNS.addService("http", "tcp", _cfg->httpPort);
    _mdnsStarted = true;
    LOG_I("wifi: mDNS %s.local", _hostname);
  } else {
    LOG_W("wifi: mDNS start failed");
  }
}

void WifiManager::queueEvent(arduino_event_id_t id, const arduino_event_info_t& info) {
  // Runs on the network event task: only queue, never act.
  Event e{static_cast<int32_t>(id), 0};
  if (id == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    e.reason = info.wifi_sta_disconnected.reason;
  }
  if (_events != nullptr) {
    xQueueSend(_events, &e, 0);
  }
}

void WifiManager::handleEvent(const Event& e) {
  const auto id = static_cast<arduino_event_id_t>(e.id);
  switch (id) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      LOG_I("wifi: connected to \"%s\", ip %s, rssi %d dBm", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
      _awaitingDisconnect = false;
      if (_state != WifiState::StaConnected) {
        if (_state == WifiState::StaReconnecting || _state == WifiState::ApFallback) {
          _reconnects++;
        }
        _connectedIdx = indexOfSsid(WiFi.SSID());
        enterState(WifiState::StaConnected);
        startMdns();
        notify(_onConnected);
      }
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    case ARDUINO_EVENT_WIFI_STA_LOST_IP: {
      const char* why = (id == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
                            ? WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(e.reason))
                            : "lost ip";
      if (_awaitingDisconnect) {
        // Our own disconnect completed: connect the next network right away.
        _awaitingDisconnect = false;
        _staAttemptActive = false;
        if (_retryPending) {
          _retryAt = millis();
        }
        break;
      }
      switch (_state) {
        case WifiState::StaConnected:
          LOG_W("wifi: link lost (%s)", why);
          _staAttemptActive = false;
          notify(_onDisconnected);
          if (_settings.reconnectTimeoutMs == 0) {
            enterState(WifiState::ApFallback);
            _nextRoundAt = millis() + kInitialRetryMs;  // AP right away, STA retries continue next to it
          } else {
            enterState(WifiState::StaReconnecting);
          }
          break;
        case WifiState::StaConnecting:
        case WifiState::StaReconnecting:
        case WifiState::ApFallback:
          attemptFailed(why);
          break;
        default:
          break;
      }
      break;
    }

    case ARDUINO_EVENT_WIFI_AP_START:
      _apActive = true;
      if (_state == WifiState::StaConnected && !_apPersistent && _apStopAt == 0) {
        _apStopAt = millis() + kApGraceAfterStaMs;  // STA won the race, AP is not needed
      }
      if (!_dns.isUp()) {
        _dns.setTTL(60);
        if (!_dns.start()) {  // port 53, every name resolves to the AP IP
          LOG_W("wifi: captive DNS failed to start");
        }
      }
      LOG_I("wifi: AP \"%s\" up at %s (%s)", _apSsid, WiFi.softAPIP().toString().c_str(), _apPass[0] ? "wpa2" : "open");
      notify(_onApStarted);
      break;

    case ARDUINO_EVENT_WIFI_AP_STOP:
      _apActive = false;
      _dns.stop();
      break;

    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      LOG_I("wifi: AP client joined (%u connected)", WiFi.softAPgetStationNum());
      break;

    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      LOG_I("wifi: AP client left (%u connected)", WiFi.softAPgetStationNum());
      break;

    default:
      break;
  }
}

void WifiManager::applySettingsNow() {
  const WifiMode old = _settings.mode;
  loadSettings();
  LOG_I("wifi: settings: mode %s, sta timeout %lu ms, reconnect timeout %lu ms, ap retry %lu ms", modeName(_settings.mode),
        static_cast<unsigned long>(_settings.staTimeoutMs), static_cast<unsigned long>(_settings.reconnectTimeoutMs),
        static_cast<unsigned long>(_settings.apRetryIntervalMs));
  if (old == _settings.mode || _state == WifiState::Boot) {
    return;
  }
  switch (_settings.mode) {
    case WifiMode::Ap:
      disconnectStation();
      _awaitingDisconnect = false;
      startAp();
      enterState(WifiState::ApOnly);
      break;
    case WifiMode::ApSta:
      startAp();
      if (_state == WifiState::ApOnly) {
        startStation();
      }
      break;
    case WifiMode::Sta:
      if (_state == WifiState::ApOnly) {
        startStation();
      } else if (_state == WifiState::StaConnected && _apActive) {
        stopAp();
      }
      break;
  }
}

void WifiManager::loop() {
  if (_cfg == nullptr) {
    return;
  }
  const bool scanWasActive = _scanPhase != ScanPhase::Idle;
  serviceScan();
  if (_wantEnabled != enabled() && _scanPhase != ScanPhase::Running) {  // either way, the switch waits for a running scan
    if (_wantEnabled) {
      radioOn();
    } else {
      radioOff();
    }
  }
  if (_state == WifiState::Off) {
    // Only a scan can have the driver up in this state: it got the radio for its
    // own duration, and the radio goes down again when it is over.
    if (scanWasActive && _scanPhase == ScanPhase::Idle) {
      WiFi.mode(WIFI_OFF);
    }
    xQueueReset(_events);
    return;
  }
  if (_state == WifiState::Boot) {
    startDriver();
  }
  Event e;
  while (_events != nullptr && xQueueReceive(_events, &e, 0) == pdTRUE) {
    handleEvent(e);
  }

  const uint32_t now = millis();

  if (_settingsChanged) {
    _settingsChanged = false;
    applySettingsNow();
  }

  if (_forgetRequested) {
    _forgetRequested = false;
    loadCredentials();
    LOG_W("wifi: all stored networks removed");
    if (_state != WifiState::ApOnly) {
      disconnectStation();
      _awaitingDisconnect = false;
      enterState(WifiState::ApFallback);
    }
  }

  if (_listChanged) {
    _listChanged = false;
    loadCredentials();
    LOG_I("wifi: %u stored network(s)", static_cast<unsigned>(_candCount));
    if (_state == WifiState::ApFallback && hasCredentials() && !_roundActive && !_staAttemptActive) {
      startRound();  // something new to try, do not wait for the retry interval
    } else if (_state != WifiState::StaConnected && _state != WifiState::ApOnly && !hasCredentials() && !_staAttemptActive) {
      enterState(WifiState::ApFallback);
    }
  }

  if (_credentialsChanged || _reconnectRequested) {
    const bool changed = _credentialsChanged;
    _credentialsChanged = false;
    _reconnectRequested = false;
    const uint8_t preferred = _preferredSlot;
    _preferredSlot = 0;
    loadCredentials();
    if (_state == WifiState::ApOnly) {
      LOG_W("wifi: station disabled (mode ap), %s", changed ? "credentials stored only" : "nothing to reconnect");
    } else if (!hasCredentials()) {
      LOG_W("wifi: no stored network to connect to");
    } else {
      LOG_I("wifi: %s, connecting", changed ? "new credentials" : "reconnect requested");
      // Calling WiFi.begin() while still associated makes the core block for
      // up to 1 s; disconnect asynchronously and connect on the event instead.
      if (disconnectStation()) {
        _awaitingDisconnect = true;
      }
      _candIdx = preferred ? indexOfSlot(preferred) : 0;
      enterState(WifiState::StaConnecting);  // AP (if up) stays up until STA succeeds
    }
  }

  if (_connectCallFailed) {
    _connectCallFailed = false;
    attemptFailed("connect call failed");
  }

  switch (_state) {
    case WifiState::StaConnecting:
      serviceAttempt(now);
      break;

    case WifiState::StaReconnecting:
      serviceAttempt(now);
      if (now - _stateSince >= _settings.reconnectTimeoutMs) {
        LOG_W("wifi: still down after %lu ms, starting AP", static_cast<unsigned long>(_settings.reconnectTimeoutMs));
        enterState(WifiState::ApFallback);
        _nextRoundAt = now + kInitialRetryMs;  // keep retrying next to the AP
      }
      break;

    case WifiState::ApFallback:
      if (!serviceAttempt(now) && !_roundActive && !_staAttemptActive && hasCredentials() && _settings.apRetryIntervalMs > 0 &&
          timeReached(now, _nextRoundAt)) {
        LOG_I("wifi: retrying stored networks from AP mode");
        startRound();
      }
      break;

    case WifiState::StaConnected:
      if (_apActive && !_apPersistent && _apStopAt != 0 && timeReached(now, _apStopAt)) {
        stopAp();
      }
      break;

    default:
      break;
  }
}

// ---- Credentials -----------------------------------------------------------------

bool WifiManager::storeCredentials(const String& ssid, const String& pass, uint8_t slot) {
  if (_store == nullptr || slot < 1 || slot > kMaxSlots || !validCredentials(ssid, pass)) {
    return false;
  }
  const SlotKeys keys(slot);
  if (!_store->setString(keys.ssid, ssid) || !_store->setString(keys.pass, pass)) {
    LOG_E("wifi: failed to store credentials");
    return false;
  }
  _listChanged = true;
  return true;
}

bool WifiManager::setCredentials(const String& ssid, const String& pass, uint8_t slot) {
  if (!storeCredentials(ssid, pass, slot)) {
    return false;
  }
  _listChanged = false;  // the reconnect path reloads the list itself
  _preferredSlot = slot;
  _credentialsChanged = true;
  return true;
}

uint8_t WifiManager::addCredentials(const String& ssid, const String& pass) {
  if (_store == nullptr || !validCredentials(ssid, pass)) {
    return 0;
  }
  for (uint8_t slot = 1; slot <= kMaxSlots; ++slot) {
    if (_store->getString(SlotKeys(slot).ssid) == ssid) {
      return storeCredentials(ssid, pass, slot) ? slot : 0;  // update the password of a known network
    }
  }
  for (uint8_t slot = 1; slot <= kMaxSlots; ++slot) {
    if (_store->getString(SlotKeys(slot).ssid).isEmpty()) {
      return storeCredentials(ssid, pass, slot) ? slot : 0;
    }
  }
  LOG_W("wifi: all %u network slots are in use", kMaxSlots);
  return 0;
}

bool WifiManager::removeCredentials(uint8_t slot) {
  if (_store == nullptr || slot < 1 || slot > kMaxSlots) {
    return false;
  }
  const SlotKeys keys(slot);
  _store->remove(keys.ssid);
  _store->remove(keys.pass);
  _listChanged = true;
  return true;
}

bool WifiManager::forgetCredentials() {
  if (_store == nullptr) {
    return false;
  }
  for (uint8_t slot = 1; slot <= kMaxSlots; ++slot) {
    const SlotKeys keys(slot);
    _store->remove(keys.ssid);
    _store->remove(keys.pass);
  }
  _forgetRequested = true;
  return true;
}

size_t WifiManager::listCredentials(Slot* out, size_t max) {
  if (_store == nullptr) {
    return 0;
  }
  size_t n = 0;
  for (uint8_t slot = 1; slot <= kMaxSlots && n < max; ++slot) {
    const SlotKeys keys(slot);
    const String ssid = _store->getString(keys.ssid);
    if (ssid.isEmpty()) {
      continue;
    }
    out[n].slot = slot;
    out[n].ssid = ssid;
    out[n].hasPassword = !_store->getString(keys.pass).isEmpty();
    n++;
  }
  return n;
}

void WifiManager::reconnect(uint8_t slot) {
  _preferredSlot = slot;
  _reconnectRequested = true;
}

bool WifiManager::applySetting(const char* key, const String& value, String& error) {
  if (_store == nullptr || key == nullptr) {
    error = "store not ready";
    return false;
  }
  uint8_t slot = 0;
  bool isPass = false;
  if (EspBaseKeys::isSlotKey(key, slot, isPass)) {
    const char* invalid = isPass ? passError(value) : ssidError(value);
    if (invalid != nullptr) {
      error = invalid;
      return false;
    }
    if (!_store->setString(key, value)) {
      error = "write failed";
      return false;
    }
    _listChanged = true;
    return true;
  }
  if (strcmp(key, EspBaseKeys::WifiModeKey) == 0) {
    WifiMode mode;
    if (!parseMode(value.c_str(), mode)) {
      error = "wifi_mode must be sta, ap or apsta";
      return false;
    }
    if (!_store->setString(key, modeName(mode))) {
      error = "write failed";
      return false;
    }
    _settingsChanged = true;
    return true;
  }
  uint32_t ms = 0;
  if (strcmp(key, EspBaseKeys::StaTimeout) == 0) {
    if (!parseMs(value, ms) || ms < kMinStaTimeoutMs || ms > kMaxStaTimeoutMs) {
      error = "sta_timeout must be 3000..300000 ms";
      return false;
    }
  } else if (strcmp(key, EspBaseKeys::ReconnTimeout) == 0) {
    if (!parseMs(value, ms) || ms > kMaxReconnectTimeoutMs) {
      error = "reconn_timeout must be 0..3600000 ms (0 = AP immediately)";
      return false;
    }
  } else if (strcmp(key, EspBaseKeys::ApRetry) == 0) {
    if (!parseMs(value, ms) || (ms != 0 && (ms < kMinApRetryMs || ms > kMaxApRetryMs))) {
      error = "ap_retry must be 0 (never) or 5000..3600000 ms";
      return false;
    }
  } else {
    error = "not a wifi setting";
    return false;
  }
  if (!_store->setInt(key, static_cast<int32_t>(ms))) {
    error = "write failed";
    return false;
  }
  _settingsChanged = true;
  return true;
}

// ---- Scan -----------------------------------------------------------------------

// One radio operation at a time, and the driver is the loop task's alone: this
// only files a request, from any task. The mutex settles two tasks asking at once.
bool WifiManager::startScan(uint32_t maxMsPerChannel, bool logResults) {
  if (_scanMutex == nullptr || xSemaphoreTake(_scanMutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }
  // Refused rather than queued behind a connection attempt: the caller hears "busy" now.
  const bool accepted = _scanPhase == ScanPhase::Idle && !_staAttemptActive;
  if (accepted) {
    _scanMsPerChannel = std::min(maxMsPerChannel, kMaxScanMsPerChannel);
    _scanLog = logResults;
    _scanPhase = ScanPhase::Requested;
  }
  xSemaphoreGive(_scanMutex);
  return accepted;
}

// Loop task. Starts a filed scan and collects a finished one. A scan filed
// before the driver's first start, or just before a connection attempt began
// (the driver would refuse it), waits for that.
void WifiManager::serviceScan() {
  if (_scanPhase == ScanPhase::Requested && _state != WifiState::Boot && !_staAttemptActive) {
    _scanPhase = ScanPhase::Running;
    if (!beginScan()) {
      endScan();
    }
  }
  if (_scanPhase == ScanPhase::Running) {
    pollScan();
  }
}

// Where every accepted scan ends. Idle first, so the callback may file the next
// one; the driver keeps the results until the callback returns.
void WifiManager::endScan() {
  _scanPhase = ScanPhase::Idle;
  notify(_onScanDone);
  WiFi.scanDelete();
}

bool WifiManager::beginScan() {
  // The core waits kScanActiveMinMs on every channel even when nothing answers; a tighter bound lowers that too.
  WiFi.setScanActiveMinTime(std::min(kScanActiveMinMs, _scanMsPerChannel));
  // Starts the station by itself when the radio is off. Nothing connects without WiFi.begin().
  const int16_t r = WiFi.scanNetworks(true, true, false, _scanMsPerChannel);
  if (r == WIFI_SCAN_FAILED) {
    LOG_W("wifi: scan could not start");
    return false;
  }
  if (_scanLog) {
    LOG_I("wifi: scan started");
  }
  return true;
}

void WifiManager::pollScan() {
  const int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    return;
  }
  _scanCompletedAt = millis();
  if (n < 0) {
    LOG_W("wifi: scan failed");
    endScan();
    return;
  }
  std::vector<int> order;
  order.reserve(n);
  for (int i = 0; i < n; ++i) {
    order.push_back(i);
  }
  std::sort(order.begin(), order.end(), [](int a, int b) { return WiFi.RSSI(a) > WiFi.RSSI(b); });

  JsonDocument doc;
  JsonArray nets = doc.to<JsonArray>();
  if (_scanLog) {
    LOG_I("wifi: scan found %d networks", n);
  } else {
    LOG_D("wifi: scan found %d networks", n);
  }
  for (size_t k = 0; k < order.size(); ++k) {
    const int i = order[k];
    const String ssid = WiFi.SSID(i);
    const int32_t rssi = WiFi.RSSI(i);
    const int32_t ch = WiFi.channel(i);
    const wifi_auth_mode_t auth = WiFi.encryptionType(i);
    JsonObject net = nets.add<JsonObject>();
    net["ssid"] = ssid;
    net["rssi"] = rssi;
    net["ch"] = ch;
    net["enc"] = (auth != WIFI_AUTH_OPEN);
    if (_scanLog && k < kMaxScanLogged) {
      LOG_I("  %4ld dBm ch%-2ld %-9s %s", static_cast<long>(rssi), static_cast<long>(ch), authName(auth),
            ssid.isEmpty() ? "<hidden>" : ssid.c_str());
    }
  }
  String json;
  serializeJson(doc, json);
  endScan();
  if (xSemaphoreTake(_scanMutex, portMAX_DELAY) == pdTRUE) {
    _scanJson = json;
    xSemaphoreGive(_scanMutex);
  }
}

String WifiManager::scanResultJson() {
  String copy;
  if (_scanMutex != nullptr && xSemaphoreTake(_scanMutex, portMAX_DELAY) == pdTRUE) {
    copy = _scanJson;
    xSemaphoreGive(_scanMutex);
  }
  return copy;
}

// ---- Getters / status --------------------------------------------------------

String WifiManager::staSsid() const {
  if (isConnected()) {
    return WiFi.SSID();
  }
  if (_candCount > 0 && _candIdx < _candCount) {
    return String(_cands[_candIdx].ssid);
  }
  return String();
}

IPAddress WifiManager::localIP() const {
  return WiFi.localIP();
}

IPAddress WifiManager::apIP() const {
  return WiFi.softAPIP();
}

int8_t WifiManager::rssi() const {
  return isConnected() ? WiFi.RSSI() : 0;
}

uint8_t WifiManager::apClients() const {
  return _apActive ? WiFi.softAPgetStationNum() : 0;
}

String WifiManager::macAddress() const {
  return WiFi.macAddress();
}

void WifiManager::notify(std::vector<Callback>& list) {
  for (auto& cb : list) {
    if (cb) {
      cb();
    }
  }
}

void WifiManager::printNetworks(Print& out) {
  Slot slots[kMaxSlots];
  const size_t n = listCredentials(slots, kMaxSlots);
  if (n == 0) {
    out.println("  (no stored networks)");
    return;
  }
  const String current = isConnected() ? WiFi.SSID() : String();
  for (size_t i = 0; i < n; ++i) {
    out.printf("%c slot %u  %-32s %s\r\n", (!current.isEmpty() && slots[i].ssid == current) ? '*' : ' ', slots[i].slot, slots[i].ssid.c_str(),
               slots[i].hasPassword ? "(password set)" : "(open)");
  }
}

void WifiManager::printStatus(Print& out) {
  out.printf("state:      %s (mode %s)\r\n", stateName(), modeName(_settings.mode));
  out.printf("hostname:   %s (%s.local)\r\n", _hostname, _hostname);
  out.printf("mac:        %s\r\n", WiFi.macAddress().c_str());
  if (isConnected()) {
    out.printf("ssid:       %s (%s, ch %d)\r\n", WiFi.SSID().c_str(), WiFi.BSSIDstr().c_str(), static_cast<int>(WiFi.channel()));
    out.printf("rssi:       %d dBm\r\n", WiFi.RSSI());
    out.printf("ip:         %s\r\n", WiFi.localIP().toString().c_str());
    out.printf("gateway:    %s  mask %s  dns %s\r\n", WiFi.gatewayIP().toString().c_str(), WiFi.subnetMask().toString().c_str(),
               WiFi.dnsIP().toString().c_str());
    out.printf("reconnects: %lu\r\n", static_cast<unsigned long>(_reconnects));
  } else if (_staAttemptActive && _candIdx < _candCount) {
    out.printf("trying:     %s (slot %u)\r\n", _cands[_candIdx].ssid, _cands[_candIdx].slot);
  }
  if (_apActive) {
    out.printf("ap:         %s at %s, %u client(s), %s%s\r\n", _apSsid, WiFi.softAPIP().toString().c_str(), WiFi.softAPgetStationNum(),
               _apPass[0] ? "wpa2" : "open", _apPersistent ? ", always on" : "");
  }
  out.printf("timeouts:   attempt %lu s, reconnect %lu s%s, ap retry %s\r\n", static_cast<unsigned long>(_settings.staTimeoutMs / 1000),
             static_cast<unsigned long>(_settings.reconnectTimeoutMs / 1000), _settings.reconnectTimeoutMs == 0 ? " (AP immediately)" : "",
             _settings.apRetryIntervalMs == 0 ? "never" : (String(_settings.apRetryIntervalMs / 1000) + " s").c_str());
  if (scanRunning()) {
    out.println("scan:       running");
  }
  out.println("networks:");
  printNetworks(out);
}
