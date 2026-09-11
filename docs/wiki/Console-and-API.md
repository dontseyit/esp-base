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

| Route | Description |
| --- | --- |
| `GET /` | The console page, `Content-Encoding: gzip`, `ETag` and `Cache-Control: no-cache` (304 on revalidation). |
| `GET /api/info` | JSON: `chip`, `target`, `rev`, `cores`, `cpuMhz`, `flash`, `heap{free,min,maxAlloc,total}`, `psram{size,free}`, `sketch{size,free}`, `uptime`, `hostname`, `mac`, `wifi{state,ssid,ip,rssi,ap{ssid,ip,clients}}`, `fw`, `build`, `lib`, `core`, `idf`, `reset`, `ota`, `ws`, `log`. |
| `GET /api/log` | The ring buffer as text. |
| `GET /api/ws-ticket` | `{"ticket":"..."}`, a single-use WebSocket ticket valid for 30 s. |
| `GET /api/wifi` | `{"mode","state","ssid","ip","rssi","ap":{...},"settings":{"staTimeoutMs","reconnectTimeoutMs","apRetryIntervalMs"},"maxSlots":5,"networks":[{"slot","ssid","hasPassword"}]}`. |
| `POST /api/wifi` | `{"ssid","pass","slot"?,"connect"?}` stores (slot 1 by default) and connects. `"connect":false` stores only, in the given slot or the first free one, and the response carries `"slot"`. `{"slot":n}` alone starts a round at that network. 400 on invalid input. |
| `DELETE /api/wifi?slot=n` | Remove a stored network. |
| `POST /api/wifi/settings` | Any of `{"mode":"sta\|ap\|apsta","staTimeoutMs","reconnectTimeoutMs","apRetryIntervalMs"}`, validated, persisted and applied without a reboot. 400 with an `error` on the first invalid field. |
| `POST /api/wifi/scan` | Start a scan: 202, or 409 when busy. |
| `GET /api/wifi/scan` | `{"scanning":bool,"age":seconds,"networks":[{"ssid","rssi","ch","enc"}]}`. |
| `GET /api/apps`, `POST /api/apps` | App registry, stack and stats; `open`, `replace`, `close`, `home` and `key` actions. See [Apps](Apps.md#console-and-api). |
| `POST /update` | Multipart firmware upload. See [Getting started](Getting-Started.md#4-update-the-firmware). |
| `GET /ws` | WebSocket. |

Any other path returns 404, except while the AP is up: requests that arrive through the AP interface for a foreign host (the connectivity probes phones and laptops send) are redirected to the portal.

## WebSocket protocol

Connect to `/ws?t=<ticket>` with a ticket from `GET /api/ws-ticket`. Each ticket works once, within 30 s. Messages are JSON text frames, and a frame carries one message or an array of them.

| Direction | Message |
| --- | --- |
| server to client | `{"t":"log","l":"I","ms":1234,"m":"..."}`, level `E`, `W`, `I` or `D` |
| client to server | `{"t":"cmd","c":"wifi status"}` |
| server to client | `{"t":"out","m":"..."}`, the command's output |

A new client first receives the ring buffer, then live lines. A client that falls behind loses lines and gets one warning saying how many, so it never stalls the device. Beyond 4 clients the oldest is dropped.

## Security

There is no TLS, so treat the web interface as a LAN service. Set `cfg.webPassword` to require HTTP digest auth as user `admin` for the page, the API and uploads. Even without a password, other web sites cannot drive the device. [Security](Security.md) has the details.
