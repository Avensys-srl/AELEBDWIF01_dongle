# AELEBDWIF01_dongle

ESP32 firmware based on ESP-IDF (PlatformIO) for Wi-Fi/BLE connectivity, MQTT to AWS IoT, and management of an external module ("Quarke"/WBM) over UART, with OTA support.

## Overview
This project implements an ESP32 device that:
- connects to a Wi-Fi network (credentials via BLE or hardcoded),
- publishes/subscribes MQTT topics (AWS IoT Core),
- manages serial communication with an external board (WBM/Quarke),
- performs OTA updates for the ESP32 firmware and, optionally, the external board.

## Key Features
- **Wi-Fi STA** with timeout handling and auto-reconnect.
- **BLE provisioning** for SSID/password with NVS storage.
- **TLS MQTT** with embedded certificates.
- **HTTPS OTA** with JSON version check.
- **UART1** for WBM/Quarke serial protocol.
- **SPIFFS** storage (used for Quarke firmware handling).

## Architecture (high level)
- **ESP32**
  - Wi-Fi + MQTT (AWS IoT)
  - BLE GATT (credentials provisioning)
  - ESP32 OTA (HTTPS)
  - UART1 <-> WBM/Quarke
- **WBM/Quarke**
  - Serial data exchange and polling
  - Firmware update downloaded by ESP32

## Requirements
- **Hardware**: ESP32 DevKit v1 (board: `esp32doit-devkit-v1`)
- **Toolchain**: PlatformIO with ESP-IDF framework

## Configuration
### Wi-Fi
- Hardcoded in `src/main.c`:
  - `WIFI_SSID`, `WIFI_PASSWORD`
- Or provisioned via BLE:
  - Stored in NVS keys `wifi_ssid` and `wifi_pass`

### MQTT / AWS IoT
- TLS broker in `src/main.c`:
  - `BROKER_URL`
- Embedded certs from `platformio.ini`:
  - `src/aws-root-ca.pem`
  - `src/certificate.pem.crt`
  - `src/private.pem.key`

Main topics (derived from device id):
- `/[device]/app/eeprom`
- `/[device]/app/request`
- `/[device]/esp/eeprom`

### ESP32 OTA
- Version JSON URL: `VERSION_URL` in `src/main.c`
- Current version: `CURRENT_VERSION`

### Quarke Update
- Remote firmware: `Quarke_URL` in `src/main.c`
- Uses SPIFFS and custom partition table: `partition_one_ota.csv`

## Build and Flash
From repo root:

```bash
pio run
pio run -t upload
pio device monitor
```

Useful parameters (already set in `platformio.ini`):
- `monitor_speed = 115200`
- `upload_speed = 921600`
- OTA + SPIFFS partition layout (`partition_one_ota.csv`)

## Repository Structure
- `src/main.c` - entrypoint, core tasks, MQTT/OTA/Quarke
- `src/ble.c` - BLE provisioning
- `src/wifi_connect.c` - Wi-Fi management
- `src/Uart1.c` - UART1 driver
- `src/WBM_Serial.c` - WBM/Quarke serial protocol
- `include/` - headers and definitions

## Hardware Notes
- UART1 on GPIO17 (TX) and GPIO16 (RX)
- UART1 baud rate: 230400 (default), 9600 (alternate init)

## Security
This repo currently contains keys/certificates. For GitHub publication:
- replace PEM files with placeholders,
- move secrets out of the repository,
- use environment variables or secure storage.

## License
Not specified.