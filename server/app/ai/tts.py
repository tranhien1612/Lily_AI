import asyncio
import io
import math
import struct
import logging
from typing import AsyncGenerator
import edge_tts
from app.config import settings

logger = logging.getLogger("TTS")

class TTSEngine:
    def __init__(self):
        self.voice = settings.TTS_VOICE
        self.fallback_voice = "vi-VN-NamMinhNeural"

    def _generate_fallback_beep(self, duration_sec: float = 0.5) -> bytes:
        """Generates simple 800Hz PCM tone if TTS network fails."""
        sample_rate = 16000
        num_samples = int(sample_rate * duration_sec)
        pcm = bytearray()
        for i in range(num_samples):
            val = int(math.sin(2 * math.pi * 800 * i / sample_rate) * 8000)
            pcm.extend(struct.pack("<h", val))
        return bytes(pcm)

    async def _fetch_mp3(self, text: str, voice: str) -> bytes:
        rate = settings.TTS_RATE if settings.TTS_RATE not in ["", "+0%"] else None
        communicate = edge_tts.Communicate(text, voice, rate=rate) if rate else edge_tts.Communicate(text, voice)
        mp3_io = io.BytesIO()
        async for chunk in communicate.stream():
            if chunk["type"] == "audio":
                mp3_io.write(chunk["data"])
        return mp3_io.getvalue()

    def _fetch_gtts_mp3(self, text: str) -> bytes:
        """Fallback to Google TTS for 100% reliable Vietnamese voice synthesis."""
        try:
            from gtts import gTTS
            fp = io.BytesIO()
            tts = gTTS(text=text, lang="vi", slow=False)
            tts.write_to_fp(fp)
            return fp.getvalue()
        except Exception as e:
            logger.error(f"gTTS fallback failed: {e}")
            return b""

    async def text_to_pcm_stream(self, text: str, chunk_size: int = 1024) -> AsyncGenerator[bytes, None]:
        """
        Synthesizes text into raw 16kHz 16-bit Mono PCM chunks.
        Uses ultra-fast, high-reliability Vietnamese TTS with fallback.
        """
        if not text or len(text.strip()) == 0:
            return

        clean_text = text.replace("*", "").replace("#", "").replace('"', '').strip()
        logger.info(f"Synthesizing TTS: '{clean_text}'")

        mp3_data = b""
        # Primary: Google TTS (fast, reliable, never throws NoAudioReceived for Vietnamese)
        try:
            loop = asyncio.get_running_loop()
            mp3_data = await asyncio.wait_for(
                loop.run_in_executor(None, self._fetch_gtts_mp3, clean_text),
                timeout=4.0
            )
            if mp3_data:
                logger.info(f"Generated {len(mp3_data)} bytes of MP3 via gTTS")
        except Exception as e:
            logger.warning(f"gTTS failed: {e}. Trying Edge-TTS fallback...")

        # Fallback: Edge-TTS
        if not mp3_data:
            try:
                mp3_data = await asyncio.wait_for(self._fetch_mp3(clean_text, self.voice), timeout=3.0)
            except Exception as e2:
                logger.warning(f"Edge-TTS failed: {e2}")

        if not mp3_data:
            logger.warning("All TTS engines failed. Yielding fallback notification tone.")
            beep_pcm = self._generate_fallback_beep(0.4)
            yield beep_pcm
            return

        try:
            # Fast in-memory conversion to raw PCM 16kHz 16-bit Mono using ffmpeg
            process = await asyncio.create_subprocess_exec(
                "ffmpeg",
                "-i", "pipe:0",
                "-f", "s16le",
                "-acodec", "pcm_s16le",
                "-ac", "1",
                "-ar", "16000",
                "pipe:1",
                stdin=asyncio.subprocess.PIPE,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.DEVNULL
            )
            stdout_data, _ = await process.communicate(input=mp3_data)
            logger.info(f"Generated {len(stdout_data)} bytes of PCM audio")

            for i in range(0, len(stdout_data), chunk_size):
                yield stdout_data[i:i + chunk_size]
        except Exception as e:
            logger.error(f"Error converting MP3 to PCM: {e}")

tts_engine = TTSEngine()
