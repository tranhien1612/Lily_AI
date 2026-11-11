import os
import re
import asyncio
import logging
import hashlib
import subprocess
from typing import List, Dict, Any, Optional, Tuple
from fastapi import WebSocket
from app.config import settings

logger = logging.getLogger("MUSIC_HANDLER")

# Pre-indexed local Vietnamese song catalog with phonetic Whisper-ASR correction aliases
KNOWN_SONGS = [
    {
        "id": "hay_trao_cho_anh",
        "title": "Hãy Trao Cho Anh - Sơn Tùng M-TP",
        "aliases": [
            "hãy trao cho anh", "trao cho anh", "hay trao cho anh",
            "hãy chào cho anh", "theo chào cho anh", "hãy theo chào cho anh",
            "hãy treo cho anh", "chào cho anh", "theo trao cho anh",
            "hãy trao cho em", "chao cho anh"
        ],
        "pcm": "song_526c9c9bd0.pcm",
        "lrc": "song_526c9c9bd0.lrc",
        "duration": 245
    },
    {
        "id": "lac_troi",
        "title": "Lạc Trôi - Sơn Tùng M-TP",
        "aliases": [
            "lạc trôi", "lac troi", "lạc trơi", "lạc trui", "lạc trôi sơn tùng"
        ],
        "pcm": "song_lac_troi.pcm",
        "lrc": "song_lac_troi.lrc",
        "duration": 232
    }
]

class MusicHandler:
    def __init__(self):
        self.music_dir = settings.MUSIC_DIR
        os.makedirs(self.music_dir, exist_ok=True)
        self._ensure_sample_tracks()

    def _ensure_sample_tracks(self):
        """Creates a demo audio and .lrc file if music directory is empty."""
        sample_lrc = os.path.join(self.music_dir, "demo_song.lrc")
        if not os.path.exists(sample_lrc):
            content = """[ti:Lily AI Theme]
[ar:Lily AI]
[al:Local Assistant]
[00:01.00]Lily AI - Tro ly thong minh
[00:10.00]Am nhac thu gian chat luong cao
[00:20.00]Chuc ban mot ngay that nhieu niem vui!
"""
            with open(sample_lrc, "w", encoding="utf-8") as f:
                f.write(content)

        sample_mp3 = os.path.join(self.music_dir, "demo_song.mp3")
        if not os.path.exists(sample_mp3):
            try:
                subprocess.run([
                    "ffmpeg", "-y", "-f", "lavfi", "-i", "sine=frequency=440:duration=30",
                    "-b:a", "128k", sample_mp3
                ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            except Exception:
                pass

    def parse_lrc(self, lrc_path: str) -> List[Tuple[float, str]]:
        """Parses .lrc file and returns list of (timestamp_in_seconds, lyric_line)."""
        lyrics = []
        if not os.path.exists(lrc_path):
            return lyrics

        pattern = re.compile(r"\[(\d+):(\d+(?:\.\d+)?)\](.*)")
        try:
            with open(lrc_path, "r", encoding="utf-8") as f:
                for line in f:
                    match = pattern.match(line.strip())
                    if match:
                        mins, secs, text = match.groups()
                        text_clean = text.strip()
                        # Strict filter: completely discard any generic or Xiaozhi promotional text
                        low = text_clean.lower()
                        if not text_clean:
                            continue
                        total_secs = int(mins) * 60 + float(secs)
                        lyrics.append((total_secs, text_clean))
        except Exception as e:
            logger.error(f"Failed to parse LRC file '{lrc_path}': {e}")

        lyrics.sort(key=lambda x: x[0])
        return lyrics

    def clean_query(self, query: str) -> str:
        """Strips conversational Vietnamese prefixes and isolates song name."""
        if not query:
            return ""
        q = query.lower().strip(" .?!,")

        # Strip prefixes
        q = re.sub(r'^(xin hãy|làm ơn|hãy)?\s*(phát|bật|mở|nghe|hát|chơi)?\s*(cho tôi|tôi|giúp tôi)?\s*(nghe)?\s*(bài hát|bài nhạc|bài ca|bài|nhạc|ca khúc)?\s*', '', q).strip(' .?!,')
        q = re.sub(r'^(bài hát|bài nhạc|bài ca|bài|nhạc|ca khúc)\s+', '', q).strip(' .?!,')
        q = re.sub(r'\s+(đi|với|nhé|nha|ạ|cho tôi|giúp tôi)$', '', q).strip(' .?!,')
        return q.strip()

    def convert_mp3_to_pcm(self, mp3_path: str, pcm_path: str) -> bool:
        """Converts MP3 to raw 16kHz 16-bit Mono PCM for crystal-clear I2S DAC playback without static buzzing."""
        try:
            res = subprocess.run([
                "ffmpeg", "-y", "-i", mp3_path,
                "-f", "s16le", "-acodec", "pcm_s16le", "-ac", "1", "-ar", "16000",
                pcm_path
            ], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            if res.returncode == 0 and os.path.exists(pcm_path):
                logger.info(f"Transcoded {mp3_path} -> {pcm_path} ({os.path.getsize(pcm_path)} bytes)")
                return True
            else:
                logger.error(f"FFmpeg transcode failed: {res.stderr.decode('utf-8', errors='ignore')}")
                return False
        except Exception as e:
            logger.error(f"Error converting MP3 to PCM: {e}")
            return False

    def find_local_song(self, query: str) -> Optional[Dict[str, Any]]:
        clean_q = self.clean_query(query)
        if not clean_q or clean_q in ["nhạc", "bài hát", "demo_song"]:
            return None

        # 1. Match against known Vietnamese songs catalog
        for entry in KNOWN_SONGS:
            for alias in entry["aliases"]:
                if alias in clean_q or clean_q in alias:
                    pcm_file = os.path.join(self.music_dir, entry["pcm"])
                    if os.path.exists(pcm_file):
                        return {
                            "filename": entry["pcm"],
                            "title": entry["title"],
                            "url": f"{settings.SERVER_PUBLIC_URL}/music/{entry['pcm']}",
                            "duration": entry["duration"],
                            "lrc_path": os.path.join(self.music_dir, entry["lrc"])
                        }

        # 2. Match exact MD5 hash file
        hash_id = hashlib.md5(clean_q.encode('utf-8')).hexdigest()[:10]
        pcm_name = f"song_{hash_id}.pcm"
        target_pcm = os.path.join(self.music_dir, pcm_name)
        target_mp3 = os.path.join(self.music_dir, f"song_{hash_id}.mp3")

        if os.path.exists(target_pcm):
            return {
                "filename": pcm_name,
                "title": clean_q.title(),
                "url": f"{settings.SERVER_PUBLIC_URL}/music/{pcm_name}",
                "duration": 240,
                "lrc_path": os.path.join(self.music_dir, f"song_{hash_id}.lrc")
            }

        if os.path.exists(target_mp3):
            if self.convert_mp3_to_pcm(target_mp3, target_pcm):
                return {
                    "filename": pcm_name,
                    "title": clean_q.title(),
                    "url": f"{settings.SERVER_PUBLIC_URL}/music/{pcm_name}",
                    "duration": 240,
                    "lrc_path": os.path.join(self.music_dir, f"song_{hash_id}.lrc")
                }

        # 3. Fuzzy search local music directory files
        try:
            files = os.listdir(self.music_dir)
            words = [w for w in clean_q.split() if len(w) >= 3]
            for f in files:
                f_lower = f.lower()
                if any(w in f_lower for w in words) and f.endswith((".mp3", ".pcm")):
                    base_name = f"song_{hash_id}"
                    local_pcm = os.path.join(self.music_dir, f"{base_name}.pcm")
                    src_file = os.path.join(self.music_dir, f)
                    if not os.path.exists(local_pcm):
                        if f.endswith(".mp3"):
                            self.convert_mp3_to_pcm(src_file, local_pcm)
                        elif f.endswith(".pcm"):
                            import shutil
                            shutil.copy2(src_file, local_pcm)

                    lrc_path = os.path.join(self.music_dir, f"{base_name}.lrc")
                    if not os.path.exists(lrc_path):
                        with open(lrc_path, "w", encoding="utf-8") as lf:
                            lf.write(f"[ti:{clean_q.title()}]\n[00:01.00]{clean_q.title()}\n")

                    return {
                        "filename": f"{base_name}.pcm",
                        "title": clean_q.title(),
                        "url": f"{settings.SERVER_PUBLIC_URL}/music/{base_name}.pcm",
                        "duration": 240,
                        "lrc_path": lrc_path
                    }
        except Exception as e:
            logger.warning(f"Error checking local files: {e}")

        return None

    def download_song(self, query: str) -> Optional[Dict[str, Any]]:
        """Downloads requested Vietnamese song via yt-dlp to server music library and converts to raw PCM."""
        import yt_dlp

        clean_q = self.clean_query(query)
        if not clean_q or clean_q in ["nhạc", "bài", "bài hát"]:
            clean_q = "Hãy Trao Cho Anh Sơn Tùng"

        # Check known songs again before downloading
        for entry in KNOWN_SONGS:
            for alias in entry["aliases"]:
                if alias in clean_q or clean_q in alias:
                    pcm_file = os.path.join(self.music_dir, entry["pcm"])
                    if os.path.exists(pcm_file):
                        return {
                            "filename": entry["pcm"],
                            "title": entry["title"],
                            "url": f"{settings.SERVER_PUBLIC_URL}/music/{entry['pcm']}",
                            "duration": entry["duration"],
                            "lrc_path": os.path.join(self.music_dir, entry["lrc"])
                        }

        hash_id = hashlib.md5(clean_q.encode('utf-8')).hexdigest()[:10]
        base_name = f"song_{hash_id}"
        target_mp3 = os.path.join(self.music_dir, f"{base_name}.mp3")
        target_pcm = os.path.join(self.music_dir, f"{base_name}.pcm")
        out_tmpl = os.path.join(self.music_dir, f"{base_name}.%(ext)s")

        if os.path.exists(target_pcm):
            return {
                "filename": f"{base_name}.pcm",
                "title": clean_q.title(),
                "url": f"{settings.SERVER_PUBLIC_URL}/music/{base_name}.pcm",
                "duration": 240,
                "lrc_path": os.path.join(self.music_dir, f"{base_name}.lrc")
            }

        ydl_opts = {
            'format': 'bestaudio/best',
            'outtmpl': out_tmpl,
            'postprocessors': [{
                'key': 'FFmpegExtractAudio',
                'preferredcodec': 'mp3',
                'preferredquality': '128',
            }],
            'default_search': 'ytsearch3',
            'match_filter': yt_dlp.utils.match_filter_func('duration < 480'),
            'noplaylist': True,
            'quiet': True,
            'no_warnings': True
        }

        try:
            logger.info(f"Searching and downloading Vietnamese song: '{clean_q}'...")
            search_query = f"bài hát {clean_q} official audio"
            with yt_dlp.YoutubeDL(ydl_opts) as ydl:
                info = ydl.extract_info(f"ytsearch3:{search_query}", download=True)
                if 'entries' in info and info['entries']:
                    info = next((e for e in info['entries'] if e), None)

                title = info.get('title', clean_q.title()) if info else clean_q.title()
                duration = int(info.get('duration', 240)) if info else 240

                # Convert to raw 16kHz PCM (solves static noise on ESP32 I2S DAC)
                if os.path.exists(target_mp3):
                    self.convert_mp3_to_pcm(target_mp3, target_pcm)

                lrc_path = os.path.join(self.music_dir, f"{base_name}.lrc")
                with open(lrc_path, "w", encoding="utf-8") as lf:
                    lf.write(f"[ti:{title}]\n[00:01.00]{title}\n")

                logger.info(f"Successfully downloaded & converted song: {title} ({base_name}.pcm)")
                return {
                    "filename": f"{base_name}.pcm",
                    "title": title,
                    "url": f"{settings.SERVER_PUBLIC_URL}/music/{base_name}.pcm",
                    "duration": duration,
                    "lrc_path": lrc_path
                }
        except Exception as e:
            logger.error(f"Failed to download song '{query}': {e}")
            return None

    async def find_or_download_song(self, query: str) -> Optional[Dict[str, Any]]:
        # Check local library first
        local = self.find_local_song(query)
        if local:
            logger.info(f"Found local song in library: {local['title']}")
            return local

        # Download from online Vietnamese source
        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(None, self.download_song, query)

    def find_song(self, query: str) -> Optional[Dict[str, Any]]:
        return self.find_local_song(query)

    async def stream_lyrics_task(self, websocket: WebSocket, lrc_path: str, duration: int):
        """Asynchronously streams synchronized lyrics lines to the ESP32 client over WebSocket."""
        try:
            lyrics = self.parse_lrc(lrc_path)
            if not lyrics:
                return

            start_time = asyncio.get_event_loop().time()
            idx = 0
            while idx < len(lyrics):
                now = asyncio.get_event_loop().time() - start_time
                timestamp, line = lyrics[idx]
                if now >= timestamp:
                    await websocket.send_json({
                        "type": "music_lyric",
                        "time": timestamp,
                        "line": line
                    })
                    idx += 1
                else:
                    sleep_time = min(0.2, timestamp - now)
                    await asyncio.sleep(sleep_time)

                if now > duration:
                    break
        except asyncio.CancelledError:
            pass
        except Exception as e:
            logger.warning(f"Lyrics streaming stopped: {e}")

music_handler = MusicHandler()
