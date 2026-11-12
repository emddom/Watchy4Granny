---
name: watchy-firmware
description: >-
  Build, flash, inspect, and back up/restore NVS settings (WiFi credentials,
  NTP server, timezone, options) on the Watchy4Granny smartwatch. Use this
  skill whenever building or uploading firmware, reading or decoding device
  settings from the connected Watchy, or modifying Watchy state machines.
---

# Watchy4Granny Firmware & NVS Settings Skill

This skill provides workflows and helper utilities for managing the **Watchy4Granny** firmware and preserving device settings stored in the ESP32 Non-Volatile Storage (NVS) partition.

## 1. Backing Up & Decoding Watchy Settings (NVS)

Watchy_GSR stores persistent settings in the ESP32 `nvs` flash partition (`offset = 0x9000`, `size = 0x5000` / 20 KB on `min_spiffs.csv`) under the `"storage"` namespace:
- `GSR-Options`: Base64-encoded binary structure (Version 140) containing up to 10 WiFi SSIDs/passwords/TX power levels, custom NTP server URL, NTP sync schedule, weather static coordinates/interval, 24-hour/light/lefty/swapped button flags, sleep schedule, alarms, timers, and RTC drift.
- `GSR-TZ`: POSIX timezone string.
- `Steps` / `YSteps`: Cached step counts across reboots.

### Backup & Decode from Connected Watchy
Run [nvs_tool.py](./scripts/nvs_tool.py) to read the NVS partition from `/dev/ttyUSB0`, save the raw partition image to `backups/nvs_backup.bin`, and decode all settings to `backups/watchy_settings.json`:

```bash
python3 .agents/skills/watchy-firmware/scripts/nvs_tool.py backup --port /dev/ttyUSB0
```

### Decode an Existing NVS Binary
```bash
python3 .agents/skills/watchy-firmware/scripts/nvs_tool.py decode --input backups/nvs_backup.bin
```

### Restore NVS Backup to Connected Watchy
If NVS is ever erased or corrupted, restore the exact settings partition with:
```bash
python3 .agents/skills/watchy-firmware/scripts/nvs_tool.py restore --port /dev/ttyUSB0 --input backups/nvs_backup.bin
```

## 2. Building & Flashing Firmware (Podman Containerized Workflow)

Prefer running builds and uploads inside the `docker.io/espressif/idf:latest` Podman container via [podman_pio.sh](../../../scripts/podman_pio.sh):

1. **Back up NVS first** if a Watchy is connected on `/dev/ttyUSB0`.
2. **Compile the firmware in Podman**:
   ```bash
   ./scripts/podman_pio.sh run -e watchy-v1-v2
   ```
3. **Upload to the Watchy from Podman** (writes bootloader, partitions, and `app0` at `0x10000`, leaving `nvs` at `0x9000` intact):
   ```bash
   ./scripts/podman_pio.sh run -e watchy-v1-v2 -t upload
   ```
4. **Verify**: Check that compilation succeeds with `0` errors and `firmware.bin` is generated in `.pio/build/watchy-v1-v2/firmware.bin`.
