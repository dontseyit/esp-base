# Console and API

[Home](Home.md) / Console and API

## The web console

Open `http://<hostname>.local/` or `http://192.168.4.1/`. The page is a single gzipped file served from flash, about 6 KB.

- **Console**: live log with level and text filter, command line with history (arrow keys).
- **WiFi**: stored networks, primary and backup entry, scan, mode and timers.
- **Update**: firmware upload.
- **Info**: device summary as JSON.

## Built-in commands

| Command | Does |
| --- | --- |
| `help` | List every command. |
| `info`, `heap` | Device summary, heap statistics. |
| `reboot`, `factory-reset` | Restart, or erase the `espbase` namespace and restart. |
| `wifi ...` | Networks, mode, scan. See [WiFi](WiFi.md#managing-networks). |
| `config list\|get\|set` | Persisted keys. See [Logging and config](Logging-and-Config.md#key-reference). |
| `log level [e\|w\|i\|d]`, `log dump` | Runtime log level (persisted), print the ring buffer. |
| `app ...` | App framework. See [Apps](Apps.md#console-and-api). |

## Adding a command

```cpp
base.console().addCommand("led",
  [](const CmdArgs& a, Print& out) {
    if (a.is(0, "on"))       { digitalWrite(LED, HIGH); out.println("on"); }
    else if (a.is(0, "off")) { digitalWrite(LED, LOW);  out.println("off"); }
    else                     out.println("usage: led on|off");
  },
  "led on|off");
```

- `CmdArgs` gives `a[i]`, `a.has(i)`, `a.is(i, "word")` (case insensitive), `a.toInt(i, def)`, `a.size()` and `a.raw()` for everything after the name. Up to 8 arguments; quote values with spaces.
- Handlers run on the loop task, so any library API is safe. Return quickly: the output is collected and sent as one message.
- Registering an existing name replaces its handler.
- To accept commands on Serial too, pass each line to `base.console().execute(line, Serial)`. The example does this.

## Adding an HTTP route

```cpp
base.web().on("/api/state", HTTP_GET, [](AsyncWebServerRequest* r) {
  r->send(200, "application/json", "{\"led\":true}");
});

base.web().on("/api/led", HTTP_POST, [](AsyncWebServerRequest* r, JsonVariant& json) {
  ledRequested = json["on"] | false;   // a flag the loop task acts on
  r->send(200, "application/json", "{\"ok\":true}");
});
```

- Register routes in `setup()` after `base.begin()`.
- Handlers run on the async_tcp task. Never block or call `delay()`. Reading state is fine; hand changes to the loop task with a flag or a queue.
- JSON bodies use ArduinoJson 7 through the handler signature shown above.
- Routes under `/api/` get the origin check and the login for free. Put anything that changes state there.

## HTTP routes

| Route | Purpose |
| --- | --- |
| `GET /` | The console page (gzip, ETag). |
| `GET /api/info` | Chip, memory, network, versions, reset reason. |
| `GET /api/log` | Ring buffer as text. |
| `GET /api/ws-ticket` | Single-use WebSocket ticket. |
| `GET\|POST\|DELETE /api/wifi` | Stored networks and connection state. |
| `POST /api/wifi/settings` | Mode and timers. |
| `GET\|POST /api/wifi/scan` | Scan results, start a scan. |
| `GET\|POST /api/apps` | App registry, stack and stats; navigation actions. |
| `POST /update` | Firmware upload. |
| `GET /ws` | WebSocket. |

Payloads are listed in the [README](../../README.md#http-api).

## WebSocket protocol

Connect to `/ws?t=<ticket>` with a ticket from `GET /api/ws-ticket`. Each ticket works once, within 30 s. Messages are JSON text frames, and a frame carries one message or an array of them.

| Direction | Message |
| --- | --- |
| server to client | `{"t":"log","l":"I","ms":1234,"m":"..."}`, level `E`, `W`, `I` or `D` |
| client to server | `{"t":"cmd","c":"wifi status"}` |
| server to client | `{"t":"out","m":"..."}`, the command's output |

A new client first receives the ring buffer, then live lines. A client that falls behind loses lines and gets one warning saying how many, so it never stalls the device. Beyond 4 clients the oldest is dropped.

## Security

There is no TLS, so treat the web interface as a LAN service.

- Set `cfg.webPassword`, or `config set web_pass` and reboot. The page, the API and uploads then need HTTP digest auth as user `admin`. Five wrong passwords lock logins for 30 s.
- Without a password the console is open to the network, but other web sites still cannot reach it: `/api/`, `/ws` and `/update` refuse foreign `Origin` headers, and foreign `Host` names (DNS rebinding).
- espota uses `cfg.otaPassword`. Uploads through `POST /update` use the console password when one is set, otherwise the OTA password.
- Set `apPassword` too, and see the [README](../../README.md#security) for what stays with the project: NVS encryption, Secure Boot and the trusted serial console.
