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
  https://github.com/dontseyit/esp-base.git#1.3.0
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
- `min_spiffs.csv` gives two 1.9 MB app slots, enough for OTA on every chip. [Partition table](#partition-table) covers the other choices.
- The USB flags route `Serial` to the USB port. The C3 and C6 also need `ARDUINO_USB_MODE=1`.

### Partition table

OTA needs two app slots of the same size, because it writes the one it is not running from. The bundled example builds to 1.08 to 1.27 MB depending on the chip, before any code of yours ([where that goes](Architecture.md#resource-budget)). Go by the size of `.pio/build/<env>/firmware.bin`, which is what has to fit in a slot. PlatformIO's `Flash:` line reads 30 to 75 KB lower and passes builds that will not fit.

The stock tables for 4 MB flash:

| Table | App slots | Filesystem | The example |
| --- | --- | --- | --- |
| `default.csv` | 2 × 1.25 MB | 1.375 MB | fits on ESP32, S2 and S3 with 110 to 170 KB to spare; too big on C3 and C6 |
| `min_spiffs.csv` | 2 × 1.875 MB | 128 KB | fits everywhere, with room to grow |
| `ota_nofs_4MB.csv` | 2 × 1.94 MB | none | fits everywhere |
| `huge_app.csv` | 1 × 3 MB | 896 KB | no OTA |
| `max_app_4MB.csv` | 1 × 3.9 MB | none | no OTA |

esp-base itself needs no filesystem, because the page is compiled in and settings live in NVS. With a one-slot table OTA switches itself off, and `-DESPBASE_NO_OTA` also removes its code. On 8 MB and 16 MB flash two slots cost little.

No stock 4 MB table has both OTA and a filesystem of useful size. For both, write your own. This one has two 1.5 MB slots and 896 KB for LittleFS:

```csv
# Name,   Type, SubType,  Offset,   Size
nvs,      data, nvs,      0x9000,   0x5000
otadata,  data, ota,      0xe000,   0x2000
app0,     app,  ota_0,    0x10000,  0x180000
app1,     app,  ota_1,    0x190000, 0x180000
spiffs,   data, spiffs,   0x310000, 0xE0000
coredump, data, coredump, 0x3F0000, 0x10000
```

Save it next to `platformio.ini` as `partitions.csv` and set `board_build.partitions = partitions.csv`. Each partition starts where the one before it ends, app slots start on a 64 KB boundary, and the last one ends at `0x400000` on 4 MB flash. Move the `app1` and `spiffs` offsets together to trade slot size for filesystem space. The filesystem keeps the name `spiffs`, the partition `LittleFS.begin()` mounts by default.

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

The new firmware boots on trial. If it resets within `cfg.otaTrialMs` (30 s by default), whether from a crash, a watchdog or a power cut, the bootloader goes back to the previous firmware and the log says so on the next boot. `reboot` from the console, or `base.reboot()`, ends the trial early and keeps the new firmware.

## Build flags

| Flag | Where | Why |
| --- | --- | --- |
| `-DARDUINO_USB_CDC_ON_BOOT=1` | S3, C3, C6 projects | Route `Serial` to the USB port. |
| `-DARDUINO_USB_MODE=1` | C3, C6 projects | Required with the flag above: those chips only have the USB Serial/JTAG peripheral and their board manifests do not set it. The S3 manifest already does. |
| `-DCONFIG_ASYNC_TCP_RUNNING_CORE=1` | dual core projects | Pin the async_tcp task to the application core (AsyncTCP default: any core). |
| `-DCONFIG_ASYNC_TCP_STACK_SIZE=4096` | optional | AsyncTCP defaults to 16 KB. |
| `-DESPBASE_NO_OTA` | optional | Compile the OTA module out. |
| `-DESPBASE_NO_APPS` | headless projects | Compile the app framework out: no `base.apps()`, `app` command, `/api/apps` or `boot_app` key. |
| `-DESPBASE_LOG_MAX_LEVEL=3` | optional | Compile `LOG_D` out. |
| `-DFW_VERSION=\"1.2.3\"` | recommended | Firmware version shown by `info` and `/api/info`. |

Projects that want lower network latency can call `WiFi.setSleep(false)` after `begin()`; the library keeps the core's default modem sleep.

## Next

- Add your own commands and routes: [Console and API](Console-and-API.md).
- Build your product as apps: [Apps](Apps.md).
