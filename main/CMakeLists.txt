idf_component_register(SRCS
                              "main.c" 
                              "bmp.c"
                              "sensirion.c"
                              "wifi.c"
                              "watchdog.c"
                              "device_config.c"
                              "device_status.c"
                              "web_server.c"
                           
                       PRIV_REQUIRES
                              cjson
                              esp_driver_gpio
                              esp_driver_i2c
                              esp_http_client
                              esp_http_server
                              esp_netif
                              esp_timer
                              esp_wifi
                              nvs_flash

                              bmx-wrapper
                              scd4x-wrapper
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES "index.html")
