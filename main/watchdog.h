#pragma once

#include <stdint.h>

void watchdog_start(void);
void watchdog_feed(void);
void restart_with_uptime(void) __attribute__((noreturn));
int64_t uptime_us(void);
