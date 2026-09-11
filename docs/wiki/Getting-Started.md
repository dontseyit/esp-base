# Getting started

[Home](Home.md) / Getting started

## 1. Add the library

`platformio.ini` of your project:

```ini
[env]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip
framework = arduino
lib_compat_mode = strict
lib_ldf_mode = chain
lib_deps =
  https://github.com/dontseyit/esp-base.git#v1.0.1
build_flags =
  -DFW_VERSION=\"1.2.3\"
board_build.partitions = min_spiffs.csv
monitor_speed = 115200

[env:esp32s3]
board = esp32-s3-devkitc-1
build_flags = ${env.build_flags} -DARDUINO_USB_CDC_ON_BOOT=1

[env:esp32c3]
board = esp32-c3-devkitm-1
build_flags = ${env.build_flags} -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1
```

Why these lines:

- `lib_compat_mode = strict` and `lib_ldf_mode = chain` keep the ESP8266 and RP2040 TCP backends that ESPAsyncWebServer declares out of an ESP32 build.
- `min_spiffs.csv` gives two 1.9 MB app slots. A typical build is 1.1 to 1.25 MB and OTA needs two slots.
- The USB flags route `Serial` to the USB port. The C3 and C6 also need `ARDUINO_USB_MODE=1`.

## 2. Minimal sketch

```cpp
#include <EspBase.h>

EspBaseConfig cfg;
EspBase base;

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);         // do not stall when no USB host is attached
#endif
  cfg.hostname     = "my-device";   // http://my-device.local/
  cfg.apSsidPrefix = "MyDevice";    // fallback AP "MyDevice-XXXX"
  cfg.logOutput    = &Serial;       // optional serial sink for the log
  // cfg.webPassword = "<8+ chars>"; // console password (user "admin"), set it before shipping
  base.begin(cfg);                  // returns immediately
}

void loop() { base.loop(); }
```

All config fields are listed in [Logging and config](Logging-and-Config.md#espbaseconfig).

## 3. First boot

1. No network is stored, so the device opens the access point `MyDevice-XXXX` (open unless `cfg.apPassword` is set).
2. Join it. The captive portal opens, or browse to `http://192.168.4.1/`.
3. In the **WiFi** tab pick a network and press **Save as primary & connect**. The AP stays up until the station connects, then closes after 3 s.
4. The console now lives at `http://my-device.local/`, or at the IP printed in the log.

## 4. Update the firmware

| Path | How |
| --- | --- |
| USB | `pio run -t upload` |
| espota | `upload_protocol = espota`, `upload_port = my-device.local`, add `upload_flags = --auth=<password>` if one is set |
| Browser | **Update** tab, choose `.pio/build/<env>/firmware.bin` |
| curl | `curl -u admin:<password> -F "firmware=@.pio/build/esp32/firmware.bin" http://my-device.local/update`, add `--digest` when a console password is set |

The device writes the other app slot, verifies the image and reboots into it. Uploads take the console password when one is set, otherwise `cfg.otaPassword`. A wrong password is rejected before anything is written.

## Build flags

| Flag | Where | Why |
| --- | --- | --- |
| `-DARDUINO_USB_CDC_ON_BOOT=1` | S3, C3, C6 projects | Route `Serial` to the USB port. |
| `-DARDUINO_USB_MODE=1` | C3, C6 projects | Required with the flag above: those chips only have the USB Serial/JTAG peripheral and their board manifests do not set it. The S3 manifest already does. |
| `-DCONFIG_ASYNC_TCP_RUNNING_CORE=1` | dual core projects | Pin the async_tcp task to the application core (AsyncTCP default: any core). |
| `-DCONFIG_ASYNC_TCP_STACK_SIZE=4096` | optional | AsyncTCP defaults to 16 KB. |
| `-DESPBASE_NO_OTA` | optional | Compile the OTA module out. |
| `-DESPBASE_LOG_MAX_LEVEL=3` | optional | Compile `LOG_D` out. |
| `-DFW_VERSION=\"1.2.3\"` | recommended | Firmware version shown by `info` and `/api/info`. |

Projects that want lower network latency can call `WiFi.setSleep(false)` after `begin()`; the library keeps the core's default modem sleep.

## Next

- Add your own commands and routes: [Console and API](Console-and-API.md).
- Build your product as apps: [Apps](Apps.md).
