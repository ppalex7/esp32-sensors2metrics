#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Maximum UTF-8 byte length, excluding the terminating NUL.
#define LABEL_MAX_LENGTH 128

typedef struct {
    char location[LABEL_MAX_LENGTH + 1];
    char room[LABEL_MAX_LENGTH + 1];
} DeviceLabels;

// Call after NVS has been initialized, before starting the HTTP server.
esp_err_t device_config_init(void);
DeviceLabels device_config_get(void);
bool device_config_ready(const DeviceLabels *labels);

// Empty (or whitespace-only) values are valid and disable metric submission.
// The pair becomes visible to other tasks only after a successful NVS commit.
esp_err_t device_config_save(const char *location, const char *room);
