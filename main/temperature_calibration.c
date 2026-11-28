#include "temperature_calibration.h"

#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "scd4x_i2c.h"

// Low-power periodic measurements arrive approximately every 30 seconds.
#define CALIBRATION_TIMEOUT_US (60LL * 1000000)
#define MAX_OFFSET_C 20.0

static const char *TAG = "scd40_calibration";
static TemperatureCalibration calibration;
static int64_t requested_at_us;
static portMUX_TYPE calibration_lock = portMUX_INITIALIZER_UNLOCKED;
// Accessed only by the measurement task. Retry a failed restart on later cycles.
static bool restart_pending;

bool temperature_calibration_request(uint32_t *id)
{
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&calibration_lock);
    if (calibration.state == CALIBRATION_PENDING || calibration.state == CALIBRATION_RUNNING) {
        portEXIT_CRITICAL(&calibration_lock);
        return false;
    }
    calibration = (TemperatureCalibration){.id = calibration.id + 1, .state = CALIBRATION_PENDING};
    requested_at_us = now;
    *id = calibration.id;
    portEXIT_CRITICAL(&calibration_lock);
    return true;
}

TemperatureCalibration temperature_calibration_get(void)
{
    portENTER_CRITICAL(&calibration_lock);
    TemperatureCalibration result = calibration;
    portEXIT_CRITICAL(&calibration_lock);
    return result;
}

static void publish_result(TemperatureCalibration *result)
{
    result->state = result->error ? CALIBRATION_FAILED : CALIBRATION_SUCCEEDED;
    portENTER_CRITICAL(&calibration_lock);
    calibration = *result;
    portEXIT_CRITICAL(&calibration_lock);
}

static void calibrate(TemperatureCalibration *result)
{
    // The SCD4x allows offset reads/writes and persistence only in idle mode.
    if (scd4x_stop_periodic_measurement() != 0) {
        result->error = "Failed to stop SCD40 periodic measurements";
        // The command may have reached the sensor despite the transport error.
        restart_pending = true;
        return;
    }

    do {
        if (scd4x_get_temperature_offset_raw(&result->old_offset_raw) != 0) {
            result->error = "Failed to read the old temperature offset";
            break;
        }
        result->has_old_offset = true;
        const double old_offset = result->old_offset_raw * 175.0 / 65535.0;
        const double new_offset = old_offset + result->scd40_temperature - result->reference_temperature;
        if (new_offset < 0.0 || new_offset > MAX_OFFSET_C) {
            result->error = "Calculated offset is outside the recommended 0–20 °C range; not written";
            break;
        }
        result->new_offset_raw = (uint16_t)(new_offset * 65535.0 / 175.0 + 0.5);
        // Do not round the upper endpoint above the recommended range.
        const uint16_t max_raw = (uint16_t)(MAX_OFFSET_C * 65535.0 / 175.0);
        if (result->new_offset_raw > max_raw) {
            result->new_offset_raw = max_raw;
        }
        result->has_new_offset = true;

        if (result->new_offset_raw != result->old_offset_raw) {
            uint16_t read_back = 0;
            if (scd4x_set_temperature_offset_raw(result->new_offset_raw) != 0 ||
                scd4x_get_temperature_offset_raw(&read_back) != 0 || read_back != result->new_offset_raw) {
                // Best effort: restore the previously read RAM setting, never persist an unverified value.
                bool restored = scd4x_set_temperature_offset_raw(result->old_offset_raw) == 0 &&
                                scd4x_get_temperature_offset_raw(&read_back) == 0 && read_back == result->old_offset_raw;
                result->error = restored ? "Offset write/readback failed; previous offset restored"
                                         : "Offset write/readback failed; current offset is uncertain";
                break;
            }
        }

        // A manual calibration explicitly requests persistence. This also persists an unchanged
        // raw value that may previously have been written to RAM only.
        if (scd4x_persist_settings() != 0) {
            result->error = "EEPROM persistence failed; RAM offset may be changed, saved offset is uncertain";
            break;
        }
        result->persisted = true;
        ESP_LOGI(TAG, "Temperature offset saved: 0x%04x -> 0x%04x", (unsigned)result->old_offset_raw,
                 (unsigned)result->new_offset_raw);
    } while (false);

    if (scd4x_start_low_power_periodic_measurement() != 0) {
        restart_pending = true;
        result->error = result->persisted ? "Offset saved, but restarting measurements failed; will retry"
                                          : "Calibration failed and restarting measurements failed; will retry";
    }
}

void temperature_calibration_process(const SensorMetrics *reference, SensorMetrics *scd40,
                                     bool *scd40_data_ready)
{
    if (restart_pending) {
        if ((*scd40_data_ready && scd40->valid.has_temp) || scd4x_start_low_power_periodic_measurement() == 0) {
            restart_pending = false;
        }
    }

    const int64_t now = esp_timer_get_time();
    // Both temperatures must be in the specified SCD4x temperature range.
    const bool fresh = !restart_pending && *scd40_data_ready && reference->valid.has_temp &&
                       scd40->valid.has_temp && isfinite(reference->temperature) && isfinite(scd40->temperature) &&
                       reference->temperature >= -10.0 && reference->temperature <= 60.0 &&
                       scd40->temperature >= -10.0 && scd40->temperature <= 60.0;

    portENTER_CRITICAL(&calibration_lock);
    if (calibration.state != CALIBRATION_PENDING) {
        portEXIT_CRITICAL(&calibration_lock);
        return;
    }
    if (now - requested_at_us >= CALIBRATION_TIMEOUT_US) {
        calibration.state = CALIBRATION_FAILED;
        calibration.error = "No fresh valid temperatures from both sensors within 60 seconds";
        portEXIT_CRITICAL(&calibration_lock);
        return;
    }
    if (!fresh) {
        portEXIT_CRITICAL(&calibration_lock);
        return;
    }
    calibration.state = CALIBRATION_RUNNING;
    TemperatureCalibration result = calibration;
    portEXIT_CRITICAL(&calibration_lock);

    result.has_temperatures = true;
    result.reference_temperature = reference->temperature;
    result.scd40_temperature = scd40->temperature;
    calibrate(&result);

    // Do not publish/send measurements taken before calibration as new temperature/humidity.
    scd40->valid.all_flags = 0;
    *scd40_data_ready = false;
    if (result.error) {
        ESP_LOGW(TAG, "%s", result.error);
    }
    publish_result(&result);
}
