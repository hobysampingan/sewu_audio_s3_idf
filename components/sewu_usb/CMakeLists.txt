idf_component_register(SRCS "sewu_usb_audio.c"
                       INCLUDE_DIRS "include"
                       REQUIRES sewu_core esp_ringbuf espressif__usb_device_uac)
