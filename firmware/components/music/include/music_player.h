#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t music_player_init(void);
esp_err_t music_player_play(const char *url, const char *title, int duration);
esp_err_t music_player_stop(void);
bool music_player_is_playing(void);

#ifdef __cplusplus
}
#endif

