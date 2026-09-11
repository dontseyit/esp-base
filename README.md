# ESP-BASE

A reusable Arduino library that is _trying to_ give every ESP32 project the same baseline out of the box:

- WiFi station with AP fallback and captive portal, up to five stored networks tried in order (hidden ones included), reconnect with backoff, credentials set from the browser or console without a reboot, three radio modes (station with AP fallback, AP only, AP always on) and mDNS `<hostname>.local`.
- A browser console served from flash as one gzipped page, with live logs over WebSocket, a command line with history, WiFi setup, firmware upload and a device info view.
- OTA through ArduinoOTA (`espota`) and `POST /update` from a browser or `curl`, with an optional password.
- Persistent config through a small typed wrapper over `Preferences` that you can reuse for your own settings.
- Structured logging with `LOG_E/W/I/D`: millis and level prefix, serial sink, ring buffer and a runtime level.
- An app framework. Apps are built into the firmware, registered at static-init time and run from `loop()` at declared tick and draw rates, with a navigation stack, overlays, hidden services and deferred transitions. The drawing surface and the input source stay in the project.

The same code builds for ESP32, ESP32-S3 and ESP32-C3 (hardware tested) and for ESP32-C6 and ESP32-S2 (build tested). Projects consume it through `lib_deps`.

The [wiki](docs/wiki/Home.md) explains how it works and how to develop with it. This README is the complete reference.

## Requirements

| Component | Version |
| --- | --- |
| Platform | [pioarduino](https://github.com/pioarduino/platform-espressif32) 55.03.311 (Arduino core 3.3.11, ESP-IDF 5.5.5). The rolling `stable` release also works. |
| PlatformIO Core | 6.1 or newer (the standard `pip install platformio` works with the pioarduino platform URL) |
| Libraries | `ESP32Async/AsyncTCP` ^3.5, `ESP32Async/ESPAsyncWebServer` ^3.12, `bblanchon/ArduinoJson` ^7.4 |

The ESP32Async libraries are consumed with `lib_compat_mode = strict` and `lib_ldf_mode = chain`, the configuration their own pioarduino example uses. Strict mode keeps the ESP8266 and RP2040 TCP backends, which those libraries declare as platform-filtered dependencies, out of an ESP32 build. Because of strict mode, `library.json` declares `"platforms": "espressif32"` and `"frameworks": "arduino"`.

## Quick start

`platformio.ini` of your project:

```ini
[env]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip
framework = arduino
lib_compat_mode = strict
lib_ldf_mode = chain
lib_deps =
  https://github.com/<you>/esp-base.git#v1.0.0
build_flags =
  -DFW_VERSION=\"1.2.3\"
board_build.partitions = min_spiffs.csv   ; two 1.9 MB OTA slots
monitor_speed = 115200

[env:esp32s3]
board = esp32-s3-devkitc-1
build_flags = ${env.build_flags} -DARDUINO_USB_CDC_ON_BOOT=1

[env:esp32c3]
board = esp32-c3-devkitm-1
build_flags = ${env.build_flags} -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1

[env:esp32]
board = esp32dev
```

`src/main.cpp`:

```cpp
#include <EspBase.h>

EspBaseConfig cfg;
EspBase base;

void setup() {
  Serial.begin(115200);
  cfg.hostname     = "my-device";   // also the mDNS name
  cfg.apSsidPrefix = "MyDevice";    // AP SSID = prefix + "-" + last 4 hex of the MAC
  cfg.apPassword   = "";            // empty = open AP
  cfg.wifiMode     = WifiMode::Sta; // Sta (AP as fallback), Ap (AP only), ApSta (AP always on)
  cfg.staTimeoutMs = 20000;         // per stored network, before trying the next one
  cfg.statusLedPin = -1;            // -1 = none, the project supplies the pin
  cfg.httpPort     = 80;
  cfg.otaEnabled   = true;
  cfg.otaPassword  = "";
  cfg.webPassword  = "";            // console password, user "admin"; empty = open console
  cfg.logOutput    = &Serial;       // serial sink for LOG_x (optional)

  base.begin(cfg);                  // returns without waiting for the network

  base.console().addCommand("led",
    [](const CmdArgs& a, Print& out) { out.println("ok"); },
    "led <on|off>");

  base.web().on("/api/state", HTTP_GET,
    [](AsyncWebServerRequest* r) { r->send(200, "application/json", "{}"); });

  base.wifi().onConnected([] { LOG_I("up: %s", WiFi.localIP().toString().c_str()); });
}

void loop() { base.loop(); }
```

On first boot no credentials are stored, so the device starts an access point named `MyDevice-XXXX`. Join it and the captive portal opens (or browse to `http://192.168.4.1/`), then pick a network in the **WiFi** tab. After that the console lives at `http://my-device.local/`. Backup networks, the radio mode and the retry timers are all set from the same tab.

The complete example is in [`examples/minimal`](examples/minimal/src/main.cpp); it also shows how to feed commands typed on Serial into the same registry.

## API rules

- Nothing blocks. `begin()` sets things up and returns; the WiFi driver starts on the first `loop()` call and connecting happens from `loop()` and WiFi events.
- The library never touches `Serial`; the only serial output is the `Print*` you hand to `cfg.logOutput`.
- The library contains no GPIO numbers. Pins, USB CDC flags, partition tables and PSRAM settings belong to the project.

## Configuration reference

All fields of `EspBaseConfig` with their defaults:

| Field | Default | Meaning |
| --- | --- | --- |
| `hostname` | `"esp-base"` | DHCP hostname and mDNS name. The persisted `hostname` key overrides it. |
| `apSsidPrefix` | `"EspBase"` | AP SSID is `prefix-XXXX` with the last two MAC bytes. |
| `apPassword` | `""` | Empty = open AP, otherwise 8..63 characters (WPA2). The persisted `ap_pass` key overrides it. |
| `wifiMode` | `WifiMode::Sta` | `Sta`: station, AP only as fallback. `Ap`: AP only, station disabled. `ApSta`: AP always on next to the station. Persisted key `wifi_mode` (`sta`, `ap`, `apsta`) overrides it. |
| `staTimeoutMs` | `20000` | Give up on one stored network after this and try the next (3 s .. 300 s). Key `sta_timeout`. |
| `reconnectTimeoutMs` | `60000` | After a lost link: retry with 1, 2, 4 .. 30 s backoff for this long, then AP fallback. `0` = AP immediately, retries continue next to it. Key `reconn_timeout`. |
| `apRetryIntervalMs` | `60000` | In AP fallback: retry every stored network this often (5 s .. 1 h). `0` = never, until `wifi reconnect`. Key `ap_retry`. |
| `statusLedPin` | `-1` | Status LED, `-1` = none. |
| `statusLedActiveLow` | `false` | LED polarity. |
| `httpPort` | `80` | Web console port. |
| `maxWsClients` | `4` | Beyond this the oldest WebSocket client is dropped. |
| `webPassword` | `""` | Password for the page, the API and `POST /update` (HTTP digest auth, user `admin`, 8..63 characters). Empty = open console. The persisted `web_pass` key overrides it. |
| `otaEnabled` | `true` | Runtime switch for espota and `POST /update`. |
| `otaPassword` | `""` | espota password. It also protects `POST /update` (HTTP basic auth, user `admin`) while no `webPassword` is set. |
| `logOutput` | `nullptr` | Serial sink for the log, e.g. `&Serial` after `Serial.begin()`. |
| `logBufferBytes` | `4096` | Ring buffer replayed to new console clients. |
| `logLevel` | `LogLevel::Info` | Default level; the persisted `log_lvl` key overrides it. |
| `fwVersion` | `FW_VERSION` macro or `"0.0.0"` | Reported by `/api/info` and `info`. Pass `-DFW_VERSION=\"x.y.z\"`. |

Status LED patterns: fast blink while connecting or reconnecting, slow blink in AP mode, a short blink every 3 s when connected.

### Stored networks and connection rounds

Up to five networks are stored in slots. Slot 1 is the primary (`sta_ssid` / `sta_pass`); slots 2 to 5 use `sta2_ssid` / `sta2_pass` and so on. A connection *round* tries every stored network in slot order, each for at most `staTimeoutMs`; nothing depends on a scan, so hidden networks work. When a round fails the AP comes up (mode `sta`) and rounds repeat every `apRetryIntervalMs` with the AP staying up. After a lost link the network that was connected is retried first with backoff, then the others, for `reconnectTimeoutMs`; after that the AP joins in. Adding a network while in AP mode starts a round at once.

## Console

Open `http://<hostname>.local/` (STA) or `http://192.168.4.1/` (AP). Tabs: **Console** (log pane with level filter, text filter, autoscroll, command input with history), **WiFi** (stored networks with connect/remove, primary and backup entry, scan, radio mode and retry timers), **Update** (firmware upload), **Info** (`/api/info` as JSON).

### Commands

| Command | Description |
| --- | --- |
| `help` | List commands. |
| `info` | Chip, memory, network and version summary. |
| `heap` | Heap and PSRAM statistics. |
| `reboot` | Restart. |
| `factory-reset` | Erase the `espbase` namespace (credentials, hostname, AP and console passwords, log level) and restart. |
| `wifi status` | State, mode, SSID, IP, RSSI, AP details, timers and the stored networks. |
| `wifi list` | Stored networks by slot; `*` marks the connected one. |
| `wifi scan` | Start an asynchronous scan; results are logged. |
| `wifi set <ssid> [pass]` | Store as slot 1 (primary) and connect to it now. Quote names with spaces: `wifi set "My Net" secret`. |
| `wifi add <ssid> [pass]` | Store in the first free slot as a backup without touching the current connection (updates the password if the SSID is already stored). |
| `wifi remove <slot>` | Remove one stored network. |
| `wifi forget` | Remove all stored networks and switch to AP mode. |
| `wifi reconnect [slot]` | Start a new round now, optionally at a given slot. |
| `wifi mode [sta\|ap\|apsta]` | Show or change the radio mode; applied immediately and persisted. |
| `config list` / `config get <key>` / `config set <key> <value>` | Keys: `sta_ssid`, `sta_pass`, `sta2_ssid` .. `sta5_pass`, `hostname`, `ap_pass`, `web_pass`, `log_lvl`, `wifi_mode`, `sta_timeout`, `reconn_timeout`, `ap_retry` (timers in ms), `boot_app`. Secrets are masked; WiFi keys are validated and applied live. |
| `app ...` | App framework, see [Apps](#apps). |
| `log level [e\|w\|i\|d]` | Show or set the runtime log level (persisted). |
| `log dump` | Print the ring buffer. |

Custom commands: `base.console().addCommand(name, handler, help)`. The handler receives the parsed arguments (`args[0]`, `args.is(0, "on")`, `args.toInt(1)`, `args.raw()`) and a `Print&` for output. Commands run from `loop()` on the main task, so handlers may call any library API. `base.console().execute(line, Serial)` runs a line from anywhere else, for example a serial reader.

### HTTP API

| Route | Description |
| --- | --- |
| `GET /` | The console page, `Content-Encoding: gzip`, `ETag` + `Cache-Control: no-cache` (304 on revalidation). |
| `GET /api/info` | JSON: `chip`, `target`, `rev`, `cores`, `cpuMhz`, `flash`, `heap{free,min,maxAlloc,total}`, `psram{size,free}`, `sketch{size,free}`, `uptime`, `hostname`, `mac`, `wifi{state,ssid,ip,rssi,ap{ssid,ip,clients}}`, `fw`, `build`, `lib`, `core`, `idf`, `reset`, `ota`, `ws`, `log`. |
| `GET /api/log` | The ring buffer as text. |
| `GET /api/ws-ticket` | `{"ticket":"..."}`, a single-use WebSocket ticket valid for 30 s. |
| `GET /api/wifi` | `{"mode","state","ssid","ip","rssi","ap":{...},"settings":{"staTimeoutMs","reconnectTimeoutMs","apRetryIntervalMs"},"maxSlots":5,"networks":[{"slot","ssid","hasPassword"}]}`. |
| `POST /api/wifi` | `{"ssid","pass","slot"?,"connect"?}`: stores (slot 1 by default) and connects. `"connect":false` stores only, in the given slot or the first free one (response carries `"slot"`). `{"slot":n}` alone starts a round at that network. 400 on invalid input. |
| `DELETE /api/wifi?slot=n` | Remove a stored network. |
| `POST /api/wifi/settings` | Any of `{"mode":"sta\|ap\|apsta","staTimeoutMs","reconnectTimeoutMs","apRetryIntervalMs"}`; validated, persisted and applied without a reboot. 400 with an `error` on the first invalid field. |
| `POST /api/wifi/scan` | Start a scan (202, or 409 when busy). |
| `GET /api/wifi/scan` | `{"scanning":bool,"age":seconds,"networks":[{"ssid","rssi","ch","enc"}]}`. |
| `POST /update` | Multipart firmware upload (see OTA). |
| `GET /api/apps` / `POST /api/apps` | App registry, stack and stats; open, replace, close, home, key actions. |
| `GET /ws` | WebSocket. |

Any other path returns 404, except while the AP is up: requests that arrive through the AP interface for a foreign host (the connectivity probes phones and laptops send) are redirected to the portal.

Every route under `/api/`, plus `/ws` and `/update`, passes the origin check and, with a password, the login before its handler runs. See [Security](#security).

### WebSocket protocol

Connect to `/ws?t=<ticket>` with a ticket from `GET /api/ws-ticket`. Each ticket works once, within 30 s. Messages are JSON text frames, and a frame carries either one message object or an array of them.

| Direction | Message |
| --- | --- |
| server to client | `{"t":"log","l":"I","ms":1234,"m":"..."}` with level `E`, `W`, `I` or `D` |
| client to server | `{"t":"cmd","c":"wifi status"}` |
| server to client | `{"t":"out","m":"..."}` the command's output |

On connect the server replays the ring buffer as `log` messages, then streams new lines. A client that stops reading never stalls the device: the lines it misses are dropped, and it gets a `... N log lines dropped` warning once it catches up.

## OTA

Both paths write the other app slot, verify the image and reboot. Progress and errors go through `Log`.

With espota (port 3232), add this to `platformio.ini`:

```ini
upload_protocol = espota
upload_port = my-device.local        ; or the IP
upload_flags = --auth=<otaPassword>  ; only if a password is set
```

From a browser, use the **Update** tab. With curl:

```sh
curl -u admin:<otaPassword> -F "firmware=@.pio/build/esp32/firmware.bin" http://my-device.local/update
```

`POST /update` takes the console password when one is set (`curl --digest -u admin:<webPassword> ...`), otherwise the OTA password. The device checks credentials and origin before the first byte reaches flash, so a wrong password gets 401 and nothing is written. Only one upload runs at a time. `-DESPBASE_NO_OTA` removes the module and its dependencies from the build entirely.

## Logging

```cpp
LOG_E("fmt %d", v);  LOG_W(...);  LOG_I(...);  LOG_D(...);
```

The macros take printf-style arguments and prefix each line with `[   12345][I]`. They are mutex protected and callable from any task, but not from an ISR. Output goes to the optional `Print*`, the ring buffer and the console. The console is fed from `loop()`, so logging never touches the network stack from the caller's task. Messages are cut at 240 characters. `-DESPBASE_LOG_MAX_LEVEL=3` compiles `LOG_D` out (0 = none .. 4 = debug).

## ConfigStore for your own settings

```cpp
ConfigStore appCfg;
appCfg.begin("myapp");                       // any NVS namespace, 15 chars max
int32_t n = appCfg.getInt("count", 0);
appCfg.setString("name", "kitchen");
appCfg.erase();                              // wipe the namespace
```

`base.config()` is the library's own instance (`espbase`).

## ChipInfo

```cpp
ChipInfo::cores();      // 1 or 2
ChipInfo::hasBle();     // SOC_BLE_SUPPORTED
ChipInfo::hasUsbCdc();  // USB Serial/JTAG or USB OTG present
ChipInfo::name();       // "esp32s3"
ChipInfo::appCore();    // core the Arduino loop runs on: 1 on dual core, 0 on single core
```

Every `SOC_*` or `CONFIG_IDF_TARGET` check in the library lives in [`src/ChipInfo.h`](src/ChipInfo.h).

## Apps

The base runs your applications: a registry of apps compiled into the firmware and a manager that schedules them cooperatively from `loop()`. The library never sees a display or a button; the project defines `struct Frame` (a sprite, a canvas, a text grid) and feeds input events.

### Writing an app

```cpp
#include <EspBase.h>
#include "Frame.h"   // your definition

class ClockApp : public App {
 public:
  ClockApp() : App({.id = "clock", .name = "Clock", .tickHz = 1, .drawHz = 2, .order = 10}) {}
  void begin(EspBase& base) override { /* once at start(): register console commands, look up services */ }
  void open(const String& args) override { requestDraw(); }     // now foreground
  void pause() override {}                                       // covered by a pushed app
  void resume(int result) override { requestDraw(); }            // the app above closed
  void close() override {}
  void tick(uint32_t dtMs) override { requestDraw(); }
  void draw(Frame& f) override { /* draw into the shared frame */ }
  bool input(const InputEvent& ev) override {
    if (ev.type == InputEvent::Press && ev.code == InputCode::Select) { manager().push("confirm", "Reboot?"); return true; }
    return false;   // an unconsumed Back press pops the app
  }
};
REGISTER_APP(ClockApp);
```

`AppInfo` fields: `id` (stable, used by the console, the API and the `boot_app` key), `name`, `tickHz` (0 = every loop), `drawHz` (a cap; drawing also needs `requestDraw()`), `order` (registry order, for launchers) and `flags`:

| Flag | Meaning |
| --- | --- |
| `AppTickInBackground` | Keep ticking while another app is on top (timers, players). |
| `AppOverlay` | Draw on top of the app below instead of replacing it (dialogs, toasts). The chain from the topmost opaque app up is redrawn together. |
| `AppHidden` | A service: opened once at start, ticks forever, never on the stack, never draws, cannot be opened. |

### Registration

`REGISTER_APP(Type, ctor args...)` creates a static instance and links it into an intrusive list during static initialisation. The list head is a constant-initialised static, so registration order does not matter; the registry sorts by `order`, then `id`, on first use. `AppRegistry::add(app)` does the same explicitly for people who prefer to see the list in `setup()`.

Registration only runs for object files the linker keeps, so apps belong in the project's `src/` (linked as objects) or in a library built with `lib_archive = false`. Constructors must not log either, because the `Log` global may not exist yet.

### Running

```cpp
static Frame frame;
base.begin(cfg);
base.apps().setIdleTimeout(120000);                       // optional: back to the root after 2 min without input
base.apps().start(&frame, [](Frame& f) { present(f); }, "home");
```

`start()` calls `begin()` on every app, opens the services, and opens the root app: the persisted `boot_app` if set, else the `homeId` given, else the first visible app. Headless devices can pass no frame: apps still tick, but `draw()` is never called.

Each loop iteration: queued input goes to the foreground app, every due app ticks (foreground always; background only with `AppTickInBackground`; services always), the draw chain redraws when something is dirty and the foreground's `drawHz` allows, `present()` is called once, then at most one pending transition is applied, then the idle timeout is checked. Ticks are timed; a tick slower than the budget (`setTickBudget`, default 20 ms) counts as an overrun and is logged, rate limited.

### Navigation

`manager().push(id, args)`, `pop(result)`, `replace(id, args)` and `popToRoot()` validate immediately (unknown id, service, already open, stack full, root cannot be popped, another transition pending) and are applied at the end of the iteration, so an app can close itself from `tick()` or `input()`. `pop(result)` delivers the result to the app below through `resume(result)`. The stack holds 8 entries. The root app is persisted as `boot_app` whenever it changes, so a device using `replace()` from a launcher comes back to the same app after a reboot; `app boot -` clears it.

Input: `base.apps().input(ev)` from any task, `inputFromISR(ev)` from an interrupt, `key(code)` for a plain press. `InputCode::Back` is the only code the manager interprets; everything else is between the project and its apps.

### Console and API

`app list|stack|stats|open <id> [args]|replace <id> [args]|close [result]|home|boot [id|-]|key back|select|up|down|left|right|<code> [value]`, and `GET /api/apps` (registry, stack, stats) plus `POST /api/apps` with `{"action":"open|replace|close|home|key", ...}`. `app key` makes the example usable without any hardware.

### The example

[`examples/minimal`](examples/minimal/src/) defines a 20x4 character `Frame` that is logged whenever it changes, and four apps in [`apps.cpp`](examples/minimal/src/apps.cpp): a launcher as root, a clock that redraws every second, an overlay confirmation dialog that returns a result, and a hidden heartbeat service the clock reads. Launcher UI and navigation depend on the hardware, so they live in the example.

## Security

The web interface has no TLS, so treat it as a LAN service. Inside that limit, esp-base protects it in these ways:

- `webPassword`, or `config set web_pass` followed by a reboot, protects the page, every `/api/` route and `POST /update` with HTTP digest auth, so the password never crosses the network in clear text. After 5 wrong passwords, logins are refused for 30 s.
- The WebSocket takes a single-use ticket from the authenticated API instead of HTTP credentials.
- Requests to `/api/`, `/ws` and `/update` whose `Origin` names another site get 403. A web page the user happens to visit therefore cannot drive the device (cross-site request forgery and WebSocket hijacking). Without a password the `Host` header must also be an IP address, `<hostname>` or `<hostname>.local`, which stops DNS rebinding. With a password set, any host name works.
- The page is served with a hash-based Content-Security-Policy, `X-Frame-Options: DENY`, `X-Content-Type-Options: nosniff` and `Referrer-Policy: no-referrer`. The UI writes text with `textContent` only.
- JSON request bodies are capped at 1 KB and incoming WebSocket messages at 512 bytes.
- Passwords are never logged or returned. `config` masks every `*_pass` key, only command names reach the log, and the browser keeps password-carrying commands out of its stored history. Control characters in log lines are replaced, and invalid UTF-8 is replaced before it reaches a WebSocket.
- The device logs a warning at boot while the console or OTA has no password.

These stay with the project:

- Set `webPassword`, `otaPassword` and `apPassword` before shipping. esp-base ships no default password, so first-time setup works without one printed on the device.
- NVS stores the WiFi and console passwords in plain text. Enable flash encryption with NVS encryption if someone could read the flash.
- esp-base does not check OTA image signatures. Use Secure Boot V2 if firmware authenticity matters.
- The serial console is trusted. Anyone with physical access can run commands, including `factory-reset`, which is also the way back from a forgotten password.

## Build flags worth knowing

| Flag | Where | Why |
| --- | --- | --- |
| `-DARDUINO_USB_CDC_ON_BOOT=1` | S3, C3, C6 projects | Route `Serial` to the USB port. |
| `-DARDUINO_USB_MODE=1` | C3, C6 projects | Required with the flag above; those chips only have the USB Serial/JTAG peripheral and the board manifests do not set it. The S3 board manifest already does. |
| `-DCONFIG_ASYNC_TCP_RUNNING_CORE=1` | dual core projects | Pin the async_tcp task to the application core (AsyncTCP default: any core). |
| `-DCONFIG_ASYNC_TCP_STACK_SIZE=4096` | optional | AsyncTCP defaults to 16 KB. |
| `-DESPBASE_NO_OTA` | optional | Compile the OTA module out. |
| `-DESPBASE_LOG_MAX_LEVEL=3` | optional | Compile `LOG_D` out. |
| `-DFW_VERSION=\"1.2.3\"` | recommended | Firmware version shown by `info` and `/api/info`. |

Projects that want lower network latency can call `WiFi.setSleep(false)` after `begin()`; the library keeps the core's default modem sleep.

USB CDC boards: call `Serial.setTxTimeoutMs(0)` after `Serial.begin()` when `ARDUINO_USB_CDC_ON_BOOT` is set. Otherwise every write blocks for up to 100 ms while no host is attached, and each log line would stall `loop()` on an unplugged board. The example does this.

## Design notes

- WiFi events are queued from the network event task and processed in `loop()`; the state machine never polls `WiFi.status()`. Messages logged before `begin()` are kept in a default 4 KB ring.
- WebSocket and HTTP callbacks run on the async_tcp task and only queue work (commands, client connects); command execution, log streaming and client cleanup happen in `loop()`. Log writes never call into the network stack, which avoids lock inversions between the log mutex and AsyncTCP.
- Whenever the AP is up the STA interface stays enabled, so scans work and retry rounds need no mode change. A retry can interrupt the AP for a second or two. In mode `ap` no station attempt is ever made; in mode `apsta` the AP is never stopped.
- The persisted keys `hostname`, `ap_pass` and `web_pass` take effect after a reboot; `sta_ssid` / `sta_pass` apply immediately through `wifi set` or `POST /api/wifi`.

## Decisions on the open questions

1. Open AP by default. `apPassword` defaults to empty; set it in `cfg` or persist `ap_pass` to require WPA2.
2. Console login is optional. `webPassword` turns on HTTP digest auth for the whole web interface. It stays empty by default so first-time setup needs no printed password. See [Security](#security).
3. ArduinoJson stays. It parses the WebSocket and `/api/wifi` input safely and builds `/api/info`; hand-rolled parsing would be riskier.
4. Versioning. Adding a field with a default to `EspBaseConfig`, a command, a route or a JSON field is a minor release. Renaming or removing a field, changing a default that alters behaviour, changing a persisted key, or changing the WebSocket message shapes is a major release. Bug fixes are patch releases.

## Development

```sh
pip install platformio
python tools/build_web.py           # webui/ -> src/web/index_html_gz.h (commit the result)
python tools/build_web.py --check   # what CI runs: fails if the header is stale
pio run                             # tier 1 envs; pio run -e esp32c6 -e esp32s2 for tier 2
pio test -e native                  # host unit tests for the request checks
```

Layout:

```
src/            library (EspBase facade, Log, ConfigStore, WifiManager, WebConsole, OtaUpdater, ChipInfo, App framework)
src/web/        generated gzipped page (committed, consumers never run scripts)
webui/          index.html, app.js, style.css
tools/          build_web.py
test/           host unit tests (pio test -e native)
examples/minimal
.github/workflows/ci.yml   asset guard, build matrix, warning guard, release on v* tags
```

Cross chip checklist for reviews: no GPIO numbers in `src/`, no literal core ids (use `ChipInfo::appCore()`), no direct `Serial` calls, feature checks only through `SOC_*` capability macros inside `ChipInfo.h`, warning free on every env.

Releasing: bump `version` in `library.json`, update `CHANGELOG.md`, tag `vX.Y.Z`. CI attaches `esp-base-minimal-<env>.bin` for every env to the GitHub release.

## License

esp-base is released under the [MIT License](LICENSE).

Firmware built with it also contains ESPAsyncWebServer and AsyncTCP (LGPL-3.0), ArduinoJson (MIT) and the Arduino ESP32 core (LGPL-2.1 or later). If you distribute firmware binaries, the terms of those licenses apply to them as well.
