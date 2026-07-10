# TrailCurrent Milepost

Touchscreen display firmware for the Waveshare ESP32-S3-Touch-LCD-7B providing a wall-mounted control panel for the [TrailCurrent](https://trailcurrent.com) modular system.

## Hardware

- **Board:** Waveshare ESP32-S3-Touch-LCD-7B
- **MCU:** ESP32-S3-WROOM-1-N16R8 (16MB Flash, 8MB PSRAM)
- **Display:** 7" 1024x600 RGB565 IPS with GT911 capacitive touch
- **IO Expander:** CH32V003 MCU on I2C (backlight PWM/enable, touch reset, LCD reset, CAN/USB mux, SD CS)
- **Interfaces:** CAN bus, RS485, I2C, UART, SD card, battery connector

## Features

- Device control buttons for up to 8 PDM-connected devices
- Interior temperature, humidity, and thermostat display
- GPS satellite count, GNSS mode, and elevation
- Battery SOC, voltage, wattage, and time-to-go
- Solar MPPT charge status and wattage
- Software brightness dimming via LVGL overlay
- Screen timeout with auto-wake on touch
- Light/dark theme switching
- NVS-persisted user settings
- OTA firmware updates triggered over CAN bus
- OTA rollback protection (auto-reverts bad firmware)
- LVGL-based UI designed with EEZ Studio

## Building and Flashing

Requires ESP-IDF v5.1+ (tested with v5.5.2).

```bash
# First time: set target
idf.py set-target esp32s3

# Build
idf.py build

# Flash via CH343 UART port (top USB-C, labeled UART1)
idf.py -p /dev/ttyACM0 flash

# Monitor serial output
idf.py -p /dev/ttyACM0 monitor
```

Dependencies (LVGL 8.4, GT911 touch driver) are resolved automatically from the ESP Component Registry on first build.

## Troubleshooting

**Touch stops responding in only part of the screen.** Check the touch panel ribbon cable and the GT911 boot log before suspecting firmware. On boot the firmware logs `GT911 PID=...` and `GT911 cfg_ver=... x_max=... y_max=... panel=1024x600` — an X/Y max that disagrees with the panel size, or a failed register read, points at a config or wiring problem. Dead regions with **no** logged touch events (not even the throttled `GT911 coord out of range` warning from the runtime clamp) indicate a hardware/cable fault, since a config problem produces out-of-range coordinates that get clamped and logged, not silence.

## OTA Updates

OTA is triggered via CAN bus message ID `0x00` containing the target device's MAC bytes [3,4,5]. The device connects to WiFi (credentials stored in NVS via CAN ID `0x01`), starts an HTTP server on port 3232, and waits up to 180 seconds for a firmware upload.

```bash
curl -X POST --data-binary @build/trailcurrent_milepost.bin http://<device-ip>:3232/update
```

## Project Structure

```
TrailCurrentMilepost/
├── CMakeLists.txt              # ESP-IDF root project file
├── sdkconfig.defaults          # Build configuration defaults
├── partitions.csv              # Flash partition layout (dual OTA slots)
├── main/
│   ├── CMakeLists.txt          # Component source and dependency list
│   ├── idf_component.yml       # ESP Component Registry dependencies
│   ├── main.c                  # Hardware init, display, CAN RX, UI loop
│   ├── actions.c               # UI action callbacks (button presses, etc.)
│   ├── vars.c                  # UI variable getters/setters
│   ├── ota.c                   # OTA update module (WiFi, HTTP server, flash)
│   ├── include/
│   │   ├── ota.h               # OTA public API
│   │   └── bsp/                # Shim headers for EEZ Studio compatibility
│   └── ui/                     # EEZ Studio generated UI code (do not edit)
├── GUI/
│   ├── TrailCurrentMilepost.eez-project       # UI design source file
│   └── TrailCurrentMilepost.eez-project-ui-state
└── DOCS/
    └── board-setup-guide.md    # Hardware setup and pin reference
```

## CAN Bus Protocol

| CAN ID | Direction | Description |
|--------|-----------|-------------|
| 0x00   | RX        | OTA trigger (bytes 0-2: target MAC[3:5]) |
| 0x01   | RX        | WiFi credential provisioning (multi-frame) |
| 0x07   | RX        | GPS satellite count and GNSS mode |
| 0x08   | RX        | GPS altitude |
| 0x18   | TX/RX     | Device toggle commands / status |
| 0x1B   | RX        | Device PWM status (8 channels) |
| 0x1F   | RX        | Temperature and humidity |
| 0x23   | RX        | Battery voltage and SOC |
| 0x24   | RX        | Battery wattage and time-to-go |
| 0x2C   | RX        | Solar MPPT wattage and charge status |

## License

MIT License - See LICENSE file for details.
