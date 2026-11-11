#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#include "board_pins.h"
#include "nvs_storage.h"
#include "wifi_manager.h"
#include "audio_hal.h"
#include "audio_analyzer.h"
#include "wakeword.h"
#include "ws_client.h"
#include "mcp_server.h"
#include "gpio_tools.h"
#include "ui_manager.h"
#include "music_player.h"

static const char *TAG = "APP_MAIN";

#define MIC_READ_CHUNK 320 // 20ms at 16kHz

static void on_wakeword_detected(void)
{
    ESP_LOGI(TAG, "Wakeword detected! Transitioning to LISTENING...");
    ui_set_state(UI_STATE_LISTENING);
    ws_send_listen_start();
}

static void on_wifi_state_changed(wifi_mgr_state_t state)
{
    switch (state) {
    case WIFI_MGR_STATE_CONNECTED: {
        ESP_LOGI(TAG, "WiFi Connected. Connecting to WebSocket Server...");
        ui_set_wifi_status(true);

        char ws_url[128] = {0};
        esp_err_t err = nvs_storage_get_ws_url(ws_url, sizeof(ws_url));
        if (err != ESP_OK || strlen(ws_url) == 0) {
            strcpy(ws_url, "ws://192.168.1.102:8000/ws");
        }
        ws_client_connect(ws_url);
        break;
    }
    case WIFI_MGR_STATE_PROVISIONING:
        ESP_LOGI(TAG, "WiFi Provisioning active");
        ui_set_wifi_status(false);
        ui_set_state(UI_STATE_PROVISIONING);
        break;
    case WIFI_MGR_STATE_DISCONNECTED:
        ESP_LOGW(TAG, "WiFi Disconnected");
        ui_set_wifi_status(false);
        ui_set_state(UI_STATE_ERROR);
        break;
    default:
        break;
    }
}

static void on_ws_status_changed(ws_status_t status)
{
    if (status == WS_STATUS_CONNECTED) {
        ESP_LOGI(TAG, "WebSocket connected successfully");
        ui_set_state(UI_STATE_IDLE);
    } else if (status == WS_STATUS_DISCONNECTED) {
        ESP_LOGW(TAG, "WebSocket disconnected");
        ui_set_state(UI_STATE_ERROR);
    }
}

// Background task monitoring BOOT button (GPIO 0), Vol+ (GPIO 40), Vol- (GPIO 39)
static void button_monitor_task(void *pvParameters)
{
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO) | 
                        (1ULL << VOLUME_UP_BUTTON_GPIO) | 
                        (1ULL << VOLUME_DOWN_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);

    int press_start_ms = 0;
    bool is_boot_pressed = false;
    int64_t last_vol_up_us = 0;
    int64_t last_vol_down_us = 0;

    while (1) {
        int64_t now_us = esp_timer_get_time();

        // 1. Monitor BOOT Button (GPIO 0)
        int boot_level = gpio_get_level(BOOT_BUTTON_GPIO);
        if (boot_level == 0) { // Pressed (Active Low)
            if (!is_boot_pressed) {
                is_boot_pressed = true;
                press_start_ms = now_us / 1000;
            } else {
                int held_ms = (now_us / 1000) - press_start_ms;
                if (held_ms >= 3000) {
                    ESP_LOGW(TAG, "BOOT button held for >= 3s! Resetting WiFi and entering AP mode...");
                    audio_hal_play_beep(600, 200);
                    wifi_manager_trigger_provisioning();
                    while (gpio_get_level(BOOT_BUTTON_GPIO) == 0) {
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }
                    is_boot_pressed = false;
                }
            }
        } else { // Released
            if (is_boot_pressed) {
                int held_ms = (now_us / 1000) - press_start_ms;
                is_boot_pressed = false;
                if (held_ms > 50 && held_ms < 1500) {
                    if (ui_get_state() == UI_STATE_LISTENING) {
                        ESP_LOGI(TAG, "BOOT button clicked: finish recording immediately");
                        ws_send_listen_stop();
                        ui_set_state(UI_STATE_THINKING);
                    } else {
                        ESP_LOGI(TAG, "BOOT button short click: manual wake trigger");
                        wakeword_trigger_manual();
                    }
                }
            }
        }

        // 2. Monitor Volume UP Button (GPIO 40)
        int vol_up_level = gpio_get_level(VOLUME_UP_BUTTON_GPIO);
        if (vol_up_level == 0 && (now_us - last_vol_up_us > 250000)) { // 250ms debounce
            last_vol_up_us = now_us;
            int cur_v = audio_hal_get_volume();
            int new_v = cur_v + 10;
            if (new_v > 100) new_v = 100;
            audio_hal_set_volume(new_v);
            ESP_LOGI(TAG, "Vol+ button pressed -> %d%%", new_v);
        }

        // 3. Monitor Volume DOWN Button (GPIO 39)
        int vol_down_level = gpio_get_level(VOLUME_DOWN_BUTTON_GPIO);
        if (vol_down_level == 0 && (now_us - last_vol_down_us > 250000)) { // 250ms debounce
            last_vol_down_us = now_us;
            int cur_v = audio_hal_get_volume();
            int new_v = cur_v - 10;
            if (new_v < 0) new_v = 0;
            audio_hal_set_volume(new_v);
            ESP_LOGI(TAG, "Vol- button pressed -> %d%%", new_v);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Audio recording & processing task
static void audio_loop_task(void *pvParameters)
{
    int16_t *mic_buf = (int16_t *)malloc(MIC_READ_CHUNK * sizeof(int16_t));
    if (!mic_buf) {
        ESP_LOGE(TAG, "Failed to allocate audio loop buffer");
        vTaskDelete(NULL);
        return;
    }

    uint32_t recording_samples = 0;
    #define MAX_RECORDING_SAMPLES (AUDIO_SAMPLE_RATE_HZ * 7 / 2) // 3.5 seconds max query for low latency

    while (1) {
        int samples_read = audio_hal_read_pcm(mic_buf, MIC_READ_CHUNK, 50);
        if (samples_read > 0) {
            ui_state_t cur_ui = ui_get_state();

            if (cur_ui == UI_STATE_LISTENING) {
                // Stream audio frame to WebSocket server
                ws_send_audio((const uint8_t *)mic_buf, samples_read * sizeof(int16_t));
                recording_samples += samples_read;

                // Stop listening after max speech duration or timeout
                if (recording_samples >= MAX_RECORDING_SAMPLES) {
                    ESP_LOGI(TAG, "Speech recording reached max duration. Stopping...");
                    ws_send_listen_stop();
                    ui_set_state(UI_STATE_THINKING);
                    recording_samples = 0;
                }
            } else {
                recording_samples = 0;
                // Feed samples to Wakeword engine when in IDLE
                if (cur_ui == UI_STATE_IDLE) {
                    wakeword_feed(mic_buf, samples_read);
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, "Starting Xiaozhi Custom Local System for XH-S3E");
    ESP_LOGI(TAG, "ESP32-S3 | 16MB Flash | 8MB Octal PSRAM");
    ESP_LOGI(TAG, "================================================");

    // 1. Initialize NVS
    ESP_ERROR_CHECK(nvs_storage_init());

    // 2. Initialize Audio HAL
    ESP_ERROR_CHECK(audio_hal_init());
    audio_analyzer_init();

    // 3. Initialize Display & UI Manager
    ESP_ERROR_CHECK(ui_manager_init());
    ui_set_state(UI_STATE_BOOTING);

    // Create 20Hz UI Task pinned to Core 1
    xTaskCreatePinnedToCore(ui_task, "ui_task", 4096, NULL, 3, NULL, 1);

    // 4. Initialize MCP Server & Tools
    ESP_ERROR_CHECK(mcp_server_init());
    gpio_tools_register();

    // 5. Initialize Music Player
    ESP_ERROR_CHECK(music_player_init());

    // 6. Initialize Wakeword Engine
    ESP_ERROR_CHECK(wakeword_init(on_wakeword_detected));
    wakeword_start();

    // 7. Initialize WebSocket Client
    ESP_ERROR_CHECK(ws_client_init(on_ws_status_changed));

    // 8. Start Background Tasks
    xTaskCreate(button_monitor_task, "btn_task", 4096, NULL, 5, NULL);
    xTaskCreatePinnedToCore(audio_loop_task, "audio_task", 6144, NULL, 5, NULL, 0);

    // 9. Start WiFi Manager
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_ERROR_CHECK(wifi_manager_init(on_wifi_state_changed));
    wifi_manager_start();

    ESP_LOGI(TAG, "System initialization complete. Event loop running.");
}

