# TrailCurrent Milepost

Touchscreen display firmware for a Sunton ESP32-8048S070 7-inch 800x480 display providing a wall-mounted control panel for the [TrailCurrent](https://trailcurrent.com) modular system.

## Hardware Overview

- **Microcontroller:** ESP32-S3 (16MB Flash, 8MB PSRAM)
- **Display:** Sunton ESP32-8048S070 - 7" 800x480 RGB TFT with GT911 capacitive touch
- **Key Features:**
  - Wall-mounted touchscreen control panel
  - Thermostat with interior/exterior temperature display
  - Device control buttons for PDM-connected devices
  - WiFi network scanning and connection UI
  - Screen brightness control with PWM backlight
  - Screen timeout with auto-wake on touch
  - Theme switching (light/dark)
  - NVM-persisted user settings
  - LVGL-based UI designed with EEZ Studio

## Hardware Requirements

### Components

- **Display Module:** Sunton ESP32-8048S070 (7" 800x480 RGB TFT)
- **Touch Controller:** GT911 (I2C, SDA: GPIO 19, SCL: GPIO 20, RST: GPIO 38)
- **Backlight:** PWM on GPIO 2

## Firmware

See `src/` directory for PlatformIO-based firmware.

**Setup:**
```bash
# Install PlatformIO (if not already installed)
pip install platformio

# Build firmware
pio run

# Upload to board (serial)
pio run -t upload
```

### Firmware Dependencies

All dependencies are automatically resolved by PlatformIO during the build process:

- **[LVGL](https://github.com/lvgl/lvgl)** (v8.4.0) - Light and Versatile Graphics Library
- **[GFX Library for Arduino](https://github.com/moononournation/Arduino_GFX)** (v1.4.0+) - Display driver

### Display Rendering

The firmware uses LVGL direct mode with zero-copy rendering into the RGB panel's DMA framebuffer for optimal performance. Bounce buffers ensure DMA reads from SRAM instead of PSRAM.

## Project Structure

```
├── GUI/                          # EEZ Studio UI design files
├── include/                      # Header files
│   ├── lv_conf.h                 # LVGL configuration
│   └── bsp/                      # Board support package headers
├── src/                          # Firmware source
│   ├── main.cpp                  # Display, touch, and LVGL initialization
│   ├── actions.cpp               # UI action callbacks
│   ├── vars.cpp                  # UI variable bindings
│   └── ui/                       # EEZ Studio generated UI code
├── lib/                          # Local libraries
├── platformio.ini                # Build configuration
└── partitions.csv                # ESP32 flash partition layout (16MB)
```

## License

MIT License - See LICENSE file for details.

## Contributing

Improvements and contributions are welcome! Please submit issues or pull requests.
