import os
from pydantic_settings import BaseSettings

class Settings(BaseSettings):
    HOST: str = os.getenv("HOST", "0.0.0.0")
    PORT: int = int(os.getenv("PORT", "8000"))
    
    # Ollama LLM Config
    OLLAMA_HOST: str = os.getenv("OLLAMA_HOST", "http://127.0.0.1:11434")
    OLLAMA_MODEL: str = os.getenv("OLLAMA_MODEL", "qwen2.5:1.5b")
    
    # Whisper ASR Config
    WHISPER_MODEL_SIZE: str = os.getenv("WHISPER_MODEL_SIZE", "base")
    WHISPER_DEVICE: str = os.getenv("WHISPER_DEVICE", "cpu")
    WHISPER_COMPUTE_TYPE: str = os.getenv("WHISPER_COMPUTE_TYPE", "int8")
    WHISPER_LANGUAGE: str = os.getenv("WHISPER_LANGUAGE", "vi")
    
    # Edge-TTS Config
    TTS_VOICE: str = os.getenv("TTS_VOICE", "vi-VN-HoaiMyNeural")
    TTS_RATE: str = os.getenv("TTS_RATE", "+0%")
    
    # Music Directory & Server URL Base
    MUSIC_DIR: str = os.getenv("MUSIC_DIR", os.path.join(os.path.dirname(__file__), "..", "music_library"))
    SERVER_PUBLIC_URL: str = os.getenv("SERVER_PUBLIC_URL", "http://192.168.1.102:8000")

    class Config:
        env_file = ".env"
        extra = "allow"

settings = Settings()

