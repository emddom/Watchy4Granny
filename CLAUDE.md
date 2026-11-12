# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Firmware for a **Watchy** e-paper smartwatch, customized as an assistive/safety device for an elderly user ("Granny") — adds a Medical Info screen, a "Find My Phone" panic button, and an on-screen button-legend overlay on top of the [Watchy_GSR](https://github.com/GuruSR/Watchy_GSR) framework. Built with **PlatformIO** (Arduino framework) targeting ESP32 (Watchy v1.5/v2.0) and ESP32-S3 (Watchy v3.0).

`src/Watchy_GSR.{cpp,h}`, `Defines_GSR.h`, `Fonts_GSR.h`, `Icons_GSR.h`, `Locale_GSR.h`, `Web-HTML.h`, and `ArduinoNvs.*` are a **vendored copy of the upstream Watchy_GSR framework**, not original code — treat them as third-party sources. Local customizations live in `OverrideGSR.*`, `PowerStateMachine.*`, `UiStateMachine.*`, `StateMachine.h`, and `GSR.cpp`. `Watchy_GSR.cpp`/`Defines_GSR.h` have received hand-applied, targeted patches (see "Vendored-framework patches" below) to expose a menu hook the override layer needed; beyond that, avoid editing the vendored files — extend behavior through the `OverrideGSR` override points instead.

## Building and Running

```bash
# Build firmware (default env is watchy-v1-v2, per platformio.ini)
pio run -e watchy-v1-v2      # original ESP32 Watchy (v1.5, v2.0)
pio run -e watchy-v3         # ESP32-S3 Watchy (v3.0)

# Flash to a connected device
pio run -e <env> -t upload

# Serial monitor (115200 baud)
pio run -e <env> -t monitor
```

There is no automated test suite — this is embedded firmware; verification is by flashing to hardware (or reasoning through the state machine) and observing behavior on the device/monitor.

`-msvc` variants of each env (`watchy-v1-v2-msvc`, `watchy-v3-msvc`) exist only to add `-Zc:__cplusplus` for editors/IntelliSense that need standards-conformant `__cplusplus`; don't use them for real builds.

## Architecture

Two independent, cooperating state machines drive the device, both built on the generic `StateMachine`/`State` base in `src/StateMachine.h` (enter/execute/exit + `handleButton`). `src/GSR.cpp` is the entry point: `setup()` initializes the framework and powers up, `loop()` ticks both machines and sleeps when idle. See `watchy_state_machine.md` (Mermaid diagram, previewable in VS Code) for the full picture.

**`PowerStateMachine`** (`PowerStateMachine.{cpp,h}`) — `PS_AWAKE` / `PS_SLEEP`. Wakes on hardware interrupt/timer, calls `watchy->tick()` while awake, and deep-sleeps once `ActiveMode` (an upstream framework flag) goes false.

**`UiStateMachine`** (`UiStateMachine.{cpp,h}`) — runs only while awake: `WatchfaceState`, `MenuState`, `MedInfoState`, `FindFoneState`, `SuspendState`, `ShowButtonsState`. Button presses are dispatched to the current state's `handleButton`. Custom states (`MedInfoState`, `FindFoneState`, `ShowButtonsState`, `SuspendState`) generally toggle an `RTC_DATA_ATTR` flag (e.g. `showingMedInfo`, `callingForHelp`, `showingButtons`, `isSuspended`, all declared in `OverrideGSR.h`/defined in `OverrideGSR.cpp`) that `OverrideGSR::drawWatchFace()` checks to decide what to overlay on the e-paper display. `WatchfaceState` and `MenuState` fall back to the upstream framework's own `InsertHandlePressed()` / `handleMenuButton()` when a custom state doesn't claim the button press — the custom layer is additive on top of stock Watchy_GSR behavior, not a replacement for it.

**Utilities menu integration**: `MedInfoState`, `FindFoneState`, and `ShowButtonsState` are also reachable from the framework's native menu via Menu → Options → **Utilities** (`GSR_MENU_UTILS*` / `GSR_MENU_INUTILS` constants added to `Defines_GSR.h`, wired into `Watchy_GSR.cpp`'s menu draw/navigation switch — see "Vendored-framework patches"). `InsertHandleShowButtons()` / `InsertHandleFindPhone()` / `InsertHandleMedInfo()` in `OverrideGSR.cpp` are the entry points the native menu calls into, and just forward to `uiSM.setState(&...)`.

**Deep sleep persistence**: any variable that must survive a deep-sleep cycle needs `RTC_DATA_ATTR` (see the flags declared at the top of `OverrideGSR.h`/`.cpp`). Regular globals/statics do not survive sleep.

**Physical buttons** (see `USER_GUIDE.md` for the full user-facing mapping): SW1 = Menu/Enter, SW2 = Back/Cancel, SW3 = Up (also toggles Medical Info from the watchface), SW4 = Down (also triggers Find My Phone from the watchface). Framework logic can swap SW1/SW2 and SW3/SW4 for left-handed use, so don't assume a fixed physical-to-logical mapping when reading button-handling code — use `WatchyGSR::getSWValue(...)` (as `drawWatchFace()`'s `showingButtons` overlay does) to resolve physical → logical.

### Extending functionality

- Custom watch faces: override `InsertAddWatchStyles()`, `InsertInitWatchStyle(uint8_t StyleID)`, `InsertDrawWatchStyle(uint8_t StyleID)` in `OverrideGSR`.
- Custom button behavior: override `InsertHandlePressed()` — return `true` once you've consumed a press, and set the `Haptic`/`Refresh` out-params as needed.
- New full-screen custom states: add a `State` subclass in `UiStateMachine.{h,cpp}`, give it a global instance, add an `RTC_DATA_ATTR` flag if `drawWatchFace()` needs to know about it while other code paths run, and wire an `Insert...`-style entry point in `OverrideGSR` if it should also be reachable from the native menu.
- Watch-face addon headers live in `Watchface Addons/` (gitignored) and are conditionally pulled into `OverrideGSR.h` via `__has_include`.

### Vendored-framework patches

`patch_defines.py` and `patch_watchy_gsr.py` are one-shot scripts (already applied, not part of the build) that record how `Defines_GSR.h` and `Watchy_GSR.cpp` were mechanically edited to insert the `GSR_MENU_UTILS` submenu (renumbering subsequent `GSR_MENU_*` constants, adding draw-menu cases, and adding Enter/Back/Up/Down handling for the new menu style). If the upstream vendored files are ever re-pulled from Watchy_GSR, these scripts show what needs to be reapplied — read them before hand-patching a fresh vendor drop.

### Config

`Defines_GSR.h` holds compile-time constants: WiFi AP defaults, battery voltage curve (`GSR_MaxBattery`, `GSR_ChargePerMin`), and all `GSR_MENU_*`/`GSR_MENU_IN*` menu-position/segment constants. Menu constants are positional (roller navigation walks a numeric range via the `roller()` macro), so inserting a new menu item requires renumbering everything after it — that's what `patch_defines.py` automates.

Hardcoded personal data (the Medical Info panel's name/ID/allergy/birthdate strings, an IFTTT webhook URL/key for Find My Phone) lives directly in `OverrideGSR.cpp`. Treat that file as containing sensitive personal info — don't echo its contents into logs, commit messages, or external services beyond what's already committed.

## Repo layout notes

- `Bin/`, `Images/`, `lib/`, `Watchface Addons/` are gitignored — present locally but not tracked.
- `Bin/` holds pre-built OTA binaries for flashing without compiling (see `Bin/readme.md`).
- `watchy_state_machine.md` / `.html` — Mermaid state-diagram documentation of the two state machines; keep in sync when adding/removing states.
- `USER_GUIDE.md` — end-user-facing button reference; update it when button behavior changes.
