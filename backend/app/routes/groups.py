import random
from dataclasses import dataclass
from datetime import datetime

from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy import func
from sqlalchemy.orm import Session

from app.auth import get_current_user
from app.models.database import get_db
from app.models.models import User, Pack, PackGroup, Reading, BatteryReading
from app.models.schemas import GroupCreate, GroupUpdate, GroupAddPack, GroupResponse

router = APIRouter(prefix="/v1/groups", tags=["groups"])


def _serialize_group(group: PackGroup) -> dict:
    return {
        "id": group.id,
        "name": group.name,
        "connection_type": group.connection_type,
        "pack_ids": [p.id for p in group.packs],
    }


def _user_packs_by_id(db: Session, user_id: int, pack_ids: list[int]) -> list[Pack]:
    if not pack_ids:
        return []
    return (
        db.query(Pack)
        .filter(Pack.user_id == user_id, Pack.id.in_(pack_ids))
        .all()
    )


@router.post("", response_model=GroupResponse, status_code=status.HTTP_201_CREATED)
def create_group(
    payload: GroupCreate,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    group = PackGroup(
        name=payload.name.strip() or "Untitled Group",
        connection_type=payload.connection_type,
        user_id=current_user.id,
    )
    if payload.pack_ids:
        packs = _user_packs_by_id(db, current_user.id, payload.pack_ids)
        if len(packs) != len(set(payload.pack_ids)):
            raise HTTPException(
                status_code=status.HTTP_400_BAD_REQUEST,
                detail="One or more pack ids do not belong to you",
            )
        group.packs = packs
    db.add(group)
    db.commit()
    db.refresh(group)
    return _serialize_group(group)


@router.get("", response_model=list[GroupResponse])
def list_groups(
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    groups = db.query(PackGroup).filter(PackGroup.user_id == current_user.id).all()
    return [_serialize_group(g) for g in groups]


@router.patch("/{group_id}", response_model=GroupResponse)
def update_group(
    group_id: int,
    payload: GroupUpdate,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    group = db.query(PackGroup).filter(
        PackGroup.id == group_id, PackGroup.user_id == current_user.id
    ).first()
    if not group:
        raise HTTPException(status_code=404, detail="Group not found")
    if payload.name is not None:
        group.name = payload.name.strip() or group.name
    if payload.connection_type is not None:
        group.connection_type = payload.connection_type
    db.commit()
    db.refresh(group)
    return _serialize_group(group)


@router.delete("/{group_id}", status_code=status.HTTP_204_NO_CONTENT)
def delete_group(
    group_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Delete a group. Underlying packs are NOT deleted — they're modular."""
    group = db.query(PackGroup).filter(
        PackGroup.id == group_id, PackGroup.user_id == current_user.id
    ).first()
    if not group:
        raise HTTPException(status_code=404, detail="Group not found")
    db.delete(group)
    db.commit()


@router.post("/{group_id}/packs", response_model=GroupResponse)
def add_pack_to_group(
    group_id: int,
    payload: GroupAddPack,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    group = db.query(PackGroup).filter(
        PackGroup.id == group_id, PackGroup.user_id == current_user.id
    ).first()
    if not group:
        raise HTTPException(status_code=404, detail="Group not found")
    pack = db.query(Pack).filter(
        Pack.id == payload.pack_id, Pack.user_id == current_user.id
    ).first()
    if not pack:
        raise HTTPException(status_code=404, detail="Pack not found")
    if pack not in group.packs:
        group.packs.append(pack)
        db.commit()
        db.refresh(group)
    return _serialize_group(group)


@router.delete("/{group_id}/packs/{pack_id}", response_model=GroupResponse)
def remove_pack_from_group(
    group_id: int,
    pack_id: int,
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Remove a pack from a group (separates them — pack remains intact)."""
    group = db.query(PackGroup).filter(
        PackGroup.id == group_id, PackGroup.user_id == current_user.id
    ).first()
    if not group:
        raise HTTPException(status_code=404, detail="Group not found")
    group.packs = [p for p in group.packs if p.id != pack_id]
    db.commit()
    db.refresh(group)
    return _serialize_group(group)


@dataclass
class _PackSnapshot:
    v_real: float
    current: float
    temperature: float
    soc: float
    soh: float
    demo: bool = False


def _latest_reading_for_pack(db: Session, pack: Pack) -> _PackSnapshot:
    """Return latest pack reading. Falls back to mock data when no Reading rows exist
    so the combined view stays in sync with the per-pack endpoint."""
    r = (
        db.query(Reading)
        .filter(Reading.pack_id == pack.id)
        .order_by(Reading.timestamp.desc())
        .first()
    )
    if r:
        return _PackSnapshot(
            v_real=r.v_real,
            current=r.current,
            temperature=r.temperature,
            soc=r.soc,
            soh=r.soh,
            demo=False,
        )
    rng = random.Random(pack.id)
    series = pack.series_count
    return _PackSnapshot(
        v_real=round(rng.uniform(3.2 * series, 4.2 * series), 2),
        current=round(rng.uniform(5.0, 15.0), 2),
        temperature=round(rng.uniform(25.0, 40.0), 2),
        soc=float(rng.randint(50, 100)),
        soh=float(rng.randint(90, 100)),
        demo=True,
    )


def _latest_cells_for_pack(db: Session, pack: Pack) -> list[dict]:
    total_cells = pack.series_count * pack.parallel_count
    rng = random.Random(pack.id)
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
    cells = []
    for cr in sorted(cell_readings, key=lambda x: x.battery_position):
        v = cr.voltage
        if v < 3.0 or v > 4.25:
            status_ = "alert"
        elif v < 3.3 or v > 4.1:
            status_ = "caution"
        else:
            status_ = "safe"
        cells.append({"value": f"{v:.2f}", "status": status_, "pack_id": pack.id, "pack_name": pack.name})
    # Mock fill when no real cell readings exist
    while len(cells) < total_cells:
        v = round(rng.uniform(3.2, 4.2), 2)
        status_ = "caution" if v < 3.3 or v > 4.1 else "safe"
        cells.append({"value": f"{v:.2f}", "status": status_, "pack_id": pack.id, "pack_name": pack.name})
    return cells[:total_cells]


@router.get("/data/latest")
def get_latest_group_data(
    current_user: User = Depends(get_current_user),
    db: Session = Depends(get_db),
):
    """Return combined readings for each of the user's groups."""
    groups = db.query(PackGroup).filter(PackGroup.user_id == current_user.id).all()

    out = []
    for group in groups:
        if not group.packs:
            out.append({
                "id": group.id,
                "name": group.name,
                "connection_type": group.connection_type,
                "pack_ids": [],
                "members": [],
                "status": "safe",
                "soc": 0,
                "soh": 0,
                "voltage": "0.00",
                "current": "0.00",
                "temp": "0.00",
                "config": "empty",
                "series": "0",
                "parallel": "0",
                "cells": [],
                "demo": False,
            })
            continue

        readings = [(p, _latest_reading_for_pack(db, p)) for p in group.packs]

        # The whole group is a demo only if NONE of its members have real data.
        # A mix of real + placeholder members is still treated as live (the
        # aggregate is dominated by real readings).
        group_demo = all(snap.demo for _, snap in readings)

        voltages = [r.v_real for _, r in readings]
        currents = [r.current for _, r in readings]
        socs = [r.soc for _, r in readings]
        sohs = [r.soh for _, r in readings]
        temps = [r.temperature for _, r in readings]

        if group.connection_type == "series":
            voltage = sum(voltages)
            current_val = sum(currents) / len(currents)
            soc = min(socs)
        else:  # parallel
            voltage = sum(voltages) / len(voltages)
            current_val = sum(currents)
            soc = sum(socs) / len(socs)

        soh = sum(sohs) / len(sohs)
        temp = max(temps)

        # Concatenate all cells from member packs
        cells: list[dict] = []
        total_series = 0
        total_parallel = 0
        for p, _ in readings:
            cells.extend(_latest_cells_for_pack(db, p))
            if group.connection_type == "series":
                total_series += p.series_count
                total_parallel = max(total_parallel, p.parallel_count)
            else:
                total_parallel += p.parallel_count
                total_series = max(total_series, p.series_count)

        # Status
        soc_i = int(soc)
        soh_i = int(soh)
        if soc_i < 20 or soh_i < 80:
            pack_status = "alert"
        elif soc_i < 40 or soh_i < 90:
            pack_status = "caution"
        else:
            pack_status = "safe"

        out.append({
            "id": group.id,
            "name": group.name,
            "connection_type": group.connection_type,
            "pack_ids": [p.id for p in group.packs],
            "members": [{"id": p.id, "name": p.name, "identifier": p.pack_identifier} for p in group.packs],
            "status": pack_status,
            "soc": soc_i,
            "soh": soh_i,
            "voltage": f"{voltage:.2f}",
            "current": f"{current_val:.2f}",
            "temp": f"{temp:.2f}",
            "config": f"{total_series}S{total_parallel}P ({len(cells)} cells)",
            "series": str(total_series),
            "parallel": str(total_parallel),
            "cells": cells,
            "demo": group_demo,
        })

    return {
        "status": "success",
        "timestamp": datetime.now().replace(microsecond=0).isoformat(),
        "groups": out,
    }
