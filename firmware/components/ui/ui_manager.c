#include "ui_manager.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "display.h"
#include "audio_hal.h"
#include "audio_analyzer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

static const char *TAG = "UI_MGR";

static ui_state_t s_current_state = UI_STATE_BOOTING;
static ui_state_t s_prev_state = UI_STATE_IDLE;
static ui_data_t s_ui_data;
static SemaphoreHandle_t s_ui_mutex = NULL;
static uint32_t s_frame_count = 0;
static int64_t s_toast_start_us = 0;

esp_err_t ui_manager_init(void)
{
    if (!s_ui_mutex) {
        s_ui_mutex = xSemaphoreCreateMutex();
    }
    memset(&s_ui_data, 0, sizeof(s_ui_data));
    s_ui_data.battery_pct = 95;
    s_ui_data.wifi_connected = false;
    strcpy(s_ui_data.response_text, "San sang lang nghe ban...");

    display_init();
    ESP_LOGI(TAG, "UI Manager initialized (20Hz State Machine)");
    return ESP_OK;
}

void ui_set_state(ui_state_t state)
{
    if (xSemaphoreTake(s_ui_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (s_current_state != state && s_current_state != UI_STATE_MCP_ACTION) {
            s_prev_state = s_current_state;
        }
        s_current_state = state;
        xSemaphoreGive(s_ui_mutex);
    }
}

ui_state_t ui_get_state(void)
{
    return s_current_state;
}

void ui_update_asr_text(const char *text)
{
    if (!text) return;
    if (xSemaphoreTake(s_ui_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        strncpy(s_ui_data.asr_text, text, sizeof(s_ui_data.asr_text) - 1);
        xSemaphoreGive(s_ui_mutex);
    }
}

void ui_update_response_text(const char *text)
{
    if (!text) return;
    if (xSemaphoreTake(s_ui_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        strncpy(s_ui_data.response_text, text, sizeof(s_ui_data.response_text) - 1);
        xSemaphoreGive(s_ui_mutex);
    }
}

void ui_update_music_title(const char *title)
{
    if (!title) return;
    if (xSemaphoreTake(s_ui_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        strncpy(s_ui_data.music_title, title, sizeof(s_ui_data.music_title) - 1);
        xSemaphoreGive(s_ui_mutex);
    }
}

void ui_update_lyric(const char *line)
{
    if (!line) return;
    if (xSemaphoreTake(s_ui_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        strncpy(s_ui_data.lyric_line, line, sizeof(s_ui_data.lyric_line) - 1);
        xSemaphoreGive(s_ui_mutex);
    }
}

void ui_update_spectrum(const float *bins)
{
    if (!bins) return;
    if (xSemaphoreTake(s_ui_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        memcpy(s_ui_data.spectrum, bins, sizeof(s_ui_data.spectrum));
        xSemaphoreGive(s_ui_mutex);
    }
}

void ui_set_wifi_status(bool connected)
{
    if (xSemaphoreTake(s_ui_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_ui_data.wifi_connected = connected;
        xSemaphoreGive(s_ui_mutex);
    }
}

void ui_show_toast(const char *msg)
{
    if (!msg) return;
    if (xSemaphoreTake(s_ui_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        strncpy(s_ui_data.toast_text, msg, sizeof(s_ui_data.toast_text) - 1);
        s_toast_start_us = esp_timer_get_time();
        if (s_current_state != UI_STATE_MCP_ACTION) {
            s_prev_state = s_current_state;
        }
        s_current_state = UI_STATE_MCP_ACTION;
        xSemaphoreGive(s_ui_mutex);
    }
}

// -------------------------------------------------------------
// Expressive Animated Emotion Renderers (Otto / Vector Robot Style)
// -------------------------------------------------------------

static int get_wifi_bars(bool wifi_connected)
{
    if (!wifi_connected) return 0;
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        int rssi = ap_info.rssi;
        if (rssi >= -55) return 4;
        if (rssi >= -68) return 3;
        if (rssi >= -80) return 2;
        return 1;
    }
    return 3;
}

static void draw_header_bar(bool wifi)
{
    // 1. Speaker icon followed by Volume % on top left (x = 1..32, y = 1..9)
    int vol = audio_hal_get_volume();
    // Speaker base
    display_fill_rect(1, 4, 2, 3, true);
    display_draw_line(3, 4, 5, 2, true);
    display_draw_line(3, 6, 5, 8, true);
    display_draw_line(5, 2, 5, 8, true);

    if (vol <= 0) {
        // Mute 'x' mark next to speaker
        display_draw_line(7, 4, 9, 6, true);
        display_draw_line(9, 4, 7, 6, true);
    } else {
        // Sound wave pulse arc next to speaker
        display_draw_line(7, 4, 7, 6, true);
        if (vol >= 50) {
            display_draw_line(8, 3, 8, 7, true);
        }
    }

    char vol_str[8];
    snprintf(vol_str, sizeof(vol_str), "%d%%", vol);
    display_draw_string(10, 2, vol_str, true);

    // 2. Current Time centered in taskbar (x = 49, y = 2)
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char time_str[8];
    if (now > 100000) {
        snprintf(time_str, sizeof(time_str), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    } else {
        snprintf(time_str, sizeof(time_str), "--:--");
    }
    display_draw_string(49, 2, time_str, true);

    // 3. WiFi Signal Column (top right: x = 115..125, y = 1..9)
    if (wifi) {
        int bars = get_wifi_bars(true);
        if (bars >= 1) display_fill_rect(115, 7, 2, 3, true); else display_draw_rect(115, 7, 2, 3, true);
        if (bars >= 2) display_fill_rect(118, 5, 2, 5, true); else display_draw_rect(118, 5, 2, 5, true);
        if (bars >= 3) display_fill_rect(121, 3, 2, 7, true); else display_draw_rect(121, 3, 2, 7, true);
        if (bars >= 4) display_fill_rect(124, 1, 2, 9, true); else display_draw_rect(124, 1, 2, 9, true);
    } else {
        // Disconnected: prominent 'X' at WiFi column position
        display_draw_line(116, 2, 123, 9, true);
        display_draw_line(116, 3, 123, 10, true);
        display_draw_line(123, 2, 116, 9, true);
        display_draw_line(123, 3, 116, 10, true);
    }

    // 4. Taskbar bottom separator line
    display_draw_line(0, 11, 127, 11, true);
}

// Draw a single expressive robot eye (rounded box with pupil cutout)
static void draw_robot_eye(int cx, int cy, int w, int h, int r, int pupil_dx, int pupil_dy)
{
    if (h <= 2) {
        // Flat blinking closed line
        display_fill_rect(cx - w / 2, cy - 1, w, 2, true);
        return;
    }

    int x = cx - w / 2;
    int y = cy - h / 2;
    display_fill_round_rect(x, y, w, h, r, true);

    // Inner dark pupil cutout & shine
    if (h >= 14 && w >= 14) {
        int pw = w / 3 + 2;
        int ph = h / 3 + 2;
        int px = cx + pupil_dx - pw / 2;
        int py = cy + pupil_dy - ph / 2;
        if (px < x + 2) px = x + 2;
        if (px + pw > x + w - 2) px = x + w - 2 - pw;
        if (py < y + 2) py = y + 2;
        if (py + ph > y + h - 2) py = y + h - 2 - ph;

        display_fill_round_rect(px, py, pw, ph, 2, false);
        // Specular glint in top-right of pupil
        display_draw_pixel(px + pw - 2, py + 1, true);
    }
}

// Draw happy curved eye (crescent arc ⌒)
static void draw_happy_eye(int cx, int cy, int w, int h)
{
    int x0 = cx - w / 2;
    int x1 = cx + w / 2;
    for (int x = x0; x <= x1; x++) {
        float norm = (float)(x - cx) / (float)(w / 2);
        if (norm >= -1.0f && norm <= 1.0f) {
            int y = cy - (int)((1.0f - norm * norm) * (float)h);
            display_draw_pixel(x, y, true);
            display_draw_pixel(x, y + 1, true);
            display_draw_pixel(x, y + 2, true);
        }
    }
}

// Draw closed / sleeping eye (curved eyelid ◡)
static void draw_sleeping_eye(int cx, int cy, int w)
{
    int half_w = w / 2;
    for (int x = cx - half_w; x <= cx + half_w; x++) {
        float norm = (float)(x - cx) / (float)half_w;
        if (norm >= -1.0f && norm <= 1.0f) {
            int y = cy + (int)((1.0f - norm * norm) * 4.0f);
            display_draw_pixel(x, y, true);
            display_draw_pixel(x, y + 1, true);
        }
    }
}

// Draw musical note symbol ♪ or ♫
static void draw_musical_note(int x, int y, bool is_double)
{
    display_fill_circle(x, y + 6, 2, true);
    display_draw_line(x + 2, y + 6, x + 2, y, true);
    if (!is_double) {
        display_draw_line(x + 2, y, x + 5, y + 2, true);
    } else {
        display_fill_circle(x + 8, y + 5, 2, true);
        display_draw_line(x + 10, y + 5, x + 10, y - 1, true);
        display_draw_line(x + 2, y, x + 10, y - 1, true);
        display_draw_line(x + 2, y + 1, x + 10, y, true);
    }
}

static void render_booting(void)
{
    draw_header_bar(false);

    // Expressive robot eyes waking up at cy = 32
    int eye_h = 4 + ((s_frame_count * 2) % 24);
    draw_robot_eye(40, 32, 26, eye_h, 6, 0, 0);
    draw_robot_eye(88, 32, 26, eye_h, 6, 0, 0);

    // Animated loading progress bar at y = 50
    int progress = (s_frame_count * 4) % 100;
    display_draw_round_rect(18, 50, 92, 8, 3, true);
    display_fill_round_rect(20, 52, (progress * 88) / 100, 4, 1, true);
}

static void render_provisioning(void)
{
    draw_header_bar(false);
    display_draw_string(8, 22, "WiFi: Lily-Config", true);
    display_draw_string(14, 40, "IP: 192.168.4.1", true);
}

static void render_idle(void)
{
    draw_header_bar(s_ui_data.wifi_connected);

    // Natural blink animation cycle (blink every ~70 frames = 3.5s)
    int cycle = s_frame_count % 70;
    int eye_h = 28;
    if (cycle >= 64 && cycle <= 68) {
        int blink_phase = cycle - 64;
        static const int blink_h[] = { 18, 8, 2, 10, 20 };
        eye_h = blink_h[blink_phase];
    }

    // Gentle eye drift (looking around naturally)
    int drift_cycle = (s_frame_count / 40) % 4;
    int pdx = 0, pdy = 0;
    if (drift_cycle == 1) pdx = -2;
    else if (drift_cycle == 3) pdx = 2;

    // Draw Left and Right Robot Eyes centered vertically at cy = 38 (comfortable spacing from taskbar)
    draw_robot_eye(40, 38, 26, eye_h, 6, pdx, pdy);
    draw_robot_eye(88, 38, 26, eye_h, 6, pdx, pdy);

    // NO MOUTH, NO TEXT (Clean, iconic robot eyes only)
}

static void render_listening(void)
{
    draw_header_bar(s_ui_data.wifi_connected);

    // Wide, attentive curious eyes centered vertically at cy = 38
    int eye_h = 30;
    draw_robot_eye(44, 38, 26, eye_h, 7, 0, 0);
    draw_robot_eye(84, 38, 26, eye_h, 7, 0, 0);

    // Animated radar / sonic rings expanding from both sides
    int wave_phase = (s_frame_count / 2) % 3;
    for (int w = 0; w < 3; w++) {
        int r = 5 + ((w + wave_phase) % 3) * 5;
        // Left sonic wave arc
        display_draw_line(16 - r/2, 38 - r/2, 16 - r, 38, true);
        display_draw_line(16 - r, 38, 16 - r/2, 38 + r/2, true);
        // Right sonic wave arc
        display_draw_line(112 + r/2, 38 - r/2, 112 + r, 38, true);
        display_draw_line(112 + r, 38, 112 + r/2, 38 + r/2, true);
    }

    // 3 animated bouncing dots at bottom
    int dot_phase = (s_frame_count / 3) % 3;
    for (int i = 0; i < 3; i++) {
        int dy = (i == dot_phase) ? -2 : 0;
        display_fill_circle(58 + i * 7, 58 + dy, 1, true);
    }

    // NO TEXT
}

static void render_thinking(void)
{
    draw_header_bar(s_ui_data.wifi_connected);

    // Inquisitive eyes looking up and to the right, centered vertically around cy = 38
    draw_robot_eye(38, 39, 24, 24, 6, 3, -4);
    draw_robot_eye(82, 37, 26, 28, 6, 4, -4);

    // Animated thinking bubbles floating up to top right
    int phase = (s_frame_count / 3) % 4;
    int b1_r = 1 + (phase % 2);
    int b2_r = 2 + ((phase + 1) % 2);
    int b3_r = 3 + ((phase + 2) % 2);
    display_draw_circle(104, 32, b1_r, true);
    display_draw_circle(112, 24, b2_r, true);
    display_draw_circle(120, 16, b3_r, true);
    display_draw_pixel(121, 15, false); // specular glint

    // NO TEXT
}

static void render_speaking(void)
{
    draw_header_bar(s_ui_data.wifi_connected);

    // Cheerful happy curved eyes (⌒ ⌒) at cy = 25
    draw_happy_eye(40, 25, 24, 7);
    draw_happy_eye(88, 25, 24, 7);

    // Animated talking robot mouth
    int mouth_h = 3 + (abs((int)(s_frame_count % 8) - 4) * 2);
    display_fill_round_rect(56, 36 - mouth_h / 2, 16, mouth_h, 2, true);

    // Animated voice waves from sides
    int sw = (s_frame_count / 2) % 2;
    display_draw_line(20, 28, 20, 34, true);
    if (sw == 1) display_draw_line(16, 26, 16, 36, true);
    display_draw_line(108, 28, 108, 34, true);
    if (sw == 1) display_draw_line(112, 26, 112, 36, true);

    display_draw_line(0, 46, 127, 46, true);

    // Subtitle response marquee with full Vietnamese text support
    int offset = s_frame_count * 2;
    display_draw_string_marquee(0, 52, s_ui_data.response_text, offset, 128, true);
}

static void render_playing(void)
{
    // Taskbar: Volume % on left, Lily in center, WiFi RSSI on right
    draw_header_bar(s_ui_data.wifi_connected);

    // Dancing / grooving robot eyes swaying slightly to the rhythm
    int sway = ((s_frame_count / 5) % 2 == 0) ? -1 : 1;
    draw_happy_eye(42 + sway, 20, 22, 6);
    draw_happy_eye(86 + sway, 20, 22, 6);

    // Floating animated musical notes in corners
    int bob1 = ((s_frame_count / 4) % 2) * 2;
    int bob2 = (((s_frame_count + 4) / 4) % 2) * 2;
    draw_musical_note(10, 14 + bob1, false);
    draw_musical_note(110, 14 + bob2, true);

    // Middle: 24 columns dynamic FFT spectrum analyzer
    audio_analyzer_get_spectrum(s_ui_data.spectrum);
    for (int i = 0; i < 24; i++) {
        int bar_h = (int)(s_ui_data.spectrum[i] * 16.0f);
        if (bar_h > 16) bar_h = 16;
        if (bar_h < 1) bar_h = 1;
        int bx = 4 + i * 5;
        int by = 48 - bar_h;
        display_fill_rect(bx, by, 4, bar_h, true);
    }
    display_draw_line(0, 50, 127, 50, true);

    // Bottom row: Song title marquee only (no lyric to avoid timing delay)
    int offset = s_frame_count * 2;
    const char *title_to_show = strlen(s_ui_data.music_title) > 0 ? s_ui_data.music_title : "Đang phát nhạc...";
    display_draw_string_marquee(0, 54, title_to_show, offset, 128, true);
}

static void render_mcp_action(void)
{
    draw_header_bar(s_ui_data.wifi_connected);

    // Visor scanner
    display_fill_round_rect(20, 16, 88, 16, 4, true);
    int scan_x = (s_frame_count * 4) % 76;
    display_fill_rect(24 + scan_x, 17, 6, 14, false);

    // Toast popup with action command
    display_draw_round_rect(4, 38, 120, 24, 3, true);
    display_fill_round_rect(5, 39, 118, 22, 2, false);
    display_draw_string_marquee(8, 46, s_ui_data.toast_text, s_frame_count * 2, 112, true);

    // Timeout after 2.0s
    int64_t elapsed = esp_timer_get_time() - s_toast_start_us;
    if (elapsed > 2000000) {
        s_current_state = s_prev_state;
    }
}

static void render_error(void)
{
    draw_header_bar(s_ui_data.wifi_connected);

    // Two cute sleeping / offline eyes centered at cy = 38 (comfortable spacing)
    draw_sleeping_eye(40, 38, 24);
    draw_sleeping_eye(88, 38, 24);

    // Cute animated floating 'z' drifting up from right eye to show standby/offline
    int z_step = (s_frame_count / 10) % 4;
    display_draw_string(96, 32 - z_step * 3, "z", true);
    display_draw_string(104, 26 - z_step * 3, "Z", true);

    // NO TEXT (pure expressive emotion)
}

void ui_task(void *pvParameters)
{
    ESP_LOGI(TAG, "UI Task running at 20Hz");
    TickType_t last_wake_time = xTaskGetTickCount();

    while (1) {
        if (xSemaphoreTake(s_ui_mutex, pdMS_TO_TICKS(40)) == pdTRUE) {
            display_clear();

            switch (s_current_state) {
            case UI_STATE_BOOTING:
                render_booting();
                break;
            case UI_STATE_PROVISIONING:
                render_provisioning();
                break;
            case UI_STATE_IDLE:
                render_idle();
                break;
            case UI_STATE_LISTENING:
                render_listening();
                break;
            case UI_STATE_THINKING:
                render_thinking();
                break;
            case UI_STATE_SPEAKING:
                render_speaking();
                break;
            case UI_STATE_PLAYING:
                render_playing();
                break;
            case UI_STATE_MCP_ACTION:
                render_mcp_action();
                break;
            case UI_STATE_ERROR:
                render_error();
                break;
            }

            display_flush();
            s_frame_count++;
            xSemaphoreGive(s_ui_mutex);
        }

        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(50)); // 20Hz (50ms per frame)
    }
}

