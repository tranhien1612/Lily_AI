idf_component_register(SRCS "app_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_storage wifi audio wakeword ws mcp display ui music driver esp_timer)

