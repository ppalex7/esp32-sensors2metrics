idf_component_register(
    SRCS 
        "bmx_hal.c"
        "vendor/BME280_SensorAPI/bme280.c"
    INCLUDE_DIRS 
        "."
        "vendor/BME280_SensorAPI"
    REQUIRES 
        esp_driver_i2c
)

