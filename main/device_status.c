#include "device_status.h"

#include "freertos/FreeRTOS.h"

static DeviceStatus status;
static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;

void device_status_publish(const SensorMetrics *pressure, const SensorMetrics *co2,
                           bool co2_data_ready)
{
    portENTER_CRITICAL(&status_lock);
    status.pressure = *pressure;
    status.co2 = *co2;
    status.co2_data_ready = co2_data_ready;
    portEXIT_CRITICAL(&status_lock);
}

DeviceStatus device_status_get(void)
{
    portENTER_CRITICAL(&status_lock);
    DeviceStatus result = status;
    portEXIT_CRITICAL(&status_lock);
    return result;
}
