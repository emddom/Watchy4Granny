#!/usr/bin/env python3
"""
Watchy4Granny NVS Backup, Restore, and Decoder Tool.

Reads/writes the ESP32 NVS partition (offset 0x9000, size 0x5000) and decodes:
1. All raw ESP32 NVS namespaces and key-value pairs (including "storage" and "nvs.net80211").
2. Watchy_GSR's Base64-encoded "GSR-Options" blob (WiFi APs, passwords, TX power,
   custom NTP server, NTP sync settings, weather static coordinates, display/button
   preferences, alarms, timers, and RTC drift) and "GSR-TZ" POSIX timezone.
"""

import argparse
import base64
import glob
import json
import os
import struct
import sys
import types

NVS_OFFSET = 0x9000
NVS_SIZE = 0x5000  # 20 KB (5 pages of 4096 bytes)

TX_POWER_LABELS = [
    "Default",
    "19.5dBm [Max]",
    "19dBm",
    "18.5dBm",
    "17dBm",
    "15dBm [Laptop]",
    "13dBm",
    "11dBm",
    "8.5dBm [Medium]",
    "7dBm",
    "5dBm [10 meters]",
    "2dBm [Low]",
]


def load_esptool():
    """Load esptool from system or local pip wheel cache."""
    try:
        import esptool.cmds as cmds
        return cmds
    except ImportError:
        import click
        click.RichGroup = click.Group
        click.RichCommand = click.Command
        click.rich_click = types.SimpleNamespace()
        sys.modules["rich_click"] = click

        ih = types.ModuleType("intelhex")
        ih.IntelHex = object
        ih.RecordError = Exception
        ih.HexRecordError = Exception
        sys.modules["intelhex"] = ih

        rs = types.ModuleType("reedsolo")
        rs.RSCodec = object
        rs.ReedSolomonError = Exception
        sys.modules["reedsolo"] = rs

        wheels = sorted(
            glob.glob(os.path.expanduser("~/.cache/pip/wheels/**/esptool-*.whl"), recursive=True),
            reverse=True,
        )
        if not wheels:
            raise RuntimeError("esptool not found in Python environment or ~/.cache/pip/wheels")
        extract_dir = "/tmp/watchy_esptool_pkg"
        if not os.path.exists(os.path.join(extract_dir, "esptool")):
            import zipfile
            os.makedirs(extract_dir, exist_ok=True)
            with zipfile.ZipFile(wheels[0], "r") as zf:
                zf.extractall(extract_dir)
        sys.path.insert(0, extract_dir)
        import esptool.cmds as cmds
        return cmds


def parse_nvs_partition(data: bytes) -> dict:
    """Parse an ESP32 NVS partition binary into namespaces and keys."""
    PAGE_SIZE = 4096
    PAGE_ACTIVE = 0xFFFFFFFE
    PAGE_FULL = 0xFFFFFFFC

    namespaces = {0: "__namespaces__"}
    result = {}
    blob_chunks = {}  # (ns_idx, key) -> list of (chunk_idx, bytes)

    # Pass 1: Discover all namespace index -> name mappings across all pages
    for page_offset in range(0, len(data), PAGE_SIZE):
        page = data[page_offset : page_offset + PAGE_SIZE]
        if len(page) < PAGE_SIZE:
            break
        state = struct.unpack_from("<I", page, 0)[0]
        if state not in (PAGE_ACTIVE, PAGE_FULL):
            continue
        bitmap = page[32:64]
        for entry_idx in range(126):
            estate = (bitmap[(entry_idx * 2) // 8] >> ((entry_idx * 2) % 8)) & 0x03
            if estate != 2:
                continue
            entry_offset = 64 + entry_idx * 32
            entry = page[entry_offset : entry_offset + 32]
            ns_idx, item_type, span, _, _ = struct.unpack_from("<BBBBI", entry, 0)
            if ns_idx == 0 and item_type == 0x01:
                key = entry[8:24].split(b"\x00", 1)[0].decode("utf-8", errors="replace")
                val = entry[24]
                namespaces[val] = key
                result.setdefault(key, {})

    # Pass 2: Parse all key-value entries across all pages
    for page_offset in range(0, len(data), PAGE_SIZE):
        page = data[page_offset : page_offset + PAGE_SIZE]
        if len(page) < PAGE_SIZE:
            break

        state = struct.unpack_from("<I", page, 0)[0]
        if state not in (PAGE_ACTIVE, PAGE_FULL):
            continue

        # Entry state bitmap is at offset 32..64 (126 entries, 2 bits each)
        bitmap = page[32:64]

        def get_entry_state(idx: int) -> int:
            byte_idx = (idx * 2) // 8
            bit_offset = (idx * 2) % 8
            return (bitmap[byte_idx] >> bit_offset) & 0x03

        entry_idx = 0
        while entry_idx < 126:
            estate = get_entry_state(entry_idx)
            # 0b10 (2) = WRITTEN
            if estate != 2:
                entry_idx += 1
                continue

            entry_offset = 64 + entry_idx * 32
            entry = page[entry_offset : entry_offset + 32]
            ns_idx, item_type, span, chunk_idx, crc = struct.unpack_from("<BBBBI", entry, 0)
            key_raw = entry[8:24]
            key = key_raw.split(b"\x00", 1)[0].decode("utf-8", errors="replace")
            raw_data = entry[24:32]

            span = max(1, span)
            payload = b""
            if span > 1:
                payload = page[entry_offset + 32 : entry_offset + span * 32]

            if ns_idx == 0 and item_type == 0x01:
                val = raw_data[0]
                namespaces[val] = key
                result.setdefault(key, {})
            else:
                ns_name = namespaces.get(ns_idx, f"ns_{ns_idx}")
                ns_dict = result.setdefault(ns_name, {})

                if item_type == 0x01:  # U8
                    ns_dict[key] = struct.unpack_from("<B", raw_data, 0)[0]
                elif item_type == 0x11:  # I8
                    ns_dict[key] = struct.unpack_from("<b", raw_data, 0)[0]
                elif item_type == 0x02:  # U16
                    ns_dict[key] = struct.unpack_from("<H", raw_data, 0)[0]
                elif item_type == 0x12:  # I16
                    ns_dict[key] = struct.unpack_from("<h", raw_data, 0)[0]
                elif item_type == 0x04:  # U32
                    ns_dict[key] = struct.unpack_from("<I", raw_data, 0)[0]
                elif item_type == 0x14:  # I32
                    ns_dict[key] = struct.unpack_from("<i", raw_data, 0)[0]
                elif item_type == 0x08:  # U64
                    ns_dict[key] = struct.unpack_from("<Q", raw_data, 0)[0]
                elif item_type == 0x18:  # I64
                    ns_dict[key] = struct.unpack_from("<q", raw_data, 0)[0]
                elif item_type == 0x21:  # SZ (null-terminated string)
                    size = struct.unpack_from("<H", raw_data, 0)[0]
                    s_bytes = payload[:size].rstrip(b"\x00")
                    ns_dict[key] = s_bytes.decode("utf-8", errors="replace")
                elif item_type == 0x41:  # BLOB (legacy single-span)
                    size = struct.unpack_from("<H", raw_data, 0)[0]
                    ns_dict[key] = {"_blob_hex": payload[:size].hex(), "_size": size}
                elif item_type == 0x42:  # BLOB_DATA (multi-chunk blob)
                    size = struct.unpack_from("<H", raw_data, 0)[0]
                    blob_chunks.setdefault((ns_idx, key), []).append((chunk_idx, payload[:size]))
                elif item_type == 0x48:  # BLOB_IDX
                    total_size = struct.unpack_from("<I", raw_data, 0)[0]
                    chunks = sorted(blob_chunks.get((ns_idx, key), []), key=lambda x: x[0])
                    full_blob = b"".join(c[1] for c in chunks)[:total_size]
                    # Also check if printable ascii
                    try:
                        ascii_str = full_blob.rstrip(b"\x00").decode("ascii")
                        if all(32 <= ord(ch) < 127 for ch in ascii_str) and len(ascii_str) > 0:
                            ns_dict[key] = {"_blob_hex": full_blob.hex(), "_ascii": ascii_str, "_size": total_size}
                        else:
                            ns_dict[key] = {"_blob_hex": full_blob.hex(), "_size": total_size}
                    except UnicodeDecodeError:
                        ns_dict[key] = {"_blob_hex": full_blob.hex(), "_size": total_size}

            entry_idx += span

    return result


def decode_gsr_options(b64_str: str) -> dict:
    """Decode Watchy_GSR's Base64 'GSR-Options' string (StoreSettings format)."""
    if not b64_str or len(b64_str) < 5:
        return {}

    raw = base64.b64decode(b64_str)
    # Note: StoreSettings does `L--` (takes dead zero off end if encoded length included trailing 0)
    # Wait: GetSettings passes `J` bytes to mbedtls_base64_encode, and StoreSettings does `L--`
    L = len(raw) - 1 if len(raw) > 0 else 0
    O = raw

    J = 0
    new_v = 0
    if L > J and O[J] > 128:
        new_v = O[J]
        J += 1

    decoded = {"version": new_v}

    if L > J:
        decoded["steps_reset_hour"] = O[J]
    J += 1
    if L > J:
        decoded["steps_reset_minutes"] = O[J]
    J += 1

    if L > J:
        v = O[J]
        decoded["options"] = {
            "twenty_four_hour": bool(v & 1),
            "light_mode": bool(v & 2),
            "feedback_haptic": bool(v & 4),
            "border": bool(v & 8),
            "lefty": bool(v & 16),
            "swapped": bool(v & 32),
            "orientated": bool(v & 64),
        }

    dx = 0
    di = 0
    if new_v > 128:
        J += 1
        if L > J + 1:
            if new_v > 136:
                dx = struct.unpack_from("<I", O, J)[0]
                J += 4
                di = struct.unpack_from("<I", O, J)[0]
                J += 3  # StoreSettings increments J+=3 here because J++ follows
            else:
                if new_v < 133:
                    J += 1
                if 133 < new_v < 137:
                    J += 1
        J += 1
        if L > J:
            decoded["options"]["performance"] = (O[J] & 240) >> 4
            decoded["options"]["sleep_style"] = O[J] & 7
        J += 1
        if L > J:
            decoded["options"]["sleep_mode"] = O[J]
        J += 1
        if L > J:
            decoded["options"]["sleep_start_hour"] = O[J]
        J += 1
        if L > J:
            decoded["options"]["sleep_end_hour"] = O[J]

        if new_v > 129:
            J += 1
            if L > J:
                tx_idx = O[J]
                decoded["wifi_default_tx_power_index"] = tx_idx
                decoded["wifi_default_tx_power"] = (
                    TX_POWER_LABELS[tx_idx + 1] if 0 <= tx_idx + 1 < len(TX_POWER_LABELS) else str(tx_idx)
                )
            J += 1
            if L > J:
                v = O[J]
                decoded["options"]["bedtime_orientation"] = bool(v & 1)
                decoded["weather_metric"] = bool(v & 2)

        if new_v > 130:
            J += 1
            if L > J:
                decoded["options"]["watch_face_style_id"] = O[J]
            if new_v > 135:
                J += 1
                if L > J:
                    decoded["options"]["game_id"] = O[J]

        if new_v > 131:
            J += 1
            if L > J:
                decoded["options"]["language_id"] = O[J]

    J += 1
    if L > J:
        decoded["options"]["master_repeats"] = (O[J] & 224) >> 5
        decoded["options"]["turbo_seconds"] = O[J] & 31

    J += 1
    timer_down = {}
    if L > J:
        timer_down["max_tones"] = (O[J] & 224) >> 5
        timer_down["max_hours"] = O[J] & 31
    J += 1
    if L > J:
        timer_down["max_mins"] = O[J]
    if new_v > 132:
        J += 1
        if L > J:
            timer_down["max_secs"] = O[J] & 127
            timer_down["repeats"] = bool(O[J] & 128)
    decoded["countdown_timer"] = timer_down

    alarms = []
    for k in range(4):
        alarm = {"index": k + 1}
        J += 1
        if L > J:
            alarm["repeats"] = (O[J] & 224) >> 5
            alarm["hour"] = O[J] & 31
        J += 1
        if L > J:
            alarm["minutes"] = O[J]
        J += 1
        if L > J + 1:
            flags = struct.unpack_from("<H", O, J)[0] & 511
            alarm["flags_raw"] = flags
            alarm["active"] = bool(flags & 256)
            alarm["repeat_flag"] = bool(flags & 128)
            alarm["days_mask"] = flags & 127
            J += 1
        alarms.append(alarm)
    decoded["alarms"] = alarms

    if new_v > 133:
        J += 1
        if L > J:
            fx = bool(O[J] & 1)
            J += 1
            fi = bool(O[J] & 8) if new_v > 136 else False
            ntp_autosync = bool(O[J] & 2)
            esprtc = bool(O[J] & 4)
            low_error = (O[J] & 240) / 1600.0
            decoded["rtc_drift"] = {
                "external_drift": dx,
                "external_fast": fx,
                "internal_drift": di,
                "internal_fast": fi,
                "use_esp32_rtc": esprtc,
            }
            decoded["battery_low_error"] = low_error
            decoded["ntp"] = {"auto_sync": ntp_autosync}
        J += 1
        if L > J:
            decoded.setdefault("ntp", {})["sync_hour"] = O[J]
            J += 1
        if L > J:
            decoded.setdefault("ntp", {})["sync_mins"] = O[J]
            J += 1
        if new_v > 137 and L > J:
            decoded.setdefault("ntp", {})["sync_days"] = O[J]
            J += 1
        if new_v > 139 and L > J:
            w = O[J]
            J += 1
            ntp_url = O[J : J + w].decode("utf-8", errors="replace") if w else ""
            J += w
            decoded.setdefault("ntp", {})["ntp_server_from_web"] = ntp_url
            decoded["ntp"]["effective_ntp_server"] = ntp_url if ntp_url else "pool.ntp.org (default)"
        if new_v > 134:
            if new_v > 138:
                lat_s = O[J : J + 13].split(b"\x00", 1)[0].decode("utf-8", errors="replace")
                J += 13
                lon_s = O[J : J + 13].split(b"\x00", 1)[0].decode("utf-8", errors="replace")
                J += 13
                decoded["weather"] = {
                    "static_lat": lat_s,
                    "static_lon": lon_s,
                    "use_static_pos": bool(lat_s and lon_s),
                }
            else:
                J += 32
            if L >= J:
                decoded.setdefault("weather", {})["interval_5min_units"] = O[J]
                decoded["weather"]["interval_minutes"] = O[J] * 5

    wifi_aps = []
    J += 1
    ap_slot = 0
    while L > J:
        tpwr_idx = 0
        if new_v > 129 and L > J:
            tpwr_idx = O[J]
            J += 1
        if L <= J:
            break
        w_ssid = O[J]
        J += 1
        ssid = O[J : J + w_ssid].decode("utf-8", errors="replace")
        J += w_ssid
        w_pass = 0
        passwd = ""
        if L > J:
            w_pass = O[J]
            J += 1
            passwd = O[J : J + w_pass].decode("utf-8", errors="replace")
            J += w_pass
        wifi_aps.append(
            {
                "slot": ap_slot,
                "label": f"AP-{chr(65 + ap_slot)}",
                "ssid": ssid,
                "password": passwd,
                "tx_power_index": tpwr_idx,
                "tx_power": TX_POWER_LABELS[tpwr_idx] if 0 <= tpwr_idx < len(TX_POWER_LABELS) else str(tpwr_idx),
            }
        )
        ap_slot += 1

    decoded["wifi_access_points"] = wifi_aps
    return decoded


def extract_and_decode(nvs_bytes: bytes) -> dict:
    """Parse NVS binary and decode Watchy_GSR settings into a structured dictionary."""
    raw_nvs = parse_nvs_partition(nvs_bytes)
    storage = raw_nvs.get("storage", {})
    gsr_options_b64 = storage.get("GSR-Options", "")
    gsr_tz = storage.get("GSR-TZ", "")

    summary = {
        "nvs_partition": {
            "offset": hex(NVS_OFFSET),
            "size": hex(NVS_SIZE),
        },
        "watchy_gsr": {
            "timezone_posix": gsr_tz,
            "steps_cached": storage.get("Steps", ""),
            "yesterday_steps": storage.get("YSteps", ""),
            "no_nvs": storage.get("NoNVS", ""),
            "raw_gsr_options_base64": gsr_options_b64,
            "decoded_options": decode_gsr_options(gsr_options_b64) if gsr_options_b64 else None,
        },
        "raw_nvs_namespaces": raw_nvs,
    }
    return summary


def cmd_backup(args):
    cmds = load_esptool()
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    os.makedirs(os.path.dirname(os.path.abspath(args.json)), exist_ok=True)

    print(f"Connecting to Watchy on {args.port}...")
    with cmds.detect_chip(port=args.port, baud=args.baud) as esp:
        chip_name = esp.get_chip_description()
        mac = ":".join(f"{b:02x}" for b in esp.read_mac())
        print(f"Connected to {chip_name} (MAC: {mac})")
        esp = esp.run_stub()
        print(f"Reading NVS partition at 0x{NVS_OFFSET:x} ({NVS_SIZE} bytes) -> {args.output}")
        cmds.read_flash(esp, NVS_OFFSET, NVS_SIZE, args.output)
        esp.hard_reset()

    with open(args.output, "rb") as f:
        nvs_bytes = f.read()

    summary = extract_and_decode(nvs_bytes)
    summary["device"] = {"chip": chip_name, "mac": mac, "port": args.port}

    with open(args.json, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)

    print(f"Saved decoded settings to {args.json}")
    return summary


def cmd_decode(args):
    with open(args.input, "rb") as f:
        nvs_bytes = f.read()
    summary = extract_and_decode(nvs_bytes)
    os.makedirs(os.path.dirname(os.path.abspath(args.json)), exist_ok=True)
    with open(args.json, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)
    print(f"Saved decoded settings to {args.json}")
    return summary


def cmd_restore(args):
    if not os.path.exists(args.input):
        raise FileNotFoundError(f"Backup file not found: {args.input}")
    cmds = load_esptool()
    print(f"Connecting to Watchy on {args.port} to restore NVS from {args.input}...")
    with cmds.detect_chip(port=args.port, baud=args.baud) as esp:
        esp = esp.run_stub()
        with open(args.input, "rb") as f:
            cmds.write_flash(esp, [(NVS_OFFSET, f)])
        esp.hard_reset()
    print("Successfully restored NVS partition and reset Watchy.")


def main():
    parser = argparse.ArgumentParser(description="Watchy4Granny NVS Backup/Decode/Restore Tool")
    sub = parser.add_subparsers(dest="command", required=True)

    p_backup = sub.add_parser("backup", help="Read NVS partition from Watchy and decode settings")
    p_backup.add_argument("--port", default="/dev/ttyUSB0", help="Serial port (default: /dev/ttyUSB0)")
    p_backup.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    p_backup.add_argument("--output", default="backups/nvs_backup.bin", help="Output raw NVS binary path")
    p_backup.add_argument("--json", default="backups/watchy_settings.json", help="Output decoded JSON path")

    p_decode = sub.add_parser("decode", help="Decode an existing NVS binary file")
    p_decode.add_argument("--input", default="backups/nvs_backup.bin", help="Input raw NVS binary path")
    p_decode.add_argument("--json", default="backups/watchy_settings.json", help="Output decoded JSON path")

    p_restore = sub.add_parser("restore", help="Restore raw NVS binary to Watchy")
    p_restore.add_argument("--port", default="/dev/ttyUSB0", help="Serial port (default: /dev/ttyUSB0)")
    p_restore.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    p_restore.add_argument("--input", default="backups/nvs_backup.bin", help="Input raw NVS binary path")

    args = parser.parse_args()
    if args.command == "backup":
        cmd_backup(args)
    elif args.command == "decode":
        cmd_decode(args)
    elif args.command == "restore":
        cmd_restore(args)


if __name__ == "__main__":
    main()
