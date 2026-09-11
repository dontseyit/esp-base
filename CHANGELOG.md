# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

## [1.0.0] - 2026-09-10

### Added

- `EspBase` facade: `begin(cfg)` / `loop()` wiring for every module, status LED patterns per WiFi state, deferred `reboot()` and `factoryReset()`.
- `Log`: `LOG_E/W/I/D` printf style macros with millis and level prefix, optional serial sink (any `Print*`), 4 KB ring buffer replayed to new console clients, runtime level persisted as `log_lvl`, compile time gating with `ESPBASE_LOG_MAX_LEVEL`.
- `ConfigStore`: mutex protected `Preferences` wrapper, namespace `espbase`, typed get/set, `erase()` for factory reset, reusable by projects with their own namespace.
- `WifiManager`: event driven state machine (STA connecting, connected, reconnecting with 1..30 s backoff, AP fallback, AP only). Up to five stored networks are tried in slot order without a scan, so hidden networks work, and retry rounds continue from AP mode. Radio modes `sta` / `ap` / `apsta` and the three retry timers are persisted and changeable at runtime (0 = disabled). Also captive portal DNS, `wifi set|add|remove|forget|reconnect|mode` without a reboot, async scan, mDNS `<hostname>.local` and `onConnected` / `onDisconnected` / `onApStarted` callbacks.
- `WebConsole`: `AsyncWebServer` + `AsyncWebSocket` at `/ws` with a gzipped single page UI (live logs, level filter, command history, WiFi setup, OTA upload and info tabs). Routes `GET /api/info`, `GET /api/log`, `GET|POST|DELETE /api/wifi`, `POST /api/wifi/settings` and `GET|POST /api/wifi/scan`. Captive portal redirects for Android, Apple, Windows and Firefox probes. Command registry with the built-ins `help`, `info`, `heap`, `reboot`, `factory-reset`, `wifi`, `config` and `log`.
- `OtaUpdater`: ArduinoOTA (espota, port 3232) and `POST /update` uploads from a browser or curl as user `admin`, protected by the console password or, without one, the OTA password. Progress goes through `Log`; `ESPBASE_NO_OTA` compiles the module out.
- App framework: the `App` interface (begin/open/pause/resume/close/tick/draw/input) and `REGISTER_APP` static-init registration into an intrusive, order-independent registry. `AppManager` runs each app at its tick and draw rates, draws dirty apps into a project-defined `Frame`, and keeps a navigation stack (push/pop with result/replace/popToRoot) whose transitions wait for the end of the loop iteration. It also handles overlays, hidden services, background ticking, an input queue with an ISR entry point, an idle timeout, a tick budget with overrun logging, a persisted `boot_app`, the `app` console command and `/api/apps`. The example has a text frame, a launcher, a clock, a confirm dialog and a heartbeat service.
- `ChipInfo`: the only place with `SOC_*` / `CONFIG_IDF_TARGET` checks (`cores()`, `hasBle()`, `hasUsbCdc()`, `hasPsramSupport()`, `name()`, `appCore()`).
- `tools/build_web.py` web asset pipeline with a deterministic gzip and a zlib independent `--check` mode.
- Host unit tests for the request checks (`pio test -e native`).
- GitHub Actions: stale asset guard, build matrix for esp32, esp32s3, esp32c3, esp32c6, esp32s2, warning guard, release job attaching example binaries on `v*` tags.

### Security

- Optional console password (`webPassword`, persisted as `web_pass`): HTTP digest auth for the page, the API and `POST /update`, with a 30 s lockout after five failures.
- Origin checks on `/api/`, `/ws` and `/update` against cross-site request forgery and WebSocket hijacking, and a `Host` allowlist against DNS rebinding while no password is set.
- Single-use WebSocket tickets from `GET /api/ws-ticket`.
- Hash-based Content-Security-Policy for the page, plus `X-Frame-Options`, `X-Content-Type-Options` and `Referrer-Policy` headers.
- 1 KB limit on JSON request bodies.
- OTA uploads are authorized before the first flash write, run one at a time and abort when the client disconnects.
- Command arguments are not logged, and commands that carry passwords stay out of stored browser history.
- Control characters in log lines are replaced, and invalid UTF-8 never reaches a WebSocket.
- Hostnames set from the console are validated as RFC 1123 labels.
- Boot warnings while the console or OTA has no password.
- CI actions pinned to commit SHAs with a read-only default token; PlatformIO and the platform (pioarduino 55.03.311) pinned.
