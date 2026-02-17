#include <stdint.h>

#include "esp_log.h"
#include "driver/i2c_master.h"

#include "scd4x_i2c.h"
#include "sensirion_common.h"

#include "sensirion.h"

static const char *TAG = "sensirion";

i2c_master_dev_handle_t scd4x_dev_handle;

void add_sensirion_device_to_i2c_bus()
{
    i2c_master_bus_handle_t bus_handle;
    if (i2c_master_get_bus_handle(0, &bus_handle) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to obtain I2C master bus handle");
        abort();
    }

    i2c_device_config_t i2c_dev_conf = {
        .scl_speed_hz = 10000,
        .device_address = SCD40_I2C_ADDR_62,
    };
    if (i2c_master_bus_add_device(bus_handle, &i2c_dev_conf, &scd4x_dev_handle) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add sensirion device to bus");
        abort();
    }
}

void sensirion_read_measurement(sensirion_measurement *measurement)
{
    uint16_t raw_co2;
    uint16_t raw_temp;
    uint16_t raw_rh;

    int16_t ret = scd4x_read_measurement_raw(&raw_co2, &raw_temp, &raw_rh);
    if (ret == 0)
    {
        int32_t temp = -45 + 175 * raw_temp / ((1 << 16) - 1);
        measurement->co2 = raw_co2;
        measurement->t = (int16_t)temp;
        measurement->rh = 100 * raw_rh / ((1 << 16) - 1);
        ESP_LOGI(TAG, "read_measurement values: CO2=%04d ppm, T=%02d, RH=%02d", measurement->co2, temp, measurement->rh);
    }
    else
    {
        ESP_LOGW(TAG, "read_measurement failed: %d", ret);
    }
}
