#include "music_player.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "audio_hal.h"
#include "audio_analyzer.h"
#include "ui_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MUSIC_PLAYER";

static TaskHandle_t s_play_task_handle = NULL;
static bool s_is_playing = false;
static bool s_stop_requested = false;
static char s_current_url[256] = {0};

static void music_stream_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Music Stream Task started: %s", s_current_url);
    s_is_playing = true;
    s_stop_requested = false;

    esp_http_client_config_t config = {
        .url = s_current_url,
        .timeout_ms = 8000,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client for music");
        s_is_playing = false;
        s_play_task_handle = NULL;
        ui_set_state(UI_STATE_IDLE);
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        s_is_playing = false;
        s_play_task_handle = NULL;
        ui_set_state(UI_STATE_IDLE);
        vTaskDelete(NULL);
        return;
    }

    int content_length = esp_http_client_fetch_headers(client);
    ESP_LOGI(TAG, "HTTP Response Content-Length = %d", content_length);

    #define CHUNK_SIZE 1024
    uint8_t *chunk = (uint8_t *)malloc(CHUNK_SIZE);
    if (!chunk) {
        ESP_LOGE(TAG, "Failed to allocate memory for music chunk");
        esp_http_client_cleanup(client);
        s_is_playing = false;
        s_play_task_handle = NULL;
        ui_set_state(UI_STATE_IDLE);
        vTaskDelete(NULL);
        return;
    }

    while (!s_stop_requested) {
        int read_bytes = esp_http_client_read(client, (char *)chunk, CHUNK_SIZE);
        if (read_bytes <= 0) {
            ESP_LOGI(TAG, "End of music stream reached");
            break;
        }

        // Stream PCM to Audio HAL and feed spectrum analyzer
        int samples = read_bytes / sizeof(int16_t);
        if (samples > 0) {
            audio_hal_play_pcm((const int16_t *)chunk, samples, 100);
            audio_analyzer_feed((const int16_t *)chunk, samples);
        }
    }

    free(chunk);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "Music Stream Task finished -> Returning to IDLE");
    s_is_playing = false;
    s_play_task_handle = NULL;
    ui_set_state(UI_STATE_IDLE);
    vTaskDelete(NULL);
}

esp_err_t music_player_init(void)
{
    s_is_playing = false;
    s_stop_requested = false;
    ESP_LOGI(TAG, "Music Player module initialized");
    return ESP_OK;
}

esp_err_t music_player_play(const char *url, const char *title, int duration)
{
    if (!url || strlen(url) == 0) return ESP_ERR_INVALID_ARG;
    music_player_stop();

    strncpy(s_current_url, url, sizeof(s_current_url) - 1);
    s_stop_requested = false;

    BaseType_t ret = xTaskCreatePinnedToCore(music_stream_task, "music_task", 6144, NULL, 4, &s_play_task_handle, 1);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}

esp_err_t music_player_stop(void)
{
    if (s_is_playing && s_play_task_handle != NULL) {
        ESP_LOGI(TAG, "Stopping music playback...");
        s_stop_requested = true;
        // Wait for task to finish gracefully
        for (int i = 0; i < 20 && s_is_playing; i++) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    s_is_playing = false;
    return ESP_OK;
}

bool music_player_is_playing(void)
{
    return s_is_playing;
}

