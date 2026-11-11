#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t nvs_storage_init(void);

esp_err_t nvs_storage_get_wifi(char *ssid, size_t max_ssid_len, char *password, size_t max_pass_len);
esp_err_t nvs_storage_set_wifi(const char *ssid, const char *password);

esp_err_t nvs_storage_get_ws_url(char *url, size_t max_url_len);
esp_err_t nvs_storage_set_ws_url(const char *url);

esp_err_t nvs_storage_get_volume(int *volume);
esp_err_t nvs_storage_set_volume(int volume);

esp_err_t nvs_storage_clear_all(void);

#ifdef __cplusplus
}
#endif

