// Persisted keys of the "espbase" NVS namespace and the helpers around them.
// Pure C++, no Arduino dependency, so they are unit tested on the host.
//
// The key names are API: a stored value survives firmware updates, so a
// renamed key loses settings on devices in the field. Renames are major
// releases with a migration.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace espbase {
namespace Keys {

constexpr const char* Namespace = "espbase";
constexpr const char* StaSsid = "sta_ssid";  // slot 1 (primary network)
constexpr const char* StaPass = "sta_pass";
constexpr const char* Hostname = "hostname";
constexpr const char* ApPass = "ap_pass";
constexpr const char* WebPass = "web_pass";  // console password, applied after reboot
constexpr const char* LogLevelKey = "log_lvl";
constexpr const char* WifiModeKey = "wifi_mode";         // sta | ap | apsta
constexpr const char* StaTimeout = "sta_timeout";        // ms per connect attempt
constexpr const char* ReconnTimeout = "reconn_timeout";  // ms of retries after link loss before AP fallback, 0 = AP immediately
constexpr const char* ApRetry = "ap_retry";              // ms between retry rounds in AP mode, 0 = never
constexpr const char* BootApp = "boot_app";              // root app id restored by AppManager::start()

constexpr size_t kMaxKeyLen = 15;  // NVS limit for keys and namespaces

// Stored networks live in slots 1..kMaxStaSlots. Slot 1 uses sta_ssid/sta_pass,
// slots 2.. use sta<n>_ssid/sta<n>_pass (e.g. sta2_ssid). Tried in slot order.
constexpr uint8_t kMaxStaSlots = 5;
const char* slotSsidKey(uint8_t slot, char* buf, size_t len);
const char* slotPassKey(uint8_t slot, char* buf, size_t len);
// Recognises slot keys; sets slot (1..kMaxStaSlots) and whether it is the password key.
bool isSlotKey(const char* key, uint8_t& slot, bool& isPass);

// Keys the built-in "config" command knows about, nullptr terminated.
extern const char* const All[];
// Keys whose values are masked in console output (every *_pass key).
bool isSecret(const char* key);

}  // namespace Keys
}  // namespace espbase
