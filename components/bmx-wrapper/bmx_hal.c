#include "bmx_hal.h"

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "bmx_hal";

int8_t bmx_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    i2c_master_dev_handle_t i2c_dev = (i2c_master_dev_handle_t)intf_ptr;

    ESP_LOGD(TAG, "Read %d bytes from reg 0x%02x", len, reg_addr);
    esp_err_t err = i2c_master_transmit_receive(i2c_dev, &reg_addr, 1, reg_data, len, CONFIG_I2C_TIMEOUT_VALUE_MS);

    return (err == ESP_OK) ? 0 : -2;
}

int8_t bmx_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    i2c_master_dev_handle_t i2c_dev = (i2c_master_dev_handle_t)intf_ptr;

    uint8_t write_buf[len + 1];
    write_buf[0] = reg_addr;
    memcpy(&write_buf[1], reg_data, len);

    ESP_LOGD(TAG, "Write %d bytes to reg 0x%02x", len, reg_addr);
    esp_err_t err = i2c_master_transmit(i2c_dev, write_buf, len + 1, CONFIG_I2C_TIMEOUT_VALUE_MS);

    return (err == ESP_OK) ? 0 : -2;
}

void bmx_delay_us(uint32_t period_us, void *intf_ptr)
{
    ESP_LOGV(TAG, "Delay for %d uSeconds", period_us);
    // Keep sub-tick delays accurate and give the sensor time to settle.
    if (period_us <= 10000)
    {
        esp_rom_delay_us(period_us + 1000);
    }
    else
    {
        // Round up and add a tick to guarantee the minimum delay.
        vTaskDelay(pdMS_TO_TICKS(period_us / 1000) + 2);
    }
}
