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
| An app is missing from `app list` | the linker dropped it from an archive | keep apps in the project's `src/`, or `lib_archive = false` |
| Crash before `setup()` | an app constructor uses `Log` or another global | move that code to `begin()` |
| "another transition is pending" in the log | a second navigation request in one loop iteration | issue the next one from `open()` or `resume()` |
| "tick took ... us" warnings | a tick exceeds the budget | split the work across ticks, or `setTickBudget()` |
| The device boots into the wrong app | `boot_app` is stored | `app boot -` clears it |
| The console shows the old page after a UI change | the header was not regenerated | `python tools/build_web.py`, rebuild, flash |
| 403 "cross-origin request refused" through a custom DNS name | without a password only IP addresses and `<hostname>.local` are accepted (DNS rebinding) | use the IP or `<hostname>.local`, or set a console password |
| The browser keeps asking for the password | five wrong passwords lock logins for 30 s | wait 30 s, log in as `admin` |
| Forgot the console password | it is stored in NVS | on the serial console: `config set web_pass ""`, then `reboot`; or `factory-reset` |
| No live log, "WebSocket ... refused" in the serial log | a page cached from older firmware connects without a ticket | reload the page |

Still stuck: `log level d` shows the WiFi state machine and every app transition, `wifi status` and `app stats` show the current state, and `GET /api/log` returns the recent log.
