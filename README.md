# ESP-BASE

A reusable Arduino library that gives every ESP32 project the same baseline out of the box:

- WiFi station with AP fallback and captive portal, up to five stored networks, three radio modes and mDNS.
- A browser console served from flash, with live logs, commands, WiFi setup and firmware upload.
- OTA updates through espota or the browser, with optional passwords.
- Persistent settings over NVS, and structured logging with a ring buffer.
- An app framework with a navigation stack for building the product on top.

The same code builds for ESP32, ESP32-S3 and ESP32-C3 (hardware tested) and for ESP32-C6 and ESP32-S2 (build tested). Pins, display, input, partitions and USB flags stay in your project.

## Documentation

The [wiki](docs/wiki/Home.md) explains how esp-base works and how to build on it.

| Page | Covers |
| --- | --- |
| [Getting started](docs/wiki/Getting-Started.md) | `platformio.ini`, first boot, firmware updates, build flags |
| [Architecture](docs/wiki/Architecture.md) | modules, tasks, the loop, rules, `ChipInfo` |
| [WiFi](docs/wiki/WiFi.md) | modes, connection rounds, timers, captive portal |
| [Console and API](docs/wiki/Console-and-API.md) | commands, HTTP routes, WebSocket protocol |
| [Security](docs/wiki/Security.md) | passwords, request checks, what stays with your project |
| [Apps](docs/wiki/Apps.md) | writing apps, lifecycle, scheduling, navigation |
| [Logging and config](docs/wiki/Logging-and-Config.md) | logging, `ConfigStore`, every key and config field |
| [Development](docs/wiki/Development.md) | building, web UI, tests, CI, releases |
| [Troubleshooting](docs/wiki/Troubleshooting.md) | known symptoms and fixes |

## Requirements

| Component | Version |
| --- | --- |
| Platform | [pioarduino](https://github.com/pioarduino/platform-espressif32) 55.03.311 (Arduino core 3.3.11, ESP-IDF 5.5.5). The rolling `stable` release also works. |
| PlatformIO Core | 6.1 or newer |
| Libraries | `ESP32Async/AsyncTCP` ^3.5, `ESP32Async/ESPAsyncWebServer` ^3.12, `bblanchon/ArduinoJson` ^7.4 |

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
  cfg.hostname     = "my-device";   // http://my-device.local/
  cfg.apSsidPrefix = "MyDevice";    // fallback AP "MyDevice-XXXX"
  cfg.webPassword  = "";            // console password, user "admin"; empty = open console
  cfg.logOutput    = &Serial;       // optional serial sink for LOG_x
  base.begin(cfg);                  // returns without waiting for the network

  base.console().addCommand("led",
    [](const CmdArgs& a, Print& out) { out.println(a.is(0, "on") ? "on" : "off"); },
    "led <on|off>");
  base.wifi().onConnected([] { LOG_I("up: %s", WiFi.localIP().toString().c_str()); });
}

void loop() { base.loop(); }
```

On first boot no network is stored, so the device starts the access point `MyDevice-XXXX`. Join it, pick a network in the **WiFi** tab of the captive portal, and the console moves to `http://my-device.local/`. [Getting started](docs/wiki/Getting-Started.md) explains each line and how to update firmware, and [`examples/minimal`](examples/minimal/src/) is a complete project with example apps.

## Security

The web interface has no TLS. Set `webPassword`, `otaPassword` and `apPassword` before shipping, because esp-base ships no default passwords. [Security](docs/wiki/Security.md) lists the protections and what stays with your project.

## Development

```sh
python tools/build_web.py --check   # generated web header matches webui/
pio run                             # tier 1 envs; -e esp32c6 -e esp32s2 for tier 2
pio test -e native                  # host unit tests
```

See [Development](docs/wiki/Development.md) for the web UI workflow, CI and releases.

## License

esp-base is released under the [MIT License](LICENSE).

Firmware built with it also contains ESPAsyncWebServer and AsyncTCP (LGPL-3.0), ArduinoJson (MIT) and the Arduino ESP32 core (LGPL-2.1 or later). If you distribute firmware binaries, the terms of those licenses apply to them as well.
