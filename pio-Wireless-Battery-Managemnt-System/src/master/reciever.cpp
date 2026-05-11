#include <esp_now.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <esp_wifi.h>
#include <esp_https_ota.h>
#include <esp_ota_ops.h>
#include <esp_crt_bundle.h>
#include <string.h>
#include "config.h"
#include "BatteryEKF.h"

// ==================== QUEUE TYPES ====================
typedef struct
{
  DeviceMessage data;
  int senderIndex;
} QueuedMessage;

// ==================== QUEUE SYSTEM ====================
QueuedMessage messageQueue[QUEUE_SIZE];
volatile int queueHead = 0;
volatile int queueTail = 0;
volatile int queueCount = 0;
portMUX_TYPE queueMux = portMUX_INITIALIZER_UNLOCKED;

bool enqueueMessage(const DeviceMessage &data, int senderIndex)
{
  portENTER_CRITICAL(&queueMux);
  if (queueCount >= QUEUE_SIZE)
  {
    portEXIT_CRITICAL(&queueMux);
    Serial.println("[Queue] ⚠️ Queue full, dropping message");
    return false;
  }
  messageQueue[queueHead].data = data;
  messageQueue[queueHead].senderIndex = senderIndex;
  queueHead = (queueHead + 1) % QUEUE_SIZE;
  queueCount++;
  portEXIT_CRITICAL(&queueMux);
  return true;
}

bool dequeueMessage(QueuedMessage &msg)
{
  portENTER_CRITICAL(&queueMux);
  if (queueCount == 0)
  {
    portEXIT_CRITICAL(&queueMux);
    return false;
  }
  msg = messageQueue[queueTail];
  queueTail = (queueTail + 1) % QUEUE_SIZE;
  queueCount--;
  portEXIT_CRITICAL(&queueMux);
  return true;
}

// ==================== EKF (one per sender) ====================
BatteryEKF ekf[NUM_SENDERS] = { BatteryEKF(EKF_SAMPLE_TIME) };
bool ekfInitialized[NUM_SENDERS] = { false };

// ==================== STATE MANAGEMENT ====================
unsigned long lastWiFiCheck = 0;
unsigned long lastMqttReconnect = 0;
uint8_t currentChannel = 0;

// ==================== MQTT CLIENT ====================
WiFiClient espClient;
PubSubClient mqtt(espClient);

// ==================== OTA STATE ====================
// ISRG Root X1 — Let's Encrypt's root CA. Embedded directly because the
// Arduino-ESP32 crt_bundle didn't link into this build (runtime
// "Failed to attach bundle"). This single root covers wbms.systems and
// every other LE-issued cert.
static const char ISRG_ROOT_X1_PEM[] =
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "-----END CERTIFICATE-----\n";

// Master's pairing code = last 3 bytes of its STA MAC, formatted as hex
// (mirrors the slave's derivation in sender.cpp). Used as the suffix of the
// MQTT command topic the master subscribes to.
char masterPairingCode[7] = "000000";
char mqttCmdTopic[40] = "";

// Per-sender health snapshot used to decide if it's safe to OTA. Updated
// each time a message is dequeued. `lastSoc < 0` means we haven't observed
// this sender yet — treated as "not safe to OTA".
typedef struct
{
  float lastSoc;
  bool faultActive;
  bool hasData;
} SenderState;
SenderState senderState[NUM_SENDERS];

// One-shot: after a successful OTA, the new firmware boots into a "pending
// verify" state. We mark it valid once both WiFi and MQTT are confirmed up,
// proving the new image at least gets us back online.
bool otaMarkValidPending = true;

// ==================== HELPERS ====================
String macToString(const uint8_t *mac)
{
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

// ==================== WIFI FUNCTIONS ====================
WiFiManager wifiManager;

bool initWiFi()
{
  // Hold BOOT button during startup to clear saved WiFi and re-enter setup portal
  pinMode(WIFI_RESET_PIN, INPUT_PULLUP);
  delay(100);
  if (digitalRead(WIFI_RESET_PIN) == LOW)
  {
    Serial.println("[WiFi] 🔄 Reset button held — clearing saved credentials");
    wifiManager.resetSettings();
  }

  WiFi.mode(WIFI_AP_STA);
  wifiManager.setConfigPortalTimeout(180); // Portal stays open 3 minutes then gives up
  wifiManager.setConnectTimeout(WIFI_TIMEOUT_MS / 1000);

  Serial.println("[WiFi] Starting WiFi provisioning...");
  Serial.println("[WiFi] If no saved network — connect your phone to 'WBMS-Setup' to configure");
  bool connected = wifiManager.autoConnect("WBMS-Setup");

  // Ensure AP_STA mode for ESP-NOW after WiFiManager may have changed it
  WiFi.mode(WIFI_AP_STA);

  if (connected)
  {
    currentChannel = WiFi.channel();
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    // Start discovery AP so slaves can find our channel
    WiFi.softAP(MASTER_AP_SSID, NULL, currentChannel);
    Serial.println("[WiFi] ✅ Connected!");
    Serial.printf("[WiFi] IP: %s, Channel: %d\n", WiFi.localIP().toString().c_str(), currentChannel);
    Serial.printf("[WiFi] Discovery AP '%s' active on channel %d\n", MASTER_AP_SSID, currentChannel);
    return true;
  }

  Serial.println("[WiFi] ❌ Portal timed out — running in ESP-NOW only mode");
  // Still start discovery AP on fallback channel so slaves can sync
  currentChannel = 6;
  WiFi.softAP(MASTER_AP_SSID, NULL, currentChannel);
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
  Serial.printf("[WiFi] Discovery AP '%s' on fallback channel %d\n", MASTER_AP_SSID, currentChannel);
  return false;
}

void updateESPNowChannel(uint8_t newChannel)
{
  if (newChannel == currentChannel)
    return;
  Serial.printf("[WiFi] Channel changed: %d -> %d, updating ESP-NOW peers\n", currentChannel, newChannel);
  currentChannel = newChannel;
  esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
  for (int i = 0; i < NUM_SENDERS; i++)
  {
    esp_now_del_peer(SENDER_ADDRESSES[i]);
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, SENDER_ADDRESSES[i], 6);
    peerInfo.channel = currentChannel;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
  }
}

void maintainWiFi()
{
  if (millis() - lastWiFiCheck >= WIFI_RETRY_INTERVAL_MS)
  {
    lastWiFiCheck = millis();
    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.println("[WiFi] Reconnecting...");
      WiFi.reconnect(); // Uses credentials saved in NVS by WiFiManager
    }
    else
    {
      uint8_t newChannel = WiFi.channel();
      if (newChannel != currentChannel)
      {
        updateESPNowChannel(newChannel);
        // Keep discovery AP in sync with the new channel
        WiFi.softAP(MASTER_AP_SSID, NULL, newChannel);
        Serial.printf("[WiFi] Discovery AP moved to channel %d\n", newChannel);
      }
    }
  }
}

// ==================== OTA HELPERS ====================
// Cheap manual JSON string-field extractor: looks for `"key":"<value>"` in
// `json` and copies the value into `out`. Returns false if the key is
// missing or the value won't fit. Good enough for the small, master-only
// command payloads — we don't want to pull ArduinoJson in for this.
bool extractJsonString(const char *json, const char *key, char *out, size_t outSize)
{
  if (!json || !key || !out || outSize == 0) return false;

  char needle[32];
  int needleLen = snprintf(needle, sizeof(needle), "\"%s\"", key);
  if (needleLen <= 0 || needleLen >= (int)sizeof(needle)) return false;

  const char *p = strstr(json, needle);
  if (!p) return false;
  p += needleLen;

  while (*p == ' ' || *p == '\t') p++;
  if (*p != ':') return false;
  p++;
  while (*p == ' ' || *p == '\t') p++;
  if (*p != '"') return false;
  p++;

  size_t i = 0;
  while (*p && *p != '"' && i + 1 < outSize)
  {
    out[i++] = *p++;
  }
  if (*p != '"') return false;
  out[i] = '\0';
  return true;
}

// "Fault active" proxy derived from telemetry we already have on the wire.
// The BQ76952 getProtectionStatus() isn't part of DeviceMessage (V1 keeps the
// wire contract frozen), so this is a conservative guess: any cell outside
// [2.5, 4.25] V or any temperature above the limits blocks OTA.
bool deriveFaultFromMessage(const DeviceMessage &data)
{
  for (int i = 0; i < 16; i++)
  {
    unsigned int mv = data.v[i];
    if (mv == 0) continue; // disconnected channel
    if (mv < 2500 || mv > 4250) return true;
  }
  if (data.temp1 > 60.0f || data.temp2 > 60.0f || data.temp3 > 60.0f) return true;
  if (data.chip_temp > 75.0f) return true;
  return false;
}

// Walk every sender's snapshot and decide whether OTA is safe right now.
// Writes a human-readable reason on refusal for the serial log.
bool senderFleetSafeForOta(char *reason, size_t reasonSize)
{
#ifdef OTA_SKIP_SAFETY_CHECKS
  snprintf(reason, reasonSize, "safety checks disabled at build time");
  Serial.println("[OTA] ⚠ OTA_SKIP_SAFETY_CHECKS active — bypassing prechecks");
  return true;
#else
  for (int i = 0; i < NUM_SENDERS; i++)
  {
    if (!senderState[i].hasData)
    {
      snprintf(reason, reasonSize, "Sender %d has no telemetry yet", i + 1);
      return false;
    }
    if (senderState[i].lastSoc < MIN_SOC_FOR_OTA)
    {
      snprintf(reason, reasonSize, "Sender %d SoC=%.1f%% < %.1f%%",
               i + 1, senderState[i].lastSoc, MIN_SOC_FOR_OTA);
      return false;
    }
    if (senderState[i].faultActive)
    {
      snprintf(reason, reasonSize, "Sender %d has active protection fault", i + 1);
      return false;
    }
  }
  return true;
#endif
}

void performOta(const char *url, const char *version, const char *sha256)
{
  Serial.printf("[OTA] Starting upgrade to v%s\n", version);
  Serial.printf("[OTA]   URL:    %s\n", url);
  Serial.printf("[OTA]   SHA256: %s\n", (sha256 && *sha256) ? sha256 : "(none provided)");

  esp_http_client_config_t httpConfig = {};
  httpConfig.url = url;
  httpConfig.timeout_ms = 30000;
  httpConfig.keep_alive_enable = true;
  httpConfig.cert_pem = ISRG_ROOT_X1_PEM;

  esp_err_t err = esp_https_ota(&httpConfig);
  if (err == ESP_OK)
  {
    Serial.println("[OTA] ✅ Image installed, rebooting...");
    delay(500);
    esp_restart();
  }
  else
  {
    Serial.printf("[OTA] ❌ Upgrade failed: %s (0x%x)\n", esp_err_to_name(err), err);
  }
}

void handleOtaCommand(const char *json)
{
  char op[16] = {0};
  if (!extractJsonString(json, "op", op, sizeof(op)) || strcmp(op, "ota") != 0)
  {
    Serial.printf("[OTA] Ignoring command, op=%s\n", op);
    return;
  }

  char url[256] = {0};
  char version[24] = {0};
  char sha256[80] = {0};
  if (!extractJsonString(json, "url", url, sizeof(url)) ||
      !extractJsonString(json, "version", version, sizeof(version)))
  {
    Serial.println("[OTA] ❌ Command missing url or version");
    return;
  }
  extractJsonString(json, "sha256", sha256, sizeof(sha256)); // optional

  if (strcmp(version, FW_VERSION) == 0)
  {
    Serial.printf("[OTA] Refused: already on v%s\n", FW_VERSION);
    return;
  }

  char reason[80];
  if (!senderFleetSafeForOta(reason, sizeof(reason)))
  {
    Serial.printf("[OTA] Refused: %s\n", reason);
    return;
  }

  performOta(url, version, sha256);
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  // Copy payload to a local null-terminated buffer so strstr/strcmp work.
  char buf[512];
  if (length >= sizeof(buf))
  {
    Serial.printf("[MQTT] Command on %s too large (%u bytes), dropping\n", topic, length);
    return;
  }
  memcpy(buf, payload, length);
  buf[length] = '\0';

  Serial.printf("[MQTT] 📨 Command on %s: %s\n", topic, buf);

  if (strcmp(topic, mqttCmdTopic) == 0)
  {
    handleOtaCommand(buf);
  }
}

// ==================== MQTT FUNCTIONS ====================
void connectMqtt()
{
  if (WiFi.status() != WL_CONNECTED)
    return;
  if (mqtt.connected())
    return;
  if (millis() - lastMqttReconnect < MQTT_RECONNECT_INTERVAL_MS)
    return;

  lastMqttReconnect = millis();
  Serial.printf("[MQTT] Connecting to %s:%d...\n", MQTT_BROKER, MQTT_PORT);

  if (mqtt.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD))
  {
    Serial.println("[MQTT] ✅ Connected!");
    if (mqtt.subscribe(mqttCmdTopic))
    {
      Serial.printf("[MQTT] 🔔 Subscribed to %s\n", mqttCmdTopic);
    }
    else
    {
      Serial.printf("[MQTT] ✗ Subscribe to %s failed\n", mqttCmdTopic);
    }
  }
  else
  {
    Serial.printf("[MQTT] ✗ Failed, rc=%d\n", mqtt.state());
  }
}

String buildJsonPayload(int senderIndex, const DeviceMessage &data, float soc)
{
  // Extract pairing code from message field (format: "BMS:XXXXXX")
  String pairingCode = "unknown";
  String msg = String(data.message);
  if (msg.startsWith("BMS:") && msg.length() >= 10)
  {
    pairingCode = msg.substring(4);
  }

  String json = "{";
  json += "\"senderIndex\":" + String(senderIndex + 1) + ",";
  json += "\"connectedCells\":" + String(CONNECTED_CELLS) + ",";
  json += "\"pairingCode\":\"" + pairingCode + "\",";
  json += "\"masterPairingCode\":\"" + String(masterPairingCode) + "\",";
  json += "\"fwVersion\":\"" + String(FW_VERSION) + "\",";

  for (int i = 0; i < 16; i++)
  {
    if (data.v[i] > 0)
    {
      json += "\"v" + String(i + 1) + "\":" + String(data.v[i]) + ",";
    }
  }

  json += "\"vStack\":" + String(data.v_stack) + ",";
  json += "\"vPack\":" + String(data.v_pack) + ",";
  json += "\"current\":" + String(data.current) + ",";
  json += "\"charge\":" + String(data.charge) + ",";
  json += "\"chargeTime\":" + String(data.charge_time) + ",";
  json += "\"chipTemp\":" + String(data.chip_temp) + ",";
  json += "\"temp1\":" + String(data.temp1) + ",";
  json += "\"temp2\":" + String(data.temp2) + ",";
  json += "\"temp3\":" + String(data.temp3) + ",";
  json += "\"isCharging\":" + String(data.isCharging ? "true" : "false") + ",";
  json += "\"isDischarging\":" + String(data.isDischarging ? "true" : "false") + ",";
  json += "\"soc\":" + String(soc, 1) + ",";
  json += "\"message\":\"" + msg + "\"";
  json += "}";
  return json;
}

bool publishToMqtt(int senderIndex, const DeviceMessage &data, float soc)
{
  if (!mqtt.connected())
    return false;

  String payload = buildJsonPayload(senderIndex, data, soc);
  bool ok = mqtt.publish(MQTT_TOPIC, payload.c_str());

  if (ok)
  {
    Serial.printf("[MQTT] ✓ Published to %s (%d bytes)\n", MQTT_TOPIC, payload.length());
  }
  else
  {
    Serial.println("[MQTT] ✗ Publish failed");
  }
  return ok;
}

// ==================== ESP-NOW CALLBACK ====================
int findSenderIndex(const uint8_t *macAddr)
{
  for (int i = 0; i < NUM_SENDERS; i++)
  {
    if (memcmp(macAddr, SENDER_ADDRESSES[i], 6) == 0)
      return i;
  }
  return -1;
}

void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len)
{
  if (len != sizeof(DeviceMessage))
  {
    Serial.printf("[ESP-NOW] ❌ Size mismatch: %d bytes\n", len);
    return;
  }

  int senderIndex = findSenderIndex(mac_addr);
  if (senderIndex == -1)
  {
    Serial.printf("[ESP-NOW] ⚠️ Unknown sender MAC: %s\n", macToString(mac_addr).c_str());
    return;
  }

  DeviceMessage tempData;
  memcpy(&tempData, incomingData, sizeof(tempData));
  tempData.message[sizeof(tempData.message) - 1] = '\0';

  Serial.printf("[ESP-NOW] 📥 Received %d bytes from %s (Sender %d)\n",
                len, macToString(mac_addr).c_str(), senderIndex + 1);
  Serial.printf("[ESP-NOW]   Cells: ");
  for (int i = 0; i < CONNECTED_CELLS; i++)
  {
    Serial.printf("V%d=%umV ", i + 1, tempData.v[i]);
  }
  Serial.println();
  Serial.printf("[ESP-NOW]   Stack=%umV Pack=%umV Current=%dmA\n",
                tempData.v_stack, tempData.v_pack, tempData.current);
  Serial.printf("[ESP-NOW]   Temps: chip=%.1fC t1=%.1fC t2=%.1fC t3=%.1fC\n",
                tempData.chip_temp, tempData.temp1, tempData.temp2, tempData.temp3);
  Serial.printf("[ESP-NOW]   Charge=%.1fAh Time=%us Charging=%d Discharging=%d\n",
                tempData.charge, tempData.charge_time, tempData.isCharging, tempData.isDischarging);
  Serial.printf("[ESP-NOW]   Message: %s\n", tempData.message);

  if (enqueueMessage(tempData, senderIndex))
  {
    Serial.printf("[ESP-NOW] ✅ Queued data from Sender %d\n", senderIndex + 1);
  }
}

// ==================== SETUP & LOOP ====================
void setup()
{
  Serial.begin(115200);
  delay(1000);

  // Per-sender state: -1 SoC = unknown, no fault, no data yet.
  for (int i = 0; i < NUM_SENDERS; i++)
  {
    senderState[i].lastSoc = -1.0f;
    senderState[i].faultActive = false;
    senderState[i].hasData = false;
  }

  // Derive master pairing code from STA MAC last 3 bytes — matches the
  // slave's scheme so the backend can pair masters and slaves by code.
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(masterPairingCode, sizeof(masterPairingCode), "%02X%02X%02X",
           mac[3], mac[4], mac[5]);
  snprintf(mqttCmdTopic, sizeof(mqttCmdTopic), "%s%s",
           MQTT_CMD_TOPIC_PREFIX, masterPairingCode);
  Serial.printf("[System] FW v%s, masterPairingCode=%s\n", FW_VERSION, masterPairingCode);
  Serial.printf("[System] MQTT command topic: %s\n", mqttCmdTopic);

  if (!initWiFi())
  {
    Serial.println("[System] Running in ESP-NOW only mode");
  }

  // ── MQTT setup ──
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setBufferSize(512);
  mqtt.setCallback(mqttCallback);
  connectMqtt();

  // ── ESP-NOW setup ──
  if (esp_now_init() != ESP_OK)
  {
    Serial.println("[ESP-NOW] ❌ Init Failed");
    ESP.restart();
  }

  esp_now_register_recv_cb(OnDataRecv);

  for (int i = 0; i < NUM_SENDERS; i++)
  {
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, SENDER_ADDRESSES[i], 6);
    peerInfo.channel = currentChannel;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
  }

  Serial.println("[System] ✅ Master ready (MQTT + ESP-NOW)");
}

void loop()
{
  maintainWiFi();
  connectMqtt();
  mqtt.loop();

  // One-shot: cancel pending rollback once the new image proves it can get
  // both WiFi and MQTT up. Skipped silently on the first boot of a flashed
  // (non-OTA) image because esp_ota_mark_app_valid_cancel_rollback() is a
  // no-op outside the PENDING_VERIFY state.
  if (otaMarkValidPending && WiFi.status() == WL_CONNECTED && mqtt.connected())
  {
    esp_err_t markErr = esp_ota_mark_app_valid_cancel_rollback();
    if (markErr == ESP_OK)
    {
      Serial.println("[OTA] ✅ Marked running image valid, rollback cancelled");
    }
    otaMarkValidPending = false;
  }

  QueuedMessage qMsg;
  if (dequeueMessage(qMsg))
  {
    Serial.println("[Queue] Processing BMS data...");

    int idx = qMsg.senderIndex;
    if (idx >= 0 && idx < NUM_SENDERS)
    {
      // Use cell 1 voltage for EKF (matches testboard updateEKF pattern)
      float cellV = (float)qMsg.data.v[0] / 1000.0f;  // mV -> V

      float currentA = qMsg.data.current / 1000.0f;  // mA -> A

      if (!ekfInitialized[idx])
      {
        // Bootstrap SoC from OCV (assumes pack is roughly at rest on first reading)
        float initSoc = ekf[idx].invertOCV_Discharge(cellV);
        ekf[idx].begin(initSoc);
        ekfInitialized[idx] = true;
        Serial.printf("[EKF] Sender %d initialized at SoC=%.1f%%\n", idx + 1, initSoc);
      }

      ekf[idx].update(currentA, cellV);
      float soc = ekf[idx].getSOC();
      Serial.printf("[EKF] Sender %d: SoC=%.1f%% (V_err=%.1fmV)\n",
                    idx + 1, soc, ekf[idx].getVoltageError() * 1000.0f);

      senderState[idx].lastSoc = soc;
      senderState[idx].faultActive = deriveFaultFromMessage(qMsg.data);
      senderState[idx].hasData = true;

      publishToMqtt(idx, qMsg.data, soc);
    }
  }
  delay(10);
}