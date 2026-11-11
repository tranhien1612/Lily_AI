#include "ws_client.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "cJSON.h"
#include "mcp_server.h"
#include "ui_manager.h"
#include "audio_hal.h"
#include "audio_analyzer.h"
#include "music_player.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "WS_CLIENT";

static esp_websocket_client_handle_t s_client = NULL;
static ws_event_callback_t s_status_cb = NULL;
static bool s_is_connected = false;
static char s_current_url[256] = {0};

static void handle_json_message(const char *data, int len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse incoming JSON: %.*s", len, data);
        return;
    }

    cJSON *type_obj = cJSON_GetObjectItem(root, "type");
    if (type_obj && cJSON_IsString(type_obj)) {
        const char *type = type_obj->valuestring;
        ESP_LOGI(TAG, "Received message type: %s", type);

        if (strcmp(type, "hello") == 0) {
            ESP_LOGI(TAG, "Server greeted. System ready.");
            cJSON *epoch_obj = cJSON_GetObjectItem(root, "epoch");
            if (epoch_obj && cJSON_IsNumber(epoch_obj)) {
                time_t sec = (time_t)epoch_obj->valuedouble;
                struct timeval tv = { .tv_sec = sec, .tv_usec = 0 };
                settimeofday(&tv, NULL);
                setenv("TZ", "ICT-7", 1);
                tzset();
                ESP_LOGI(TAG, "Synchronized time from server: %ld", (long)sec);
            }
            ui_set_state(UI_STATE_IDLE);
        } else if (strcmp(type, "time") == 0) {
            cJSON *epoch_obj = cJSON_GetObjectItem(root, "epoch");
            if (epoch_obj && cJSON_IsNumber(epoch_obj)) {
                time_t sec = (time_t)epoch_obj->valuedouble;
                struct timeval tv = { .tv_sec = sec, .tv_usec = 0 };
                settimeofday(&tv, NULL);
                setenv("TZ", "ICT-7", 1);
                tzset();
                ESP_LOGI(TAG, "Updated time from server: %ld", (long)sec);
            }
        } else if (strcmp(type, "stt") == 0 || strcmp(type, "transcript") == 0) {
            cJSON *text_obj = cJSON_GetObjectItem(root, "text");
            if (text_obj && cJSON_IsString(text_obj)) {
                ui_update_asr_text(text_obj->valuestring);
            }
        } else if (strcmp(type, "tts") == 0) {
            cJSON *state_obj = cJSON_GetObjectItem(root, "state");
            const char *state_str = (state_obj && cJSON_IsString(state_obj)) ? state_obj->valuestring : "";
            if (strcmp(state_str, "start") == 0) {
                ui_set_state(UI_STATE_SPEAKING);
            } else if (strcmp(state_str, "stop") == 0) {
                ui_set_state(UI_STATE_IDLE);
            } else if (strcmp(state_str, "sentence_start") == 0) {
                cJSON *text_obj = cJSON_GetObjectItem(root, "text");
                if (text_obj && cJSON_IsString(text_obj)) {
                    ui_update_response_text(text_obj->valuestring);
                }
            }
        } else if (strcmp(type, "response_text") == 0) {
            cJSON *text_obj = cJSON_GetObjectItem(root, "text");
            if (text_obj && cJSON_IsString(text_obj)) {
                ui_update_response_text(text_obj->valuestring);
            }
        } else if (strcmp(type, "llm") == 0) {
            cJSON *emotion_obj = cJSON_GetObjectItem(root, "emotion");
            if (emotion_obj && cJSON_IsString(emotion_obj)) {
                if (strcmp(emotion_obj->valuestring, "thinking") == 0) {
                    ui_set_state(UI_STATE_THINKING);
                }
            }
        } else if (strcmp(type, "tts_start") == 0) {
            ui_set_state(UI_STATE_SPEAKING);
        } else if (strcmp(type, "tts_stop") == 0) {
            ui_set_state(UI_STATE_IDLE);
        } else if (strcmp(type, "music_start") == 0) {
            cJSON *url_obj = cJSON_GetObjectItem(root, "url");
            cJSON *title_obj = cJSON_GetObjectItem(root, "title");
            cJSON *dur_obj = cJSON_GetObjectItem(root, "duration");
            const char *url = (url_obj && cJSON_IsString(url_obj)) ? url_obj->valuestring : "";
            const char *title = (title_obj && cJSON_IsString(title_obj)) ? title_obj->valuestring : "Music";
            int duration = (dur_obj && cJSON_IsNumber(dur_obj)) ? dur_obj->valueint : 180;

            ui_update_music_title(title);
            ui_set_state(UI_STATE_PLAYING);
            music_player_play(url, title, duration);
        } else if (strcmp(type, "music_lyric") == 0) {
            cJSON *line_obj = cJSON_GetObjectItem(root, "line");
            if (line_obj && cJSON_IsString(line_obj)) {
                ui_update_lyric(line_obj->valuestring);
            }
        } else if (strcmp(type, "music_stop") == 0) {
            music_player_stop();
            ui_set_state(UI_STATE_IDLE);
        } else if (strcmp(type, "mcp") == 0 || strcmp(type, "mcp_call") == 0) {
            ui_show_toast("Đang thực thi lệnh...");
            cJSON *payload_obj = cJSON_GetObjectItem(root, "payload");
            cJSON *target_json = payload_obj ? payload_obj : root;
            cJSON *res_rpc = mcp_handle_message(target_json);
            if (res_rpc) {
                cJSON *wrap = cJSON_CreateObject();
                cJSON_AddStringToObject(wrap, "type", "mcp_result");
                cJSON *res_inner = cJSON_DetachItemFromObject(res_rpc, "result");
                cJSON *err_inner = cJSON_DetachItemFromObject(res_rpc, "error");
                cJSON *id_inner  = cJSON_DetachItemFromObject(res_rpc, "id");
                cJSON_AddStringToObject(wrap, "jsonrpc", "2.0");
                if (res_inner) cJSON_AddItemToObject(wrap, "result", res_inner);
                if (err_inner) cJSON_AddItemToObject(wrap, "error", err_inner);
                if (id_inner)  cJSON_AddItemToObject(wrap, "id", id_inner);

                char *res_str = cJSON_PrintUnformatted(wrap);
                if (res_str) {
                    ws_send_text(res_str);
                    free(res_str);
                }
                cJSON_Delete(wrap);
                cJSON_Delete(res_rpc);
            }
        } else if (strcmp(type, "error") == 0) {
            cJSON *msg = cJSON_GetObjectItem(root, "message");
            ESP_LOGE(TAG, "Server reported error: %s", msg ? msg->valuestring : "unknown");
            ui_set_state(UI_STATE_ERROR);
        }
    }
    cJSON_Delete(root);
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket Connected to %s", s_current_url);
        s_is_connected = true;
        // Send official Xiaozhi hello handshake
        ws_send_text("{\"type\":\"hello\",\"version\":1,\"transport\":\"websocket\",\"features\":{\"mcp\":true},\"audio_params\":{\"format\":\"pcm\",\"sample_rate\":16000,\"channels\":1,\"frame_duration\":60}}");
        if (s_status_cb) s_status_cb(WS_STATUS_CONNECTED);
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WebSocket Disconnected. Reconnecting...");
        s_is_connected = false;
        ui_set_state(UI_STATE_ERROR);
        if (s_status_cb) s_status_cb(WS_STATUS_DISCONNECTED);
        break;

    case WEBSOCKET_EVENT_DATA:
        if (data->op_code == 0x01) { // Text frame
            handle_json_message(data->data_ptr, data->data_len);
        } else if (data->op_code == 0x02) { // Binary audio frame (TTS audio)
            // Play received PCM audio
            int samples = data->data_len / sizeof(int16_t);
            if (samples > 0) {
                audio_hal_play_pcm((const int16_t *)data->data_ptr, samples, 500);
                audio_analyzer_feed((const int16_t *)data->data_ptr, samples);
            }
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket Error occurred");
        s_is_connected = false;
        ui_set_state(UI_STATE_ERROR);
        break;
    }
}

esp_err_t ws_client_init(ws_event_callback_t cb)
{
    s_status_cb = cb;
    return ESP_OK;
}

esp_err_t ws_client_connect(const char *url)
{
    if (!url || strlen(url) == 0) return ESP_ERR_INVALID_ARG;
    strncpy(s_current_url, url, sizeof(s_current_url) - 1);

    if (s_client) {
        esp_websocket_client_stop(s_client);
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
    }

    esp_websocket_client_config_t ws_cfg = {
        .uri = s_current_url,
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms = 5000,
        .buffer_size = 4096,
    };

    ESP_LOGI(TAG, "Connecting to WebSocket Server: %s", s_current_url);
    s_client = esp_websocket_client_init(&ws_cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "Failed to initialize websocket client");
        return ESP_FAIL;
    }

    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)s_client);
    return esp_websocket_client_start(s_client);
}

esp_err_t ws_client_disconnect(void)
{
    if (s_client) {
        esp_websocket_client_stop(s_client);
        s_is_connected = false;
    }
    return ESP_OK;
}

bool ws_client_is_connected(void)
{
    return s_is_connected;
}

esp_err_t ws_send_text(const char *text)
{
    if (!s_client || !s_is_connected || !text) return ESP_ERR_INVALID_STATE;
    int ret = esp_websocket_client_send_text(s_client, text, strlen(text), pdMS_TO_TICKS(1000));
    return (ret >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t ws_send_audio(const uint8_t *data, size_t len)
{
    if (!s_client || !s_is_connected || !data || len == 0) return ESP_ERR_INVALID_STATE;
    int ret = esp_websocket_client_send_bin(s_client, (const char *)data, len, pdMS_TO_TICKS(500));
    return (ret >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t ws_send_listen_start(void)
{
    return ws_send_text("{\"type\":\"listen\",\"state\":\"start\"}");
}

esp_err_t ws_send_listen_stop(void)
{
    return ws_send_text("{\"type\":\"listen\",\"state\":\"stop\"}");
}

