# HƯỚNG DẪN TRIỂN KHAI HỆ THỐNG XIAOZHI CUSTOM CHẠY LOCAL (LAN)

Tài liệu hướng dẫn chi tiết cách triển khai trọn bộ hệ thống trợ lý ảo AI **Xiaozhi Custom** chạy hoàn toàn trong mạng nội bộ (LAN):
- **Firmware:** ESP32-S3 trên mạch tím **XH-S3E N16R8** (16MB Flash, 8MB Octal PSRAM).
- **Server:** Raspberry Pi 4 hoặc Raspberry Pi 5 chạy Local AI Server (ASR Whisper, Ollama LLM, Edge-TTS, MCP Orchestrator, Music Player).

---

## 1. SƠ ĐỒ CHÂN PHẦN CỨNG (XH-S3E N16R8)

| Ngoại vi | Chân tín hiệu | Chân ESP32-S3 | Ghi chú |
| :--- | :--- | :--- | :--- |
| **Loa (I2S TX)** | BCLK (Clock) | **GPIO 15** | Mạch khuếch đại (MAX98357A / NS4168) |
| | LRCK / WS | **GPIO 16** | Word Select |
| | DOUT (Data) | **GPIO 7** | Data Output |
| | AMP_EN | **GPIO 17** | Chân kích hoạt Amp (Mức HIGH để mở) |
| **Micro (I2S RX)**| SCK / BCLK | **GPIO 5** | Micro MEMS (INMP441 / MSM261) |
| | WS / LRCK | **GPIO 4** | Word Select |
| | DIN / SD | **GPIO 6** | Serial Data Input |
| **Màn hình OLED** | SDA (Data) | **GPIO 41** | Màn hình SSD1306 128x64 (I2C) |
| | SCL (Clock) | **GPIO 42** | I2C Clock (Tốc độ 400kHz) |
| **Nút nhấn & LED** | Nút BOOT | **GPIO 0** | Bấm nhanh: Gọi AI / Bấm giữ >3s: Cấu hình WiFi |
| | Nút Vol+ (Tăng âm) | **GPIO 40** | Bấm để tăng âm lượng (+10%), lưu NVS |
| | Nút Vol- (Giảm âm) | **GPIO 39** | Bấm để giảm âm lượng (-10%), lưu NVS |
| | LED Onboard | **GPIO 48** | LED hiển thị trạng thái / Điều khiển qua lệnh MCP |

---

## 2. HƯỚNG DẪN TRIỂN KHAI SERVER TRÊN RASPBERRY PI 4/5

### Bước 2.1: Cài đặt Ollama (LLM Engine)
Trên Raspberry Pi, mở Terminal và chạy lệnh cài đặt Ollama:
```bash
curl -fsSL https://ollama.com/install.sh | sh
```

Khởi động Ollama service và tải mô hình phù hợp cho Raspberry Pi:
```bash
# Tải mô hình Qwen 2.5 1.5B (rất nhẹ, phản hồi tiếng Việt cực nhanh trên RPi 5)
ollama run qwen2.5:1.5b
```

### Bước 2.2: Cài đặt môi trường Python cho Server
Di chuyển vào thư mục server và tạo môi trường ảo:
```bash
cd /home/raspberry/Desktop/xiaozhi/server

# Tạo Virtual Environment
python3 -m venv venv
source venv/bin/activate

# Cài đặt các thư viện cần thiết
pip install -r requirements.txt
```

### Bước 2.3: Cấu hình biến môi trường
Tạo file `.env` trong thư mục `server/`:
```ini
HOST=0.0.0.0
PORT=8000
OLLAMA_HOST=http://127.0.0.1:11434
OLLAMA_MODEL=qwen2.5:1.5b
WHISPER_MODEL_SIZE=base
WHISPER_DEVICE=cpu
WHISPER_COMPUTE_TYPE=int8
WHISPER_LANGUAGE=vi
TTS_VOICE=vi-VN-HoaiMyNeural
SERVER_PUBLIC_URL=http://192.168.1.102:8000
```
*(Thay thế `192.168.1.102` bằng địa chỉ IP LAN thực tế của Raspberry Pi, kiểm tra bằng `ip -br a`)*.

### Bước 2.4: Khởi động Server
```bash
source venv/bin/activate
python3 run.py
```
Server sẽ chạy tại `http://0.0.0.0:8000`. WebSocket endpoint sẵn sàng tại `ws://<ip-raspi>:8000/ws`.

---

## 3. HƯỚNG DẪN BIÊN DỊCH VÀ NẠP FIRMWARE (ESP32-S3)

### Bước 3.1: Yêu cầu môi trường
- ESP-IDF v5.1 trở lên.
- Cáp USB Type-C kết nối bo mạch XH-S3E vào máy tính hoặc Raspberry Pi.

### Bước 3.2: Biên dịch Firmware
Mở terminal trong thư mục `firmware/`:
```bash

sudo apt update
sudo apt install -y git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0

mkdir -p ~/esp
cd ~/esp
git clone -b v5.1.4 --recursive --depth 1 https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
. ~/esp/esp-idf/export.sh
echo "alias get_idf='. \$HOME/esp/esp-idf/export.sh'" >> ~/.bashrc
source ~/.bashrc

cd /home/raspberry/Desktop/xiaozhi/firmware

# Thiết lập target là ESP32-S3
idf.py set-target esp32s3

# Biên dịch toàn bộ dự án
idf.py build
```

### Bước 3.3: Nạp Firmware vào bo mạch XH-S3E
Cắm bo mạch qua cổng USB và chạy lệnh:
```bash
# Nạp code và mở Serial Monitor theo dõi log
idf.py -p /dev/ttyACM0 flash monitor
```
*(Nếu cổng COM là `/dev/ttyUSB0`, hãy thay thế tương ứng).*

---

## 4. QUY TRÌNH SỬ DỤNG VÀ NGHIỆM THU

### 1. Cấu hình WiFi lần đầu (Web Captive Portal):
1. Khi bo mạch chưa có WiFi (hoặc khi **nhấn giữ nút BOOT 3 giây**), màn hình OLED sẽ chuyển sang trạng thái **PROVISIONING**:
   - `WiFi: Xiaozhi-S3-Config`
   - `IP: 192.168.4.1`
2. Dùng điện thoại hoặc máy tính kết nối vào mạng WiFi: `Xiaozhi-S3-Config` (không mật khẩu).
3. Trình duyệt sẽ tự động mở trang cấu hình (hoặc truy cập `http://192.168.4.1`).
4. Bấm **"Quét mạng"**, chọn WiFi nhà bạn, nhập mật khẩu và nhập URL WebSocket (ví dụ: `ws://192.168.1.102:8000/ws`), sau đó bấm **"Lưu & Kết nối"**.
5. Thiết bị tự khởi động lại, kết nối WiFi và chuyển sang màn hình **IDLE** (Mặt cười `^ _ ^`).

### 2. Đánh thức bằng giọng nói (Wakeword):
- Nói *"Hi Lily"* (hoặc *"Lily"*) hoặc bấm nhanh nút **BOOT (GPIO 0)**.
- Thiết bị phát tiếng bíp `♪`, màn hình OLED chuyển sang **LISTENING** (Icon micro rung động).

### 3. Hỏi đáp AI thời gian thực:
- Người dùng nói: *"Hôm nay thời tiết thế nào?"*.
- Thiết bị gửi âm thanh lên Server -> Server giải mã Whisper -> hiển thị `transcript` trên OLED -> LLM sinh câu trả lời -> Server stream giọng nói TTS Hoài My -> Thiết bị phát ra loa và cuộn text câu trả lời.

### 4. Điều khiển GPIO & Âm lượng bằng MCP:
- **Bật/Tắt đèn:**
  - Người dùng nói hoặc chat: *"Bật đèn lên"*.
  - LLM kích hoạt công cụ `gpio_write(pin=48, level=1)`.
  - Server gửi lệnh `mcp_call` xuống ESP32 -> ESP32 bật đèn GPIO 48 -> hiển thị Toast popup -> gửi kết quả về Server -> phát âm thanh xác nhận.
- **Tăng / Giảm âm lượng bằng MCP (Giọng nói hoặc Web):**
  - Người dùng nói hoặc chat: *"Tăng âm lượng"* / *"To lên"* $\rightarrow$ LLM gọi `set_volume(delta=15)` $\rightarrow$ Âm lượng tăng thêm 15%.
  - Người dùng nói hoặc chat: *"Giảm âm lượng"* / *"Nhỏ lại"* $\rightarrow$ LLM gọi `set_volume(delta=-15)` $\rightarrow$ Âm lượng giảm 15%.
  - Người dùng nói hoặc chat: *"Đặt âm lượng 50%"* $\rightarrow$ LLM gọi `set_volume(volume=50)`.
  - Mức âm lượng được lưu vào Flash NVS của ESP32 và tự khôi phục sau khi khởi động lại.
- **Tăng / Giảm âm lượng bằng 2 Nút nhấn vật lý trên mạch XH-S3E:**
  - **Nút Vol+ (GPIO 40):** Bấm để tăng âm lượng (+10%), màn hình OLED hiển thị Popup `Am luong: X%` và phát âm thanh tick xác nhận.
  - **Nút Vol- (GPIO 39):** Bấm để giảm âm lượng (-10%), màn hình OLED hiển thị Popup `Am luong: X%` và phát âm thanh tick xác nhận.

### 5. Phát nhạc trực tuyến Việt Nam + Tải tự động + Lyric + Spectrum Analyzer:
- **Trường hợp 1 - Chưa có tên bài hát:**
  - Người dùng nói: *"Bật nhạc lên"*.
  - Trợ lý hỏi: *"Bạn muốn nghe bài hát nào? Hãy nói tên bài hát cho tôi nhé."*.
  - Người dùng trả lời: *"Hãy Trao Cho Anh"*.
  - Trợ lý: *"Đang tìm bài hát Hãy Trao Cho Anh cho bạn..."*, tự động tải file MP3 về server và bắt đầu phát.
- **Trường hợp 2 - Có sẵn tên bài hát:**
  - Người dùng nói: *"Phát cho tôi bài Lạc Trôi"* (hoặc *"Mở bài Cắt Đôi Nỗi Sầu"*).
  - Trợ lý: Tự động tải bài hát về `/server/music/` và gửi lệnh phát tới ESP32.
- **Hiệu ứng trên ESP32:**
  - OLED hiển thị tên bài hát, lời bài hát chạy cuộn (`music_lyric`) và **24 cột sóng Spectrum** chuyển động theo âm điệu bài hát.

### 6. Quản lý thiết bị, Nghe lại Audio & Chat trực tiếp từ Web (Web Dashboard):
- Mở trình duyệt web trên bất kỳ máy tính hoặc điện thoại nào trong cùng mạng LAN và truy cập:
  ```
  http://<dia_chi_ip_raspberry>:8000/dashboard
  ```
  *(hoặc truy cập trực tiếp trang chủ `http://<dia_chi_ip_raspberry>:8000/`)*.
- **Tính năng trên Dashboard:**
  - **Chỉ hiển thị thiết bị đang trực tuyến (Online-only):** Cột bên trái chỉ liệt kê các mạch Xiaozhi ESP32-S3 đang có kết nối hoạt động với Server (tự động cập nhật mỗi 3 giây).
  - **Đoạn hội thoại chi tiết:** Khi nhấn chuột vào thiết bị, giao diện bên phải hiển thị đầy đủ cuộc trò chuyện giữa thiết bị và server theo thời gian thực.
  - **Đồng bộ chuẩn xác Giọng nói (Voice) và Văn bản (Text):**
    - Người dùng: Hiển thị văn bản nhận diện chính xác từ giọng nói người dùng qua micro kèm khung phát audio `.wav`.
    - Trợ lý Xiaozhi: Hiển thị văn bản trả lời của AI kèm khung phát audio `.wav` giọng đọc đầy đủ.
    - Lệnh điều khiển MCP: Hiển thị nhãn lệnh GPIO hoặc bài nhạc đã thực thi.
  - **Ô Chat trực tiếp từ Web (Thay thế cho nói qua micro ESP32):**
    - Người dùng có thể gõ tin nhắn trực tiếp vào ô chat ở góc dưới màn hình và nhấn `Enter` hoặc nút `🚀 Gửi`.
    - Hệ thống xử lý qua LLM, gửi câu hỏi và cảm xúc lên màn hình OLED của ESP32, thực thi lệnh phần cứng (nếu có) và phát âm thanh câu trả lời ra loa ESP32 đồng thời lưu audio trên Web.
    - Tích hợp sẵn các nút tắt (Quick Chips) thao tác nhanh: `💡 Bật đèn`, `🌑 Tắt đèn`, `🎵 Hãy Trao Cho Anh`, `🎶 Lạc Trôi`, `⏹️ Dừng nhạc`.

