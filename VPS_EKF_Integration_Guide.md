# VPS-Side EKF Digital Twin — Integration Guide

How to run an **independent, cloud-side SoC estimator** on the VPS that shadows each
physical pack — a *digital twin* — and store **both** the device's SoC and the twin's
SoC in the database so their **divergence** becomes a first-class diagnostic signal.

> **Scope:** this is the design + reference implementation for the *cloud* work. It is a
> sibling to the firmware document `pio-.../EKF_Integration_Guide.md` (which is a
> firmware changelog, not a VPS guide). Code blocks are reference implementations to
> adapt, not drop-in-and-forget.

> **✅ Implementation status (built):** the twin is implemented under
> `backend/app/ekf/` (`battery_ekf.py`, `worker.py`, generated `model_data.npz`),
> `backend/tools/convert_ekf_model.py`, and `backend/tests/test_battery_ekf.py`, with
> the `ekf-worker` Compose service. **Final naming differs from the body below:** the
> twin column is **`vps_ekf_soc`** (+ `vps_ekf_soc_uncertainty`), *renamed* from the
> legacy `ekf_soc` via a migration (existing data preserved) — so wherever this design
> text says "reuse `ekf_soc`", the shipped column is `vps_ekf_soc`. The **NN is omitted**
> (ECM-only model, inert under the binary strategy — §5.3). **Firmware was intentionally
> not modified** (§5.0 fix is coupled + needs reflashing both nodes; the corrected EKF
> lives only in the cloud twin, so no reflash is required).

---

## 0. The idea in one paragraph

The master ESP32 already computes a SoC and publishes it. We add a **second** estimator
that runs **on the VPS**, reads telemetry back out of the database, and produces its
**own** SoC. The two numbers live side-by-side in every row (`soc` = device,
`ekf_soc` = twin). Because the twin is *independent* of the device's onboard
calculation, the gap between them means something: sensor drift, a firmware bug, cell
aging, a thermal problem. That comparison is the whole point — it's what makes this a
digital twin rather than a duplicated number.

```
   physical pack ──telemetry──▶  device SoC  (soc)      ┐
                                                         ├──▶  divergence = diagnostic
   same telemetry ─▶ VPS twin ─▶  twin SoC   (ekf_soc)  ┘
```

**Design principle:** a twin only has diagnostic value if it does **not** replicate the
device's estimator. If both ran the same algorithm on the same inputs they'd agree by
construction and tell you nothing. So the twin is the **reference-grade** model the
resource-constrained device can't run — specifically the *corrected/robust* EKF that the
firmware guide validated (and which the master firmware does **not** yet run; see §5.0).
When the device is healthy the two should converge; when it misbehaves they diverge.

---

## 1. Locked-in decisions

| Decision | Choice | Why |
|---|---|---|
| Twin model | **Reference EKF, same family as device** | Reuses the validated 4-state ECM; "cloud = gold standard, device = onboard approximation" (Option A) |
| Fidelity | **The *robust* EKF from the firmware guide**, not the code as-is | The code still has the buggy version; the twin runs the *fixed* one (§5) |
| Runtime | **Separate worker service** (its own container) | "Reads from the database"; decoupled, restartable, can backfill (§3) |
| Storage | **Reuse `readings.ekf_soc`** (+ new `ekf_soc_uncertainty`) | Column already exists, is `NOT NULL`, and is currently unread (§2, §6.3) |
| Neural net | **Off by default** | The binary strategy makes the NN inert — skip the 383 KB model (§5.3) |

---

## 2. The current pipeline (what we extend)

```
BQ76952 ─I2C→ Slave ESP32 (sender.cpp)  → soc_ekf (local), soh
                 │  ESP-NOW DeviceMessage  (config.h)  — NO soc field; carries
                 │  v[16], current(mA, RAW cc1, no deadband), temps, charge, soh, ssA/B/C
                 ▼
            Master ESP32 (reciever.cpp)  → runs its OWN EKF on cell-1 V → soc   (:1058)
                 │  MQTT "bms/data"  (buildJsonPayload :830 → "soc": ...)
                 ▼
            backend/app/mqtt_subscriber.py  (_persist_reading :180)
                 • mV→V, mA→A; one Reading + N BatteryReading rows
                 • soc      = payload["soc"]   (device)
                 • ekf_soc  = soc              ← redundant copy TODAY (:271)
                 ▼
            Postgres → FastAPI /v1/packs/* → React Dashboard
```

Facts that shaped the design:

- **`readings.ekf_soc` already exists**, is `NOT NULL`, and is just a copy of `soc`.
  **Nothing reads it** (not the API, not the frontend). It's the natural home for the
  twin estimate.
- **The cloud receives raw current.** `espnow_link.h:203` sends `cc1_raw` "clean … no
  deadband" — so `Reading.current` is **uncalibrated and un-deadbanded**. The twin must
  apply the calibration + deadband itself (§5.2, §7). This is *good*: the twin controls
  its own input conditioning.
- The firmware EKF is **per-cell** (cell-1 V, current ÷ parallel — `sender.cpp:238`,
  `reciever.cpp:1040`). The DB stores **pack-level** values. The twin must reconstruct
  per-cell inputs (§7) — the single most important correctness detail.
- Deployment is Docker Compose: `postgres:16`, `mosquitto`, `backend`, `frontend`.
  Backend image is `python:3.12-slim`; `numpy` is **not** yet a dependency.

---

## 3. Target architecture

```
   MQTT ──▶ backend (uvicorn)
              mqtt_subscriber → INSERT Reading (soc=device, ekf_soc=soc provisional)
                       │ writes
                       ▼
                 ┌───────────┐
                 │ Postgres  │  readings (+ ekf_soc_uncertainty)
                 │           │  ekf_state (NEW: per-pack twin state + watermark)
                 └─────┬─────┘
                       │ poll new rows (id > watermark), UPDATE ekf_soc(+uncertainty)
                       ▼
   ┌──────────────────────────────────────────────┐
   │ ekf-worker (NEW container, reuses backend img)│
   │  app/ekf/worker.py                            │
   │   • per-pack BatteryEKF (robust Python port)  │
   │   • condition current (calibrate + deadband)  │
   │   • reconstruct per-cell V & I, real Δt       │
   │   • write twin SoC + uncertainty back         │
   └──────────────────────────────────────────────┘
```

The worker is a separate process reusing the **same image** as the backend (different
command), so it shares models/config with zero duplication. It needs **only** the DB
(no MQTT, no ports). The subscriber's `ekf_soc = soc` stays as a **provisional seed**:
keeps `NOT NULL` satisfied and means a brand-new row always has *some* value until the
worker overwrites it (usually within one poll). "Not yet processed by the twin" is
detectable as `ekf_soc_uncertainty IS NULL`.

---

## 4. The digital-twin payoff: divergence

Once both SoCs are stored, `ekf_soc − soc` is the twin signal. Useful views:

- **Live:** show device SoC and twin SoC side-by-side on the dashboard, plus the delta.
- **Health:** sustained `|ekf_soc − soc| > τ` (e.g. 5 % for >N minutes) ⇒ raise a
  "device/cloud SoC disagreement" alert (possible sensor/firmware fault).
- **Expectation:** with today's firmware (old EKF) vs the twin (robust EKF), expect the
  twin to drain *less* under heavy load (the firmware guide measured 3.3 % vs a
  hallucinated 9.9 % on a 10 A test). When the firmware is fixed, they should converge —
  that convergence is how you confirm the fix in the field.

Divergence is computed on read (`ekf_soc − soc`); no extra column needed. Alerting is
future work (§12).

---

## 5. What the twin computes (the robust EKF)

Reference: `pio-.../lib/BatteryEKF/BatteryEKF.cpp` for structure, **plus the corrections
described in `pio-.../EKF_Integration_Guide.md`**.

### 5.0 ⚠️ The twin runs the *corrected* EKF, not the code as-is

The firmware guide documents a "robust binary EKF" overhaul, but **the working-tree code
still has the old version.** The twin intentionally implements the **corrected** design.
Concrete differences to apply in the Python port:

| Aspect | Old (in `BatteryEKF.cpp` today) | **Robust (what the twin uses)** | Ref |
|---|---|---|---|
| `adaptQR` | adaptive: load `Q[0]=1e-5,R=0.01`; rest `1e-6,R=0.05`; 20 mA | **binary**: load `Q[0]=1e-8,R=1.0`; rest `1e-5,R=0.005`; **10 mA** | guide §3B |
| Bootstrap R | `R0_avg/parallel + 2.43 Ω` parasitic | **`R0_avg/parallel` only** (2.43 Ω was a typo) | guide §3A |
| Sample time | 1 s (slave) / 2 s (master) | cloud uses **real Δt** from timestamps | guide §3D |
| Current | raw | **`I = (I_raw − 0.048)/1.030`** + **10 mA deadband** | guide §3C |

> This is deliberate. The twin is the reference; the device is the approximation. They
> are *meant* to differ until the firmware catches up.

### 5.1 The filter (unchanged structure)

State `x = [SOC, V_RC1, V_RC2, V_RC3]`, measurement = terminal voltage, input = current
(A; **negative = discharge**). Constants (`BatteryEKF.h`): `Q_NOMINAL_AH = 3.043`
(**per cell**, Samsung 30Q), `Q_NOMINAL_AS = Q_NOMINAL_AH·3600`.

| Step | Formula |
|---|---|
| State transition | `SOC' = SOC + I·Δt/Q_AS·100`; `V_RCk' = V_RCk·aₖ + Rₖ(1−aₖ)·I`, `aₖ = exp(−Δt/max(Rₖ·Cₖ,1e-9))` |
| OCV (hysteresis) | `I>0.02`→charge LUT; `I<−0.02`→discharge LUT; else average of both |
| Measurement | `V̂ = OCV + R0·I + V_RC1 + V_RC2 + V_RC3 (+ V_NN if enabled)` |
| Jacobian H | `H[0]=dOCV/dSOC + dR0/dSOC·I` (finite diff Δ=0.5), `H[1..3]=1` |
| Trust guard | if `|V−V̂| > 0.1` then `R ×= 1000` |
| Gain clamp | clamp `K[0]` to `±0.5` %/update |

### 5.2 Input conditioning (cloud-side, the twin's job)

The cloud gets raw pack current, so the worker applies, **in this order**, before the EKF:

1. **Calibrate:** `I = (I_pack − 0.048) / 1.030` (48 mA offset, +3 % gain).
   ⚠️ These are **device-specific** lab values. Defaults match the firmware guide; make
   them configurable per deployment (`EKF_CURRENT_OFFSET_A` / `EKF_CURRENT_GAIN`), set
   offset=0, gain=1 to disable. Ideally calibrate per pack eventually (§12).
2. **Deadband:** if `|I| < 0.010 A` → `I = 0` (kills idle quiescent-current drift).
3. **Per-cell:** `I_cell = I / parallel_count`.

### 5.3 Why the NN is off by default

The binary strategy makes the neural-net correction **inert**:
- **Under load** (`|I|>10 mA`): `R=1.0` ⇒ the Kalman gain on voltage is ~0, so the
  voltage prediction (and thus the NN term) barely affects SoC — it's coulomb counting.
- **At rest** (`|I|<10 mA`, deadbanded to 0): the NN gate is `|I|>0.01` ⇒ never fires.

So the NN never meaningfully changes the output. The twin therefore **skips the 383 KB
`NN_Weights.h`** by default (ECM tables only). It remains a one-flag re-enable
(`EKF_NN_ENABLED=true` + `--with-nn` on the converter) if you ever drop the binary
strategy. This is "same model family" — identical ECM EKF; the optional correction term
is simply inactive in this regime.

> **float32 vs float64:** firmware is 32-bit; NumPy is 64-bit. Sub-0.1 % numeric drift;
> use `np.float32` throughout for bit-closer parity (optional).

---

## 6. Implementation

### File layout

```
backend/
  requirements.txt                 # + numpy
  app/ekf/
    __init__.py                    # NEW
    battery_ekf.py                 # NEW – robust Python port
    model_data.npz                 # NEW – generated, committed (ECM only by default)
    worker.py                      # NEW – polling worker (entrypoint)
  app/models/models.py             # EDIT – + ekf_soc_uncertainty, + EkfState
  app/models/init_db.py            # EDIT – + lightweight migration row
  app/routes/packs.py              # EDIT – surface ekf_soc + divergence (additive)
  tools/convert_ekf_model.py       # NEW – C headers → model_data.npz (dev-time)
docker-compose.yml                 # EDIT – + ekf-worker service
tests/test_battery_ekf.py          # NEW – parity / sanity tests
```

> **Docker build-context caveat:** the backend image builds from `./backend` (`COPY . .`),
> so the firmware `.h` files under `pio-.../lib/BatteryEKF/` are **outside** the context.
> Run `convert_ekf_model.py` **at dev time**, **commit** `backend/app/ekf/model_data.npz`,
> and rebuild the image. Never read the `.h` files at container build/run time.

---

### 6.1 Step 1 — Convert the ECM tables to Python

`backend/tools/convert_ekf_model.py`. ECM tables are required; NN is opt-in (`--with-nn`).

```python
"""Convert firmware EKF model headers → backend/app/ekf/model_data.npz.

Dev-time only. ECM tables always; NN only with --with-nn (the twin doesn't need it).
Run from repo root, then COMMIT the .npz (headers are outside the Docker build context).

    python backend/tools/convert_ekf_model.py            # ECM only (default)
    python backend/tools/convert_ekf_model.py --with-nn  # also embed the NN
"""
import re
import sys
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[2]
LIB = REPO / "pio-Wireless-Battery-Managemnt-System" / "lib" / "BatteryEKF"
OUT = REPO / "backend" / "app" / "ekf" / "model_data.npz"

ECM = {  # header name -> (key, shape)   (row-major, matching the C++ indexing)
    "LUT_OCV_Discharge": ("OCV_Discharge", (101,)),
    "LUT_OCV_Charge":    ("OCV_Charge",    (101,)),
    "LUT_R0_2D":         ("R0",            (101, 4)),
    "LUT_RC1_R_2D":      ("RC1_R",         (101, 4)),
    "LUT_RC2_R_2D":      ("RC2_R",         (101, 4)),
    "LUT_RC3_R_2D":      ("RC3_R",         (101, 4)),
    "LUT_RC1_C":         ("RC1_C",         (101,)),
    "LUT_RC2_C":         ("RC2_C",         (101,)),
    "LUT_RC3_C":         ("RC3_C",         (101,)),
}
NN = {
    "NN_W1": ("W1", (128, 7)),   "NN_b1": ("b1", (128,)),
    "NN_W2": ("W2", (128, 128)), "NN_b2": ("b2", (128,)),
    "NN_W3": ("W3", (64, 128)),  "NN_b3": ("b3", (64,)),
    "NN_W4": ("W4", (64,)),      "NN_b4": ("b4", (1,)),
    "NN_feat_means": ("feat_means", (7,)),
    "NN_feat_stds":  ("feat_stds",  (7,)),
}
NN_SCALARS = {"NN_target_mean": "target_mean", "NN_target_std": "target_std"}

_FLOAT = re.compile(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?f?")


def parse_array(text, name, shape):
    # Capture between the first '{' after '=' and the terminating '};'. Inner rows of
    # 2-D tables end in '},' (never '};'), so the lazy match stops at the real end.
    m = re.search(rf"{name}\b[^=]*=\s*\{{(.*?)\}}\s*;", text, re.DOTALL)
    if not m:
        sys.exit(f"ERROR: array {name!r} not found")
    nums = [float(t[:-1] if t.endswith("f") else t) for t in _FLOAT.findall(m.group(1))]
    arr = np.array(nums, dtype=np.float64)
    if arr.size != int(np.prod(shape)):
        sys.exit(f"ERROR: {name}: {arr.size} values, expected {int(np.prod(shape))}")
    return arr.reshape(shape)


def parse_scalar(text, name):
    m = re.search(rf"{name}\s*=\s*([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)f?\s*;", text)
    if not m:
        sys.exit(f"ERROR: scalar {name!r} not found")
    return float(m.group(1))


def main():
    with_nn = "--with-nn" in sys.argv
    ecm_text = (LIB / "ECM_LookupTables.h").read_text()
    out = {key: parse_array(ecm_text, cname, shp) for cname, (key, shp) in ECM.items()}
    out["TEMP_AXIS"] = np.array([10.0, 25.0, 45.0, 60.0])
    if with_nn:
        nn_text = (LIB / "NN_Weights.h").read_text()
        for cname, (key, shp) in NN.items():
            out[key] = parse_array(nn_text, cname, shp)
        for cname, key in NN_SCALARS.items():
            out[key] = np.float64(parse_scalar(nn_text, cname))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(OUT, **out)
    print(f"Wrote {OUT} ({OUT.stat().st_size/1024:.0f} KB, {len(out)} arrays, NN={with_nn})")


if __name__ == "__main__":
    main()
```

**Verify:** run it; `python -c "import numpy as np; d=np.load('backend/app/ekf/model_data.npz'); print({k:v.shape for k,v in d.items()})"` shows `R0 (101,4)`, `OCV_Charge (101,)`, etc.

---

### 6.2 Step 2 — The robust EKF (Python)

`backend/app/ekf/battery_ekf.py`. Mirrors the firmware structure with the §5.0 corrections.

```python
"""Robust Python port of the firmware BatteryEKF, for the VPS digital twin.

Implements the *corrected* design (binary Q/R, no parasitic bootstrap) from
pio-.../EKF_Integration_Guide.md — NOT the older code in BatteryEKF.cpp. Operates on
PER-CELL inputs (volts, amps). Input conditioning (calibration/deadband) is the worker's
job (see the integration guide §5.2 / §7) — this class is a pure filter.
"""
from __future__ import annotations

import math
from functools import lru_cache
from pathlib import Path

import numpy as np

Q_NOMINAL_AH = 3.043
Q_NOMINAL_AS = Q_NOMINAL_AH * 3600.0


@lru_cache(maxsize=1)
def _model() -> dict:
    data = np.load(Path(__file__).with_name("model_data.npz"))
    return {k: data[k] for k in data.files}


def _lut1d(lut, soc):
    s = min(max(soc, 0.0), 100.0)
    i0 = int(s); i1 = min(i0 + 1, 100)
    return float(lut[i0]) if i1 == i0 else float(lut[i0] + (s - i0) * (lut[i1] - lut[i0]))


def _lut2d(lut, soc, temp, taxis):
    s = min(max(soc, 0.0), 100.0); t = min(max(temp, 10.0), 60.0)
    s0 = int(s); s1 = min(s0 + 1, 100); sf = s - s0
    if t >= 45.0:   t0, t1 = 2, 3
    elif t >= 25.0: t0, t1 = 1, 2
    else:           t0, t1 = 0, 1
    tf = (t - taxis[t0]) / (taxis[t1] - taxis[t0])
    v0 = lut[s0, t0] + sf * (lut[s1, t0] - lut[s0, t0])
    v1 = lut[s0, t1] + sf * (lut[s1, t1] - lut[s0, t1])
    return float(v0 + tf * (v1 - v0))


class BatteryEKF:
    def __init__(self, sample_time_s=2.0, adaptive=True, nn=False,
                 parasitic_r=0.0, max_gain_clamp=0.5, trust_guard=0.1):
        self.m = _model()
        if nn and "W1" not in self.m:
            raise RuntimeError("NN requested but model_data.npz has no NN (rebuild with --with-nn)")
        self.Ts = sample_time_s
        self.adaptive = adaptive
        self.nn = nn
        self.parasitic_R = parasitic_r
        self.max_gain_clamp = max_gain_clamp
        self.trust_guard_threshold = trust_guard
        self.x = np.zeros(4)                 # [SOC, V_RC1, V_RC2, V_RC3]
        self.P = np.zeros((4, 4))
        self.Q = np.array([1e-8, 1e-3, 1e-3, 1e-3])
        self.R = 1.0
        self.V_predicted = 0.0
        self.voltage_error = 0.0

    # ---- lifecycle ----
    def begin(self, soc_pct):
        self.x[:] = [min(max(soc_pct, 0.0), 100.0), 0.0, 0.0, 0.0]
        self.P[:] = 0.0
        self.P[0, 0] = 1.0
        self.P[1, 1] = self.P[2, 2] = self.P[3, 3] = 1e-6

    @property
    def soc(self): return float(self.x[0])
    @property
    def soc_uncertainty(self): return float(math.sqrt(max(self.P[0, 0], 0.0)))

    # ---- OCV inversion (bootstrap) ----
    def invert_ocv_average(self, voltage):
        avg = 0.5 * (self.m["OCV_Charge"] + self.m["OCV_Discharge"])
        if voltage <= avg[0]:   return 0.0
        if voltage >= avg[100]: return 100.0
        for soc in range(100):
            if avg[soc] <= voltage <= avg[soc + 1]:
                return soc + (voltage - avg[soc]) / (avg[soc + 1] - avg[soc])
        return 50.0

    # ---- ROBUST binary Q/R (the §5.0 correction) ----
    def _adapt_qr(self, current):
        if not self.adaptive:
            self.Q = np.array([1e-6, 1e-3, 1e-3, 1e-3]); self.R = 0.005 ** 2; return
        if abs(current) > 0.010:   # >10 mA: active load → pure coulomb counter
            self.Q[0] = 1e-8       # freeze SOC vs voltage
            self.R = 1.0           # distrust voltage under load
        else:                      # at rest → trust OCV
            self.Q[0] = 1e-5
            self.R = 0.005
        self.Q[1] = self.Q[2] = self.Q[3] = 1e-3

    def _nn_correction(self, soc, current, vrc2, vrc3, ocv, temp):
        m = self.m
        x = np.array([soc, current, vrc2, vrc3, 1.0 if current >= 0 else -1.0, ocv, temp])
        x = (x - m["feat_means"]) / m["feat_stds"]
        h1 = np.tanh(m["W1"] @ x + m["b1"])
        h2 = np.tanh(m["W2"] @ h1 + m["b2"])
        h3 = np.tanh(m["W3"] @ h2 + m["b3"])
        return float(m["W4"] @ h3 + m["b4"][0]) * float(m["target_std"]) + float(m["target_mean"])

    def _measurement(self, x, current, temp):
        m, taxis = self.m, self.m["TEMP_AXIS"]
        soc, v1, v2, v3 = x
        sc = min(max(soc, 0.0), 100.0)

        def ocv_at(s):
            if current > 0.02:  return _lut1d(m["OCV_Charge"], s)
            if current < -0.02: return _lut1d(m["OCV_Discharge"], s)
            return 0.5 * (_lut1d(m["OCV_Charge"], s) + _lut1d(m["OCV_Discharge"], s))

        ocv = ocv_at(sc)
        r0 = _lut2d(m["R0"], sc, temp, taxis)
        vnn = self._nn_correction(sc, current, v2, v3, ocv, temp) if (self.nn and abs(current) > 0.01) else 0.0
        v_pred = ocv + r0 * current + v1 + v2 + v3 + vnn

        d = 0.5; s_hi, s_lo = min(100.0, sc + d), max(0.0, sc - d)
        dv = ((ocv_at(s_hi) - ocv_at(s_lo)) / (s_hi - s_lo)
              + (_lut2d(m["R0"], s_hi, temp, taxis) - _lut2d(m["R0"], s_lo, temp, taxis)) / (s_hi - s_lo) * current)
        return v_pred, np.array([dv, 1.0, 1.0, 1.0])

    # ---- EKF step ----
    def update(self, current, voltage, temp=25.0):
        if self.parasitic_R > 0.001 and abs(current) > 0.05:
            voltage = voltage - current * self.parasitic_R
        self._adapt_qr(current)

        soc, v1, v2, v3 = self.x
        sc = min(max(soc, 0.0), 100.0); taxis = self.m["TEMP_AXIS"]
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

    # ---- state (de)serialization for the worker ----
    def dump_state(self):
        return {"x": self.x.tolist(), "P": self.P.flatten().tolist(),
                "voltage_error": self.voltage_error}

    def load_state(self, s):
        self.x = np.array(s["x"], dtype=float)
        self.P = np.array(s["P"], dtype=float).reshape(4, 4)
        self.voltage_error = s.get("voltage_error", 0.0)
```

---

### 6.3 Step 3 — Schema

**`backend/app/models/models.py`** — one column on `Reading`, one new table:

```python
# class Reading(Base):  ── beside ekf_soc:
    ekf_soc = Column(Float, nullable=False)              # twin SoC (was a copy of soc)
    ekf_soc_uncertainty = Column(Float, nullable=True)   # NEW: twin sqrt(P[0,0])
```

```python
class EkfState(Base):
    """Resumable per-pack twin state + ingest watermark. One row per pack."""
    __tablename__ = "ekf_state"
    pack_id = Column(Integer, ForeignKey("packs.id", ondelete="CASCADE"), primary_key=True)
    last_reading_id = Column(Integer, nullable=False, default=0)   # watermark
    last_timestamp = Column(DateTime, nullable=True)              # for real Δt
    state_json = Column(Text, nullable=True)                      # BatteryEKF.dump_state()
    initialized = Column(Boolean, nullable=False, default=False)
    updated_at = Column(DateTime, default=datetime.utcnow, nullable=False)
```

**`backend/app/models/init_db.py`** — add to `_LIGHTWEIGHT_MIGRATIONS` (the `ekf_state`
table is created by `create_all`, no entry needed):

```python
    ("readings", "ekf_soc_uncertainty", "FLOAT"),
```

**Verify:** start backend or worker; `\d readings` shows `ekf_soc_uncertainty`; `\dt`
lists `ekf_state`.

---

### 6.4 Step 4 — The worker

`backend/app/ekf/worker.py`.

```python
"""VPS digital-twin EKF worker.

Polls readings newer than each pack's watermark, conditions the current
(calibrate + deadband), reconstructs per-cell inputs, runs the robust BatteryEKF with
real Δt, and writes the twin estimate into readings.ekf_soc / ekf_soc_uncertainty.
Resumable via ekf_state.   Entrypoint:  python -m app.ekf.worker
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
MAX_DT_S = float(os.getenv("EKF_MAX_DT_S", "300"))       # gap beyond this → re-bootstrap
NN_ENABLED = os.getenv("EKF_NN_ENABLED", "false").lower() == "true"
ADAPTIVE = os.getenv("EKF_ADAPTIVE", "true").lower() != "false"
# Input conditioning (§5.2). DEVICE-SPECIFIC — defaults from the firmware guide.
CAL_GAIN = float(os.getenv("EKF_CURRENT_GAIN", "1.030"))
CAL_OFFSET_A = float(os.getenv("EKF_CURRENT_OFFSET_A", "0.048"))
DEADBAND_A = float(os.getenv("EKF_CURRENT_DEADBAND_A", "0.010"))
R0_AVG = float(os.getenv("EKF_R0_AVG", "0.018"))         # bootstrap IR (no 2.43Ω parasitic)


def _condition_current(i_pack_a: float) -> float:
    i = (i_pack_a - CAL_OFFSET_A) / CAL_GAIN
    return 0.0 if abs(i) < DEADBAND_A else i


def _cell_voltage(db, r: Reading, pack: Pack) -> float:
    """Per-cell representative voltage: mean of this row's BatteryReadings (exact),
    else v_real / series_count."""
    mean_v = (
        db.query(func.avg(BatteryReading.voltage))
        .filter(BatteryReading.pack_id == pack.id, BatteryReading.timestamp == r.timestamp)
        .scalar()
    )
    return float(mean_v) if mean_v else r.v_real / (pack.series_count or 1)


def _process_pack(db, pack: Pack) -> int:
    st = db.query(EkfState).filter(EkfState.pack_id == pack.id).first()
    if st is None:
        st = EkfState(pack_id=pack.id, last_reading_id=0, initialized=False)
        db.add(st)

    rows = (
        db.query(Reading)
        .filter(Reading.pack_id == pack.id, Reading.id > st.last_reading_id)
        .order_by(Reading.id.asc()).limit(BATCH_SIZE).all()
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
            # Cold start / long gap → re-bootstrap from OCV (no 2.43Ω parasitic).
            seed = ekf.invert_ocv_average(v_cell - R0_AVG * i_cell)
            ekf.begin(seed)
            st.initialized = True
            ekf.Ts = DEFAULT_DT_S
        else:
            ekf.Ts = dt

        ekf.update(i_cell, v_cell, temp)
        r.ekf_soc = ekf.soc
        r.ekf_soc_uncertainty = ekf.soc_uncertainty
        last_ts = r.timestamp

    st.last_reading_id = rows[-1].id
    st.last_timestamp = last_ts
    st.state_json = json.dumps(ekf.dump_state())
    st.updated_at = datetime.utcnow()
    db.commit()
    return len(rows)


def run_once() -> int:
    db = SessionLocal(); total = 0
    try:
        for pack in db.query(Pack).all():
            try:
                total += _process_pack(db, pack)
            except Exception:
                db.rollback()
                log.exception("EKF failed for pack %s", pack.id)
        return total
    finally:
        db.close()


def main():
    logging.basicConfig(level=os.getenv("WBMS_LOG_LEVEL", "INFO"),
                        format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    Base.metadata.create_all(bind=engine)
    apply_lightweight_migrations(engine)
    log.info("EKF twin worker started (poll=%.1fs batch=%d nn=%s)", POLL_INTERVAL_S, BATCH_SIZE, NN_ENABLED)
    while True:
        try:
            n = run_once()
            if n:
                log.info("twin processed %d readings", n)
        except Exception:
            log.exception("poll cycle failed")
        time.sleep(POLL_INTERVAL_S)


if __name__ == "__main__":
    main()
```

> The subscriber needs **no change** — `ekf_soc = soc` stays as the provisional seed.

**Verify:** `python -m app.ekf.worker` logs the start banner; once telemetry exists,
`ekf_soc_uncertainty` goes non-NULL and `ekf_soc` diverges from `soc`.

---

### 6.5 Step 5 — Deploy

`backend/requirements.txt`: add `numpy==2.1.3`.

`docker-compose.yml` — new service reusing the backend image:

```yaml
  ekf-worker:
    build: ./backend
    restart: unless-stopped
    command: ["python", "-m", "app.ekf.worker"]
    environment:
      - WBMS_DATABASE_URL=postgresql+psycopg2://wbms:50efdb4e90c54b99cb9d83dcd6a26f2308b3@postgres:5432/wbms
      - WBMS_LOG_LEVEL=INFO
      - EKF_POLL_INTERVAL_S=5
      - EKF_NN_ENABLED=false
      # DEVICE-SPECIFIC current calibration (set offset=0, gain=1 to disable):
      - EKF_CURRENT_OFFSET_A=0.048
      - EKF_CURRENT_GAIN=1.030
      - EKF_CURRENT_DEADBAND_A=0.010
    depends_on:
      postgres:
        condition: service_healthy
```

**Verify:** `docker compose up -d --build ekf-worker`; `docker compose logs -f ekf-worker`.

---

### 6.6 Step 6 — Surface the twin (additive)

The frontend reading-history parser is **positional** (`Dashboard.jsx:2042` reads
`r[1]`=soc), so **append** new columns to the end of `_EXPORT_COLUMNS` — never insert.

**`backend/app/routes/packs.py`:**

```python
_EXPORT_COLUMNS = [
    "timestamp", "soc", "soh", "voltage_v", "current_a",
    "temperature_c", "temp1_c", "temp2_c", "temp3_c", "power_w", "state",
    "ekf_soc", "ekf_soc_uncertainty",          # NEW — appended (twin)
]

def _reading_row(r):
    return [
        # ... unchanged fields ...,
        _round(r.ekf_soc, 2), _round(r.ekf_soc_uncertainty, 3),
    ]
```

Live card in `get_latest_pack_data` — expose both SoCs and the divergence:

```python
            ekf_soc = round(latest_reading.ekf_soc) if latest_reading.ekf_soc is not None else None
            # ...
            battery_packs.append({
                # ... existing keys ...
                "soc": soc,                         # device
                "ekf_soc": ekf_soc,                 # twin (server)
                "soc_divergence": (round(latest_reading.ekf_soc - latest_reading.soc, 1)
                                   if latest_reading.ekf_soc is not None else None),
            })
```

Frontend (optional): plot `ekf_soc` as a second SoC line and show a "Device vs Twin"
delta. Label clearly — `soc` = device, `ekf_soc` = cloud twin.

**Verify:** `/v1/packs/{id}/readings` returns the two trailing columns; CSV headers
include them; existing `soc` charts unchanged (indices preserved).

---

### 6.7 Step 7 — Tests

`tests/test_battery_ekf.py`:

```python
import numpy as np
from app.ekf.battery_ekf import BatteryEKF, _model


def test_ecm_shapes():
    m = _model()
    assert m["R0"].shape == (101, 4)
    assert m["OCV_Charge"].shape == (101,)


def test_bootstrap_monotonic():
    ekf = BatteryEKF()
    assert ekf.invert_ocv_average(3.3) < ekf.invert_ocv_average(3.9)


def test_binary_strategy_is_coulomb_under_load():
    """Under load the robust filter ≈ pure coulomb counter: SoC drop matches I·t/Q,
    nearly independent of the (deliberately wrong) voltage."""
    ekf = BatteryEKF(sample_time_s=2.0)
    ekf.begin(80.0)
    soc0 = ekf.soc
    for _ in range(100):                       # ~1C cell discharge, bogus voltage
        ekf.update(current=-3.0, voltage=3.0, temp=25.0)
        assert 0.0 <= ekf.soc <= 100.0
    expected_drop = 3.0 * (100 * 2.0) / (3.043 * 3600) * 100
    assert abs((soc0 - ekf.soc) - expected_drop) < 0.5   # within 0.5% of pure CC


def test_rest_corrects_toward_ocv():
    ekf = BatteryEKF(sample_time_s=2.0)
    ekf.begin(50.0)
    for _ in range(200):                       # at rest (I=0): trust OCV
        ekf.update(current=0.0, voltage=4.0, temp=25.0)
    assert ekf.soc > 50.0                       # higher resting V pulls SoC up
```

**Parity (manual):** capture a real device `[EKF] I=..A, V=..V -> SOC=..%` log
(`sender.cpp:242`) or the slave AP CSV export, feed the same `(I, V)` to the Python
filter, and confirm the trajectories agree. Remember the twin runs the *robust* tuning,
so it should match the *fixed* firmware, not necessarily today's master output.

---

## 7. Per-cell + conditioning contract (read twice)

The firmware EKF models **one 30Q cell** (`Q_NOMINAL_AH = 3.043`). The DB stores **pack**
values, **raw**. The worker converts, in order:

| EKF input | DB source | Conversion |
|---|---|---|
| current (A) | `readings.current` (raw, pack) | `(I − 0.048)/1.030` → deadband 10 mA → `÷ parallel_count` |
| cell voltage (V) | `battery_readings.voltage` (mean) or `readings.v_real` | mean of cells, else `v_real / series_count` |
| temperature (°C) | `readings.temperature` | as-is (LUT clamps 10–60) |
| Δt (s) | `readings.timestamp` deltas | real Δt; re-bootstrap if `> EKF_MAX_DT_S` |

- **Current sign** is already correct end-to-end (negative = discharge). Don't flip it.
- **Calibration is device-specific.** The 0.048/1.030 defaults come from one lab unit;
  per-pack calibration is future work (§12). `offset=0, gain=1` disables it.
- **`series_count` mismatch:** if a pack's configured `series_count` ≠ reported cell
  count, `v_real / series_count` is wrong — which is why the mean of `battery_readings`
  is preferred and the division is only a fallback.
- **Capacity:** `Q_NOMINAL_AH` is the 30Q value. A different cell integrates at the
  wrong rate; scale from `packs.cell_capacity_ah` when populated (§12).

---

## 8. State, restarts, idempotency

- **Watermark** = `ekf_state.last_reading_id`; process `Reading.id > watermark` in `id`
  order. Crash mid-batch → uncommitted → reruns cleanly.
- **Resumable state**: `ekf_state.state_json` holds `x` (4) + `P` (16) so the recursive
  filter survives restarts.
- **Cold start / gaps**: no state, or Δt `> EKF_MAX_DT_S` → re-bootstrap from OCV (not
  integrate a bogus multi-hour Δt).
- **Reprocess** after a model/tuning change: `DELETE FROM ekf_state;` (optionally reset
  `readings.ekf_soc`); the worker rebuilds every pack from id 0.
- **Single writer**: run **one** `ekf-worker`. Per-pack state makes concurrent workers on
  the same pack unsafe; to scale, shard by `pack_id` or take a Postgres advisory lock
  (`pg_try_advisory_lock(pack_id)`).
- **Backfill** is automatic: a fresh worker walks history in `BATCH_SIZE` chunks until
  caught up, then settles into polling.

---

## 9. Edge cases & gotchas

- **`ekf_soc` is `NOT NULL`** → keep the subscriber's provisional `ekf_soc = soc`.
- **Fake telemetry is not physical.** `fake_publisher.py` sends random voltages with no
  causal I–V relationship; under the binary strategy the twin will still coulomb-count
  the (random) current, but the result is meaningless. Validate with real/replayed data.
- **NN off by default** is intentional (§5.3) — the binary regime makes it inert. Only
  rebuild with `--with-nn` + `EKF_NN_ENABLED=true` if you abandon the binary strategy.
- **Positional CSV/JSON** → append-only to `_EXPORT_COLUMNS`.
- **SQLite vs Postgres**: worker is DB-agnostic; SQLite is fine for a single local worker.
- **Model file ships in the image** (`backend/app/ekf/model_data.npz`, committed);
  rebuild after re-running the converter.
- **Device-specific calibration** can *hurt* on a pack it wasn't measured for — prefer
  disabling (gain=1, offset=0) over applying wrong constants fleet-wide.

---

## 10. Acceptance criteria

1. `convert_ekf_model.py` emits `model_data.npz` with the ECM shapes in §6.1.
2. `pytest tests/test_battery_ekf.py` passes (incl. the coulomb-counter check).
3. With real/replayed telemetry: every new row gets non-NULL `ekf_soc_uncertainty`, and
   `ekf_soc` tracks but is **not identical** to `soc`.
4. Restarting `ekf-worker` resumes from the watermark (no recompute storm, no gaps).
5. `/v1/packs/{id}/readings` + CSV expose `ekf_soc`/`ekf_soc_uncertainty` as **trailing**
   columns; existing `soc` charts unchanged; `/data/latest` returns `soc_divergence`.
6. Parity spot-check vs a real device log agrees within a few tenths of a percent
   (against the *robust* firmware behavior).

---

## 11. Rollout & rollback

**Rollout:** add `numpy`; run converter; commit `.npz` → land `app/ekf/` + model/route
edits → deploy backend (applies migration + creates `ekf_state`) →
`docker compose up -d --build ekf-worker` → confirm §10.

**Rollback:** stop/remove `ekf-worker`. New rows keep `ekf_soc == soc`; nothing breaks.
The added column/table are harmless if left; to fully revert, drop
`readings.ekf_soc_uncertainty` + the `ekf_state` table and revert the `packs.py` edits.

---

## 12. Future work

- **Divergence alerting:** raise an alert when `|soc − ekf_soc| > τ` for a sustained
  window (device-vs-twin disagreement = candidate fault). The richest payoff of the twin.
- **Per-pack calibration & capacity:** store current offset/gain and `cell_capacity_ah`
  per pack; scale `Q_NOMINAL_AS` and conditioning accordingly.
- **Reconcile firmware:** apply the robust EKF to the master too; healthy convergence
  then validates the firmware fix in the field.
- **Richer twin state:** the twin already estimates `V_RC1..3` and uncertainty — surface
  them; optionally add coulomb-count (`cc_soc` from `readings.charge`) as a third trace.
- **Per-cell twin:** one filter per `battery_position` for imbalance detection (N× cost).

---

## Appendix — quick reference

**Worker env vars**

| Var | Default | Meaning |
|---|---|---|
| `EKF_POLL_INTERVAL_S` | 5 | seconds between polls |
| `EKF_BATCH_SIZE` | 500 | max rows per pack per cycle |
| `EKF_SAMPLE_TIME_DEFAULT` | 2.0 | Δt at bootstrap (s) |
| `EKF_MAX_DT_S` | 300 | gap → re-bootstrap (s) |
| `EKF_NN_ENABLED` | false | enable NN (needs `--with-nn` model) |
| `EKF_ADAPTIVE` | true | binary Q/R (false → fixed tuning) |
| `EKF_CURRENT_OFFSET_A` | 0.048 | current offset (device-specific) |
| `EKF_CURRENT_GAIN` | 1.030 | current gain (device-specific) |
| `EKF_CURRENT_DEADBAND_A` | 0.010 | idle deadband |
| `EKF_R0_AVG` | 0.018 | bootstrap IR (no parasitic) |

**Source of truth**
- `pio-.../EKF_Integration_Guide.md` — the robust EKF corrections (binary Q/R, no
  parasitic, calibration, deadband) — **the behavior the twin implements**
- `pio-.../lib/BatteryEKF/BatteryEKF.cpp` — filter *structure* (the math), but its
  `adaptQR`/bootstrap are the **old** version — do not copy those verbatim
- `pio-.../lib/BatteryEKF/ECM_LookupTables.h` — OCV/R0/RC tables (required)
- `pio-.../lib/BatteryEKF/NN_Weights.h` — NN weights (optional; inert here)

**Backend touch-points**
- `mqtt_subscriber.py:258-279` — where `soc`/`ekf_soc` are written
- `models/models.py` — `Reading`, new `EkfState`
- `models/init_db.py` — lightweight migrations
- `routes/packs.py` — `_EXPORT_COLUMNS`, `_reading_row`, `get_latest_pack_data`
```
