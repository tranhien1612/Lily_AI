async function scanWifi() {
  const select = document.getElementById('wifi-select');
  const btn = document.getElementById('btn-scan');
  btn.disabled = true;
  btn.innerText = "⏳ Đang quét...";
  select.innerHTML = '<option value="">-- Đang quét mạng WiFi... --</option>';

  try {
    const res = await fetch('/api/scan');
    const data = await res.json();
    select.innerHTML = '<option value="">-- Chọn mạng WiFi từ danh sách --</option>';
    if (Array.isArray(data) && data.length > 0) {
      data.forEach(net => {
        const opt = document.createElement('option');
        opt.value = net.ssid;
        opt.innerText = `${net.ssid} (${net.rssi} dBm)`;
        select.appendChild(opt);
      });
    } else {
      select.innerHTML = '<option value="">-- Không tìm thấy mạng WiFi nào --</option>';
    }
  } catch (err) {
    console.error("Scan error:", err);
    select.innerHTML = '<option value="">-- Lỗi khi quét WiFi --</option>';
  } finally {
    btn.disabled = false;
    btn.innerText = "🔄 Quét mạng";
  }
}

function onSelectWifi(val) {
  if (val) {
    document.getElementById('ssid').value = val;
  }
}

async function saveConfig(e) {
  e.preventDefault();
  const ssid = document.getElementById('ssid').value.trim();
  const password = document.getElementById('password').value;
  const ws_url = document.getElementById('ws_url').value.trim();
  const statusDiv = document.getElementById('status');
  const btn = document.getElementById('btn-save');

  if (!ssid || !ws_url) {
    statusDiv.className = "status-msg status-error";
    statusDiv.innerText = "Vui lòng nhập đầy đủ SSID và Địa chỉ WebSocket Server!";
    return;
  }

  btn.disabled = true;
  btn.innerText = "⏳ Đang lưu cấu hình...";

  try {
    const res = await fetch('/api/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid, password, ws_url })
    });
    const result = await res.json();
    if (result.status === 'ok') {
      statusDiv.className = "status-msg status-success";
      statusDiv.innerText = "✅ Đã lưu cấu hình thành công! Thiết bị đang khởi động lại và kết nối...";
    } else {
      statusDiv.className = "status-msg status-error";
      statusDiv.innerText = "❌ Lỗi: " + (result.message || "Không thể lưu cấu hình");
      btn.disabled = false;
      btn.innerText = "💾 Lưu & Kết nối";
    }
  } catch (err) {
    statusDiv.className = "status-msg status-error";
    statusDiv.innerText = "❌ Lỗi kết nối tới thiết bị!";
    btn.disabled = false;
    btn.innerText = "💾 Lưu & Kết nối";
  }
}

window.onload = () => {
  scanWifi();
};

