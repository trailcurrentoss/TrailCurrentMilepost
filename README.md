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

**Screen doesn't wake after being asleep for a long time (historical bug — fixed in July 2026).**

*What used to happen:* the screen would time out and go dark as designed. But after roughly an hour of being dark — especially when the device was connected to a busy CAN bus — tapping the screen would do nothing. Sometimes the screen came back but with flicker or shifted image; sometimes it stayed dark until the panel was physically unplugged and plugged back in. Once wedged, even a software reset didn't recover it.

*Why it happened:* the 7" display has a very demanding video pipeline. Every fraction of a second, a small "helper" (an interrupt handler) has to refill the display's data buffer or the picture breaks. That helper lives on one of the ESP32's two CPUs (CPU 0). CAN bus traffic ALSO produces interrupts, and by default the CAN interrupt handler ended up on the same CPU 0. On a busy bus, thousands of CAN interrupts per second were arriving right in the middle of the display's tight refill deadline, causing the display to underrun. When that happened, the software waiting for the "frame done" signal from the display never got it, and the entire UI thread stopped moving. The touch chip was still healthy — you could see it responding to I2C probes — but nothing was actually processing your taps because the UI thread was frozen.

*What fixes it:*

1. **Moving critical display code into faster memory.** Under normal ESP-IDF configuration, when the chip writes to flash storage (which happens whenever you change a setting — brightness, timezone, theme, etc.) it briefly turns off its instruction cache. If a display interrupt fires during that window, it can't run because the code isn't cached. The RGB display, GDMA (memory transfer), and CAN interrupt handlers are now marked "IRAM-safe" — they live in on-chip fast memory and keep running even when the cache is off.
2. **Letting the display self-heal.** If the display ever does underrun, a new setting tells the hardware to automatically restart cleanly at the next frame boundary instead of latching a corrupted state that only a full board reset can clear.
3. **Moving the CAN interrupt off the display's CPU.** The CAN driver's interrupt handler is now installed from the CAN receive task, which is pinned to CPU 1. So the display's CPU 0 no longer has to service CAN interrupts at all.
4. **Filtering CAN traffic in hardware.** Milepost only cares about a small range of message IDs (0x00 to 0x3E). The hardware acceptance filter now rejects everything outside that range before the interrupt fires at all, collapsing per-second interrupt load under bus flood.
5. **A safety net.** The UI thread no longer waits forever for the "frame done" signal — it waits at most 100 ms, logs an error if the signal never arrives, and continues anyway. So even if a future regression re-introduces the starvation, the UI won't freeze and the fault becomes visible in the logs.
6. **Diagnostics kept permanently.** The firmware now prints why the chip last restarted at every boot (`[wakediag] ======== BOOT reset_reason=N (NAME) ========`), tracks a running vsync counter, and counts flush-wait timeouts. If this bug ever returns, the log will name the cause on the next boot.

*How to tell if the fix is working:* while the screen is dim, the periodic `[wakediag] sleep_tick ...` log line should show the `vsync=` counter incrementing by roughly 168–169 every 5 seconds (the panel's design refresh rate is 33.6 Hz) and `flush_timeouts=0`. If you ever see `flush_timeouts` climb above zero or `vsync` stop advancing between consecutive sleep_ticks, the panel pipeline has wedged and something has regressed.

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
