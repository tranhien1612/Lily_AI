import uvicorn
from app.config import settings

if __name__ == "__main__":
    print(f"[*] Starting Xiaozhi Local AI Server on {settings.HOST}:{settings.PORT}")
    uvicorn.run(
        "app.ws_server:app",
        host=settings.HOST,
        port=settings.PORT,
        reload=False,
        log_level="info"
    )

