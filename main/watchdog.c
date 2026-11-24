#include "watchdog.h"

#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define WATCHDOG_INTERVAL_US (60LL * 1000000)

static esp_timer_handle_t watchdog;
static int64_t previous_uptime_us;
static portMUX_TYPE restart_lock = portMUX_INITIALIZER_UNLOCKED;

// Only a requested recovery restart hands elapsed time to the next boot.
static RTC_NOINIT_ATTR struct {
    int64_t elapsed_us;
    uint64_t check;
} restart_record;

int64_t uptime_us(void)
{
    return previous_uptime_us + esp_timer_get_time();
}

void restart_with_uptime(void)
{
    portENTER_CRITICAL(&restart_lock);
    restart_record.elapsed_us = uptime_us();
    restart_record.check = ~(uint64_t)restart_record.elapsed_us;
    portEXIT_CRITICAL(&restart_lock);
    esp_restart();
}

static void watchdog_expired(void *arg)
{
    ESP_LOGE("watchdog", "Measurement loop stalled, restarting");
    restart_with_uptime();
}

void watchdog_start(void)
{
    if (esp_reset_reason() == ESP_RST_SW &&
        restart_record.elapsed_us > 0 &&
        restart_record.check == ~(uint64_t)restart_record.elapsed_us) {
        previous_uptime_us = restart_record.elapsed_us;
    }
    restart_record.check = 0;

    const esp_timer_create_args_t config = {
        .callback = watchdog_expired,
        .name = "measurement_wdt",
    };
    ESP_ERROR_CHECK(esp_timer_create(&config, &watchdog));
    ESP_ERROR_CHECK(esp_timer_start_once(watchdog, WATCHDOG_INTERVAL_US));
}

void watchdog_feed(void)
{
    ESP_ERROR_CHECK(esp_timer_restart(watchdog, WATCHDOG_INTERVAL_US));
}
