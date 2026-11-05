idf_component_register(
    SRCS "main.c"
         "gnarl.c"
         "ble.c"
         "adc.c"
         "oled.c"
         "ota.c"
    INCLUDE_DIRS "../include"
                 "."
    REQUIRES radio medtronic esp_timer nvs_flash bt driver esp_adc led_strip esp_driver_uart
             esp_wifi esp_event esp_netif esp_http_server app_update
)
