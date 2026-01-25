#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "cJSON.h"

#include "wifi.h"
#include "bmp.h"
#include "sensirion.h"

static gpio_num_t i2c_gpio_sda = CONFIG_I2C_MASTER_SDA;
static gpio_num_t i2c_gpio_scl = CONFIG_I2C_MASTER_SCL;
static i2c_port_t i2c_port = I2C_NUM_0;

static const char *TAG = "co2-monitoring";

volatile int b_tmp_metric;

void input_polling_task(void *pvParameter)
{
    while (1)
    {
        b_tmp_metric = !gpio_get_level(GPIO_NUM_10);
        gpio_set_level(GPIO_NUM_8, !b_tmp_metric);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void configure_gpio()
{
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_8),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&led_conf);

    gpio_config_t input_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_10),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&input_conf);

    configASSERT(xTaskCreate(input_polling_task, "poll_input", 512, NULL, 5, NULL));
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
    add_bmp_device_to_i2c_bus();
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

void send_metrics(bmp_measurement *bmp, sensirion_measurement *sensirion)
{
    ESP_LOGD(TAG, "prepare JSON data for monitoring");
    cJSON *root = cJSON_CreateObject();

    cJSON *labels = cJSON_AddObjectToObject(root, "labels");
#if CONFIG_MONITORING_LABEL_LOCATION_IS_EMPTY
#error "location label is mandatory"
#endif
    cJSON_AddStringToObject(labels, "location", CONFIG_MONITORING_LABEL_LOCATION);
    cJSON_AddStringToObject(labels, "room", CONFIG_MONITORING_LABEL_ROOM);
    if (b_tmp_metric)
    {
        cJSON_AddBoolToObject(labels, "debug", cJSON_True);
    }

    cJSON *metric_temperature = cJSON_CreateObject();
    cJSON_AddStringToObject(metric_temperature, "name", "temperature");
    cJSON_AddNumberToObject(metric_temperature, "value", bmp->t);

    cJSON *metric_pressure = cJSON_CreateObject();
    cJSON_AddStringToObject(metric_pressure, "name", "air_pressure");
    cJSON_AddNumberToObject(metric_pressure, "value", bmp->pressure);
    cJSON_AddStringToObject(metric_pressure, "type", "IGAUGE");

    cJSON *metric_humidity = cJSON_CreateObject();
    cJSON_AddStringToObject(metric_humidity, "name", "relative_humidity");
    cJSON_AddNumberToObject(metric_humidity, "value", sensirion->rh);

    cJSON *metric_co2 = cJSON_CreateObject();
    cJSON_AddStringToObject(metric_co2, "name", "co2_concentration");
    cJSON_AddNumberToObject(metric_co2, "value", sensirion->co2);
    cJSON_AddStringToObject(metric_co2, "type", "IGAUGE");

    cJSON *metrics = cJSON_AddArrayToObject(root, "metrics");
    cJSON_AddItemToArray(metrics, metric_temperature);
    cJSON_AddItemToArray(metrics, metric_pressure);
    cJSON_AddItemToArray(metrics, metric_humidity);
    cJSON_AddItemToArray(metrics, metric_co2);

    char *post_data = cJSON_PrintUnformatted(root);

    // 3. Настройка HTTP клиента
    esp_http_client_config_t config = {
        .method = HTTP_METHOD_POST,
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
    esp_http_client_set_header(client, "Authorization", ("Bearer " CONFIG_MONITORING_TOKEN));

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
}

void app_main(void)
{
    wifi_init_nvs();
    wifi_init_sta();

    configure_gpio();
    configure_i2c();

#if CONFIG_DEBUG_HTTP_CLIENT
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_VERBOSE);
#endif

    esp_err_t ret;
    int retries = 20;
    do
    {
        vTaskDelay(pdMS_TO_TICKS(20));
        ESP_LOGI(TAG, "Enable pressure measurement, retries left %d", retries);
        ret = bmp_enable_pressure();
    } while (retries-- > 0 && ret != ESP_OK);

    bmp_measurement bmp_measurement;
    sensirion_measurement sensirion_measurement;
    while (1)
    {
        bmp_read_measurement(&bmp_measurement);
        sensirion_set_ambient_pressure(bmp_measurement.pressure);

        sensirion_measure_single_shot();
        int retries = 20;
        int ready;
        do
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            ready = sensirion_get_data_ready_status();
        } while (!ready && (retries-- > 0));
        if (ready)
        {
            sensirion_read_measurement(&sensirion_measurement);
            send_metrics(&bmp_measurement, &sensirion_measurement);
        }
        else
        {
            ESP_LOGW(TAG, "data from sensirion is not available");
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
