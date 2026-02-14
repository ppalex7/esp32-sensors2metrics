#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c_master.h"

#include "sensirion_i2c_hal.h"

extern i2c_master_dev_handle_t scd4x_dev_handle;

int8_t sensirion_i2c_hal_read(uint8_t address, uint8_t *data, uint8_t count)
{
    esp_err_t ret = i2c_master_receive(scd4x_dev_handle, data, count, CONFIG_I2C_TIMEOUT_VALUE_MS);
    return ret;
}

int8_t sensirion_i2c_hal_write(uint8_t address, const uint8_t *data, uint8_t count)
{
    esp_err_t ret = i2c_master_transmit(scd4x_dev_handle, data, count, CONFIG_I2C_TIMEOUT_VALUE_MS);
    return ret;
}

void sensirion_i2c_hal_sleep_usec(uint32_t useconds)
{
    vTaskDelay(((useconds / 1000) + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS);
}
