set(VENDOR_DIR "vendor/embedded-i2c-scd4x")

idf_component_register(
    SRCS 
        "${VENDOR_DIR}/scd4x_i2c.c"
        "${VENDOR_DIR}/sensirion_common.c"
        "${VENDOR_DIR}/sensirion_i2c.c"
        "sensirion_i2c_hal.c"
    INCLUDE_DIRS 
        "."
        "${VENDOR_DIR}"
    REQUIRES 
        esp_driver_i2c
)
