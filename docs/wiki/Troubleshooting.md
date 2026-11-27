# Troubleshooting

[Home](Home.md) / Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| The loop stalls when no USB cable or host is attached | USB CDC writes wait for a host | `Serial.setTxTimeoutMs(0)` after `Serial.begin()` |
| C3 or C6 build fails around `Serial` or `USBSerial` | `ARDUINO_USB_CDC_ON_BOOT` without the USB mode flag | add `-DARDUINO_USB_MODE=1` |
| The build pulls in ESPAsyncTCP or RPAsyncTCP | library compatibility mode is not strict | `lib_compat_mode = strict` and `lib_ldf_mode = chain` |
| OTA fails with "not enough space" | the app slot is too small | `board_build.partitions = min_spiffs.csv` or a larger table |
| espota cannot find the device | mDNS only runs while the station is connected | use the IP; on the AP use `192.168.4.1`; allow UDP 3232 |
| The captive portal does not pop up | the phone stays on mobile data or cached the check | open `http://192.168.4.1/` by hand |
| The AP takes long to appear | each stored network gets `sta_timeout` in turn | lower `sta_timeout`; set `reconn_timeout` to 0 for an instant AP after a lost link |
| AP clients drop about once a minute | retry rounds briefly pause the AP (one radio) | raise `ap_retry`, or 0 to retry only on demand |
| A hostname or AP password change has no effect | both are read at boot | reboot |
| A fifth browser tab kicks out the first | 4 console clients by default | raise `maxWsClients` |
| An app is missing from `app list` | the linker dropped it from an archive | keep apps in the project's sources (sketch folder or `src/`), or `lib_archive = false` |
| Crash before `setup()` | an app constructor uses `Log` or another global | move that code to `begin()` |
| "another transition is pending" in the log | a second navigation request in one loop iteration | issue the next one from `open()` or `resume()` |
| "tick took ... us" warnings | a tick exceeds the 20 ms budget | split the work across ticks |
| The device boots into the wrong app | `boot_app` is stored | `app boot -` clears it |
| The console shows the old page after a UI change | the header was not regenerated | `python tools/build_web.py`, rebuild, flash |
| Opening the console by the router's name (`device.lan`, `device.fritz.box`) jumps to the IP | without a password only IP addresses and `<hostname>.local` are accepted by the API (DNS rebinding), so the page redirects | expected; set a console password to use any name |
| 403 "cross-origin request refused" on your own `/api` route | the request came from another origin, or without a password from a host name the device does not accept | call it from the console page's origin, by IP, or set a console password |
| The browser keeps asking for the password | five wrong passwords from your address lock it out for 30 s | wait 30 s, log in as `admin` |
| Build stops with "esp-base needs Arduino core 3.x" | the stock `espressif32` platform or an `esp32` boards package 2.x | use the pioarduino platform (see Getting started) or the `esp32` package 3.x |
| Arduino IDE: "Frame does not name a type" in your app | `Frame` must be defined inside `namespace espbase` | wrap the struct in `namespace espbase { ... }` |
| `time` says "not synced yet" | SNTP needs a station connection and a few seconds | check `wifi status`; in AP only mode there is no NTP |
| Forgot the console password | it is stored in NVS | on the serial console: `config set web_pass ""`, then `reboot`; or `factory-reset` |
| No live log, "WebSocket ... refused" in the serial log | a page cached from older firmware connects without a ticket | reload the page |

Still stuck: `log level d` shows the WiFi state machine and every app transition, `wifi status`, `app list` and `app stack` show the current state, and `GET /api/log` returns the recent log.
