idf_component_register(
    SRCS 
        "bmx_hal.c"
        "vendor/BME280_SensorAPI/bme280.c"
        "vendor/BMP5_SensorAPI/bmp5.c"
    INCLUDE_DIRS 
        "."
        "vendor/BME280_SensorAPI"
        "vendor/BMP5_SensorAPI"
    REQUIRES 
        esp_driver_i2c
)

