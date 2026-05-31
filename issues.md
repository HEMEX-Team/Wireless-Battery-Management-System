========================================================================
wBMS — KNOWN ISSUES, GAPS, AND OPEN QUESTIONS
========================================================================
Companion to guide.md. Each item is a logical problem that will bite the
merged firmware if it is not handled. Numbering matches guide.md parts
where relevant.

========================================================================
PART A — MODE-SWITCH AND HEARTBEAT (guide Part 5)
========================================================================

A1. CHANNEL MISMATCH ON RECOVERY (already known).
The slave's AP and the master's ESP-NOW channel can differ. While
serving the AP the slave cannot hear the master. Recovery requires
periodically hopping back to the master's channel to listen/probe.

A2. HEARTBEAT CADENCE vs. CHANNEL-HOP CADENCE.
Master heartbeat is proposed at ~1 s. Slave only hops back to listen
every 10-30 s. The slave will miss almost every heartbeat by design.
Recovery condition must be "did I hear ANY heartbeat during my listen
window?" not "no heartbeat for N seconds."

A3. "MASTER AP DISCOVERABLE" != "UPLINK UP".
scanForChannel() locates master AP "WBMS-Node". If the master is up
but has no internet/MQTT, the scan still succeeds and the slave
falsely declares recovery. Same root cause as the ACK-only failure
detection in guide Part 5A: forward and reverse paths share the bug.

A4. NO HYSTERESIS / FLAPPING RISK.
Guide Part 5 defines only a forward threshold (FAILURE_THRESHOLD=10).
It does not define a recovery threshold. One stray heartbeat or one
missed window will toggle modes. Need symmetric "K good heartbeats
over T seconds" before tearing down the AP, and a cooldown to prevent
rapid mode flips.

A5. PART 5C CONTRADICTS THE HEARTBEAT PLAN.
Part 5C says "ESP-NOW is dead in fallback, so AP-on-any-channel is
fine." But Part 5A's heartbeat design needs ESP-NOW alive during AP
mode. Either the AP must run on the master's channel (loses the
"any channel" freedom) or the heartbeat is only heard during the
channel-hop windows in A2. Pick one and document it.

A6. AP CLIENTS DROP DURING CHANNEL HOP.
Browsers polling /api/data lose their session every 10-30 s while
the slave hops away. Hop window must be short (< ~1 s) and the
dashboard JS must retry silently. Otherwise users see "dashboard
broken" every recovery probe.

========================================================================
PART B — DATA PIPELINE AND MERGE (guide Parts 3, 4, 6)
========================================================================

B1. EKF STATE CONTINUITY ACROSS MODE SWITCH.
Guide Part 4 step 6 leaves "EKF always vs only-offline" as a choice
but does not state the cost of only-offline: SOC re-initializes from
OCV at every fallback, which is wrong under load. Recommendation:
EKF runs always. Then define how cloud-corrected SOC (Part 8) merges
with local EKF state — overwrite, blend with covariance, or ignore.
No rule exists today.

B2. 20 mA DEADBAND CORRUPTS CLOUD CHARGE INTEGRATION.
Guide Part 6 lists this as a value mismatch. The real problem is
that it accumulates: every idle period the cloud's coulomb count
diverges from the slave's. Over days, long-running cloud SOH/SOC
drifts. Send a cleaner current (smaller deadband, or CC1) for
cloud-side estimation.

B3. DA CONFIG 0x04 vs 0x05 IS A ONE-WAY MIGRATION.
Different scaling means raw current/voltage values get reinterpreted.
Existing recorded telemetry and any cloud-side calibration become
invalid the moment you switch. Treat as a migration, not a free
config pick.

B4. temp2 IS A SILENT SEMANTIC BREAK.
Same field name, different physical sensor (HDQ vs CFETOFF). Cloud
history labeled "temp2" means one thing; dashboard means another.
Whichever side you align to, the other side's history is mislabeled.
Rename, do not reconcile.

B5. CHARGE/TIME HANDOVER ON RECONNECT.
The BQ accumulators keep running; the cloud's "last seen" snapshot
freezes. On reconnect, the delta looks like a giant spike. Need a
"reset baseline on recovery" rule, or send absolute counters and let
the cloud compute deltas.

========================================================================
PART C — IDENTITY AND MULTI-DEVICE (guide Parts 8, 10)
========================================================================

C1. PAIRING CODE COLLISIONS.
Pairing code = last 3 MAC bytes = 24 bits. Birthday-collision risk
is real at fleet scale. Two slaves can publish to the same
"pairingCode" field on bms/data and subscribe to the same
bms/feedback/{code}. Fine for a handful of units, not fine at scale.

C2. SHARED AP SSID ACROSS SLAVES.
All slaves expose "wBMS-SlaveAP" / "wbms1234". Two offline slaves
in the same room are indistinguishable to a client. SSID should
include the pairing code (e.g. "wBMS-SlaveAP-ABC123").

========================================================================
PART D — SAFETY AND CONFIG (guide Appendices A, B)
========================================================================

D1. GLITCH FILTER MAY HIDE A FAILED CELL.
Cell-voltage filter `500 < V < 5000 mV` silently drops genuinely
failed cells (shorted, deeply over-discharged read < 500 mV). That
is a safety condition being hidden. Route out-of-range readings to
a fault flag, do not drop.

D2. OCC THRESHOLD 6 A vs 10 A IS A SAFETY DECISION.
Guide Part 6 calls it "confirm the real target." The real impact:
the BQ trips at 6 A under normal load if 10 A was intended. Treat as
a safety setting, sign-off required.

========================================================================
PART E — CROSS-REFERENCES THE GUIDE GETS WRONG
========================================================================

E1. ESP-NOW DIRECTIONALITY CONTRADICTION.
Guide Part 8 states ESP-NOW is one-way (slave -> master) as if it
were a permanent limitation. Part 5A's recommended fix and Part 8
item 1 both require bidirectional. The guide should make clear this
is a current-state limitation, not a protocol limit, and that both
features share the same plumbing.

========================================================================
PART F — OFFLINE READING BUFFER (NEW — to be added to guide.md)
========================================================================
When the slave goes offline (master/MQTT unreachable) it should cache
readings to flash and upload them when it comes back online. Today
NOTHING is cached: while in AP mode the slave only serves the _current_
reading at /api/data. The cloud sees a hole for the entire outage.

F1. STORAGE OPTIONS ON ESP32. - NVS: ~16 KB. Used today only for balancing settings and SOH cycle
count. Too small for a time-series buffer; do not use. - SPIFFS: legacy, deprecated for new designs. Avoid. - LittleFS: recommended. Power-loss safe, wear-leveling, good for
ring-buffered time series. - Partition budget on a 4 MB ESP32 module (typical):
bootloader + nvs + phy_init ~64 KB
app0 (factory or ota_0) ~1.5 MB
app1 (ota_1, optional) ~1.5 MB
data partition (SPIFFS/LFS) ~1 MB (default with OTA)
~3 MB (if OTA dropped) - On 8 MB / 16 MB modules the data partition can be much larger.
CONFIRM which module the production slave actually uses before
sizing the buffer.

F2. RECORD SIZE OPTIONS.
The full DeviceMessage is 152 bytes. For a time-series buffer that
is wasteful — most fields are static or low value. Three tiers:

    TIER 1 — Minimal (recommended for buffering):
      timestamp (4) + v_pack (4) + current (4) + soc (1) + soh (1)
      + temp_max (2) + flags (2) = 18 bytes/record.

    TIER 2 — Diagnostic:
      Tier 1 + per-cell delta (1B per cell, 13 cells) + safA/B/C (3)
      = 18 + 13 + 3 = 34 bytes/record.

    TIER 3 — Full struct:
      152 bytes (same as ESP-NOW). Use only if you want a complete
      replay of the live stream.

F3. CAPACITY MATH (rule of thumb: 1 MB data partition).
records_capacity = 1,000,000 / record_size
duration_hours = records_capacity \* sample_period_s / 3600

    With a 1 MB partition:
      record   period   records      duration
      18 B     10 s     ~55,500      ~154 h   (~6.4 days)
      18 B     30 s     ~55,500      ~462 h   (~19 days)
      18 B     60 s     ~55,500      ~925 h   (~38 days)
      34 B     10 s     ~29,400      ~ 81 h   (~3.4 days)
      34 B     30 s     ~29,400      ~245 h   (~10 days)
      152 B    10 s     ~6,580       ~ 18 h
      152 B    30 s     ~6,580       ~ 54 h   (~2.3 days)
      152 B   500 ms    ~6,580       ~55 min  (current live cadence,
                                                useless as a buffer)

    With a 3 MB partition (OTA dropped) multiply durations by 3.

F4. OFFLINE SAMPLING POLICY.
Live (online) cadence is 500 ms. That is far too fast for buffered
storage. Recommended offline policy:

      - Slow the buffered sample rate to one record every 10 s (default)
        or 30 s (long-outage mode).
      - KEEP the BQ I2C read loop at 500 ms so the local AP dashboard
        still feels live and the EKF stays well-fed.
      - The buffered record is decimated/averaged from the live samples
        (mean voltage, mean current, max temp, last flags) rather than a
        raw snapshot — preserves the integral, not just the moment.

    Adaptive option: trigger a fast-cadence burst (e.g. 1 s for 60 s)
    when a safety flag changes or |current| > threshold, so events are
    not smoothed out of the record.

F5. RING BUFFER, NOT APPEND-FOREVER.
Once full, overwrite the oldest record. Never let the partition fill
and brick writes. Track head/tail in NVS so a power cycle does not
lose the pointers. One file with fixed-size records and a wrap
pointer is simpler than many small files (and avoids LittleFS
metadata overhead).

F6. UPLOAD-ON-RECONNECT PROTOCOL. - On recovery (mode flip back to ONLINE), drain the buffer to the
master before resuming live sends, OR interleave (one buffered
record per N live sends) so the live stream is not blocked. - Each buffered record must carry its own timestamp (live ESP-NOW
sends do not — they are timestamped by the master on receipt). - The master must distinguish live vs. backfill on MQTT (separate
topic bms/backfill, or a flag in the payload) so the cloud does
not treat backfill as the latest reading. - ACK each backfilled record (or batch) before advancing the tail
pointer, so a mid-upload outage does not lose data.

F7. LEAKAGE / DRIFT ACCOUNTING WHILE OFFLINE.
Two separate concerns are bundled under "leakage":

    (a) Coulomb-count accuracy under slow sampling.
        The BQ76952 integrates current internally (DASTATUS6 accumulated
        charge, DASTATUS5 CC1/CC3). It does NOT lose accuracy when the
        ESP polls less often — it is the BQ that integrates, not the
        ESP. So a 10 s or 30 s buffered cadence is fine for total
        charge, as long as each buffered record stores the BQ's
        accumulated counter (absolute), not a per-record delta computed
        on the ESP. Then drift = 0 regardless of sample period.

        WATCH OUT: the 20 mA software deadband (guide Part 6) is applied
        on the ESP. If it is applied to buffered records too, small idle
        currents (self-discharge, balancing, ESP power draw) are dropped
        and the buffered total will drift relative to the BQ's own
        counter. Solution: buffer the raw BQ accumulator, apply deadband
        only for display.

    (b) Battery self-discharge and ESP power draw during AP mode.
        - Li-ion self-discharge: ~2-3 %/month, negligible over a few
          days of outage.
        - ESP32 in WiFi AP + active web server: ~120-180 mA continuous.
          On a typical 13S pack this is small in absolute terms but it
          IS being drawn from the monitored pack itself — so the cloud
          will see the SOC drop while the slave is offline even if no
          external load is present. Document this so the operator does
          not interpret it as a fault.
        - At 150 mA average, ESP-only draw over a 24 h outage = 3.6 Ah.
          On a (say) 50 Ah pack that is ~7 % SOC consumed by monitoring.
          Worth surfacing in the dashboard as "monitor self-consumption"
          rather than burying in the cell current.

F8. WRITE WEAR.
LittleFS does wear-leveling, but at 1 record per 10 s into ~1 MB:
writes/day = 8,640
flash endurance ~100k erase cycles per block
block count on 1 MB ~ 256 blocks of 4 KB
total writes before block exhaustion ~ 25.6 M
=> years of continuous offline buffering before wear matters.
Fine in practice. Only flag if the device is expected to spend most
of its life offline.

F9. WHAT TO DO NEXT (build order). 1. Confirm the actual flash size on the production slave module. 2. Repartition: drop OTA if you can, give the data partition >=2 MB. 3. Pick the record tier (start with Tier 1, 18 B). 4. Implement the ring buffer with head/tail in NVS. 5. Wire the offline path: when fallback AP starts, also start the
buffer writer at 10 s cadence; when AP stops, start the drain. 6. Add the bms/backfill topic (or backfill flag) on the master. 7. Surface "monitor self-consumption" on the dashboard.
