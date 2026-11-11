import os
import json
import time
import uuid
import wave
import logging
from datetime import datetime
from typing import Dict, List, Any, Optional
from fastapi import WebSocket

logger = logging.getLogger("SESSION_MGR")

RECORDINGS_DIR = os.path.join(os.path.dirname(__file__), "..", "recordings")
STORAGE_FILE = os.path.join(os.path.dirname(__file__), "..", "devices_data.json")
os.makedirs(RECORDINGS_DIR, exist_ok=True)

def save_pcm_to_wav(pcm_bytes: bytes, filename: str, sample_rate: int = 16000) -> Optional[str]:
    """Saves raw 16kHz 16-bit Mono PCM bytes as a .wav file and returns relative URL."""
    if not pcm_bytes or len(pcm_bytes) < 320:
        return None
    try:
        file_path = os.path.join(RECORDINGS_DIR, filename)
        with wave.open(file_path, "wb") as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)
            wf.setframerate(sample_rate)
            wf.writeframes(pcm_bytes)
        return f"/recordings/{filename}"
    except Exception as e:
        logger.error(f"Failed to save audio file {filename}: {e}")
        return None

class DeviceRecord:
    def __init__(self, device_id: str, ip: str, name: Optional[str] = None):
        self.device_id: str = device_id
        self.ip: str = ip
        self.name: str = "Xiaozhi S3"
        self.is_online: bool = False
        self.session_id: Optional[str] = None
        now_str = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        self.first_seen: str = now_str
        self.last_seen: str = now_str
        self.messages: List[Dict[str, Any]] = []

    def to_dict(self) -> Dict[str, Any]:
        return {
            "device_id": self.device_id,
            "name": self.name,
            "ip": self.ip,
            "is_online": self.is_online,
            "session_id": self.session_id,
            "first_seen": self.first_seen,
            "last_seen": self.last_seen,
            "message_count": len(self.messages),
            "last_message": self.messages[-1]["text"] if self.messages else "Chưa có tin nhắn"
        }

class DeviceSession:
    def __init__(self, websocket: WebSocket, device_id: str, ip: str):
        self.session_id: str = str(uuid.uuid4())
        self.websocket: WebSocket = websocket
        self.device_id: str = device_id
        self.ip: str = ip
        self.is_ready: bool = False
        self.history: List[Dict[str, Any]] = []  # LLM conversation window
        self.tools: List[Dict[str, Any]] = []
        self.is_recording: bool = False
        self.current_music_task: Optional[Any] = None

    def add_history(self, role: str, content: str):
        self.history.append({"role": role, "content": content})
        if len(self.history) > 10:
            self.history = self.history[-10:]

class SessionManager:
    def __init__(self):
        self.active_sessions: Dict[str, DeviceSession] = {} # session_id -> DeviceSession
        self.devices: Dict[str, DeviceRecord] = {}           # device_id -> DeviceRecord
        self._load_storage()

    def _load_storage(self):
        if os.path.exists(STORAGE_FILE):
            try:
                with open(STORAGE_FILE, "r", encoding="utf-8") as f:
                    data = json.load(f)
                    for d_id, d_data in data.items():
                        rec = DeviceRecord(d_id, d_data.get("ip", "unknown"), d_data.get("name"))
                        rec.first_seen = d_data.get("first_seen", "")
                        rec.last_seen = d_data.get("last_seen", "")
                        rec.messages = d_data.get("messages", [])
                        rec.is_online = False
                        self.devices[d_id] = rec
                logger.info(f"Loaded {len(self.devices)} registered devices from database.")
            except Exception as e:
                logger.warning(f"Could not load devices database: {e}")

    def _save_storage(self):
        try:
            data = {}
            for d_id, rec in self.devices.items():
                data[d_id] = {
                    "device_id": rec.device_id,
                    "name": rec.name,
                    "ip": rec.ip,
                    "first_seen": rec.first_seen,
                    "last_seen": rec.last_seen,
                    "messages": rec.messages[-50:] # Keep last 50 messages per device
                }
            with open(STORAGE_FILE, "w", encoding="utf-8") as f:
                json.dump(data, f, ensure_ascii=False, indent=2)
        except Exception as e:
            logger.error(f"Could not save devices database: {e}")

    def register_connection(self, websocket: WebSocket) -> DeviceSession:
        client_host = websocket.client.host if websocket.client else "127.0.0.1"
        device_id = f"esp32_{client_host.replace('.', '_')}"

        if device_id not in self.devices:
            self.devices[device_id] = DeviceRecord(device_id, client_host)
        
        dev = self.devices[device_id]
        dev.is_online = True
        dev.last_seen = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        session = DeviceSession(websocket, device_id, client_host)
        dev.session_id = session.session_id
        self.active_sessions[session.session_id] = session
        self._save_storage()
        return session

    def unregister_connection(self, session_id: str):
        if session_id in self.active_sessions:
            session = self.active_sessions[session_id]
            if session.device_id in self.devices:
                dev = self.devices[session.device_id]
                dev.is_online = False
                dev.last_seen = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                dev.session_id = None
            del self.active_sessions[session_id]
            self._save_storage()

    def get_session(self, session_id: str) -> Optional[DeviceSession]:
        return self.active_sessions.get(session_id)

    def add_chat_message(
        self,
        device_id: str,
        sender: str,
        text: str,
        audio_bytes: Optional[bytes] = None,
        tool_call: Optional[Dict[str, Any]] = None
    ) -> Dict[str, Any]:
        """Records a user or bot message with optional audio file."""
        if device_id not in self.devices:
            return {}

        now = datetime.now()
        ts_id = int(time.time() * 1000)

        msg = {
            "id": f"msg_{ts_id}",
            "sender": sender, # "user" or "lily"
            "text": text,
            "timestamp": now.strftime("%H:%M:%S"),
            "date": now.strftime("%Y-%m-%d"),
            "tool_call": tool_call
        }

        self.devices[device_id].messages.append(msg)
        self.devices[device_id].last_seen = now.strftime("%Y-%m-%d %H:%M:%S")
        self._save_storage()
        return msg

    def get_all_devices(self, online_only: bool = True) -> List[Dict[str, Any]]:
        devs = self.devices.values()
        if online_only:
            devs = [d for d in devs if d.is_online]
        return [d.to_dict() for d in devs]

    def get_device_messages(self, device_id: str) -> List[Dict[str, Any]]:
        if device_id in self.devices:
            return self.devices[device_id].messages
        return []

session_manager = SessionManager()
