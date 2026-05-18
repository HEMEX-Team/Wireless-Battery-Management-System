#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stddef.h>

// ==================== NETWORK CONFIG ====================
const char *const WIFI_SSID = "PE_Lab";
const char *const WIFI_PASSWORD = "P0werM0$FET";

const char *const MASTER_AP_SSID = "WBMS-Node"; // Soft AP the slave scans to discover the master's WiFi channel
const int WIFI_RESET_PIN = 0;                    // BOOT button on ESP32 DevKit V1 — unused with hardcoded creds

const uint32_t WIFI_TIMEOUT_MS = 15000;
const uint32_t WIFI_RETRY_INTERVAL_MS = 10000;

// ==================== MQTT CONFIG ====================
const char *const MQTT_BROKER = "143.198.121.33";
const uint16_t MQTT_PORT = 1883;
const char *const MQTT_TOPIC = "bms/data";
const char *const MQTT_CLIENT_ID = "wbms-master";
const char *const MQTT_USERNAME = "wbms-master";
const char *const MQTT_PASSWORD = "mito1234";
const uint32_t MQTT_RECONNECT_INTERVAL_MS = 5000;

// ==================== HARDWARE CONFIG ====================
// Mainboard PCB pinout (ESP32-S3): SDA=12, SCL=13. Same wiring as
// mainboard_dashboard/tb_config.h so the slave sketch and dashboard sketch
// are interchangeable on the same hardware.
const int I2C_SDA_PIN = 12;
const int I2C_SCL_PIN = 13;

// ==================== BMS CONFIG ====================
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
const int FALLBACK_CHANNEL = 1; // PE_Lab router channel — match master's actual WiFi channel
const int FAILURE_THRESHOLD = 10; // Consecutive send failures before fallback AP starts
const char *const FALLBACK_AP_SSID = "WBMS-Slave-Direct";
const char *const FALLBACK_AP_PASS = "wbms1234";
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
} __attribute__((packed)) DeviceMessage;

#endif