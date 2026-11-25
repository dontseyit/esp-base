# Architecture

[Home](Home.md) / Architecture

## Modules

| Module | Responsibility | Source |
| --- | --- | --- |
| `EspBase` | Facade. Owns the modules, runs `begin()` and `loop()`, built-in commands, status LED, deferred reboot. | `EspBase.*` |
| `Log` | `LOG_x` macros, serial sink, ring buffer. | `Log.*` |
| `ConfigStore` | Mutex protected wrapper over `Preferences` (NVS). | `ConfigStore.*` |
| `ProjectSettings` | A project's own settings: definitions, values through `ConfigStore`, hand-over from other tasks, change callbacks. | `ProjectSettings.*`, `SettingsCheck.h` |
| `DeviceStatus` | A project's own values and buttons for the Device tab. | `DeviceStatus.*` |
| `WifiManager` | Radio modes, stored networks, state machine, AP with captive DNS, mDNS, scan. | `WifiManager.*`, `WifiNaming.h`, `ApNotice.h` |
| `WebConsole` | HTTP server, WebSocket, command registry, `/api` routes, embedded page. | `WebConsole.*`, `web/index_html_gz.h` |
| `OtaUpdater` | ArduinoOTA, `POST /update` and the trial that rolls back a failing update. Removed by `-DESPBASE_NO_OTA`. | `OtaUpdater.*` |
| `AppRegistry`, `AppManager` | App registration, lifecycle, scheduling, navigation. Removed by `-DESPBASE_NO_APPS`. | `App.h`, `AppRegistry.cpp`, `AppManager.*` |
| `ChipInfo` | The only place with `SOC_*` and `CONFIG_IDF_TARGET` checks. | `ChipInfo.h` |

Reach them through `base.wifi()`, `base.console()`, `base.web()`, `base.config()`, `base.settings()`, `base.status()`, `base.apps()` and `base.ota()`.

`ChipInfo` is usable from project code too:

```cpp
ChipInfo::cores();      // 1 or 2
ChipInfo::hasBle();     // SOC_BLE_SUPPORTED
ChipInfo::hasOwnWifi(); // SOC_WIFI_SUPPORTED: false where WiFi is a second chip (ESP32-P4)
ChipInfo::hasUsbCdc();  // USB Serial/JTAG or USB OTG present
ChipInfo::name();       // "esp32s3"
ChipInfo::appCore();    // core the Arduino loop runs on: 1 on dual core, 0 on single core
```

## Tasks and hand-offs

Only the loop task changes library state. The other tasks hand their work over.

```mermaid
flowchart LR
  EV["network event task: WiFi.onEvent"] -->|event queue| WM
  TCP["async_tcp task: HTTP and WebSocket"] -->|command queue| WC
  TCP -->|credentials, settings, flags| WM
  ANY["any task: LOG_x"] -->|mutex| RING[(log ring)]
  RING -->|per-client cursor| WC
  subgraph LOOP["loop task: base.loop()"]
    WM[WifiManager] --> WC[WebConsole] --> AM[AppManager] --> OT[OtaUpdater]
  end
```

What this means for your code:

- Console commands run on the loop task. They may call any library API.
- HTTP handlers run on the async_tcp task. Keep them short and never block. For anything that changes device state, set a flag or push to a queue and act on it in `loop()`, the way the built-in routes do.
- `LOG_x` is safe from any task because logging never touches the network stack. It is not allowed from an ISR.
- Every route under `/api/` and `/app/`, plus `/ws` and `/update`, passes the origin check and, with a password, the login before its handler runs. Put routes that change state under `/api/` and pages of your own under `/app/`.

## One loop iteration

`base.loop()` runs these in order, and none of them waits:

1. `WifiManager` starts the driver on the first call, drains WiFi events, applies queued credential and setting changes, runs its timers.
2. `ProjectSettings` applies the changes the web server handed over and runs the change callbacks.
3. `WebConsole` handles WebSocket connects, runs pending commands, streams new log lines, drops the oldest client beyond the limit.
4. `AppManager` delivers input, ticks apps, draws, applies one navigation transition, checks the idle timeout.
5. `OtaUpdater` keeps an updated firmware once its trial is over, runs `ArduinoOTA.handle()` and reboots after a finished web upload.
6. Status LED and any scheduled reboot.

`begin()` returns within a few milliseconds. The WiFi driver starts on the first `loop()` call. Where WiFi is a second chip that one call takes about 2 s (measured on an M5Stack Tab5), because the other chip is brought up first.

## Rules

The library follows these, and code built on it should too:

1. Nothing blocks. No `delay()` in loop paths, no waiting for the network.
2. The library never touches `Serial`. It writes to the `Print*` in `cfg.logOutput`.
3. No GPIO numbers in `src/`. Pins come from the project.
4. Chip differences go through `ChipInfo` and `SOC_*` capability macros, never chip names. No literal core ids; use `ChipInfo::appCore()`.
5. Partitions, PSRAM and USB flags belong to the project.
6. Warning free with `-Wall -Wextra` on every target.

## Resource budget

Measured on the bundled example:

| Item | Size |
| --- | --- |
| Static RAM | 39 to 54 KB depending on the chip |
| Flash (`firmware.bin`) | 1.08 to 1.27 MB |
| Web page | about 8 KB gzipped, budget 20 KB |
| Log ring | 4 KB (`logBufferBytes`) |
| WebSocket clients | 4 (`maxWsClients`) |

Most of the flash is the Arduino core and its WiFi stack. A sketch that only calls `WiFi.begin()` is already 0.78 to 0.94 MB on the same platform. The example adds 0.29 to 0.35 MB to that (ESP32 / ESP32-C6):

| Part | Adds |
| --- | --- |
| esp-base's own code, the page included | 81 / 95 KB |
| ESPAsyncWebServer and AsyncTCP | 66 / 77 KB |
| String literals of everything in this table | 32 / 38 KB |
| mDNS | 30 / 36 KB |
| OTA: ArduinoOTA with its Hash library, Update | 20 / 24 KB |
| Arduino network classes, lwIP, captive DNS, Preferences, the example sketch | 63 / 77 KB |

`-DESPBASE_NO_APPS -DESPBASE_NO_OTA` removes about 50 KB. Partition choices are in [Getting started](Getting-Started.md#partition-table).

## Design decisions

- The AP is open by default and the console login is optional, so first-time setup needs no password printed on the device. Set `apPassword` and `webPassword` before shipping; see [Security](Security.md).
- ArduinoJson parses the WebSocket and API input and builds `/api/info`. Hand-rolled parsing would be riskier.
- Versioning rules are in [Development](Development.md#versioning-and-release).
