#pragma once

#include <stdint.h>

#include "metrics.h"

void add_sensirion_device_to_i2c_bus();
void sensirion_read_measurement(SensorMetrics *measurement);
