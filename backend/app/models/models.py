# models.py
from sqlalchemy import Boolean, Column, Integer, String, Float, DateTime, ForeignKey, Index, Table, Text
from sqlalchemy.orm import relationship
from app.models.database import Base
from datetime import datetime


# Association table for many-to-many between Pack and PackGroup
pack_group_members = Table(
    "pack_group_members",
    Base.metadata,
    Column("pack_id", Integer, ForeignKey("packs.id", ondelete="CASCADE"), primary_key=True),
    Column("group_id", Integer, ForeignKey("pack_groups.id", ondelete="CASCADE"), primary_key=True),
)


class User(Base):
    __tablename__ = "users"

    id = Column(Integer, primary_key=True, autoincrement=True)
    first_name = Column(String(100), nullable=False)
    last_name = Column(String(100), nullable=False)
    email = Column(String(255), unique=True, nullable=False, index=True)
    password = Column(String(255), nullable=True)
    google_sub = Column(String(255), unique=True, nullable=True, index=True)
    # Access level: "user" (default) | "developer" | "admin". Gates dev-only
    # controls in the web UI. (The offline AP portal's dev-vs-user variant is a
    # firmware-side flag — the ESP32 can't read this column when cloud is down.)
    role = Column(String(20), nullable=False, default="user")

    packs = relationship("Pack", back_populates="owner")
    groups = relationship("PackGroup", back_populates="owner", cascade="all, delete-orphan")


class Pack(Base):
    __tablename__ = "packs"

    id = Column(Integer, primary_key=True, autoincrement=True)
    name = Column(String(255), nullable=False, index=True)
    pack_identifier = Column(String(50), unique=True, nullable=False, index=True)
    pairing_code = Column(String(50), unique=True, nullable=False, index=True)
    # Master ESP32's own pairing code (last 3 bytes of its MAC). Distinct
    # from `pairing_code`, which is the slave's. Populated when the master
    # first sends telemetry containing `masterPairingCode`. Used as the MQTT
    # command-topic suffix for OTA dispatch.
    master_pairing_code = Column(String(10), nullable=True, index=True)
    # Firmware version reported by the master in the most recent telemetry
    # message. Frontend uses this to detect when an OTA has landed.
    master_firmware_version = Column(String(32), nullable=True)
    series_count = Column(Integer, nullable=False, default=3)
    parallel_count = Column(Integer, nullable=False, default=1)
    # Manually-entered cell specs used to compute the dashboard gauge redlines.
    # All nullable: packs created before this feature fall back to a constant.
    #   rated current (A) = max_discharge_c × cell_capacity_ah × parallel_count
    #   full voltage  (V) = series_count × 4.2 (cell full-charge)
    cell_nominal_voltage = Column(Float, nullable=True)  # per-cell nominal V, e.g. 3.6
    cell_capacity_ah = Column(Float, nullable=True)      # per-cell capacity Ah, e.g. 3.5
    max_discharge_c = Column(Float, nullable=True)       # max continuous discharge C-rate
    user_id = Column(
        Integer,
        ForeignKey("users.id", ondelete="SET NULL"),
        nullable=True,
        index=True,
    )
    auto_created = Column(Boolean, default=False, nullable=False)

    owner = relationship("User", back_populates="packs")
    groups = relationship("PackGroup", secondary=pack_group_members, back_populates="packs")

    # One Pack has many Readings and BatteryReadings
    readings = relationship(
        "Reading",
        back_populates="pack",
        cascade="all, delete-orphan",
        lazy="dynamic",
    )
    battery_readings = relationship(
        "BatteryReading",
        back_populates="pack",
        cascade="all, delete-orphan",
        lazy="dynamic",
    )


class PackGroup(Base):
    __tablename__ = "pack_groups"

    id = Column(Integer, primary_key=True, autoincrement=True)
    name = Column(String(255), nullable=False)
    connection_type = Column(String(20), nullable=False, default="parallel")  # "parallel" | "series"
    user_id = Column(
        Integer,
        ForeignKey("users.id", ondelete="CASCADE"),
        nullable=False,
        index=True,
    )
    created_at = Column(DateTime, default=datetime.utcnow, nullable=False)

    owner = relationship("User", back_populates="groups")
    packs = relationship("Pack", secondary=pack_group_members, back_populates="groups")

class Reading(Base):
    __tablename__ = "readings"

    id = Column(Integer, primary_key=True, autoincrement=True)
    timestamp = Column(DateTime, default=datetime.utcnow, nullable=False, index=True)
    
    # Foreign Key to Pack
    pack_id = Column(
        Integer, 
        ForeignKey("packs.id", ondelete="CASCADE", onupdate="CASCADE"), 
        nullable=False,
        index=True
    )
    
    v_real = Column(Float, nullable=False)
    current = Column(Float, nullable=False)
    temperature = Column(Float, nullable=False)  # mean of the thermistors below
    # Individual thermistor temps (left/middle/right). Nullable: older rows and
    # firmware that only reports chipTemp won't have them. The thermal heatmap
    # uses these for a real spatial view instead of synthesizing one.
    temp1 = Column(Float, nullable=True)
    temp2 = Column(Float, nullable=True)
    temp3 = Column(Float, nullable=True)
    cycles = Column(Integer, nullable=False)
    v_estimated = Column(Float, nullable=False)
    soc = Column(Float, nullable=False)
    soh = Column(Float, nullable=False)
    ekf_soc = Column(Float, nullable=False)
    power = Column(Float, nullable=True)
    charging_discharging = Column(Boolean, nullable=True)
    # Coulomb-counter accumulators reported by firmware (charge in Ah, time in s).
    charge = Column(Float, nullable=True)
    charge_time = Column(Integer, nullable=True)
    # BQ76952 latched Safety Status bytes (0x03/0x05/0x07); 0/None = no fault.
    # Decoded into named protection alerts on the serve side.
    ss_a = Column(Integer, nullable=True)
    ss_b = Column(Integer, nullable=True)
    ss_c = Column(Integer, nullable=True)

    # Relationship - Many Readings belong to one Pack
    pack = relationship("Pack", back_populates="readings")
    
    # Composite index for efficient queries
    __table_args__ = (
        Index('idx_pack_timestamp', 'pack_id', 'timestamp'),
    )

class FirmwareImage(Base):
    __tablename__ = "firmware_images"

    id = Column(Integer, primary_key=True, autoincrement=True)
    version = Column(String(32), unique=True, nullable=False, index=True)
    sha256 = Column(String(64), nullable=False)
    size = Column(Integer, nullable=False)
    artifact_path = Column(String(512), nullable=False)
    created_at = Column(DateTime, default=datetime.utcnow, nullable=False)


class BatteryReading(Base):
    __tablename__ = "battery_readings"

    id = Column(Integer, primary_key=True, autoincrement=True)
    battery_position = Column(Integer, nullable=False)
    timestamp = Column(DateTime, default=datetime.utcnow, nullable=False, index=True)
    
    # Foreign Key to Pack
    pack_id = Column(
        Integer, 
        ForeignKey("packs.id", ondelete="CASCADE", onupdate="CASCADE"), 
        nullable=False,
        index=True
    )
    
    voltage = Column(Float, nullable=False)
    
    # Relationship - Many BatteryReadings belong to one Pack
    pack = relationship("Pack", back_populates="battery_readings")
    
    # Composite index for efficient queries
    __table_args__ = (
        Index('idx_pack_battery_timestamp', 'pack_id', 'battery_position', 'timestamp'),
    )


class BmsSnapshot(Base):
    """Latest on-demand full read-only snapshot of a pack's BMS (the ~60-field
    surface from the slave AP), one row per pack (upserted by the subscriber)."""
    __tablename__ = "bms_snapshots"

    id = Column(Integer, primary_key=True, autoincrement=True)
    pack_id = Column(
        Integer, ForeignKey("packs.id", ondelete="CASCADE"),
        nullable=False, unique=True, index=True,
    )
    payload = Column(Text, nullable=False)  # raw JSON snapshot from firmware
    received_at = Column(DateTime, default=datetime.utcnow, nullable=False)


class BmsCommand(Base):
    """Audit + status of an admin command dispatched to a pack's BMS. status
    flips pending->applied/failed when the slave echoes lastCmdSeq in telemetry."""
    __tablename__ = "bms_commands"

    id = Column(Integer, primary_key=True, autoincrement=True)
    pack_id = Column(
        Integer, ForeignKey("packs.id", ondelete="CASCADE"),
        nullable=False, index=True,
    )
    seq = Column(Integer, nullable=False)
    action = Column(String(40), nullable=False)
    args = Column(Text, nullable=True)          # JSON of clamped args
    status = Column(String(16), nullable=False, default="pending")  # pending|applied|failed|expired
    issued_by = Column(String(255), nullable=True)  # admin email (audit trail)
    created_at = Column(DateTime, default=datetime.utcnow, nullable=False, index=True)
    acked_at = Column(DateTime, nullable=True)

    __table_args__ = (
        Index('idx_bms_cmd_pack_seq', 'pack_id', 'seq'),
    )