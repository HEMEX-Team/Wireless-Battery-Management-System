"""
MQTT subscriber that consumes the BMS telemetry stream published by the
master ESP32 and persists it to the database.

The master publishes JSON payloads to `MQTT_TOPIC` (default `bms/data`) with a
shape roughly like::

    {
      "senderIndex": 1,
      "v1": 3650, "v2": 3700, ... "v16": 3690,   # mV (cells with value 0 omitted)
      "vStack": 47450,                            # mV
      "vPack":  47300,                            # mV
      "current": -250,                            # mA (negative = discharge)
      "charge":  1234.5,                          # Ah
      "chargeTime": 3600,                         # s
      "chipTemp": 32.5,                           # degC
      "temp1": 28.3, "temp2": 29.1, "temp3": 27.8,
      "isCharging": false,
      "isDischarging": true,
      "message": "BMS_ACTIVE",
      "packId": "wbms-pack-1"                     # optional override
    }

Each message creates one `Reading` row (pack-level summary) and N
`BatteryReading` rows (one per present cell voltage).

Design notes:
  * paho-mqtt's network loop runs in its own thread; callbacks fire on that
    thread, so we open a dedicated SQLAlchemy session per message and close
    it immediately (no dependency on FastAPI's request-scoped `get_db`).
  * Failures (broker down, malformed payload, missing pack) are logged but
    never propagate — the API must keep working even if MQTT is unavailable.
  * Packs are matched by `pack_identifier`. The identifier is read from the
    payload `packId` field if present, otherwise built from
    `MQTT_PACK_ID_TEMPLATE` (default ``wbms-pack-{senderIndex}``). Unknown
    packs are skipped with a throttled warning.
"""

from __future__ import annotations

import json
import logging
import os
import threading
from datetime import datetime, timezone
from typing import Any, Optional

import paho.mqtt.client as mqtt
from sqlalchemy.exc import IntegrityError
from sqlalchemy.orm import Session

from app.models.database import SessionLocal
from app.models.models import BatteryReading, Pack, Reading, BmsSnapshot, BmsCommand

log = logging.getLogger("wbms.mqtt")

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

MQTT_BROKER = os.getenv("MQTT_BROKER", "localhost")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_TOPIC = os.getenv("MQTT_TOPIC", "bms/data")
MQTT_SNAPSHOT_TOPIC = os.getenv("MQTT_SNAPSHOT_TOPIC", "bms/snapshot")
MQTT_USERNAME = os.getenv("MQTT_USERNAME") or None
MQTT_PASSWORD = os.getenv("MQTT_PASSWORD") or None
MQTT_CLIENT_ID = os.getenv("MQTT_CLIENT_ID", "wbms-backend")
MQTT_CMD_TOPIC_PREFIX = os.getenv("MQTT_CMD_TOPIC_PREFIX", "bms/cmd/")
CONNECTED_CELLS_DEFAULT = int(os.getenv("CONNECTED_CELLS", "3"))

# OTA-related config — read here so all MQTT-adjacent settings live together.
FIRMWARE_DIR = os.getenv("FIRMWARE_DIR", "./firmware_artifacts")
OTA_URL_SECRET = os.getenv("OTA_URL_SECRET", "dev-ota-secret-change-in-production")
OTA_BLOB_TTL_SECONDS = int(os.getenv("OTA_BLOB_TTL_SECONDS", "300"))

# Nominal LiFePO4/Li-ion SOC curve endpoints (volts per cell)
_CELL_V_EMPTY = 3.0
_CELL_V_FULL = 4.2

# Module-level client so start/stop can manage a single instance.
_client: Optional[mqtt.Client] = None
_client_lock = threading.Lock()


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _resolve_pairing_code(payload: dict[str, Any]) -> Optional[str]:
    """Extract the pairing code from the MQTT payload."""
    code = payload.get("pairingCode")
    if code and str(code) != "unknown":
        return str(code).strip().upper()
    return None


def _estimate_soc(cell_voltages_v: list[float]) -> float:
    """Simple linear SoC estimate from the mean cell voltage."""
    if not cell_voltages_v:
        return 0.0
    mean_v = sum(cell_voltages_v) / len(cell_voltages_v)
    pct = (mean_v - _CELL_V_EMPTY) / (_CELL_V_FULL - _CELL_V_EMPTY) * 100.0
    return max(0.0, min(100.0, pct))


def _extract_cell_voltages(payload: dict[str, Any]) -> dict[int, float]:
    """Return {position: voltage_volts} for keys v1..v16 present in payload.

    Firmware reports cell voltages in millivolts; we convert to volts here so
    the rest of the persistence path operates in SI units.
    """
    cells: dict[int, float] = {}
    for i in range(1, 17):
        val = payload.get(f"v{i}")
        if val is None:
            continue
        try:
            mv = float(val)
        except (TypeError, ValueError):
            continue
        if mv <= 0:
            continue
        cells[i] = mv / 1000.0
    return cells


def _charging_discharging_flag(payload: dict[str, Any]) -> Optional[bool]:
    """Map firmware's split isCharging/isDischarging into a single tri-state.

    True  = charging, False = discharging, None = idle / unknown.
    """
    if payload.get("isCharging"):
        return True
    if payload.get("isDischarging"):
        return False
    return None


def _update_master_metadata(db: Session, pack: Pack, payload: dict[str, Any]) -> None:
    """Refresh fields the master self-reports in telemetry (fw version, its
    own pairing code). Only writes when the reported value differs to keep
    commit churn down — telemetry arrives at 2 Hz.
    """
    changed = False

    fw = payload.get("fwVersion")
    if isinstance(fw, str) and fw.strip():
        fw = fw.strip()
        if pack.master_firmware_version != fw:
            pack.master_firmware_version = fw
            changed = True

    mpc = payload.get("masterPairingCode")
    if isinstance(mpc, str) and mpc.strip():
        mpc = mpc.strip().upper()
        if pack.master_pairing_code != mpc:
            pack.master_pairing_code = mpc
            changed = True

    if changed:
        db.commit()
        db.refresh(pack)


def _persist_reading(db: Session, pack: Pack, payload: dict[str, Any]) -> None:
    cell_voltages = _extract_cell_voltages(payload)

    # Pack-level electricals
    v_pack_mv = payload.get("vPack") or payload.get("vStack") or 0
    try:
        v_real = float(v_pack_mv) / 1000.0  # mV → V
    except (TypeError, ValueError):
        v_real = 0.0

    try:
        current_a = float(payload.get("current", 0)) / 1000.0  # mA → A
    except (TypeError, ValueError):
        current_a = 0.0

    # Individual thermistor temps (kept separately for the spatial heatmap),
    # plus their mean for the single-value `temperature` display column.
    def _as_float(key: str) -> Optional[float]:
        val = payload.get(key)
        if val is None:
            return None
        try:
            return float(val)
        except (TypeError, ValueError):
            return None

    temp1_v = _as_float("temp1")
    temp2_v = _as_float("temp2")
    temp3_v = _as_float("temp3")
    present_temps = [t for t in (temp1_v, temp2_v, temp3_v) if t is not None]
    if present_temps:
        temperature = sum(present_temps) / len(present_temps)
    else:
        temperature = _as_float("chipTemp") or 0.0

    # Prefer the master's EKF SoC (published as 'soc'); only fall back to a
    # linear estimate for old firmware that doesn't send it.
    soc = _as_float("soc")
    if soc is None:
        soc = _estimate_soc(list(cell_voltages.values()))
    # Real SOH from the slave's SOHTracker (published as 'soh'); 100 if absent.
    soh = _as_float("soh")
    if soh is None:
        soh = 100.0

    # Coulomb-counter accumulators as reported by firmware.
    charge_ah = _as_float("charge")
    raw_charge_time = payload.get("chargeTime")
    try:
        charge_time_s = int(raw_charge_time) if raw_charge_time is not None else None
    except (TypeError, ValueError):
        charge_time_s = None

    # Latched BQ76952 Safety Status bytes -> persisted so the cloud can raise
    # named protection alerts (OC/OV/UV/SC/OT/UT...).
    def _as_u8(key: str) -> Optional[int]:
        val = payload.get(key)
        if val is None:
            return None
        try:
            return int(val) & 0xFF
        except (TypeError, ValueError):
            return None

    ss_a = _as_u8("ssA")
    ss_b = _as_u8("ssB")
    ss_c = _as_u8("ssC")

    power = v_real * current_a
    now = datetime.now(timezone.utc).replace(tzinfo=None)

    reading = Reading(
        timestamp=now,
        pack_id=pack.id,
        v_real=v_real,
        current=current_a,
        temperature=temperature,
        temp1=temp1_v,
        temp2=temp2_v,
        temp3=temp3_v,
        cycles=0,
        v_estimated=v_real,
        soc=soc,
        soh=soh,
        ekf_soc=soc,
        power=power,
        charging_discharging=_charging_discharging_flag(payload),
        charge=charge_ah,
        charge_time=charge_time_s,
        ss_a=ss_a,
        ss_b=ss_b,
        ss_c=ss_c,
    )
    db.add(reading)

    for position, voltage_v in cell_voltages.items():
        db.add(
            BatteryReading(
                battery_position=position,
                timestamp=now,
                pack_id=pack.id,
                voltage=voltage_v,
            )
        )

    db.commit()


def _handle_payload(raw: bytes) -> None:
    try:
        payload = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        log.warning("Dropping malformed MQTT payload: %s", exc)
        return

    if not isinstance(payload, dict):
        log.warning("Dropping non-object MQTT payload: %r", type(payload).__name__)
        return

    pairing_code = _resolve_pairing_code(payload)
    if not pairing_code:
        log.warning("MQTT payload has no valid pairingCode: %r", payload)
        return

    db = SessionLocal()
    try:
        pack = (
            db.query(Pack).filter(Pack.pairing_code == pairing_code).first()
        )
        if pack is None:
            # Auto-create pack from first MQTT message
            connected_cells = CONNECTED_CELLS_DEFAULT
            raw_cells = payload.get("connectedCells")
            if raw_cells is not None:
                try:
                    connected_cells = max(1, min(16, int(raw_cells)))
                except (TypeError, ValueError):
                    pass

            pack = Pack(
                name=f"Pack {pairing_code}",
                pack_identifier=f"wbms-{pairing_code.lower()}",
                pairing_code=pairing_code,
                series_count=connected_cells,
                parallel_count=1,
                user_id=None,
                auto_created=True,
            )
            try:
                db.add(pack)
                db.commit()
                db.refresh(pack)
                log.info(
                    "Auto-created pack %r (series=%d) from MQTT data",
                    pairing_code, connected_cells,
                )
            except IntegrityError:
                db.rollback()
                pack = db.query(Pack).filter(Pack.pairing_code == pairing_code).first()
                if pack is None:
                    log.error("Race condition: could not create or find pack %r", pairing_code)
                    return
        else:
            # Warn if firmware config changed
            incoming_cells = payload.get("connectedCells")
            if incoming_cells is not None:
                try:
                    incoming_cells = int(incoming_cells)
                    if incoming_cells != pack.series_count:
                        log.warning(
                            "Pack %r has series_count=%d but firmware reports connectedCells=%d",
                            pairing_code, pack.series_count, incoming_cells,
                        )
                except (TypeError, ValueError):
                    pass

        _update_master_metadata(db, pack, payload)
        _persist_reading(db, pack, payload)
        _mark_command(db, pack, payload)
    except Exception:
        db.rollback()
        log.exception("Failed to persist MQTT reading for pack=%r", pairing_code)
    finally:
        db.close()


def _mark_command(db: Session, pack: Pack, payload: dict[str, Any]) -> None:
    """Confirm dispatched admin commands. The slave echoes the last applied
    command seq + result code (lastCmdSeq/lastCmdRc) in telemetry; mark all
    still-pending commands with seq <= that applied (or failed if rc >= 10).
    Idempotent — a no-op if nothing matches (e.g. ack arrived before the insert)."""
    seq = payload.get("lastCmdSeq")
    if seq is None:
        return
    try:
        seq = int(seq)
        rc = int(payload.get("lastCmdRc", 0))
    except (TypeError, ValueError):
        return
    if seq <= 0:
        return
    pending = (
        db.query(BmsCommand)
        .filter(BmsCommand.pack_id == pack.id, BmsCommand.status == "pending", BmsCommand.seq <= seq)
        .all()
    )
    if not pending:
        return
    now = datetime.now(timezone.utc).replace(tzinfo=None)
    for cmd in pending:
        # Only the exact-seq command carries the result code; earlier ones are
        # inferred applied (the device processes commands in seq order).
        cmd.status = ("failed" if rc >= 10 else "applied") if cmd.seq == seq else "applied"
        cmd.acked_at = now
    db.commit()


def _handle_snapshot(raw: bytes) -> None:
    """Persist an on-demand full BMS snapshot (one latest row per pack)."""
    try:
        payload = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        log.warning("Dropping malformed snapshot payload: %s", exc)
        return
    if not isinstance(payload, dict):
        return
    pairing_code = _resolve_pairing_code(payload)
    if not pairing_code:
        log.warning("Snapshot has no valid pairingCode")
        return

    db = SessionLocal()
    try:
        pack = db.query(Pack).filter(Pack.pairing_code == pairing_code).first()
        if pack is None:
            log.warning("Snapshot for unknown pack %r, dropping", pairing_code)
            return
        now = datetime.now(timezone.utc).replace(tzinfo=None)
        body = json.dumps(payload)
        row = db.query(BmsSnapshot).filter(BmsSnapshot.pack_id == pack.id).first()
        if row is None:
            db.add(BmsSnapshot(pack_id=pack.id, payload=body, received_at=now))
        else:
            row.payload = body
            row.received_at = now
        db.commit()
        log.info("Stored BMS snapshot for pack %r", pairing_code)
    except Exception:
        db.rollback()
        log.exception("Failed to store snapshot for pack=%r", pairing_code)
    finally:
        db.close()


# ---------------------------------------------------------------------------
# paho-mqtt callbacks
# ---------------------------------------------------------------------------

def _on_connect(client, _userdata, _flags, reason_code, _properties=None):
    # paho-mqtt v2 passes a ReasonCode; v1 passed an int. Both stringify.
    log.info("MQTT connected (reason=%s), subscribing to %r + %r", reason_code, MQTT_TOPIC, MQTT_SNAPSHOT_TOPIC)
    client.subscribe(MQTT_TOPIC, qos=0)
    client.subscribe(MQTT_SNAPSHOT_TOPIC, qos=1)  # snapshots are admin-requested; qos1


def _on_disconnect(_client, _userdata, _flags, reason_code, _properties=None):
    log.warning("MQTT disconnected (reason=%s); paho will auto-reconnect", reason_code)


def _on_message(_client, _userdata, msg):
    if msg.topic == MQTT_SNAPSHOT_TOPIC:
        _handle_snapshot(msg.payload)
    else:
        _handle_payload(msg.payload)


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def start_mqtt() -> None:
    """Start the MQTT subscriber in a background thread.

    Safe to call multiple times — subsequent calls are no-ops. Broker
    unavailability at startup does NOT raise; paho will keep retrying.
    """
    global _client
    with _client_lock:
        if _client is not None:
            return

        client = mqtt.Client(
            client_id=MQTT_CLIENT_ID,
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        )
        if MQTT_USERNAME:
            client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)

        client.on_connect = _on_connect
        client.on_disconnect = _on_disconnect
        client.on_message = _on_message
        # paho handles reconnect automatically; tune the backoff a bit.
        client.reconnect_delay_set(min_delay=1, max_delay=30)

        try:
            client.connect_async(MQTT_BROKER, MQTT_PORT, keepalive=60)
        except Exception as exc:
            log.warning(
                "MQTT initial connect_async to %s:%s failed: %s (will keep retrying)",
                MQTT_BROKER,
                MQTT_PORT,
                exc,
            )

        client.loop_start()
        _client = client
        log.info("MQTT subscriber started (broker=%s:%s)", MQTT_BROKER, MQTT_PORT)


def stop_mqtt() -> None:
    """Stop the MQTT subscriber. Safe to call if not started."""
    global _client
    with _client_lock:
        if _client is None:
            return
        try:
            _client.loop_stop()
            _client.disconnect()
        except Exception:
            log.exception("Error while stopping MQTT client")
        _client = None
        log.info("MQTT subscriber stopped")


def publish_command(pairing_code: str, payload: dict[str, Any]) -> bool:
    """Publish a command payload to `bms/cmd/<pairing_code>` over the shared
    MQTT client. Returns True on enqueue success.

    Used by the OTA dispatch route. The client may not be connected yet —
    paho will retry the underlying socket, and a publish before connection
    is queued by paho when `loop_start()` is running.
    """
    code = (pairing_code or "").strip().upper()
    if not code:
        log.warning("publish_command called with empty pairing_code")
        return False

    with _client_lock:
        client = _client

    if client is None:
        log.warning("publish_command: MQTT client not started")
        return False

    topic = f"{MQTT_CMD_TOPIC_PREFIX}{code}"
    body = json.dumps(payload, separators=(",", ":"))
    info = client.publish(topic, body, qos=1)
    log.info("MQTT command -> %s (%d bytes), rc=%s", topic, len(body), info.rc)
    return info.rc == mqtt.MQTT_ERR_SUCCESS
