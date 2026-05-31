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
};
const int NUM_SENDERS = sizeof(SENDER_ADDRESSES) / sizeof(SENDER_ADDRESSES[0]);

// ==================== SLAVE CONFIG ====================
// The offline AP SSID is built at runtime as "wBMS-Slave-<pairingCode>" and
// uses MB_AP_PASSWORD (tb_config.h); there is no fixed fallback SSID constant.
const int FALLBACK_CHANNEL = 1;   // used only until the master's channel is learned
const int FAILURE_THRESHOLD = 10; // consecutive ESP-NOW send failures before fallback AP starts
const uint32_t SEND_INTERVAL_MS = 500;

// ==================== MASTER CONFIG ====================
#define QUEUE_SIZE 10
const float EKF_SAMPLE_TIME = 0.5f; // Must match SEND_INTERVAL_MS / 1000

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

#endif