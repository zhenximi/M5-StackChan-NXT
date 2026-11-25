# 🤖 M5-StackChan-NXT

Control a **Lego Mindstorms NXT** brick using an **M5Stack CoreS3 (Stack-chan)** over direct **USB cable**, steered via an interactive touch WebUI with synchronized facial expressions!

![M5-StackChan-NXT Banner](https://img.shields.io/badge/M5Stack-CoreS3-blue?logo=espressif)
![ESP32-S3](https://img.shields.io/badge/ESP32--S3-USB--Host-green)
![Lego Mindstorms NXT](https://img.shields.io/badge/Lego-Mindstorms%20NXT-orange)

---

## ✨ Features

- **Direct USB OTG Host Control**: Uses ESP32-S3's native USB Host controller to communicate directly with the Lego NXT (`VID 0x0694`, `PID 0x0002`) over USB bulk transfers (Endpoint `0x01`).
- **Differential Steering**:
  - **Left Motor**: Port B
  - **Right Motor**: Port C
- **Interactive Mobile WebUI**:
  - Touch-friendly D-Pad with automatic deadman's switch (auto-stop on release).
  - Variable speed slider (30% to 100%).
  - Sound effects trigger (Beep, Fanfare) on the NXT internal speaker.
  - Desktop keyboard controls (`W`/`A`/`S`/`D` or Arrow Keys, Space to stop).
  - Real-time NXT USB connection status monitor.
- **Stack-chan Reactive Face**:
  - Automatically reacts to robot movement (smiles when driving, shifts gaze left/right during turns, looks down in reverse).
  - Natural blinking cycle.
  - Surprised expression when playing sounds!

---

## 🛠️ Hardware Requirements

1. **M5Stack CoreS3** (Stack-chan): ESP32-S3 with native USB OTG Host support and AXP2101 power management.
2. **Lego Mindstorms NXT 1.0 or 2.0 Brick**: Powered on with batteries.
3. **Motors**:
   - Left Motor plugged into **Port B**.
   - Right Motor plugged into **Port C**.
4. **USB Cable**:
   - USB-C to USB-B cable, OR
   - USB-C OTG male-to-USB-A female adapter + standard Lego NXT USB-A to USB-B cable.

---

## 🔌 Connection & Power

1. Plug the USB-C end into the M5Stack CoreS3.
2. Plug the USB-B end into the Lego NXT brick.
3. Turn on the Lego NXT.
4. When CoreS3 boots, it enables 5V VBUS power on the USB-C port via `M5.Power.setUsbOutput(true)`. The Lego NXT brick will detect the host connection immediately.

---

## 🚀 Building & Flashing

### Using PlatformIO (Recommended)

1. Clone this repository:
   ```bash
   git clone https://github.com/zhenximi/M5-StackChan-NXT.git
   cd M5-StackChan-NXT
   ```
2. Open the folder in VS Code with the PlatformIO extension.
3. Connect the M5Stack CoreS3 via USB to your computer.
4. Click **PlatformIO: Upload** (or run `pio run -t upload`).

### Using Arduino IDE

1. Install **ESP32 by Espressif Systems** (version >= 2.0.14 or 3.x) in the Boards Manager.
2. Select Board: **M5Stack CoreS3** (or `ESP32S3 Dev Module` with `USB CDC On Boot: Enabled`, `PSRAM: OPI PSRAM`).
3. Install the **M5Unified** library via the Library Manager.
4. Open `src/main.cpp` (or rename to `.ino`), ensure `nxt_driver.h`, `avatar.h`, and `web_ui.h` are in the same directory, and click **Upload**.

---

## 🎮 How to Control

1. Turn on the Lego NXT brick and power up the M5Stack CoreS3.
2. Connect your smartphone, tablet, or laptop to the Wi-Fi Access Point:
   - **SSID**: `StackChan-NXT`
   - **Password**: `12345678`
3. Open your browser and navigate to:
   ```
   http://192.168.4.1
   ```
4. The status badge will show `● Lego NXT Connected`.
5. Hold down the D-pad buttons to drive! Releasing the button safely brakes the robot.

---

## 📜 Lego NXT Direct Command Protocol Reference

The Lego NXT communicates via standard USB Bulk transfers without the 2-byte RFCOMM length header used over Bluetooth:

### Motor Drive (`SETOUTPUTSTATE` - 12 bytes)
| Byte | Value | Description |
|---|---|---|
| 0 | `0x80` | Direct command, no reply required |
| 1 | `0x04` | Command: `SETOUTPUTSTATE` |
| 2 | `0x01` / `0x02` | Port B (Left) or Port C (Right) |
| 3 | `-100` to `100` | Motor power |
| 4 | `0x07` / `0x02` | Mode (`MOTORON \| BRAKE \| REGULATED`) |
| 5 | `0x01` / `0x00` | Regulation: `MOTOR_SPEED` |
| 6 | `0x00` | Turn ratio |
| 7 | `0x20` / `0x00` | Run state: `RUNNING` |
| 8–11 | `0x00000000` | Tacho limit (`0` = run continuously) |

### Play Tone (`PLAYTONE` - 6 bytes)
| Byte | Value | Description |
|---|---|---|
| 0 | `0x80` | Direct command, no reply required |
| 1 | `0x03` | Command: `PLAYTONE` |
| 2–3 | Frequency | uint16_t in Hz (Little-Endian) |
| 4–5 | Duration | uint16_t in ms (Little-Endian) |

---

## 📄 License

MIT License. Feel free to use, modify, and build awesome Lego robots with Stack-chan!

## Voice control (offline, English)
NXT.CMD listens on StackChan's mic using ESP-SR MultiNet7 (`mn7_en`), no internet needed.
Say: "go forward", "go back", "turn left", "turn right", "spin around", "stop", "faster", "slower", "beep".
Setup: use `stackchan_app/partitions.csv` (adds `nxtvoice` partition at 0x710000; shrinks `ota_1` so OTA updates won't fit), then pack and flash the model:
```bash
mkdir -p build/nxt_models && cp -r managed_components/espressif__esp-sr/model/multinet_model/mn7_en build/nxt_models/
python managed_components/espressif__esp-sr/model/pack_model.py -m build/nxt_models -o ../nxt_srmodels.bin
python -m esptool --chip esp32s3 -b 921600 --after watchdog-reset write-flash --flash-mode dio --flash-size 16MB \
  0x8000 build/partition_table/partition-table.bin 0x20000 build/stack-chan.bin 0x710000 build/nxt_srmodels.bin
```
