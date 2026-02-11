# React Native BLE/Wi-Fi Integration Guide (ESP32 Unit)

This document defines how the React Native app must configure BLE and Wi-Fi provisioning for the firmware in this repository.

## 1) Scope

The app has two operating modes:
- Provisioning mode: send Wi-Fi SSID/password over BLE so the ESP32 can connect to internet and MQTT.
- Pure BLE mode: connect to the unit and read machine data from GATT characteristics.

## 2) BLE Discovery and Identification

- Device name prefix: `AVENSYS`
- Runtime advertised name format: `AVENSYS_XX:XX:XX:XX:XX:XX`
- Recommended app UI: always show full BLE name and RSSI, let operator explicitly select one unit.

## 3) BLE Service and Characteristics

Primary service:
- 16-bit UUID: `0x00FF`
- 128-bit UUID: `0000FF00-0000-1000-8000-00805F9B34FB`

Characteristics used by app:
- EEPROM data: `0xFF01` (`0000FF01-0000-1000-8000-00805F9B34FB`)
- Debug data: `0xFF02` (`0000FF02-0000-1000-8000-00805F9B34FB`)
- Polling data: `0xFF03` (`0000FF03-0000-1000-8000-00805F9B34FB`)
- Wi-Fi SSID: `0xFF05` (`0000FF05-0000-1000-8000-00805F9B34FB`) [encrypted read/write]
- Wi-Fi password: `0xFF06` (`0000FF06-0000-1000-8000-00805F9B34FB`) [encrypted read/write]
- Connect-to-cloud trigger: `0xFF07` (`0000FF07-0000-1000-8000-00805F9B34FB`)
- Provision status: `0xFF0B` (`0000FF0B-0000-1000-8000-00805F9B34FB`) [read + notify]

Notes:
- SSID/password characteristics require BLE encryption, so pairing/bonding is required.
- Machine data characteristics are exposed for read/write in firmware. App should treat them as read-only unless write is explicitly needed.

## 4) Provisioning Status Codes (FF0B)

- `0`: IDLE
- `1`: WAIT_SSID
- `2`: WAIT_PASSWORD
- `3`: READY
- `4`: APPLYING
- `5`: DONE
- `6`: ERROR (including timeout)

## 5) Provisioning Flow (Required)

1. Scan and select target unit (`AVENSYS_...`).
2. Connect and discover services/characteristics.
3. Subscribe to notifications on `FF0B` (provision status).
4. Ensure pairing is completed (OS-level prompt may appear).
5. Write SSID bytes to `FF05`.
6. Write password bytes to `FF06`.
7. Optional but recommended: write any payload to `FF07` to explicitly commit/apply provisioning.
8. Wait for status updates:
   - expected path: `WAIT_SSID/WAIT_PASSWORD -> READY -> APPLYING -> DONE`
   - if `ERROR`, restart flow.

## 6) Provisioning Timeout Behavior

- Firmware timeout: 90 seconds inactivity during provisioning.
- On timeout: SSID/password temporary buffers are reset and status becomes `ERROR`.
- App requirement:
  - if no activity for >60s, show countdown or warning.
  - if status becomes `ERROR`, clear local provisioning state and restart from SSID step.

## 7) Pairing and Security Requirements

Firmware security configuration:
- Secure Connections bonding enabled.
- Encryption requested on connect.
- SSID/password operations require encrypted link.

App requirements:
- Handle pairing prompt on Android/iOS.
- If write to `FF05` or `FF06` fails with security/auth error, force re-pair and retry.
- Keep bonding for known devices to speed repeated tests.

## 8) Pure BLE Data Mode (No Wi-Fi Provisioning)

- App can connect and read data from `FF01`, `FF02`, `FF03`.
- For now, app should not assume continuous notify streaming for these three characteristics; implement periodic reads.
- Suggested poll intervals:
  - Polling data (`FF03`): 1-2 s
  - Debug data (`FF02`): 2-5 s
  - EEPROM (`FF01`): on-demand or low frequency

## 9) React Native Implementation Notes

Recommended BLE library:
- `react-native-ble-plx` (or equivalent with reliable pairing handling)

Android permissions (API 31+):
- `BLUETOOTH_SCAN`
- `BLUETOOTH_CONNECT`
- `ACCESS_FINE_LOCATION` (for older Android behavior compatibility if needed)

iOS Info.plist keys:
- `NSBluetoothAlwaysUsageDescription`
- `NSBluetoothPeripheralUsageDescription` (if target iOS/tooling still expects it)

## 10) Minimal Validation Checklist for App QA

- Scan shows unique `AVENSYS_...` entries for multiple powered units.
- Pairing prompt appears once and provisioning writes succeed afterward.
- `FF0B` status sequence matches expected flow.
- Timeout case tested (>90s inactivity) and app recovery works.
- Pure BLE mode reads `FF01/FF02/FF03` without provisioning.

## 11) Future-Proofing for Cross-Project Reuse

When using this document in another project/chat, validate these fields first:
- Service UUID (`0x00FF`)
- Characteristic UUID map (`FF01..FF0B`)
- Security policy for SSID/password (encrypted required)
- Provision timeout value (currently 90s)
- Device naming format (`AVENSYS_<MAC>`)
