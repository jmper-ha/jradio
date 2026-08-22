idf_component_register(SRCS "usb_storage.c" INCLUDE_DIRS "include"
                       REQUIRES file_storage usb fatfs usb_host_msc)
