| Supported Targets | ESP32-S3 |
| ----------------- | -------- |

| Supported LCD Controller    | ST7262 |
| ----------------------------| -------|

| Supported TOUCH Controller    | GT911 |
| ----------------------------| -------|
## How to use the example

## ESP-IDF Required

### Hardware Required

* An Waveshare ESP32-S3-Touch-LCD-4.3 development board

### Hardware Connection

The connection between ESP Board and the LCD is as follows:

```
       ESP Board                           RGB  Panel
+-----------------------+              +-------------------+
|                   GND +--------------+GND                |
|                       |              |                   |
|                   3V3 +--------------+VCC                |
|                       |              |                   |
|                   PCLK+--------------+PCLK               |
|                       |              |                   |
|             DATA[15:0]+--------------+DATA[15:0]         |
|                       |              |                   |
|                  HSYNC+--------------+HSYNC              |
|                       |              |                   |
|                  VSYNC+--------------+VSYNC              |
|                       |              |                   |
|                     DE+--------------+DE                 |
|                       |              |                   |
|               BK_LIGHT+--------------+BLK                |
       ESP Board                             TOUCH  
+-----------------------+              +-------------------+
|                    GND+--------------+GND                |
|                       |              |                   |
|                    3V3+--------------+VCC                |
|                       |              |                   |
|                  GPIO8+--------------+SDA                |
|                       |              |                   |
|                  GPIO9+--------------+SCL                |
|                       |              |                   |
       ESP Board                                LED
+-----------------------+              +-------------------+
|                   GND +--------------+GND                |
|                       |              |                   |
|                   3V3 +--------------+VCC                |
|                       |              |                   |
|                    AD +--------------+LED                |
+-----------------------+              |                   |
|                       |              |                   |
       IO EXTENSION.EXIO1+--------------+TP_RST            |
|                       |              |                   |
       IO EXTENSION.EXIO2+--------------+DISP_EN           |          
                                       +-------------------+
```

* Demonstrates an LVGL slider to control LED brightness.

### Configure the Project

### Build and Flash

Run `idf.py set-target esp32s3` to select the target chip.

Run `idf.py -p PORT build flash monitor` to build, flash and monitor the project. A fancy animation will show up on the LCD as expected.

The first time you run `idf.py` for the example will cost extra time.

(To exit the serial monitor, type ``Ctrl-]``.)

See the [Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html) for full steps to configure and use ESP-IDF to build projects.

## Troubleshooting

For any technical queries, please open an https://service.waveshare.com/. We will get back to you soon.
