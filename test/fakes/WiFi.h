// A host stand-in for the WiFi driver. It records what the library asks of it
// and hands events to the library's handler the way the network event task
// does. The tests play the network: they set what the air holds (the network
// joined, scan results) and call emit().
#pragma once

#include <Arduino.h>

#include <functional>
#include <vector>

enum arduino_event_id_t {
  ARDUINO_EVENT_WIFI_STA_GOT_IP,
  ARDUINO_EVENT_WIFI_STA_DISCONNECTED,
  ARDUINO_EVENT_WIFI_STA_LOST_IP,
  ARDUINO_EVENT_WIFI_AP_START,
  ARDUINO_EVENT_WIFI_AP_STOP,
  ARDUINO_EVENT_WIFI_AP_STACONNECTED,
  ARDUINO_EVENT_WIFI_AP_STADISCONNECTED,
};
struct arduino_event_info_t {
  struct {
    uint8_t reason;
  } wifi_sta_disconnected;
};
enum wifi_mode_t { WIFI_OFF, WIFI_STA, WIFI_AP, WIFI_AP_STA };
enum wl_status_t { WL_IDLE_STATUS, WL_CONNECTED, WL_CONNECT_FAILED, WL_DISCONNECTED };
enum wifi_auth_mode_t {
  WIFI_AUTH_OPEN,
  WIFI_AUTH_WEP,
  WIFI_AUTH_WPA_PSK,
  WIFI_AUTH_WPA2_PSK,
  WIFI_AUTH_WPA_WPA2_PSK,
  WIFI_AUTH_WPA2_ENTERPRISE,
  WIFI_AUTH_WPA3_PSK,
  WIFI_AUTH_WPA2_WPA3_PSK,
};
enum wifi_err_reason_t { WIFI_REASON_UNSPECIFIED = 1 };
enum wifi_interface_t { WIFI_IF_STA };
enum esp_mac_type_t { ESP_MAC_WIFI_STA };
using esp_err_t = int;
#define ESP_OK 0
#define WIFI_SCAN_RUNNING (-1)
#define WIFI_SCAN_FAILED (-2)

inline esp_err_t esp_read_mac(uint8_t* mac, esp_mac_type_t) {
  const uint8_t m[6] = {0x24, 0x6F, 0x28, 0x00, 0x2B, 0x90};
  memcpy(mac, m, sizeof(m));
  return ESP_OK;
}
inline esp_err_t esp_wifi_get_mac(wifi_interface_t, uint8_t* mac) { return esp_read_mac(mac, ESP_MAC_WIFI_STA); }

struct FakeWiFi {
  struct Net {
    const char* ssid;
    int32_t rssi;
    int32_t ch;
    wifi_auth_mode_t auth;
  };

  std::function<void(arduino_event_id_t, arduino_event_info_t)> handler;
  wifi_mode_t currentMode = WIFI_OFF;
  std::vector<std::string> begins;  // every network begin() was asked to join, in order
  int disconnects = 0;
  std::string apSsid;  // the access point's name while it is up
  std::string ssid;    // the network the station is on
  std::vector<Net> nets;
  int16_t scanState = WIFI_SCAN_RUNNING;
  int scans = 0;

  void emit(arduino_event_id_t id, uint8_t reason = 0) {
    arduino_event_info_t info{};
    info.wifi_sta_disconnected.reason = reason;
    if (handler) {
      handler(id, info);
    }
  }

  void persistent(bool) {}
  void setAutoReconnect(bool) {}
  void setHostname(const char*) {}
  void onEvent(std::function<void(arduino_event_id_t, arduino_event_info_t)> cb) { handler = std::move(cb); }
  bool mode(wifi_mode_t m) {
    currentMode = m;
    return true;
  }
  wl_status_t begin(const char* s, const char*) {
    begins.push_back(s);
    return WL_DISCONNECTED;  // the answer comes as an event
  }
  bool disconnectAsync(bool, bool) {
    ++disconnects;
    return true;
  }
  bool softAP(const char* name, const char*, int, int, int) {
    apSsid = name;
    emit(ARDUINO_EVENT_WIFI_AP_START);
    return true;
  }
  bool softAPdisconnect(bool) {
    apSsid.clear();
    emit(ARDUINO_EVENT_WIFI_AP_STOP);
    return true;
  }
  IPAddress softAPIP() { return IPAddress(192, 168, 4, 1); }
  uint8_t softAPgetStationNum() { return 0; }
  String SSID() { return String(ssid.c_str()); }
  int8_t RSSI() { return -50; }
  int32_t channel() { return 6; }
  IPAddress localIP() { return IPAddress(192, 168, 1, 50); }
  IPAddress gatewayIP() { return IPAddress(192, 168, 1, 1); }
  IPAddress subnetMask() { return IPAddress(255, 255, 255, 0); }
  IPAddress dnsIP() { return IPAddress(192, 168, 1, 1); }
  String BSSIDstr() { return String("00:11:22:33:44:55"); }
  String macAddress() { return String("24:6F:28:00:2B:90"); }
  const char* disconnectReasonName(wifi_err_reason_t) { return "test"; }

  void setScanActiveMinTime(uint32_t) {}
  int16_t scanNetworks(bool, bool, bool, uint32_t) {
    ++scans;
    scanState = WIFI_SCAN_RUNNING;
    return WIFI_SCAN_RUNNING;
  }
  int16_t scanComplete() { return scanState; }
  void scanDelete() {}
  String SSID(int i) { return String(nets[i].ssid); }
  int32_t RSSI(int i) { return nets[i].rssi; }
  int32_t channel(int i) { return nets[i].ch; }
  wifi_auth_mode_t encryptionType(int i) { return nets[i].auth; }
};
inline FakeWiFi WiFi;

struct FakeNetwork {
  void begin() {}
};
inline FakeNetwork Network;
