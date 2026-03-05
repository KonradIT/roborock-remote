#!/usr/bin/env python3
"""
Configure M5Stack Roborock remote via serial.

Reads ~/.roborock (created by python-roborock CLI) and sends the
necessary credentials + device info to the M5Stack over serial.

Usage:
    python configure_device.py --port COM5
    python configure_device.py --port /dev/ttyACM0 --config ~/.roborock
"""

import argparse
import json
import sys
import time
from pathlib import Path

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("pyserial is required:  pip install pyserial")
    sys.exit(1)


REGION_URLS = {
    "us": "https://usiot.roborock.com",
    "eu": "https://euiot.roborock.com",
    "cn": "https://cniot.roborock.com",
    "ru": "https://ruiot.roborock.com",
}


def list_serial_ports():
    ports = serial.tools.list_ports.comports()
    return [(p.device, p.description) for p in ports]


def read_roborock_file(path: Path) -> dict:
    with open(path) as f:
        return json.load(f)


def pick_device(devices: list[dict]) -> dict:
    if not devices:
        print("No devices found in home data.")
        sys.exit(1)
    if len(devices) == 1:
        d = devices[0]
        print(f"  Using only device: {d.get('name', '?')} ({d.get('duid', '?')})")
        return d
    print("\nAvailable devices:")
    for i, d in enumerate(devices):
        name = d.get("name", "?")
        duid = d.get("duid", "?")
        online = d.get("online", False)
        tag = " [online]" if online else ""
        print(f"  {i}: {name} ({duid}){tag}")
    idx = int(input("Select device number: "))
    return devices[idx]


def send_config(ser: serial.Serial, config: dict[str, str]):
    def tx(line: str):
        ser.write((line + "\n").encode())
        time.sleep(0.05)

    tx("CONFIG_BEGIN")
    for key, value in config.items():
        if value:
            tx(f"{key}={value}")
    tx("CONFIG_END")

    # Read acknowledgements and log lines
    deadline = time.time() + 3
    while time.time() < deadline:
        if ser.in_waiting:
            line = ser.readline().decode(errors="replace").strip()
            if line:
                print(f"  << {line}")
            deadline = time.time() + 1  # extend on activity
        else:
            time.sleep(0.1)


def main():
    parser = argparse.ArgumentParser(description="Configure M5Stack Roborock remote")
    parser.add_argument("--port", "-p", help="Serial port (e.g. COM5, /dev/ttyACM0)")
    parser.add_argument("--baud", "-b", type=int, default=115200)
    parser.add_argument("--config", "-c", type=Path, default=Path.home() / ".roborock")
    args = parser.parse_args()

    # --- Read config file ---
    if not args.config.exists():
        print(f"Config file not found: {args.config}")
        print("Run 'roborock login --email <email>' first to create it,")
        print("then 'roborock discover' to populate device data.")
        sys.exit(1)

    data = read_roborock_file(args.config)
    email = data.get("email", "")
    ud = data.get("userData", {})
    token = ud.get("token", "")
    rriot = ud.get("rriot", {})
    rriot_ref = rriot.get("r", {})

    if not rriot.get("u"):
        print("ERROR: userData.rriot is missing — run 'roborock login' first.")
        sys.exit(1)

    # Determine base_url from region
    region = ud.get("region", "us")
    base_url = REGION_URLS.get(region, REGION_URLS["us"])

    # --- Home data ---
    cache = data.get("cacheData") or {}
    home = cache.get("homeData") or {}
    home_id = home.get("id")
    devices = home.get("devices", []) + home.get("receivedDevices", [])

    if not home_id or not devices:
        print("Home data not cached. Run 'roborock discover' to fetch it,")
        print("then re-run this script.")
        sys.exit(1)

    device = pick_device(devices)

    # --- WiFi ---
    print()
    wifi_ssid = input("WiFi SSID: ").strip()
    wifi_pass = input("WiFi Password: ").strip()

    config_payload = {
        "wifi_ssid": wifi_ssid,
        "wifi_pass": wifi_pass,
        "email":     email,
        "token":     token,
        "base_url":  base_url,
        "rriot_u":   rriot.get("u", ""),
        "rriot_s":   rriot.get("s", ""),
        "rriot_h":   rriot.get("h", ""),
        "rriot_k":   rriot.get("k", ""),
        "rriot_a":   rriot_ref.get("a", ""),
        "rriot_m":   rriot_ref.get("m", ""),
        "home_id":   str(home_id),
        "dev_duid":  device.get("duid", ""),
        "dev_name":  device.get("name", ""),
        "local_key": device.get("localKey", ""),
    }

    # --- Serial port ---
    port = args.port
    if not port:
        ports = list_serial_ports()
        if not ports:
            print("No serial ports found.")
            sys.exit(1)
        print("\nAvailable serial ports:")
        for i, (dev, desc) in enumerate(ports):
            print(f"  {i}: {dev}  ({desc})")
        idx = int(input("Select port number: "))
        port = ports[idx][0]

    print(f"\nOpening {port} @ {args.baud}...")
    ser = serial.Serial(port, args.baud, timeout=2)
    time.sleep(2)  # wait for ESP32 reboot after serial open

    # Drain any boot messages
    while ser.in_waiting:
        line = ser.readline().decode(errors="replace").strip()
        if line:
            print(f"  << {line}")

    print("Sending configuration...")
    send_config(ser, config_payload)
    ser.close()
    print("\nDone! The device should now connect to WiFi and fetch robot status.")


if __name__ == "__main__":
    main()
