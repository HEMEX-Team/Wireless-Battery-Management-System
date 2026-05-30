import logging
import os
from datetime import datetime, timezone

from dotenv import load_dotenv
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

# Load backend/.env before anything else reads os.getenv(...)
load_dotenv()

from app.models.database import engine, Base
from app.models.init_db import apply_lightweight_migrations
from app.mqtt_subscriber import start_mqtt, stop_mqtt
from app.routes.auth import router as auth_router
from app.routes.packs import router as packs_router
from app.routes.groups import router as groups_router
from app.routes.firmware import router as firmware_router

# Import models so Base knows about them
from app.models import models  # noqa: F401

logging.basicConfig(
    level=os.getenv("WBMS_LOG_LEVEL", "INFO"),
    format="%(asctime)s %(levelname)s %(name)s: %(message)s",
)
log = logging.getLogger("wbms.main")

app = FastAPI(title="WBMS Backend", version="1.0.0")


@app.on_event("startup")
def on_startup():
    try:
        Base.metadata.create_all(bind=engine)
        apply_lightweight_migrations(engine)
    except Exception:
        log.exception("Could not create tables on startup")
    start_mqtt()


@app.on_event("shutdown")
def on_shutdown():
    stop_mqtt()


# CORS — allow the configured frontend origin plus local dev hosts.
_frontend_origin = os.getenv("FRONTEND_ORIGIN", "http://localhost:5173")
_allowed_origins = {
    _frontend_origin,
    "http://localhost:5173",
    "http://127.0.0.1:5173",
}

app.add_middleware(
    CORSMiddleware,
    allow_origins=sorted(_allowed_origins),
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Include routers
app.include_router(auth_router)
app.include_router(packs_router)
app.include_router(groups_router)
app.include_router(firmware_router)


@app.get("/")
async def root():
    return {"message": "WBMS API"}


@app.get("/health")
async def health_check():
    return {
        "status": "Healthy",
        "timestamp": datetime.now(timezone.utc).isoformat(),
    }
