#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stddef.h>

// ==================== NETWORK CONFIG ====================
// WiFi credentials are managed by WiFiManager (captive portal) on the master.
// No hardcoded SSID/password needed — stored in NVS after first-time setup.

const char *const MASTER_AP_SSID = "WBMS-Node"; // Soft AP the slave scans to discover the master's WiFi channel
const int WIFI_RESET_PIN = 0;                    // BOOT button on ESP32 DevKit V1 — hold on startup to reset WiFi

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
const char *const MQTT_CMD_TOPIC_PREFIX = "bms/cmd/";

// ==================== OTA CONFIG ====================
const char *const FW_VERSION = "0.1.1";
const float MIN_SOC_FOR_OTA = 30.0f;

// ==================== HARDWARE CONFIG ====================
const int I2C_SDA_PIN = 21; // Standard I2C SDA on ESP32 DevKit V1
const int I2C_SCL_PIN = 22; // Standard I2C SCL on ESP32 DevKit V1

// ==================== BMS CONFIG ====================
#define CONNECTED_CELLS 3

const uint8_t RECEIVER_ADDRESS[] = {0x3C, 0x8A, 0x1F, 0x0C, 0xD5, 0x28};

const uint8_t SENDER_ADDRESSES[][6] = {
    {0x08, 0xD1, 0xF9, 0x27, 0xAF, 0x28},
};
const int NUM_SENDERS = sizeof(SENDER_ADDRESSES) / sizeof(SENDER_ADDRESSES[0]);

// ==================== SLAVE CONFIG ====================
const int FALLBACK_CHANNEL = 6;
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