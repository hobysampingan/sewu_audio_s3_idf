idf_component_register(SRCS "sewu_input.c"
                       INCLUDE_DIRS "include"
                       REQUIRES sewu_core sewu_settings sewu_usb esp_driver_gpio)
