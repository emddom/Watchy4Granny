# Watchy4Granny Button & Menu User Guide

Looking at the Watchy screen right-side up, the watch has 4 physical buttons on its edges labeled **SW1** through **SW4**.

---

## 1. Physical Buttons Overview

### **SW1: Menu / Enter (Bottom-Left Button)**
* **From the Watch Face:** Opens the Root Menu (`Steps`, `Alarms`, `Timers`, `Options`, `Utilities`).
* **Inside a Menu:** Enters the highlighted submenu, selects an action, or confirms an edited setting.
* **In `Show Buttons` Overlay:** Dismisses the overlay and returns to the watch face (after a 1-second safety delay).
* **In `BLE Remote` Screen:** Sends **Play / Pause** media key over BLE HID.

### **SW2: Back / Cancel (Top-Left Button)**
* **From the Watch Face:** If the WiFi AP server is currently active, turns off the AP immediately to conserve battery.
* **Inside a Menu:** Returns to the previous menu or exits the Root Menu back to the Watch Face.
* **In Custom Screens (`Med Info`, `Weather`, `Find Phone`, `Show Buttons`, `BLE Remote`):** Cancels/closes the screen and returns immediately to the Watch Face (stopping BLE advertising / allowing the watch to sleep).

### **SW3: Up (Top-Right Button)**
* **From the Watch Face:** Direct shortcut to the **Weather** report screen (`WeatherState`).
* **Inside a Menu:** Scrolls **Up** (`Menu.Item - 1`) through menu items or increases the selected setting value.
* **In `Weather` Screen:** Triggers a manual weather refresh request via BLE to the Digital Twin (or WiFi fallback).
* **In `BLE Remote` Screen:** Sends **Volume Up** media key over BLE HID.
* **In `Suspended` Screen (While Charging):** Wakes the watch back to the normal Watch Face when the `*` indicator is blinking next to `SW3` (`WAKE UP?`).

### **SW4: Down (Bottom-Right Button)**
* **From the Watch Face:** Direct shortcut to the **BLE Remote / Digital Twin** screen (`BleRemoteState`).
* **Inside a Menu:** Scrolls **Down** (`Menu.Item + 1`) through menu items or decreases the selected setting value.
* **In `BLE Remote` Screen:** Sends **Volume Down** media key over BLE HID.

---

## 2. Watch Face Status Indicators (Lower Corners)

On the main Watch Face, status indicators are positioned along the bottom edge (baseline `y = 195`, 5 pixels above the bezel):

* **Lower-Left Corner (Bluetooth Indicator):**
  * **`[ᛒ ✓]` (Solid filled pill with checkmark):** BLE is actively connected to a phone or Digital Twin.
  * **`[ᛒ ×]` (Outlined pill with cross):** BLE is currently disconnected or in standby.
* **Lower-Right Corner (Battery / Charging Indicator):**
  * **Battery Outline with fill percentage:** Shows remaining battery capacity.
  * **`Charging` Icon:** Displayed while plugged into a USB charger.
  * Vertically aligned to the exact baseline (`y = 195`) and vertical center (`y = 187.5`) of the Bluetooth indicator.
* **Lower-Center / Left:**
  * Displays **`AP`** when the Watchy Connect SoftAP server (`192.168.4.1`) is active. Press **SW2 (Back)** from the watch face at any time to shut the AP off.

---

## 3. Assistive Features (`Menu -> Utilities`)

To prevent accidental button presses while wearing the watch, all assistive features can also be launched from the **Utilities** menu:

1. Press **SW1 (Bottom-Left)** from the Watch Face to open the Root Menu.
2. Press **SW3 (Up)** once to highlight **`Utilities`**.
3. Press **SW1 (Menu/Enter)** to open the `Utilities` submenu, then scroll with **SW3 (Up)** / **SW4 (Down)** and select with **SW1 (Enter)**:
   * **`Show Buttons`**: Displays a 10-second on-screen corner legend showing what each physical button (`SW1`–`SW4`) does. Dismisses after 10 seconds or when any button is pressed.
   * **`Find Phone`**: Connects to WiFi and triggers an IFTTT webhook (`find_phone`) to ring your phone (`FINDING PHONE` -> `PHONE PINGED!`). Press **SW2 (Back)** at any time to cancel/exit.
   * **`Med Info`**: Displays emergency identification (Name, ID, Allergies, Birthdate). Press **SW2 (Back)** to return to the Watch Face.
   * **`Weather`**: Displays the dedicated Open-Meteo weather report:
     * **Condition Icon & Temperature**: Weather condition icon displayed before the temperature, with "feels like" in small font right next to it: e.g. `[icon] 3°C (-2°C)`.
     * **Coordinates**: Displays local coordinates `Lat/Lng` (to 1 decimal digit): e.g. `Lat/Lng: 59.4 / 24.7`.
     * **Elevation**: Displays local ground elevation from Open-Meteo: e.g. `Elevation: 113 m` (or `ft`).
     * **Details**: Relative humidity, wind speed in m/s (or mph), and barometric pressure in hPa.
     * **Sync**: Automatically requests fresh data from the phone/Twin via BLE on entry; press **SW3 (Up)** to refresh manually, or **SW2 (Back)** to return to the Watch Face.
   * **`BLE Remote`**: Starts Bluetooth Low Energy (BLE) advertising as **`Watchy4Granny`** with both:
     1. **BLE HID Media Control**: Pair with `Watchy4Granny` in your phone's Bluetooth settings, then use **SW3 (Up)** for **Volume Up**, **SW4 (Down)** for **Volume Down**, **SW1 (Enter)** for **Play/Pause**, and **SW2 (Back)** to disconnect and return to the Watch Face.
     2. **BLE Digital Twin GATT Service (`19b10000-e8f2-537e-4f6c-d104768a1214`)**: Communicates with the Native Android Digital Twin app (`android_digital_twin`) or browser Web Bluetooth (`digital_twin.html`):
        * **`TIME` (`19b10001-...`)**: Syncs phone time & POSIX timezone to Watchy without requiring WiFi.
        * **`LOCATION` (`19b10002-...`)**: Reads/updates saved weather coordinates (`lat,lon`), persisted in NVS.
        * **`WEATHER` (`19b10003-...`)**: Pushes Open-Meteo weather payload: `tempC,feelsLikeC,wmoCode,humidity,windMs,pressure,elevation`.
        * **`STATUS` (`19b10004-...`)**: Reads `batteryPct,batteryVoltage,steps,charging,screenFrameSeq,mode,bleWeatherReqSeq`.
   * **`Watchy Connect`**: Starts the local WiFi SoftAP (`192.168.4.1`) for web-based setup and firmware updates.

---

## 4. Inactivity Timeouts & Power Optimization

* **Minute-by-Minute Screen Updates**:
  * Watchy automatically wakes from deep sleep on the exact second the minute changes via its hardware RTC alarm (`ESP_SLEEP_WAKEUP_EXT0`), refreshes the e-paper screen via partial update, and returns to sleep.
  * If the watch is awake (e.g., actively connected to the Android Digital Twin via BLE or plugged in charging), the watch face automatically refreshes on the exact second of every minute rollover.
* **Daily BLE Auto-Sync & Time Synchronization**:
  * Rather than relying on WiFi NTP, Watchy schedules a daily BLE advertising window (`NTPData.SyncHour:SyncMins`). Whenever the Android Digital Twin or Web Bluetooth connects, it automatically pushes the phone's exact epoch time and timezone to Watchy without requiring user action.
* **Weather Screen Lifecycle & Sync**:
  * While a weather refresh is actively ongoing with the phone connected, the screen remains alive.
  * Once the update is received, the `*` asterisk turns off immediately, and Watchy holds the updated weather screen for **5 seconds** so you can view the new data before automatically returning to the Watch Face.
  * **Suppressed Disconnected WiFi Polling**: If the companion Digital Twin is not connected, all automatic background WiFi weather polling is disabled (`Interval = 0`). This prevents Watchy from staying awake in failing WiFi retry loops that previously caused 15-minute display update freezes.
* **Digital Twin Screen Sync Grace Period**:
  * When returning to the Watch Face with BLE connected, Watchy extends the connection and keeps the display awake while the Digital Twin polls status and downloads the 200x200 screen buffer chunks (`SCR:offset,len`). Once the screen transfer finishes and the grace period expires, Watchy cleanly deep-sleeps and turns off BLE.
* **Secondary Screens (20 Seconds)**: All secondary screens (`Med Info`, `Find Phone`, `Show Buttons`, and idle `Weather`/`BLE Remote` when disconnected) automatically return to the Watch Face after 20 seconds of inactivity.
* **Clean BLE Shutdown**: When exiting `Weather` or `BLE Remote` without a connected phone, BLE advertising stops immediately.
* **WiFi AP Auto-Shutdown (120 Seconds)**: If the WiFi AP is left running without client activity for 120 seconds, Watchy automatically powers off the AP and refreshes the display.

---

## 5. Low-Battery Suspend Mode

When the battery drops to or below the configured **Suspend Level** (`Menu -> Options -> Suspend Level`), the watch enters a low-power **`SUSPENDED`** screen to preserve the Real-Time Clock and prevent confusing frozen-time displays.
* Plug the Watchy into a USB charger: the screen will update to **`CHARGING...`**, show when it was suspended, display saved steps/timers, and blink a `*` next to **SW3 (Top-Right)** with the prompt **`WAKE UP?`**.
* Press **SW3 (Top-Right)** while charging to resume normal operation.

---

**Note on Left-Handed / Swapped Orientation:**
The Watchy_GSR framework allows swapping button sides (`Menu -> Options -> Dexterity`) and flipping Menu/Back (`Menu -> Options -> Menu & Back`). The **`Show Buttons`** screen automatically adapts its corner labels to match your active button orientation.
