idf_component_register(
    SRCS "src/milepost_config.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash log
)
