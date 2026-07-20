# Waveshare ESP32-S3-Touch-LCD-7 — Board Setup Guide

This guide covers setting up the Waveshare ESP32-S3-Touch-LCD-7 (V1.2) development
board for firmware development with ESP-IDF on Windows, Linux, and macOS.

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
4. [Install ESP-IDF](#4-install-esp-idf)
5. [Connect the Board](#5-connect-the-board)
6. [Identify the Serial Port](#6-identify-the-serial-port)
7. [Build and Flash Firmware](#7-build-and-flash-firmware)
8. [Serial Monitor](#8-serial-monitor)
9. [OTA Firmware Updates](#9-ota-firmware-updates)
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

## 4. Install ESP-IDF

This project requires ESP-IDF v5.1 or later (tested with v5.5.2).

### Option A: VS Code Extension (Recommended)

1. Install VS Code from https://code.visualstudio.com/
2. Open VS Code, go to Extensions (Ctrl+Shift+X / Cmd+Shift+X)
3. Search for "ESP-IDF" and install the **Espressif IDF** extension
4. Follow the extension's setup wizard to install ESP-IDF and the toolchain

### Option B: Command Line

Follow Espressif's official installation guide:
https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/

**Linux quick setup:**

```bash
mkdir -p ~/esp
cd ~/esp
git clone -b v5.5.2 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
```

Before building, always source the environment:

```bash
source ~/esp/esp-idf/export.sh
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
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
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

## 7. Build and Flash Firmware

### First-Time Setup

```bash
# Set the target chip (only needed once per clean checkout)
idf.py set-target esp32s3
```

### Build

```bash
idf.py build
```

Dependencies (LVGL 8.4, GT911 touch driver) are downloaded automatically from
the ESP Component Registry on first build.

### Flash

```bash
# Linux
idf.py -p /dev/ttyACM0 flash

# Windows
idf.py -p COM3 flash

# macOS
idf.py -p /dev/tty.wchusbserial14110 flash
```

The CH343 has an auto-download circuit, so the board should enter download mode
automatically. After upload completes, the board resets and runs the new firmware.

### If Auto-Download Fails

If esptool can't connect, manually enter download mode:

1. Hold the **BOOT** button
2. Press and release the **RESET** button (while still holding BOOT)
3. Release the **BOOT** button
4. Run the flash command within a few seconds

## 8. Serial Monitor

```bash
# Linux
idf.py -p /dev/ttyACM0 monitor

# Windows
idf.py -p COM3 monitor

# macOS
idf.py -p /dev/tty.wchusbserial14110 monitor

# Build, flash, and monitor in one command
idf.py -p /dev/ttyACM0 flash monitor
```

Exit the monitor with `Ctrl+]`.

Serial output from `ESP_LOG*` macros goes through the CH343 UART (UART0). This
works regardless of CAN mode because the CH343 is on a separate UART peripheral,
not GPIO19/20.

## 9. OTA Firmware Updates

The firmware supports over-the-air updates triggered via the CAN bus. This allows
updating devices in the field without physical access to the USB port.

### Prerequisites

WiFi credentials must be stored on the device first. This is done by sending a
multi-frame CAN message (ID `0x01`) containing the SSID and password. The
credentials are saved to NVS and persist across reboots.

### OTA Process

1. Send CAN ID `0x00` with the target device's last 3 MAC bytes in data[0:2]
2. The device connects to WiFi and displays its IP address on screen
3. Upload firmware within 180 seconds:

```bash
curl -X POST --data-binary @build/trailcurrent_milepost.bin http://<device-ip>:3232/update
```

4. The device flashes the new firmware and reboots automatically

### OTA Rollback Protection

If the new firmware crashes before completing initialization, the bootloader
automatically reverts to the previous working firmware on the next boot.

### OTA Timeout

If no upload is received within 180 seconds, the device disconnects WiFi and
resumes normal operation.

## 10. CAN Bus and GPIO19/20 Sharing

This board shares GPIO19 and GPIO20 between two functions:

| Mode | GPIO19 | GPIO20 | Controlled By |
|------|--------|--------|---------------|
| USB mode | USB_DP | USB_DN | CH422G EXIO5 = LOW |
| CAN mode | CAN_RX | CAN_TX | CH422G EXIO5 = HIGH |

The CH422G IO expander's EXIO5 pin switches between these modes. The firmware
starts in USB mode for serial logging during boot, then switches to CAN mode
before initializing the TWAI driver.

**This is why programming must go through the CH343 UART port (Type_C1), not the
native USB port (Type_C2).** Once CAN mode is enabled, GPIO19/20 are dedicated
to the CAN transceiver and native USB is unavailable.

Serial monitoring continues to work through the CH343 regardless of CAN mode,
because the CH343 is connected to a separate UART peripheral (UART0), not
GPIO19/20.

## 11. Troubleshooting

### "No serial data received" or "Failed to connect to ESP32-S3"

1. **Check the UART1/UART2 switch** — must be set to UART1
2. **Try manual boot mode** — hold BOOT, press RESET, release BOOT, then flash
3. **Check the USB cable** — some cables are charge-only with no data lines
4. **Verify the port** — make sure you're using the correct port name
5. **Check permissions (Linux)** — ensure your user is in the `dialout` group

### Board appears as wrong device name

| Expected | Actual | Reason |
|----------|--------|--------|
| `/dev/ttyCH343USB0` | `/dev/ttyACM0` | Using built-in `cdc_acm` driver (this is fine) |
| `/dev/ttyACM0` | `/dev/ttyACM1` | Another device took ACM0 first |
| `COM3` | Different COM number | COM numbers are assigned dynamically — check Device Manager |

### Screen stays black after upload

- The CH422G must be initialized to enable the backlight (system param `0x01` to
  address `0x24`, then EXIO2 bit set in output register at address `0x38`)
- Verify the sdkconfig has PSRAM enabled (`CONFIG_SPIRAM=y`)
- Check that `CONFIG_SPIRAM_MODE_OCT=y` is set for the 8MB OPI PSRAM

### Serial monitor shows garbage characters

- Default baud rate is 115200. ESP-IDF's `idf.py monitor` auto-detects this.
- If using a third-party terminal, set baud rate to 115200.

### Upload works but CAN bus doesn't function

- Confirm the firmware sets CH422G EXIO5 HIGH before initializing TWAI
- Check that the CAN 120R jumper is connected if this is an end node on the bus
- CAN requires at least two nodes on the bus to acknowledge frames (unless using
  `TWAI_MODE_NO_ACK` for testing)

### OTA update fails

- Ensure WiFi credentials are stored (CAN ID `0x01` provisioning)
- Check that the device and your computer are on the same network
- The HTTP server listens on port 3232 — ensure no firewall is blocking it
- Check the serial monitor for connection/upload error messages

### Screen doesn't wake after long sleep, or wakes with flicker

This was a known bug on the 7B board fixed in July 2026. See the "Screen doesn't wake after being asleep for a long time" entry in the [Milepost README Troubleshooting section](../README.md#troubleshooting) for the plain-language explanation. In short: the display's tight refill timing was being starved by CAN interrupts on the same CPU, causing the UI thread to freeze on a "frame done" signal that never arrived. The fix moves interrupts to fast memory and to a different CPU, lets the display self-heal, and adds a 100 ms safety timeout on the UI wait. If the bug appears to return after an ESP-IDF upgrade, check that `sdkconfig` still has `CONFIG_LCD_RGB_ISR_IRAM_SAFE=y`, `CONFIG_LCD_RGB_RESTART_IN_VSYNC=y`, `CONFIG_GDMA_ISR_IRAM_SAFE=y`, and `CONFIG_TWAI_ISR_IN_IRAM=y`.

---

## Pin Reference

### CH422G IO Expander (I2C on GPIO8/GPIO9)

The CH422G uses separate I2C addresses per function (no register-based addressing):

| Address | Function |
|---------|----------|
| 0x24 | System parameter (write `0x01` to enable push-pull EXIO outputs) |
| 0x38 | Output pin states |

| Bit | Name | Function |
|-----|------|----------|
| EXIO1 (bit 1) | TP_RST | Touch controller reset |
| EXIO2 (bit 2) | LCD_BL | Backlight enable (digital on/off) |
| EXIO4 (bit 4) | SD_CS | SD card chip select |
| EXIO5 (bit 5) | CAN_SEL / USB_SEL | LOW = USB mode, HIGH = CAN mode |

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
| IRQ | 4 |
| RST | CH422G EXIO1 |

GT911 I2C address: 0x5D (default when INT pin is floating)

### CAN Bus (TWAI)

| Signal | GPIO |
|--------|------|
| CAN_TX | 20 |
| CAN_RX | 19 |

Active only when CH422G EXIO5 = HIGH. Baud rate: 500 kbps.
