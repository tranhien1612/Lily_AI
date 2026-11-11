#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*wakeword_detected_cb_t)(void);

esp_err_t wakeword_init(wakeword_detected_cb_t callback);
esp_err_t wakeword_start(void);
esp_err_t wakeword_stop(void);
void wakeword_feed(const int16_t *samples, size_t count);
void wakeword_trigger_manual(void);

#ifdef __cplusplus
}
#endif

