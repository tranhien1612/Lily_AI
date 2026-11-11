#include "nvs_storage.h"
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "NVS_STORAGE";
#define NVS_NAMESPACE "xiaozhi_cfg"

esp_err_t nvs_storage_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing flash to re-initialize NVS");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t nvs_storage_get_wifi(char *ssid, size_t max_ssid_len, char *password, size_t max_pass_len)
{
    if (!ssid || !password || max_ssid_len == 0 || max_pass_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t s_len = max_ssid_len;
    err = nvs_get_str(handle, "wifi_ssid", ssid, &s_len);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    size_t p_len = max_pass_len;
    err = nvs_get_str(handle, "wifi_pass", password, &p_len);
    if (err != ESP_OK) {
        password[0] = '\0'; // password might be empty for open AP
    }

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t nvs_storage_set_wifi(const char *ssid, const char *password)
{
    if (!ssid) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(handle, "wifi_ssid", ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "wifi_pass", password ? password : "");
    }

    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t nvs_storage_get_ws_url(char *url, size_t max_url_len)
{
    if (!url || max_url_len == 0) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    size_t u_len = max_url_len;
    err = nvs_get_str(handle, "ws_url", url, &u_len);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_storage_set_ws_url(const char *url)
{
    if (!url) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(handle, "ws_url", url);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t nvs_storage_get_volume(int *volume)
{
    if (!volume) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        *volume = 70; // Default 70%
        return err;
    }
    int32_t val = 70;
    err = nvs_get_i32(handle, "volume", &val);
    nvs_close(handle);
    *volume = (err == ESP_OK) ? (int)val : 70;
    return err;
}

esp_err_t nvs_storage_set_volume(int volume)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_i32(handle, "volume", (int32_t)volume);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t nvs_storage_clear_all(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

