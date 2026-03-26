# Waveshare ESP32-S3-Touch-LCD-7 — Board Setup Guide

This guide covers setting up the Waveshare ESP32-S3-Touch-LCD-7 (V1.2) development
board for firmware development with PlatformIO on Windows, Linux, and macOS.

**Board specs:**
- MCU: ESP32-S3-WROOM-1-N16R8 (16MB Flash, 8MB PSRAM)
- Display: 7" 800x480 RGB565 capacitive touchscreen
- Interfaces: CAN bus, RS485, I2C, UART, analog sensor input, SD card, battery connector
- IO Expander: CH422G (controls backlight, touch reset, CAN/USB switching, SD CS)

**Manufacturer documentation:**
- Wiki: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7
- Schematic: https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-7/ESP32-S3-Touch-LCD-7-Sch.pdf
- Product page: https://www.waveshare.com/esp32-s3-touch-lcd-7.htm

---

## Table of Contents

1. [Board Layout and Connectors](#1-board-layout-and-connectors)
2. [Physical Switches and Buttons](#2-physical-switches-and-buttons)
3. [Install the CH343 USB-to-UART Driver](#3-install-the-ch343-usb-to-uart-driver)
4. [Install PlatformIO](#4-install-platformio)
5. [Connect the Board](#5-connect-the-board)
6. [Identify the Serial Port](#6-identify-the-serial-port)
7. [Configure platformio.ini](#7-configure-platformioini)
8. [Build and Upload Firmware](#8-build-and-upload-firmware)
9. [Serial Monitor](#9-serial-monitor)
10. [CAN Bus and GPIO19/20 Sharing](#10-can-bus-and-gpio1920-sharing)
11. [Troubleshooting](#11-troubleshooting)

---

## 1. Board Layout and Connectors

The board has two USB-C ports on the left side:

```
+--------------------------------------------------+
|  [UART2 header]                                  |
|   3V3 GND RXD TXD                               |
|                                                  |
|  [Type_C1] ---- "UART1" USB-C                   |
|     CH343 USB-to-UART bridge                     |
|     Used for: PROGRAMMING + SERIAL MONITOR       |
|                                                  |
|  [Type_C2] ---- "USB" USB-C                     |
|     Native USB (GPIO19/20)                       |
|     Used for: NOTHING during normal operation    |
|     (these pins are shared with CAN bus)         |
|                                                  |
|                              [RESET] [BOOT]      |
|                          [UART1/UART2 switch]    |
+--------------------------------------------------+
```

**IMPORTANT: Use the top USB-C port (Type_C1, labeled UART1) for all programming
and serial monitoring.** The bottom USB-C port (Type_C2) shares GPIO19/20 with the
CAN bus transceiver and is not used for development.

## 2. Physical Switches and Buttons

### UART1/UART2 Slide Switch (SW1)

Located at the bottom of the board between the "UART1" and "UART2" labels. This
switch routes the CH343 USB-to-UART chip to either:

| Position | Effect |
|----------|--------|
| **UART1** | CH343 connects to ESP32-S3 UART0 — **use this for programming** |
| **UART2** | CH343 connects to the external UART2 pin header (H3) — ESP32 cannot communicate |

**The switch MUST be set to UART1 for programming and serial output.**

If esptool reports "No serial data received" or "Failed to connect", check this
switch first.

### BOOT Button (K2)

Hold this button to force the ESP32-S3 into download mode. Normally not needed
because the CH343 auto-download circuit handles this automatically, but useful
as a fallback if auto-download fails.

### RESET Button (K1)

Resets the ESP32-S3. Press after uploading firmware if the board doesn't restart
automatically.

### CAN 120R / RS-485 120R Jumpers

Located on the right side of the board. These connect 120-ohm termination resistors
for the CAN and RS-485 buses. Leave connected (NC = Normally Connected) unless your
bus already has termination at both ends.

## 3. Install the CH343 USB-to-UART Driver

The board uses a WCH CH343P chip for USB-to-UART conversion. Driver requirements
vary by operating system.

### Windows

1. Download the driver from WCH: https://www.wch-ic.com/downloads/CH343SER_EXE.html
2. Extract the zip file
3. Run `SETUP.EXE` and click **Install**
4. Restart if prompted
5. After plugging in the board, open Device Manager — look for **"USB-SERIAL CH343 (COMx)"**

### Linux

Most modern Linux kernels (4.x+) include the `cdc_acm` driver, which works with
the CH343 out of the box. No installation needed.

The device will appear as `/dev/ttyACM0` (or `/dev/ttyACM1`, etc.).

**Permissions:** Your user must be in the `dialout` group to access serial ports:

```bash
sudo usermod -a -G dialout $USER
```

Log out and back in for the group change to take effect.

**Optional dedicated driver:** WCH provides a custom VCP driver at
https://github.com/WCHSoftGroup/ch343ser_linux that creates devices named
`/dev/ttyCH343USB0`. This is not required — the built-in `cdc_acm` driver
works fine.

**PlatformIO udev rules:** PlatformIO requires udev rules for USB device access.
Install them with:

```bash
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules | sudo tee /etc/udev/rules.d/99-platformio-udev.rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```

### macOS

1. Download the driver from WCH: https://www.wch-ic.com/downloads/CH34XSER_MAC_ZIP.html
2. Install the package:
   - macOS 10.9-10.15: Install the `.pkg` file directly
   - macOS 11.0+ (Big Sur and later): Open the `.dmg`, then open LaunchPad, find
     "CH34xVCPDriver", and click **Install**
3. You may need to allow the kernel extension in System Preferences > Security & Privacy
4. The device will appear as `/dev/tty.wchusbserial*` (e.g., `/dev/tty.wchusbserial14110`)

**Alternative (Homebrew):**

```bash
brew install --cask wch-ch34x-usb-serial-driver
```

## 4. Install PlatformIO

### Option A: VS Code Extension (Recommended)

1. Install VS Code from https://code.visualstudio.com/
2. Open VS Code, go to Extensions (Ctrl+Shift+X / Cmd+Shift+X)
3. Search for "PlatformIO IDE" and install it
4. PlatformIO Core CLI is bundled with the extension

### Option B: CLI Only

Requires Python 3.6+.

**Windows:**

```cmd
pip install platformio
```

**Linux:**

```bash
sudo apt-get install python3 python3-venv python3-pip   # Debian/Ubuntu
pip3 install platformio
```

**macOS:**

```bash
pip3 install platformio
```

Verify the installation:

```bash
pio --version
```

## 5. Connect the Board

1. Set the **UART1/UART2 switch (SW1) to UART1**
2. Plug a USB-C cable into the **top USB-C port** (Type_C1, labeled UART1)
3. Connect the other end to your computer
4. The power LED on the board should light up

## 6. Identify the Serial Port

### Windows

Open Device Manager and look under "Ports (COM & LPT)" for:
- **USB-SERIAL CH343 (COMx)** — note the COM port number (e.g., COM3)

### Linux

```bash
ls /dev/ttyACM* /dev/ttyUSB* /dev/ttyCH343* 2>/dev/null
```

Typical result: `/dev/ttyACM0`

To confirm it's the CH343:

```bash
udevadm info -a -n /dev/ttyACM0 | grep -E 'idVendor|idProduct|product'
```

Look for vendor `1a86` and product `55d3` — that's the CH343.

### macOS

```bash
ls /dev/tty.wchusbserial* /dev/tty.usbserial* 2>/dev/null
```

Or use PlatformIO to list devices:

```bash
pio device list
```

## 7. Configure platformio.ini

Set the `upload_port` and `monitor_port` to match your serial port.

### Linux Example

```ini
upload_port = /dev/ttyACM0
upload_protocol = esptool
monitor_port = /dev/ttyACM0
monitor_speed = 115200
```

### Windows Example

```ini
upload_port = COM3
upload_protocol = esptool
monitor_port = COM3
monitor_speed = 115200
```

### macOS Example

```ini
upload_port = /dev/tty.wchusbserial14110
upload_protocol = esptool
monitor_port = /dev/tty.wchusbserial14110
monitor_speed = 115200
```

### Required Build Flags

The following flags must be present for this board:

```ini
build_flags =
    -DBOARD_HAS_PSRAM
    -DARDUINO_USB_CDC_ON_BOOT=0
```

| Flag | Purpose |
|------|---------|
| `BOARD_HAS_PSRAM` | Enables the 8MB OPI PSRAM |
| `ARDUINO_USB_CDC_ON_BOOT=0` | Routes `Serial` to UART0 (CH343) instead of native USB. **Must be 0** because native USB pins are shared with CAN. |

### Memory Configuration

```ini
board_build.arduino.memory_type = qio_opi
board_upload.flash_size = 16MB
```

## 8. Build and Upload Firmware

### From VS Code

Click the PlatformIO upload arrow button in the bottom toolbar, or press
Ctrl+Alt+U (Cmd+Alt+U on macOS).

### From Command Line

```bash
pio run -t upload
```

The CH343 has an auto-download circuit, so the board should enter download mode
automatically. After upload completes, the board resets and runs the new firmware.

### If Auto-Download Fails

If esptool can't connect, manually enter download mode:

1. Hold the **BOOT** button
2. Press and release the **RESET** button (while still holding BOOT)
3. Release the **BOOT** button
4. Run the upload command within a few seconds

## 9. Serial Monitor

### From VS Code

Click the plug/serial monitor icon in the PlatformIO toolbar.

### From Command Line

```bash
pio device monitor
```

Serial output from `Serial.println()` goes through the CH343 UART — the same
port used for uploading. This works reliably because `ARDUINO_USB_CDC_ON_BOOT=0`
routes `Serial` to UART0.

## 10. CAN Bus and GPIO19/20 Sharing

This board shares GPIO19 and GPIO20 between two functions:

| Mode | GPIO19 | GPIO20 | Controlled By |
|------|--------|--------|---------------|
| USB mode | USB_DP | USB_DN | CH422G EXIO5 = LOW |
| CAN mode | CAN_RX | CAN_TX | CH422G EXIO5 = HIGH |

The CH422G IO expander's EXIO5 pin (labeled CAN_SEL or USB_SEL) switches between
these modes. In firmware:

```cpp
// During setup — start in USB mode (CAN_SEL LOW)
ch422g_out = CH422G_EXIO2_BIT;  // backlight on, CAN_SEL=0

// Later, when ready to use CAN — switch to CAN mode
ch422g_set_bit(CH422G_EXIO5_BIT, true);  // CAN_SEL=1
```

**This is why programming must go through the CH343 UART port (Type_C1), not the
native USB port (Type_C2).** Once CAN mode is enabled, GPIO19/20 are dedicated
to the CAN transceiver and native USB is unavailable.

Serial monitoring continues to work through the CH343 regardless of CAN mode,
because the CH343 is connected to a separate UART peripheral (UART0), not
GPIO19/20.

## 11. Troubleshooting

### "No serial data received" or "Failed to connect to ESP32-S3"

1. **Check the UART1/UART2 switch** — must be set to UART1
2. **Try manual boot mode** — hold BOOT, press RESET, release BOOT, then upload
3. **Check the USB cable** — some cables are charge-only with no data lines
4. **Verify the port** — make sure you're using the correct port name in platformio.ini
5. **Check permissions (Linux)** — ensure your user is in the `dialout` group

### Board appears as wrong device name

| Expected | Actual | Reason |
|----------|--------|--------|
| `/dev/ttyCH343USB0` | `/dev/ttyACM0` | Using built-in `cdc_acm` driver (this is fine) |
| `/dev/ttyACM0` | `/dev/ttyACM1` | Another device took ACM0 first — update platformio.ini |
| `COM3` | Different COM number | COM numbers are assigned dynamically — check Device Manager |

### Screen stays black after upload

- The CH422G must be initialized to enable the backlight (EXIO2 = HIGH)
- Verify PSRAM is enabled (`-DBOARD_HAS_PSRAM` in build_flags)
- Check that `board_build.arduino.memory_type = qio_opi` is set

### Serial monitor shows garbage characters

- Verify `monitor_speed` matches the baud rate in your firmware (`Serial.begin(115200)`)
- Confirm `ARDUINO_USB_CDC_ON_BOOT=0` — if set to 1, Serial output goes to native
  USB (which is disconnected when CAN is active)

### Upload works but CAN bus doesn't function

- Confirm the firmware sets CH422G EXIO5 HIGH before initializing TWAI
- Check that the CAN 120R jumper is connected if this is an end node on the bus
- CAN requires at least two nodes on the bus to acknowledge frames (unless using
  `TWAI_MODE_NO_ACK` for testing)

---

## Pin Reference

### CH422G IO Expander (I2C address 0x24, on GPIO8/GPIO9)

| Bit | Name | Function |
|-----|------|----------|
| EXIO1 | TP_RST | Touch controller reset |
| EXIO2 | LCD_BL | Backlight enable (digital on/off) |
| EXIO4 | SD_CS | SD card chip select |
| EXIO5 | CAN_SEL / USB_SEL | LOW = USB mode, HIGH = CAN mode |

### Display (RGB565 parallel interface)

| Signal | GPIO |
|--------|------|
| DE | 5 |
| VSYNC | 3 |
| HSYNC | 46 |
| PCLK | 7 |
| R[0:4] | 1, 2, 42, 41, 40 |
| G[0:5] | 39, 0, 45, 48, 47, 21 |
| B[0:4] | 14, 38, 18, 17, 10 |

### Touch (GT911, I2C)

| Signal | GPIO |
|--------|------|
| SDA | 8 |
| SCL | 9 |
| RST | CH422G EXIO1 |

### CAN Bus (TWAI)

| Signal | GPIO |
|--------|------|
| CAN_TX | 20 |
| CAN_RX | 19 |

Note: Active only when CH422G EXIO5 = HIGH.
