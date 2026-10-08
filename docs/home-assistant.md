# Home Assistant

SHIV talks to Home Assistant over its REST API — no add-on or MQTT bridge required.

## Setup
1. In Home Assistant, make a **long-lived access token** (your profile → Security).
2. On the stick's web panel → **CONNECTIONS**, set the Home Assistant URL (e.g. `http://192.168.1.10:8123`) and paste the token. The token is write-only on the panel: it shows "set", never the value.
3. Press **TEST CONNECTION** — it confirms the address and token and names your HA instance.

## HA CONTROL screen
Open the **HA CONTROL** screen on the stick to browse your entities by category (lights, switches, scenes, scripts) and toggle them with the buttons. Long names scroll.

## Favorites & macros
- **Favorites**: pick a handful of entities on the panel and they get their own quick list on the stick.
- **Macros**: any HTTP request (GET/POST, headers, body) you save on the panel can be fired from the MACROS screen — point one at an HA webhook, a `/api/services/...` call, or anything else on your network.

## MQTT (optional)
Set a broker on the CONNECTIONS tab and the **MQTT** screen scrolls live traffic from the topics you subscribe to. SHIV publishes a retained `shiv/<host>/status` (online/offline) so it shows up on your broker. The ticker is read-only.
