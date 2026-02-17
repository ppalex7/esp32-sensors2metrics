#pragma once

#include <stdint.h>

typedef struct {
    uint16_t co2;   /* ppm */
    int16_t t;      /* 'C */
    uint8_t rh;     /* % */
    bool ready;
} sensirion_measurement;

void add_sensirion_device_to_i2c_bus();
void sensirion_read_measurement(sensirion_measurement *measurement);
