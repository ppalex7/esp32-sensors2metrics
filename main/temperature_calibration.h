#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "metrics.h"

typedef enum {
    CALIBRATION_IDLE,
    CALIBRATION_PENDING,
    CALIBRATION_RUNNING,
    CALIBRATION_SUCCEEDED,
    CALIBRATION_FAILED,
} CalibrationState;

typedef struct {
    uint32_t id;
    CalibrationState state;
    bool has_temperatures;
    double reference_temperature;
    double scd40_temperature;
    bool has_old_offset;
    bool has_new_offset;
    uint16_t old_offset_raw;
    uint16_t new_offset_raw;
    bool persisted;
    const char *error;
} TemperatureCalibration;

// Called by the HTTP task. Only one request may be pending/running at a time.
bool temperature_calibration_request(uint32_t *id);
TemperatureCalibration temperature_calibration_get(void);

// Called only by the sensor task after its regular measurement reads.
// May invalidate pre-calibration SCD40 measurements after stopping the sensor.
void temperature_calibration_process(const SensorMetrics *reference, SensorMetrics *scd40,
                                     bool *scd40_data_ready);
