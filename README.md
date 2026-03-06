# Roborock Vacuum Remote 

<img width="2069" height="1135" alt="1772790136858 (1)" src="https://github.com/user-attachments/assets/ed1ffd94-04ce-49b3-a6c5-ae8be9bc70a3" />

## Supported Hardware

- M5Stack Stick S3
- Roborock S8 (other V1-protocol models may work but are untested)

## Prerequisites

1. **PlatformIO** -- install via [VS Code extension](https://platformio.org/install/ide?install=vscode) or CLI
2. **Python 3** with `pyserial` -- `pip install pyserial`
3. **python-roborock** -- `pip install python-roborock`

## Building and Flashing

Open project in VS Code and build with PlatformIO.

Select `m5stack-sticks3` in the PlatformIO environment dropdown. Make sure to select the correct port.

## Configuration

After flashing, the device will display an initial setup waiting screen and wait for credentials over serial.

### 1. Run the provisioning script

First follow the steps to generate a `$HOME/.roborock` credentials file using Python-roborock: https://github.com/Python-roborock/python-roborock

Then:

```bash
python scripts/configure_device.py --port <PORT>
```

The script reads `~/.roborock`, extracts the required credentials, and prompts you for:

- **WiFi password**
- **Robot IP address** -- provide this for local/offline mode if not specified in the file.
- **Operating mode**: cloud/local

The device will reboot and connect automatically.

### 2. Switching modes

- To switch from cloud to local mode (or vice versa), re-run the provisioning script and specify operating mode, if local it'll also use the IP address from the roborock config file.
- Erase flash (`pio run --target erase`) to fully reset all stored configuration.

## Usage

**Main screen** -- shows connection mode (LOCAL/CLOUD), online status, robot name, battery, and state.

- Blue button: start the cleaning flow: room selection, mode, suction, confirm, start
- Side button: refresh status

During cleaning, the screen shows a circular progress gauge with elapsed time, battery, and state.

## Protocol Notes

The Roborock V1 local protocol communicates over TCP port 58867 with a custom binary framing:

- **4-byte length prefix** (big-endian) followed by the message content
- **Message header**: version (`1.0`, 3 bytes) + sequence (4B) + random (4B) + timestamp (4B) + protocol ID (2B)
- **Payload**: AES-128-ECB encrypted JSON, key derived from `MD5(encode_timestamp(ts) + local_key + SALT)`
- **CRC32** checksum appended after the payload
- Protocol IDs: `0/1` (hello), `2/3` (ping), `4/5` (general request/response for local)

**IMPORTANT**: The cloud MQTT protocol uses the same binary message format but with protocol IDs `101/102` and an additional `security` field in the RPC payload.

## Credits

- [Python-roborock/python-roborock](https://github.com/Python-roborock/python-roborock) -- the reference implementation for Roborock protocol reverse engineering, authentication flow, and local/MQTT protocol details. This project would not exist without their work.
- [bblanchon/ArduinoJson](https://arduinojson.org/)
- [knolleary/PubSubClient](https://pubsubclient.knolleary.net/)

## License

This project is provided as-is for personal use. The Roborock protocol details are heavily based on the efforts of Python-roborock/python-roborock project.
