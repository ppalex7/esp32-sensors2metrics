#pragma once

#include "esp_err.h"

typedef struct {
    uint32_t pressure;   /* Pa */
    double t;      /* 'C */
} bmp_measurement;


void add_bmp_device_to_i2c_bus();
esp_err_t bmp_enable_pressure();
esp_err_t bmp_normal_mode_1hz();
void bmp_read_measurement(bmp_measurement *measurement);
