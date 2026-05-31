idf_component_register(SRCS "sewu_wifi.c"
                       INCLUDE_DIRS "include"
                       REQUIRES sewu_core esp_event esp_wifi nvs_flash)
