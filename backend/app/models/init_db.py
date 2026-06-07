# init_db.py
import logging

from sqlalchemy import inspect, text
from sqlalchemy.engine import Engine

from app.models.database import engine, Base
from app.models import models  # noqa: F401  (registers tables on Base)

log = logging.getLogger("wbms.initdb")


# Schema additions that aren't covered by Base.metadata.create_all() because
# they extend an existing table. Listed as (table, column, ddl-fragment).
# Idempotent: skipped if the column already exists.
_LIGHTWEIGHT_MIGRATIONS = [
    ("packs", "master_pairing_code", "VARCHAR(10)"),
    ("packs", "master_firmware_version", "VARCHAR(32)"),
    # Per-cell specs for gauge redlines (NULL on existing rows → constant fallback).
    ("packs", "cell_nominal_voltage", "FLOAT"),
    ("packs", "cell_capacity_ah", "FLOAT"),
    ("packs", "max_discharge_c", "FLOAT"),
    ("readings", "temp1", "FLOAT"),
    ("readings", "temp2", "FLOAT"),
    ("readings", "temp3", "FLOAT"),
    ("readings", "chip_temp", "FLOAT"),
    ("readings", "charge", "FLOAT"),
    ("readings", "charge_time", "INTEGER"),
    ("readings", "ss_a", "INTEGER"),
    ("readings", "ss_b", "INTEGER"),
    ("readings", "ss_c", "INTEGER"),
    # Existing rows default to the regular 'user' role.
    ("users", "role", "VARCHAR(20) DEFAULT 'user' NOT NULL"),
    # VPS digital-twin EKF: per-row SoC uncertainty (the SoC itself reuses the
    # renamed vps_ekf_soc column — see _COLUMN_RENAMES below).
    ("readings", "vps_ekf_soc_uncertainty", "FLOAT"),
]

# Column renames applied before the additive pass (and before any insert that uses the
# new name). Idempotent: only fires when the old name still exists and the new doesn't.
# (table, old_name, new_name)
_COLUMN_RENAMES = [
    # The old `ekf_soc` was a redundant copy of `soc`; it now holds the independent
    # VPS digital-twin estimate, renamed to disambiguate from the device `soc`.
    ("readings", "ekf_soc", "vps_ekf_soc"),
]


def apply_lightweight_migrations(bind: Engine = engine) -> None:
    """Apply column renames + additions introduced after the initial schema. Uses raw
    ALTER TABLE because we don't run Alembic — kept tiny so we can drop it once we do.

    Each statement runs in its own transaction so a failure (or a no-op skip) can't
    abort the rest, and renames run first so inserts using the new column name work.
    """
    inspector = inspect(bind)
    existing_tables = set(inspector.get_table_names())

    # 1) Renames first — a fresh DB (create_all already made the new column) skips these.
    for table, old, new in _COLUMN_RENAMES:
        if table not in existing_tables:
            continue
        columns = {c["name"] for c in inspector.get_columns(table)}
        if old in columns and new not in columns:
            try:
                with bind.begin() as conn:
                    conn.execute(text(f"ALTER TABLE {table} RENAME COLUMN {old} TO {new}"))
                log.info("Renamed column %s.%s -> %s", table, old, new)
            except Exception:
                log.exception("Failed to rename column %s.%s -> %s", table, old, new)

    # Re-inspect so the additive pass sees any just-renamed columns.
    inspector = inspect(bind)

    # 2) Additive columns.
    for table, column, ddl in _LIGHTWEIGHT_MIGRATIONS:
        if table not in existing_tables:
            continue  # create_all will handle a fresh table
        columns = {c["name"] for c in inspector.get_columns(table)}
        if column in columns:
            continue
        try:
            with bind.begin() as conn:
                conn.execute(text(f"ALTER TABLE {table} ADD COLUMN {column} {ddl}"))
            log.info("Added column %s.%s", table, column)
        except Exception:
            log.exception("Failed to add column %s.%s", table, column)


def init_database():
    """Initialize database tables - run this once"""
    Base.metadata.create_all(bind=engine)
    apply_lightweight_migrations(engine)
    print("✅ Database tables created successfully!")


if __name__ == "__main__":
    init_database()
