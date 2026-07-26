| Supported Targets | ESP32-S3 |
| ----------------- | -------- |

## How to use the example

## ESP-IDF Required

### Hardware Required

* An Waveshare ESP32-S3-Touch-LCD-4.3 development board

### Hardware Connection

The connection between ESP Board and the LED is as follows:

```
       ESP Board                               CH343
+-----------------------+              +-------------------+
|                   GND +--------------+GND                |
|                       |              |                   |
|                   3V3 +--------------+VCC                |
|                       |              |                   |
|                    TX +--------------+RX                 |
|                       |              |                   |
|                    RX +--------------+TX                 |
+-----------------------+               +-------------------+
```

* This example implements USB to serial communication

### Configure the Project

### Build and Flash

Run `idf.py set-target esp32s3` to select the target chip.

Run `idf.py -p PORT build flash` to build, flash.

The first time you run `idf.py` for the example will cost extra time.



See the [Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html) for full steps to configure and use ESP-IDF to build projects.

## Troubleshooting

For any technical queries, please open an https://service.waveshare.com/. We will get back to you soon.
