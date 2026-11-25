// Host tests for the WiFi state machine: pio test -e native
//
// WifiManager.cpp is compiled unchanged against test/fakes. The fake driver
// records what the library asks of it; these tests play the network (events
// and the clock) and check what the library does next.
#include <ArduinoJson.h>
#include <unity.h>

#include "../../src/ConfigStore.cpp"
#include "../../src/Log.cpp"
#include "../../src/WifiManager.cpp"

static EspBaseConfig cfg;
static ConfigStore store;
static int connected, disconnected, apStarted, scansDone;

void setUp() {
  fakeNvs.clear();
  WiFi = FakeWiFi();
  fakeMillis = 1000;
  cfg = EspBaseConfig();
  store.begin();
  connected = disconnected = apStarted = scansDone = 0;
}
void tearDown() {}

static void storeNet(uint8_t slot, const char* ssid, const char* pass = "") {
  char key[16];
  store.setString(EspBaseKeys::slotSsidKey(slot, key, sizeof(key)), ssid);
  store.setString(EspBaseKeys::slotPassKey(slot, key, sizeof(key)), pass);
}

// begin(), the callbacks counted, and the first loop(), which starts the driver.
static void boot(WifiManager& wm) {
  wm.begin(cfg, store);
  wm.onConnected([] { ++connected; });
  wm.onDisconnected([] { ++disconnected; });
  wm.onApStarted([] { ++apStarted; });
  wm.onScanDone([] { ++scansDone; });
  wm.loop();
}

// The loop running for ms, a pass every 50 ms.
static void run(WifiManager& wm, uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 50) {
    fakeMillis += 50;
    wm.loop();
  }
}

static void event(WifiManager& wm, arduino_event_id_t id) {
  WiFi.emit(id);
  wm.loop();
}

static void joined(WifiManager& wm, const char* ssid) {
  WiFi.ssid = ssid;
  event(wm, ARDUINO_EVENT_WIFI_STA_GOT_IP);
}

// Every network the station was told to join, in order.
static std::string tried() {
  std::string s;
  for (const std::string& b : WiFi.begins) {
    s += (s.empty() ? "" : ",") + b;
  }
  return s;
}

#define ASSERT_STATE(name, wm) TEST_ASSERT_EQUAL_STRING(name, (wm).stateName())
#define ASSERT_TRIED(list) TEST_ASSERT_EQUAL_STRING(list, tried().c_str())

static void test_without_networks_the_ap_opens() {
  WifiManager wm;
  boot(wm);
  ASSERT_STATE("ap_fallback", wm);
  TEST_ASSERT_EQUAL_STRING("EspBase-2B90", WiFi.apSsid.c_str());
  ASSERT_TRIED("");
  TEST_ASSERT_EQUAL(1, apStarted);
  // Hosted WiFi restarts the AP while setting it up: still one notice.
  WiFi.emit(ARDUINO_EVENT_WIFI_AP_STOP);
  WiFi.emit(ARDUINO_EVENT_WIFI_AP_START);
  wm.loop();
  TEST_ASSERT_EQUAL(1, apStarted);
}

static void test_the_first_network_connects_and_no_ap_opens() {
  storeNet(1, "home", "password1");
  storeNet(2, "office");
  WifiManager wm;
  boot(wm);
  ASSERT_STATE("sta_connecting", wm);
  ASSERT_TRIED("home");
  joined(wm, "home");
  ASSERT_STATE("sta_connected", wm);
  TEST_ASSERT_EQUAL(1, connected);
  TEST_ASSERT_TRUE(WiFi.apSsid.empty());
  TEST_ASSERT_EQUAL_STRING("home", wm.staSsid().c_str());
}

static void test_a_round_tries_each_network_then_opens_the_ap() {
  cfg.staTimeoutMs = 5000;
  storeNet(1, "home");
  storeNet(2, "office");
  WifiManager wm;
  boot(wm);
  run(wm, 5000);  // home does not answer
  TEST_ASSERT_EQUAL(1, WiFi.disconnects);
  ASSERT_TRIED("home");
  event(wm, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);  // that disconnect is done: the next network at once
  ASSERT_TRIED("home,office");
  event(wm, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);  // office refuses
  ASSERT_STATE("ap_fallback", wm);
  TEST_ASSERT_EQUAL_STRING("EspBase-2B90", WiFi.apSsid.c_str());
  TEST_ASSERT_EQUAL(1, apStarted);
}

static void test_a_lost_link_backs_off_then_opens_the_ap() {
  cfg.reconnectTimeoutMs = 10000;
  storeNet(1, "home");
  WifiManager wm;
  boot(wm);
  joined(wm, "home");
  event(wm, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  ASSERT_STATE("sta_reconnecting", wm);
  TEST_ASSERT_EQUAL(1, disconnected);
  run(wm, 1000);
  ASSERT_TRIED("home,home");  // 1 s
  event(wm, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  run(wm, 1950);
  ASSERT_TRIED("home,home");
  run(wm, 50);
  ASSERT_TRIED("home,home,home");  // 2 s later
  event(wm, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  run(wm, 4000);
  ASSERT_TRIED("home,home,home,home");  // 4 s later
  TEST_ASSERT_TRUE(WiFi.apSsid.empty());
  run(wm, 3000);  // 10 s since the link was lost
  ASSERT_STATE("ap_fallback", wm);
  TEST_ASSERT_FALSE(WiFi.apSsid.empty());
}

static void test_reconnecting_starts_with_the_network_that_was_up() {
  storeNet(1, "home");
  storeNet(2, "office");
  WifiManager wm;
  boot(wm);
  event(wm, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);  // home refuses
  run(wm, 1000);
  joined(wm, "office");
  event(wm, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);  // and office drops
  run(wm, 1000);
  ASSERT_TRIED("home,office,office");
  event(wm, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  run(wm, 2000);
  ASSERT_TRIED("home,office,office,home");  // then they take turns
}

static void test_without_reconnect_time_the_ap_opens_at_once() {
  cfg.reconnectTimeoutMs = 0;
  storeNet(1, "home");
  WifiManager wm;
  boot(wm);
  joined(wm, "home");
  event(wm, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  ASSERT_STATE("ap_fallback", wm);
  TEST_ASSERT_FALSE(WiFi.apSsid.empty());
  run(wm, 1000);
  ASSERT_TRIED("home,home");  // and the station keeps trying next to it
}

static void test_from_the_ap_rounds_repeat_and_the_ap_closes_once_joined() {
  cfg.apRetryIntervalMs = 10000;
  storeNet(1, "home");
  WifiManager wm;
  boot(wm);
  event(wm, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);  // home refuses: the round is over
  ASSERT_STATE("ap_fallback", wm);
  run(wm, 9950);
  ASSERT_TRIED("home");
  run(wm, 50);
  ASSERT_TRIED("home,home");
  event(wm, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);  // that round fails too: the next one waits its turn
  run(wm, 9950);
  ASSERT_TRIED("home,home");
  run(wm, 50);
  ASSERT_TRIED("home,home,home");
  joined(wm, "home");
  ASSERT_STATE("sta_connected", wm);
  TEST_ASSERT_EQUAL(1, wm.reconnectCount());
  run(wm, 2950);
  TEST_ASSERT_FALSE(WiFi.apSsid.empty());  // a moment for the page to say where the device went
  run(wm, 50);
  TEST_ASSERT_TRUE(WiFi.apSsid.empty());
  TEST_ASSERT_EQUAL(WIFI_STA, WiFi.currentMode);
}

static void test_mode_apsta_keeps_the_ap() {
  cfg.wifiMode = WifiMode::ApSta;
  storeNet(1, "home");
  WifiManager wm;
  boot(wm);
  joined(wm, "home");
  run(wm, 5000);
  ASSERT_STATE("sta_connected", wm);
  TEST_ASSERT_FALSE(WiFi.apSsid.empty());
}

static void test_mode_changes_apply_without_a_reboot() {
  storeNet(1, "home");
  WifiManager wm;
  boot(wm);
  joined(wm, "home");
  String err;
  TEST_ASSERT_TRUE(wm.applySetting("wifi_mode", "apsta", err));
  wm.loop();
  TEST_ASSERT_FALSE(WiFi.apSsid.empty());  // the AP comes up next to the link
  ASSERT_STATE("sta_connected", wm);
  TEST_ASSERT_TRUE(wm.applySetting("wifi_mode", "sta", err));
  wm.loop();
  TEST_ASSERT_TRUE(WiFi.apSsid.empty());  // and goes again
  ASSERT_STATE("sta_connected", wm);
  TEST_ASSERT_EQUAL(0, disconnected);
}

static void test_mode_ap_never_joins() {
  cfg.wifiMode = WifiMode::Ap;
  storeNet(1, "home");
  WifiManager wm;
  boot(wm);
  ASSERT_STATE("ap_only", wm);
  TEST_ASSERT_FALSE(WiFi.apSsid.empty());
  wm.reconnect();
  run(wm, 5000);
  ASSERT_TRIED("");
}

static void test_new_credentials_drop_the_link_first_then_join() {
  storeNet(1, "home");
  WifiManager wm;
  boot(wm);
  joined(wm, "home");
  TEST_ASSERT_TRUE(wm.setCredentials("cafe", "password2", 2));
  wm.loop();
  TEST_ASSERT_EQUAL(1, WiFi.disconnects);
  TEST_ASSERT_EQUAL(1, disconnected);
  ASSERT_TRIED("home");  // not while still associated: the core would block for a second
  event(wm, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  ASSERT_TRIED("home,cafe");  // the slot it was given, not slot 1
  joined(wm, "cafe");
  ASSERT_STATE("sta_connected", wm);
}

static void test_network_slots() {
  WifiManager wm;
  boot(wm);
  TEST_ASSERT_EQUAL(1, wm.addCredentials("a", ""));
  wm.loop();
  ASSERT_TRIED("a");  // a network added while the AP is up is tried at once
  TEST_ASSERT_EQUAL(2, wm.addCredentials("b", "password1"));
  TEST_ASSERT_EQUAL(1, wm.addCredentials("a", "password9"));  // a known network keeps its slot
  TEST_ASSERT_EQUAL(3, wm.addCredentials("c", ""));
  TEST_ASSERT_EQUAL(4, wm.addCredentials("d", ""));
  TEST_ASSERT_EQUAL(5, wm.addCredentials("e", ""));
  TEST_ASSERT_EQUAL(0, wm.addCredentials("f", ""));  // all five in use
  TEST_ASSERT_EQUAL(0, wm.addCredentials("", ""));
  TEST_ASSERT_EQUAL(0, wm.addCredentials("x", "short"));  // a password is 8..63
  TEST_ASSERT_EQUAL(0, wm.addCredentials("012345678901234567890123456789012", ""));  // 33 characters
  TEST_ASSERT_TRUE(wm.removeCredentials(2));
  TEST_ASSERT_EQUAL(2, wm.addCredentials("g", ""));  // the first free slot
  WifiManager::Slot slots[5];
  TEST_ASSERT_EQUAL(5, wm.listCredentials(slots, 5));
  TEST_ASSERT_EQUAL_STRING("a", slots[0].ssid.c_str());
  TEST_ASSERT_TRUE(slots[0].hasPassword);
  TEST_ASSERT_EQUAL_STRING("g", slots[1].ssid.c_str());
  TEST_ASSERT_FALSE(slots[1].hasPassword);
}

static void test_forgetting_every_network_opens_the_ap() {
  storeNet(1, "home");
  WifiManager wm;
  boot(wm);
  joined(wm, "home");
  TEST_ASSERT_TRUE(wm.forgetCredentials());
  wm.loop();
  ASSERT_STATE("ap_fallback", wm);
  TEST_ASSERT_EQUAL(1, disconnected);
  TEST_ASSERT_FALSE(WiFi.apSsid.empty());
  TEST_ASSERT_FALSE(wm.hasCredentials());
}

static void test_settings_are_checked_stored_and_applied() {
  storeNet(1, "home");
  WifiManager wm;
  boot(wm);
  joined(wm, "home");
  const struct {
    const char* key;
    const char* value;
    bool ok;
  } cases[] = {
      {"sta_timeout", "3000", true},       {"sta_timeout", "300000", true},     {"sta_timeout", "2999", false},
      {"sta_timeout", "300001", false},    {"sta_timeout", "abc", false},       {"sta_timeout", "-1", false},
      {"sta_timeout", "", false},          {"reconn_timeout", "0", true},       {"reconn_timeout", "3600000", true},
      {"reconn_timeout", "3600001", false}, {"ap_retry", "0", true},            {"ap_retry", "5000", true},
      {"ap_retry", "4999", false},         {"wifi_mode", "wlan", false},        {"sta2_ssid", "", false},
      {"sta2_pass", "short", false},       {"sta2_pass", "", true},             {"hostname", "dev", false},
  };
  String err;
  for (const auto& c : cases) {
    const String what = String(c.key) + "=" + c.value;
    TEST_ASSERT_EQUAL_MESSAGE(c.ok, wm.applySetting(c.key, c.value, err), what.c_str());
  }
  // A mode is stored in one spelling and applied without a reboot.
  TEST_ASSERT_TRUE(wm.applySetting("wifi_mode", "AP", err));
  TEST_ASSERT_EQUAL_STRING("ap", store.getString("wifi_mode").c_str());
  wm.loop();
  ASSERT_STATE("ap_only", wm);
  TEST_ASSERT_EQUAL(1, disconnected);
  TEST_ASSERT_FALSE(WiFi.apSsid.empty());
}

static void test_stored_settings_are_held_to_their_ranges() {
  store.setInt("sta_timeout", 100);
  store.setInt("reconn_timeout", -5);
  store.setInt("ap_retry", 1);
  store.setString("wifi_mode", "nonsense");
  WifiManager wm;
  boot(wm);
  TEST_ASSERT_EQUAL(3000, wm.settings().staTimeoutMs);
  TEST_ASSERT_EQUAL(0, wm.settings().reconnectTimeoutMs);
  TEST_ASSERT_EQUAL(5000, wm.settings().apRetryIntervalMs);
  TEST_ASSERT_EQUAL_STRING("sta", WifiManager::modeName(wm.settings().mode));
}

static void test_one_radio_job_at_a_time() {
  storeNet(1, "home");
  WifiManager wm;
  boot(wm);
  TEST_ASSERT_FALSE(wm.startScan());  // a connection attempt has the radio
  event(wm, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);  // home refuses: the AP, nothing in flight
  TEST_ASSERT_TRUE(wm.startScan());
  TEST_ASSERT_FALSE(wm.startScan());  // one scan at a time
  wm.loop();
  TEST_ASSERT_EQUAL(1, WiFi.scans);
  wm.reconnect();
  wm.loop();
  ASSERT_TRIED("home");  // the attempt waits: esp_wifi_connect() would abort the scan
  WiFi.nets = {{"weak", -80, 1, WIFI_AUTH_OPEN}, {"strong", -40, 6, WIFI_AUTH_WPA2_PSK}};
  WiFi.scanState = 2;
  wm.loop();
  TEST_ASSERT_EQUAL(1, scansDone);
  TEST_ASSERT_FALSE(wm.scanRunning());
  TEST_ASSERT_EQUAL_STRING(R"([{"ssid":"strong","rssi":-40,"ch":6,"enc":true},{"ssid":"weak","rssi":-80,"ch":1,"enc":false}])",
                           wm.scanResultJson().c_str());
  run(wm, 250);
  ASSERT_TRIED("home,home");
}

static void test_radio_off_and_on() {
  storeNet(1, "home");
  WifiManager wm;
  boot(wm);
  joined(wm, "home");
  wm.setEnabled(false);
  wm.loop();
  ASSERT_STATE("off", wm);
  TEST_ASSERT_EQUAL(WIFI_OFF, WiFi.currentMode);
  TEST_ASSERT_EQUAL(1, disconnected);
  joined(wm, "home");  // a late event of the radio that went away
  ASSERT_STATE("off", wm);
  TEST_ASSERT_EQUAL(1, connected);
  wm.setEnabled(true);
  wm.loop();
  ASSERT_STATE("sta_connecting", wm);
  ASSERT_TRIED("home,home");
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_without_networks_the_ap_opens);
  RUN_TEST(test_the_first_network_connects_and_no_ap_opens);
  RUN_TEST(test_a_round_tries_each_network_then_opens_the_ap);
  RUN_TEST(test_a_lost_link_backs_off_then_opens_the_ap);
  RUN_TEST(test_reconnecting_starts_with_the_network_that_was_up);
  RUN_TEST(test_without_reconnect_time_the_ap_opens_at_once);
  RUN_TEST(test_from_the_ap_rounds_repeat_and_the_ap_closes_once_joined);
  RUN_TEST(test_mode_apsta_keeps_the_ap);
  RUN_TEST(test_mode_changes_apply_without_a_reboot);
  RUN_TEST(test_mode_ap_never_joins);
  RUN_TEST(test_new_credentials_drop_the_link_first_then_join);
  RUN_TEST(test_network_slots);
  RUN_TEST(test_forgetting_every_network_opens_the_ap);
  RUN_TEST(test_settings_are_checked_stored_and_applied);
  RUN_TEST(test_stored_settings_are_held_to_their_ranges);
  RUN_TEST(test_one_radio_job_at_a_time);
  RUN_TEST(test_radio_off_and_on);
  return UNITY_END();
}
