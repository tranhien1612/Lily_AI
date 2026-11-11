import io
import logging
import numpy as np
from app.config import settings

logger = logging.getLogger("ASR")

class ASREngine:
    def __init__(self):
        self.model = None
        self._init_model()

    def _init_model(self):
        try:
            from faster_whisper import WhisperModel
            logger.info(f"Loading faster-whisper model '{settings.WHISPER_MODEL_SIZE}' on {settings.WHISPER_DEVICE} ({settings.WHISPER_COMPUTE_TYPE})...")
            self.model = WhisperModel(
                settings.WHISPER_MODEL_SIZE,
                device=settings.WHISPER_DEVICE,
                compute_type=settings.WHISPER_COMPUTE_TYPE,
                cpu_threads=4
            )
            logger.info("faster-whisper model loaded successfully.")
        except Exception as e:
            logger.warning(f"Could not load faster-whisper model: {e}. Will use mock/fallback ASR if not available.")
            self.model = None

    async def transcribe(self, pcm_data: bytes) -> str:
        """
        Transcribe raw 16kHz 16-bit Mono PCM audio into text.
        """
        if not pcm_data or len(pcm_data) < 320:
            return ""

        try:
            # Convert int16 PCM bytes to float32 numpy array (-1.0 to 1.0)
            audio_data = np.frombuffer(pcm_data, dtype=np.int16).astype(np.float32) / 32768.0

            if self.model is not None:
                segments, info = self.model.transcribe(
                    audio_data,
                    beam_size=1,
                    language=settings.WHISPER_LANGUAGE,
                    vad_filter=False
                )
                clean_segments = []
                for s in segments:
                    t = s.text.strip()
                    if t and (not clean_segments or t != clean_segments[-1]):
                        clean_segments.append(t)
                text = " ".join(clean_segments).strip()
                logger.info(f"ASR result (lang={info.language}): '{text}'")
                return text
            else:
                logger.warning("ASR model not initialized, returning empty transcript")
                return ""
        except Exception as e:
            logger.error(f"Error during speech transcription: {e}")
            return ""

asr_engine = ASREngine()

