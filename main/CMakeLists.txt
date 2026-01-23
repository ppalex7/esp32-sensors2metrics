idf_component_register(SRCS
                              "main.c" 
                              "bmp.c"
                              "sensirion.c"
                              "wifi.c"
                           
                       PRIV_REQUIRES
                              cjson
                              esp_driver_gpio
                              esp_driver_i2c
                              esp_http_client
                              esp_netif
                              esp_wifi
                              nvs_flash
                       INCLUDE_DIRS ".")
