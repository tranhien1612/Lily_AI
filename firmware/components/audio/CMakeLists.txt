idf_component_register(SRCS "audio_hal.c" "audio_analyzer.c"
                        INCLUDE_DIRS "include"
                        REQUIRES driver esp_ringbuf esp_timer nvs_storage)
