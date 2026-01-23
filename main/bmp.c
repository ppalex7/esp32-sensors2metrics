#include "bmp.h"

#include "esp_log.h"
#include "driver/i2c_master.h"

static const char *TAG = "bmp";

static i2c_master_dev_handle_t dev_handle;

void add_bmp_device_to_i2c_bus()
{
    i2c_master_bus_handle_t bus_handle;
    if (i2c_master_get_bus_handle(0, &bus_handle) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to obtain I2C master bus handle");
        abort();
    }

    i2c_device_config_t i2c_dev_conf = {
        .scl_speed_hz = 10000,
        .device_address = 0x47,
    };
    if (i2c_master_bus_add_device(bus_handle, &i2c_dev_conf, &dev_handle) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add BMP580 device to bus");
        abort();
    }
}

esp_err_t bmp_enable_pressure()
{
    ESP_LOGD(TAG, "set press_en in OSR_CONFIG");

    uint8_t command[] = {0x36, (1u << 6)};
    esp_err_t ret = i2c_master_transmit(dev_handle, command, sizeof(command), CONFIG_I2C_TIMEOUT_VALUE_MS);

    if (ret == ESP_OK)
    {
        ESP_LOGD(TAG, "OSR_CONFIG set");
    }
    else if (ret == ESP_ERR_TIMEOUT)
    {
        ESP_LOGW(TAG, "Bus is busy");
    }
    else
    {
        ESP_LOGW(TAG, "OSR_CONFIG set failed: %d", ret);
    }
    return ret;
}

esp_err_t bmp_normal_mode_1hz()
{
    ESP_LOGD(TAG, "set normal mode and ODR=1Hz in ODR_CONFIG");

    uint8_t command[] = {0x37, (0x1cu << 2) | 0b01};
    esp_err_t ret = i2c_master_transmit(dev_handle, command, sizeof(command), CONFIG_I2C_TIMEOUT_VALUE_MS);

    if (ret == ESP_OK)
    {
        ESP_LOGD(TAG, "ODR_CONFIG set");
    }
    else if (ret == ESP_ERR_TIMEOUT)
    {
        ESP_LOGW(TAG, "Bus is busy");
    }
    else
    {
        ESP_LOGW(TAG, "ODR_CONFIG set failed: %d", ret);
    }
    return ret;
}

void bmp_read_measurement(bmp_measurement *measurement)
{
    ESP_LOGD(TAG, "set single measurement mode in ODR_CONFIG");
    uint8_t command[] = {0x37, (0x1cu << 2) | 0b10};
    esp_err_t ret = i2c_master_transmit(dev_handle, command, sizeof(command), CONFIG_I2C_TIMEOUT_VALUE_MS);
    if (ret == ESP_OK)
    {
        ESP_LOGD(TAG, "ODR_CONFIG set");
    }
    else if (ret == ESP_ERR_TIMEOUT)
    {
        ESP_LOGW(TAG, "Bus is busy");
        return;
    }
    else
    {
        ESP_LOGW(TAG, "ODR_CONFIG set failed: %d", ret);
        return;
    }

    ESP_LOGD(TAG, "read measurements from BMP");
    uint8_t read_addr[] = {0x1D};
    uint8_t recv[6];
    ret = i2c_master_transmit_receive(dev_handle, read_addr, sizeof(read_addr), recv, sizeof(recv), CONFIG_I2C_TIMEOUT_VALUE_MS);
    if (ret == ESP_OK)
    {
        ESP_LOGD(TAG, "TEMP_DATA and PRESS_DATA read");
        measurement->pressure = ((recv[5] << 16) | (recv[4] << 8) | recv[3]) >> 6;
        measurement->t = ((double)((recv[2] << 16) | (recv[1] << 8) | recv[0])) / (1u << 16);
        ESP_LOGI(TAG, "temperature=%.1f, pressure=%06d", measurement->t, measurement->pressure);
    }
    else if (ret == ESP_ERR_TIMEOUT)
    {
        ESP_LOGW(TAG, "Bus is busy");
    }
    else
    {
        ESP_LOGW(TAG, "read measurements failed: %d", ret);
    }
}
