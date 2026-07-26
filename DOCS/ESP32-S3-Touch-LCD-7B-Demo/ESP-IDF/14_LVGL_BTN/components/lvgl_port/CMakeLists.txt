
idf_component_register(SRCS "lvgl_port.c" 
                        INCLUDE_DIRS "."
                        REQUIRES driver esp_lcd i2c gpio io_extension rgb_lcd_port touch lvgl
                    )

                        