# Web API

The stick serves its own panel and a JSON API on port 80 (`http://shiv.local` or its IP on the SYSTEM screen).
The server is the Arduino core's single-client `WebServer`: one request at a time, no push channel. The panel polls.

## Login
- **Panel password set** (SYSTEM tab): HTTP Basic auth, user `shiv`, password = the panel password.
- No password set: open (only `/`, `/api/scan` and `/api/setup` are served while the setup hotspot runs).
- Script endpoints also accept `?key=<apiKey>` (shown on the STATUS tab): `/api/page`, `/api/doorbell`, `/api/pihole`.
- Secrets are write-only: `/api/config` reports "set / not set", never the value. Send `"-"` to clear one.

## Endpoints

| Method + path | Body / params | Returns / does |
|---|---|---|
| `GET /` | | the panel (gzip) or the setup page while the hotspot runs |
| `GET /api/status` | | live status: version, ip, ssid, rssi, battery (`batt`, `mv`, `usb`, `pwrSrc`, `charging`), heap / psram, uptime, screen, Pi-hole, mqtt, pager, daemon mood, `ota*`, time |
| `GET /api/config` | | all settings (no secrets) + `screens[]` names + learned IR codes |
| `POST /api/config` | JSON, any subset of the GET shape | saves and applies; `pet.at` / `pet.seen` are ignored so a restored backup cannot rewind the daemon |
| `GET/POST /api/page` | `msg`, `from` | pager message; the stick beeps, wakes, shows it |
| `POST /api/pages/clear` | | delete all pager messages |
| `GET/POST /api/doorbell` | `cam` + `what`, or JSON `{cam, what}`, or UniFi Protect's native body (`triggers[0].key/device`) | doorbell alert, ding-dong |
| `POST /api/pihole` | `{"enable": true/false}` or `{}` = toggle | Pi-hole blocking on / off (timer runs on the Pi-hole) |
| `GET /api/ha/test` | | tries the saved Home Assistant URL + token |
| `GET /api/ha/entities` | `domain` | entity list for the favorites picker |
| `POST /api/screen` | `{id}` | jump the stick to a screen |
| `GET /api/screen.raw` | | the live screen, 240x135 RGB565 (64,800 bytes) |
| `POST /api/ir/send\|delete\|rename` | `{i}` (+ `name`) | learned IR codes |
| `POST /api/macro/run`, `/api/fav/run` | `{i}` | run a macro / HA favorite |
| `GET /api/scan` | | Wi-Fi networks seen + last join attempt + the stick's MAC |
| `POST /api/setup` | Wi-Fi name, password, optional username | save a network and join |
| `GET/POST /api/ota` | POST `{"action":"check"\|"install"\|"snooze"}` | self-update: GET = state (offered version, progress); POST checks the manifest, accepts an offered update, or snoozes it |
| `POST /api/reboot` | | restart after 600 ms (config saved first) |
| `POST /api/portal` | | open the setup hotspot now |
| `POST /update` | multipart `.bin` | manual over-the-air firmware update |

## Examples
```
curl -d 'msg=backup finished' 'http://shiv.local/api/page?key=KEY'
curl -X POST -H 'Content-Type: application/json' -d '{"enable":false}' 'http://shiv.local/api/pihole?key=KEY'
curl -X POST -H 'Content-Type: application/json' -d '{"action":"check"}' 'http://shiv.local/api/ota'
```

## MQTT (optional, set on the CONNECTIONS tab)
Read-only ticker: subscribes to the topics you list and scrolls the latest messages on the MQTT screen.
Publishes a retained `shiv/<host>/status` (online/offline) so you can see the stick on your broker.
