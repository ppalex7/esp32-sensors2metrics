#pragma once

#include <stdint.h>

int8_t bmx_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr);
int8_t bmx_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr);
void bmx_delay_us(uint32_t period_us, void *intf_ptr);
