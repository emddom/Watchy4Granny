# Watchy4Granny (Watchy_GSR Assistive Smartwatch Firmware)

## Project Overview

Firmware for a **Watchy** e-paper smartwatch customized as an assistive and safety device for an elderly user ("Granny"). Built on top of a vendored copy of the [Watchy_GSR](https://github.com/GuruSR/Watchy_GSR) (v1.4.7N) framework using **PlatformIO** (Arduino framework), targeting ESP32 (`watchy-v1-v2`) and ESP32-S3 (`watchy-v3`).

Key custom features include:
- **Medical Info Screen (`MedInfoState`)**: Displays emergency identification, allergies, and birth date on the e-paper screen.
- **Find My Phone (`FindFoneState`)**: Connects to WiFi and triggers an IFTTT webhook (`find_phone`) to ring the user's phone.
- **Show Buttons Overlay (`ShowButtonsState`)**: Displays a 10-second on-screen legend mapping physical buttons (`SW1`–`SW4`) to their active logical actions.
- **Low-Battery Suspend (`SuspendState`)**: Locks the watch into a low-power suspended screen when battery drops to/below the configured suspend threshold, with charging status and wake prompt (`SW3`) when plugged in.
- **Native Utilities Menu (`GSR_MENU_UTILS`)**: Integrates `Show Buttons`, `Find Phone`, and `Med Info` directly into the root Watchy_GSR menu (`Menu -> Utilities`).

## Source Layout & Vendored Code Rules

- **Entry Point & Custom Layer (Edit Here)**:
  - `src/GSR.cpp` — Arduino `setup()` and `loop()` driving `PowerStateMachine` and `UiStateMachine`.
  - `src/OverrideGSR.{h,cpp}` — Subclasses `WatchyGSR` (`OverrideGSR`), holds `RTC_DATA_ATTR` state flags, custom screen rendering in `drawWatchFace()`, IFTTT webhook in `InsertWiFi()`, and menu hook callbacks (`InsertHandleShowButtons()`, `InsertHandleFindPhone()`, `InsertHandleMedInfo()`).
  - `src/StateMachine.h` — Generic `State` and `StateMachine` base classes (`enter`, `execute`, `exit`, `handleButton`).
  - `src/PowerStateMachine.{h,cpp}` — Manages `AwakeState` (`watchy->tick()`) and `SleepState` (`watchy->deepSleep()`) based on `ActiveMode`.
  - `src/UiStateMachine.{h,cpp}` — Manages `WatchfaceState`, `MenuState`, `MedInfoState`, `FindFoneState`, `SuspendState`, and `ShowButtonsState`.
- **Vendored Framework (Treat as Third-Party)**:
  - `src/Watchy_GSR.{h,cpp}`, `src/Defines_GSR.h`, `src/Fonts_GSR.h`, `src/Icons_GSR.h`, `src/Locale_GSR.h`, `src/Web-HTML.h`, `src/ArduinoNvs.{h,cpp}`, `src/WeatherIcons.{h,cpp}`.
  - Avoid modifying vendored files unless exposing a required hook for `OverrideGSR`. Existing targeted patches to `Defines_GSR.h` and `Watchy_GSR.cpp` (for `GSR_MENU_UTILS` and WiFi build-reset preservation) are documented in `patch_defines.py` and `patch_watchy_gsr.py`.
- **Documentation**:
  - `watchy_state_machine.md` / `watchy_state_machine.html` — Mermaid state diagrams of `PowerStateMachine` and `UiStateMachine`. Keep in sync when modifying states.
  - `USER_GUIDE.md` — End-user button reference.

## Building, Flashing, and Preserving Settings

Use the `watchy-firmware` skill (`.agents/skills/watchy-firmware/SKILL.md`) for NVS backup/restore and firmware workflows.

### Common Commands (Podman Containerized Workflow — Recommended)
This project uses the local `docker.io/espressif/idf:latest` Podman image via [`scripts/podman_pio.sh`](file:///home/emddom/work/Watchy4Granny/scripts/podman_pio.sh) (and [`Containerfile`](file:///home/emddom/work/Watchy4Granny/Containerfile)), mounting the workspace and `~/.platformio` cache with `--userns=keep-id` and forwarding `/dev/ttyUSB0` when connected:

```bash
# Build firmware inside the Espressif Podman container (default env: watchy-v1-v2)
./scripts/podman_pio.sh run -e watchy-v1-v2      # Standard ESP32 Watchy (v1.5, v2.0)
./scripts/podman_pio.sh run -e watchy-v3         # ESP32-S3 Watchy (v3.0)

# Back up NVS settings (WiFi APs, NTP server, timezone, alarms, options) before flashing
python3 .agents/skills/watchy-firmware/scripts/nvs_tool.py backup --port /dev/ttyUSB0

# Flash firmware to connected Watchy from the Podman container
./scripts/podman_pio.sh run -e watchy-v1-v2 -t upload

# Serial monitor (115200 baud)
./scripts/podman_pio.sh run -e watchy-v1-v2 -t monitor
```

### Host Native Fallback Commands
```bash
pio run -e watchy-v1-v2
pio run -e watchy-v1-v2 -t upload
pio run -e watchy-v1-v2 -t monitor
```

> **Note:** `-msvc` environments (`watchy-v1-v2-msvc`, `watchy-v3-msvc`) exist only for IDE IntelliSense compatibility (`-Zc:__cplusplus`) and must not be used for hardware builds.

## Development & Safety Conventions

- **Deep Sleep Persistence**: Any state variable that must survive deep sleep cycles MUST be declared with `RTC_DATA_ATTR`.
- **Device Settings Persistence (NVS)**: User settings (up to 10 WiFi APs/passwords, custom NTP server, POSIX timezone, weather coordinates, display/button preferences, alarms, and timers) are stored in the ESP32 NVS flash partition (`0x9000`, size `0x5000`, namespace `"storage"`, keys `"GSR-Options"` and `"GSR-TZ"`). Always back up NVS to `backups/` (gitignored) before destructive flash operations.
- **Sensitive Data**: `src/OverrideGSR.cpp` contains personal medical information and an IFTTT webhook key, and `backups/` contains decoded WiFi credentials. Never expose these credentials or personal details outside local files.
