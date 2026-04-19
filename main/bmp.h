#pragma once

#include "esp_err.h"

#include "metrics.h"

void add_bmx_device_to_i2c_bus();
esp_err_t bmx_configure_device();
void bmx_read_measurement(SensorMetrics *measurement);
