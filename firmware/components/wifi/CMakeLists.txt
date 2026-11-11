idf_component_register(SRCS "wifi_manager.c" "web_server.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi esp_netif esp_http_server esp_event nvs_storage json lwip driver)

