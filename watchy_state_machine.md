# Watchy4Granny State Machines & Menu Navigation

This document outlines the state machines and complete menu navigation hierarchy controlling the Watchy4Granny device.

## 1. Complete Menu Navigation Mindmap

```mermaid
flowchart LR
    WF["Watch Face (WatchfaceState)"] -->|"SW1 (Menu)"| ROOT["Root Menu (GSR_MENU_INNORMAL)"]
    ROOT -->|"SW2 (Back)"| WF

    %% Root Menu Items (0..5)
    ROOT -->|"0"| M_STEPS["0: Steps (GSR_MENU_STEPS)"]
    ROOT -->|"1"| M_ALARMS["1: Alarms (GSR_MENU_ALARMS)"]
    ROOT -->|"2"| M_TIMERS["2: Timers (GSR_MENU_TIMERS)"]
    ROOT -->|"3 (if game loaded)"| M_GAME["3: Game (GSR_MENU_GAME)"]
    ROOT -->|"4"| M_OPTIONS["4: Options (GSR_MENU_OPTIONS)"]
    ROOT -->|"5 (Custom)"| M_UTILS["5: Utilities (GSR_MENU_UTILS)"]

    %% Steps Sub-actions
    M_STEPS -->|"SW1 (Edit)"| S_EDIT["Tomorrow Reset Time / Reset Today"]

    %% Utilities Submenu (GSR_MENU_INUTILS: 40..43)
    subgraph SUB_UTILS["Utilities Submenu (GSR_MENU_INUTILS)"]
        U_BTN["40: Show Buttons (GSR_MENU_UTILS_BTN)"]
        U_FIND["41: Find Phone (GSR_MENU_UTILS_FIND)"]
        U_MED["42: Med Info (GSR_MENU_UTILS_MED)"]
        U_WEAT["43: Weather (GSR_MENU_UTILS_WEAT)"]
    end
    M_UTILS -->|"SW1 (Enter)"| SUB_UTILS
    U_BTN -->|"SW1 -> InsertHandleShowButtons()"| ST_BTN["ShowButtonsState (10s Overlay)"]
    U_FIND -->|"SW1 -> InsertHandleFindPhone()"| ST_FIND["FindFoneState (WiFi + IFTTT Webhook)"]
    U_MED -->|"SW1 -> InsertHandleMedInfo()"| ST_MED["MedInfoState (Emergency Medical Info)"]
    U_WEAT -->|"SW1 -> InsertHandleWeather()"| ST_WEAT["WeatherState (Open-Meteo Weather Report)"]

    %% Alarms Submenu (GSR_MENU_INALARMS: 6..10)
    subgraph SUB_ALARMS["Alarms Submenu (GSR_MENU_INALARMS)"]
        A_1["6: Alarm 1 (Time, Repeats, Days, Active)"]
        A_2["7: Alarm 2 (Time, Repeats, Days, Active)"]
        A_3["8: Alarm 3 (Time, Repeats, Days, Active)"]
        A_4["9: Alarm 4 (Time, Repeats, Days, Active)"]
        A_TONES["10: Tone Repeats (Master)"]
    end
    M_ALARMS -->|"SW1 (Enter)"| SUB_ALARMS

    %% Timers Submenu (GSR_MENU_INTIMERS: 11..13)
    subgraph SUB_TIMERS["Timers Submenu (GSR_MENU_INTIMERS)"]
        T_DN["11: Countdown Timer (GSR_MENU_TIMEDN)"]
        T_CF["12: Countdown Config (GSR_MENU_TMEDCF)"]
        T_UP["13: Elapsed Timer (GSR_MENU_TIMEUP)"]
    end
    M_TIMERS -->|"SW1 (Enter)"| SUB_TIMERS

    %% Options Submenu (GSR_MENU_INOPTIONS: 14..34)
    subgraph SUB_OPTIONS["Options Submenu (GSR_MENU_INOPTIONS)"]
        O_UI["14-21: Watch Style, Language, Display, Border, Dexterity, Menu & Back, Orientation, Time Mode"]
        O_BEH["22-26: Feedback, Turbo Time, Screen Off, Performance, Information"]
        O_TRBL["27: Troubleshoot (GSR_MENU_TRBL)"]
        O_WIFI["28-33: WiFi Tx Power, Sync Watchy, Weather, Watchy Connect AP, OTA Update, OTA Website"]
        O_SUSP["34: Suspend Level (GSR_MENU_SUSP - Custom Battery %)"]
    end
    M_OPTIONS -->|"SW1 (Enter)"| SUB_OPTIONS

    %% Troubleshooting Submenu (GSR_MENU_INTROUBLE: 35..39)
    subgraph SUB_TROUBLE["Troubleshooting Submenu (GSR_MENU_INTROUBLE)"]
        TR_SCRN["35: Reset Screen (GSR_MENU_SCRN)"]
        TR_RSET["36: Watchy Reboot (GSR_MENU_RSET)"]
        TR_TOFF["37: Edit RTC / Drift (GSR_MENU_TOFF)"]
        TR_BERR["38: Battery Low Error (GSR_MENU_BERR)"]
        TR_UNVS["39: Storage Settings / NVS (GSR_MENU_UNVS)"]
    end
    O_TRBL -->|"SW1 (Enter)"| SUB_TROUBLE
```

## 2. Power & UI State Machine Diagram

```mermaid
stateDiagram-v2
    direction TB

    state "Power State Machine (PowerStateMachine)" as PSM {
        [*] --> SleepState : Boot / Initial
        SleepState --> AwakeState : handleWakeup() (RTC Timer / Button EXT1)
        AwakeState --> SleepState : shouldSleep() (!ActiveMode)
    }

    state "UI State Machine (UiStateMachine - Active during AwakeState)" as USM {
        [*] --> WatchfaceState : Initial state

        WatchfaceState --> MenuState : SW1 (Open Root Menu)
        WatchfaceState --> SuspendState : OnMinute (Battery % <= SuspendLevel & Not Charging)

        state "Native Menu System (MenuState)" as MenuState {
            [*] --> RootMenu : GSR_MENU_INNORMAL (0..5)
            RootMenu --> AlarmsSubmenu : SW1 on 1: Alarms (6..10)
            AlarmsSubmenu --> RootMenu : SW2 (Back)

            RootMenu --> TimersSubmenu : SW1 on 2: Timers (11..13)
            TimersSubmenu --> RootMenu : SW2 (Back)

            RootMenu --> OptionsSubmenu : SW1 on 4: Options (14..34)
            OptionsSubmenu --> RootMenu : SW2 (Back)
            OptionsSubmenu --> TroubleSubmenu : SW1 on 27: Troubleshoot (35..39)
            TroubleSubmenu --> OptionsSubmenu : SW2 (Back)

            RootMenu --> UtilitiesSubmenu : SW1 on 5: Utilities (40..43)
            UtilitiesSubmenu --> RootMenu : SW2 (Back)
        }

        MenuState --> WatchfaceState : SW2 from RootMenu / Auto-timeout (NoMenu())
        MenuState --> ShowButtonsState : Utilities -> 40: Show Buttons (SW1)
        MenuState --> FindFoneState : Utilities -> 41: Find Phone (SW1)
        MenuState --> MedInfoState : Utilities -> 42: Med Info (SW1)
        MenuState --> WeatherState : Utilities -> 43: Weather (SW1)

        ShowButtonsState --> WatchfaceState : Any Button (>1s debounce) / 10s Timeout / New Minute
        MedInfoState --> WatchfaceState : SW2 (Back) / New Minute
        WeatherState --> WatchfaceState : SW2 (Back) / New Minute (when idle)
        WeatherState --> WeatherState : SW3 (Up - Manual Weather Refresh)
        FindFoneState --> WatchfaceState : SW2 (Back) / Webhook Done + New Minute
        SuspendState --> WatchfaceState : SW3 (Up) while Charging (GetBatteryState() == 3)
    }

    AwakeState --> USM : ticks & dispatches buttons
```

### PowerStateMachine Summary
* **SleepState**: Puts the ESP32 into deep sleep to conserve battery.
* **AwakeState**: Powers on the display, reads sensors, runs `watchy->tick()`, and processes UI inputs. Automatically transitions to sleep when `ActiveMode` goes false.

### UiStateMachine Summary
* **WatchfaceState**: Default idle mode showing the time, battery icon, and step count.
* **MenuState**: Native `WatchyGSR` menu system (`Steps`, `Alarms`, `Timers`, `Game`, `Options`, and custom `Utilities` at the root level).
* **ShowButtonsState**: Triggered via `Menu -> Utilities -> Show Buttons`. Displays an overlay mapping `SW1`–`SW4` physical buttons to their logical functions. Ignores button inputs for the first 1 second to prevent bouncing from menu selection, then dismisses on any button press or a 10s timeout.
* **MedInfoState**: Triggered via `Menu -> Utilities -> Med Info`. Displays emergency medical info.
* **WeatherState**: Triggered via `Menu -> Utilities -> Weather`. Displays a full-screen weather report (icon, temperature, sky condition, feels-like, humidity, wind, and pressure) fetched from Open-Meteo, automatically requesting an update if stale and allowing manual refresh via `SW3`.
* **FindFoneState**: Triggered via `Menu -> Utilities -> Find Phone`. Connects to WiFi and pings the user's phone via IFTTT.
* **SuspendState**: A low-power lock screen triggered when the battery drops to/below the configured suspend threshold (`Options -> Suspend Level`). Can be awakened via `SW3` while charging.
