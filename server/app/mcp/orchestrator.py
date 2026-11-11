import asyncio
import logging
from typing import Dict, Any, Optional
from fastapi import WebSocket
from app.session import DeviceSession
from app.mcp.info_tools import get_weather, get_gold_price, get_latest_news

logger = logging.getLogger("MCP_ORCHESTRATOR")

class MCPOrchestrator:
    def __init__(self):
        self._pending_calls: Dict[int, asyncio.Future] = {}
        self._call_counter = 1

    def get_standard_tools_schema(self) -> list:
        """
        Default tools definition presented to LLM.
        """
        return [
            {
                "type": "function",
                "function": {
                    "name": "gpio_write",
                    "description": "Bật hoặc tắt đèn, quạt hoặc thiết bị điện kết nối với chân GPIO.",
                    "parameters": {
                        "type": "object",
                        "properties": {
                            "pin": {
                                "type": "integer",
                                "description": "Số chân GPIO điều khiển thiết bị (VD: 48 là LED tích hợp)"
                            },
                            "level": {
                                "type": "integer",
                                "description": "1 để BẬT, 0 để TẮT"
                            }
                        },
                        "required": ["pin", "level"]
                    }
                }
            },
            {
                "type": "function",
                "function": {
                    "name": "music_play",
                    "description": "Phát một bài hát hoặc nhạc cụ theo tên hoặc thể loại.",
                    "parameters": {
                        "type": "object",
                        "properties": {
                            "title": {
                                "type": "string",
                                "description": "Tên bài hát muốn phát (VD: 'Em Của Ngày Hôm Qua', 'Nhạc Thư Giãn')"
                            }
                        },
                        "required": ["title"]
                    }
                }
            },
            {
                "type": "function",
                "function": {
                    "name": "music_stop",
                    "description": "Dừng phát nhạc trên thiết bị.",
                    "parameters": {
                        "type": "object",
                        "properties": {}
                    }
                }
            },
            {
                "type": "function",
                "function": {
                    "name": "set_volume",
                    "description": "Điều chỉnh mức âm lượng của loa (từ 0 đến 100%), hoặc tăng / giảm âm lượng theo mức delta.",
                    "parameters": {
                        "type": "object",
                        "properties": {
                            "volume": {
                                "type": "integer",
                                "description": "Mức âm lượng mong muốn từ 0 đến 100"
                            },
                            "delta": {
                                "type": "integer",
                                "description": "Mức tăng (+) hoặc giảm (-) âm lượng (ví dụ: 15 để tăng, -15 để giảm)"
                            }
                        }
                    }
                }
            },
            {
                "type": "function",
                "function": {
                    "name": "get_weather",
                    "description": "Tra cứu thông tin thời tiết thực tế hôm nay (nhiệt độ, độ ẩm, tình trạng mưa nắng) tại bất kỳ tỉnh thành nào ở Việt Nam hoặc thế giới.",
                    "parameters": {
                        "type": "object",
                        "properties": {
                            "location": {
                                "type": "string",
                                "description": "Tên thành phố hoặc tỉnh thành muốn tra cứu (VD: 'Hà Nội', 'Hồ Chí Minh', 'Đà Nẵng', 'Hải Phòng')"
                            }
                        },
                        "required": ["location"]
                    }
                }
            },
            {
                "type": "function",
                "function": {
                    "name": "get_gold_price",
                    "description": "Tra cứu giá vàng và giá bạc hôm nay tại Việt Nam. Cung cấp chi tiết giá vàng nhẫn, vàng miếng của các thương hiệu hàng đầu: DOJI, PNJ, Bảo Tín Minh Châu, SJC và giá bạc thỏi/miếng.",
                    "parameters": {
                        "type": "object",
                        "properties": {
                            "filter_type": {
                                "type": "string",
                                "description": "Lọc theo thương hiệu hoặc loại vàng/bạc (VD: 'doji', 'pnj', 'bảo tín', 'nhẫn', 'miếng', 'bạc', hoặc để trống để xem tổng quan)"
                            }
                        }
                    }
                }
            },
            {
                "type": "function",
                "function": {
                    "name": "get_latest_news",
                    "description": "Đọc các tin tức mới nhất, thời sự trong ngày tại Việt Nam từ báo VnExpress.",
                    "parameters": {
                        "type": "object",
                        "properties": {
                            "category": {
                                "type": "string",
                                "description": "Chuyên mục tin tức nếu có (VD: 'thời sự', 'công nghệ', 'kinh doanh')"
                            }
                        }
                    }
                }
            }
        ]

    async def execute_server_tool(self, tool_name: str, arguments: Dict[str, Any]) -> Dict[str, Any]:
        """
        Executes real-time info MCP tools directly on the server without needing device hardware.
        """
        try:
            if tool_name == "get_weather":
                loc = arguments.get("location", "Hà Nội")
                loop = asyncio.get_running_loop()
                msg = await loop.run_in_executor(None, get_weather, loc)
                return {"status": "ok", "message": msg}

            elif tool_name == "get_gold_price":
                flt = arguments.get("filter_type") or arguments.get("type") or arguments.get("brand")
                loop = asyncio.get_running_loop()
                msg = await loop.run_in_executor(None, get_gold_price, flt)
                return {"status": "ok", "message": msg}

            elif tool_name == "get_latest_news":
                cat = arguments.get("category")
                loop = asyncio.get_running_loop()
                msg = await loop.run_in_executor(None, get_latest_news, cat)
                return {"status": "ok", "message": msg}

            return {"status": "error", "message": f"Công cụ '{tool_name}' không được hỗ trợ trên máy chủ."}
        except Exception as e:
            logger.error(f"Error executing server tool {tool_name}: {e}")
            return {"status": "error", "message": "Có lỗi khi tra cứu thông tin trực tuyến."}

    async def execute_device_tool(
        self,
        session: DeviceSession,
        tool_name: str,
        arguments: Dict[str, Any],
        timeout: float = 5.0
    ) -> Dict[str, Any]:
        """
        Sends mcp_call to device and waits for mcp_result.
        """
        call_id = self._call_counter
        self._call_counter += 1

        loop = asyncio.get_running_loop()
        future = loop.create_future()
        self._pending_calls[call_id] = future

        req_msg = {
            "type": "mcp_call",
            "jsonrpc": "2.0",
            "method": "tools/call",
            "params": {
                "name": tool_name,
                "arguments": arguments
            },
            "id": call_id
        }

        try:
            logger.info(f"Dispatching MCP Call #{call_id} to device: {tool_name}({arguments})")
            await session.websocket.send_json(req_msg)
            result = await asyncio.wait_for(future, timeout=timeout)
            logger.info(f"Received MCP Result #{call_id}: {result}")
            return result
        except asyncio.TimeoutError:
            logger.error(f"Timeout waiting for MCP tool call #{call_id} from device")
            return {"error": "Tool execution timed out on device"}
        except Exception as e:
            logger.error(f"Failed to execute MCP call #{call_id}: {e}")
            return {"error": str(e)}
        finally:
            self._pending_calls.pop(call_id, None)

    def handle_mcp_result(self, result_msg: Dict[str, Any]):
        call_id = result_msg.get("id")
        if call_id in self._pending_calls:
            future = self._pending_calls[call_id]
            if not future.done():
                future.set_result(result_msg.get("result") or result_msg.get("error"))

mcp_orchestrator = MCPOrchestrator()

