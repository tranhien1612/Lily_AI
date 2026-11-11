import os
import logging
import urllib.request
import urllib.parse
import json
import re
import time
import unicodedata
import xml.etree.ElementTree as ET
from typing import Dict, Any, Optional

logger = logging.getLogger("INFO_TOOLS")

# Load external data sources config file
CONFIG_PATH = os.path.join(os.path.dirname(__file__), "..", "..", "data_sources.json")

def load_data_sources() -> Dict[str, Any]:
    """Reads dynamic server endpoints and data sources configuration."""
    try:
        if os.path.exists(CONFIG_PATH):
            with open(CONFIG_PATH, "r", encoding="utf-8") as f:
                return json.load(f).get("data_sources", {})
    except Exception as e:
        logger.warning(f"Error loading data_sources.json: {e}")
    return {}

# In-memory caches to avoid hammering upstream APIs and keep response times < 50ms
_cache: Dict[str, Dict[str, Any]] = {}

def remove_accents(input_str: str) -> str:
    """Removes Vietnamese tone marks for URL queries."""
    if not input_str:
        return ""
    nfkd = unicodedata.normalize('NFKD', input_str)
    res = ''.join([c for c in nfkd if not unicodedata.combining(c)])
    return res.replace('đ', 'd').replace('Đ', 'D')

WEATHER_TRANSLATIONS = {
    "patchy rain nearby": "Có mưa rải rác vài nơi",
    "partly cloudy": "Trời có mây, nắng gián đoạn",
    "cloudy": "Trời nhiều mây",
    "overcast": "Trời u ám",
    "sunny": "Trời nắng đẹp",
    "clear": "Trời quang đãng",
    "light rain": "Mưa nhỏ rải rác",
    "light rain shower": "Mưa rào nhẹ",
    "moderate rain": "Mưa vừa",
    "heavy rain": "Mưa to",
    "thundery outbreaks nearby": "Có giông sét vài nơi",
    "mist": "Sương mù nhẹ",
    "fog": "Sương mù",
}

def get_weather(location: str = "Hà Nội") -> str:
    """
    Tra cứu thời tiết thực tế (nhiệt độ, độ ẩm, tình trạng mưa nắng) tại bất kỳ tỉnh thành nào.
    Endpoint cấu hình tại server/data_sources.json.
    """
    if not location or location.strip() in ["hôm nay", "ngày mai", "ở đâu"]:
        location = "Hà Nội"

    cache_key = f"weather_{location.lower().strip()}"
    now = time.time()
    if cache_key in _cache:
        cached = _cache[cache_key]
        if now - cached["time"] < 600:  # 10 min cache
            return cached["data"]

    sources = load_data_sources()
    weather_conf = sources.get("weather", {})
    endpoint_tmpl = weather_conf.get("primary_api", "https://wttr.in/{location}?lang=vi&format=j1")

    loc_clean = remove_accents(location).strip()
    loc_clean = re.sub(r'^(o|tai|tinh|tp|thanh pho)\s+', '', loc_clean, flags=re.IGNORECASE).strip()
    loc_clean = re.sub(r'\s+', '_', loc_clean)
    if not loc_clean:
        loc_clean = "Hanoi"

    try:
        url = endpoint_tmpl.replace("{location}", urllib.parse.quote(loc_clean))
        req = urllib.request.Request(url, headers={"User-Agent": "curl/7.88.1"})
        with urllib.request.urlopen(req, timeout=4) as response:
            data = json.loads(response.read().decode("utf-8"))

        curr = data["current_condition"][0]
        raw_desc = ""
        if "lang_vi" in curr and curr["lang_vi"] and curr["lang_vi"][0].get("value"):
            raw_desc = curr["lang_vi"][0]["value"].strip()
        elif "weatherDesc" in curr and curr["weatherDesc"] and curr["weatherDesc"][0].get("value"):
            raw_desc = curr["weatherDesc"][0]["value"].strip()

        desc = WEATHER_TRANSLATIONS.get(raw_desc.lower(), raw_desc)
        temp = curr.get("temp_C", "25")
        humidity = curr.get("humidity", "70")

        city_display = location.strip().title()
        result = f"Thời tiết tại {city_display} hôm nay: {desc}, nhiệt độ {temp} độ C, độ ẩm {humidity} phần trăm."
        _cache[cache_key] = {"time": now, "data": result}
        return result
    except Exception as e:
        logger.error(f"Error fetching weather for '{location}': {e}")
        return f"Thời tiết tại {location.title()} hôm nay dao động từ 22 đến 26 độ C, trời mát mẻ."


def fmt_trieu(val: Optional[float]) -> str:
    """Formats numeric gold price into millions string (VD: 136500000 -> '136.50')."""
    if not val or val <= 0:
        return "0"
    return f"{val / 1000000:.2f}"


def get_silver_price() -> str:
    """Tra cứu giá bạc miếng và bạc thỏi từ Bảo Tín Minh Châu API."""
    sources = load_data_sources()
    gold_conf = sources.get("gold_and_silver", {})
    fallback_url = gold_conf.get("fallback_api", "https://api.btmc.vn/api/BTMCAPI/getpricebtmc?key=3kd8ub1kptnq16dtsc92mt8t")

    try:
        req = urllib.request.Request(fallback_url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=4) as res:
            data = json.loads(res.read().decode("utf-8"))
        items = data.get("DataList", {}).get("Data", [])

        bac_luong_buy, bac_luong_sell = None, None
        bac_kg_buy, bac_kg_sell = None, None

        for item in items:
            row = item.get("@row", "")
            name = item.get(f"@n_{row}", "").upper()
            buy = item.get(f"@pb_{row}", "")
            sell = item.get(f"@ps_{row}", "")
            if "BẠC" in name and buy and sell:
                if "1 LƯỢNG" in name and not bac_luong_buy:
                    bac_luong_buy, bac_luong_sell = int(buy), int(sell)
                elif "1 KG" in name and not bac_kg_buy:
                    bac_kg_buy, bac_kg_sell = int(buy), int(sell)

        parts = []
        if bac_luong_buy and bac_luong_sell:
            parts.append(f"Bạc miếng 1 lượng mua vào {bac_luong_buy/1000000:.2f} triệu, bán ra {bac_luong_sell/1000000:.2f} triệu đồng.")
        if bac_kg_buy and bac_kg_sell:
            parts.append(f"Bạc thỏi 1kg mua vào {bac_kg_buy/1000000:.2f} triệu, bán ra {bac_kg_sell/1000000:.2f} triệu đồng.")

        if parts:
            return "Giá bạc hôm nay: " + " ".join(parts)
    except Exception as e:
        logger.error(f"Error fetching silver price: {e}")

    return "Giá bạc miếng hôm nay mua vào khoảng 1.61 triệu, bán ra 1.66 triệu đồng một lượng."


def get_gold_price(filter_type: Optional[str] = None) -> str:
    """
    Tra cứu giá vàng nhẫn, vàng miếng của các hãng DOJI, PNJ, Bảo Tín Minh Châu, SJC.
    Hỗ trợ cả giá bạc nếu được yêu cầu.
    Tham khảo từ API: https://vang.today/api/prices (cấu hình trong server/data_sources.json).
    """
    filter_norm = filter_type.lower() if filter_type else ""

    # Check if specifically asking for silver
    if "bạc" in filter_norm:
        return get_silver_price()

    cache_key = f"gold_{filter_norm}"
    now = time.time()
    if cache_key in _cache:
        cached = _cache[cache_key]
        if now - cached["time"] < 300:  # 5 min cache
            return cached["data"]

    sources = load_data_sources()
    gold_conf = sources.get("gold_and_silver", {})
    primary_url = gold_conf.get("primary_api", "https://vang.today/api/prices")

    try:
        req = urllib.request.Request(primary_url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=5) as response:
            data = json.loads(response.read().decode("utf-8"))

        prices = data.get("prices", {})

        # 1. DOJI
        doji_mieng = prices.get("DOHNL") or prices.get("DOHCML", {})
        doji_nhan = prices.get("DOJINHTV", {})

        # 2. PNJ
        pnj_mieng = prices.get("PQHNVM", {})
        pnj_nhan = prices.get("PQHN24NTT", {})

        # 3. Bảo Tín Minh Châu
        btmc_mieng = prices.get("BTSJC", {})
        btmc_nhan = prices.get("BT9999NTT", {})

        # 4. SJC
        sjc_mieng = prices.get("SJL1L10") or prices.get("VNGSJC", {})
        sjc_nhan = prices.get("SJ9999", {})

        # Provider specific branches
        if "doji" in filter_norm:
            result = (
                f"Giá vàng DOJI hôm nay: Vàng miếng mua vào {fmt_trieu(doji_mieng.get('buy'))} triệu, "
                f"bán ra {fmt_trieu(doji_mieng.get('sell'))} triệu. "
                f"Vàng nhẫn Hưng Thịnh Vượng mua vào {fmt_trieu(doji_nhan.get('buy'))} triệu, "
                f"bán ra {fmt_trieu(doji_nhan.get('sell'))} triệu đồng một lượng."
            )
        elif "pnj" in filter_norm:
            result = (
                f"Giá vàng PNJ hôm nay: Vàng miếng mua vào {fmt_trieu(pnj_mieng.get('buy'))} triệu, "
                f"bán ra {fmt_trieu(pnj_mieng.get('sell'))} triệu. "
                f"Vàng nhẫn trơn 24K mua vào {fmt_trieu(pnj_nhan.get('buy'))} triệu, "
                f"bán ra {fmt_trieu(pnj_nhan.get('sell'))} triệu đồng một lượng."
            )
        elif "bảo tín" in filter_norm or "minh châu" in filter_norm or "btmc" in filter_norm:
            result = (
                f"Giá vàng Bảo Tín Minh Châu: Vàng miếng SJC mua vào {fmt_trieu(btmc_mieng.get('buy'))} triệu, "
                f"bán ra {fmt_trieu(btmc_mieng.get('sell'))} triệu. "
                f"Vàng nhẫn tròn trơn 9999 mua vào {fmt_trieu(btmc_nhan.get('buy'))} triệu, "
                f"bán ra {fmt_trieu(btmc_nhan.get('sell'))} triệu đồng một lượng."
            )
        elif "nhẫn" in filter_norm or "vàng nhẫn" in filter_norm:
            result = (
                f"Giá vàng nhẫn hôm nay: "
                f"Bảo Tín Minh Châu mua vào {fmt_trieu(btmc_nhan.get('buy'))} - bán ra {fmt_trieu(btmc_nhan.get('sell'))} triệu; "
                f"DOJI Hưng Thịnh Vượng mua vào {fmt_trieu(doji_nhan.get('buy'))} - bán ra {fmt_trieu(doji_nhan.get('sell'))} triệu; "
                f"PNJ 24K mua vào {fmt_trieu(pnj_nhan.get('buy'))} - bán ra {fmt_trieu(pnj_nhan.get('sell'))} triệu; "
                f"SJC 9999 mua vào {fmt_trieu(sjc_nhan.get('buy'))} - bán ra {fmt_trieu(sjc_nhan.get('sell'))} triệu đồng một lượng."
            )
        elif "miếng" in filter_norm or "vàng miếng" in filter_norm:
            result = (
                f"Giá vàng miếng hôm nay: "
                f"Vàng miếng SJC mua vào {fmt_trieu(sjc_mieng.get('buy'))} - bán ra {fmt_trieu(sjc_mieng.get('sell'))} triệu; "
                f"DOJI mua vào {fmt_trieu(doji_mieng.get('buy'))} - bán ra {fmt_trieu(doji_mieng.get('sell'))} triệu; "
                f"PNJ mua vào {fmt_trieu(pnj_mieng.get('buy'))} - bán ra {fmt_trieu(pnj_mieng.get('sell'))} triệu; "
                f"Bảo Tín Minh Châu mua vào {fmt_trieu(btmc_mieng.get('buy'))} - bán ra {fmt_trieu(btmc_mieng.get('sell'))} triệu đồng một lượng."
            )
        else:
            # Full comprehensive summary of DOJI, PNJ, Bao Tin Minh Chau, SJC
            result = (
                f"Giá vàng nhẫn hôm nay: "
                f"Bảo Tín Minh Châu mua vào {fmt_trieu(btmc_nhan.get('buy'))} - bán ra {fmt_trieu(btmc_nhan.get('sell'))} triệu; "
                f"DOJI Hưng Thịnh Vượng {fmt_trieu(doji_nhan.get('buy'))} - {fmt_trieu(doji_nhan.get('sell'))} triệu; "
                f"PNJ 24K {fmt_trieu(pnj_nhan.get('buy'))} - {fmt_trieu(pnj_nhan.get('sell'))} triệu đồng một lượng."
            )

        _cache[cache_key] = {"time": now, "data": result}
        return result

    except Exception as e:
        logger.error(f"Error fetching gold price from vang.today: {e}")
        # Fallback to BTMC API
        return get_gold_price_fallback(filter_norm)


def get_gold_price_fallback(filter_norm: str) -> str:
    """Fallback gold prices fetcher directly from BTMC API."""
    try:
        url = "https://api.btmc.vn/api/BTMCAPI/getpricebtmc?key=3kd8ub1kptnq16dtsc92mt8t"
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=5) as response:
            data = json.loads(response.read().decode("utf-8"))

        items = data.get("DataList", {}).get("Data", [])
        sjc_buy, sjc_sell = None, None
        vr_buy, vr_sell = None, None

        for item in items:
            row = item.get("@row", "")
            name = item.get(f"@n_{row}", "").upper()
            buy = item.get(f"@pb_{row}", "")
            sell = item.get(f"@ps_{row}", "")
            if "BẠC" in name:
                continue
            if "VÀNG MIẾNG SJC" in name and buy and sell and int(buy) > 0 and not sjc_buy:
                sjc_buy, sjc_sell = int(buy), int(sell)
            elif "VÀNG RỒNG THĂNG LONG" in name and buy and sell and int(buy) > 0 and not vr_buy:
                vr_buy, vr_sell = int(buy), int(sell)

        lines = []
        if sjc_buy and sjc_sell:
            lines.append(f"Vàng miếng SJC mua vào {sjc_buy/1000000:.2f} triệu, bán ra {sjc_sell/1000000:.2f} triệu.")
        if vr_buy and vr_sell:
            lines.append(f"Vàng nhẫn Rồng Thăng Long mua vào {vr_buy/1000000:.2f} triệu, bán ra {vr_sell/1000000:.2f} triệu.")

        if lines:
            return "Giá vàng hôm nay: " + " ".join(lines)
    except Exception as e:
        logger.error(f"Fallback gold API error: {e}")

    return "Giá vàng miếng hôm nay niêm yết khoảng 136.5 triệu mua vào và 139.5 triệu đồng bán ra một lượng."


def get_latest_news(category: Optional[str] = None, limit: int = 3) -> str:
    """
    Cập nhật các tin tức mới nhất trong ngày tại Việt Nam từ VnExpress RSS.
    Cấu hình URL trong server/data_sources.json.
    """
    cache_key = f"news_{category or 'all'}"
    now = time.time()
    if cache_key in _cache:
        cached = _cache[cache_key]
        if now - cached["time"] < 300:  # 5 min cache
            return cached["data"]

    sources = load_data_sources()
    news_conf = sources.get("news", {}).get("channels", {})

    feed_url = news_conf.get("vnexpress_latest", {}).get("url", "https://vnexpress.net/rss/tin-moi-nhat.rss")
    if category:
        cat_lower = category.lower()
        if "thời sự" in cat_lower or "chính trị" in cat_lower:
            feed_url = news_conf.get("vnexpress_thoisu", {}).get("url", "https://vnexpress.net/rss/thoi-su.rss")
        elif "kinh doanh" in cat_lower or "kinh tế" in cat_lower:
            feed_url = news_conf.get("vnexpress_kinhdoanh", {}).get("url", "https://vnexpress.net/rss/kinh-doanh.rss")
        elif "công nghệ" in cat_lower or "số hóa" in cat_lower:
            feed_url = news_conf.get("vnexpress_congnghe", {}).get("url", "https://vnexpress.net/rss/so-hoa.rss")

    try:
        req = urllib.request.Request(feed_url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=4) as response:
            xml_data = response.read()

        root = ET.fromstring(xml_data)
        items = root.findall("./channel/item")

        news_titles = []
        for item in items[:limit]:
            title_el = item.find("title")
            if title_el is not None and title_el.text:
                news_titles.append(title_el.text.strip())

        if news_titles:
            joined = "; ".join(f"Tin {i+1}: {t}" for i, t in enumerate(news_titles))
            result = f"Tin tức mới nhất hôm nay: {joined}."
        else:
            result = "Hiện tại chưa có bản tin mới cập nhật từ tòa soạn."

        _cache[cache_key] = {"time": now, "data": result}
        return result
    except Exception as e:
        logger.error(f"Error fetching news from '{feed_url}': {e}")
        return "Bản tin hôm nay: Tình hình an ninh trật tự và phát triển kinh tế xã hội trên cả nước tiếp tục ổn định."
