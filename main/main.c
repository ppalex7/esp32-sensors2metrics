#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "watchdog.h"
#include "sdkconfig.h"

#include "cJSON.h"
#include "led_strip.h"
#include "scd4x_i2c.h"

#include "credentials.h"
#include "device_config.h"
#ifdef CONFIG_LOCAL_WEB_SERVER_ENABLED
#include "device_status.h"
#include "web_server.h"
#endif
#include "wifi.h"
#include "bmp.h"
#include "metrics.h"
#include "sensirion.h"

static gpio_num_t i2c_gpio_sda = CONFIG_I2C_MASTER_SDA;
static gpio_num_t i2c_gpio_scl = CONFIG_I2C_MASTER_SCL;
static i2c_port_t i2c_port = I2C_NUM_0;

static const char *TAG = "co2-monitoring";

volatile int b_tmp_metric;
static struct SensorMetrics pressure_metrics = {
    .sensor_name = CONFIG_PRESSURE_SENSOR_NAME,
    .valid.all_flags = 0,
};
static struct SensorMetrics co2_metrics = {
    .sensor_name = "SCD40",
    .valid.all_flags = 0,
};

SensorMetrics *all_metrics[] = {&pressure_metrics, &co2_metrics};

#ifdef CONFIG_IDF_TARGET_ESP32C6
static led_strip_handle_t led_strip;
void update_led_task(void *pvParameter)
{
    uint8_t r = 16, g = 24, b = 39;
    led_strip_set_pixel(led_strip, 0, r, g, b);
    led_strip_refresh(led_strip);
    while (1)
    {
        if (co2_metrics.valid.has_co2)
        {
            b = 0;
            if (co2_metrics.co2 <= 600)
            {
                r = 0;
                g = 255;
            }
            else if (co2_metrics.co2 <= 800)
            {
                // Переход Зеленый -> Желтый (растет Красный)
                // (co2 - 600) / 200 даст нам коэффициент от 0 до 1
                r = (uint8_t)((co2_metrics.co2 - 600) / 200.0 * 255);
                g = 255;
            }
            else if (co2_metrics.co2 <= 1200)
            {
                // Переход Желтый -> Красный (падает Зеленый)
                r = 255;
                g = (uint8_t)(255 - ((co2_metrics.co2 - 800) / 400.0 * 255));
            }
            else
            {
                r = 255;
                g = 0;
            }
        }
        if (b_tmp_metric)
        {
            led_strip_set_pixel(led_strip, 0, 0, 0, 60);
            led_strip_refresh(led_strip);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        led_strip_set_pixel(led_strip, 0, r >> 3, g >> 3, b >> 3);
        led_strip_refresh(led_strip);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
#endif

void input_polling_task(void *pvParameter)
{
    while (1)
    {
        b_tmp_metric = !gpio_get_level(CONFIG_DEBUG_PIN);
#ifdef CONFIG_IDF_TARGET_ESP32C3
        gpio_set_level(GPIO_NUM_8, !b_tmp_metric);
#endif
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void configure_gpio()
{
#ifdef CONFIG_IDF_TARGET_ESP32C3
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_8),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&led_conf);
#endif

    gpio_config_t input_conf = {
        .pin_bit_mask = (1ULL << CONFIG_DEBUG_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&input_conf);
    configASSERT(xTaskCreate(input_polling_task, "poll_input", 512, NULL, 5, NULL));

#ifdef CONFIG_IDF_TARGET_ESP32C6
    led_strip_config_t strip_config = {
        .strip_gpio_num = GPIO_NUM_8, // GPIO встроенного светодиода на C6
        .max_leds = 1,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    configASSERT(xTaskCreate(update_led_task, "indicate_co2", 1024, NULL, 5, NULL));
#endif
}

static void configure_i2c()
{
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = i2c_port,
        .scl_io_num = i2c_gpio_scl,
        .sda_io_num = i2c_gpio_sda,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t tool_bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &tool_bus_handle));

    add_sensirion_device_to_i2c_bus();
    ESP_ERROR_CHECK(scd4x_stop_periodic_measurement());
    ESP_ERROR_CHECK(scd4x_start_low_power_periodic_measurement());

    add_bmx_device_to_i2c_bus();

    const int max_attempts = 5;
    esp_err_t err = ESP_FAIL;

    // Allow the sensor to settle before the first configuration attempt.
    vTaskDelay(pdMS_TO_TICKS(100) + 1);
    for (int attempt = 1; attempt <= max_attempts; ++attempt)
    {
        err = bmx_configure_device();
        if (err == ESP_OK)
        {
            break;
        }

        ESP_LOGW(TAG, "BMx configuration attempt %d/%d failed: %d",
                 attempt, max_attempts, (int)err);
        if (attempt < max_attempts)
        {
            vTaskDelay(pdMS_TO_TICKS(100) + 1);
        }
    }
    ESP_ERROR_CHECK(err);
}

#if CONFIG_DEBUG_HTTP_CLIENT
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA)
    {
        printf("Error Response: %.*s\n", evt->data_len, (char *)evt->data);
    }
    return ESP_OK;
}
#endif

void send_metrics()
{
    DeviceLabels device_labels = device_config_get();
    if (!device_config_ready(&device_labels))
    {
        ESP_LOGI(TAG, "Skip monitoring: location and room must both be set");
        return;
    }

    ESP_LOGD(TAG, "prepare JSON data for monitoring");
    cJSON *root = cJSON_CreateObject();

    cJSON *labels = cJSON_AddObjectToObject(root, "labels");
    cJSON_AddStringToObject(labels, "location", device_labels.location);
    cJSON_AddStringToObject(labels, "room", device_labels.room);
    if (b_tmp_metric)
    {
        cJSON_AddBoolToObject(labels, "debug", cJSON_True);
    }
    cJSON *metrics = cJSON_AddArrayToObject(root, "metrics");

    for (size_t i = 0; i < sizeof(all_metrics) / sizeof(all_metrics[0]); i++)
    {
        SensorMetrics *m = all_metrics[i];
        cJSON *metric_labels = cJSON_CreateObject();
        cJSON_AddStringToObject(metric_labels, "sensor", m->sensor_name);

        if (m->valid.has_temp)
        {
            cJSON *metric = cJSON_CreateObject();
            cJSON_AddItemToObject(metric, "labels", cJSON_Duplicate(metric_labels, true));
            cJSON_AddStringToObject(metric, "name", "temperature");
            cJSON_AddNumberToObject(metric, "value", m->temperature);
            cJSON_AddItemToArray(metrics, metric);
        }
        if (m->valid.has_pres)
        {
            cJSON *metric = cJSON_CreateObject();
            cJSON_AddItemToObject(metric, "labels", cJSON_Duplicate(metric_labels, true));
            cJSON_AddStringToObject(metric, "name", "air_pressure");
            cJSON_AddNumberToObject(metric, "value", m->pressure);
            cJSON_AddItemToArray(metrics, metric);
        }
        if (m->valid.has_hum)
        {
            cJSON *metric = cJSON_CreateObject();
            cJSON_AddItemToObject(metric, "labels", cJSON_Duplicate(metric_labels, true));
            cJSON_AddStringToObject(metric, "name", "relative_humidity");
            cJSON_AddNumberToObject(metric, "value", m->humidity);
            cJSON_AddItemToArray(metrics, metric);
        }
        if (m->valid.has_co2)
        {
            cJSON *metric = cJSON_CreateObject();
            cJSON_AddItemToObject(metric, "labels", cJSON_Duplicate(metric_labels, true));
            cJSON_AddStringToObject(metric, "name", "co2_concentration");
            cJSON_AddNumberToObject(metric, "value", m->co2);
            cJSON_AddItemToArray(metrics, metric);
        }
        cJSON_Delete(metric_labels);
    }

    cJSON *metric_uptime = cJSON_CreateObject();
    cJSON_AddStringToObject(metric_uptime, "name", "uptime");
    cJSON_AddNumberToObject(metric_uptime, "value", uptime_us() / 1000000);
    cJSON_AddStringToObject(metric_uptime, "type", "IGAUGE");
    cJSON_AddItemToArray(metrics, metric_uptime);

    char *post_data = cJSON_PrintUnformatted(root);

    // 3. Настройка HTTP клиента
    esp_http_client_config_t config = {
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .host = "monitoring.api.cloud.yandex.net",
        .path = "/monitoring/v2/data/write",
        .query = ("service=custom&folderId=" CONFIG_MONITORING_FOLDER),
#if CONFIG_DEBUG_HTTP_CLIENT
        .event_handler = _http_event_handler,
#endif
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Установка заголовков
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", "Bearer " MONITORING_TOKEN);

    // Установка тела запроса
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    // 4. Выполнение запроса
    ESP_LOGI(TAG, "Send data to monitoing");
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Status = %d", esp_http_client_get_status_code(client));
    }
    else
    {
        ESP_LOGE(TAG, "Request to monitoring failed %d", err);
    }

    free(post_data);
    cJSON_Delete(root);
    esp_http_client_cleanup(client);
}

void app_main(void)
{
    watchdog_start();
    wifi_init_nvs();
    esp_err_t config_err = device_config_init();
    if (config_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load labels: %s", esp_err_to_name(config_err));
    }
    wifi_init_sta();

    configure_gpio();
    configure_i2c();
#ifdef CONFIG_LOCAL_WEB_SERVER_ENABLED
    device_status_publish(&pressure_metrics, &co2_metrics, false);
    // Do not stop sensor measurements if the local HTTP server cannot start.
    esp_err_t web_err = web_server_start();
    if (web_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start local web server: %s", esp_err_to_name(web_err));
    }
#endif

#if CONFIG_DEBUG_HTTP_CLIENT
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_VERBOSE);
#endif

    TickType_t xCycleStartTime;
    while (1)
    {
        xCycleStartTime = xTaskGetTickCount();
        bmx_read_measurement(&pressure_metrics);
        if (pressure_metrics.valid.has_pres)
        {
            scd4x_set_ambient_pressure((uint32_t)pressure_metrics.pressure);
        }

        bool co2_ready = false;
        if (scd4x_get_data_ready_status(&co2_ready) == 0 && co2_ready)
        {
            sensirion_read_measurement(&co2_metrics);
        }
        else
        {
            co2_ready = false;
            ESP_LOGI(TAG, "data from sensirion is not available");
        }
#ifdef CONFIG_LOCAL_WEB_SERVER_ENABLED
        device_status_publish(&pressure_metrics, &co2_metrics, co2_ready);
#endif
        send_metrics();
        watchdog_feed();
        vTaskDelayUntil(&xCycleStartTime, pdMS_TO_TICKS(10000));
    }
}
