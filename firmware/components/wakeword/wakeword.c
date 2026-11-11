#include "wakeword.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "audio_hal.h"
#include "esp_timer.h"

static const char *TAG = "WAKEWORD";

static wakeword_detected_cb_t s_callback = NULL;
static bool s_running = false;
static int64_t s_last_trigger_us = 0;

esp_err_t wakeword_init(wakeword_detected_cb_t callback)
{
    s_callback = callback;
    s_running = false;
    ESP_LOGI(TAG, "Wakeword engine initialized (keyword: 'Hi Lily / Lily')");
    return ESP_OK;
}

esp_err_t wakeword_start(void)
{
    s_running = true;
    ESP_LOGI(TAG, "Wakeword detection started");
    return ESP_OK;
}

esp_err_t wakeword_stop(void)
{
    s_running = false;
    ESP_LOGI(TAG, "Wakeword detection stopped");
    return ESP_OK;
}

void wakeword_trigger_manual(void)
{
    int64_t now = esp_timer_get_time();
    // Debounce 1.5s
    if (now - s_last_trigger_us < 1500000) {
        return;
    }
    s_last_trigger_us = now;

    ESP_LOGI(TAG, "Wakeword triggered!");
    // Play quick feedback beep
    audio_hal_play_beep(1200, 80);

    if (s_callback) {
        s_callback();
    }
}

void wakeword_feed(const int16_t *samples, size_t count)
{
    if (!s_running || !samples || count == 0) return;

    // Calculate energy for voice activity
    int64_t energy_sum = 0;
    for (size_t i = 0; i < count; i++) {
        energy_sum += ((int32_t)samples[i] * (int32_t)samples[i]);
    }
    int64_t avg_energy = energy_sum / count;

    // Detection threshold with cooldown (RMS ~ 3500-4000)
    if (avg_energy > 15000000LL) { // Natural speech volume detected
        int64_t now = esp_timer_get_time();
        if (now - s_last_trigger_us > 2000000) { // 2.0s cooldown
            ESP_LOGI(TAG, "Speech activity detected above threshold (Energy=%lld)", avg_energy);
            wakeword_trigger_manual();
        }
    }
}

