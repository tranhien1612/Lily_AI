#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WS_STATUS_DISCONNECTED,
    WS_STATUS_CONNECTING,
    WS_STATUS_CONNECTED
} ws_status_t;

typedef void (*ws_event_callback_t)(ws_status_t status);

esp_err_t ws_client_init(ws_event_callback_t cb);
esp_err_t ws_client_connect(const char *url);
esp_err_t ws_client_disconnect(void);
bool ws_client_is_connected(void);

// Send text control frame (JSON)
esp_err_t ws_send_text(const char *text);

// Send binary audio frame
esp_err_t ws_send_audio(const uint8_t *data, size_t len);

// Send listen_start / listen_stop
esp_err_t ws_send_listen_start(void);
esp_err_t ws_send_listen_stop(void);

#ifdef __cplusplus
}
#endif

