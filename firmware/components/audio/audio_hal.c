#include "audio_hal.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_storage.h"
#include "../../main/board_pins.h"

static const char *TAG = "AUDIO_HAL";

static i2s_chan_handle_t tx_chan = NULL;
static i2s_chan_handle_t rx_chan = NULL;
static bool s_recording = false;
static int s_volume = 70; // 0 - 100%

esp_err_t audio_hal_set_volume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    s_volume = volume;
    nvs_storage_set_volume(s_volume);
    ESP_LOGI(TAG, "Audio volume updated to %d%%", s_volume);
    return ESP_OK;
}

int audio_hal_get_volume(void)
{
    return s_volume;
}

esp_err_t audio_hal_set_amp(bool enable)
{
    gpio_set_level(AUDIO_I2S_SPK_EN, enable ? 1 : 0);
    ESP_LOGD(TAG, "Audio amp set to %s", enable ? "ENABLED" : "DISABLED");
    return ESP_OK;
}

esp_err_t audio_hal_init(void)
{
    ESP_LOGI(TAG, "Initializing Audio HAL for ESP32-S3 XH-S3E...");

    // 1. Configure Amp Enable pin
    gpio_config_t amp_conf = {
        .pin_bit_mask = (1ULL << AUDIO_I2S_SPK_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&amp_conf);
    audio_hal_set_amp(false); // start muted

    // 2. Configure I2S TX (Speaker on I2S_NUM_0)
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    tx_chan_cfg.dma_desc_num = 8;
    tx_chan_cfg.dma_frame_num = 480;
    tx_chan_cfg.auto_clear = true;

    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &tx_chan, NULL));

    i2s_std_config_t tx_std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_I2S_SPK_BCLK,
            .ws   = AUDIO_I2S_SPK_LRCK,
            .dout = AUDIO_I2S_SPK_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &tx_std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));

    // 3. Configure I2S RX (Microphone on I2S_NUM_1)
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    rx_chan_cfg.dma_desc_num = 6;
    rx_chan_cfg.dma_frame_num = 240;

    ESP_ERROR_CHECK(i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan));

    i2s_std_config_t rx_std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AUDIO_I2S_MIC_SCK,
            .ws   = AUDIO_I2S_MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = AUDIO_I2S_MIC_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    // Microphone standard slot configuration
    rx_std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &rx_std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));

    audio_hal_set_amp(true);

    int saved_vol = 70;
    if (nvs_storage_get_volume(&saved_vol) == ESP_OK) {
        s_volume = saved_vol;
    }
    ESP_LOGI(TAG, "Audio HAL initialized successfully (Volume=%d%%, TX on I2S0, RX on I2S1)", s_volume);
    return ESP_OK;
}

esp_err_t audio_hal_start_record(void)
{
    s_recording = true;
    return ESP_OK;
}

esp_err_t audio_hal_stop_record(void)
{
    s_recording = false;
    return ESP_OK;
}

int audio_hal_read_pcm(int16_t *buffer, size_t samples, uint32_t timeout_ms)
{
    if (!rx_chan || !buffer || samples == 0) return -1;
    size_t bytes_read = 0;
    size_t bytes_to_read = samples * sizeof(int16_t);

    esp_err_t ret = i2s_channel_read(rx_chan, buffer, bytes_to_read, &bytes_read, pdMS_TO_TICKS(timeout_ms));
    if (ret != ESP_OK) {
        return -1;
    }
    return (int)(bytes_read / sizeof(int16_t));
}

int audio_hal_play_pcm(const int16_t *buffer, size_t samples, uint32_t timeout_ms)
{
    if (!tx_chan || !buffer || samples == 0) return -1;
    if (s_volume == 0) return (int)samples; // Muted

    size_t bytes_written = 0;
    if (s_volume >= 100) {
        size_t bytes_to_write = samples * sizeof(int16_t);
        esp_err_t ret = i2s_channel_write(tx_chan, buffer, bytes_to_write, &bytes_written, pdMS_TO_TICKS(timeout_ms));
        if (ret != ESP_OK) return -1;
        return (int)(bytes_written / sizeof(int16_t));
    }

    // Scale audio volume digitally in batches
    #define SCALE_BATCH_SIZE 256
    int16_t scaled_buf[SCALE_BATCH_SIZE];
    size_t processed = 0;

    while (processed < samples) {
        size_t batch = samples - processed;
        if (batch > SCALE_BATCH_SIZE) batch = SCALE_BATCH_SIZE;

        for (size_t i = 0; i < batch; i++) {
            int32_t val = ((int32_t)buffer[processed + i] * s_volume) / 100;
            if (val > 32767) val = 32767;
            else if (val < -32768) val = -32768;
            scaled_buf[i] = (int16_t)val;
        }

        size_t bw = 0;
        esp_err_t ret = i2s_channel_write(tx_chan, scaled_buf, batch * sizeof(int16_t), &bw, pdMS_TO_TICKS(timeout_ms));
        if (ret != ESP_OK) return -1;
        processed += batch;
    }
    return (int)samples;
}

void audio_hal_play_beep(uint32_t freq_hz, uint32_t duration_ms)
{
    size_t total_samples = (AUDIO_SAMPLE_RATE_HZ * duration_ms) / 1000;
    int16_t *tone_buf = (int16_t *)malloc(AUDIO_FRAME_SAMPLES * sizeof(int16_t));
    if (!tone_buf) return;

    audio_hal_set_amp(true);

    float phase = 0.0f;
    float phase_step = (2.0f * M_PI * freq_hz) / (float)AUDIO_SAMPLE_RATE_HZ;
    size_t generated = 0;

    while (generated < total_samples) {
        size_t chunk = (total_samples - generated > AUDIO_FRAME_SAMPLES) ? AUDIO_FRAME_SAMPLES : (total_samples - generated);
        for (size_t i = 0; i < chunk; i++) {
            tone_buf[i] = (int16_t)(sinf(phase) * 12000.0f);
            phase += phase_step;
            if (phase >= 2.0f * M_PI) phase -= 2.0f * M_PI;
        }
        audio_hal_play_pcm(tone_buf, chunk, 100);
        generated += chunk;
    }

    free(tone_buf);
}

