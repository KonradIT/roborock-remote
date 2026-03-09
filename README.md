# Roborock Vacuum Remote 

<img width="2069" height="1135" alt="1772790136858 (1)" src="https://github.com/user-attachments/assets/ed1ffd94-04ce-49b3-a6c5-ae8be9bc70a3" />

## Supported Hardware

- M5Stack Stick S3
- Roborock S8 (other V1-protocol models may work but are untested)

## Prerequisites

1. **PlatformIO** -- install via [VS Code extension](https://platformio.org/install/ide?install=vscode) or CLI
2. **Python 3** with `pyserial` -- `pip install pyserial`
3. **python-roborock** -- `pip install python-roborock`

### Operating modes

- Local: Stick will connect directly to the robot over TCP port 58867 using the IP address from the roborock config file.
- Cloud: Stick will connect to the Roborock cloud using the MQTT protocol using the specified MQTT URL from the roborock config file.

- To switch from cloud to local mode (or vice versa), run the provisioning script and specify operating mode, if local it'll also use the IP address from the roborock config file.
- Erase flash (`pio run --target erase`) to fully reset all stored configuration.

## Usage

**Main screen** -- shows connection mode (LOCAL/CLOUD), online status, robot name, battery, and state.

- Blue button: start the cleaning flow: room selection, mode, suction, confirm, start
- Side button: refresh status

During cleaning, the screen shows a circular progress gauge with elapsed time, battery, and state.

## IMU-based joystick control

StickS3 has a 3 axis IMU, using the dedicated RC mode (will appear first as a room) you move the StickS3 around to control the robot.

https://github.com/user-attachments/assets/80223967-6021-481d-8c7e-52a1edd7f07a

Press blue button => "RC" room.

## Building and Flashing

Open project in VS Code and build with PlatformIO.

Select `m5stack-sticks3` in the PlatformIO environment dropdown. Make sure to select the correct port.

## Configuration

After flashing, the device will display an initial setup waiting screen and wait for credentials over serial.

Install the dependencies needed:

```bash
pip install -r scripts/requirements.txt
```

This will install the python-roborock library and the pyserial library.

### 1. Run the provisioning script

First follow the steps to generate a `$HOME/.roborock` credentials file using Python-roborock: https://github.com/Python-roborock/python-roborock

```bash
./scripts/setup.sh <email>
```

This will send an email to the specified email address with a code. Input it and follow the instructions. 

Then:

```bash
python scripts/configure_device.py --port <PORT>
```

The provisioning script reads `~/.roborock` which was just created by the `roborock` command in setup.sh, then it extracts the required credentials, and prompts you for:

- **WiFi password** for the SSID specified in the roborock config file.
- **Robot IP address** -- provide this for local mode if not specified in the file.
- **Operating mode**: cloud/local

The device will reboot and connect automatically.

### Defines in platformio.ini

- `REFRESH_MS` -- refresh rate for the main screen (default: 300000ms)
- `WIFI_TIMEOUT` -- timeout for WiFi connection (default: 15000ms)
- `NTP_TIMEOUT` -- timeout for NTP sync (default: 10000ms)
- `MQTT_STATUS_TIMEOUT` -- timeout for MQTT status (default: 8000ms)
- `SERIAL_BAUD` -- baud rate for the serial connection (default: 115200)
- `COL_HEADER` -- color for the header (default: 0x1A74)
- `COL_ACCENT` -- color for the accent (default: 0x07FF)
- `RC_EXPO_MID_PCT` -- middle point for the RC exponential control (default: 50)
- `RC_EXPO_ENABLED` -- enable/disable the RC exponential control (default: 1)

### Protocol Notes

The Roborock V1 local protocol communicates over TCP port 58867 with a custom binary framing:

- **4-byte length prefix** (big-endian) followed by the message content
- **Message header**: version (`1.0`, 3 bytes) + sequence (4B) + random (4B) + timestamp (4B) + protocol ID (2B)
- **Payload**: AES-128-ECB encrypted JSON, key derived from `MD5(encode_timestamp(ts) + local_key + SALT)`
- **CRC32** checksum appended after the payload
- Protocol IDs: `0/1` (hello), `2/3` (ping), `4/5` (general request/response for local)

**IMPORTANT**: The cloud MQTT protocol uses the same binary message format but with protocol IDs `101/102` and an additional `security` field in the RPC payload.

### Todo

- Add `#ifdef` defines to the code to abstract from M5Stack specific code for display render/buttons/IMU, etc... This will allow the code to be used on other boards and other manufacturers.
- Support more boards, I just happen to have the StickS3 and the ESP32-S3 allows for beefier code to be used with it's 8MB of RAM.
- Add support for more robots from Roborock! I only have the S8 so I can't test other models.

### Credits

- [Python-roborock/python-roborock](https://github.com/Python-roborock/python-roborock) -- the reference implementation for Roborock protocol reverse engineering, authentication flow, and local/MQTT protocol details. This project would not exist without their work.
- [bblanchon/ArduinoJson](https://arduinojson.org/)
- [knolleary/PubSubClient](https://pubsubclient.knolleary.net/)

### License

This project is provided as-is for personal use. The Roborock protocol details are heavily based on the efforts of Python-roborock/python-roborock project.
