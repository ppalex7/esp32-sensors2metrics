#pragma once

#include <stdbool.h>
#include "metrics.h"

typedef struct {
    SensorMetrics pressure;
    SensorMetrics co2;
    bool co2_data_ready;
} DeviceStatus;

// The measurement task publishes a coherent snapshot once per cycle.
void device_status_publish(const SensorMetrics *pressure, const SensorMetrics *co2,
                           bool co2_data_ready);
DeviceStatus device_status_get(void);
