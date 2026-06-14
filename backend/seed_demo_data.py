#!/usr/bin/env python3
"""Populate the database with demo data for website showcase (no hardware required).

Run inside the backend container:

    docker compose exec backend python seed_demo_data.py --fresh

Demo accounts (password for both: Demo123!):
    demo@wbms.local   — regular user (owns demo packs)
    admin@wbms.local  — admin role (BMS Admin tab)

Options:
    --fresh   Remove prior demo-tagged rows, then re-seed (preserves other accounts)
    --dry-run Print planned actions without writing

Note: Admin command *dispatch* requires live MQTT + hardware. Seeded bms_commands
and bms_snapshots are for UI showcase only.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from pathlib import Path

from sqlalchemy import delete, func, select, text
from sqlalchemy.orm import Session

from app.auth import hash_password
from app.models import models
from app.models.database import Base, SessionLocal, engine
from app.models.init_db import apply_lightweight_migrations
from app.models.models import pack_group_members

# --- Demo identity tags -------------------------------------------------------
DEMO_USER_EMAIL = "demo@wbms.local"
DEMO_ADMIN_EMAIL = "admin@wbms.local"
DEMO_PASSWORD = "Demo123!"
DEMO_PACK_PREFIX = "demo-"
DEMO_GROUP_PREFIX = "[Demo]"

READING_COUNT = 400
HOURS_SPAN = 24

def _utcnow() -> datetime:
    return datetime.now(timezone.utc).replace(tzinfo=None)


@dataclass(frozen=True)
class PackScenario:
    pack_identifier: str
    name: str
    pairing_code: str
    master_pairing_code: str
    end_soc: float
    end_soh: float
    vps_offset: float
    cell_voltages: tuple[float, float, float]
    temps: tuple[float, float, float]
    ss_a: int
    ss_b: int
    ss_c: int
    snapshot_key: str


PACK_SCENARIOS: list[PackScenario] = [
    PackScenario(
        pack_identifier="demo-home-1",
        name="Home Battery",
        pairing_code="DEMO01",
        master_pairing_code="DEMO01",
        end_soc=72.0,
        end_soh=96.0,
        vps_offset=0.5,
        cell_voltages=(3.85, 3.86, 3.87),
        temps=(28.0, 29.5, 30.0),
        ss_a=0,
        ss_b=0,
        ss_c=0,
        snapshot_key="home",
    ),
    PackScenario(
        pack_identifier="demo-workshop-2",
        name="Workshop Pack",
        pairing_code="DEMO02",
        master_pairing_code="DEMO02",
        end_soc=58.0,
        end_soh=92.0,
        vps_offset=12.0,
        cell_voltages=(3.25, 3.86, 3.87),
        temps=(29.0, 31.0, 30.5),
        ss_a=0,
        ss_b=0,
        ss_c=0,
        snapshot_key="workshop",
    ),
    PackScenario(
        pack_identifier="demo-spare-3",
        name="Spare Module",
        pairing_code="DEMO03",
        master_pairing_code="DEMO03",
        end_soc=18.0,
        end_soh=78.0,
        vps_offset=0.0,
        cell_voltages=(2.95, 3.80, 3.81),
        temps=(38.5, 41.5, 39.0),
        ss_a=0x20,
        ss_b=0,
        ss_c=0,
        snapshot_key="spare",
    ),
]

# SOC curve segments: (start_frac, end_frac, soc_start, soc_end, charging)
_SOC_SEGMENTS = [
    (0.00, 0.22, 90.0, 62.0, False),
    (0.22, 0.28, 62.0, 84.0, True),
    (0.28, 0.50, 84.0, 48.0, False),
    (0.50, 0.58, 48.0, 71.0, True),
    (0.58, 1.00, 71.0, None, False),
]


def _interp_soc(t: float, end_soc: float) -> tuple[float, bool]:
    """Return (soc, charging) for normalized time t in [0, 1]."""
    for start_f, end_f, soc_start, soc_end, charging in _SOC_SEGMENTS:
        if t < start_f or t > end_f:
            continue
        if t == end_f and end_f < 1.0:
            break
        span = end_f - start_f
        if span <= 0:
            return soc_start, charging
        local = (t - start_f) / span
        target_end = end_soc if soc_end is None else soc_end
        soc = soc_start + (target_end - soc_start) * local
        return max(0.0, min(100.0, soc)), charging
    return end_soc, False


def _load_snapshot_templates() -> dict:
    with SNAPSHOT_TEMPLATES_PATH.open(encoding="utf-8") as f:
        return json.load(f)


def _demo_user_emails() -> list[str]:
    return [DEMO_USER_EMAIL, DEMO_ADMIN_EMAIL]


def _demo_pack_ids(db: Session) -> list[int]:
    rows = db.scalars(
        select(models.Pack.id).where(models.Pack.pack_identifier.like(f"{DEMO_PACK_PREFIX}%"))
    ).all()
    return list(rows)


def _demo_group_ids(db: Session) -> list[int]:
    rows = db.scalars(
        select(models.PackGroup.id).where(models.PackGroup.name.like(f"{DEMO_GROUP_PREFIX}%"))
    ).all()
    return list(rows)


def _demo_user_ids(db: Session) -> list[int]:
    rows = db.scalars(
        select(models.User.id).where(models.User.email.in_(_demo_user_emails()))
    ).all()
    return list(rows)


def delete_demo_data(db: Session, dry_run: bool) -> None:
    pack_ids = _demo_pack_ids(db)
    group_ids = _demo_group_ids(db)
    user_ids = _demo_user_ids(db)

    if not pack_ids and not group_ids and not user_ids:
        print("  No existing demo data to remove.")
        return

    print(f"  Removing demo data: {len(pack_ids)} packs, {len(group_ids)} groups, {len(user_ids)} users")

    if dry_run:
        return

    if pack_ids:
        db.execute(delete(models.BmsCommand).where(models.BmsCommand.pack_id.in_(pack_ids)))
        db.execute(delete(models.BmsSnapshot).where(models.BmsSnapshot.pack_id.in_(pack_ids)))
        db.execute(delete(models.BatteryReading).where(models.BatteryReading.pack_id.in_(pack_ids)))
        db.execute(delete(models.Reading).where(models.Reading.pack_id.in_(pack_ids)))
        db.execute(delete(models.EkfState).where(models.EkfState.pack_id.in_(pack_ids)))

    if group_ids:
        db.execute(delete(pack_group_members).where(pack_group_members.c.group_id.in_(group_ids)))
    if pack_ids:
        db.execute(delete(pack_group_members).where(pack_group_members.c.pack_id.in_(pack_ids)))

    if group_ids:
        db.execute(delete(models.PackGroup).where(models.PackGroup.id.in_(group_ids)))

    if pack_ids:
        db.execute(delete(models.Pack).where(models.Pack.id.in_(pack_ids)))

    if user_ids:
        db.execute(delete(models.User).where(models.User.id.in_(user_ids)))

    db.commit()


def _ensure_user(db: Session, email: str, role: str, dry_run: bool) -> models.User | None:
    existing = db.query(models.User).filter(models.User.email == email).first()
    if existing:
        if not dry_run:
            existing.role = role
            db.flush()
        return existing

    user = models.User(
        first_name="Demo" if role == "user" else "Admin",
        last_name="User" if role == "user" else "Operator",
        email=email,
        password=hash_password(DEMO_PASSWORD),
        role=role,
    )
    if dry_run:
        print(f"  Would create user {email} (role={role})")
        return None
    db.add(user)
    db.flush()
    return user


def _create_packs(
    db: Session,
    owner: models.User | None,
    dry_run: bool,
) -> list[tuple[PackScenario, models.Pack | None]]:
    result: list[tuple[PackScenario, models.Pack | None]] = []
    for scenario in PACK_SCENARIOS:
        if dry_run:
            print(f"  Would create pack {scenario.pack_identifier!r}")
            result.append((scenario, None))
            continue
        if owner is None:
            raise RuntimeError("owner required when not dry-run")
        pack = models.Pack(
            name=scenario.name,
            pack_identifier=scenario.pack_identifier,
            pairing_code=scenario.pairing_code,
            master_pairing_code=scenario.master_pairing_code,
            master_firmware_version="0.1.0",
            series_count=3,
            parallel_count=1,
            cell_nominal_voltage=3.6,
            cell_capacity_ah=3.5,
            max_discharge_c=1.0,
            user_id=owner.id,
            auto_created=False,
        )
        db.add(pack)
        db.flush()
        result.append((scenario, pack))
    return result


def _generate_readings(
    pack: models.Pack,
    scenario: PackScenario,
    now: datetime,
) -> tuple[list[models.Reading], list[models.BatteryReading]]:
    readings: list[models.Reading] = []
    cell_rows: list[models.BatteryReading] = []
    start = now - timedelta(hours=HOURS_SPAN)
    step = timedelta(hours=HOURS_SPAN) / (READING_COUNT - 1)

    charge_time = 0
    accumulated_charge = 1200.0

    for i in range(READING_COUNT):
        ts = start + step * i
        t_norm = i / (READING_COUNT - 1)
        soc, charging = _interp_soc(t_norm, scenario.end_soc)

        if charging:
            current_a = round(1.2 + 0.3 * math.sin(i * 0.1), 3)
        else:
            current_a = round(-(0.6 + 0.4 * math.sin(i * 0.07)), 3)

        v_real = sum(scenario.cell_voltages)
        temp1, temp2, temp3 = scenario.temps
        temperature = (temp1 + temp2 + temp3) / 3.0
        chip_temp = temperature + 1.5

        soh = scenario.end_soh + (2.0 * math.sin(t_norm * math.pi) if scenario.end_soh >= 90 else 0)
        soh = max(70.0, min(100.0, soh))

        vps = max(0.0, min(100.0, soc + scenario.vps_offset))
        power = round(v_real * current_a, 3)

        charge_time += int(step.total_seconds())
        if charging:
            accumulated_charge += abs(current_a) * step.total_seconds() / 3600.0

        ss_a = scenario.ss_a if t_norm > 0.85 else 0
        ss_b = scenario.ss_b if t_norm > 0.85 else 0
        ss_c = scenario.ss_c if t_norm > 0.85 else 0

        reading = models.Reading(
            timestamp=ts,
            pack_id=pack.id,
            v_real=v_real,
            current=current_a,
            temperature=temperature,
            temp1=temp1,
            temp2=temp2,
            temp3=temp3,
            chip_temp=chip_temp,
            cycles=0,
            v_estimated=v_real,
            soc=soc,
            soh=soh,
            vps_ekf_soc=vps,
            vps_ekf_soc_uncertainty=round(0.5 + abs(scenario.vps_offset) * 0.1, 2),
            power=power,
            charging_discharging=charging,
            charge=round(accumulated_charge, 2),
            charge_time=charge_time,
            ss_a=ss_a,
            ss_b=ss_b,
            ss_c=ss_c,
        )
        readings.append(reading)

        for pos, voltage in enumerate(scenario.cell_voltages, start=1):
            cell_rows.append(
                models.BatteryReading(
                    battery_position=pos,
                    timestamp=ts,
                    pack_id=pack.id,
                    voltage=voltage,
                )
            )

    return readings, cell_rows


def _seed_telemetry(
    db: Session,
    pack_pairs: list[tuple[PackScenario, models.Pack | None]],
    dry_run: bool,
) -> int:
    if dry_run:
        print(f"  Would insert ~{READING_COUNT} readings × {len(PACK_SCENARIOS)} packs")
        return 0

    now = _utcnow()
    total = 0
    for scenario, pack in pack_pairs:
        if pack is None:
            continue
        readings, cells = _generate_readings(pack, scenario, now)
        db.bulk_save_objects(readings)
        db.bulk_save_objects(cells)
        total += len(readings)
    db.flush()
    return total


def _seed_group(
    db: Session,
    owner: models.User | None,
    pack_pairs: list[tuple[PackScenario, models.Pack | None]],
    dry_run: bool,
) -> None:
    home = next((p for s, p in pack_pairs if s.pack_identifier == "demo-home-1"), None)
    workshop = next((p for s, p in pack_pairs if s.pack_identifier == "demo-workshop-2"), None)
    if dry_run:
        print(f"  Would create group {DEMO_GROUP_PREFIX} Main Array")
        return
    if home is None or workshop is None:
        return
    if owner is None:
        return
    group = models.PackGroup(
        name=f"{DEMO_GROUP_PREFIX} Main Array",
        connection_type="parallel",
        user_id=owner.id,
    )
    group.packs = [home, workshop]
    db.add(group)
    db.flush()


def _seed_snapshots(
    db: Session,
    pack_pairs: list[tuple[PackScenario, models.Pack | None]],
    templates: dict,
    dry_run: bool,
) -> None:
    if dry_run:
        print(f"  Would insert {len(PACK_SCENARIOS)} bms_snapshots")
        return
    now = _utcnow()
    for scenario, pack in pack_pairs:
        if pack is None:
            continue
        payload = dict(templates[scenario.snapshot_key])
        db.add(
            models.BmsSnapshot(
                pack_id=pack.id,
                payload=json.dumps(payload),
                received_at=now,
            )
        )
    db.flush()


def _seed_commands(
    db: Session,
    pack: models.Pack | None,
    admin_email: str,
    dry_run: bool,
) -> None:
    if dry_run:
        print("  Would insert 3 bms_commands on demo-home-1")
        return
    if pack is None:
        return
    now = _utcnow()
    cmds = [
        ("clearFaults", 1, "applied", now - timedelta(minutes=30)),
        ("toggleBalMaster", 2, "applied", now - timedelta(minutes=15)),
        ("snapshot", 3, "applied", now - timedelta(minutes=5)),
    ]
    for action, seq, status, acked in cmds:
        db.add(
            models.BmsCommand(
                pack_id=pack.id,
                seq=seq,
                action=action,
                status=status,
                issued_by=admin_email,
                created_at=acked - timedelta(seconds=10),
                acked_at=acked,
            )
        )
    db.flush()


def _reset_sequences(db: Session) -> None:
    """Reset Postgres serial sequences after bulk inserts with explicit ids."""
    tables = [
        "users",
        "packs",
        "pack_groups",
        "readings",
        "battery_readings",
        "bms_snapshots",
        "bms_commands",
    ]
    for table in tables:
        db.execute(
            text(
                f"SELECT setval("
                f"  pg_get_serial_sequence('{table}', 'id'),"
                f"  COALESCE((SELECT MAX(id) FROM {table}), 1),"
                f"  (SELECT MAX(id) FROM {table}) IS NOT NULL"
                f")"
            )
        )


def seed(fresh: bool, dry_run: bool) -> None:
    Base.metadata.create_all(bind=engine)
    apply_lightweight_migrations(engine)

    templates = _load_snapshot_templates()
    db = SessionLocal()
    try:
        if fresh:
            print("Clearing prior demo data…")
            delete_demo_data(db, dry_run)
        elif _demo_pack_ids(db) or _demo_user_ids(db):
            sys.exit(
                "Demo data already exists. Re-run with --fresh to replace it, "
                "or delete demo rows manually."
            )

        print("Creating demo users…")
        demo_user = _ensure_user(db, DEMO_USER_EMAIL, "user", dry_run)
        _ensure_user(db, DEMO_ADMIN_EMAIL, "admin", dry_run)

        if dry_run:
            owner = None
        else:
            owner = demo_user or db.query(models.User).filter(
                models.User.email == DEMO_USER_EMAIL
            ).first()
            if owner is None:
                sys.exit("Failed to create demo user")

        print("Creating demo packs…")
        pack_pairs = _create_packs(db, owner, dry_run)

        print("Generating telemetry (24h history)…")
        reading_count = _seed_telemetry(db, pack_pairs, dry_run)

        print("Creating pack group…")
        if owner or dry_run:
            _seed_group(db, owner, pack_pairs, dry_run)

        print("Seeding admin snapshots…")
        _seed_snapshots(db, pack_pairs, templates, dry_run)

        home_pack = next((p for s, p in pack_pairs if s.pack_identifier == "demo-home-1"), None)
        print("Seeding admin command audit log…")
        _seed_commands(db, home_pack, DEMO_ADMIN_EMAIL, dry_run)

        if dry_run:
            print("\n[dry-run] No changes written.")
            return

        if engine.dialect.name == "postgresql":
            _reset_sequences(db)

        db.commit()

        print("\nDemo seed complete.")
        print("\nLogin credentials (password for both: Demo123!):")
        print(f"  User:  {DEMO_USER_EMAIL}")
        print(f"  Admin: {DEMO_ADMIN_EMAIL}")
        print(f"\nInserted ~{reading_count} readings across {len(PACK_SCENARIOS)} packs.")
        print("\nVerify:")
        print("  curl -s http://127.0.0.1:7000/health")
        print(
            "  docker compose exec postgres psql -U wbms -d wbms -c "
            "\"SELECT email, role FROM users WHERE email LIKE '%@wbms.local';\""
        )
        print(
            "  docker compose exec postgres psql -U wbms -d wbms -c "
            "\"SELECT pack_identifier, COUNT(*) FROM readings r "
            "JOIN packs p ON p.id=r.pack_id WHERE p.pack_identifier LIKE 'demo-%' GROUP BY 1;\""
        )
        print("\nNote: BMS Admin command dispatch requires live MQTT + hardware.")
    except Exception:
        db.rollback()
        raise
    finally:
        db.close()


def main() -> None:
    parser = argparse.ArgumentParser(description="Seed demo data for WBMS showcase")
    parser.add_argument(
        "--fresh",
        action="store_true",
        help="Remove existing demo-tagged rows before seeding",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print planned actions without writing",
    )
    args = parser.parse_args()
    seed(fresh=args.fresh, dry_run=args.dry_run)


if __name__ == "__main__":
    main()
