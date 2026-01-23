#include <stdint.h>
#include "sensirion.h"

#include "esp_log.h"
#include "driver/i2c_master.h"

static const char *TAG = "sensirion";

#define CRC8_POLYNOMIAL 0x31
#define CRC8_INIT 0xff

static i2c_master_dev_handle_t dev_handle;

uint8_t sensirion_common_generate_crc(const uint8_t *data, uint16_t count)
{
    uint16_t current_byte;
    uint8_t crc = CRC8_INIT;
    uint8_t crc_bit;
    /* calculates 8-Bit checksum with given polynomial */
    for (current_byte = 0; current_byte < count; ++current_byte)
    {
        crc ^= (data[current_byte]);
        for (crc_bit = 8; crc_bit > 0; --crc_bit)
        {
            if (crc & 0x80)
                crc = (crc << 1) ^ CRC8_POLYNOMIAL;
            else
                crc = (crc << 1);
        }
    }
    return crc;
}

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
        .device_address = 0x62,
    };
    if (i2c_master_bus_add_device(bus_handle, &i2c_dev_conf, &dev_handle) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add sensirion device to bus");
        abort();
    }
}

void sensirion_read_measurement(sensirion_measurement *measurement)
{
    ESP_LOGD(TAG, "Call read_measurement");

    uint8_t command[] = {0xec, 0x05};
    uint8_t recv[9];
    esp_err_t ret = i2c_master_transmit_receive(dev_handle, command, sizeof(command), recv, sizeof(recv), CONFIG_I2C_TIMEOUT_VALUE_MS);

    if (ret == ESP_OK)
    {
        ESP_LOGD(TAG, "read_measurement OK");
        int32_t temp = -45 + 175 * ((recv[3] << 8) | recv[4]) / ((1 << 16) - 1);
        measurement->co2 = (recv[0] << 8) | recv[1];
        measurement->t = (int16_t)temp;
        measurement->rh = 100 * ((recv[6] << 8) | recv[7]) / ((1 << 16) - 1);
        ESP_LOGI(TAG, "read_measurement values: CO2=%04d ppm, T=%02d, RH=%02d", measurement->co2, temp, measurement->rh);
    }
    else if (ret == ESP_ERR_TIMEOUT)
    {
        ESP_LOGW(TAG, "Bus is busy");
    }
    else
    {
        ESP_LOGW(TAG, "read_measurement failed: %d", ret);
    }
}

void sensirion_set_ambient_pressure(uint32_t pressure) {
    ESP_LOGD(TAG, "Call set_ambient_pressure %d", pressure);

    pressure = pressure / 100;
    uint8_t command[] = {0xe0, 0x00, (uint8_t)(pressure >> 8), (uint8_t) (pressure & 0x00FF), /* placeholder for crc */ 0x00};
    command[4] = sensirion_common_generate_crc(command + 2, 2);
    ESP_LOGD(TAG, "pressure=0x%0x%0x, CRC=0x%0x", command[2], command[3], command[4]);
    esp_err_t ret = i2c_master_transmit(dev_handle, command, sizeof(command), CONFIG_I2C_TIMEOUT_VALUE_MS);

    if (ret == ESP_OK)
    {
        ESP_LOGD(TAG, "set_ambient_pressure OK");
    }
    else if (ret == ESP_ERR_TIMEOUT)
    {
        ESP_LOGW(TAG, "Bus is busy");
    }
    else
    {
        ESP_LOGW(TAG, "set_ambient_pressure failed: %d", ret);
    }
}

uint32_t sensirion_get_ambient_pressure() {
    ESP_LOGD(TAG, "Call get_ambient_pressure");

    uint8_t command[] = {0xe0, 0x00};
    uint8_t recv[3];
    esp_err_t ret = i2c_master_transmit_receive(dev_handle, command, sizeof(command), recv, sizeof(recv), CONFIG_I2C_TIMEOUT_VALUE_MS);

    if (ret == ESP_OK)
    {
        ESP_LOGD(TAG, "get_ambient_pressure OK");
        ESP_LOGI(TAG, "pressure=0x%0x%0x, CRC=0x%0x", recv[0], recv[1], recv[2]);
    }
    else if (ret == ESP_ERR_TIMEOUT)
    {
        ESP_LOGW(TAG, "Bus is busy");
    }
    else
    {
        ESP_LOGW(TAG, "set_ambient_pressure failed: %d", ret);
    }
    return (((uint32_t) recv[0]) << 8 | recv[1]) * 100;
}

uint16_t sensirion_get_data_ready_status()
{
    ESP_LOGD(TAG, "Call get_data_ready_status");

    uint8_t command[] = {0xe4, 0xb8};
    uint8_t recv[3];
    esp_err_t ret = i2c_master_transmit_receive(dev_handle, command, sizeof(command), recv, sizeof(recv), CONFIG_I2C_TIMEOUT_VALUE_MS);

    if (ret == ESP_OK)
    {
        ESP_LOGD(TAG, "get_data_ready_status OK");
        ESP_LOGI(TAG, "get_data_ready_status 0x%02x%02x", recv[0], recv[1]);
       return ((uint16_t)(recv[0] << 8) | recv[1]) & 0x07FF;
    }
    else if (ret == ESP_ERR_TIMEOUT)
    {
        ESP_LOGW(TAG, "Bus is busy");
    }
    else
    {
        ESP_LOGW(TAG, "get_data_ready_status failed: %d", ret);
    }
    return 0;
}

void sensirion_measure_single_shot() {
    ESP_LOGD(TAG, "Call measure_single_shot");
    
    uint8_t command[] = {0x21, 0x9d};
    esp_err_t ret = i2c_master_transmit(dev_handle, command, sizeof(command), CONFIG_I2C_TIMEOUT_VALUE_MS);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "measure_single_shot OK");
    } else if (ret == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "Bus is busy");
    } else {
        ESP_LOGW(TAG, "measure_single_shot failed: %d", ret);
    }
}
