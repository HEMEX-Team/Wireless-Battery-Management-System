"""Sanity + behavior tests for the VPS digital-twin BatteryEKF.

Runs under pytest, or standalone:  python backend/tests/test_battery_ekf.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # backend/ on path

from app.ekf.battery_ekf import BatteryEKF, Q_NOMINAL_AS, _model  # noqa: E402


def test_ecm_shapes():
    m = _model()
    assert m["R0"].shape == (101, 4)
    assert m["OCV_Charge"].shape == (101,)
    assert m["OCV_Discharge"].shape == (101,)


def test_bootstrap_monotonic():
    ekf = BatteryEKF()
    assert ekf.invert_ocv_average(3.3) < ekf.invert_ocv_average(3.9)
    assert ekf.invert_ocv_average(2.0) == 0.0
    assert ekf.invert_ocv_average(4.5) == 100.0


def test_binary_strategy_is_coulomb_under_load():
    """Under load the robust filter ≈ a pure coulomb counter: the SoC drop matches
    I·t/Q almost independently of the (deliberately off) voltage."""
    ekf = BatteryEKF(sample_time_s=2.0)
    ekf.begin(80.0)
    soc0 = ekf.soc
    steps, i_cell, dt = 100, -3.0, 2.0
    for _ in range(steps):
        ekf.update(current=i_cell, voltage=3.6, temp=25.0)
        assert 0.0 <= ekf.soc <= 100.0
    expected_drop = -i_cell * (steps * dt) / Q_NOMINAL_AS * 100.0
    actual_drop = soc0 - ekf.soc
    assert abs(actual_drop - expected_drop) < 0.6, (actual_drop, expected_drop)


def test_rest_nudges_soc_gently_toward_ocv():
    """At rest (I=0) SoC physically should NOT move much — no charge flows. The
    voltage offset is mostly absorbed by the RC states (measurement sensitivity 1 vs
    SoC's ~0.009); SoC only nudges *gently* in the OCV direction. This mirrors the
    firmware's "do not aggressively drag SOC" rest behavior."""
    ekf = BatteryEKF(sample_time_s=2.0)
    ekf.begin(50.0)
    for _ in range(300):
        ekf.update(current=0.0, voltage=3.80, temp=25.0)  # 3.80 V > OCV(50%)=3.725
    assert ekf.soc > 50.0     # nudged up toward the higher-OCV value (right direction)
    assert ekf.soc < 53.0     # but gently — rest must not yank SoC
    assert ekf.soc_uncertainty >= 0.0


def test_state_roundtrip():
    a = BatteryEKF(); a.begin(63.0)
    for _ in range(5):
        a.update(-1.5, 3.7, 25.0)
    b = BatteryEKF()
    b.load_state(a.dump_state())
    assert b.soc == a.soc
    assert (b.P == a.P).all()


if __name__ == "__main__":
    import numpy as np
    failed = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"PASS {name}")
            except AssertionError as e:
                failed += 1
                print(f"FAIL {name}: {e}")
    # Extra: print a short trajectory for eyeballing.
    ekf = BatteryEKF(sample_time_s=2.0); ekf.begin(90.0)
    print("\n[discharge @ -2A/cell, V=3.7]  soc / unc:")
    for k in range(1, 6):
        ekf.update(-2.0, 3.7, 25.0)
        print(f"  step {k:>2}: {ekf.soc:6.3f}%  unc={ekf.soc_uncertainty:.4f}")
    print("\nFAILURES:", failed)
    sys.exit(1 if failed else 0)
