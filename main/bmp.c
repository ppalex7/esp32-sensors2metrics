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
#define SENSOR_SET 1
static struct bme280_dev bmx_sensor;
#endif

#ifdef CONFIG_SENSOR_BMP580
#include "bmp5.h"
#define BMX_I2C_ADDRESS BMP5_I2C_ADDR_SEC
#define BMX_INTF BMP5_I2C_INTF
#define SENSOR_SET 1
static struct bmp5_dev bmx_sensor;
static struct bmp5_osr_odr_press_config osr_odr_cfg = {0};
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

static esp_err_t log_configuration_error(const char *function, int8_t ret)
{
    ESP_LOGW(TAG, "%s failed: %d", function, (int)ret);
#ifdef CONFIG_SENSOR_BMP580
    uint8_t status = 0;
    int8_t status_ret = bmp5_get_regs(BMP5_REG_STATUS, &status, 1, &bmx_sensor);
    if (status_ret == BMP5_OK)
    {
        ESP_LOGW(TAG, "BMP580 STATUS (0x28): 0x%02x", (unsigned int)status);
    }
    else
    {
        ESP_LOGW(TAG, "BMP580 STATUS read failed: %d", (int)status_ret);
    }
#endif
    return ret;
}

esp_err_t bmx_configure_device()
{
    int8_t ret;

#ifdef CONFIG_SENSOR_BMP580
    // Reset without reading INT_STATUS: its POR flag is cleared on read
    // and may be needed by bmp5_init().
    const uint8_t reset_cmd = BMP5_SOFT_RESET_CMD;
    ret = bmp5_set_regs(BMP5_REG_CMD, &reset_cmd, 1, &bmx_sensor);
    vTaskDelay(pdMS_TO_TICKS(20) + 1);
    if (ret != BMP5_OK)
    {
        return log_configuration_error("bmp5_set_regs (soft reset)", ret);
    }

    ret = bmp5_init(&bmx_sensor);
    if (ret != BMP5_OK)
    {
        return log_configuration_error("bmp5_init", ret);
    }

    osr_odr_cfg.osr_t = BMP5_OVERSAMPLING_1X;
    osr_odr_cfg.osr_p = BMP5_OVERSAMPLING_1X;
    osr_odr_cfg.press_en = BMP5_ENABLE;
    osr_odr_cfg.odr = BMP5_ODR_01_HZ;
    ret = bmp5_set_osr_odr_press_config(&osr_odr_cfg, &bmx_sensor);
    if (ret != BMP5_OK)
    {
        return log_configuration_error("bmp5_set_osr_odr_press_config", ret);
    }

    ret = bmp5_set_power_mode(BMP5_POWERMODE_NORMAL, &bmx_sensor);
    if (ret != BMP5_OK)
    {
        return log_configuration_error("bmp5_set_power_mode", ret);
    }
#endif
#ifdef CONFIG_SENSOR_BME280
    ret = bme280_init(&bmx_sensor);
    if (ret != BME280_OK)
    {
        return log_configuration_error("bme280_init", ret);
    }

    struct bme280_settings settings;
    ret = bme280_get_sensor_settings(&settings, &bmx_sensor);
    if (ret != BME280_OK)
    {
        return log_configuration_error("bme280_get_sensor_settings", ret);
    }
    settings.osr_h = BME280_OVERSAMPLING_1X;
    settings.osr_p = BME280_OVERSAMPLING_1X;
    settings.osr_t = BME280_OVERSAMPLING_1X;
    settings.filter = BME280_FILTER_COEFF_OFF;
    settings.standby_time = BME280_STANDBY_TIME_1000_MS;
    uint8_t settings_sel = BME280_SEL_ALL_SETTINGS;
    ret = bme280_set_sensor_settings(settings_sel, &settings, &bmx_sensor);
    if (ret != BME280_OK)
    {
        return log_configuration_error("bme280_set_sensor_settings", ret);
    }
    ret = bme280_set_sensor_mode(BME280_POWERMODE_NORMAL, &bmx_sensor);
    if (ret != BME280_OK)
    {
        return log_configuration_error("bme280_set_sensor_mode", ret);
    }
#endif
    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

void bmx_read_measurement(SensorMetrics *measurement)
{
    measurement->valid.all_flags = 0;
#ifdef CONFIG_SENSOR_BMP580
    enum { max_failures = 3 };
    static unsigned int consecutive_failures = 0;

    // Skip reads while a failed reinitialization is pending.
    if (consecutive_failures < max_failures)
    {
        struct bmp5_sensor_data data = {0};
        ESP_LOGD(TAG, "call bmp5_get_sensor_data");
        int result = bmp5_get_sensor_data(&data, &osr_odr_cfg, &bmx_sensor);
        if (result != BMP5_OK)
        {
            ESP_LOGW(TAG, "bmp5_get_sensor_data failed: %d", result);
        }
        else if (data.temperature == 0x7F7F7F / 65536.0f ||
                 data.pressure == 0x7F7F7F / 64.0f)
        {
            // The data registers contain their reset values, not a measurement.
            ESP_LOGW(TAG, "BMP580 measurement not initialized: T=%.2f, P=%.2f",
                     data.temperature, data.pressure);
        }
        else
        {
            consecutive_failures = 0;
            measurement->pressure = data.pressure;
            measurement->valid.has_pres = 1;
            measurement->temperature = data.temperature;
            measurement->valid.has_temp = 1;
            ESP_LOGI(TAG, "temperature=%.2f, pressure=%06.1f", measurement->temperature, measurement->pressure);
            return;
        }

        ++consecutive_failures;
        ESP_LOGW(TAG, "BMP580 consecutive measurement failures: %u/%u",
                 consecutive_failures, (unsigned int)max_failures);
    }

    if (consecutive_failures >= max_failures)
    {
        ESP_LOGW(TAG, "Reinitializing BMP580");
        esp_err_t err = bmx_configure_device();
        if (err == ESP_OK)
        {
            consecutive_failures = 0;
            ESP_LOGI(TAG, "BMP580 reinitialized successfully");
        }
        else
        {
            ESP_LOGW(TAG, "BMP580 reinitialization failed: %d; retrying next cycle", (int)err);
        }
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
