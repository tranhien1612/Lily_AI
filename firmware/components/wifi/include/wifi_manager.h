#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_MGR_STATE_IDLE = 0,
    WIFI_MGR_STATE_CONNECTING,
    WIFI_MGR_STATE_CONNECTED,
    WIFI_MGR_STATE_DISCONNECTED,
    WIFI_MGR_STATE_PROVISIONING
} wifi_mgr_state_t;

typedef void (*wifi_state_cb_t)(wifi_mgr_state_t state);

esp_err_t wifi_manager_init(wifi_state_cb_t callback);
esp_err_t wifi_manager_start(void);
esp_err_t wifi_manager_start_ap(void);
esp_err_t wifi_manager_connect_sta(const char *ssid, const char *password);
bool wifi_manager_is_connected(void);
wifi_mgr_state_t wifi_manager_get_state(void);
void wifi_manager_trigger_provisioning(void);

#ifdef __cplusplus
}
#endif

