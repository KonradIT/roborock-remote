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

# gotten from: https://github.com/Python-roborock/python-roborock/blob/b658ef1c125ef5c321513e7c22bd99af814a2934/roborock/web_api.py#L34
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
        print("Run './scripts/setup.sh <email>' first to create it,")
        sys.exit(1)

    data = read_roborock_file(args.config)
    email = data.get("email", "")
    ud = data.get("userData", {})
    token = ud.get("token", "")
    rriot = ud.get("rriot", {})
    rriot_ref = rriot.get("r", {})

    if not rriot.get("u"):
        print("ERROR: userData.rriot is missing - run './scripts/setup.sh <email>' first.")
        sys.exit(1)

    # Determine base_url from region
    region = ud.get("region", "us")
    base_url = REGION_URLS.get(region, REGION_URLS["us"])

    # --- Home data ---
    cache = data.get("cacheData") or {}
    home = cache.get("homeData") or {}
    device_info = cache.get("deviceInfo") or {}
    home_id = home.get("id")
    devices = home.get("devices", []) + home.get("receivedDevices", [])

    if not home_id or not devices:
        print("Home data not cached. Run 'roborock discover' to fetch it,")
        print("then re-run this script.")
        sys.exit(1)

    device = pick_device(devices)

    # --- Rooms ---
    home_rooms = home.get("rooms", [])
    rooms_json = ""
    if home_rooms:
        rooms_data = [{"id": r["id"], "name": r["name"]} for r in home_rooms]
        rooms_json = json.dumps(rooms_data, separators=(",", ":"))
        print(f"\n  Found {len(rooms_data)} rooms: {', '.join(r['name'] for r in rooms_data)}")

    # --- WiFi & device IP ---
    wifi_ssid = ""
    if device_info:
        wifi_ssid_in_config = device_info.get(device.get("duid", ""), {}).get("networkInfo", {}).get("ssid", "")
        if wifi_ssid_in_config:
            wifi_ssid = wifi_ssid_in_config
            print(f"  Using WiFi SSID: {wifi_ssid}")
        else:
            wifi_ssid = input("WiFi SSID: ").strip()
    else:
        wifi_ssid = input("WiFi SSID: ").strip()
    
    wifi_pass = input("WiFi Password: ").strip()
    
    dev_ip = "" # Initially blank, let user decide cloud/local mode.

    mode_chosen = input("Choose mode (cloud/local): ").strip()
    if mode_chosen == "local":
        dev_ip = device_info.get(device.get("duid", ""), {}).get("networkInfo", {}).get("ip", "")
        if not dev_ip:
            print("No device IP found in home data. Please enter it manually.")
            dev_ip = input("Robot IP address: ").strip()
        else:
            print(f"  Using device IP: {dev_ip}")

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
        "dev_ip":    dev_ip,
        "rooms":     rooms_json,
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
