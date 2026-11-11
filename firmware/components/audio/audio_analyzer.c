#include "audio_analyzer.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "AUDIO_ANALYZER";

#define FFT_SIZE 256
static float s_real[FFT_SIZE];
static float s_imag[FFT_SIZE];
static float s_window[FFT_SIZE];
static float s_bins[SPECTRUM_BINS_COUNT];
static SemaphoreHandle_t s_mutex = NULL;

// Radix-2 Cooley-Tukey in-place FFT
static void compute_fft(float *real, float *imag, int n)
{
    // Bit-reversal permutation
    int j = 0;
    for (int i = 0; i < n - 1; i++) {
        if (i < j) {
            float tr = real[i]; real[i] = real[j]; real[j] = tr;
            float ti = imag[i]; imag[i] = imag[j]; imag[j] = ti;
        }
        int k = n >> 1;
        while (k <= j) {
            j -= k;
            k >>= 1;
        }
        j += k;
    }

    // Cooley-Tukey butterfly
    for (int len = 2; len <= n; len <<= 1) {
        float angle = -2.0f * (float)M_PI / (float)len;
        float wlen_r = cosf(angle);
        float wlen_i = sinf(angle);
        for (int i = 0; i < n; i += len) {
            float w_r = 1.0f;
            float w_i = 0.0f;
            for (int k = 0; k < len / 2; k++) {
                float u_r = real[i + k];
                float u_i = imag[i + k];
                float v_r = real[i + k + len / 2] * w_r - imag[i + k + len / 2] * w_i;
                float v_i = real[i + k + len / 2] * w_i + imag[i + k + len / 2] * w_r;
                real[i + k] = u_r + v_r;
                imag[i + k] = u_i + v_i;
                real[i + k + len / 2] = u_r - v_r;
                imag[i + k + len / 2] = u_i - v_i;
                float next_w_r = w_r * wlen_r - w_i * wlen_i;
                float next_w_i = w_r * wlen_i + w_i * wlen_r;
                w_r = next_w_r;
                w_i = next_w_i;
            }
        }
    }
}

void audio_analyzer_init(void)
{
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }
    // Pre-calculate Hann window
    for (int i = 0; i < FFT_SIZE; i++) {
        s_window[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / (float)(FFT_SIZE - 1)));
    }
    memset(s_bins, 0, sizeof(s_bins));
    ESP_LOGI(TAG, "Audio Analyzer initialized (24 log bins, 100Hz - 8kHz)");
}

void audio_analyzer_feed(const int16_t *samples, size_t count)
{
    if (!samples || count < FFT_SIZE || !s_mutex) return;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return;
    }

    // Apply window and copy samples to real buffer
    for (int i = 0; i < FFT_SIZE; i++) {
        s_real[i] = ((float)samples[i] / 32768.0f) * s_window[i];
        s_imag[i] = 0.0f;
    }

    compute_fft(s_real, s_imag, FFT_SIZE);

    // Map 128 positive FFT bins to 24 logarithmic bins
    // Sampling rate: 16kHz -> Nyquist: 8kHz. Frequency resolution = 16000 / 256 = 62.5 Hz per FFT bin.
    // 24 bins from 100Hz to 8000Hz (logarithmic progression)
    float min_freq = 100.0f;
    float max_freq = 7900.0f;
    float freq_ratio = powf(max_freq / min_freq, 1.0f / (float)SPECTRUM_BINS_COUNT);

    float current_low = min_freq;
    for (int b = 0; b < SPECTRUM_BINS_COUNT; b++) {
        float current_high = current_low * freq_ratio;
        int k_start = (int)(current_low / 62.5f);
        int k_end = (int)(current_high / 62.5f);
        if (k_start < 1) k_start = 1;
        if (k_end >= (FFT_SIZE / 2)) k_end = (FFT_SIZE / 2) - 1;
        if (k_end < k_start) k_end = k_start;

        float magnitude_sum = 0.0f;
        int bin_count = 0;
        for (int k = k_start; k <= k_end; k++) {
            float mag = sqrtf(s_real[k] * s_real[k] + s_imag[k] * s_imag[k]);
            magnitude_sum += mag;
            bin_count++;
        }
        float avg_mag = (bin_count > 0) ? (magnitude_sum / bin_count) : 0.0f;
        // Normalize with logarithmic compression
        float norm_val = log10f(1.0f + 9.0f * (avg_mag * 5.0f));
        if (norm_val > 1.0f) norm_val = 1.0f;
        if (norm_val < 0.0f) norm_val = 0.0f;

        // Smooth decay (exponential moving average)
        s_bins[b] = s_bins[b] * 0.4f + norm_val * 0.6f;
        current_low = current_high;
    }

    xSemaphoreGive(s_mutex);
}

void audio_analyzer_get_spectrum(float bins[SPECTRUM_BINS_COUNT])
{
    if (!s_mutex || !bins) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(bins, s_bins, sizeof(s_bins));
        xSemaphoreGive(s_mutex);
    }
}

