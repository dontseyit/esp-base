# ESP-BASE

Common ground for ESP32 projects. A fresh ESP32, ESP32-S3 or ESP32-C3 board gets, in two lines of code:

- WiFi station with AP fallback and captive portal, up to five stored networks, three radio modes and mDNS.
- A console on the serial port and in the browser: live logs, commands, WiFi setup and firmware upload.
- OTA updates through espota or the browser, with optional passwords.
- Persistent settings in NVS, structured logging with a ring buffer, NTP time.
- An optional app framework with a navigation stack for building the product on top.

esp-base is a framework rather than a library: it owns the boot sequence, the loop, the WiFi driver, the web server and the console, and your project plugs in through a config struct, callbacks, commands, routes and apps. Pins, display, input, partitions and USB flags stay with your project. Everything lives in the `espbase` namespace, and each optional module has a compile-time switch.

The same code builds for ESP32, ESP32-S3 and ESP32-C3 (hardware tested) and for ESP32-C6 and ESP32-S2 (build tested).

## Documentation

The [wiki](docs/wiki/Home.md) explains how esp-base works and how to build on it.

| Page | Covers |
| --- | --- |
| [Getting started](docs/wiki/Getting-Started.md) | PlatformIO and Arduino IDE setup, first boot, firmware updates, build flags |
| [Architecture](docs/wiki/Architecture.md) | modules, tasks, the loop, rules, what the library sets on the core, `ChipInfo` |
| [WiFi](docs/wiki/WiFi.md) | modes, connection rounds, timers, captive portal |
| [Console and API](docs/wiki/Console-and-API.md) | commands, HTTP routes, WebSocket protocol, bringing your own UI |
| [Security](docs/wiki/Security.md) | passwords, request checks, what stays with your project |
| [Apps](docs/wiki/Apps.md) | writing apps, lifecycle, scheduling, navigation |
| [Logging and config](docs/wiki/Logging-and-Config.md) | logging, `ConfigStore`, every key and config field |
| [Development](docs/wiki/Development.md) | building, web UI, tests, CI, releases |
| [Troubleshooting](docs/wiki/Troubleshooting.md) | known symptoms and fixes |

## Requirements

| Component | Version |
| --- | --- |
| Arduino core | 3.x. PlatformIO: [pioarduino](https://github.com/pioarduino/platform-espressif32) 55.03.311 (Arduino core 3.3.11, ESP-IDF 5.5.5); the rolling `stable` release also works. Arduino IDE: the `esp32` boards package 3.x. |
| Libraries | `ESP Async WebServer` ^3.12 and `Async TCP` ^3.5 by ESP32Async, `ArduinoJson` ^7.4. PlatformIO installs them with esp-base; in the Arduino IDE install them from the Library Manager. |

An older core or another chip family stops the build with one line saying so.

## Quick start

**PlatformIO:** copy [`examples/Blank`](examples/Blank/). It is a complete project: a `platformio.ini` with the platform pin, the OTA partition table and the USB flags per chip, and this sketch:

```cpp
#include <EspBase.h>

using namespace espbase;

EspBaseConfig cfg;
EspBase base;

void setup() {
  Serial.begin(115200);
  cfg.hostname = "my-device";     // http://my-device.local/
  cfg.apSsidPrefix = "MyDevice";  // fallback AP "MyDevice-XXXX"
  cfg.logOutput = &Serial;        // serial sink for LOG_x
  base.begin(cfg);                // returns at once; the network comes up from loop()
}

void loop() {
  base.loop();
  base.console().poll(Serial);  // "help" on the serial port lists the commands
}
```

Pick a board in `platformio.ini` and run `pio run -t upload`. Pin a release in `lib_deps` (`https://github.com/dontseyit/esp-base.git#2.0.0`).

**Arduino IDE:** clone this repository into your `libraries` folder (or install the release zip with **Sketch > Include Library > Add .ZIP Library**), install the three dependencies from the Library Manager, then **File > Examples > EspBase > Blank**. Pick an ESP32 board and the **Minimal SPIFFS** partition scheme, which gives two OTA slots.

On first boot no network is stored, so the device starts the access point `MyDevice-XXXX`. Join it, pick a network in the **WiFi** tab of the captive portal, and the console moves to `http://my-device.local/`. From there:

```cpp
base.console().addCommand("led",
    [](const CmdArgs& a, Print& out) { out.println(a.is(0, "on") ? "on" : "off"); },
    "led <on|off>");                                                   // serial and browser
base.wifi().onConnected([] { LOG_I("up: %s", WiFi.localIP().toString().c_str()); });
base.web().on("/api/state", HTTP_GET, [](AsyncWebServerRequest* r) { r->send(200, "application/json", "{}"); });
```

[`examples/Minimal`](examples/Minimal/) adds the app framework: a text frame, a launcher, a clock, a dialog and a background service.

## Compile-time options

| Flag | Effect |
| --- | --- |
| `-DESPBASE_NO_WEB` | No HTTP server, WebSocket or page. Leaves WiFi, NVS, the serial console and espota. |
| `-DESPBASE_NO_OTA` | No OTA at all. |
| `-DESPBASE_NO_APPS` | No app framework. |
| `-DESPBASE_LOG_MAX_LEVEL=3` | Compile `LOG_D` out (0 = none, 4 = debug). |
| `-DFW_VERSION=\"1.2.3\"` | Firmware version shown by `info` and `/api/info`. |

`cfg.webUi = false` keeps `/api` and the WebSocket but does not serve the built-in page, for a project with its own UI.

## Security

The web interface has no TLS. Set `webPassword`, `otaPassword` and `apPassword` before shipping, because esp-base ships no default passwords. [Security](docs/wiki/Security.md) lists the protections and what stays with your project.

## Development

```sh
pio run                                   # examples/Minimal for the tier 1 targets
pio test -e native                        # host unit tests
python tools/build_web.py --check         # generated web header matches webui/
python tools/check_version.py             # every version pin agrees
```

See [Development](docs/wiki/Development.md) for the web UI workflow, CI and releases, and [CONTRIBUTING.md](CONTRIBUTING.md).

## License

esp-base is released under the [MIT License](LICENSE).

Firmware built with it also contains ESPAsyncWebServer and AsyncTCP (LGPL-3.0), ArduinoJson (MIT) and the Arduino ESP32 core (LGPL-2.1 or later). If you distribute firmware binaries, the terms of those licenses apply to them as well.
