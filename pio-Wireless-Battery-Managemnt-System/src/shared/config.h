#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stddef.h>

// ==================== NETWORK CONFIG ====================
// The master's home-WiFi credentials are NOT hardcoded — they are entered once
// through the WiFiManager captive portal and saved in NVS (see reciever.cpp).
const char *const MASTER_AP_SSID = "WBMS-Node"; // Soft AP the slave scans to discover the master's WiFi channel
const uint32_t WIFI_RETRY_INTERVAL_MS = 10000;  // master background reconnect check cadence

// ==================== MQTT CONFIG ====================
const char *const MQTT_BROKER = "143.198.121.33";
const uint16_t MQTT_PORT = 1883;
const char *const MQTT_TOPIC = "bms/data";
const char *const MQTT_CLIENT_ID = "wbms-master";
const char *const MQTT_USERNAME = "wbms-master";
const char *const MQTT_PASSWORD = "mito1234";
const uint32_t MQTT_RECONNECT_INTERVAL_MS = 5000;
// Master subscribes to "<prefix><masterPairingCode>" for cloud->device commands (e.g. OTA).
const char *const MQTT_CMD_TOPIC_PREFIX = "bms/cmd/";

// ==================== OTA CONFIG (master only) ====================
const char *const FW_VERSION = "0.1.0";   // bump on every released master image
const float MIN_SOC_FOR_OTA = 30.0f;      // refuse OTA below this pack SoC
// Bench escape hatch: uncomment to skip the SoC/fault/has-data prechecks so you
// can OTA a master with no slave attached. MUST stay commented for any image
// used with real cells.
// #define OTA_SKIP_SAFETY_CHECKS

// ==================== BMS CONFIG ====================
// NOTE: the slave's I2C pins live in tb_config.h (MB_I2C_SDA/MB_I2C_SCL).
// 13S3P pack: cells 1..12 wired to VC1..VC12, cell 13 wired to VC16
// (VC13..VC15 are shorted to VC12). Parallel groups (3P) do not affect
// per-cell voltage reads — only current/Ah scaling on the consumer side.
#define CONNECTED_CELLS 13

const uint8_t RECEIVER_ADDRESS[] = {0x80, 0xF3, 0xDA, 0x54, 0x69, 0x30};

const uint8_t SENDER_ADDRESSES[][6] = {
    {0x30, 0xED, 0xA0, 0xBB, 0x9A, 0x00},
    {0x30, 0xED, 0xA0, 0xBB, 0x18, 0xB4},
};
const int NUM_SENDERS = sizeof(SENDER_ADDRESSES) / sizeof(SENDER_ADDRESSES[0]);

// ==================== ESP-NOW LINK ENCRYPTION ====================
// AES-128-CCMP on every slave<->master ESP-NOW frame (telemetry, heartbeat,
// admin commands, snapshots). The PMK encrypts the LMK at rest; the LMK is the
// key that actually encrypts the over-the-air frames. The master and EVERY slave
// must share these exact 16-byte keys, so they live here in shared config.
// Treat them like a password: regenerate per deployment and keep this file out
// of any public history.
// NOTE: while the master is connected to WiFi (our case), the radio supports at
// most 6 *encrypted* ESP-NOW peers. That covers a handful of slaves; if the
// fleet ever grows past 6, revisit (e.g. a dedicated AP-less link).
const uint8_t WBMS_ESPNOW_PMK[16] = {
    0x57, 0x42, 0x4D, 0x53, 0x5F, 0x70, 0x6D, 0x6B,
    0x5F, 0x76, 0x31, 0x5F, 0xA3, 0xC1, 0x9E, 0x2D};
const uint8_t WBMS_ESPNOW_LMK[16] = {
    0x57, 0x42, 0x4D, 0x53, 0x5F, 0x6C, 0x6D, 0x6B,
    0x5F, 0x76, 0x31, 0x5F, 0x4B, 0x7F, 0x08, 0xE6};

// ==================== SLAVE CONFIG ====================
// The offline AP SSID is built at runtime as "wBMS-Slave-<pairingCode>" and
// uses MB_AP_PASSWORD (tb_config.h); there is no fixed fallback SSID constant.
const int FALLBACK_CHANNEL = 1;   // used only until the master's channel is learned
const int FAILURE_THRESHOLD = 10; // consecutive ESP-NOW send failures before fallback AP starts
// Cold-start backstop: if the master is never reachable from boot (powered off,
// wrong RECEIVER_ADDRESS, out of range) we might never accumulate ACK *failures*
// either (no TX-done callbacks), so node-down could stall and the AP never opens.
// If we've neither heard a heartbeat NOR had one send ACKed this long after boot,
// raise the offline AP anyway. Keep > boot WiFi scan + a few send attempts.
const uint32_t COLD_START_AP_MS = 15000;
const uint32_t SEND_INTERVAL_MS = 2000;   // 0.5 Hz uplink (lowered from 500ms to stop the master's queue backing up)

// ==================== MASTER CONFIG ====================
#define QUEUE_SIZE 10
const float EKF_SAMPLE_TIME = 2.0f; // Must match SEND_INTERVAL_MS / 1000

// ==================== DATA STRUCTURE ====================
typedef struct
{
  unsigned int v[16];
  unsigned int v_stack;
  unsigned int v_pack;
  int current;
  float chip_temp;
  float temp1;
  float temp2;
  float temp3;
  float charge;
  uint32_t charge_time;
  bool isCharging;
  bool isDischarging;
  char message[50];
  // --- appended fields (reflash BOTH nodes when changing; size is validated) ---
  float soh;     // State of Health (%) from the slave's SOHTracker (sohEngine)
  uint8_t ss_a;  // BQ76952 Safety Status A (0x03), latched: SCD/OCD2/OCD1/OCC/COV/CUV/SFD/OTP
  uint8_t ss_b;  // BQ76952 Safety Status B (0x05), latched: OTF/OTINT/OTD/OTC/UTINT/UTD/UTC
  uint8_t ss_c;  // BQ76952 Safety Status C (0x07), latched: HWDF/PTO/COVL/PCHGOVR/SCDL/OCDL
  // --- admin-console command ack: echo of the last ESP-NOW command applied ---
  uint16_t lastCmdSeq;  // seq of the last SlaveCommand the slave applied (0 = none)
  uint8_t  lastCmdRc;   // CmdRc result of that command
} __attribute__((packed)) DeviceMessage;

// ==================== MASTER -> SLAVE HEARTBEAT ====================
// Bidirectional ESP-NOW: the master beacons its TRUE uplink status to the
// slave(s) so the slave can tell the difference between "master node gone"
// and "master alive but cut off from the cloud" (issues A3). The slave uses
// this both to decide when to fall back to its local AP and to recover.
// MASTER_HB_MAGIC tags the frame so the slave's receive callback never
// confuses a heartbeat with stray traffic.
const uint8_t MASTER_HB_MAGIC = 0xB7; // arbitrary tag byte identifying a heartbeat frame

typedef struct
{
  uint8_t magic;     // == MASTER_HB_MAGIC
  uint8_t uplinkUp;  // 1 = master has WiFi + MQTT to the cloud, 0 = uplink down
  uint8_t channel;   // master's current WiFi channel (so the slave can re-tune cheaply)
  uint32_t seq;      // monotonically increasing, for debugging/loss tracking
} __attribute__((packed)) MasterHeartbeat;

// ==================== HEARTBEAT / FALLBACK TIMING (shared) ====================
const uint32_t HEARTBEAT_INTERVAL_MS    = 1000;  // master beacons every 1 s
const uint32_t HEARTBEAT_TIMEOUT_MS     = 5000;  // online: no heartbeat this long -> suspect master gone
const uint32_t RECOVERY_HOP_INTERVAL_MS = 20000; // offline: how often to hop back and probe the master
const uint32_t RECOVERY_LISTEN_MS       = 1200;  // offline: listen window per hop (kept < AP-client patience)
const int      RECOVERY_GOOD_HOPS       = 2;     // consecutive good hops required before tearing down the AP (hysteresis)
const int      RESCAN_AFTER_FAILED_HOPS = 2;     // offline: after this many failed hops on the cached channel, rescan the master's SSID for its (possibly new) channel — recovers from a router channel reassignment

// ==================== ADMIN COMMAND / SNAPSHOT PROTOCOL (cloud <-> slave) ====================
// Lets the website (admin only) drive the slave's BMS config remotely, mirroring its
// local AP dashboard. Cloud -> master (MQTT bms/cmd/<masterPairingCode>) -> slave (ESP-NOW).
// New ESP-NOW frames are discriminated from the heartbeat (0xB7) by length + magic byte.
const uint8_t SLAVE_CMD_MAGIC      = 0xB8;  // master -> slave command frame
const uint8_t ADMIN_SNAPSHOT_MAGIC = 0xB9;  // slave -> master full read-only snapshot

// Op codes map 1:1 to the slave's handleApiCmd action strings (stable numeric contract).
enum SlaveCmdOp : uint8_t {
  OP_NONE = 0,
  OP_CHG_ON, OP_CHG_OFF, OP_DSG_ON, OP_DSG_OFF,
  OP_ALL_FETS_ON, OP_ALL_FETS_OFF,
  OP_CHG_TOG, OP_DSG_TOG, OP_PCHG_TOG, OP_PDSG_TOG,
  OP_FET_MASTER_TOGGLE,
  OP_CLEAR_FAULTS, OP_PF_RESET, OP_RESET, OP_RESET_CHARGE,
  OP_TOGGLE_BAL, OP_TOGGLE_BAL_MASTER, OP_TOGGLE_AUTO_SLEEP,
  OP_PWR_DEEP, OP_PWR_WAKE,
  OP_SET_HOST_BAL,     // arg_u16[0]=trigger mV, arg_u16[1]=delta mV
  OP_EKF_RESET,
  OP_SNAPSHOT_REQ,     // request a full read-only snapshot (no args)
  OP_CONFIG_WRITE      // 12 protection thresholds (cfg block below); reboots the slave
};

// Dispatcher result codes; echoed to the cloud as DeviceMessage.lastCmdRc. Values >= 10 are errors.
enum CmdRc : uint8_t {
  RC_OK = 0,
  RC_OK_VETO_CHG = 1,        // chgOn issued but the BQ did not enable the CHG FET
  RC_OK_REBOOT   = 2,        // command applied; slave will reboot shortly
  RC_ERR_PCHG_CONFLICT = 10,
  RC_ERR_PDSG_CONFLICT = 11,
  RC_ERR_CHG_CONFLICT  = 12,
  RC_ERR_DSG_CONFLICT  = 13,
  RC_ERR_DEEP_LD_HIGH  = 14,
  RC_ERR_DEEP_FAULTS   = 15,
  RC_ERR_UNKNOWN       = 20
};

typedef struct {
  uint8_t  magic;        // == SLAVE_CMD_MAGIC
  uint8_t  op;           // SlaveCmdOp
  uint16_t seq;          // echoed back in DeviceMessage.lastCmdSeq
  uint16_t arg_u16[2];   // generic args (setHostBalParams trigger/delta)
  // protection thresholds — only used when op == OP_CONFIG_WRITE
  uint16_t cuv, cuv_d, cov, cov_d, occ, occ_d, ocd1, ocd1_d, ocd2, ocd2_d;
  uint8_t  scd, scd_d;
} __attribute__((packed)) SlaveCommand;

// Full read-only snapshot (slave -> master, on demand). Field names mirror handleApiData
// so the master JSON-serialization and the cloud parser stay in sync.
typedef struct {
  uint8_t  magic;        // == ADMIN_SNAPSHOT_MAGIC
  uint8_t  pcode[3];     // slave MAC last 3 bytes (binary) for routing
  uint16_t reqSeq;       // echoes the SlaveCommand.seq that requested it
  uint16_t v[16];        // cell mV
  uint16_t vStack, vPack;
  int32_t  current;      // mA
  float    charge;       // mAh (matches handleApiData)
  uint32_t chargeTime;
  float    chipTemp, temp1, temp2, temp3;
  float    soc_ekf, cc_soc, soh, soc_uncertainty, vErr;
  float    vrc1, vrc2, vrc3, timeRemain;
  uint16_t saA, saB, saC, ssA, ssB, ssC;
  uint8_t  pfA, pfB, pfC, pfD;
  uint16_t batStat, manStat;
  uint16_t balMask;
  uint8_t  balMode, balSuspended, hwBalActive;
  uint16_t balTrig, balDelta, totalBalTime, cellDelta, minV, maxV;
  uint32_t cellBalTimes[16];
  uint8_t  pwr, pendingPwr;
  uint8_t  isCharging, isDischarging, autoSleep, fetEn, balMaster, wdFault, ledState;
  uint16_t protBits;     // packed prot_* flags
  uint16_t tempBits;     // packed temp_* flags
  uint16_t fetBits;      // packed f_pchg/f_pdsg/ddsg/dchg/ldWait flags
  uint8_t  cfg_cb, cfg_protA, cfg_protB;
  uint8_t  lastCmdRc;
} __attribute__((packed)) AdminSnapshot;

static_assert(sizeof(AdminSnapshot) <= 250,
              "AdminSnapshot exceeds the ESP-NOW 250-byte payload limit — split cellBalTimes into a 2nd frame");

#endif