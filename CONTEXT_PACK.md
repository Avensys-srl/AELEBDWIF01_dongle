# Context Pack — AELEBDWIF01_dongle (ESP32 FW)
- Repo: w:\AELEBDWIF01_Files_30102025\AELEBDWIF01_dongle
- Platform: ESP32 (esp32doit-devkit-v1), ESP-IDF via PlatformIO.
- Key modules: BLE (src/ble.c + src/ble_app.c), MQTT (src/mqtt_app.c), Wi-Fi (src/wifi_connect.c), UART/WBM (src/Uart1.c, src/WBM_Serial.c), main (src/main.c).
- BLE provisioning: GATT SSID/PASS + new PROV_STATUS characteristic (UUID 0xFF0B) with status codes 0–6; pairing/bonding enabled; SSID/PASS require encrypted read/write; 90s inactivity timeout resets provisioning.
- MQTT: wrapper functions in src/mqtt_app.c; topics built internally; TLS certs embedded.
- Certs moved to src/certs/ and embedded via platformio.ini / src/CMakeLists.txt; symbols remain _binary_<name>_start (no path prefix).
- Naming: “Quarke” fully refactored to “Unit” (variables, logs, comments, README).
- README updated with BLE/MQTT diagram and status code table.
- BLE stack init moved to ble_app_init() in src/ble_app.c to slim app_main.
- UART1 refactor: cleaner init, RX claim API in include/Uart1.h.

Notes for the app project:
- Need React Native app for BLE provisioning + machine data via BLE GATT; must handle pairing and status codes.