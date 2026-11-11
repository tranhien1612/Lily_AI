#include "web_server.h"
#include "wifi_manager.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "nvs_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");

// Embedded fallback web assets so provisioning works out-of-the-box
static const char FALLBACK_INDEX_HTML[] = 
"<!DOCTYPE html><html lang=\"vi\"><head><meta charset=\"UTF-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">"
"<title>Lily AI Config</title><style>"
"body{background:#0f172a;color:#fff;font-family:sans-serif;display:flex;justify-content:center;padding:20px;}"
".c{background:#1e293b;padding:24px;border-radius:12px;max-width:400px;width:100%;box-shadow:0 4px 6px rgba(0,0,0,0.3);}"
"h2{color:#38bdf8;text-align:center;margin-bottom:15px;}"
"input,select,button{width:100%;padding:10px;margin-top:6px;margin-bottom:14px;border-radius:6px;border:1px solid #475569;background:#0f172a;color:#fff;box-sizing:border-box;}"
"button{background:#2563eb;color:#fff;font-weight:bold;cursor:pointer;border:none;margin-top:10px;}"
"#res{margin-top:12px;padding:8px;border-radius:6px;display:none;font-size:13px;}"
"</style></head><body><div class=\"c\">"
"<h2>🤖 Cấu hình Lily AI</h2>"
"<label>Danh sách WiFi:</label>"
"<select id=\"w\" onchange=\"document.getElementById('s').value=this.value\"><option>Đang quét...</option></select>"
"<form onsubmit=\"save(event)\">"
"<label>Tên WiFi (SSID):</label><input id=\"s\" required placeholder=\"Tên WiFi\">"
"<label>Mật khẩu WiFi:</label><input id=\"p\" type=\"password\" placeholder=\"Mật khẩu\">"
"<label>WebSocket Server URL:</label><input id=\"u\" required value=\"ws://192.168.1.102:8000/ws\">"
"<button type=\"submit\" id=\"btn\">💾 Lưu & Kết nối</button>"
"</form><div id=\"res\"></div></div>"
"<script>"
"async function scan(){try{let r=await fetch('/api/scan');let d=await r.json();let s=document.getElementById('w');s.innerHTML='<option value=\"\">-- Chọn WiFi --</option>';d.forEach(n=>{let o=document.createElement('option');o.value=n.ssid;o.text=n.ssid+' ('+n.rssi+'dBm)';s.appendChild(o);});}catch(e){}}"
"async function save(e){e.preventDefault();let btn=document.getElementById('btn');btn.disabled=true;btn.innerText='Đang lưu...';"
"let r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:document.getElementById('s').value,password:document.getElementById('p').value,ws_url:document.getElementById('u').value})});"
"let res=await r.json();let d=document.getElementById('res');d.style.display='block';"
"if(res.status==='ok'){d.style.background='#14532d';d.innerText='Đã lưu thành công! Đang kết nối lại...';}else{d.style.background='#7f1d1d';d.innerText='Lỗi: '+res.message;btn.disabled=false;}}"
"window.onload=scan;"
"</script></body></html>";

static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, FALLBACK_INDEX_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t captive_redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t scan_get_handler(httpd_req_t *req)
{
    wifi_scan_config_t scan_config = {
        .ssid = 0,
        .bssid = 0,
        .channel = 0,
        .show_hidden = false
    };

    esp_wifi_scan_start(&scan_config, true);

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count > 20) ap_count = 20;

    wifi_ap_record_t *ap_records = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
    cJSON *root = cJSON_CreateArray();

    if (ap_records && ap_count > 0) {
        esp_wifi_scan_get_ap_records(&ap_count, ap_records);
        for (int i = 0; i < ap_count; i++) {
            if (strlen((char *)ap_records[i].ssid) == 0) continue;
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "ssid", (char *)ap_records[i].ssid);
            cJSON_AddNumberToObject(item, "rssi", ap_records[i].rssi);
            cJSON_AddNumberToObject(item, "auth", ap_records[i].authmode);
            cJSON_AddItemToArray(root, item);
        }
        free(ap_records);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str ? json_str : "[]", HTTPD_RESP_USE_STRLEN);
    if (json_str) free(json_str);

    return ESP_OK;
}

static void apply_config_task(void *param)
{
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "Applying new WiFi config directly without restart...");

    char ssid[64] = {0};
    char pass[64] = {0};
    if (nvs_storage_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass)) == ESP_OK && strlen(ssid) > 0) {
        web_server_stop();
        wifi_manager_connect_sta(ssid, pass);
    }
    vTaskDelete(NULL);
}

static esp_err_t config_post_handler(httpd_req_t *req)
{
    char buf[256];
    int ret, remaining = req->content_len;
    if (remaining >= sizeof(buf)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *ssid = cJSON_GetObjectItem(json, "ssid");
    cJSON *pass = cJSON_GetObjectItem(json, "password");
    cJSON *ws   = cJSON_GetObjectItem(json, "ws_url");

    if (!ssid || !cJSON_IsString(ssid) || !ws || !cJSON_IsString(ws)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ssid or ws_url");
        return ESP_FAIL;
    }

    const char *p = (pass && cJSON_IsString(pass)) ? pass->valuestring : "";
    nvs_storage_set_wifi(ssid->valuestring, p);
    nvs_storage_set_ws_url(ws->valuestring);
    cJSON_Delete(json);

    ESP_LOGI(TAG, "Config saved: SSID=%s, WS_URL=%s", ssid->valuestring, ws->valuestring);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);

    // Apply new WiFi config directly without restarting the MCU
    xTaskCreate(apply_config_task, "apply_cfg_task", 4096, NULL, 5, NULL);
    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    if (server != NULL) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_index = { .uri = "/", .method = HTTP_GET, .handler = index_get_handler };
        httpd_register_uri_handler(server, &uri_index);

        httpd_uri_t uri_scan = { .uri = "/api/scan", .method = HTTP_GET, .handler = scan_get_handler };
        httpd_register_uri_handler(server, &uri_scan);

        httpd_uri_t uri_config = { .uri = "/api/config", .method = HTTP_POST, .handler = config_post_handler };
        httpd_register_uri_handler(server, &uri_config);

        // Captive portal triggers
        httpd_uri_t cp1 = { .uri = "/generate_204", .method = HTTP_GET, .handler = captive_redirect_handler };
        httpd_uri_t cp2 = { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = captive_redirect_handler };
        httpd_uri_t cp3 = { .uri = "/ncsi.txt", .method = HTTP_GET, .handler = captive_redirect_handler };
        httpd_register_uri_handler(server, &cp1);
        httpd_register_uri_handler(server, &cp2);
        httpd_register_uri_handler(server, &cp3);

        ESP_LOGI(TAG, "HTTP Web Provisioning Server started");
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t web_server_stop(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "HTTP Web Provisioning Server stopped");
    }
    return ESP_OK;
}

