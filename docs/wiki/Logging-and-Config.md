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

`config list` shows every key with secrets masked. A value with spaces goes in quotes: `config set city "New York"`. `config set` validates WiFi keys the same way the WiFi tab does, and accepts only letters, digits and `-` in a hostname.

## Project settings

Your project's own settings live beside the library's keys. Define them in `setup()`, after `base.begin(cfg)`:

```cpp
static const SettingDef kSettings[] = {
    {.key = "api_key", .label = "API key", .kind = SettingKind::Secret},
    {.key = "units", .label = "Units", .kind = SettingKind::Choice, .def = "metric", .choices = "metric,imperial"},
    {.key = "city", .label = "City", .def = "Berlin", .hint = "a town", .maxLen = 40},
};
for (const SettingDef& d : kSettings) base.settings().add(d);
base.settings().onChange([](const char* key) { LOG_I("setting %s changed", key); });
```

| Field | Default | Meaning |
| --- | --- | --- |
| `key` | | The NVS key: 1 to 15 of `a-z`, `0-9` and `_`, and not one of the library's keys. |
| `label` | | Shown on the page. |
| `kind` | `Text` | `Text`, `Secret` or `Choice`. A secret is write-only: the page, the API and the console tell only whether it is set. |
| `def` | `""` | The value while nothing is stored. A `Choice` needs one of its choices here. |
| `choices` | `""` | `Choice` only: `"a,b,c"`. |
| `hint` | `""` | The field's placeholder on the page. |
| `maxLen` | `63` | Bytes, 1 to 255. |
| `validate` | none | `bool (*)(const String& value, String& err)`, a check of your own. It runs on the loop task and on the web server's task, so it must not touch state. |

- `add()` returns false and logs an error for a bad or duplicate definition, and for the 25th.
- `get(key)` returns the stored value or the default. It also returns the default for a stored value the definition no longer allows, after an update changed the choices or `maxLen`. `set()` and `clear()` are for the loop task; `queue()` hands changes over from any other task.
- `onChange` callbacks run on the loop task after a value changed. Do not log a secret's value there.
- The page shows the settings in a **Settings** tab, which is there only when at least one is defined. The console lists them with `settings`, and `config get|set|list` accept their keys.
- The values are in the `espbase` namespace, so `factory-reset` erases them with everything else.

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
| `consolePath` | `"/"` | Where the console page is served. Another path such as `"/console"` frees `/` for a portal of your own, and `""` serves no page. The API and `/ws` stay either way. See [Your own portal](Console-and-API.md#your-own-portal). |
| `maxWsClients` | `4` | Console clients before the oldest is dropped. |
| `apLandingTab` | `""` | The tab the page opens on while the device is its own access point: `""` (Console), `"wifi"`, `"settings"` or `"device"`. |
| `appPage`, `appPageTitle` | `""`, `"App"` | A page of the project's own, linked from the tab bar. It must be under `/app/`, which the console login covers. See [A page of your own](Console-and-API.md#a-page-of-your-own). |
| `webPassword` | `""` | Console, API and upload password (user `admin`), 8 to 63 characters; empty = open. |
| `otaEnabled`, `otaPassword` | `true`, `""` | OTA switch. The password protects espota, and `POST /update` while no `webPassword` is set. OTA stays off on a partition table with one app slot. |
| `otaTrialMs` | `30000` | How long an updated firmware must run before it is kept. A reset sooner returns to the previous firmware. `0` keeps it once `loop()` runs. |
| `logOutput`, `logBufferBytes`, `logLevel` | `nullptr`, `4096`, `Info` | Log sinks and default level. |
| `fwVersion` | `FW_VERSION`, else `"0.0.0"` | Shown by `info` and `/api/info`. Pass `-DFW_VERSION=\"x.y.z\"`. |
