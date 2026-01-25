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
                              esp_timer
                              esp_wifi
                              nvs_flash
                       INCLUDE_DIRS ".")

if(NOT CONFIG_MONITORING_LABEL_LOCATION)
     add_definitions(-DCONFIG_MONITORING_LABEL_LOCATION_IS_EMPTY=1)
     message(WARNING "Переменная CONFIG_MONITORING_LABEL_LOCATION не задана")    
endif()
