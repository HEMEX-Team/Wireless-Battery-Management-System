"""Convert firmware EKF model headers -> backend/app/ekf/model_data.npz.

Dev-time only. ECM tables always; NN only with --with-nn (the digital twin's binary
strategy makes the NN inert, so it is omitted by default). Run from the repo root,
then COMMIT the resulting .npz (the .h files live outside the Docker build context).

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

# header symbol -> (output key, shape).  Row-major, matching the C++ indexing.
ECM = {
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


def parse_array(text: str, name: str, shape) -> np.ndarray:
    # Capture between the first '{' after '=' and the terminating '};'. Inner rows of
    # 2-D tables end in '},' (never '};'), so the lazy match stops at the real end.
    m = re.search(rf"{name}\b[^=]*=\s*\{{(.*?)\}}\s*;", text, re.DOTALL)
    if not m:
        sys.exit(f"ERROR: array {name!r} not found in headers")
    nums = [float(t[:-1] if t.endswith("f") else t) for t in _FLOAT.findall(m.group(1))]
    arr = np.array(nums, dtype=np.float64)
    if arr.size != int(np.prod(shape)):
        sys.exit(f"ERROR: {name}: parsed {arr.size} values, expected {int(np.prod(shape))}")
    return arr.reshape(shape)


def parse_scalar(text: str, name: str) -> float:
    m = re.search(rf"{name}\s*=\s*([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)f?\s*;", text)
    if not m:
        sys.exit(f"ERROR: scalar {name!r} not found in headers")
    return float(m.group(1))


def main() -> None:
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
    print(f"Wrote {OUT}")
    print(f"  {OUT.stat().st_size / 1024:.0f} KB, {len(out)} arrays, NN={with_nn}")


if __name__ == "__main__":
    main()
