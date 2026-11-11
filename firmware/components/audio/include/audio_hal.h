#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_SAMPLE_RATE_HZ     16000
#define AUDIO_BITS_PER_SAMPLE    16
#define AUDIO_CHANNELS           1
#define AUDIO_FRAME_SAMPLES      320     // 20ms frame at 16kHz
#define AUDIO_BUFFER_SIZE_PSRAM  (128 * 1024) // 128KB in PSRAM for smooth streaming

esp_err_t audio_hal_init(void);
esp_err_t audio_hal_set_amp(bool enable);
esp_err_t audio_hal_start_record(void);
esp_err_t audio_hal_stop_record(void);

// Read raw PCM 16kHz 16-bit Mono from microphone
int audio_hal_read_pcm(int16_t *buffer, size_t samples, uint32_t timeout_ms);

// Write raw PCM 16kHz 16-bit Mono to speaker (scaled by volume)
int audio_hal_play_pcm(const int16_t *buffer, size_t samples, uint32_t timeout_ms);

// Volume control (0 - 100%)
esp_err_t audio_hal_set_volume(int volume);
int audio_hal_get_volume(void);

// Play a short notification beep (e.g., wake word acknowledgment)
void audio_hal_play_beep(uint32_t freq_hz, uint32_t duration_ms);

#ifdef __cplusplus
}
#endif

