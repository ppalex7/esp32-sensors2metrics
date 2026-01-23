#pragma once

#include <stdint.h>

typedef struct {
    uint16_t co2;   /* ppm */
    int16_t t;      /* 'C */
    uint8_t rh;     /* % */
} sensirion_measurement;

void add_sensirion_device_to_i2c_bus();
void sensirion_measure_single_shot();
void sensirion_read_measurement(sensirion_measurement *measurement);
void sensirion_set_ambient_pressure(uint32_t pressure);
uint32_t sensirion_get_ambient_pressure();
uint16_t sensirion_get_data_ready_status();
