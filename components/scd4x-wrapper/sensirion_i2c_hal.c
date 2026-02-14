#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c_master.h"
#include "esp_log.h"

#include "sensirion_i2c_hal.h"

static const char *TAG = "sensirion_i2c_hal";

extern i2c_master_dev_handle_t scd4x_dev_handle;

int8_t sensirion_i2c_hal_read(uint8_t address, uint8_t *data, uint8_t count)
{
    esp_err_t ret = i2c_master_receive(scd4x_dev_handle, data, count, CONFIG_I2C_TIMEOUT_VALUE_MS);
    return ret;
}

int8_t sensirion_i2c_hal_write(uint8_t address, const uint8_t *data, uint8_t count)
{
    uint16_t log_command = (data[0] << 8) | (data[1]);
    uint32_t log_arg = 0;
    for (size_t i = 2; i < count; i++)
    {
        log_arg = (log_arg << 8) | data[i];
    }
    ESP_LOGD(TAG, "Send command 0x%02x with arg 0x%03x", log_command, log_arg);

    esp_err_t ret = i2c_master_transmit(scd4x_dev_handle, data, count, CONFIG_I2C_TIMEOUT_VALUE_MS);
    return ret;
}

void sensirion_i2c_hal_sleep_usec(uint32_t useconds)
{
    ESP_LOGV(TAG, "Delay for %d uSeconds", useconds);
    vTaskDelay(((useconds / 1000) + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS);
}
