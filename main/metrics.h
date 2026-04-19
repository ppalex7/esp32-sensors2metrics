#pragma once

#include <stdint.h>

typedef struct SensorMetrics
{
    const char *const sensor_name;

    double temperature;
    double pressure;
    double humidity;
    double co2;

    union
    {
        uint8_t all_flags;
        struct
        {
            uint8_t has_temp : 1;
            uint8_t has_pres : 1;
            uint8_t has_hum : 1;
            uint8_t has_co2 : 1;
        };
    } valid;
} SensorMetrics;
