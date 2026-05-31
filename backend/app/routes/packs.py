import csv
import io
import random
from datetime import datetime, timezone
from typing import Optional

from fastapi import APIRouter, Depends, HTTPException, Query, Request, status
from fastapi.responses import StreamingResponse
from sqlalchemy import func
from sqlalchemy.orm import Session

from app.auth import get_current_user
from app.models.database import get_db
from app.models.models import User, Pack, Reading, BatteryReading
from app.models.schemas import FirmwareDispatchRequest, PackCreate, PackClaim, PackResponse
from app.routes.firmware import dispatch_ota

router = APIRouter(prefix="/v1/packs", tags=["packs"])

# A pack that hasn't sent telemetry within this window is treated as offline.
# Its last real reading is still returned, but flagged `stale` so the UI can
# label it instead of presenting frozen values as if they were live.
STALE_AFTER_SECONDS = 30


# Latched BQ76952 Safety Status bit -> (code, human label). These are the
# protections that today only show on the slave's local AP; the master now
# forwards the raw bytes (ssA/ssB/ssC) so the cloud can alert on them.
_SS_A_BITS = {
    7: ("SCD", "Short-Circuit in Discharge"), 6: ("OCD2", "Overcurrent in Discharge 2"),
    5: ("OCD1", "Overcurrent in Discharge 1"), 4: ("OCC", "Overcurrent in Charge"),
    3: ("COV", "Cell Overvoltage"), 2: ("CUV", "Cell Undervoltage"),
    1: ("SFD", "Secondary Fault Detected"), 0: ("OTP", "OTP Access Fault"),
}
_SS_B_BITS = {
    7: ("OTF", "Overtemperature FET"), 6: ("OTINT", "Overtemperature Internal"),
    5: ("OTD", "Overtemperature in Discharge"), 4: ("OTC", "Overtemperature in Charge"),
    2: ("UTINT", "Undertemperature Internal"), 1: ("UTD", "Undertemperature in Discharge"),
    0: ("UTC", "Undertemperature in Charge"),
}
_SS_C_BITS = {
    7: ("HWDF", "Hardware Diagnostic Fault"), 6: ("PTO", "Precharge Timeout"),
    5: ("COVL", "Cell Overvoltage Latch"), 3: ("SCDL", "Short-Circuit Discharge Latch"),
    2: ("OCDL", "Overcurrent Discharge Latch"),
}


def decode_protections(ss_a, ss_b, ss_c) -> list:
    """Decode latched Safety Status bytes into active protection faults, e.g.
    [{"code": "OCD1", "label": "Overcurrent in Discharge 1"}]. Empty == no fault."""
    out = []
    for reg, bits in ((ss_a, _SS_A_BITS), (ss_b, _SS_B_BITS), (ss_c, _SS_C_BITS)):
        if not reg:
            continue
        try:
            reg = int(reg)
        except (TypeError, ValueError):
            continue
        for bit, (code, label) in bits.items():
            if reg & (1 << bit):
                out.append({"code": code, "label": label})
    return out


@router.post("", response_model=PackResponse, status_code=status.HTTP_201_CREATED)
def create_pack(
    pack_data: PackCreate,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    existing = db.query(Pack).filter(Pack.pack_identifier == pack_data.pack_identifier).first()
    if existing:
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail="A pack with this identifier already exists",
        )

    pairing_code = pack_data.pairing_code or pack_data.pack_identifier.upper()
    pack = Pack(
        name=pack_data.name,
        pack_identifier=pack_data.pack_identifier,
        pairing_code=pairing_code,
        series_count=pack_data.series_count,
        parallel_count=pack_data.parallel_count,
        user_id=current_user.id,
        auto_created=False,
    )
    db.add(pack)
    db.commit()
    db.refresh(pack)
    return pack


@router.get("", response_model=list[PackResponse])
def list_packs(
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    return db.query(Pack).filter(Pack.user_id == current_user.id).all()


@router.post("/claim", response_model=PackResponse)
def claim_pack(
    claim_data: PackClaim,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    code = claim_data.pairing_code.strip().upper()
    pack = db.query(Pack).filter(Pack.pairing_code == code).first()
    if not pack:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail="No device found with this pairing code. Make sure your pack is powered on and connected.",
        )
    if pack.user_id is not None:
        if pack.user_id == current_user.id:
            return pack  # Already claimed by this user
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail="This pack is already claimed by another user",
        )
    pack.user_id = current_user.id
    db.commit()
    db.refresh(pack)
    return pack


@router.delete("/{pack_id}", status_code=status.HTTP_204_NO_CONTENT)
def delete_pack(
    pack_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    pack = db.query(Pack).filter(Pack.id == pack_id, Pack.user_id == current_user.id).first()
    if not pack:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Pack not found")
    db.delete(pack)
    db.commit()


@router.post("/{pack_id}/ota")
def trigger_pack_ota(
    pack_id: int,
    payload: FirmwareDispatchRequest,
    request: Request,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    return dispatch_ota(pack_id, payload, request, current_user, db)


@router.get("/data/latest")
def get_latest_pack_data(
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    packs = db.query(Pack).filter(Pack.user_id == current_user.id).all()
    now_utc = datetime.utcnow()

    battery_packs = []
    for pack in packs:
        series = pack.series_count
        parallel = pack.parallel_count
        total_cells = series * parallel

        # Stable per-pack RNG so any fabricated/demo values are identical on
        # every poll (seeded by the pack's id) instead of jittering each time,
        # which would otherwise read as a broken live feed.
        rng = random.Random(pack.id)

        # Try to get latest real reading from DB
        latest_reading = (
            db.query(Reading)
            .filter(Reading.pack_id == pack.id)
            .order_by(Reading.timestamp.desc())
            .first()
        )

        if latest_reading:
            demo = False
            age_s = (now_utc - latest_reading.timestamp).total_seconds()
            stale = age_s > STALE_AFTER_SECONDS
            last_update = latest_reading.timestamp.replace(microsecond=0).isoformat() + "Z"
            soc = round(latest_reading.soc)
            soh = round(latest_reading.soh)
            voltage = f"{latest_reading.v_real:.2f}"
            current_val = f"{latest_reading.current:.2f}"
            temp = f"{latest_reading.temperature:.2f}"

            # Spatial thermistor values for the heatmap. Use the real per-sensor
            # readings when the firmware reported them; otherwise spread the
            # single mean temp across all three (renders flat = "no spatial
            # data" rather than a fabricated gradient).
            t1, t2, t3 = latest_reading.temp1, latest_reading.temp2, latest_reading.temp3
            if t1 is not None and t2 is not None and t3 is not None:
                thermistors = [{"value": f"{t1:.2f}"}, {"value": f"{t2:.2f}"}, {"value": f"{t3:.2f}"}]
            else:
                thermistors = [{"value": temp}, {"value": temp}, {"value": temp}]

            # Get latest voltage per cell position (correlated subquery to
            # avoid picking multiple rows from the same cell when several
            # readings share a timestamp or arrive in bursts).
            latest_per_cell = (
                db.query(
                    BatteryReading.battery_position.label("pos"),
                    func.max(BatteryReading.timestamp).label("max_ts"),
                )
                .filter(BatteryReading.pack_id == pack.id)
                .group_by(BatteryReading.battery_position)
                .subquery()
            )
            cell_readings = (
                db.query(BatteryReading)
                .join(
                    latest_per_cell,
                    (BatteryReading.battery_position == latest_per_cell.c.pos)
                    & (BatteryReading.timestamp == latest_per_cell.c.max_ts),
                )
                .filter(BatteryReading.pack_id == pack.id)
                .all()
            )

            if cell_readings:
                cells = []
                for cr in sorted(cell_readings, key=lambda x: x.battery_position):
                    v = cr.voltage
                    # Three bands so a severe single-cell excursion renders red
                    # ('alert') and raises the "Cell Out of Range" alarm, instead
                    # of being capped at amber 'caution'.
                    if v < 3.0 or v > 4.25:
                        cell_status = "alert"
                    elif v < 3.3 or v > 4.1:
                        cell_status = "caution"
                    else:
                        cell_status = "safe"
                    cells.append({"value": f"{v:.2f}", "status": cell_status})
                # Pad if not enough cell readings
                while len(cells) < total_cells:
                    cells.append({"value": "0.00", "status": "safe"})
                # Truncate if pack was shrunk
                cells = cells[:total_cells]
            else:
                cells = _mock_cells(total_cells, rng)

            # Active BQ76952 protection faults (latched) forwarded from the BMS.
            protections = decode_protections(
                latest_reading.ss_a, latest_reading.ss_b, latest_reading.ss_c
            )

            pack_status = "safe"
            if protections or soc < 20 or soh < 80:
                pack_status = "alert"
            elif soc < 40 or soh < 90:
                pack_status = "caution"
        else:
            # No telemetry yet (e.g. a manually-added or not-yet-connected
            # pack, or a fake pairing code). Show a STABLE, clearly-labeled
            # demo preview so the card isn't empty — the frontend renders a
            # "DEMO" badge for these rather than passing them off as live.
            demo = True
            stale = False
            last_update = None
            soc = rng.randint(50, 100)
            soh = rng.randint(90, 100)
            voltage = f"{rng.uniform(3.2 * series, 4.2 * series):.2f}"
            current_val = f"{rng.uniform(5.0, 15.0):.2f}"
            base_temp = rng.uniform(25.0, 40.0)
            temp = f"{base_temp:.2f}"
            # Stable per-pack spatial spread (middle runs hotter, like a real
            # pack) so the demo heatmap looks plausible and doesn't jitter.
            thermistors = [
                {"value": f"{base_temp + rng.uniform(-2.0, 1.0):.2f}"},
                {"value": f"{base_temp + rng.uniform(1.0, 3.0):.2f}"},
                {"value": f"{base_temp + rng.uniform(-2.0, 1.0):.2f}"},
            ]
            cells = _mock_cells(total_cells, rng)
            pack_status = "safe"
            protections = []

        battery_packs.append({
            "name": pack.name,
            "id": pack.pack_identifier,
            "status": pack_status,
            "soc": soc,
            "soh": soh,
            "voltage": voltage,
            "current": current_val,
            "temp": temp,
            "config": f"{series}S{parallel}P ({total_cells} cells)",
            "series": str(series),
            "parallel": str(parallel),
            "cells": cells,
            "thermistors": thermistors,
            "protections": protections,
            "demo": demo,
            "stale": stale,
            "last_update": last_update,
        })

    return {
        "status": "success",
        "timestamp": datetime.now().replace(microsecond=0).isoformat(),
        "packs": battery_packs,
    }


# Columns shared by the JSON preview and the CSV export so the on-screen table
# and the downloaded file always match exactly.
_EXPORT_COLUMNS = [
    "timestamp", "soc", "soh", "voltage_v", "current_a",
    "temperature_c", "temp1_c", "temp2_c", "temp3_c", "power_w", "state",
]


def _parse_iso(value: Optional[str], field: str) -> Optional[datetime]:
    """Parse an ISO-8601 string to a naive UTC datetime (timestamps are stored
    tz-naive UTC). Tolerates a trailing 'Z'. Returns None for empty input."""
    if not value:
        return None
    try:
        dt = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        raise HTTPException(status_code=400, detail=f"Invalid {field} (use ISO-8601)")
    # Timestamps are stored UTC-naive; normalize tz-aware input to UTC-naive so
    # the comparison is correct regardless of the server's local timezone. A
    # naive input is taken to already be UTC.
    if dt.tzinfo is not None:
        dt = dt.astimezone(timezone.utc).replace(tzinfo=None)
    return dt


def _round(value, ndigits):
    return None if value is None else round(value, ndigits)


def _reading_row(r: Reading) -> list:
    """One reading as a list aligned to _EXPORT_COLUMNS. None for missing values
    (JSON serializes as null; the CSV writer maps them to empty cells)."""
    if r.charging_discharging is None:
        state = "idle"
    else:
        state = "charging" if r.charging_discharging else "discharging"
    return [
        r.timestamp.replace(microsecond=0).isoformat(),
        _round(r.soc, 2), _round(r.soh, 2),
        _round(r.v_real, 3), _round(r.current, 3),
        _round(r.temperature, 2), _round(r.temp1, 2), _round(r.temp2, 2), _round(r.temp3, 2),
        _round(r.power, 2), state,
    ]


@router.get("/{pack_id}/readings")
def get_pack_readings(
    pack_id: int,
    start: Optional[str] = None,
    end: Optional[str] = None,
    limit: int = Query(100, ge=1, le=100_000),
    format: str = Query("json", pattern="^(json|csv)$"),
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Historical readings for one owned pack over an optional time range.

    `format=json` returns the most recent `limit` rows (chronological) for an
    on-screen preview; `format=csv` streams the range as a downloadable file.
    Rows are arrays aligned to the `columns` list so preview and CSV match.
    """
    pack = db.query(Pack).filter(
        Pack.id == pack_id, Pack.user_id == current_user.id
    ).first()
    if not pack:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Pack not found")

    start_dt = _parse_iso(start, "start")
    end_dt = _parse_iso(end, "end")

    q = db.query(Reading).filter(Reading.pack_id == pack.id)
    if start_dt is not None:
        q = q.filter(Reading.timestamp >= start_dt)
    if end_dt is not None:
        q = q.filter(Reading.timestamp <= end_dt)

    if format == "csv":
        rows = q.order_by(Reading.timestamp.asc()).limit(limit).all()
        buf = io.StringIO()
        writer = csv.writer(buf)
        writer.writerow(_EXPORT_COLUMNS)
        for r in rows:
            writer.writerow(["" if v is None else v for v in _reading_row(r)])
        buf.seek(0)
        filename = f"{pack.pack_identifier}-readings.csv"
        return StreamingResponse(
            iter([buf.getvalue()]),
            media_type="text/csv",
            headers={"Content-Disposition": f'attachment; filename="{filename}"'},
        )

    # JSON preview: newest `limit` rows, returned oldest→newest for display.
    rows = q.order_by(Reading.timestamp.desc()).limit(limit).all()
    rows.reverse()
    return {
        "pack": {"id": pack.id, "identifier": pack.pack_identifier, "name": pack.name},
        "columns": _EXPORT_COLUMNS,
        "count": len(rows),
        "rows": [_reading_row(r) for r in rows],
    }


@router.get("/{pack_id}/charge-stats")
def get_pack_charge_stats(
    pack_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Charge statistics for one owned pack, computed from stored DB readings
    (not a client-side session series). Uses cheap queries plus one bounded scan
    for the cycle count so it stays fast on large telemetry tables.
    """
    pack = db.query(Pack).filter(
        Pack.id == pack_id, Pack.user_id == current_user.id
    ).first()
    if not pack:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Pack not found")

    latest = (
        db.query(Reading)
        .filter(Reading.pack_id == pack.id)
        .order_by(Reading.timestamp.desc())
        .first()
    )
    if latest is None:
        return {
            "current_soc": None, "current_soh": None,
            "last_charged": None, "last_charged_to": None,
            "time_to_empty_min": None, "cycles": 0, "based_on_rows": 0,
            "accumulated_charge_ah": None, "charge_time_s": None,
        }

    current_soc = latest.soc
    current_amps = latest.current

    # Time to empty: only meaningful while discharging (negative current).
    ASSUMED_CAPACITY_AH = 20.0
    time_to_empty_min = None
    if current_amps is not None and current_amps < -0.05 and current_soc:
        hours_left = (current_soc / 100.0) * ASSUMED_CAPACITY_AH / abs(current_amps)
        time_to_empty_min = round(hours_left * 60.0, 1)

    # Last charge: most recent reading flagged as charging. 'Z' marks UTC so the
    # browser renders "time since" correctly regardless of its timezone.
    last_chg = (
        db.query(Reading)
        .filter(Reading.pack_id == pack.id, Reading.charging_discharging.is_(True))
        .order_by(Reading.timestamp.desc())
        .first()
    )
    last_charged = None
    last_charged_to = None
    if last_chg is not None:
        last_charged = last_chg.timestamp.replace(microsecond=0).isoformat() + "Z"
        last_charged_to = round(last_chg.soc, 1) if last_chg.soc is not None else None

    # Cycle count: count rising edges of the charging flag over a bounded recent
    # window (telemetry is ~2 Hz, so cap the scan rather than read the whole table).
    CYCLE_WINDOW_ROWS = 20000
    flag_rows = (
        db.query(Reading.charging_discharging)
        .filter(Reading.pack_id == pack.id)
        .order_by(Reading.timestamp.desc())
        .limit(CYCLE_WINDOW_ROWS)
        .all()
    )
    cycles = 0
    prev_charging = False
    for (flag,) in reversed(flag_rows):  # walk oldest -> newest
        charging = flag is True
        if charging and not prev_charging:
            cycles += 1
        prev_charging = charging

    return {
        "current_soc": round(current_soc, 1) if current_soc is not None else None,
        "current_soh": round(latest.soh, 1) if latest.soh is not None else None,
        "last_charged": last_charged,
        "last_charged_to": last_charged_to,
        "time_to_empty_min": time_to_empty_min,
        "cycles": cycles,
        "based_on_rows": len(flag_rows),
        # Real coulomb-counter accumulators from the BQ76952 (previously dropped).
        "accumulated_charge_ah": round(latest.charge, 3) if latest.charge is not None else None,
        "charge_time_s": latest.charge_time,
    }


def _mock_cells(count: int, rng: random.Random) -> list:
    cells = []
    for _ in range(count):
        cell_voltage = round(rng.uniform(3.2, 4.2), 2)
        cell_status = "caution" if cell_voltage < 3.3 or cell_voltage > 4.1 else "safe"
        cells.append({"value": f"{cell_voltage:.2f}", "status": cell_status})
    return cells
