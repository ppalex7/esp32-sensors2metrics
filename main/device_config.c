#include "device_config.h"

#include <ctype.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#ifdef CONFIG_LOCAL_WEB_SERVER_ENABLED
#include "nvs.h"

static const char *NAMESPACE = "device_config";
static const char *KEY = "labels";
#endif
static DeviceLabels labels;
static portMUX_TYPE labels_lock = portMUX_INITIALIZER_UNLOCKED;

static bool normalize_label(const char *input, char *output)
{
    if (!input) {
        return false;
    }
    while (isspace((unsigned char)*input)) {
        ++input;
    }
    size_t length = strlen(input);
    while (length && isspace((unsigned char)input[length - 1])) {
        --length;
    }
    if (length > LABEL_MAX_LENGTH) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)input[i];
        if (ch < 0x20 || ch == 0x7f) {
            return false;
        }
    }
    memcpy(output, input, length);
    output[length] = '\0';
    return true;
}

static void publish_labels(const DeviceLabels *value)
{
    portENTER_CRITICAL(&labels_lock);
    labels = *value;
    portEXIT_CRITICAL(&labels_lock);
}

esp_err_t device_config_init(void)
{
#ifdef CONFIG_LOCAL_WEB_SERVER_ENABLED
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    DeviceLabels stored = {0};
    size_t size = sizeof(stored);
    err = nvs_get_blob(handle, KEY, &stored, &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    DeviceLabels normalized = {0};
    if (size != sizeof(stored) || !memchr(stored.location, '\0', sizeof(stored.location)) ||
        !memchr(stored.room, '\0', sizeof(stored.room)) ||
        !normalize_label(stored.location, normalized.location) ||
        !normalize_label(stored.room, normalized.room)) {
        return ESP_ERR_INVALID_STATE;
    }
    publish_labels(&normalized);
#else
    DeviceLabels configured = {0};
    if (!normalize_label(CONFIG_MONITORING_LABEL_LOCATION, configured.location) ||
        !normalize_label(CONFIG_MONITORING_LABEL_ROOM, configured.room)) {
        return ESP_ERR_INVALID_ARG;
    }
    publish_labels(&configured);
#endif
    return ESP_OK;
}

DeviceLabels device_config_get(void)
{
    portENTER_CRITICAL(&labels_lock);
    DeviceLabels result = labels;
    portEXIT_CRITICAL(&labels_lock);
    return result;
}

bool device_config_ready(const DeviceLabels *value)
{
    return value->location[0] != '\0' && value->room[0] != '\0';
}

#ifdef CONFIG_LOCAL_WEB_SERVER_ENABLED
esp_err_t device_config_save(const char *location, const char *room)
{
    DeviceLabels updated = {0};
    if (!normalize_label(location, updated.location) || !normalize_label(room, updated.room)) {
        return ESP_ERR_INVALID_ARG;
    }

    // The HTTP server is the sole writer. Avoid unnecessary flash writes.
    DeviceLabels current = device_config_get();
    if (!strcmp(current.location, updated.location) && !strcmp(current.room, updated.room)) {
        return ESP_OK;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(handle, KEY, &updated, sizeof(updated));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err == ESP_OK) {
        publish_labels(&updated);
    }
    return err;
}
#endif
