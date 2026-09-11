# Security

[Home](Home.md) / Security

The web interface has no TLS, so treat it as a LAN service. Inside that limit, esp-base protects it in these ways:

- `webPassword`, or `config set web_pass` followed by a reboot, protects the page, every `/api/` route and `POST /update` with HTTP digest auth as user `admin`, so the password never crosses the network in clear text. After 5 wrong passwords, logins are refused for 30 s.
- The WebSocket takes a single-use ticket from the authenticated API instead of HTTP credentials.
- Requests to `/api/`, `/ws` and `/update` whose `Origin` names another site get 403. A web page the user happens to visit therefore cannot drive the device (cross-site request forgery and WebSocket hijacking). Without a password the `Host` header must also be an IP address, `<hostname>` or `<hostname>.local`, which stops DNS rebinding. With a password set, any host name works.
- The page is served with a hash-based Content-Security-Policy, `X-Frame-Options: DENY`, `X-Content-Type-Options: nosniff` and `Referrer-Policy: no-referrer`. The UI writes text with `textContent` only.
- JSON request bodies are capped at 1 KB and incoming WebSocket messages at 512 bytes.
- Passwords are never logged or returned. `config` masks every `*_pass` key, only command names reach the log, and the browser keeps password-carrying commands out of its stored history. Control characters in log lines are replaced, and invalid UTF-8 is replaced before it reaches a WebSocket.
- The device logs a warning at boot while the console or OTA has no password.

## Passwords

| Setting | Stored key | Protects |
| --- | --- | --- |
| `apPassword` | `ap_pass` | the fallback access point (WPA2); empty = open AP |
| `webPassword` | `web_pass` | the page, the API and `POST /update` |
| `otaPassword` | none | espota, and `POST /update` while no `webPassword` is set (HTTP basic auth) |

Passwords are 8 to 63 characters. Stored keys override the config fields and apply after a reboot.

## Routes you add

Routes under `/api/` get the origin check and, with a password, the login before your handler runs. Put every route that changes state there. See [Console and API](Console-and-API.md#adding-an-http-route).

## What stays with the project

- Set `webPassword`, `otaPassword` and `apPassword` before shipping. esp-base ships no default password, so first-time setup works without one printed on the device.
- NVS stores the WiFi and console passwords in plain text. Enable flash encryption with NVS encryption if someone could read the flash.
- esp-base does not check OTA image signatures. Use Secure Boot V2 if firmware authenticity matters.
- The serial console is trusted. Anyone with physical access can run commands, including `factory-reset`, which is also the way back from a forgotten password.
