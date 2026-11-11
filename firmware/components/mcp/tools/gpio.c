#include "gpio_tools.h"
#include "mcp_server.h"
#include <string.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "audio_hal.h"
#include "ui_manager.h"

static const char *TAG = "MCP_GPIO";

// Tool: gpio_write
static cJSON *tool_gpio_write_handler(const cJSON *args)
{
    cJSON *pin_obj = cJSON_GetObjectItem(args, "pin");
    cJSON *level_obj = cJSON_GetObjectItem(args, "level");

    if (!pin_obj || !level_obj || !cJSON_IsNumber(pin_obj) || !cJSON_IsNumber(level_obj)) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "error", "Invalid arguments: pin and level (number) required");
        return err;
    }

    int pin = pin_obj->valueint;
    int level = level_obj->valueint ? 1 : 0;

    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, level);

    ESP_LOGI(TAG, "Tool gpio_write: Pin %d set to %d", pin, level);

    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "status", "success");
    cJSON_AddNumberToObject(res, "pin", pin);
    cJSON_AddNumberToObject(res, "level", level);
    cJSON_AddStringToObject(res, "message", level ? "Đã bật thiết bị thành công" : "Đã tắt thiết bị thành công");
    return res;
}

// Tool: gpio_read
static cJSON *tool_gpio_read_handler(const cJSON *args)
{
    cJSON *pin_obj = cJSON_GetObjectItem(args, "pin");
    if (!pin_obj || !cJSON_IsNumber(pin_obj)) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "error", "Invalid arguments: pin (number) required");
        return err;
    }

    int pin = pin_obj->valueint;
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    int level = gpio_get_level(pin);

    ESP_LOGI(TAG, "Tool gpio_read: Pin %d is %d", pin, level);

    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "status", "success");
    cJSON_AddNumberToObject(res, "pin", pin);
    cJSON_AddNumberToObject(res, "level", level);
    return res;
}

// Tool: pwm_write
static cJSON *tool_pwm_write_handler(const cJSON *args)
{
    cJSON *pin_obj = cJSON_GetObjectItem(args, "pin");
    cJSON *duty_obj = cJSON_GetObjectItem(args, "duty");

    if (!pin_obj || !duty_obj || !cJSON_IsNumber(pin_obj) || !cJSON_IsNumber(duty_obj)) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "error", "Invalid arguments: pin and duty (0-255) required");
        return err;
    }

    int pin = pin_obj->valueint;
    int duty = duty_obj->valueint;
    if (duty < 0) duty = 0;
    if (duty > 255) duty = 255;

    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_8_BIT,
        .freq_hz          = 5000,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = pin,
        .duty           = duty,
        .hpoint         = 0
    };
    ledc_channel_config(&ledc_channel);

    ESP_LOGI(TAG, "Tool pwm_write: Pin %d duty %d", pin, duty);

    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "status", "success");
    cJSON_AddNumberToObject(res, "pin", pin);
    cJSON_AddNumberToObject(res, "duty", duty);
    return res;
}

// Tool: set_volume
static cJSON *tool_set_volume_handler(const cJSON *args)
{
    cJSON *vol_obj = cJSON_GetObjectItem(args, "volume");
    cJSON *delta_obj = cJSON_GetObjectItem(args, "delta");

    int cur_vol = audio_hal_get_volume();
    int new_vol = cur_vol;

    if (vol_obj && cJSON_IsNumber(vol_obj)) {
        new_vol = vol_obj->valueint;
    } else if (delta_obj && cJSON_IsNumber(delta_obj)) {
        new_vol = cur_vol + delta_obj->valueint;
    }

    if (new_vol < 0) new_vol = 0;
    if (new_vol > 100) new_vol = 100;

    audio_hal_set_volume(new_vol);

    ESP_LOGI(TAG, "Tool set_volume: Volume adjusted to %d%%", new_vol);

    cJSON *res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "status", "success");
    cJSON_AddNumberToObject(res, "volume", new_vol);
    char msg[64];
    snprintf(msg, sizeof(msg), "Đã điều chỉnh âm lượng loa thành %d%%", new_vol);
    cJSON_AddStringToObject(res, "message", msg);
    return res;
}

esp_err_t gpio_tools_register(void)
{
    mcp_tool_t t_write = {
        .name = "gpio_write",
        .description = "Bật hoặc tắt chân GPIO điều khiển thiết bị điện (đèn, quạt, rơ le)",
        .parameters_schema_json = "{\"type\":\"object\",\"properties\":{\"pin\":{\"type\":\"integer\",\"description\":\"Số chân GPIO (ví dụ: 48 là LED)\"},\"level\":{\"type\":\"integer\",\"description\":\"1 để bật, 0 để tắt\"}},\"required\":[\"pin\",\"level\"]}",
        .handler = tool_gpio_write_handler
    };
    mcp_register_tool(&t_write);

    mcp_tool_t t_vol = {
        .name = "set_volume",
        .description = "Điều chỉnh mức âm lượng của loa (từ 0 đến 100%), hoặc tăng / giảm âm lượng theo mức delta.",
        .parameters_schema_json = "{\"type\":\"object\",\"properties\":{\"volume\":{\"type\":\"integer\",\"description\":\"Mức âm lượng từ 0 đến 100\"},\"delta\":{\"type\":\"integer\",\"description\":\"Mức tăng (+) hoặc giảm (-) âm lượng\"}}}",
        .handler = tool_set_volume_handler
    };
    mcp_register_tool(&t_vol);

    mcp_tool_t t_read = {
        .name = "gpio_read",
        .description = "Đọc trạng thái logic (0 hoặc 1) của một chân GPIO hoặc cảm biến",
        .parameters_schema_json = "{\"type\":\"object\",\"properties\":{\"pin\":{\"type\":\"integer\",\"description\":\"Số chân GPIO\"}},\"required\":[\"pin\"]}",
        .handler = tool_gpio_read_handler
    };
    mcp_register_tool(&t_read);

    mcp_tool_t t_pwm = {
        .name = "pwm_write",
        .description = "Điều khiển độ sáng đèn LED hoặc tốc độ quạt bằng PWM",
        .parameters_schema_json = "{\"type\":\"object\",\"properties\":{\"pin\":{\"type\":\"integer\",\"description\":\"Số chân GPIO\"},\"duty\":{\"type\":\"integer\",\"description\":\"Mức duty từ 0 (tắt) đến 255 (sáng nhất)\"}},\"required\":[\"pin\",\"duty\"]}",
        .handler = tool_pwm_write_handler
    };
    mcp_register_tool(&t_pwm);

    ESP_LOGI(TAG, "Registered GPIO MCP Tools: gpio_write, gpio_read, pwm_write");
    return ESP_OK;
}

