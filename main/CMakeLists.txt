if(CONFIG_LOCAL_WEB_SERVER_ENABLED)
    set(web_server_sources "device_status.c" "web_server.c")
    set(web_server_html "index.html")
endif()

if(CONFIG_SCD40_TEMPERATURE_CALIBRATION_ENABLED)
    list(APPEND web_server_sources "temperature_calibration.c")
endif()

idf_component_register(SRCS
                              "main.c" 
                              "bmp.c"
                              "sensirion.c"
                              "wifi.c"
                              "watchdog.c"
                              "device_config.c"
                              ${web_server_sources}
                           
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
                       EMBED_TXTFILES ${web_server_html})
