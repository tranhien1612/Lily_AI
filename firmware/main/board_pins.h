#pragma once

#include "driver/gpio.h"
#include "driver/i2c.h"

/**
 * @brief Board pinout for SparkleIoT XH-S3E N16R8 (Purple Xiaozhi Board)
 * ESP32-S3 with 16MB Flash and 8MB Octal PSRAM
 */

// Speaker I2S (MAX98357A / NS4168)
#define AUDIO_I2S_SPK_BCLK      GPIO_NUM_15
#define AUDIO_I2S_SPK_LRCK      GPIO_NUM_16
#define AUDIO_I2S_SPK_DOUT      GPIO_NUM_7
#define AUDIO_I2S_SPK_EN        GPIO_NUM_17  // Active HIGH amplifier enable

// Microphone I2S (INMP441 / MEMS)
#define AUDIO_I2S_MIC_SCK       GPIO_NUM_5
#define AUDIO_I2S_MIC_WS        GPIO_NUM_4
#define AUDIO_I2S_MIC_DIN       GPIO_NUM_6

// Display OLED SSD1306 (I2C)
#define DISPLAY_I2C_PORT        I2C_NUM_0
#define DISPLAY_I2C_SDA         GPIO_NUM_41
#define DISPLAY_I2C_SCL         GPIO_NUM_42
#define DISPLAY_I2C_ADDR        0x3C
#define DISPLAY_WIDTH           128
#define DISPLAY_HEIGHT          64

// Buttons & Indicators
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_40  // S3 button for Vol+
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_39  // S3 button for Vol-
#define BUILTIN_LED_GPIO        GPIO_NUM_48

