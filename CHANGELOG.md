# Changelog

All notable changes to SHIV. The running version is shown on the boot screen and in
the web panel (SYSTEM → VERSION).

## 1.0.0 — first public release
- The pocket-daemon firmware for the M5Stack StickS3: clock, timer, pomodoro, weather,
  Pi-hole dashboard + pause, LAN host monitor, Shopify/Jobs tiles, Home Assistant
  browse & control, macros, pager, doorbell, MQTT ticker, Wi-Fi/channel/BLE scanners,
  IR remote + TV-off, bubble level, oracle, three games, a virtual-pet daemon, and a
  live mic spectrum.
- **Self-update over the air**: the stick checks a manifest for a newer build and offers
  it on screen — press FRONT to download, flash, and reboot. Fails safe; never installs
  without a press.
- On-device web panel for all configuration, captive-portal Wi-Fi setup (5 saved
  networks), 7 themes, and a headless render/panel/logic test harness in `tools/`.
