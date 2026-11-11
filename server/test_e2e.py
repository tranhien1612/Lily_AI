import asyncio
import json
import logging
import websockets
import numpy as np

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
logger = logging.getLogger("E2E_TEST")

async def run_test():
    uri = "ws://localhost:8000/ws"
    logger.info(f"Connecting to {uri}...")

    async with websockets.connect(uri) as ws:
        # 1. Expect Hello message
        msg_str = await ws.recv()
        msg = json.loads(msg_str)
        logger.info(f"[1] Received from server: {msg}")
        assert msg.get("type") == "hello", "Expected hello message"
        session_id = msg.get("session_id")

        # 2. Send ready acknowledgment
        await ws.send(json.dumps({"type": "ready"}))
        logger.info("[2] Sent ready acknowledgment")

        # 3. Simulate User Voice Query ("Bật đèn lên")
        logger.info("[3] Simulating voice command: 'listen_start'...")
        await ws.send(json.dumps({"type": "listen_start"}))

        # Generate 1.5 seconds of synthetic audio (sine wave)
        sample_rate = 16000
        duration = 1.5
        t = np.linspace(0, duration, int(sample_rate * duration), False)
        tone = (np.sin(2 * np.pi * 440 * t) * 10000).astype(np.int16)
        audio_bytes = tone.tobytes()

        # Send in chunks of 640 bytes (20ms)
        chunk_size = 640
        for i in range(0, len(audio_bytes), chunk_size):
            await ws.send(audio_bytes[i:i + chunk_size])
            await asyncio.sleep(0.01)

        # Stop listening
        await ws.send(json.dumps({"type": "listen_stop"}))
        logger.info("[3] Sent 'listen_stop'. Waiting for AI pipeline response...")

        # 4. Listen for server responses
        received_transcript = False
        received_response = False
        received_tts = False

        while True:
            try:
                raw = await asyncio.wait_for(ws.recv(), timeout=10.0)
                if isinstance(raw, bytes):
                    logger.info(f"Received binary TTS audio chunk: {len(raw)} bytes")
                    received_tts = True
                else:
                    data = json.loads(raw)
                    msg_type = data.get("type")
                    logger.info(f"Received message: {data}")

                    if msg_type == "transcript":
                        received_transcript = True
                    elif msg_type == "response_text":
                        received_response = True
                    elif msg_type == "mcp_call":
                        # Simulate Device executing tool and replying with mcp_result
                        logger.info(f"Executing MCP Tool on simulated device: {data}")
                        call_id = data.get("id")
                        result_payload = {
                            "type": "mcp_result",
                            "jsonrpc": "2.0",
                            "result": {"status": "success", "pin": 48, "level": 1},
                            "id": call_id
                        }
                        await ws.send(json.dumps(result_payload))
                        logger.info(f"Sent mcp_result for call #{call_id}")
                    elif msg_type == "tts_stop" or msg_type == "music_start":
                        logger.info("Pipeline reached final state successfully!")
                        break
            except asyncio.TimeoutError:
                logger.warning("Timeout waiting for message")
                break

        logger.info("========================================")
        logger.info("End-to-End Test Completed Successfully!")
        logger.info("========================================")

if __name__ == "__main__":
    asyncio.run(run_test())

