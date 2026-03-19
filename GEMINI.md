# Watchy4Granny (Watchy_GSR Project)

## Project Overview

This is an Arduino/PlatformIO-based firmware project for the **Watchy** e-paper smartwatch. It leverages the highly customizable [Watchy_GSR](https://github.com/GuruSR/Watchy_GSR) framework. The framework is designed to provide robust power management, structured menus, and an easy-to-use API for creating custom watch faces and features. The code supports multiple hardware versions, including the original ESP32-based Watchy (v1-v2) and the newer ESP32-S3-based Watchy (v3).

## Building and Running

The project uses **PlatformIO** for dependency management and compiling.

### Available Environments (from `platformio.ini`)
*   `watchy-v1-v2`: For standard ESP32 Watchy devices (v1.5, v2.0).
*   `watchy-v3`: For the newer ESP32-S3 Watchy devices (v3.0).
*   `nowatchy`: A default/placeholder environment.

### Common Commands
*   **Build the firmware:**
    ```bash
    pio run -e <environment_name>
    # Example for Watchy v2: pio run -e watchy-v1-v2
    ```
*   **Upload to the Watchy:**
    ```bash
    pio run -e <environment_name> -t upload
    # Example for Watchy v3: pio run -e watchy-v3 -t upload
    ```
*   **Monitor Serial Output:**
    ```bash
    pio run -e <environment_name> -t monitor
    ```

## Development Conventions

*   **Entry Point:** The main application logic is contained in `src/GSR.ino`.
*   **Extending Functionality:** Custom functionality, watch faces, and behavior modifications are implemented by extending the `WatchyGSR` base class. This is done within the `OverrideGSR` class in `src/GSR.ino`.
*   **Custom Watch Faces:** You can add custom styles by overriding methods like `InsertAddWatchStyles()`, `InsertInitWatchStyle(uint8_t StyleID)`, and `InsertDrawWatchStyle(uint8_t StyleID)`.
*   **Button Handling:** Custom button behavior can be defined by overriding the `InsertHandlePressed()` method.
*   **Deep Sleep State:** Variables that need to retain their values across deep sleep cycles must use the `RTC_DATA_ATTR` attribute (e.g., `RTC_DATA_ATTR uint8_t MyStyle;`).
