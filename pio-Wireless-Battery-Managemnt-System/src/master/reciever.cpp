#include <esp_now.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <esp_wifi.h>
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

// ==================== HELPERS ====================
String macToString(const uint8_t *mac)
{
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

// ==================== WIFI FUNCTIONS ====================
bool initWiFi()
{
  WiFi.mode(WIFI_AP_STA);
  Serial.printf("[WiFi] Connecting to '%s'...\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS)
  {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
  {
    currentChannel = WiFi.channel();
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    WiFi.softAP(MASTER_AP_SSID, NULL, currentChannel);
    Serial.println("[WiFi] ✅ Connected!");
    Serial.printf("[WiFi] IP: %s, Channel: %d\n", WiFi.localIP().toString().c_str(), currentChannel);
    Serial.printf("[WiFi] Discovery AP '%s' active on channel %d\n", MASTER_AP_SSID, currentChannel);
    return true;
  }

  Serial.printf("[WiFi] ❌ Could not connect to '%s' — running in ESP-NOW only mode\n", WIFI_SSID);
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

  DeviceMessage tempData;
  memcpy(&tempData, incomingData, sizeof(tempData));
  tempData.message[sizeof(tempData.message) - 1] = '\0';

  int senderIndex = findSenderIndex(mac_addr);
  String senderMac = macToString(mac_addr);

  Serial.printf("[ESP-NOW] 📥 Received %d bytes from %s (Sender %d)\n", len, senderMac.c_str(), senderIndex + 1);
  Serial.printf("[ESP-NOW]   Cells: ");
  for (int i = 0; i < CONNECTED_CELLS; i++)
  {
    Serial.printf("V%d=%umV ", i + 1, tempData.v[i]);
  }
  Serial.println();
  Serial.printf("[ESP-NOW]   Stack=%umV Pack=%umV Current=%dmA\n", tempData.v_stack, tempData.v_pack, tempData.current);
  Serial.printf("[ESP-NOW]   Temps: chip=%.1fC t1=%.1fC t2=%.1fC t3=%.1fC\n", tempData.chip_temp, tempData.temp1, tempData.temp2, tempData.temp3);
  Serial.printf("[ESP-NOW]   Charge=%.1fAh Time=%us Charging=%d Discharging=%d\n", tempData.charge, tempData.charge_time, tempData.isCharging, tempData.isDischarging);
  Serial.printf("[ESP-NOW]   Message: %s\n", tempData.message);

  if (senderIndex != -1)
  {
    if (enqueueMessage(tempData, senderIndex))
    {
      Serial.printf("[ESP-NOW] ✅ Queued data from Sender %d\n", senderIndex + 1);
    }
  }
  else
  {
    Serial.printf("[ESP-NOW] ⚠️ Unknown sender MAC: %s\n", senderMac.c_str());
  }
}

// ==================== SETUP & LOOP ====================
void setup()
{
  Serial.begin(115200);
  delay(1000);

  if (!initWiFi())
  {
    Serial.println("[System] Running in ESP-NOW only mode");
  }

  // ── MQTT setup ──
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setBufferSize(512);
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

      publishToMqtt(idx, qMsg.data, soc);
    }
  }
  delay(10);
}