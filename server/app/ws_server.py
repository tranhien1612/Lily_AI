import os
import json
import time
from datetime import datetime
import logging
import asyncio
import mimetypes
from typing import Optional, Dict, Any
from pydantic import BaseModel
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse, JSONResponse
from fastapi.middleware.cors import CORSMiddleware

from app.config import settings
from app.session import session_manager, DeviceSession, RECORDINGS_DIR
from app.ai.asr import asr_engine
from app.ai.llm import llm_engine
from app.ai.tts import tts_engine
from app.mcp.orchestrator import mcp_orchestrator
from app.media.music import music_handler

# Ensure standard audio MIME types for web browsers
mimetypes.add_type("audio/wav", ".wav")
mimetypes.add_type("audio/mpeg", ".mp3")

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(name)s: %(message)s")
logger = logging.getLogger("WS_SERVER")

app = FastAPI(title="Xiaozhi Local AI Server", version="1.3.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Directories
STATIC_DIR = os.path.join(os.path.dirname(__file__), "static")
os.makedirs(settings.MUSIC_DIR, exist_ok=True)
os.makedirs(RECORDINGS_DIR, exist_ok=True)
os.makedirs(STATIC_DIR, exist_ok=True)

# Mount static endpoints
app.mount("/music", StaticFiles(directory=settings.MUSIC_DIR), name="music")
app.mount("/recordings", StaticFiles(directory=RECORDINGS_DIR), name="recordings")
app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")

class WebChatMessage(BaseModel):
    text: str

# Reusable full conversation processing pipeline (supports both Voice on ESP32 and Text on Web)
async def process_conversation_turn(
    device_id: str,
    text: str,
    user_audio_bytes: Optional[bytes] = None,
    session: Optional[DeviceSession] = None
) -> Dict[str, Any]:
    websocket = session.websocket if (session and session.websocket) else None

    # 1. Update ESP32 OLED prompt if online
    if websocket:
        await websocket.send_json({
            "type": "stt",
            "text": text,
            "session_id": session.session_id
        })
        await websocket.send_json({
            "type": "transcript",
            "text": text
        })

    # 2. Record User message in history
    session_manager.add_chat_message(device_id, "user", text, user_audio_bytes)
    
    if session:
        session.add_history("user", text)
        history = session.history
    else:
        history = [{"role": "user", "content": text}]

    # 3. LLM Thinking & Intent Processing
    if websocket:
        await websocket.send_json({"type": "llm", "emotion": "thinking"})

    tools = mcp_orchestrator.get_standard_tools_schema()
    response_text, tool_call = await llm_engine.chat(history, tools=tools)

    # 4. Handle Tool Calls
    if tool_call:
        fn_name = tool_call.get("name")
        fn_args = tool_call.get("arguments", {})

        if fn_name == "music_play":
            song_query = fn_args.get("title", "")
            song = await music_handler.find_or_download_song(song_query)
            if song:
                reply_text = f"Đang phát: {song['title']}"
                intro_spoken = f"Đang phát bài hát {song['title']}"
                intro_pcm = bytearray()
                try:
                    if websocket:
                        await websocket.send_json({"type": "tts", "state": "sentence_start", "text": reply_text})
                        await websocket.send_json({"type": "response_text", "text": reply_text})
                        await websocket.send_json({"type": "tts", "state": "start"})
                        await websocket.send_json({"type": "tts_start"})

                    async for pcm_chunk in tts_engine.text_to_pcm_stream(intro_spoken):
                        intro_pcm.extend(pcm_chunk)
                        if websocket:
                            await websocket.send_bytes(pcm_chunk)

                    if websocket:
                        await websocket.send_json({"type": "tts", "state": "stop"})
                        await websocket.send_json({"type": "tts_stop"})
                        await websocket.send_json({
                            "type": "music_start",
                            "url": song["url"],
                            "title": song["title"],
                            "duration": song["duration"]
                        })
                        if song.get("lrc_path"):
                            if session and session.current_music_task:
                                session.current_music_task.cancel()
                            if session:
                                session.current_music_task = asyncio.create_task(
                                    music_handler.stream_lyrics_task(websocket, song["lrc_path"], song["duration"])
                                )
                except Exception as ws_err:
                    logger.warning(f"Error sending music to websocket: {ws_err}")

                session_manager.add_chat_message(
                    device_id, "lily", reply_text,
                    audio_bytes=bytes(intro_pcm), tool_call=tool_call
                )
                return {"status": "ok", "reply": reply_text, "tool": "music_play", "song": song["title"]}
            else:
                fail_text = f"Không tìm thấy bài hát '{song_query}'."
                if websocket:
                    await websocket.send_json({"type": "tts", "state": "sentence_start", "text": fail_text})
                    await websocket.send_json({"type": "response_text", "text": fail_text})

                bot_pcm = bytearray()
                if websocket:
                    await websocket.send_json({"type": "tts", "state": "start"})
                    await websocket.send_json({"type": "tts_start"})

                async for pcm_chunk in tts_engine.text_to_pcm_stream(fail_text):
                    bot_pcm.extend(pcm_chunk)
                    if websocket:
                        await websocket.send_bytes(pcm_chunk)

                if websocket:
                    await websocket.send_json({"type": "tts", "state": "stop"})
                    await websocket.send_json({"type": "tts_stop"})

                session_manager.add_chat_message(device_id, "lily", fail_text, audio_bytes=bytes(bot_pcm))
                return {"status": "ok", "reply": fail_text}

        elif fn_name == "music_stop":
            if session and session.current_music_task:
                session.current_music_task.cancel()
            if websocket:
                await websocket.send_json({"type": "music_stop"})

            stop_text = "Đã dừng phát nhạc."
            if websocket:
                await websocket.send_json({"type": "tts", "state": "sentence_start", "text": stop_text})
                await websocket.send_json({"type": "response_text", "text": stop_text})

            stop_pcm = bytearray()
            if websocket:
                await websocket.send_json({"type": "tts", "state": "start"})
                await websocket.send_json({"type": "tts_start"})

            async for pcm_chunk in tts_engine.text_to_pcm_stream(stop_text):
                stop_pcm.extend(pcm_chunk)
                if websocket:
                    await websocket.send_bytes(pcm_chunk)

            if websocket:
                await websocket.send_json({"type": "tts", "state": "stop"})
                await websocket.send_json({"type": "tts_stop"})

            session_manager.add_chat_message(device_id, "lily", stop_text, audio_bytes=bytes(stop_pcm), tool_call=tool_call)
            return {"status": "ok", "reply": stop_text, "tool": "music_stop"}

        elif fn_name in ["get_weather", "get_gold_price", "get_latest_news"]:
            server_res = await mcp_orchestrator.execute_server_tool(fn_name, fn_args)
            raw_facts = server_res.get("message", "Đã tra cứu thông tin xong.")
            # Approach 1: Synthesize dynamic, non-rigid response using LLM while preserving 100% factual accuracy
            dynamic_reply = await llm_engine.synthesize_dynamic_response(text, raw_facts)
            response_text = dynamic_reply

        else:
            # Forward MCP tool call (GPIO LED, etc.) to ESP32 device
            preface = response_text if response_text else "Đang thực hiện lệnh..."
            if websocket:
                await websocket.send_json({"type": "tts", "state": "sentence_start", "text": preface})
                await websocket.send_json({"type": "response_text", "text": preface})

            final_text = "Đã thực hiện xong lệnh của bạn."
            if session and websocket:
                tool_result = await mcp_orchestrator.execute_device_tool(
                    session, fn_name, fn_args, timeout=3.0
                )
                if tool_result and isinstance(tool_result, dict):
                    final_text = tool_result.get("message", "Đã thực hiện xong.")
            response_text = final_text

    # 5. Fast TTS Streaming & Response Saving
    if response_text:
        if websocket:
            await websocket.send_json({"type": "tts", "state": "sentence_start", "text": response_text})
            await websocket.send_json({"type": "response_text", "text": response_text})

        if session:
            session.add_history("assistant", response_text)

        bot_pcm = bytearray()
        if websocket:
            await websocket.send_json({"type": "tts", "state": "start"})
            await websocket.send_json({"type": "tts_start"})

        async for pcm_chunk in tts_engine.text_to_pcm_stream(response_text):
            bot_pcm.extend(pcm_chunk)
            if websocket:
                await websocket.send_bytes(pcm_chunk)

        if websocket:
            await websocket.send_json({"type": "tts", "state": "stop"})
            await websocket.send_json({"type": "tts_stop"})

        session_manager.add_chat_message(
            device_id, "lily", response_text,
            audio_bytes=bytes(bot_pcm), tool_call=tool_call
        )
        return {"status": "ok", "reply": response_text, "tool": tool_call}

    return {"status": "ok", "reply": ""}


# Dashboard UI Routes
@app.get("/")
@app.get("/dashboard")
async def serve_dashboard():
    dashboard_html = os.path.join(STATIC_DIR, "dashboard.html")
    if os.path.exists(dashboard_html):
        return FileResponse(dashboard_html)
    return JSONResponse({"status": "online", "message": "Dashboard file not found"})

@app.get("/api/status")
async def api_status():
    return {
        "status": "online",
        "service": "LiLy AI Server (Local)",
        "ip": settings.SERVER_PUBLIC_URL,
        "tts_voice": settings.TTS_VOICE,
        "ollama_model": settings.OLLAMA_MODEL,
        "connected_devices": len(session_manager.active_sessions)
    }

@app.get("/api/devices")
async def api_get_devices(online_only: bool = True):
    return session_manager.get_all_devices(online_only=online_only)

@app.get("/api/devices/{device_id}/messages")
async def api_get_device_messages(device_id: str):
    return session_manager.get_device_messages(device_id)

@app.post("/api/devices/{device_id}/chat")
async def api_send_device_chat(device_id: str, payload: WebChatMessage):
    text = payload.text.strip()
    if not text:
        return JSONResponse({"status": "error", "message": "Nội dung tin nhắn không được để trống."}, status_code=400)

    # Check if device is registered
    dev = session_manager.devices.get(device_id)
    if not dev:
        return JSONResponse({"status": "error", "message": f"Không tìm thấy thiết bị: {device_id}"}, status_code=404)

    # Look for active session if device is online
    session = None
    for s in session_manager.active_sessions.values():
        if s.device_id == device_id:
            session = s
            break

    logger.info(f"Received Web chat message for {device_id}: '{text}' (online={session is not None})")
    result = await process_conversation_turn(device_id, text, user_audio_bytes=None, session=session)
    return JSONResponse(result)


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    session: DeviceSession = session_manager.register_connection(websocket)
    logger.info(f"New ESP32 client connected! Device: {session.device_id}, Session: {session.session_id}")

    audio_buffer = bytearray()

    try:
        # Step 1: Send hello handshake message to device (Xiaozhi standard)
        await websocket.send_json({
            "type": "hello",
            "transport": "websocket",
            "session_id": session.session_id,
            "epoch": int(time.time()),
            "time": datetime.now().strftime("%H:%M"),
            "audio_params": {
                "format": "pcm",
                "sample_rate": 16000,
                "channels": 1,
                "frame_duration": 60
            }
        })

        while True:
            try:
                message = await websocket.receive()
            except WebSocketDisconnect:
                logger.info(f"Client disconnected: {session.session_id}")
                break
            except Exception:
                break

            if message.get("type") == "websocket.disconnect":
                logger.info(f"Received disconnect frame: {session.session_id}")
                break

            if "bytes" in message and message["bytes"] is not None:
                # Binary frame: Audio stream from microphone
                if session.is_recording:
                    audio_buffer.extend(message["bytes"])

            elif "text" in message and message["text"] is not None:
                try:
                    data = json.loads(message["text"])
                except Exception:
                    continue

                msg_type = data.get("type", "")
                msg_state = data.get("state", "")
                logger.info(f"[{session.session_id}] Received text message: {msg_type} (state={msg_state})")

                if msg_type == "hello":
                    session.is_ready = True
                    await websocket.send_json({
                        "type": "hello",
                        "transport": "websocket",
                        "session_id": session.session_id,
                        "epoch": int(time.time()),
                        "time": datetime.now().strftime("%H:%M"),
                        "audio_params": {
                            "format": "pcm",
                            "sample_rate": 16000,
                            "channels": 1,
                            "frame_duration": 60
                        }
                    })
                    logger.info(f"Handshake confirmed for device: {session.device_id}")

                elif msg_type == "ready":
                    session.is_ready = True
                    logger.info(f"Device confirmed ready: {session.session_id}")

                # Compatible with both official Xiaozhi (type=listen, state=start/detect) and legacy listen_start
                elif msg_type in ["listen_start", "wake_word_detected"] or (msg_type == "listen" and msg_state in ["start", "detect"]):
                    session.is_recording = True
                    audio_buffer.clear()
                    logger.info(f"Recording started on device (trigger={msg_type}, state={msg_state})...")

                elif msg_type == "abort":
                    session.is_recording = False
                    audio_buffer.clear()
                    if session.current_music_task:
                        session.current_music_task.cancel()
                    logger.info("Abort command received from device.")

                # Compatible with both official Xiaozhi (type=listen, state=stop) and legacy listen_stop
                elif msg_type == "listen_stop" or (msg_type == "listen" and msg_state == "stop"):
                    session.is_recording = False
                    recorded_audio_bytes = bytes(audio_buffer)
                    logger.info(f"Recording stopped. Received {len(recorded_audio_bytes)} bytes of audio.")

                    # Pipeline Step 1: Fast ASR
                    transcript = await asr_engine.transcribe(recorded_audio_bytes)
                    
                    # Silence fast-path: Don't wait for Ollama when no speech detected
                    if not transcript or len(transcript.strip()) == 0:
                        logger.info("Silence detected. Fast fallback reply.")
                        await websocket.send_json({"type": "stt", "text": "...", "session_id": session.session_id})
                        await websocket.send_json({"type": "transcript", "text": "..."})
                        reply = "Tôi chưa nghe rõ, bạn có thể nói lại không?"
                        await websocket.send_json({"type": "tts", "state": "sentence_start", "text": reply})
                        await websocket.send_json({"type": "response_text", "text": reply})
                        
                        # Save user voice & bot reply to conversation history
                        session_manager.add_chat_message(session.device_id, "user", "[Im lặng / Không nghe rõ]", recorded_audio_bytes)
                        bot_pcm = bytearray()
                        await websocket.send_json({"type": "tts", "state": "start"})
                        await websocket.send_json({"type": "tts_start"})
                        async for pcm_chunk in tts_engine.text_to_pcm_stream(reply):
                            bot_pcm.extend(pcm_chunk)
                            await websocket.send_bytes(pcm_chunk)
                        await websocket.send_json({"type": "tts", "state": "stop"})
                        await websocket.send_json({"type": "tts_stop"})
                        session_manager.add_chat_message(session.device_id, "lily", reply, bytes(bot_pcm))
                        continue

                    # Execute full conversation turn
                    await process_conversation_turn(
                        session.device_id,
                        transcript,
                        user_audio_bytes=recorded_audio_bytes,
                        session=session
                    )

                elif msg_type == "mcp_result":
                    mcp_orchestrator.handle_mcp_result(data)

    except WebSocketDisconnect:
        logger.info(f"Client disconnected: {session.session_id}")
    except Exception as e:
        logger.error(f"Error in websocket loop: {e}")
    finally:
        if session.current_music_task:
            session.current_music_task.cancel()
        session_manager.unregister_connection(session.session_id)
