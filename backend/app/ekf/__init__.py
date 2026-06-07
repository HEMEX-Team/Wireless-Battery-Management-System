"""VPS-side EKF digital twin: an independent cloud SoC estimator.

See VPS_EKF_Integration_Guide.md (repo root) for the full design. The twin runs the
*robust* version of the firmware EKF and writes its estimate into
readings.vps_ekf_soc / vps_ekf_soc_uncertainty so it sits next to the device's `soc`.
"""
