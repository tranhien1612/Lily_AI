# Lily_AI - Trợ lý ảo thông minh (ESP32-S3 & Raspberry Pi)

## BẢN ĐẶC TẢ KIẾN TRÚC HỆ THỐNG (Self-contained)
## 1. TỔNG QUAN HỆ THỐNG
Xây dựng một hệ thống trợ lý ảo AI hoàn chỉnh gồm 2 thành phần: Firmware chạy trên vi điều khiển ESP32-S3 và Server AI chạy Local trên Raspberry Pi. Hệ thống hoạt động hoàn toàn trong mạng LAN, không phụ thuộc vào Cloud, không hỗ trợ OTA (firmware được nạp trực tiếp qua cổng USB mỗi khi có phiên bản mới).

Các chức năng cốt lõi cần triển khai:
1. Cấu hình mạng qua Web Captive Portal (nhập WiFi + địa chỉ Server, chuyển chế độ mượt mà không cần restart chip).
2. Đánh thức ngoại tuyến (Wakeword) bằng giọng nói từ khóa "Hi Lily" hoặc "Lily".
3. Hỏi đáp AI theo thời gian thực (ASR → LLM → TTS streaming).
4. Điều khiển GPIO bằng giọng nói thông qua giao thức MCP.
5. Phát nhạc trực tuyến (streaming HTTP) kèm hiển thị lyric và spectrum.
6. Giao diện OLED thay đổi động theo từng chế độ hoạt động.
7. Giao diện Web Dashboard trên Server: Quản lý các thiết bị trực tuyến trong mạng LAN, hiển thị lịch sử đoạn chat giữa thiết bị và server với cả âm thanh giọng nói (Voice Audio Player) và văn bản (Text), đồng thời tích hợp ô Chat trực tiếp từ Web để tương tác với AI và điều khiển ESP32 mà không cần nói qua micro.

## 2. PHẦN CỨNG VÀ NỀN TẢNG
| Thành phần | Thông số |
| -----------| -------- |
| Vi điều khiển | ESP32-S3 (mạch XH-S3E N16R8: 16MB Flash, 8MB PSRAM) |
| Màn hình | OLED SSD1306 128x64 (I2C) — hoặc LCD ST7789 (SPI) nếu cần màu |
| Micro | I2S MEMS Microphone (INMP441 hoặc tương đương) |
| Loa | I2S Amplifier + Speaker (MAX98357A hoặc tương đương) |
| Nút nhấn | Nút BOOT (GPIO0) — dùng để reset cấu hình WiFi |
| Server | Raspberry Pi 4/5, Ubuntu Server 20.04+ |
| Framework Firmware| ESP-IDF v5.1 trở lên (C/C++) |
| Framework Server	| Python 3.10+ (FastAPI) |

## 3. KIẾN TRÚC TỔNG THỂ
```
┌────────────────────────────────────────────────────────────┐
│                   THIẾT BỊ (ESP32-S3)                       │
│                                                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐         │
│  │ WiFi Manager│  │ Audio HAL   │  │ Display/UI  │         │
│  │  (STA/AP)   │  │ (I2S+Opus)  │  │ (u8g2/State)│         │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘         │
│         │                │                 │                │
│  ┌──────┴────────────────┴─────────────────┴──────┐        │
│  │              Core Event Loop (FreeRTOS)         │        │
│  └──────┬────────────────┬─────────────────┬──────┘        │
│         │                │                 │                │
│  ┌──────┴──────┐  ┌──────┴──────┐  ┌──────┴──────┐         │
│  │  Wakeword   │  │  WebSocket  │  │ MCP Server  │         │
│  │  (ESP-SR)   │  │   Client    │  │ (JSON-RPC)  │         │
│  └─────────────┘  └──────┬──────┘  └──────┬──────┘         │
│                           │                 │                │
│                    ┌──────┴─────────────────┴──────┐        │
│                    │  Audio Analyzer (FFT/esp-dsp)  │        │
│                    └────────────────────────────────┘        │
└──────────────────────────┬──────────────────────────────────┘
                           │ WebSocket (LAN)
                           │ ws://<raspi-ip>:8000/ws
                           │
┌──────────────────────────┴──────────────────────────────────┐
│                  SERVER (Raspberry Pi)                       │
│                                                              │
│  ┌────────────┐   ┌────────────┐   ┌────────────┐           │
│  │ ASR        │ → │ LLM        │ → │ TTS        │           │
│  │ (Whisper)  │   │ (Ollama)   │   │ (Edge-TTS) │           │
│  └────────────┘   └──────┬─────┘   └────────────┘           │
│                           │                                  │
│  ┌────────────┐   ┌──────┴─────┐   ┌────────────┐           │
│  │ Music      │   │ MCP        │   │ WebSocket  │           │
│  │ Handler    │   │Orchestrator│   │  Server    │           │
│  └────────────┘   └────────────┘   └────────────┘           │
└──────────────────────────────────────────────────────────────┘
```

## 4. KIẾN TRÚC THIẾT BỊ (FIRMWARE)
### 4.1. Phân vùng bộ nhớ (Partition Table — No OTA)
|Name|	Type|	SubType|	Offset|	Size|
|-|-|-|-|-|
|nvs|	data|	nvs|	0x9000|	0x4000|
|phy_init|	data|	phy|	0xd000|	0x1000|
|factory|	app|	factory|	0x10000|	4M|
|storage|	data|	spiffs|	0x410000|	11M|

### 4.2. Danh sách Module và Trách nhiệm
|Module	|File	|Trách nhiệm chính|	API công khai (gợi ý)|
|-|-|-|-|
|WiFi Manager|	components/wifi/wifi_manager.c|	Quản lý chế độ STA/AP, Captive Portal, kết nối WiFi tự động|	wifi_init(), wifi_start_ap(), wifi_is_connected()|
|Audio HAL|	components/audio/audio_hal.c|	Khởi tạo I2S, encode/decode Opus, quản lý Ring Buffer|	audio_init(), audio_start_record(), audio_play_pcm()|
|Wakeword|	components/wakeword/wakeword.c|	Phát hiện từ khóa offline bằng ESP-SR|	wakeword_init(), callback on_wakeword_detected()|
|WebSocket Client|	components/ws/ws_client.c|	Kết nối tới Server, parse JSON, gửi/nhận audio + control|	ws_connect(), ws_send_json(), ws_send_audio()|
|MCP Server|	components/mcp/mcp_server.c|	JSON-RPC 2.0, xử lý tools/list và tools/call|	mcp_init(), mcp_register_tool(), mcp_handle_message()|
|GPIO Tools|	components/mcp/tools/gpio.c|	Đăng ký các tool gpio_write, gpio_read, pwm_write|	gpio_tools_register()|
|Music Player|	components/music/music_player.c|	Stream HTTP file nhạc, decode, đẩy ra I2S	|music_play(url), music_stop()|
|Audio Analyzer|	components/audio/audio_analyzer.c|	Tính FFT từ buffer audio bằng esp-dsp, cấp dữ liệu spectrum cho UI|	analyzer_get_spectrum(bins[])|
|Display Driver|	components/display/display.c|	Cấu hình I2C/SPI, driver cho OLED SSD1306 hoặc LCD ST7789	|display_init(), display_flush()|
|UI Manager|	components/ui/ui_manager.c|	State Machine cho giao diện, mỗi state có hàm render() riêng	|ui_set_state(), ui_update_text(), ui_render_tick()|
|App Main|	main/app_main.c|	Khởi tạo NVS, các module, tạo Task, event loop chính|	app_main()|

### 4.3. UI State Machine — Chi tiết hiển thị
Đây là phần quan trọng nhất để đáp ứng yêu cầu "giao diện thay đổi theo chế độ".

|State|	Kích hoạt|	Hiển thị trên OLED|
|-|-|-|
|BOOTING	|Khởi động|	Logo Xiaozhi, text "Đang khởi động...", icon WiFi nhấp nháy|
|PROVISIONING|	Không có WiFi / Nhấn giữ nút BOOT|	Text "Kết nối WiFi 'Xiaozhi-S3-Config'", text "Truy cập 192.168.4.1"|
|IDLE|	Sẵn sàng, không tương tác|	Mặt cười, icon WiFi (có sóng), icon pin, text "Hi Lily..." nhấp nháy|
|LISTENING|	Wakeword detected / Bấm nút|	Icon Micro rung (2 frame animation), text "Đang nghe...", 3 chấm loading|
|THINKING|	Đã gửi xong audio lên Server|	Icon não bộ, text "Đang suy nghĩ...", vòng tròn loading xoay|
|SPEAKING|	Nhận tts_start từ Server|	Dòng trên: Text ASR thu được (scroll ngang nếu dài) Dòng giữa: Icon loa nhấp nháy Dòng dưới: Text phản hồi LLM (scroll ngang nếu dài)|
|PLAYING	|Nhận music_start từ Server|	Trên cùng: Tiêu đề bài hát Giữa: Spectrum analyzer (dải 16-24 cột sóng nhảy theo nhạc) Dưới cùng: Lyric chạy (text cuộn theo timestamp)|
|MCP_ACTION	|Đang thực thi tool|	Toast popup 2 giây: "Đang bật đèn...", sau đó quay lại State trước|
|ERROR|	Mất WiFi / Mất WS|	Icon X đỏ, text "Mất kết nối", icon WiFi gạch chéo, text "Đang thử lại..."|

### 4.4. Chi tiết triển khai UI Manager
```c
typedef enum {
    UI_STATE_BOOTING,
    UI_STATE_PROVISIONING,
    UI_STATE_IDLE,
    UI_STATE_LISTENING,
    UI_STATE_THINKING,
    UI_STATE_SPEAKING,
    UI_STATE_PLAYING,
    UI_STATE_MCP_ACTION,
    UI_STATE_ERROR
} ui_state_t;

// Cấu trúc dữ liệu UI cần hiển thị
typedef struct {
    char asr_text[128];       // Text nhận dạng được
    char response_text[256];  // Text phản hồi LLM
    char music_title[64];
    char lyric_line[128];
    float spectrum[24];       // 24 bins cho spectrum
    uint8_t battery_pct;
    bool wifi_connected;
} ui_data_t;

void ui_set_state(ui_state_t state);
void ui_update_asr_text(const char *text);
void ui_update_response_text(const char *text);
void ui_update_lyric(const char *line);
void ui_update_spectrum(float *bins);
void ui_task(void *pvParameters); // FreeRTOS task, tick 20Hz
```

Yêu cầu kỹ thuật cho UI:
- Tần số render: 20 Hz (đủ mượt cho animation, không tốn CPU).
- Sử dụng double buffer để tránh nhấp nháy (OLED SSD1306 hỗ trợ qua thư viện u8g2).
- Text dài hơn màn hình → dùng hiệu ứng scroll ngang (marquee), tốc độ 30 pixels/giây.
- Spectrum analyzer: 24 cột, mỗi cột map với dải tần từ FFT (chia đều dải 100Hz-8kHz thành 24 bins, lấy logarit).

### 4.5. Luồng khởi động Firmware
```txt
1. NVS Init → Đọc cấu hình WiFi + WS URL
2. Nếu chưa có WiFi → Chế độ AP (PROVISIONING)
3. Nếu có WiFi → Kết nối STA
   - Thành công (≤ 60s): Tiếp tục bước 4
   - Thất bại: Rơi về chế độ AP
4. Kết nối WebSocket tới Server (URL trong NVS)
   - Thành công: Chuyển sang IDLE
   - Thất bại: State ERROR, retry mỗi 5s
5. Khởi động các task: wakeword, ui_task, audio_task, ws_task
6. Chờ wakeword → chuyển sang LISTENING
```

## 5. KIẾN TRÚC SERVER (RASPBERRY PI)
### 5.1. Danh sách Module
|Module|	File|	Trách nhiệm|
|-|-|-|
|WebSocket Server|	app/ws_server.py|	FastAPI WebSocket endpoint, quản lý connection, route message|
|ASR Engine|	app/ai/asr.py|	Speech-to-Text với faster-whisper (model small/base)|
|LLM Engine|	app/ai/llm.py|	Chat với Ollama (model qwen2:1.5b hoặc gemma2:2b)|
|TTS Engine|	app/ai/tts.py|	Text-to-Speech với edge-tts (voice vi-VN-HoaiMyNeural)|
|MCP Orchestrator|	app/mcp/orchestrator.py|	Quản lý tool registry của từng device, xử lý tool calls từ LLM|
|Music Handler|	app/media/music.py|	Quản lý thư viện nhạc, stream HTTP, parse file lyric (.lrc)|
|Session Manager|	app/session.py|	Quản lý trạng thái từng thiết bị, lưu trữ tin nhắn hội thoại và file ghi âm giọng nói|
|Web Dashboard|	app/static/dashboard.html|	Giao diện web trực quan quản lý danh sách thiết bị LAN, xem lịch sử chat và phát audio voice|
|Config|	app/config.py|	Đọc biến môi trường, cấu hình model|

### 5.2. Chi tiết AI Pipeline
Bước 1 — ASR:
- Nhận audio Opus → decode ra PCM 16kHz → đưa vào faster-whisper.
- Kết quả: text (ví dụ: "Hôm nay trời thế nào?").
- Gửi ngay text này xuống Device qua message transcript.

Bước 2 — LLM:
- Input: text từ ASR + lịch sử hội thoại (context window 5 lượt gần nhất).
- LLM được cấu hình với tool definitions (danh sách tool của device).
- LLM có thể:
    - Trả về text thuần (câu trả lời thông thường).
    - Hoặc sinh ra tool_call (yêu cầu gọi MCP tool).
    - Gửi text phản hồi xuống Device qua message response_text (trước khi gọi tool nếu có).

Bước 3 — Xử lý Tool Call (nếu có):
- Server đóng gói JSON-RPC và gửi xuống Device qua mcp_call.
- Chờ mcp_result từ Device (timeout 5s).
- Gửi kết quả tool vào LLM để LLM sinh câu trả lời cuối cùng.

Bước 4 — TTS:
- Text cuối cùng từ LLM → edge-tts → file MP3 tạm → convert sang Opus.
- Gửi tts_start → stream các frame Opus → tts_stop.

### 5.3. Chi tiết Music Handler & Tải nhạc Việt Nam tự động
Hệ thống tích hợp bộ xử lý nhạc thông minh theo chuẩn TienHuyIoT (`xiaozhi-esp32_vietnam`):
1. **Phân tích ngữ cảnh & Hỏi tên bài hát:**
   - Nếu người dùng chỉ nói *"Bật nhạc"*, *"Mở nhạc"*, *"Phát nhạc"* (chưa nói rõ tên bài): AI sẽ hỏi lại: *"Bạn muốn nghe bài hát nào? Hãy nói tên bài hát cho tôi nhé."*.
   - Khi người dùng cung cấp tên bài (hoặc nói trực tiếp *"Phát cho tôi bài Hãy Trao Cho Anh"*, *"Mở bài Cắt Đôi Nỗi Sầu"*): Hệ thống trích xuất chính xác tên bài hát.
2. **Tìm kiếm & Tự động tải nhạc Việt Nam (yt-dlp Engine):**
   - Kiểm tra thư viện cục bộ `/server/music/`: Nếu đã có file thì phát ngay tức thì (<10ms).
   - Nếu chưa có: Server thông báo giọng nói *"Đang tìm bài hát [Tên bài] cho bạn..."*, đồng thời tự động tìm kiếm nguồn nhạc Việt Nam chất lượng cao và tải về định dạng MP3 128kbps chuẩn cho vi điều khiển ESP32.
   - Tự động sinh file lyric (`.lrc`) đi kèm.
3. **Phát nhạc & Hiển thị trên thiết bị:**
   - Server gửi gói lệnh `music_start` (chứa URL tải trực tiếp, tiêu đề bài hát và thời lượng).
   - ESP32 tải file qua HTTP stream từ `/music/{filename}` và giải mã đẩy ra loa.
   - Màn hình OLED hiển thị tiêu đề, chạy lời bài hát cuộn và biểu diễn **24 cột sóng phổ âm thanh Spectrum (FFT)** nhảy sống động theo điệu nhạc.

### 5.4. Web Dashboard — Quản lý Thiết bị & Lịch sử Hội thoại
Server cung cấp giao diện web Dashboard trực tiếp tại đường dẫn `http://<raspi-ip>:8000/dashboard` (hoặc `http://<raspi-ip>:8000/`):
- **Quản lý danh sách thiết bị trực tuyến (Online-only):**
  - Tự động nhận diện thiết bị ESP32 khi kết nối WebSocket (theo IP LAN hoặc ID định danh).
  - Web chỉ hiển thị các thiết bị **đang trực tuyến (Online)** theo thời gian thực (tự động cập nhật mỗi 3 giây khi thiết bị kết nối hoặc ngắt kết nối).
  - Hiển thị địa chỉ IP, thời gian hoạt động gần nhất (Last Seen) và số lượng tin nhắn trao đổi.
- **Xem chi tiết đoạn chat giữa thiết bị và server:**
  - Khi người dùng click vào thiết bị trực tuyến ở danh sách bên trái, khu vực bên phải hiển thị toàn bộ lịch sử hội thoại dạng tin nhắn (Chat bubble) giữa thiết bị đó và AI Assistant (Lily).
  - **Đồng bộ chuẩn xác Giọng nói (Voice) và Văn bản (Text):**
    - **Phía người dùng (Micro thu âm):** Văn bản ASR nhận diện chính xác nội dung người dùng nói (không bị VAD lọc nhầm) kèm trình phát HTML5 Audio để nghe lại trực tiếp file âm thanh thu âm từ Micro (`.wav` 16kHz Mono).
    - **Phía trợ lý (Lily):** Văn bản phản hồi của AI kèm trình phát HTML5 Audio phát âm thanh giọng đọc TTS (`.wav` 16kHz Mono) hoàn chỉnh tương ứng.
    - **Tool Execution:** Nếu lượt hội thoại có gọi lệnh MCP (như bật/tắt GPIO hoặc phát nhạc), giao diện hiển thị badge thông tin tool được thực thi.
- **API hỗ trợ Dashboard:**
  - `GET /api/devices`: Trả về danh sách JSON các thiết bị kèm trạng thái và metadata.
  - `GET /api/devices/{device_id}/messages`: Trả về toàn bộ danh sách tin nhắn, link file audio wav và tool call của thiết bị.
  - `GET /recordings/{filename}`: Endpoint phục vụ phát trực tiếp các file âm thanh thu âm và TTS.

## 6. GIAO THỨC TRUYỀN THÔNG
### 6.1. Cấu trúc chung
- Transport: WebSocket (port 8000, path /ws).
- Audio: Binary frame (Opus encoded).
- Control: Text frame (JSON UTF-8).

### 6.2. Bảng đầy đủ các Message Types
|type|	Hướng|	Payload|	Mục đích|
|-|-|-|-|
|hello|	S→D|	{"session_id": "uuid"}|	Server chào Device, tạo session|
|ready|	D→S|	{}|	Device xác nhận sẵn sàng|
|listen_start|	D→S|	{}|	Device bắt đầu ghi âm|
|listen_stop|	D→S|	{}|	Device kết thúc ghi âm (do VAD hoặc user bấm nút)|
|transcript|	S→D|	{"text": "..."}|	Text ASR — UI hiển thị|
|response_text|	S→D|	{"text": "..."}|	Text LLM — UI hiển thị|
|tts_start|	S→D|	{}|	Bắt đầu stream TTS (UI chuyển SPEAKING)|
|tts_stop|	S→D|	{}|	Kết thúc TTS (UI về IDLE)|
|music_start|	S→D	|{"url": "...", "title": "...", "duration": 180}|	Bắt đầu phát nhạc (UI chuyển PLAYING)|
|music_lyric	|S→D|	{"line": "...", "timestamp": 12.5}|	Dòng lyric tại thời điểm t (giây)|
|music_stop	|S→D|	{}|	Dừng nhạc (UI về IDLE)|
|mcp_call	|S→D|	{"jsonrpc": "2.0", "method": "tools/call", "params": {...}, "id": 1}|	Gọi tool MCP
|mcp_result|	D→S|	{"jsonrpc": "2.0", "result": {...}, "id": 1}|	Trả kết quả tool
|error|	S↔D|	{"code": 500, "message": "..."}	|Báo lỗi|

### 6.3. Sequence Diagram — Hỏi đáp có Tool Call
```txt
Device                Server                  Ollama
  │                     │                       │
  │──listen_start──────▶│                       │
  │──[audio opus]──────▶│                       │
  │──listen_stop───────▶│                       │
  │                     │──ASR────────────────▶│
  │◀──transcript────────│                       │
  │                     │──chat (with tools)──▶│
  │                     │◀──tool_call──────────│
  │◀──response_text─────│  ("Đang bật đèn")     │
  │◀──mcp_call──────────│                       │
  │──mcp_result────────▶│                       │
  │                     │──chat (tool result)─▶│
  │                     │◀──final_text─────────│
  │◀──tts_start─────────│                       │
  │◀──[audio tts]───────│                       │
  │◀──tts_stop──────────│                       │
```
### 6.4. Sequence Diagram — Phát nhạc
```txt
Device                Server                  Storage
  │                     │                       │
  │──listen_start──────▶│                       │
  │──[audio opus]──────▶│                       │
  │──listen_stop───────▶│                       │
  │◀──transcript────────│                       │
  │◀──mcp_call──────────│  music_play           │
  │──mcp_result────────▶│                       │
  │                     │──Read .lrc───────────▶│
  │◀──music_start───────│                       │
  │──HTTP GET mp3──────────────────────────────▶│
  │◀──[mp3 stream]──────────────────────────────│
  │   (tự phát nhạc + tính FFT)                 │
  │◀──music_lyric───────│ (mỗi dòng lyric)      │
  │◀──music_stop────────│ (khi hết nhạc)        │
```

## 7. LỘ TRÌNH THỰC HIỆN (Dành cho AI Agent)
Thực hiện tuần tự từng Phase, mỗi Phase sinh code hoàn chỉnh cho các file được liệt kê.

### Phase 1 — Scaffold dự án
Sinh ra:
- Cấu trúc thư mục ESP-IDF: main/, components/ với các thư mục con cho từng module.
- File partitions.csv (theo bảng 4.1).
- File sdkconfig.defaults (bật PSRAM, tắt OTA, cấu hình flash 16MB).
- Cấu trúc thư mục FastAPI Server: app/, app/ai/, app/mcp/, app/media/, requirements.txt, run.py.

### Phase 2 — Cấu hình WiFi & Web Provisioning
Sinh ra:
- wifi_manager.c: Logic STA/AP, state machine.
- web_server.c: HTTP server với thư mục spiffs/www/ chứa index.html, style.css, script.js.
- Trang web phải có: quét WiFi, form nhập SSID/password, form nhập WebSocket Server URL (VD: ws://192.168.1.12:8000/ws), nút Save.
- nvs_storage.c: Wrapper đọc/ghi NVS.

### Phase 3 — Audio & Wakeword
Sinh ra:
- audio_hal.c: Cấu hình I2S, Opus encode/decode.
- wakeword.c: Tích hợp ESP-SR, callback.
- audio_analyzer.c: FFT với esp-dsp, hàm analyzer_get_spectrum().

### Phase 4 — Giao thức & MCP
Sinh ra:
- ws_client.c: WebSocket client, JSON parser (cJSON), xử lý tất cả message types ở bảng 6.2.
- mcp_server.c: JSON-RPC 2.0, đăng ký tool.
- gpio.c trong mcp/tools/: Tool gpio_write, gpio_read, pwm_write.

### Phase 5 — UI Manager & Display
Sinh ra:
- display.c: Driver I2C cho OLED SSD1306 (dùng u8g2).
- ui_manager.c: State machine đầy đủ 9 state (bảng 4.3).
- render_idle(): mặt cười + icon.
- render_listening(): icon mic animation.
- render_thinking(): icon loading.
- render_speaking(): 2 dòng text scroll.
- render_playing(): tiêu đề + spectrum + lyric.
- render_error(): icon X đỏ.
- ui_task: FreeRTOS task, tick 20Hz.

### Phase 6 — Music Player
Sinh ra:
- music_player.c: HTTP GET, decode MP3 (dùng libmad hoặc helix), đẩy ra I2S. Cung cấp dữ liệu PCM cho audio_analyzer.

### Phase 7 — Server
Sinh ra:
- ws_server.py: WebSocket endpoint, session manager.
- asr.py: faster-whisper wrapper.
- llm.py: Ollama client với tool definitions.
- tts.py: edge-tts wrapper.
- orchestrator.py: MCP orchestrator.
- music.py: Thư viện nhạc, parse .lrc, gửi lyric task.

### Phase 8 — Tích hợp & Kiểm thử
Sinh ra:
- app_main.c: Khởi tạo tất cả module, tạo task.
- Script test end-to-end.
- Tài liệu hướng dẫn nạp firmware và chạy server.

## 8. TIÊU CHÍ NGHIỆM THU
| #	| Tiêu chí	| Cách kiểm tra |
|-- | --------- | ------------- |
| 1 |	Cấu hình WiFi qua Web|	Nhấn giữ BOOT 3s → AP Xiaozhi-S3-Config → Web nhập WiFi + WS URL → lưu |
|2|	Kết nối Server|	Thiết bị tự kết nối WS sau khi có WiFi, UI chuyển IDLE|
|3|	Wakeword|   	Nói "Hi Lily" (hoặc "Lily") → UI chuyển LISTENING, nghe tiếng "bíp"|
|4|	Hỏi đáp có text|	Nói "Hôm nay thế nào?" → UI hiển thị text ASR + text phản hồi + phát giọng nói|
|5|	MCP GPIO|	Nói "Bật đèn" → GPIO chỉ định chuyển HIGH, UI hiện toast|
|6|	Phát nhạc + Lyric + Spectrum|	Nói "Mở nhạc" → UI PLAYING, hiển thị lyric chạy + 24 cột spectrum nhảy theo nhạc|
|7|	Mất kết nối |	Rút WiFi → UI ERROR → tự reconnect trong 5s|
|8|	Không OTA|	Xác nhận không có endpoint/partition OTA nào trong firmware|
|9|	Web Quản lý Thiết bị & Lịch sử Chat|	Truy cập http://<raspi-ip>:8000/dashboard, thấy danh sách thiết bị Online/Offline, click vào thiết bị xem lịch sử đoạn chat và nghe được cả voice lẫn text|

## 9. GHI CHÚ QUAN TRỌNG CHO AI AGENT
1. Không tạo code OTA dưới bất kỳ hình thức nào (esp_ota_*, app_update, endpoint /ota).
2. Ưu tiên tiếng Việt cho text UI và giọng TTS (vi-VN-HoaiMyNeural).
3. Tối ưu cho ESP32-S3: Sử dụng PSRAM cho buffer audio lớn (≥ 64KB), tận dụng các peripheral DMA của I2S.
4. State Machine phải thread-safe: Dùng queue hoặc mutex khi chuyển state từ các task khác nhau.
5. Timeout & Retry: Mọi kết nối (WiFi, WS, HTTP) phải có timeout và cơ chế retry với exponential backoff.
6. Logging: Sử dụng ESP_LOGI/W/E với tag rõ ràng để debug.
7. Đảm bảo mã nguồn biên dịch được với idf.py build và pip install -r requirements.txt không lỗi.

## 10. Tài liệu tham khảo
- https://github.com/78/xiaozhi-esp32 
- https://github.com/TienHuyIoT/xiaozhi-esp32_vietnam/tree/develop_vn
- https://github.com/xinnan-tech/xiaozhi-esp32-server

## 11. ĐẶC TẢ CHUẨN GIAO THỨC & THIẾT KẾ ĐỒNG BỘ NGUYÊN BẢN (XIAOZHI STANDARD)

### 11.1. Chuẩn Bắt tay & Giao thức WebSocket (Khớp `xinnan-tech/xiaozhi-esp32-server` & `xiaozhi-esp32`)
Hệ thống tuân thủ toàn diện chuẩn gói tin JSON WebSocket của Xiaozhi:
1. **Bắt tay kết nối (Handshake Hello):**
   - Client gửi khi kết nối thành công:
     ```json
     {
       "type": "hello",
       "version": 1,
       "transport": "websocket",
       "features": {"mcp": true},
       "audio_params": {"format": "pcm", "sample_rate": 16000, "channels": 1, "frame_duration": 60}
     }
     ```
   - Server phản hồi xác nhận:
     ```json
     {
       "type": "hello",
       "transport": "websocket",
       "session_id": "...",
       "audio_params": {"format": "pcm", "sample_rate": 16000, "channels": 1, "frame_duration": 60}
     }
     ```
2. **Luồng hội thoại & Trạng thái giọng nói (STT / LLM / TTS):**
   - **STT (ASR):** Server gửi `{"type": "stt", "text": "...", "session_id": "..."}` để cập nhật câu nói của người dùng lên OLED và Web Dashboard.
   - **LLM Thinking:** Server gửi `{"type": "llm", "emotion": "thinking"}` báo hiệu AI đang suy luận.
   - **TTS Streaming:**
     - Bắt đầu nói: Server gửi `{"type": "tts", "state": "start"}` (kèm `tts_start`).
     - Từng câu nói: Server gửi `{"type": "tts", "state": "sentence_start", "text": "..."}` để hiển thị phụ đề chạy chữ.
     - Dữ liệu âm thanh: Server gửi stream nhị phân (binary PCM 16kHz) qua WebSocket.
     - Kết thúc nói: Server gửi `{"type": "tts", "state": "stop"}` (kèm `tts_stop`) để thiết bị về trạng thái IDLE.
3. **Giao thức MCP (Model Context Protocol):**
   - Server gửi lệnh điều khiển phần cứng:
     ```json
     {
       "type": "mcp",
       "session_id": "...",
       "payload": {"jsonrpc": "2.0", "method": "gpio_write", "params": {"pin": 48, "level": 1}, "id": 1}
     }
     ```
   - Client thực thi trên vi điều khiển và trả về kết quả qua `mcp_result`.
4. **Giao thức phát nhạc (Music streaming):**
   - Bắt đầu phát: `{"type": "music_start", "url": "http://.../music/song_{hash}.pcm", "title": "...", "duration": ...}`
   - Chạy lời bài hát đồng bộ thời gian: `{"type": "music_lyric", "line": "...", "timestamp": ...}`
   - Dừng phát nhạc: `{"type": "music_stop"}`

### 11.2. Xử lý âm thanh & Khắc phục hoàn toàn tiếng rè (Static Noise)
- **Nguyên nhân tiếng rè trước đây:** Thiết bị dùng I2S DAC (MAX98357A) tiếp nhận trực tiếp luồng PCM thô không qua chip giải mã MP3 phần cứng. Khi server gửi file MP3 nén, vi điều khiển đưa trực tiếp bitstream nén vào DAC gây ra tiếng rè, rít chói tai.
- **Giải pháp chuẩn:** Server tải bài hát từ nguồn nhạc chính thống, tự động sử dụng FFmpeg chuyển đổi tức thì thành file PCM nguyên bản (`s16le`, 16kHz, mono) với tên file mã băm an toàn ASCII (`song_{hash}.pcm`). ESP32 kéo file PCM qua HTTP streaming và đưa thẳng vào I2S DMA, cho chất lượng âm thanh trong trẻo, chân thực và mượt mà.

### 11.3. Biểu cảm khuôn mặt Robot hoạt hình (Otto / Vector Robot Animated Emotions)
Giao diện OLED SSD1306 được nâng cấp với bộ biểu cảm vector mượt mà:
- **IDLE:** Mắt robot bo góc lớn, tự động chớp mắt sau mỗi 3 giây, đảo mắt nhìn quanh, nụ cười nhẹ và má hồng.
- **LISTENING:** Mắt mở to tròn chăm chú, các vòng sóng radar âm thanh lan tỏa 2 bên má, 3 chấm nảy sinh động.
- **THINKING:** Mắt liếc lên góc trên bên phải, chùm bong bóng suy nghĩ (thought bubbles) bay bổng.
- **SPEAKING:** Cặp mắt trăng khuyết cười tươi (`⌒ ⌒`), miệng robot đóng mở liên tục theo nhịp điệu giọng nói, phụ đề chạy ở chân màn hình.
- **PLAYING:** Mắt cười nhún nhảy lắc lư theo nhịp beat, các nốt nhạc `♪` `♫` bay bổng lơ lửng, phổ âm thanh 24 cột nhảy múa linh hoạt cùng lời bài hát chạy chữ marquee.
- **ERROR:** Cặp mắt chữ thập buồn (`> <`), miệng mếu.

