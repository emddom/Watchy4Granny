# Watchy4Granny Digital Twin (Android App)

The **Watchy4Granny Twin** Android app is a companion tool and real-time simulator for the **Watchy4Granny** e-paper smartwatch. It mirrors the watch face live, enables remote button control, synchronizes time and timezone without WiFi, and automatically fetches and pushes Open-Meteo weather data to the watch on demand.

---

## Features

* **Dual-Mode Connectivity**:
  * **BLE (Bluetooth Low Energy)**: Connects to Watchy's custom NimBLE GATT service (`19b10000-e8f2-537e-4f6c-d104768a1214`). Operates with near-zero watch battery impact and works outdoors away from WiFi.
  * **WiFi SoftAP**: Connects to Watchy's built-in web portal AP (`http://192.168.4.1`) for web configuration, REST API telemetry, and screen capture.
* **Live 200×200 E-Paper Screen Mirror**:
  * Reads the 5,000-byte monochrome framebuffer (`200×200` pixels, 1 bit per pixel) from Watchy in 500-byte chunks (`SCR:offset,len`).
  * Automatically detects display updates via `screenFrameSeq` in the telemetry status string and refreshes the on-screen preview.
* **On-Demand Background Weather Sync**:
  * When the user opens the Weather screen on Watchy (`SW3`), Watchy signals `bleWeatherReqSeq > 0`.
  * The Android app immediately fetches live weather and ground elevation from Open-Meteo using the phone's GPS/cellular connection, and pushes the payload to Watchy.
* **Offline Time & Timezone Sync**:
  * Sends phone Unix epoch seconds and IANA/POSIX timezone (e.g. `1731580000,Europe/Bucharest`) to Watchy in a single BLE write.
* **Virtual Hardware Buttons (`SW1`–`SW4`)**:
  * Interactive on-screen buttons to control the watch remotely over BLE (`BTN:1` to `BTN:4`) or WiFi REST endpoints (`/api/button?btn=X`).

---

## BLE GATT Protocol Specification

### Service UUID: `19b10000-e8f2-537e-4f6c-d104768a1214`

| Characteristic | UUID | Properties | Purpose & Payload Format |
| :--- | :--- | :--- | :--- |
| **`TIME`** | `19b10001-...` | Read, Write | **Read**: `YYYY-MM-DD HH:MM:SS,utcEpoch`<br>**Write (Time Sync)**: `epochSec[,Timezone]`<br>**Write (Mode)**: `MODE:WATCH`, `MODE:BLE`, `MODE:OFF`<br>**Write (Buttons)**: `BTN:1` .. `BTN:4`<br>**Write (Screen Transfer)**: `SCR:offset,len` or `SCR:END` |
| **`LOCATION`** | `19b10002-...` | Read, Write | **Read / Write**: `lat,lon` (e.g., `44.4323,26.1063`), persisted to Watchy NVS. |
| **`WEATHER`** | `19b10003-...` | Read, Write | **Read / Write**: `tempC,feelsC,wmoCode,humidity,windMs,pressure,elevation`<br>Example: `3,-2,1,78,4.2,1018,113` |
| **`STATUS`** | `19b10004-...` | Read, Notify | **Read**: Returns comma-separated telemetry:<br>`pct,voltage,steps,charging,screenFrameSeq,mode,bleWeatherReqSeq` |

### `STATUS` Telemetry Fields Breakdown

1. **`pct`** (`0`–`100`): Computed battery state of charge.
2. **`voltage`** (e.g. `3.92`): Raw battery voltage measured by the ESP32 ADC.
3. **`steps`** (e.g. `2554`): Daily step counter from the BMA423 accelerometer.
4. **`charging`** (`0` or `1`): `1` if plugged into a USB charger, `0` otherwise.
5. **`screenFrameSeq`** (Integer): Incremented on every Watchy e-paper refresh. When `seq != oldSeq`, the Twin automatically triggers a screen capture.
6. **`mode`** (`0`, `1`, `2`): Current active screen state (`0` = Watchface, `1` = BLE Remote, `2` = Weather).
7. **`bleWeatherReqSeq`** (Integer): Set to non-zero when Watchy requests a weather update (`WEATHER *`). Returns to `0` when sync is complete.

---

## Building & Installing

### Requirements
* Android SDK 34 / Java 17
* Android device running Android 8.0+ (API 26+) with Bluetooth Low Energy

### Build and Install via ADB
```bash
cd android_digital_twin
./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```
