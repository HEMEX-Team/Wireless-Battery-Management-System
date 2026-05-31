========================================================================
wBMS INTEGRATION GUIDE (CORRECTED)
========================================================================
This guide describes the target design (one firmware, two runtime modes)
and what the code actually does today, so you know exactly what to build
to get there. It is based on the real code in the repo.

========================================================================
PART 1: THE TARGET DESIGN — ONE FIRMWARE, TWO RUNTIME MODES
========================================================================
You flash ONE firmware onto the slave ESP32. It switches mode at runtime:

MODE 1 — ONLINE (default):
  - Slave reads the BQ76952 over I2C.
  - Sends a small binary struct to the master over ESP-NOW every 500 ms.
  - Master forwards it to the cloud over MQTT (topic bms/data).
  - No local dashboard needed; the website shows the data.

MODE 2 — OFFLINE (fallback, when the master is unreachable):
  - Slave stops sending over ESP-NOW.
  - Slave starts its own WiFi Access Point.
  - Slave serves a local web dashboard + JSON at GET /api/data.
  - Slave runs its local EKF and SOH so the dashboard still shows SOC/SOH.
  - When the master comes back, slave drops the AP and returns to Mode 1.

The trigger to switch: consecutive ESP-NOW send failures. The slave's
send callback already counts failures; after a threshold it is "offline."

The BQ76952 is read the SAME way in both modes. Only the OUTPUT changes:
in Mode 1 the reading is packed into the ESP-NOW struct; in Mode 2 the
same reading is served as the dashboard JSON.

========================================================================
PART 2: CURRENT STATE — THE CODE IS NOT YET ONE FIRMWARE
========================================================================
Today the two modes live in TWO SEPARATE SKETCHES. Neither one alone is
the firmware you want. To reach Part 1, they must be merged into one.

SKETCH A — ESP-NOW SENDER  (src/slaves/sender.cpp + src/master/reciever.cpp)
  Has:
    - ESP-NOW client sending the struct every 500 ms (Mode 1 output).
    - A heartbeat: consecutiveFailures counter, FAILURE_THRESHOLD = 10.
    - startFallbackAP() / stopFallbackAP() that switch WiFi mode when the
      master is unreachable.
  Missing:
    - The fallback AP serves NOTHING. There is no web server, no
      /api/data, no dashboard in this sketch.
    - No EKF, no SOH.

SKETCH B — AP DASHBOARD  (src/tb_v2_on_mainboard/)
  Has:
    - Web server + full dashboard + JSON at /api/data (Mode 2 output).
    - Local EKF and SOH.
    - SoftAP "wBMS-SlaveAP" / "wbms1234" (tb_config.h).
  Missing:
    - No ESP-NOW, no master link, no MQTT.
    - Its AP is ALWAYS on (it never acts as a client).

So: Sketch A knows how to be the client and how to detect "offline" and
flip to AP. Sketch B knows how to BE the AP dashboard. The merge puts
Sketch B's web server + EKF + SOH behind Sketch A's mode switch.

========================================================================
PART 3: ONE BQ READ, TWO DIFFERENT OUTPUTS (the byte budget)
========================================================================
The two modes do NOT output the same data. Same source, different format
and size.

MODE 1 OUTPUT — ESP-NOW struct (DeviceMessage in config.h):
  - Binary packed. Total = 152 bytes:
    v[16]=64, v_stack=4, v_pack=4, current=4, chip_temp=4,
    temp1/2/3=12, charge=4, charge_time=4, isCharging=1,
    isDischarging=1, message[50]=50  ->  152 bytes.
  - ESP-NOW maximum payload is 250 bytes. 152 fits, with room to spare.
  - Carries about 15 values. NO EKF, NO SOH, NO safety registers, NO
    balancing, NO FET status — those fields are not in the struct.

MODE 2 OUTPUT — AP dashboard JSON (web_api.h, /api/data):
  - About 60 fields. Buffer reserved at 2048 bytes; real size ~1.2-1.8 KB.
  - Carries everything (full list in Appendix C).

WHY THIS MATTERS FOR THE MERGE:
  - You CANNOT send the dashboard JSON (~1.5 KB) over ESP-NOW. It is ~6x
    over the 250-byte limit. The big JSON is for the local AP only.
  - So the website (Mode 1, via cloud) sees only the 15 struct values.
    The local dashboard (Mode 2) sees all 60.
  - If you want the cloud to also receive SOH / EKF / safety, you must
    ADD those fields to the 152-byte struct and keep the total under 250.

========================================================================
PART 4: STEPS TO MERGE INTO ONE FIRMWARE
========================================================================
1. Start from the sender (Sketch A). It already has ESP-NOW, the failure
   counter, and the AP switch.
2. Bring in from Sketch B: web_api.h (web server + /api/data handlers),
   the EKF (BatteryEKF), and SOH_Tracker.
3. Keep ONE readBMSData() as the single source. After each read:
     - if ONLINE  -> pack the 152-byte struct and esp_now_send().
     - if OFFLINE -> the web server serves the JSON; run the local EKF/SOH.
4. In startFallbackAP(): also start the web server and enable the local
   EKF/SOH so the dashboard is useful offline.
5. In stopFallbackAP(): stop (or pause) the web server and resume ESP-NOW
   sending.
6. Decide whether the EKF runs always or only offline. Running it always
   is cheap and means SOC is ready the instant you go offline.

========================================================================
PART 5: MODE-SWITCH PROBLEMS YOU MUST HANDLE
========================================================================
These are real gaps in the current switch logic. Fix them in the merge.

A. RETURNING TO ONLINE IS BROKEN TODAY.
   - The slave only resets consecutiveFailures to 0 on a SUCCESSFUL
     ESP-NOW send. But in AP mode it is not sending. So "failures == 0"
     can never become true while the AP is up, and stopFallbackAP() can
     never fire. The slave would get stuck in AP mode.

   First understand what a send failure actually means:
   - An ESP-NOW send "succeeds" when the master's radio ACKs the frame.
     That only proves the master ESP32 is powered and on the same channel.
     It does NOT prove the master still has internet / MQTT.
   - So the current failure counter only detects "master node gone." If
     the master is alive but lost its uplink, every send still succeeds,
     the slave never falls back, and the website just goes stale.

   RECOMMENDED FIX — MASTER HEARTBEAT (bidirectional ESP-NOW):
   - The master sends a small heartbeat to the slave over ESP-NOW (for
     example every 1 s), carrying its real status (e.g. "MQTT connected"
     or "uplink down"). This needs the master to add the slave as a peer
     and the slave to register a receive callback. Today ESP-NOW is one
     way (slave -> master) only, so this plumbing must be added.
   - Slave logic:
       online  -> heartbeat seen recently (and status OK) -> keep sending.
       offline -> no heartbeat for N seconds (or status = uplink down)
                  -> start AP + local dashboard/EKF.
       recover -> heartbeat seen again -> drop AP, resume client.
   - Bonus: this is the SAME bidirectional link needed for the cloud ->
     device feedback in Part 8. Build it once, use it for both.

   THE CHANNEL CATCH (must handle either way):
   - ESP-NOW only works when both nodes are on the SAME WiFi channel, and
     a radio can only be on one channel at a time. When the slave hosts
     its own AP, that AP sits on some channel. If it is not the master's
     channel, the slave CANNOT hear the master's heartbeat.
   - So recovery cannot be purely passive while serving a foreign-channel
     AP. The slave must periodically return to the master's channel to
     listen: e.g. every ~10-30 s, briefly pause/leave the AP, hop to the
     master's channel (re-scan if needed), listen for the heartbeat (or
     send a probe). If heard -> tear down AP and resume client. If not ->
     go back to the AP.

   SIMPLER ALTERNATIVE (no master changes) — SLAVE PROBE:
   - If you do not want to touch the master yet, skip the heartbeat and
     have the slave itself probe: periodically leave the AP, re-scan for
     the master AP / re-init ESP-NOW on its channel, and try one send. A
     successful send means the master node is back. Downside: this only
     detects the master NODE, not cloud status, and the AP blinks during
     each probe.

B. WIFI CHANNEL / PEER MUST BE RESTORED.
   - ESP-NOW uses the master's WiFi channel (found by scanForChannel()).
     The fallback AP can be on any channel. When you go back online you
     MUST re-scan the channel and re-add the ESP-NOW peer.
   - The current stopFallbackAP() switches WiFi mode back to STA but does
     NOT re-scan the channel or re-add the peer. Add that.

C. ESP-NOW + SOFTAP AT THE SAME TIME.
   - If you keep ESP-NOW alive while the AP is up, both must be on the
     same channel, which fights with hosting your own AP. In pure
     fallback (master gone) this is fine because ESP-NOW is dead anyway.
     Do not try to do both on different channels.

========================================================================
PART 6: CONFIG DIFFERENCES BETWEEN THE TWO SKETCHES (RECONCILE THESE)
========================================================================
When you merge, pick ONE value for each. Today the sender and the
dashboard disagree:

DA Configuration (register 0x9303):
  - Dashboard (bms_init.h): 0x05
  - Sender   (sender.cpp):  0x04
  -> Different scaling. Pick one and use it.

temp2 field — different physical sensor:
  - Dashboard: temp2 = CFETOFF pin (direct read 0x7C)
  - Sender:    temp2 = HDQ thermistor
  -> Same name, different sensor.

charge field — different units:
  - Dashboard JSON: charge in mAh
  - Sender struct:  charge in Ah (value divided by 1000)
  -> Same name, 1000x apart.

OCC threshold (0x9280):
  - Sender writes 6 A (0x9280 = 3)
  - Appendix A / old guide said 10 A
  -> Confirm the real target.

Current sent to the cloud is NOT raw:
  - Sender applies a 20 mA deadband: currents at or below 20 mA become 0
    before sending. The dashboard EKF uses CC1 (low noise), which is NOT
    what is transmitted. For the cloud EKF you probably want to send a
    cleaner current (smaller or no deadband, or CC1).

========================================================================
PART 7: BUG TO FIX FIRST
========================================================================
config.h, DeviceMessage struct, the pack-voltage field is broken:

    unsigned int v_ ack;     <-- literally "v_ ack" with a space

  - Will not compile as written.
  - sender.cpp and reciever.cpp both use ".v_pack".
  - Fix: rename to  v_pack  (no space).

========================================================================
PART 8: NOT BUILT YET (do these later, after the merge works)
========================================================================
1. CLOUD -> DEVICE FEEDBACK (corrected SOC/SOH back to the slave).
   - reciever.cpp has NO MQTT subscribe, NO bms/feedback topic, and NO
     ESP-NOW send back to the slave. ESP-NOW is one-way (slave -> master).
   - The struct has no field for corrected values.
   - To build: master subscribes to bms/feedback/{pairing_code}; add a
     small slave-ward ESP-NOW message; add a receive callback on the
     slave; add fields to store the corrected SOC/SOH.
   - The upstream half already works: the slave puts "BMS:XXXXXX" (last 3
     MAC bytes) in the message field, and the master extracts it as
     pairingCode in the published JSON. So the key for the feedback topic
     already exists.

2. OFFLINE FLASH BUFFERING (store readings while offline, upload later).
   - There is NO SPIFFS or LittleFS code anywhere in the repo. NVS is used
     only for balancing settings and the SOH cycle count.
   - The "16 KB NVS / 4 MB SPIFFS" math in the old guide describes a
     feature that is not implemented. Build it only if you need history
     to survive an outage.

========================================================================
PART 9: MQTT TOPICS (current reality)
========================================================================
FROM device (works today):
  Topic:   bms/data
  Payload: JSON built by the master from the 152-byte struct (~15 fields,
           includes "pairingCode").

TO device (NOT built — see Part 8):
  Topic:   bms/feedback/{pairing_code}
  Payload (planned): { "soc_corrected": 46.1, "soh_corrected": 98.5 }

========================================================================
PART 10: WHAT ALREADY WORKS (you can rely on these)
========================================================================
- 13S cell mapping is consistent everywhere:
  CELL_TO_BQ = {1..12, 16}, VCELL mask 0x8FFF (cells 1-12 + cell 16).
- The dashboard EKF: input CC1 current, measurement cell 1 voltage,
  1 second update, 1.0 second sample time, OCV + parasitic-R start value.
- Upstream pairing code path (slave packs it, master extracts it).
- Master networking: WIFI_AP_STA, creates AP "WBMS-Node" so the slave can
  find the WiFi channel, publishes to bms/data.

# ========================================================================

                        APPENDICES — CODE REFERENCE

# ========================================================================

========================================================================
APPENDIX A: BQ76952 REGISTER CONFIGURATION (dashboard bms_init.h)
========================================================================
All writes happen in CONFIG_UPDATE mode during setup().
Timing: 50ms between writes, 200ms after exit, 500ms after reset.
NOTE: the sender writes a similar but NOT identical set. Differences are
in Part 6.

--- Power & Cell Routing ---
0x9234 = Power Config (LOOP_SLOW=3, SLEEP bit)
0x9304 = 0x8FFF (VCell Mode: cells 1-12 + cell 16 = 13S)

--- Thermistors ---
0x92FD = 0x07 (TS1: NTC10K)
0x92FF = 0x07 (TS3: NTC10K)
0x9300 = 0x07 (HDQ: NTC10K)
0x92FA = 0x07 (CFETOFF: NTC10K)
0x92FE = 0x00 (TS2: Disabled)

--- Protection Thresholds ---
0x9275/76  CUV
0x9278/79  COV
0x9280/81  OCC   (sender writes 6 A here; see Part 6)
0x9282/83  OCD1
0x9284/85  OCD2
0x9286/87  SCD

--- Enabled Protections ---
0x9261 = 0xFC (CUV, COV, OCC, OCD1, OCD2, SCD on)
0x9262 = 0x00 (temperature protections off)
0x9263 = 0x00 (open wire off)

--- FET & Manufacturing ---
0x9308 = 0x0D (FET Options: Autonomous CHG/DSG/PCHG)
0x9343 = 0x0050 (Mfg Init: FET_EN + PF_EN)

--- DA Configuration ---
0x9303 = 0x05 on the dashboard, 0x04 on the sender (see Part 6)
0x9307 = 50 (CC3 filter: 50 samples)

========================================================================
APPENDIX B: DATA COLLECTION REGISTERS (read every ~500 ms)
========================================================================
0x12        Battery Status (bit15 = SLEEP, bits0-3 = FET states)
VC1..VC16   Cell voltages (mV, glitch filtered: 500 < V < 5000)
0x32        Stack voltage (mV)
0x34        Pack voltage (mV)
0x0075      DASTATUS5: CC3 (smoothed current) + CC1 (raw), 1 mA units
0x0076      DASTATUS6: accumulated charge + accumulated time
TS1 / TS3   External thermistors (deg C)
0x7C        CFETOFF temperature (raw/10 - 273.15)
HDQ         HDQ thermistor (deg C)
0x7F        FET status (bit0=CHG, bit1=PCHG, bit2=DSG, bit3=PDSG)
0x02/04/06  Safety Alert A/B/C
0x03/05/07  Safety Status A/B/C
0x0B,0x0D   Permanent failure status
0x00        Control status (bit12 = LD_TIMEOUT)
0x38        LD pin voltage (10 mV units)
0x62        Alarm status (bit13 = FULLSCAN done)

Current pipeline (dashboard): CC3 -> filter (alpha 0.8) -> 2 mA deadband
  -> display. EKF uses CC1, not the display current.
Current pipeline (sender): CC3 -> 20 mA deadband -> sent over ESP-NOW.

========================================================================
APPENDIX C: AP DASHBOARD JSON FIELDS (/api/data) — LOCAL ONLY
========================================================================
Full ~60-field object served on the local AP only (Mode 2). It is NOT
what the cloud receives in Mode 1 (the cloud gets the ~15-field master
JSON from the 152-byte struct; see Part 3).

{
"v":[v1..v16], "vStack", "vPack", "current", "charge", "chargeTime",
"chipTemp", "temp1", "temp2", "temp3", "isCharging", "isDischarging",
"autoSleep", "fetEn", "vLd", "ldWait", "safA/B/C", "f_pchg", "f_pdsg",
"prot_sc/oc2/oc1/occ/ov/uv", "temp_otf/oti/otd/otc/uti/utd/utc",
"cc_soc", "soc_ekf", "soh", "soc_uncertainty", "vErr", "vrc1/vrc2/vrc3",
"timeRemain", "saA/B/C", "ssA/B/C", "pfA/B/C/D", "batStat",
"hwBalActive", "balMode", "bal", "balTrig", "balDelta", "balTime",
"cellDelta", "minV/maxV", "p_i2c/p_ekf/p_web", "cellBalTimes[16]",
"manStat", "pwr", "txCount", "wdFault", "cfg_cb", "cfg_protA/B"
}

========================================================================
APPENDIX D: FILE REFERENCE
========================================================================
ESP-NOW path (Sketch A):
  src/slaves/sender.cpp    Client send + heartbeat + fallback AP switch
  src/master/reciever.cpp  Receives ESP-NOW, publishes to MQTT bms/data
  src/shared/config.h      DeviceMessage struct, MACs, MQTT, WiFi config
  src/shared/BQ76952.cpp/h Shared BQ76952 driver

AP dashboard (Sketch B):
  src/tb_v2_on_mainboard/tb_config.h        Pins, topology, AP SSID/pass
  src/tb_v2_on_mainboard/bms_init.h         BQ76952 config sequence
  src/tb_v2_on_mainboard/New_Dashboard.cpp  Data collection, EKF, SOH, AP
  src/tb_v2_on_mainboard/web_api.h          Dashboard HTML + JSON API
  src/tb_v2_on_mainboard/SOH_Tracker.h      SOH model

Merge target: ONE firmware = Sketch A's mode switch + Sketch B's web
server, EKF, and SOH.
