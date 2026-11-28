#include "web_server.h"

#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "device_config.h"
#include "device_status.h"
#include "watchdog.h"
#ifdef CONFIG_SCD40_TEMPERATURE_CALIBRATION_ENABLED
#include "temperature_calibration.h"
#endif

static const char *TAG = "web_server";
extern const char index_html_start[] asm("_binary_index_html_start");

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, body);
    cJSON_free(body);
    return err;
}

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, index_html_start);
}

static bool add_labels(cJSON *root, const DeviceLabels *labels)
{
    cJSON *object = cJSON_AddObjectToObject(root, "labels");
    return object &&
           cJSON_AddStringToObject(object, "location", labels->location) &&
           cJSON_AddStringToObject(object, "room", labels->room) &&
           cJSON_AddBoolToObject(root, "labels_ready", device_config_ready(labels));
}

static bool add_reading(cJSON *sensor, const char *name, double value, bool ready)
{
    cJSON *reading = cJSON_AddObjectToObject(sensor, name);
    return reading && cJSON_AddBoolToObject(reading, "ready", ready) &&
           (ready ? cJSON_AddNumberToObject(reading, "value", value)
                  : cJSON_AddNullToObject(reading, "value"));
}

#ifdef CONFIG_SCD40_TEMPERATURE_CALIBRATION_ENABLED
static const char *calibration_state_name(CalibrationState state)
{
    switch (state) {
        case CALIBRATION_PENDING: return "pending";
        case CALIBRATION_RUNNING: return "running";
        case CALIBRATION_SUCCEEDED: return "succeeded";
        case CALIBRATION_FAILED: return "failed";
        default: return "idle";
    }
}

static bool add_calibration_status(cJSON *root)
{
    TemperatureCalibration job = temperature_calibration_get();
    cJSON *object = cJSON_AddObjectToObject(root, "scd40_temperature_calibration");
    return object && cJSON_AddNumberToObject(object, "id", job.id) &&
           cJSON_AddStringToObject(object, "state", calibration_state_name(job.state)) &&
           cJSON_AddBoolToObject(object, "persisted", job.persisted) &&
           (!job.has_temperatures ||
            (cJSON_AddNumberToObject(object, "reference_temperature", job.reference_temperature) &&
             cJSON_AddNumberToObject(object, "scd40_temperature", job.scd40_temperature))) &&
           (!job.has_old_offset || cJSON_AddNumberToObject(object, "old_offset_raw", job.old_offset_raw)) &&
           (!job.has_new_offset || cJSON_AddNumberToObject(object, "new_offset_raw", job.new_offset_raw)) &&
           (!job.error || cJSON_AddStringToObject(object, "error", job.error));
}

static esp_err_t calibrate_temperature_handler(httpd_req_t *req)
{
    // Require a non-simple browser content type. No parameters are accepted:
    // the reference and measured temperatures come from the sensor task.
    char content_type[32];
    if (req->content_len != 0 ||
        httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
        strcasecmp(content_type, "application/json") != 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected an empty application/json request");
    }

    uint32_t id;
    if (!temperature_calibration_request(&id)) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "Calibration already in progress");
    }

    cJSON *root = cJSON_CreateObject();
    if (!root || !cJSON_AddNumberToObject(root, "id", id)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    httpd_resp_set_status(req, "202 Accepted");
    return send_json(req, root);
}
#endif

static esp_err_t status_handler(httpd_req_t *req)
{
    DeviceLabels labels = device_config_get();
    DeviceStatus status = device_status_get();
    cJSON *root = cJSON_CreateObject();
    cJSON *sensors = cJSON_AddObjectToObject(root, "sensors");
    const SensorMetrics *p = &status.pressure;
    const SensorMetrics *c = &status.co2;
    cJSON *pressure = cJSON_AddObjectToObject(sensors, CONFIG_PRESSURE_SENSOR_NAME);
    cJSON *co2 = cJSON_AddObjectToObject(sensors, "SCD40");

    bool ok = root && sensors && pressure && co2 && add_labels(root, &labels) &&
              cJSON_AddNumberToObject(root, "uptime", uptime_us() / 1000000) &&
              add_reading(pressure, "temperature", p->temperature, p->valid.has_temp) &&
              add_reading(pressure, "air_pressure", p->pressure, p->valid.has_pres) &&
#ifdef CONFIG_SENSOR_BME280
              add_reading(pressure, "relative_humidity", p->humidity, p->valid.has_hum) &&
#endif
              add_reading(co2, "temperature", c->temperature, c->valid.has_temp) &&
              add_reading(co2, "relative_humidity", c->humidity, c->valid.has_hum) &&
              add_reading(co2, "co2_concentration", c->co2, c->valid.has_co2) &&
              cJSON_AddBoolToObject(root, "scd40_data_ready", status.co2_data_ready)
#ifdef CONFIG_SCD40_TEMPERATURE_CALIBRATION_ENABLED
              && add_calibration_status(root)
#endif
              ;

    if (!ok) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    return send_json(req, root);
}

static esp_err_t labels_handler(httpd_req_t *req)
{
    char content_type[32];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
        strcasecmp(content_type, "application/json") != 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected application/json");
    }

    // Worst-case JSON escaping of two labels plus field names/formatting.
    enum { max_body = 2 * LABEL_MAX_LENGTH * 6 + 128 };
    if (req->content_len == 0 || req->content_len > max_body) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body length");
    }

    char body[max_body + 1];
    size_t received = 0;
    while (received < req->content_len) {
        int count = httpd_req_recv(req, body + received, req->content_len - received);
        if (count <= 0) {
            // A failed/timed-out receive closes this socket instead of blocking the HTTP task.
            return ESP_FAIL;
        }
        received += count;
    }
    if (memchr(body, '\0', received)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }
    body[received] = '\0';
    // Only a flat object with string fields is needed. Limit nesting before
    // passing untrusted input to the recursive JSON parser on the HTTP task stack.
    bool quoted = false;
    bool escaped = false;
    unsigned int depth = 0;
    for (size_t i = 0; i < received; ++i) {
        char ch = body[i];
        if (quoted) {
            if (escaped) {
                if (ch == 'u' && i + 4 < received && !memcmp(body + i + 1, "0000", 4)) {
                    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "NUL is not allowed in labels");
                }
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                quoted = false;
            }
        } else if (ch == '"') {
            quoted = true;
        } else if (ch == '{') {
            if (++depth > 1) {
                return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected a flat JSON object");
            }
        } else if (ch == '}') {
            if (!depth) {
                return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
            }
            --depth;
        } else if (ch == '[' || ch == ']') {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected a flat JSON object");
        }
    }
    cJSON *json = cJSON_ParseWithLengthOpts(body, received + 1, NULL, true);
    cJSON *location = cJSON_GetObjectItemCaseSensitive(json, "location");
    cJSON *room = cJSON_GetObjectItemCaseSensitive(json, "room");
    if (!cJSON_IsObject(json) || !cJSON_IsString(location) || !cJSON_IsString(room)) {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected location and room strings");
    }
    esp_err_t err = device_config_save(location->valuestring, room->valuestring);
    cJSON_Delete(json);
    if (err == ESP_ERR_INVALID_ARG) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid label (max 128 UTF-8 bytes, no control characters)");
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save labels: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save labels");
    }

    // Return the persisted (normalized) values rather than the submitted strings.
    DeviceLabels labels = device_config_get();
    cJSON *root = cJSON_CreateObject();
    if (!root || !add_labels(root, &labels)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    return send_json(req, root);
}

esp_err_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // JSON parsing, request body, label snapshots and response serialization share this stack.
    config.stack_size = 8192;
    // Reserve sockets for the outbound monitoring HTTPS client.
    config.max_open_sockets = 3;
    config.lru_purge_enable = true;
    httpd_handle_t server;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        return err;
    }
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index_handler},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler},
        {.uri = "/api/labels", .method = HTTP_POST, .handler = labels_handler},
#ifdef CONFIG_SCD40_TEMPERATURE_CALIBRATION_ENABLED
        {.uri = "/api/scd40/calibrate-temperature", .method = HTTP_POST, .handler = calibrate_temperature_handler},
#endif
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) {
            httpd_stop(server);
            return err;
        }
    }
    ESP_LOGI(TAG, "Status and settings page available on port %u", (unsigned)config.server_port);
    return ESP_OK;
}
