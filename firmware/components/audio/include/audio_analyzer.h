#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPECTRUM_BINS_COUNT 24

void audio_analyzer_init(void);

/**
 * @brief Feed PCM samples to the analyzer for FFT computation
 * @param samples 16-bit PCM samples
 * @param count Number of samples
 */
void audio_analyzer_feed(const int16_t *samples, size_t count);

/**
 * @brief Get normalized spectrum bins (0.0 to 1.0)
 * @param bins Array of size SPECTRUM_BINS_COUNT (24)
 */
void audio_analyzer_get_spectrum(float bins[SPECTRUM_BINS_COUNT]);

#ifdef __cplusplus
}
#endif

