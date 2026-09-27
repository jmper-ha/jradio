idf_component_register(SRCS "rda5807.c" "rds.c" "fm_presets.c" "fm_tuner.c"
                       INCLUDE_DIRS "include"
                       REQUIRES board esp_driver_i2c)
