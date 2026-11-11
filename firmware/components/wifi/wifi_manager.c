#include "wifi_manager.h"
#include <string.h>
#include <time.h>
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs_storage.h"
#include "web_server.h"
#include "driver/gpio.h"

static const char *TAG = "WIFI_MGR";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_RETRY_COUNT    5

static EventGroupHandle_t s_wifi_event_group;
static wifi_mgr_state_t s_current_state = WIFI_MGR_STATE_IDLE;
static wifi_state_cb_t s_state_callback = NULL;
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static int s_retry_count = 0;

static void set_state(wifi_mgr_state_t state)
{
    s_current_state = state;
    if (s_state_callback) {
        s_state_callback(state);
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA start, attempting connection...");
        set_state(WIFI_MGR_STATE_CONNECTING);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < MAX_RETRY_COUNT) {
            s_retry_count++;
            uint32_t delay_ms = (1 << s_retry_count) * 1000;
            if (delay_ms > 10000) delay_ms = 10000;
            ESP_LOGW(TAG, "Connect failed. Retrying in %lu ms (Attempt %d/%d)...", delay_ms, s_retry_count, MAX_RETRY_COUNT);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
            set_state(WIFI_MGR_STATE_CONNECTING);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "Failed to connect to STA after %d retries. Falling back to AP mode.", MAX_RETRY_COUNT);
            set_state(WIFI_MGR_STATE_DISCONNECTED);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            wifi_manager_start_ap();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected to WiFi! Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        set_state(WIFI_MGR_STATE_CONNECTED);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        // Initialize SNTP with Vietnam timezone
        setenv("TZ", "ICT-7", 1);
        tzset();
        if (!esp_sntp_enabled()) {
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "pool.ntp.org");
            esp_sntp_setservername(1, "time.google.com");
            esp_sntp_init();
            ESP_LOGI(TAG, "SNTP initialized");
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "Station joined SoftAP");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGI(TAG, "Station left SoftAP");
    }
}

esp_err_t wifi_manager_init(wifi_state_cb_t callback)
{
    s_state_callback = callback;
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    return ESP_OK;
}

esp_err_t wifi_manager_start_ap(void)
{
    ESP_LOGI(TAG, "Starting SoftAP: Xiaozhi-S3-Config (192.168.4.1)");
    set_state(WIFI_MGR_STATE_PROVISIONING);

    wifi_config_t ap_config = {
        .ap = {
            .ssid = "Xiaozhi-S3-Config",
            .ssid_len = strlen("Xiaozhi-S3-Config"),
            .channel = 1,
            .password = "",
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Start captive portal web server
    web_server_start();
    return ESP_OK;
}

esp_err_t wifi_manager_connect_sta(const char *ssid, const char *password)
{
    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password && strlen(password) > 0) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }
    wifi_config.sta.threshold.authmode = (password && strlen(password) > 0) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_LOGI(TAG, "Configuring STA to connect to '%s'...", ssid);
    s_retry_count = 0;
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_err_t err = esp_wifi_start();
    if (err == ESP_ERR_WIFI_STATE) {
        // WiFi driver already started, trigger STA connect directly
        esp_wifi_connect();
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start error: %d", err);
        return err;
    }

    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    char ssid[64] = {0};
    char password[64] = {0};

    esp_err_t err = nvs_storage_get_wifi(ssid, sizeof(ssid), password, sizeof(password));
    if (err == ESP_OK && strlen(ssid) > 0) {
        ESP_LOGI(TAG, "Found stored WiFi credentials for '%s'. Connecting...", ssid);
        return wifi_manager_connect_sta(ssid, password);
    } else {
        ESP_LOGW(TAG, "No valid WiFi credentials found in NVS. Entering Provisioning mode.");
        return wifi_manager_start_ap();
    }
}

bool wifi_manager_is_connected(void)
{
    return s_current_state == WIFI_MGR_STATE_CONNECTED;
}

wifi_mgr_state_t wifi_manager_get_state(void)
{
    return s_current_state;
}

void wifi_manager_trigger_provisioning(void)
{
    ESP_LOGI(TAG, "Manual provisioning triggered by BOOT button");
    wifi_manager_start_ap();
}

