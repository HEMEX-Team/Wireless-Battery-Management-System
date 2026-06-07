"""Robust Python port of the firmware BatteryEKF, for the VPS digital twin.

Implements the *corrected* design (binary Q/R, no parasitic bootstrap) documented in
pio-.../EKF_Integration_Guide.md — NOT the older code in BatteryEKF.cpp. The filter
structure (4-state ECM: [SOC, V_RC1, V_RC2, V_RC3]) mirrors BatteryEKF.cpp; the tuning
is the robust version.

Operates on PER-CELL inputs (volts, amps). Input conditioning (current calibration /
deadband) is the worker's job — this class is a pure filter. See the guide §5 / §7.
"""
from __future__ import annotations

import math
from functools import lru_cache
from pathlib import Path

import numpy as np

# Samsung 30Q cell — PER CELL (the firmware models a single representative cell).
Q_NOMINAL_AH = 3.043
Q_NOMINAL_AS = Q_NOMINAL_AH * 3600.0


@lru_cache(maxsize=1)
def _model() -> dict:
    data = np.load(Path(__file__).with_name("model_data.npz"))
    return {k: data[k] for k in data.files}


def _lut1d(lut, soc: float) -> float:
    s = min(max(soc, 0.0), 100.0)
    i0 = int(s)
    i1 = min(i0 + 1, 100)
    return float(lut[i0]) if i1 == i0 else float(lut[i0] + (s - i0) * (lut[i1] - lut[i0]))


def _lut2d(lut, soc: float, temp: float, taxis) -> float:
    s = min(max(soc, 0.0), 100.0)
    t = min(max(temp, 10.0), 60.0)
    s0 = int(s)
    s1 = min(s0 + 1, 100)
    sf = s - s0
    if t >= 45.0:
        t0, t1 = 2, 3
    elif t >= 25.0:
        t0, t1 = 1, 2
    else:
        t0, t1 = 0, 1
    tf = (t - taxis[t0]) / (taxis[t1] - taxis[t0])
    v0 = lut[s0, t0] + sf * (lut[s1, t0] - lut[s0, t0])
    v1 = lut[s0, t1] + sf * (lut[s1, t1] - lut[s0, t1])
    return float(v0 + tf * (v1 - v0))


class BatteryEKF:
    def __init__(self, sample_time_s: float = 2.0, adaptive: bool = True,
                 nn: bool = False, parasitic_r: float = 0.0,
                 max_gain_clamp: float = 0.5, trust_guard: float = 0.1):
        self.m = _model()
        if nn and "W1" not in self.m:
            raise RuntimeError("NN requested but model_data.npz has no NN "
                               "(regenerate with: convert_ekf_model.py --with-nn)")
        self.Ts = sample_time_s
        self.adaptive = adaptive
        self.nn = nn
        self.parasitic_R = parasitic_r
        self.max_gain_clamp = max_gain_clamp
        self.trust_guard_threshold = trust_guard
        self.x = np.zeros(4)                       # [SOC, V_RC1, V_RC2, V_RC3]
        self.P = np.zeros((4, 4))
        self.Q = np.array([1e-8, 1e-3, 1e-3, 1e-3])
        self.R = 1.0
        self.V_predicted = 0.0
        self.voltage_error = 0.0

    # ---- lifecycle -------------------------------------------------------
    def begin(self, soc_pct: float) -> None:
        self.x[:] = [min(max(soc_pct, 0.0), 100.0), 0.0, 0.0, 0.0]
        self.P[:] = 0.0
        self.P[0, 0] = 1.0
        self.P[1, 1] = self.P[2, 2] = self.P[3, 3] = 1e-6

    @property
    def soc(self) -> float:
        return float(self.x[0])

    @property
    def soc_uncertainty(self) -> float:
        return float(math.sqrt(max(self.P[0, 0], 0.0)))

    # ---- OCV inversion (bootstrap) --------------------------------------
    def invert_ocv_average(self, voltage: float) -> float:
        """Seed SoC from a rest-state voltage using the mean of the charge/discharge
        OCV branches (both monotonic in SoC)."""
        avg = 0.5 * (self.m["OCV_Charge"] + self.m["OCV_Discharge"])
        if voltage <= avg[0]:
            return 0.0
        if voltage >= avg[100]:
            return 100.0
        for soc in range(100):
            if avg[soc] <= voltage <= avg[soc + 1]:
                return float(soc + (voltage - avg[soc]) / (avg[soc + 1] - avg[soc]))
        return 50.0

    # ---- ROBUST binary Q/R (the firmware-guide §3B correction) ----------
    def _adapt_qr(self, current: float) -> None:
        if not self.adaptive:
            self.Q = np.array([1e-6, 1e-3, 1e-3, 1e-3])
            self.R = 0.005 ** 2
            return
        if abs(current) > 0.010:   # > 10 mA: active load -> pure coulomb counter
            self.Q[0] = 1e-8       # freeze SOC state vs voltage
            self.R = 1.0           # distrust voltage under load
        else:                      # at rest -> trust OCV curve
            self.Q[0] = 1e-5
            self.R = 0.005
        self.Q[1] = self.Q[2] = self.Q[3] = 1e-3

    def _nn_correction(self, soc, current, vrc2, vrc3, ocv, temp) -> float:
        m = self.m
        x = np.array([soc, current, vrc2, vrc3,
                      1.0 if current >= 0.0 else -1.0, ocv, temp])
        x = (x - m["feat_means"]) / m["feat_stds"]
        h1 = np.tanh(m["W1"] @ x + m["b1"])
        h2 = np.tanh(m["W2"] @ h1 + m["b2"])
        h3 = np.tanh(m["W3"] @ h2 + m["b3"])
        y = float(m["W4"] @ h3 + m["b4"][0])
        return y * float(m["target_std"]) + float(m["target_mean"])

    def _measurement(self, x, current, temp):
        m, taxis = self.m, self.m["TEMP_AXIS"]
        soc, v1, v2, v3 = x
        sc = min(max(soc, 0.0), 100.0)

        def ocv_at(s):
            if current > 0.02:
                return _lut1d(m["OCV_Charge"], s)
            if current < -0.02:
                return _lut1d(m["OCV_Discharge"], s)
            return 0.5 * (_lut1d(m["OCV_Charge"], s) + _lut1d(m["OCV_Discharge"], s))

        ocv = ocv_at(sc)
        r0 = _lut2d(m["R0"], sc, temp, taxis)
        vnn = (self._nn_correction(sc, current, v2, v3, ocv, temp)
               if (self.nn and abs(current) > 0.01) else 0.0)
        v_pred = ocv + r0 * current + v1 + v2 + v3 + vnn

        d = 0.5
        s_hi, s_lo = min(100.0, sc + d), max(0.0, sc - d)
        dv = ((ocv_at(s_hi) - ocv_at(s_lo)) / (s_hi - s_lo)
              + (_lut2d(m["R0"], s_hi, temp, taxis) - _lut2d(m["R0"], s_lo, temp, taxis))
              / (s_hi - s_lo) * current)
        return v_pred, np.array([dv, 1.0, 1.0, 1.0])

    # ---- EKF step --------------------------------------------------------
    def update(self, current: float, voltage: float, temp: float = 25.0) -> None:
        if self.parasitic_R > 0.001 and abs(current) > 0.05:
            voltage = voltage - current * self.parasitic_R

        self._adapt_qr(current)

        # --- prediction ---
        soc, v1, v2, v3 = self.x
        sc = min(max(soc, 0.0), 100.0)
        taxis = self.m["TEMP_AXIS"]
        R1 = _lut2d(self.m["RC1_R"], sc, temp, taxis); C1 = _lut1d(self.m["RC1_C"], sc)
        R2 = _lut2d(self.m["RC2_R"], sc, temp, taxis); C2 = _lut1d(self.m["RC2_C"], sc)
        R3 = _lut2d(self.m["RC3_R"], sc, temp, taxis); C3 = _lut1d(self.m["RC3_C"], sc)
        a1 = math.exp(-self.Ts / max(R1 * C1, 1e-9))
        a2 = math.exp(-self.Ts / max(R2 * C2, 1e-9))
        a3 = math.exp(-self.Ts / max(R3 * C3, 1e-9))
        x_pred = np.array([
            soc + current * self.Ts / Q_NOMINAL_AS * 100.0,
            v1 * a1 + R1 * (1.0 - a1) * current,
            v2 * a2 + R2 * (1.0 - a2) * current,
            v3 * a3 + R3 * (1.0 - a3) * current,
        ])
        F = np.diag([1.0, a1, a2, a3])
        P_pred = F @ self.P @ F.T
        P_pred[np.diag_indices(4)] += self.Q

        # --- measurement update ---
        v_hat, H = self._measurement(x_pred, current, temp)
        self.V_predicted = v_hat
        self.voltage_error = abs(voltage - v_hat)
        final_R = self.R * (1000.0 if self.voltage_error > self.trust_guard_threshold else 1.0)

        S = float(H @ P_pred @ H) + final_R
        K = (P_pred @ H) / S
        K[0] = min(max(K[0], -self.max_gain_clamp), self.max_gain_clamp)
        self.x = x_pred + K * (voltage - v_hat)
        self.x[0] = min(max(self.x[0], 0.0), 100.0)
        self.P = (np.eye(4) - np.outer(K, H)) @ P_pred

    # ---- state (de)serialization for the worker's ekf_state table -------
    def dump_state(self) -> dict:
        return {"x": self.x.tolist(), "P": self.P.flatten().tolist(),
                "voltage_error": self.voltage_error}

    def load_state(self, s: dict) -> None:
        self.x = np.array(s["x"], dtype=float)
        self.P = np.array(s["P"], dtype=float).reshape(4, 4)
        self.voltage_error = s.get("voltage_error", 0.0)
