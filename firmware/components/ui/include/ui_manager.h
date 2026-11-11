#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_STATE_BOOTING,
    UI_STATE_PROVISIONING,
    UI_STATE_IDLE,
    UI_STATE_LISTENING,
    UI_STATE_THINKING,
    UI_STATE_SPEAKING,
    UI_STATE_PLAYING,
    UI_STATE_MCP_ACTION,
    UI_STATE_ERROR
} ui_state_t;

typedef struct {
    char asr_text[256];
    char response_text[1024];
    char music_title[64];
    char lyric_line[128];
    char toast_text[64];
    float spectrum[24];
    uint8_t battery_pct;
    bool wifi_connected;
} ui_data_t;

esp_err_t ui_manager_init(void);
void ui_set_state(ui_state_t state);
ui_state_t ui_get_state(void);

void ui_update_asr_text(const char *text);
void ui_update_response_text(const char *text);
void ui_update_music_title(const char *title);
void ui_update_lyric(const char *line);
void ui_update_spectrum(const float *bins);
void ui_set_wifi_status(bool connected);
void ui_show_toast(const char *msg);

void ui_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

