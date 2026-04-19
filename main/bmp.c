#include "bmp.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c_master.h"
#include "esp_log.h"

#include "bmx_hal.h"

#ifdef CONFIG_SENSOR_BME280
#include "bme280.h"
#define BMX_I2C_ADDRESS BME280_I2C_ADDR_PRIM
#define BMX_INTF BME280_I2C_INTF
#define BMX_INIT bme280_init
#define SENSOR_SET 1
static struct bme280_dev bmx_sensor;
#endif

#ifdef CONFIG_SENSOR_BMP580
#include "bmp5.h"
#define BMX_I2C_ADDRESS BMP5_I2C_ADDR_SEC
#define BMX_INTF BMP5_I2C_INTF
#define BMX_INIT bmp5_init
#define SENSOR_SET 1
#endif

static const char *TAG = "BMx";

static i2c_master_dev_handle_t dev_handle;

#ifndef SENSOR_SET
#error "No pressure sensor configured"
#endif

void add_bmx_device_to_i2c_bus()
{
    i2c_master_bus_handle_t bus_handle;
    if (i2c_master_get_bus_handle(0, &bus_handle) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to obtain I2C master bus handle");
        abort();
    }

    i2c_device_config_t i2c_dev_conf = {
        .scl_speed_hz = 10000,
        .device_address = BMX_I2C_ADDRESS,
    };
    if (i2c_master_bus_add_device(bus_handle, &i2c_dev_conf, &dev_handle) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add BMP580 device to bus");
        abort();
    }

    bmx_sensor.intf = BMX_INTF;
    bmx_sensor.read = bmx_i2c_read;
    bmx_sensor.write = bmx_i2c_write;
    bmx_sensor.delay_us = bmx_delay_us;
    bmx_sensor.intf_ptr = dev_handle;
}

esp_err_t bmx_configure_device()
{
    int8_t ret = BMX_INIT(&bmx_sensor);
    ESP_LOGD(TAG, "init function returned %d", ret);
    if (ret != 0)
    {
        return ret;
    }

#ifdef CONFIG_SENOR_BMP580
    ESP_LOGD(TAG, "set press_en in OSR_CONFIG");

    uint8_t command[] = {0x36, (1u << 6)};
    ret = i2c_master_transmit(dev_handle, command, sizeof(command), CONFIG_I2C_TIMEOUT_VALUE_MS);

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

    ESP_LOGD(TAG, "set normal mode and ODR=1Hz in ODR_CONFIG");

    uint8_t command[] = {0x37, (0x1cu << 2) | 0b01};
    ret = i2c_master_transmit(dev_handle, command, sizeof(command), CONFIG_I2C_TIMEOUT_VALUE_MS);

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
#endif
#ifdef CONFIG_SENSOR_BME280
    struct bme280_settings settings;
    ret = bme280_get_sensor_settings(&settings, &bmx_sensor);
    ESP_LOGD(TAG, "bme280_get_sensor_settings result %d", ret);
    if (ret != BME280_OK)
    {
        return ret;
    }
    settings.osr_h = BME280_OVERSAMPLING_1X;
    settings.osr_p = BME280_OVERSAMPLING_1X;
    settings.osr_t = BME280_OVERSAMPLING_1X;
    settings.filter = BME280_FILTER_COEFF_OFF;
    settings.standby_time = BME280_STANDBY_TIME_1000_MS;
    uint8_t settings_sel = BME280_SEL_ALL_SETTINGS;
    ret = bme280_set_sensor_settings(settings_sel, &settings, &bmx_sensor);
    ESP_LOGD(TAG, "bme280_set_sensor_settings result %d", ret);
    if (ret != BME280_OK)
    {
        return ret;
    }
    ret = bme280_set_sensor_mode(BME280_POWERMODE_NORMAL, &bmx_sensor);
    ESP_LOGD(TAG, "bme280_set_sensor_mode result %d", ret);
#endif
    vTaskDelay(pdMS_TO_TICKS(100));
    return ret;
}

void bmx_read_measurement(SensorMetrics *measurement)
{
    measurement->valid.all_flags = 0;
#ifdef CONFIG_SENSOR_BMP580
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
#endif
#ifdef CONFIG_SENSOR_BME280
    ESP_LOGD(TAG, "call bme280_get_sensor_data");
    struct bme280_data comp_data;
    int8_t result = bme280_get_sensor_data(BME280_ALL, &comp_data, &bmx_sensor);
    if (result == BME280_OK)
    {
        measurement->temperature = comp_data.temperature;
        measurement->valid.has_temp = 1;
        measurement->pressure = comp_data.pressure;
        measurement->valid.has_pres = 1;
        measurement->humidity = comp_data.humidity;
        measurement->valid.has_hum = 1;
        ESP_LOGI(TAG, "temperature=%.2f, pressure=%06.1f, humidity=%.1f", comp_data.temperature, comp_data.pressure, comp_data.humidity);
    }
    else
    {
        ESP_LOGW(TAG, "bme280_get_sensor_data failed: %d", result);
    }
#endif
}
