# Logging and config

[Home](Home.md) / Logging and config

## Logging

```cpp
LOG_E("sensor %d failed: %s", id, why);
LOG_W(...);  LOG_I(...);  LOG_D(...);
```

Each line looks like `[   12345][I] message`: milliseconds since boot, then the level.

| Sink | Details |
| --- | --- |
| Serial | Any `Print*` set in `cfg.logOutput`. Nothing is printed when it is null. |
| Ring buffer | 4 KB by default (`logBufferBytes`). `log dump` and `GET /api/log` print it. |
| Console | Streamed from the ring by the loop task, replayed to every new client. |

- Safe from any task. Calls from an ISR are ignored.
- Messages longer than 240 characters are cut and end in `...`.
- Runtime level: `log level d` in the console, persisted as `log_lvl`, or `cfg.logLevel` as the default. `-DESPBASE_LOG_MAX_LEVEL=3` removes `LOG_D` at compile time (0 = none, 4 = debug).
- Lines logged before `base.begin()` are kept in the ring and reach the console later. They are not printed to Serial.
- The Arduino core's own `log_x` messages bypass `Log` and go to the UART; their level is set with `CORE_DEBUG_LEVEL`.
- USB CDC boards: call `Serial.setTxTimeoutMs(0)` after `Serial.begin()`. Otherwise writes wait for a USB host and stall `loop()`.

## ConfigStore

A thread safe wrapper over `Preferences`. Use your own instance and namespace:

```cpp
ConfigStore settings;
settings.begin("myapp");                  // NVS namespace, 15 characters max
int32_t n = settings.getInt("count", 0);  // default when the key is missing
settings.setInt("count", n + 1);
settings.setString("name", "kitchen");
settings.has("name");  settings.remove("name");  settings.erase();
```

Types: `String`, `int32_t`, `uint8_t`, `bool`. Keys are limited to 15 characters. `base.config()` is the library's own `espbase` namespace; keep your keys out of it. `factory-reset` erases only `espbase`.

## Key reference

Namespace `espbase`. A stored key overrides the matching `EspBaseConfig` field.

| Key | Set by | Takes effect |
| --- | --- | --- |
| `sta_ssid`, `sta_pass` | `wifi set`, WiFi tab | immediately |
| `sta2_ssid` .. `sta5_pass` | `wifi add`, WiFi tab | from the next connection attempt |
| `wifi_mode` | `wifi mode`, WiFi tab | immediately |
| `sta_timeout`, `reconn_timeout`, `ap_retry` | WiFi tab, `config set` (ms) | immediately |
| `hostname` | `config set` | after a reboot |
| `ap_pass` | `config set` | after a reboot |
| `web_pass` | `config set` | after a reboot |
| `log_lvl` | `log level` | immediately |
| `boot_app` | `app boot`, replacing the root app | next start |

`config list` shows every key with secrets masked. `config set` validates WiFi keys the same way the WiFi tab does, and accepts only letters, digits and `-` in a hostname.

## EspBaseConfig

| Field | Default | Meaning |
| --- | --- | --- |
| `hostname` | `"esp-base"` | DHCP and mDNS name. |
| `apSsidPrefix` | `"EspBase"` | AP SSID is `prefix-XXXX`. |
| `apPassword` | `""` | Empty for an open AP, else 8 to 63 characters. |
| `wifiMode` | `WifiMode::Sta` | `Sta`, `Ap` or `ApSta`. See [WiFi](WiFi.md). |
| `staTimeoutMs` | `20000` | Per network connect attempt. |
| `reconnectTimeoutMs` | `60000` | Retries after a lost link before the AP; 0 = AP at once. |
| `apRetryIntervalMs` | `60000` | Retry rounds in AP fallback; 0 = never. |
| `statusLedPin`, `statusLedActiveLow` | `-1`, `false` | Optional status LED. |
| `httpPort` | `80` | Web console port. |
| `maxWsClients` | `4` | Console clients before the oldest is dropped. |
| `webPassword` | `""` | Console, API and upload password (user `admin`), 8 to 63 characters; empty = open. |
| `otaEnabled`, `otaPassword` | `true`, `""` | OTA switch. The password protects espota, and `POST /update` while no `webPassword` is set. |
| `logOutput`, `logBufferBytes`, `logLevel` | `nullptr`, `4096`, `Info` | Log sinks and default level. |
| `fwVersion` | `FW_VERSION`, else `"0.0.0"` | Shown by `info` and `/api/info`. Pass `-DFW_VERSION=\"x.y.z\"`. |
