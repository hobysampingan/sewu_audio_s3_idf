idf_component_register(SRCS "sewu_audio_engine.c"
                       INCLUDE_DIRS "include"
                       REQUIRES sewu_core sewu_usb sewu_dsp sewu_wifi esp_driver_i2s esp_driver_gpio)
