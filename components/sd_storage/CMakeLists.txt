idf_component_register(SRCS "sd_storage.c" INCLUDE_DIRS "include"
                       REQUIRES board fatfs sdmmc esp_driver_sdspi esp_driver_spi esp_driver_gpio)
