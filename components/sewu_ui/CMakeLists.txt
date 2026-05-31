 idf_component_register(SRCS "sewu_ui.c"
                       INCLUDE_DIRS "include"
                       REQUIRES sewu_core sewu_usb esp_driver_ledc esp_lcd sewu_gfx)
