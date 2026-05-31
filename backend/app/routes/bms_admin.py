"""Admin-only cloud BMS console.

Lets an admin drive a pack's BMS configuration remotely (mirroring the slave's
local AP dashboard). Commands are validated + clamped here, published to the
master's MQTT command topic, bridged to the slave over ESP-NOW, and confirmed by
the slave echoing the command seq back in telemetry (see mqtt_subscriber).

Every route is gated by `require_admin`. The action allowlist + arg clamping
means the device can never receive an unknown or out-of-range command even if a
client is tampered with.
"""

import json as _json
from datetime import datetime, timezone

from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy import func
from sqlalchemy.orm import Session

from app.auth import require_admin
from app.models.database import get_db
from app.models.models import User, Pack, Reading, BmsSnapshot, BmsCommand
from app.models.schemas import BmsCommandRequest
from app.mqtt_subscriber import publish_command

router = APIRouter(prefix="/v1/packs", tags=["bms-admin"])

# A command is only dispatched if telemetry is fresher than this (pack online).
ONLINE_MAX_AGE_S = 30

# action -> {args: {name: (min, max)}, dangerous: bool, reboots: bool}
# Ranges mirror the slave AP dashboard (web_api.h). SCD/SCD_d are enum indices.
_ALLOWED = {
    # Protection thresholds (one logical command; all 12 required; reboots ~45s)
    "setProtection": {"reboots": True, "args": {
        "cuv": (2000, 3500), "cuv_d": (3, 419), "cov": (3500, 4500), "cov_d": (3, 419),
        "occ": (1, 50), "occ_d": (3, 419), "ocd1": (1, 100), "ocd1_d": (3, 419),
        "ocd2": (1, 100), "ocd2_d": (3, 419), "scd": (0, 9), "scd_d": (0, 7),
    }},
    # Balancing
    "toggleBal": {}, "toggleBalMaster": {},
    "setHostBalParams": {"args": {"trigger": (0, 5000), "delta": (0, 500)}},
    # FET controls (dangerous — can disconnect/connect the pack remotely)
    "chgTog": {"dangerous": True}, "dsgTog": {"dangerous": True},
    "pchgTog": {"dangerous": True}, "pdsgTog": {"dangerous": True},
    "allFetsOn": {"dangerous": True}, "allFetsOff": {"dangerous": True},
    "fetMasterToggle": {"dangerous": True},
    # Resets
    "reset": {"reboots": True, "dangerous": True}, "clearFaults": {},
    "pfReset": {"dangerous": True}, "resetCharge": {}, "ekfReset": {},
    # Power
    "pwrDeep": {"dangerous": True}, "pwrWake": {}, "toggleAutoSleep": {},
}


def _resolve_admin_pack(pack_id: int, db: Session) -> Pack:
    """Admin can target ANY pack (no ownership filter)."""
    pack = db.query(Pack).filter(Pack.id == pack_id).first()
    if not pack:
        raise HTTPException(status_code=404, detail="Pack not found")
    if not pack.master_pairing_code:
        raise HTTPException(
            status_code=409,
            detail="Pack's master has not reported in yet (no master pairing code)",
        )
    return pack


def _latest_reading_age(pack: Pack, db: Session):
    latest = (
        db.query(Reading)
        .filter(Reading.pack_id == pack.id)
        .order_by(Reading.timestamp.desc())
        .first()
    )
    if latest is None:
        return None
    return (datetime.utcnow() - latest.timestamp).total_seconds()


def _assert_online(pack: Pack, db: Session) -> None:
    age = _latest_reading_age(pack, db)
    if age is None or age > ONLINE_MAX_AGE_S:
        raise HTTPException(
            status_code=409,
            detail="Pack is offline (no telemetry in the last 30s) — cannot dispatch commands",
        )


def _clamp_args(action: str, raw: dict) -> dict:
    """Validate + clamp args against the allowlist. Drops unknown keys. For
    setProtection all 12 keys are required (a partial write would zero the rest
    and disable protections)."""
    spec = _ALLOWED[action].get("args", {})
    out = {}
    if action == "setProtection":
        missing = [k for k in spec if k not in raw]
        if missing:
            raise HTTPException(status_code=400, detail=f"setProtection missing: {missing}")
    for key, (lo, hi) in spec.items():
        if key not in raw:
            continue
        try:
            v = int(raw[key])
        except (TypeError, ValueError):
            raise HTTPException(status_code=400, detail=f"Arg {key} must be an integer")
        out[key] = max(lo, min(hi, v))
    return out


def _next_seq(pack: Pack, db: Session) -> int:
    cur = db.query(func.max(BmsCommand.seq)).filter(BmsCommand.pack_id == pack.id).scalar()
    # seq rides a uint16 on the wire; wrap well below 65535.
    return ((cur or 0) % 60000) + 1


@router.post("/{pack_id}/bms/command")
def dispatch_bms_command(
    pack_id: int,
    body: BmsCommandRequest,
    current_user: User = Depends(require_admin),
    db: Session = Depends(get_db),
):
    action = body.action
    if action not in _ALLOWED:
        raise HTTPException(status_code=400, detail=f"Unknown action '{action}'")

    pack = _resolve_admin_pack(pack_id, db)
    _assert_online(pack, db)

    args = _clamp_args(action, body.args or {})
    seq = _next_seq(pack, db)

    cmd = BmsCommand(
        pack_id=pack.id, seq=seq, action=action,
        args=_json.dumps(args) if args else None,
        status="pending", issued_by=current_user.email,
    )
    db.add(cmd)
    db.commit()  # commit BEFORE publish so an echoed ack can never reference a missing row

    payload = {"op": "slavecmd", "target": pack.pairing_code, "action": action, "seq": seq}
    payload.update(args)
    ok = publish_command(pack.master_pairing_code, payload)
    if not ok:
        cmd.status = "failed"
        db.commit()
        raise HTTPException(status_code=502, detail="Failed to publish command to broker")

    meta = _ALLOWED[action]
    return {
        "seq": seq, "status": "pending",
        "dangerous": bool(meta.get("dangerous")), "reboots": bool(meta.get("reboots")),
    }


@router.post("/{pack_id}/bms/snapshot/request")
def request_bms_snapshot(
    pack_id: int,
    current_user: User = Depends(require_admin),
    db: Session = Depends(get_db),
):
    pack = _resolve_admin_pack(pack_id, db)
    _assert_online(pack, db)
    seq = _next_seq(pack, db)
    db.add(BmsCommand(pack_id=pack.id, seq=seq, action="snapshot",
                      status="pending", issued_by=current_user.email))
    db.commit()
    ok = publish_command(pack.master_pairing_code,
                         {"op": "snapshot", "target": pack.pairing_code, "seq": seq})
    if not ok:
        raise HTTPException(status_code=502, detail="Failed to publish snapshot request")
    return {"requested": True, "seq": seq}


@router.get("/{pack_id}/bms/snapshot")
def get_bms_snapshot(
    pack_id: int,
    current_user: User = Depends(require_admin),
    db: Session = Depends(get_db),
):
    pack = _resolve_admin_pack(pack_id, db)
    row = db.query(BmsSnapshot).filter(BmsSnapshot.pack_id == pack.id).first()
    if row is None:
        return {"payload": None, "age_s": None, "received_at": None}
    age = (datetime.utcnow() - row.received_at).total_seconds()
    try:
        payload = _json.loads(row.payload)
    except (TypeError, ValueError):
        payload = None
    return {
        "payload": payload,
        "age_s": round(age, 1),
        "received_at": row.received_at.replace(microsecond=0).isoformat() + "Z",
    }


@router.get("/{pack_id}/bms/command/{seq}")
def get_bms_command_status(
    pack_id: int,
    seq: int,
    current_user: User = Depends(require_admin),
    db: Session = Depends(get_db),
):
    pack = _resolve_admin_pack(pack_id, db)
    cmd = (
        db.query(BmsCommand)
        .filter(BmsCommand.pack_id == pack.id, BmsCommand.seq == seq)
        .order_by(BmsCommand.created_at.desc())
        .first()
    )
    if cmd is None:
        raise HTTPException(status_code=404, detail="Command not found")
    return {
        "seq": cmd.seq, "action": cmd.action, "status": cmd.status,
        "created_at": cmd.created_at.replace(microsecond=0).isoformat() + "Z",
        "acked_at": (cmd.acked_at.replace(microsecond=0).isoformat() + "Z") if cmd.acked_at else None,
    }
