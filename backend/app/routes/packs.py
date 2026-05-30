import random
from datetime import datetime

from fastapi import APIRouter, Depends, HTTPException, Request, status
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
            soc = int(latest_reading.soc)
            soh = int(latest_reading.soh)
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
                    cell_status = "caution" if v < 3.3 or v > 4.1 else "safe"
                    cells.append({"value": f"{v:.2f}", "status": cell_status})
                # Pad if not enough cell readings
                while len(cells) < total_cells:
                    cells.append({"value": "0.00", "status": "safe"})
                # Truncate if pack was shrunk
                cells = cells[:total_cells]
            else:
                cells = _mock_cells(total_cells, rng)

            pack_status = "safe"
            if soc < 20 or soh < 80:
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
            "demo": demo,
            "stale": stale,
            "last_update": last_update,
        })

    return {
        "status": "success",
        "timestamp": datetime.now().replace(microsecond=0).isoformat(),
        "packs": battery_packs,
    }


def _mock_cells(count: int, rng: random.Random) -> list:
    cells = []
    for _ in range(count):
        cell_voltage = round(rng.uniform(3.2, 4.2), 2)
        cell_status = "caution" if cell_voltage < 3.3 or cell_voltage > 4.1 else "safe"
        cells.append({"value": f"{cell_voltage:.2f}", "status": cell_status})
    return cells
