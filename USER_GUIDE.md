# Watchy4Granny Button & Menu User Guide

Looking at the Watchy screen right-side up, the watch has 4 physical buttons on its edges labeled **SW1** through **SW4**.

---

## 1. Physical Buttons Overview

### **SW1: Menu / Enter (Bottom-Left Button)**
* **From the Watch Face:** Opens the Root Menu (`Steps`, `Alarms`, `Timers`, `Options`, `Utilities`).
* **Inside a Menu:** Enters the highlighted submenu, selects an action, or confirms an edited setting.
* **In `Show Buttons` Overlay:** Dismisses the overlay and returns to the watch face (after a 1-second safety delay).

### **SW2: Back / Cancel (Top-Left Button)**
* **Inside a Menu:** Returns to the previous menu or exits the Root Menu back to the Watch Face.
* **In Custom Screens (`Med Info`, `Weather`, `Find Phone`, `Show Buttons`):** Cancels/closes the screen and returns immediately to the Watch Face.

### **SW3: Up (Top-Right Button)**
* **Inside a Menu:** Scrolls **Up** (`Menu.Item - 1`) through menu items or increases the selected setting value.
* **In `Weather` Screen:** Triggers an immediate WiFi weather refresh from Open-Meteo.
* **In `Suspended` Screen (While Charging):** Wakes the watch back to the normal Watch Face when the `*` indicator is blinking next to `SW3`.

### **SW4: Down (Bottom-Right Button)**
* **Inside a Menu:** Scrolls **Down** (`Menu.Item + 1`) through menu items or decreases the selected setting value.

---

## 2. Reaching the Assistive Features (`Menu -> Utilities`)

To prevent accidental button presses while wearing the watch, the custom assistive features are located in the **Utilities** menu:

1. Press **SW1 (Bottom-Left)** from the Watch Face to open the Root Menu.
2. Press **SW3 (Up)** once (or **SW4 (Down)** to scroll) to highlight **`Utilities`**.
3. Press **SW1 (Menu/Enter)** to open the `Utilities` submenu, then use **SW3 (Up)** / **SW4 (Down)** and **SW1 (Enter)** to launch:
   * **`Show Buttons`**: Displays a 10-second on-screen corner legend showing what each physical button (`SW1`–`SW4`) does. Dismisses after 10 seconds or when any button is pressed.
   * **`Find Phone`**: Connects to WiFi and triggers the IFTTT webhook to ring your phone (`FINDING PHONE` -> `PHONE PINGED!`). Press **SW2 (Back)** at any time to cancel/exit.
   * **`Med Info`**: Displays emergency identification (Name, ID, Allergies, Birthdate). Press **SW2 (Back)** to return to the Watch Face.
   * **`Weather`**: Displays the dedicated Open-Meteo weather report (condition icon, temperature in °C, sky condition, feels-like, humidity, wind speed in m/s, and pressure). Automatically fetches fresh data over WiFi if needed; press **SW3 (Up)** to manually refresh or **SW2 (Back)** to return to the Watch Face.
   * **`BLE Remote`**: Starts Bluetooth Low Energy (BLE) advertising as **`Watchy4Granny`** with both:
     1. **BLE HID Media Control**: Pair with `Watchy4Granny` in your phone's Bluetooth settings, then use **SW3 (Up)** for **Volume Up**, **SW4 (Down)** for **Volume Down**, **SW1 (Enter)** for **Play/Pause**, and **SW2 (Back)** to disconnect and return to the Watch Face.
     2. **BLE Digital Twin GATT Service (`19b10000-e8f2-537e-4f6c-d104768a1214`)**: Allows your phone (via `digital_twin.html` in Chrome, MIT App Inventor, or Thunkable) to **sync phone time & timezone to Watchy without WiFi**, read/update saved weather coordinates (`lat,lon`), push Open-Meteo weather data directly from the phone to Watchy, and read battery/step status.
        * **`TIME` (`19b10001-e8f2-537e-4f6c-d104768a1214`)**: Read returns `YYYY-MM-DD HH:MM:SS,utcEpoch`. Write accepts Unix epoch seconds (`1731580000`), milliseconds (`Clock1.SystemTime`), `epochSec,OlsonTZ` (e.g. `1731580000,Europe/Bucharest`), or local `YYYY-MM-DD HH:MM:SS`.
        * **`LOCATION` (`19b10002-e8f2-537e-4f6c-d104768a1214`)**: Read/Write `lat,lon` (persisted to NVS).
        * **`WEATHER` (`19b10003-e8f2-537e-4f6c-d104768a1214`)**: Read/Write `tempC,feelsLikeC,wmoCode,humidity,windMs,pressure`.
        * **`STATUS` (`19b10004-e8f2-537e-4f6c-d104768a1214`)**: Read `batteryPct,batteryVoltage,steps,charging`.

---

## 3. Low-Battery Suspend Mode

When the battery drops to or below the configured **Suspend Level** (`Menu -> Options -> Suspend Level`), the watch enters a low-power **`SUSPENDED`** screen to preserve the Real-Time Clock and prevent confusing frozen-time displays.
* Plug the Watchy into a USB charger: the screen will update to **`CHARGING...`**, show when it was suspended, display saved steps/timers, and blink a `*` next to **SW3 (Top-Right)** with the prompt **`WAKE UP?`**.
* Press **SW3 (Top-Right)** while charging to resume normal operation.

---

**Note on Left-Handed / Swapped Orientation:**
The Watchy_GSR framework allows swapping button sides (`Menu -> Options -> Dexterity`) and flipping Menu/Back (`Menu -> Options -> Menu & Back`). The **`Show Buttons`** screen automatically adapts its corner labels to match your active button orientation.
