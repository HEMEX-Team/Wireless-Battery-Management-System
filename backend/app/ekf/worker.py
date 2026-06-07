"""VPS digital-twin EKF worker.

Polls the readings table for rows newer than each pack's watermark, conditions the
current (calibrate + deadband), reconstructs per-cell inputs, runs the robust
BatteryEKF with real Δt, and writes the twin estimate into
readings.vps_ekf_soc / vps_ekf_soc_uncertainty. Resumable via the ekf_state table.

Entrypoint:  python -m app.ekf.worker
"""
from __future__ import annotations

import json
import logging
import os
import time
from datetime import datetime

from sqlalchemy import func

from app.models.database import SessionLocal, engine, Base
from app.models.init_db import apply_lightweight_migrations
from app.models.models import Reading, Pack, BatteryReading, EkfState
from app.ekf.battery_ekf import BatteryEKF

log = logging.getLogger("wbms.ekf")

POLL_INTERVAL_S = float(os.getenv("EKF_POLL_INTERVAL_S", "5"))
BATCH_SIZE = int(os.getenv("EKF_BATCH_SIZE", "500"))
DEFAULT_DT_S = float(os.getenv("EKF_SAMPLE_TIME_DEFAULT", "2.0"))
MAX_DT_S = float(os.getenv("EKF_MAX_DT_S", "300"))         # gap beyond this -> re-bootstrap
NN_ENABLED = os.getenv("EKF_NN_ENABLED", "false").lower() == "true"
ADAPTIVE = os.getenv("EKF_ADAPTIVE", "true").lower() != "false"
# Input conditioning. DEVICE-SPECIFIC — defaults from the firmware guide; set
# offset=0/gain=1 to disable if they don't match your pack's BQ76952 calibration.
CAL_GAIN = float(os.getenv("EKF_CURRENT_GAIN", "1.030"))
CAL_OFFSET_A = float(os.getenv("EKF_CURRENT_OFFSET_A", "0.048"))
DEADBAND_A = float(os.getenv("EKF_CURRENT_DEADBAND_A", "0.010"))
R0_AVG = float(os.getenv("EKF_R0_AVG", "0.018"))          # bootstrap IR (no 2.43Ω parasitic)


def _condition_current(i_pack_a: float) -> float:
    """Calibrate then deadband the raw pack current (the cloud receives un-conditioned
    cc1). Order matches the firmware guide §3C."""
    i = (i_pack_a - CAL_OFFSET_A) / CAL_GAIN
    return 0.0 if abs(i) < DEADBAND_A else i


def _cell_voltage(db, r: Reading, pack: Pack) -> float:
    """Per-cell representative voltage: mean of this row's per-cell BatteryReadings
    (exact); fall back to v_real / series_count."""
    mean_v = (
        db.query(func.avg(BatteryReading.voltage))
        .filter(BatteryReading.pack_id == pack.id, BatteryReading.timestamp == r.timestamp)
        .scalar()
    )
    if mean_v:
        return float(mean_v)
    return r.v_real / (pack.series_count or 1)


def _process_pack(db, pack: Pack) -> int:
    st = db.query(EkfState).filter(EkfState.pack_id == pack.id).first()
    if st is None:
        st = EkfState(pack_id=pack.id, last_reading_id=0, initialized=False)
        db.add(st)

    rows = (
        db.query(Reading)
        .filter(Reading.pack_id == pack.id, Reading.id > st.last_reading_id)
        .order_by(Reading.id.asc())
        .limit(BATCH_SIZE)
        .all()
    )
    if not rows:
        return 0

    ekf = BatteryEKF(sample_time_s=DEFAULT_DT_S, adaptive=ADAPTIVE, nn=NN_ENABLED)
    if st.initialized and st.state_json:
        ekf.load_state(json.loads(st.state_json))

    parallel = pack.parallel_count or 1
    last_ts = st.last_timestamp

    for r in rows:
        v_cell = _cell_voltage(db, r, pack)
        i_cell = _condition_current(r.current) / parallel
        temp = r.temperature if r.temperature is not None else 25.0
        dt = (r.timestamp - last_ts).total_seconds() if last_ts else DEFAULT_DT_S

        if not st.initialized or dt <= 0 or dt > MAX_DT_S:
            # Cold start / long gap -> re-bootstrap from OCV (no 2.43Ω parasitic).
            seed = ekf.invert_ocv_average(v_cell - R0_AVG * i_cell)
            ekf.begin(seed)
            st.initialized = True
            ekf.Ts = DEFAULT_DT_S
        else:
            ekf.Ts = dt

        ekf.update(i_cell, v_cell, temp)
        r.vps_ekf_soc = ekf.soc
        r.vps_ekf_soc_uncertainty = ekf.soc_uncertainty
        last_ts = r.timestamp

    st.last_reading_id = rows[-1].id
    st.last_timestamp = last_ts
    st.state_json = json.dumps(ekf.dump_state())
    st.updated_at = datetime.utcnow()
    db.commit()
    return len(rows)


def run_once() -> int:
    db = SessionLocal()
    total = 0
    try:
        for pack in db.query(Pack).all():
            try:
                total += _process_pack(db, pack)
            except Exception:
                db.rollback()
                log.exception("EKF processing failed for pack %s", pack.id)
        return total
    finally:
        db.close()


def main() -> None:
    logging.basicConfig(
        level=os.getenv("WBMS_LOG_LEVEL", "INFO"),
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    Base.metadata.create_all(bind=engine)
    apply_lightweight_migrations(engine)
    log.info("EKF twin worker started (poll=%.1fs batch=%d nn=%s adaptive=%s)",
             POLL_INTERVAL_S, BATCH_SIZE, NN_ENABLED, ADAPTIVE)
    while True:
        try:
            n = run_once()
            if n:
                log.info("twin processed %d readings", n)
        except Exception:
            log.exception("EKF poll cycle failed")
        time.sleep(POLL_INTERVAL_S)


if __name__ == "__main__":
    main()
