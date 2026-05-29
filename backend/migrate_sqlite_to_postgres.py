"""One-off migration: copy all data from the legacy SQLite DB into Postgres.

Run inside the backend container/image (so `app` is importable and the
db-data volume with wbms.db is mounted):

    docker compose run --rm backend python migrate_sqlite_to_postgres.py

Source  : SOURCE_SQLITE_URL    (default sqlite:////app/data/wbms.db)
Target  : WBMS_DATABASE_URL    (must be the Postgres URL from compose)

The script is safe to reason about:
  * It reads the source through the ORM so column types (datetimes,
    booleans) are converted properly before being written to Postgres.
  * Tables are copied in foreign-key-safe order, preserving primary keys.
  * It refuses to run if any target table already has rows, so it can
    never silently duplicate data.
  * All writes happen in a single transaction — all or nothing.
  * Postgres identity sequences are reset to MAX(id) afterwards so new
    inserts don't collide with migrated rows.
"""

import os
import sys

from sqlalchemy import create_engine, func, insert, select, text
from sqlalchemy.orm import sessionmaker

from app.models import models
from app.models.database import Base
from app.models.models import pack_group_members

SQLITE_URL = os.getenv("SOURCE_SQLITE_URL", "sqlite:////app/data/wbms.db")
PG_URL = os.getenv("WBMS_DATABASE_URL", "")

if not PG_URL or PG_URL.startswith("sqlite"):
    sys.exit("WBMS_DATABASE_URL must point to Postgres, got: %r" % PG_URL)

# FK-safe order. pack_group_members (association) goes last; it references
# both packs and pack_groups. readings/battery_readings reference packs.
ORM_ORDER = [
    models.User,
    models.Pack,
    models.PackGroup,
    models.Reading,
    models.BatteryReading,
]

src_engine = create_engine(SQLITE_URL, connect_args={"check_same_thread": False})
dst_engine = create_engine(PG_URL, pool_pre_ping=True)

# Make sure the schema exists on the target.
Base.metadata.create_all(bind=dst_engine)

SrcSession = sessionmaker(bind=src_engine)


def main():
    src = SrcSession()
    try:
        with dst_engine.begin() as dst:
            # --- safety: target must be empty ---
            for model in ORM_ORDER:
                n = dst.execute(
                    select(func.count()).select_from(model.__table__)
                ).scalar()
                if n:
                    sys.exit(
                        f"ABORT: target table {model.__tablename__!r} already "
                        f"has {n} rows. Refusing to migrate into a non-empty DB."
                    )
            n = dst.execute(
                select(func.count()).select_from(pack_group_members)
            ).scalar()
            if n:
                sys.exit(
                    "ABORT: target table 'pack_group_members' already has "
                    f"{n} rows."
                )

            # --- copy ORM-mapped tables (column types converted via ORM) ---
            for model in ORM_ORDER:
                table = model.__table__
                objs = src.query(model).all()
                if objs:
                    rows = [
                        {c.name: getattr(o, c.name) for c in table.columns}
                        for o in objs
                    ]
                    dst.execute(insert(table), rows)
                print(f"  {model.__tablename__:<22} {len(objs):>6} rows")

            # --- copy the association table via Core ---
            assoc_rows = [
                dict(r._mapping) for r in src.execute(select(pack_group_members))
            ]
            if assoc_rows:
                dst.execute(insert(pack_group_members), assoc_rows)
            print(f"  {'pack_group_members':<22} {len(assoc_rows):>6} rows")

            # --- reset Postgres identity sequences to MAX(id) ---
            for model in ORM_ORDER:
                t = model.__table__.name
                dst.execute(
                    text(
                        f"SELECT setval("
                        f"  pg_get_serial_sequence('{t}', 'id'),"
                        f"  COALESCE((SELECT MAX(id) FROM {t}), 1),"
                        f"  (SELECT MAX(id) FROM {t}) IS NOT NULL"
                        f")"
                    )
                )

        print("\n✅ Migration complete.")
    finally:
        src.close()


if __name__ == "__main__":
    main()
