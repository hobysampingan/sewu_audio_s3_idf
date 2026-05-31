idf_component_register(SRCS "sewu_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES sewu_core sewu_audio sewu_usb sewu_dsp sewu_settings sewu_input sewu_ui sewu_wifi nvs_flash)
