#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_wifi.h>
#include "BQ76952.h"
#include "config.h"

DeviceMessage outgoingData;
BQ76952 bms;

// ==================== 13S CELL MAPPING ====================
// Mirrors mainboard_dashboard/tb_config.h CELL_TO_BQ. Cells 1..12 read from
// VC1..VC12, cell 13 reads from VC16 (VC13..VC15 are shorted to VC12).
static const uint8_t CELL_TO_BQ[13] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 16};
// Vcell Mode: enable VC1..VC12 + VC16, disable VC13..VC15 -> 0b1000_1111_1111_1111
static const uint16_t VCELL_MODE_13S = 0x8FFF;

// ==================== PAIRING CODE ====================
char pairingCode[7] = "000000"; // Last 3 bytes of MAC as hex (e.g. "27AF28")

// ==================== HEARTBEAT / FALLBACK AP ====================
volatile int consecutiveFailures = 0;
portMUX_TYPE failureMux = portMUX_INITIALIZER_UNLOCKED;
bool fallbackAPActive = false;

void startFallbackAP()
{
  if (fallbackAPActive)
    return;
  Serial.println("[Heartbeat] Master unreachable -- starting fallback AP");
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(FALLBACK_AP_SSID, FALLBACK_AP_PASS);
  fallbackAPActive = true;
  Serial.printf("[Heartbeat] AP '%s' active at %s\n",
                FALLBACK_AP_SSID, WiFi.softAPIP().toString().c_str());
}

void stopFallbackAP()
{
  if (!fallbackAPActive)
    return;
  Serial.println("[Heartbeat] Master reachable again -- stopping fallback AP");
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  fallbackAPActive = false;
}

// ==================== CC3 CURRENT (mirrors mainboard) ====================
// Triggers DASTATUS5 and snipes the 2-byte CC3 current at buffer offset 0x14.
// CC3 is the heavily averaged current (filter set to 50 samples in initBMS()).
int getCC3CurrentOptimized()
{
  Wire.beginTransmission(0x08);
  Wire.write(0x3E);
  Wire.write(0x75); // DASTATUS5 low byte
  Wire.write(0x00);
  Wire.endTransmission();

  delayMicroseconds(1000); // TRM ~660us MAC execution

  Wire.beginTransmission(0x08);
  Wire.write(0x54); // 0x40 transfer buffer + offset 0x14 = CC3 current
  Wire.endTransmission(false);

  Wire.requestFrom((int)0x08, 2);
  if (Wire.available() >= 2) {
    uint8_t lo = Wire.read();
    uint8_t hi = Wire.read();
    return (int)((int16_t)((hi << 8) | lo));
  }
  return 0;
}

void collectBMSData()
{
  for (int i = 0; i < 16; i++)
  {
    if (i < CONNECTED_CELLS) {
      outgoingData.v[i] = bms.getCellVoltage(CELL_TO_BQ[i]);
    } else {
      outgoingData.v[i] = 0;
    }
  }
  outgoingData.v_stack = bms.getCellVoltage(17);
  outgoingData.v_pack = bms.getCellVoltage(18);

  // CC3-smoothed current with 20 mA deadband (kills ambient thermal noise)
  int cur = getCC3CurrentOptimized();
  if (abs(cur) <= 20) cur = 0;
  outgoingData.current = cur;

  // With DA_Configuration = 0x04, getAccumulatedCharge() returns mAh.
  // DeviceMessage.charge is documented as Ah, so divide by 1000.
  outgoingData.charge = bms.getAccumulatedCharge() / 1000.0f;
  outgoingData.charge_time = bms.AccumulatedChargeTime;
  outgoingData.chip_temp = bms.getInternalTemp();
  outgoingData.temp1 = bms.getThermistorTemp(TS1);
  outgoingData.temp2 = bms.getThermistorTemp(HDQ); // TS2 is disabled on hw; HDQ wired as NTC
  outgoingData.temp3 = bms.getThermistorTemp(TS3);
  outgoingData.isCharging = bms.isCharging();
  outgoingData.isDischarging = bms.isDischarging();
  snprintf(outgoingData.message, sizeof(outgoingData.message), "BMS:%s", pairingCode);
}

// ==================== BMS HARDWARE INIT (mirrors bms_init.h) ====================
// Slim hardware-only mirror of mainboard_dashboard/bms_init.h. Skips the
// dynamic balancing / NVS / EKF bits that belong to the dashboard, but the
// raw sensor configuration is identical so slave and dashboard read 1:1.
void initBMS()
{
  // Wake + unseal so subsequent writes are accepted
  bms.directCommandRead(0x12);
  bms.CommandOnlysubCommand(0x009A); // SLEEP_DISABLE
  bms.unseal();
  delay(50);

  // Power Config: LOOP_SLOW=3, SLEEP DISABLED (predictable for a continuous sender)
  bms.writeIntToMemory(0x9234, 0x298C);
  bms.writeIntToMemory(0x9237, 0);
  bms.writeByteToMemory(0x9236, 0x0D); // REG1 = 3.3V

  // Cell config: 13 cells, VC1..VC12 + VC16
  bms.setConnectedCells(CONNECTED_CELLS);
  bms.writeIntToMemory(0x9304, VCELL_MODE_13S);
  delay(50);

  // DA Configuration: USER_VOLTS_CV=1 (10 mV pack V), USER_AMPS=0 (1 mA current)
  bms.writeByteToMemory(DA_Configuration, 0x04);
  // CC3 filter: average 50 samples for clean current readings
  bms.writeByteToMemory(0x9307, 50);
  delay(50);

  // Thermistor pin configs: TS1/TS3/HDQ = NTC10K + 18k pull-up. TS2 OFF.
  bms.writeByteToMemory(TS1_Config, 0x07);
  bms.writeByteToMemory(TS2_Config, 0x00);
  bms.writeByteToMemory(TS3_Config, 0x07);
  bms.writeByteToMemory(0x9300, 0x07);     // HDQ as NTC10K
  bms.writeByteToMemory(0x92FA, 0x07);     // CFETOFF as NTC10K
  bms.writeByteToMemory(DFETOFF_Pin_Config, 0x00);
  bms.writeByteToMemory(DCHG_Pin_Config, 0x00);
  bms.writeByteToMemory(0x9302, 0x00);     // DDSG pin disabled
  delay(50);

  // Protection thresholds (Samsung 30Q + 1mΩ shunt — same as mainboard)
  bms.writeByteToMemory(0x9275, 54); // CUV ~2.75 V
  bms.writeByteToMemory(0x9276, 74); // CUV delay ~250 ms
  bms.writeByteToMemory(0x9278, 81); // COV ~4.1 V
  bms.writeByteToMemory(0x9279, 74); // COV delay ~250 ms
  bms.writeByteToMemory(0x9280, 3);  // OCC 6 A
  bms.writeByteToMemory(0x9281, 20); // OCC delay ~73 ms
  bms.writeByteToMemory(0x9282, 6);  // OCD1 12 A
  bms.writeByteToMemory(0x9283, 60); // OCD1 delay ~201 ms
  bms.writeByteToMemory(0x9284, 10); // OCD2 20 A
  bms.writeByteToMemory(0x9285, 14); // OCD2 delay ~50 ms
  bms.writeByteToMemory(0x9286, 2);  // SCD 40 A
  bms.writeByteToMemory(0x9287, 2);  // SCD delay ~15 us
  delay(50);

  // Enable V/I protections, leave temperature protections OFF (mainboard parity)
  bms.writeByteToMemory(Enabled_Protections_A, 0xFC);
  bms.writeByteToMemory(Enabled_Protections_B, 0x00);
  bms.writeByteToMemory(Enabled_Protections_C, 0x00);
  delay(50);

  // FET driver options + Mfg Status (FET_EN + PF_EN) so FETs stay enabled
  // after CONFIG_UPDATE exits
  bms.writeByteToMemory(FET_Options, 0x0D);
  bms.writeIntToMemory(Mfg_Status_Init, 0x0050);
  delay(50);

  // Clean integrator on boot
  bms.ResetAccumulatedCharge();
  delay(50);
}


int scanForChannel()
{
  Serial.printf("[WiFi] Scanning for master AP '%s' to detect channel...\n", MASTER_AP_SSID);
  int n = WiFi.scanNetworks();
  Serial.printf("[WiFi] Scan found %d networks:\n", n);
  int found = -1;
  for (int i = 0; i < n; i++)
  {
    int ch = WiFi.channel(i);
    int rssi = WiFi.RSSI(i);
    Serial.printf("[WiFi]   %2d: SSID='%s' ch=%d rssi=%d\n", i, WiFi.SSID(i).c_str(), ch, rssi);
    if (found < 0 && String(WiFi.SSID(i)) == MASTER_AP_SSID)
    {
      found = ch;
    }
  }
  WiFi.scanDelete();
  if (found >= 0)
  {
    Serial.printf("[WiFi] Found master AP '%s' on channel %d\n", MASTER_AP_SSID, found);
    return found;
  }
  Serial.printf("[WiFi] Master AP '%s' not found, using fallback channel %d\n", MASTER_AP_SSID, FALLBACK_CHANNEL);
  return FALLBACK_CHANNEL;
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
  portENTER_CRITICAL(&failureMux);
  if (status == ESP_NOW_SEND_SUCCESS)
  {
    consecutiveFailures = 0;
  }
  else
  {
    consecutiveFailures++;
  }
  portEXIT_CRITICAL(&failureMux);

  Serial.print("\r[ESP-NOW] Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAILED");
}

void setup()
{
  Serial.begin(115200);
  // BMS init — talks to real BQ76952 (or tb_bq_node simulator) over I2C
  bms.setDebug(false);
  bms.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  bms.reset();
  delay(500); // TRM: 250 ms+ after reset before I2C is ready

  // Re-init I2C bus — reset can leave it in a bad state
  Wire.end();
  delay(50);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 400000);
  delay(50);

  initBMS();

  WiFi.mode(WIFI_STA);
  // Derive pairing code from last 3 bytes of MAC address
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(pairingCode, sizeof(pairingCode), "%02X%02X%02X", mac[3], mac[4], mac[5]);
  Serial.printf("[INFO] Sender MAC: %s\n", WiFi.macAddress().c_str());
  Serial.printf("[INFO] Pairing Code: %s\n", pairingCode);
  int channel = scanForChannel();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("[ESP-NOW] ❌ Init Failed");
    ESP.restart();
  }
  esp_now_register_send_cb(OnDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, RECEIVER_ADDRESS, 6);
  peerInfo.channel = channel;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
}

void loop()
{
  collectBMSData();

  // Debug: print full 13S readout
  Serial.print("[I2C] V=");
  for (int i = 0; i < CONNECTED_CELLS; i++) {
    Serial.printf("%u ", outgoingData.v[i]);
  }
  Serial.printf("| Stack=%u Pack=%u Cur=%dmA Die=%.1fC T1=%.1f T2=%.1f T3=%.1f\n",
                outgoingData.v_stack, outgoingData.v_pack, outgoingData.current,
                outgoingData.chip_temp, outgoingData.temp1, outgoingData.temp2, outgoingData.temp3);

  esp_now_send(RECEIVER_ADDRESS, (uint8_t *)&outgoingData, sizeof(outgoingData));

  // ── Heartbeat check ──
  int failures;
  portENTER_CRITICAL(&failureMux);
  failures = consecutiveFailures;
  portEXIT_CRITICAL(&failureMux);

  if (failures >= FAILURE_THRESHOLD)
  {
    startFallbackAP();
  }
  else if (failures == 0 && fallbackAPActive)
  {
    stopFallbackAP();
  }

  delay(SEND_INTERVAL_MS);
}
