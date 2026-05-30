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
]


def apply_lightweight_migrations(bind: Engine = engine) -> None:
    """Add columns introduced after the initial schema. Uses raw ALTER TABLE
    because we don't run Alembic — kept tiny so we can drop it once we do.
    """
    inspector = inspect(bind)
    existing_tables = set(inspector.get_table_names())
    with bind.begin() as conn:
        for table, column, ddl in _LIGHTWEIGHT_MIGRATIONS:
            if table not in existing_tables:
                continue  # create_all will handle a fresh table
            columns = {c["name"] for c in inspector.get_columns(table)}
            if column in columns:
                continue
            try:
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
