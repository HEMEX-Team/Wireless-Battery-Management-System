# models.py
from sqlalchemy import Boolean, Column, Integer, String, Float, DateTime, ForeignKey, Index, Table
from sqlalchemy.orm import relationship
from app.models.database import Base
from datetime import datetime, timezone


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
    temperature = Column(Float, nullable=False)
    cycles = Column(Integer, nullable=False)
    v_estimated = Column(Float, nullable=False)
    soc = Column(Float, nullable=False)
    soh = Column(Float, nullable=False)
    ekf_soc = Column(Float, nullable=False)
    power = Column(Float, nullable=True)
    charging_discharging = Column(Boolean, nullable=True)
    
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