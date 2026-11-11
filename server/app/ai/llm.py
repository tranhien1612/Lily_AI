import json
import logging
from typing import List, Dict, Any, Tuple, Optional
import httpx
from app.config import settings

logger = logging.getLogger("LLM")

SYSTEM_PROMPT = """Bạn là Lily, một trợ lý ảo thông minh, thân thiện và chu đáo chạy trên máy chủ cục bộ và mạch ESP32-S3.
Nhiệm vụ của bạn:
1. Khi người dùng hỏi về thông tin thực tế (thời tiết, giá vàng, tin tức mới nhất, điều khiển thiết bị, phát nhạc), hãy gọi tool tương ứng hoặc trả lời dựa trên thông tin đã cung cấp.
2. Trả lời theo ngữ cảnh, trò chuyện tự nhiên, vui vẻ, dí dỏm và thân thiện (độ dài khoảng 2 đến 4 câu văn liền mạch). Có thể kèm theo lời khuyên hoặc gợi ý hữu ích phù hợp với ngữ cảnh để tạo cảm giác trò chuyện gần gũi.
3. Tuyệt đối không dùng dấu sao (*), thăng (#), bullet hay bảng biểu để đảm bảo giọng đọc TTS mượt mà.
"""

class LLMEngine:
    def __init__(self):
        self.host = settings.OLLAMA_HOST
        self.model = settings.OLLAMA_MODEL

    def extract_music_intent(self, text: str, messages: List[Dict[str, Any]]) -> Tuple[Optional[str], Optional[str]]:
        import re
        text_clean = text.lower().strip(" .?!,")

        # 1. Stop music
        if any(w in text_clean for w in ["dừng nhạc", "tắt nhạc", "ngừng nhạc", "dừng bài hát", "ngưng nhạc"]):
            return "stop", None

        # 2. Check if previous turn asked for song title
        if messages and len(messages) >= 2:
            prev_turn = messages[-2] if messages[-1].get("role") == "user" else messages[-1]
            content = prev_turn.get("content", "").lower()
            if "bạn muốn nghe bài hát nào" in content or "tên bài hát" in content:
                title = re.sub(r'^(bài|bài hát|cho tôi bài|phát bài|mở bài|nghe bài)\s+', '', text_clean).strip(" .?!,")
                if title and title not in ["không", "thôi", "bỏ qua"]:
                    return "play", title

        # Common Whisper misrecognitions of "bật nhạc"
        if text_clean in ["rất nhạc", "bát nhạc", "bặt nhạc", "bật nhạc", "mở nhạc", "phát nhạc", "nghe nhạc", "hát nhạc"]:
            return "ask_title", None

        # 3. Triggers for playing music
        play_triggers = [
            "phát cho tôi", "mở cho tôi", "bật cho tôi", "mở tôi", "bật tôi", "phát tôi",
            "phát bài hát", "mở bài hát", "bật bài hát",
            "phát bài nhạc", "mở bài nhạc", "bật bài nhạc",
            "phát bài", "mở bài", "bật bài",
            "phát nhạc", "mở nhạc", "bật nhạc",
            "nghe bài hát", "nghe bài nhạc", "nghe bài", "nghe nhạc",
            "hát bài", "hát đi", "tôi muốn nghe nhạc", "cho tôi nghe nhạc",
            "hãy trao cho anh", "hãy chào cho anh", "theo chào cho anh", "hãy theo chào cho anh",
            "lạc trôi"
        ]

        has_trigger = any(t in text_clean for t in play_triggers) or "bài hát" in text_clean or "bài nhạc" in text_clean
        if not has_trigger:
            return None, None

        # Strip prefixes
        cleaned = re.sub(
            r'^(xin hãy|làm ơn|hãy)?\s*(phát|bật|mở|nghe|hát|chơi)?\s*(cho tôi|tôi|giúp tôi)?\s*(nghe)?\s*(bài nhạc|bài hát|bài ca|bài|nhạc|ca khúc)?\s*',
            '',
            text_clean
        ).strip(' .?!,')
        cleaned = re.sub(r'^(bài hát|bài nhạc|bài ca|bài|nhạc|ca khúc)\s+', '', cleaned).strip(' .?!,')
        cleaned = re.sub(r'\s+(đi|với|nhé|nha|ạ|cho tôi|giúp tôi)$', '', cleaned).strip(' .?!,')

        if not cleaned or cleaned in ['nhạc', 'bài', 'bài hát', 'bài nhạc', 'nào đó', 'gì đó', '']:
            return "ask_title", None

        return "play", cleaned

    async def chat(
        self,
        messages: List[Dict[str, Any]],
        tools: Optional[List[Dict[str, Any]]] = None
    ) -> Tuple[str, Optional[Dict[str, Any]]]:
        """
        Chat with Ollama LLM with fast-path for smart speaker commands.
        """
        last_user_msg = messages[-1]["content"].lower().strip() if messages else ""

        # --- Fast Intent Path (Instant response < 10ms for common commands) ---
        if any(w in last_user_msg for w in ["bật đèn", "mở đèn", "sáng đèn"]):
            logger.info("Fast-path triggered: gpio_write (LED ON)")
            return "Đã bật đèn cho bạn.", {"name": "gpio_write", "arguments": {"pin": 48, "level": 1}}

        if any(w in last_user_msg for w in ["tắt đèn", "đóng đèn"]):
            logger.info("Fast-path triggered: gpio_write (LED OFF)")
            return "Đã tắt đèn.", {"name": "gpio_write", "arguments": {"pin": 48, "level": 0}}

        # Volume Control Fast-Path
        if any(w in last_user_msg for w in ["tăng âm lượng", "to lên", "bật to lên", "cho to lên", "tăng loa", "to hơn"]):
            logger.info("Fast-path triggered: set_volume (delta=+15)")
            return "Đang tăng âm lượng loa.", {"name": "set_volume", "arguments": {"delta": 15}}

        if any(w in last_user_msg for w in ["giảm âm lượng", "nhỏ lại", "bật nhỏ lại", "cho nhỏ lại", "giảm loa", "nhỏ hơn"]):
            logger.info("Fast-path triggered: set_volume (delta=-15)")
            return "Đang giảm âm lượng loa.", {"name": "set_volume", "arguments": {"delta": -15}}

        if any(w in last_user_msg for w in ["tắt tiếng", "mute", "im lặng đi"]):
            logger.info("Fast-path triggered: set_volume (volume=0)")
            return "Đã tắt tiếng loa.", {"name": "set_volume", "arguments": {"volume": 0}}

        import re
        vol_match = re.search(r'âm lượng\s+(\d+)', last_user_msg)
        if vol_match:
            vol_val = int(vol_match.group(1))
            if 0 <= vol_val <= 100:
                logger.info(f"Fast-path triggered: set_volume (volume={vol_val})")
                return f"Đang đặt âm lượng {vol_val} phần trăm.", {"name": "set_volume", "arguments": {"volume": vol_val}}

        # Music Control Fast-Path
        music_intent, song_title = self.extract_music_intent(last_user_msg, messages)
        if music_intent == "stop":
            logger.info("Fast-path triggered: music_stop")
            return "Đã dừng nhạc.", {"name": "music_stop", "arguments": {}}
        elif music_intent == "ask_title":
            logger.info("Fast-path triggered: ask_title for music")
            return "Bạn muốn nghe bài hát nào? Hãy nói tên bài hát cho tôi nhé.", None
        elif music_intent == "play":
            logger.info(f"Fast-path triggered: music_play for '{song_title}'")
            return f"Đang tìm bài hát {song_title} cho bạn.", {"name": "music_play", "arguments": {"title": song_title}}

        # Greetings fast-path for low latency
        if last_user_msg in ["xin chào", "chào", "chào bạn", "hello", "hi"]:
            return "Chào bạn! Tôi có thể giúp gì cho bạn hôm nay?", None

        # Weather Fast-Path
        if any(w in last_user_msg for w in ["thời tiết", "nhiệt độ", "mưa hay nắng", "trời mưa không", "trời nắng không"]):
            loc = "Hà Nội"
            loc_match = re.search(r'(?:ở|tại|tỉnh|thành phố|tp\.?)\s+([a-zA-ZÀ-ỹ\s]+)', last_user_msg)
            if loc_match:
                loc = loc_match.group(1).strip(" .?!,")
            else:
                for city in ["hà nội", "hồ chí minh", "sài gòn", "đà nẵng", "hải phòng", "cần thơ", "nha trang", "huế", "đà lạt"]:
                    if city in last_user_msg:
                        loc = city
                        break
            loc = re.sub(r'\s+(như thế nào|thế nào|sao|hôm nay|ngày mai|hả bạn|nhỉ|ạ)$', '', loc).strip()
            if not loc:
                loc = "Hà Nội"
            logger.info(f"Fast-path triggered: get_weather (location={loc})")
            return f"Đang tra cứu thời tiết tại {loc.title()}...", {"name": "get_weather", "arguments": {"location": loc.title()}}

        # Gold & Silver Price Fast-Path
        if any(w in last_user_msg for w in ["giá vàng", "giá bạc", "vàng doji", "vàng pnj", "vàng bảo tín", "vàng nhẫn", "vàng miếng", "vàng sjc", "giá sjc"]):
            flt = ""
            if "bạc" in last_user_msg:
                flt = "bạc"
            elif "doji" in last_user_msg:
                flt = "doji"
            elif "pnj" in last_user_msg:
                flt = "pnj"
            elif "bảo tín" in last_user_msg or "minh châu" in last_user_msg:
                flt = "bảo tín"
            elif "nhẫn" in last_user_msg:
                flt = "nhẫn"
            elif "miếng" in last_user_msg:
                flt = "miếng"

            logger.info(f"Fast-path triggered: get_gold_price (filter={flt})")
            return "Đang tra cứu bảng giá kim hoàn hôm nay...", {"name": "get_gold_price", "arguments": {"filter_type": flt}}

        # Latest News Fast-Path
        if any(w in last_user_msg for w in ["tin tức", "đọc tin tức", "tin mới nhất", "tin hôm nay", "bản tin", "thời sự hôm nay"]):
            logger.info("Fast-path triggered: get_latest_news")
            return "Đang lấy các tin tức mới nhất...", {"name": "get_latest_news", "arguments": {}}

        # --- Ollama AI Query Path (Optimized for RPi 5 CPU) ---
        full_messages = [{"role": "system", "content": SYSTEM_PROMPT}] + messages

        payload: Dict[str, Any] = {
            "model": self.model,
            "messages": full_messages,
            "stream": False,
            "options": {
                "temperature": 0.4,
                "num_predict": 180,
                "top_k": 20,
                "top_p": 0.9,
            }
        }
        if tools:
            payload["tools"] = tools

        try:
            async with httpx.AsyncClient(timeout=30.0) as client:
                res = await client.post(f"{self.host}/api/chat", json=payload)
                if res.status_code == 200:
                    data = res.json()
                    msg = data.get("message", {})
                    content = msg.get("content", "").strip()

                    # Handle Ollama tool calls
                    tool_calls = msg.get("tool_calls", [])
                    if tool_calls:
                        tc = tool_calls[0]
                        fn = tc.get("function", {})
                        fn_name = fn.get("name")
                        fn_args = fn.get("arguments", {})
                        if isinstance(fn_args, str):
                            try:
                                fn_args = json.loads(fn_args)
                            except Exception:
                                fn_args = {}
                        return content, {"name": fn_name, "arguments": fn_args}

                    return content, None
                else:
                    logger.error(f"Ollama returned HTTP {res.status_code}: {res.text}")
                    return "Xin lỗi, tôi đang gặp chút sự cố khi xử lý câu hỏi của bạn.", None
        except Exception as e:
            logger.error(f"Error querying Ollama LLM: {e}")
            return "Xin lỗi, hiện tại tôi không thể kết nối tới mô hình AI.", None

    async def synthesize_dynamic_response(self, user_query: str, factual_data: str) -> str:
        """
        Uses LLM to dynamically synthesize a natural, flexible, varied response
        based on real-time factual data instead of a rigid fixed template.
        Supports rich, conversational multi-sentence replies (2-4 sentences) with helpful context.
        """
        prompt = (
            f"Thông tin thực tế: {factual_data}\n"
            f"Câu hỏi của người dùng: \"{user_query}\"\n\n"
            f"Dựa vào thông tin thực tế ở trên, hãy đóng vai trợ lý Lily trả lời trực tiếp cho người dùng. "
            f"Nói bằng tiếng Việt tự nhiên, vui vẻ, dí dỏm, thân thiện và liền mạch (độ dài khoảng 2 đến 4 câu văn). "
            f"Trình bày rõ các điểm chính từ thông tin trên, kèm lời bình hoặc lời khuyên hữu ích phù hợp ngữ cảnh. "
            f"Tuyệt đối không thêm tiền tố tên người nói như \"Lily:\", không dùng bullet hay dấu hoa thị (*), thăng (#):\n"
            f"Trả lời:"
        )
        try:
            payload = {
                "model": self.model,
                "prompt": prompt,
                "stream": False,
                "options": {
                    "temperature": 0.65,
                    "num_predict": 180,
                    "top_p": 0.9,
                }
            }
            async with httpx.AsyncClient(timeout=25.0) as client:
                res = await client.post(f"{self.host}/api/generate", json=payload)
                if res.status_code == 200:
                    data = res.json()
                    answer = data.get("response", "").strip()
                    import re
                    answer = re.sub(r'^(?:Lily|Trợ lý|Assistant)\s*:\s*', '', answer, flags=re.I).strip()
                    answer = answer.replace("*", "").replace("#", "").replace("\n", " ").strip()
                    sentence_match = re.search(r'^(.*[.!?])', answer, re.DOTALL)
                    if sentence_match:
                        answer = sentence_match.group(1).strip()
                    if len(answer) > 10:
                        logger.info(f"LLM dynamically synthesized reply: '{answer}'")
                        return answer
        except Exception as e:
            logger.warning(f"Dynamic LLM synthesis timed out or failed: {e}. Using factual fallback.")

        # Fallback to direct factual data
        return factual_data

    async def synthesize_dynamic_response(self, user_query: str, factual_data: str) -> str:
        """
        Uses LLM to dynamically synthesize a natural, flexible, varied response
        based on real-time factual data instead of a rigid fixed template.
        """
        prompt = (
            f"Thông tin thực tế: {factual_data}\n"
            f"Câu hỏi của người dùng: \"{user_query}\"\n\n"
            f"Dựa vào thông tin thực tế ở trên, hãy đóng vai trợ lý Lily trả lời trực tiếp cho người dùng. "
            f"Nói bằng tiếng Việt tự nhiên, vui vẻ, dí dỏm, thân thiện và liền mạch (độ dài khoảng 2 đến 4 câu văn). "
            f"Trình bày rõ các điểm chính từ thông tin trên, kèm lời bình hoặc lời khuyên hữu ích phù hợp ngữ cảnh. "
            f"Tuyệt đối không thêm tiền tố tên người nói như \"Lily:\", không dùng bullet hay dấu hoa thị (*), thăng (#):\n"
            f"Trả lời:"
        )
        try:
            payload = {
                "model": self.model,
                "prompt": prompt,
                "stream": False,
                "options": {
                    "temperature": 0.65,
                    "num_predict": 180,
                    "top_p": 0.9,
                }
            }
            async with httpx.AsyncClient(timeout=25.0) as client:
                res = await client.post(f"{self.host}/api/generate", json=payload)
                if res.status_code == 200:
                    data = res.json()
                    answer = data.get("response", "").strip()
                    import re
                    answer = re.sub(r'^(?:Lily|Trợ lý|Assistant)\s*:\s*', '', answer, flags=re.I).strip()
                    answer = answer.replace("*", "").replace("#", "").replace("\n", " ").strip()
                    sentence_match = re.search(r'^(.*[.!?])', answer, re.DOTALL)
                    if sentence_match:
                        answer = sentence_match.group(1).strip()
                    if len(answer) > 10:
                        logger.info(f"LLM dynamically synthesized reply: '{answer}'")
                        return answer
        except Exception as e:
            logger.warning(f"Dynamic LLM synthesis timed out or failed: {e}. Using factual fallback.")

        # Fallback to direct factual data
        return factual_data

llm_engine = LLMEngine()
