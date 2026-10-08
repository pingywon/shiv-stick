# Hardware: the M5Stack StickS3

| Part | Detail | Used for |
|---|---|---|
| SoC | ESP32-S3-PICO-1, 8 MB flash + 8 MB PSRAM (OPI), USB-Serial/JTAG | everything |
| Screen | 1.14" 240 x 135 IPS | UI; text floor 16 px (17 px caps), enforced by the render audit |
| Buttons | FRONT = BtnA GPIO 11, SIDE = BtnB GPIO 12 (hold threshold 550 ms); power key (double-click = power off, hardware) | input; both also wake light sleep |
| IMU | BMI270, accelerometer read at 50 Hz while the screen is on | auto-flip, level, oracle shake, daemon eyes |
| Power | 250 mAh cell; PM1 power chip at I2C `0x6E`, register `0x04` (0/1 = USB, 2 = battery) | USB detect, desk-clock mode |
| Audio | ES8311 codec + MEMS mic + speaker (not at the same time) | tones; mic drives the spectrum face and "the daemon hears the room" |
| IR | transmitter + receiver | learn / replay, TV-off sweep |
| Radio | 2.4 GHz Wi-Fi only (no 5 GHz); Bluetooth LE via **NimBLE** (m5stack core 3.3.8) | Wi-Fi, BLE scanner |
| Free pins | G1-G10 (touch + ADC1) on the header | none yet |
| RTC | none; time comes from NTP | |

Battery life: about 1.3-2 h with Bluetooth and the screen on. Use USB-C for long sessions at the desk.

## Flash layout (8 MB, "default_8MB")
| Partition | Size | Holds |
|---|---|---|
| nvs | 20 KB | Wi-Fi driver data |
| app0 / app1 | 3.19 MB each | firmware (OTA flips between them) |
| spiffs (LittleFS) | 1.5 MB | `/config.json`, `/ir.json`, `/irNN.bin` |
| coredump | 64 KB at `0x7F0000` | last crash, readable with `esp-coredump` |

1.0.0 uses about 1.9 MB of an app slot (~57 %).

## Reading a crash dump
```
esptool --chip esp32s3 read_flash 0x7F0000 0x10000 core.bin
esp-coredump info_corefile -t raw -c core.bin firmware/SHIV.ino.elf
```
The ELF must match the flashed build.

## Flashing notes
The no-compile flash kit (`flash.bat`) uses esptool from the Arduino ESP32 install and auto-detects the serial port.
On Windows, open the sketch from a local folder — Arduino chokes on UNC network paths.
