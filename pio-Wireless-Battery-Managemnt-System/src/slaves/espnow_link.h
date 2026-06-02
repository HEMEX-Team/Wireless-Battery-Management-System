#pragma once
// ==============================================================
// espnow_link.h — ESP-NOW client + online/offline mode switch
// ==============================================================
// This is the "merge glue" that turns the standalone dashboard into the
// production slave firmware described in guide.md Part 1:
//
//   MODE_ONLINE  : WiFi STA on the master's channel. Packs the live BMS
//                  reading into the 152-byte DeviceMessage and ships it to
//                  the master over ESP-NOW every SEND_INTERVAL_MS. Listens
//                  for the master's heartbeat to know the cloud is alive.
//
//   MODE_OFFLINE : master/cloud unreachable -> raise a local SoftAP, serve
//                  the full dashboard + a captive-portal redirect, and keep
//                  the EKF/SOH running. Periodically hop back to the master's
//                  channel to probe for recovery.
//
// MUST be #included AFTER the dashboard globals (cellVoltages[], vStack,
// vPack, cc1_raw, chipTemp, temp1/2/3, software_charge_Ah, chargeTime,
// isCharging, isDischarging) and the `server` WebServer object are defined,
// because packDeviceMessage() reads them directly.
// ==============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <DNSServer.h>
#include "config.h"     // DeviceMessage, MasterHeartbeat, MACs, timing (shared)
#include "tb_config.h"  // MB_AP_PASSWORD

// ---- external dashboard state we serialize for the cloud ----
extern unsigned int cellVoltages[16];
extern unsigned int vStack;
extern unsigned int vPack;
extern int cc1_raw;        // low-noise CC1 current, 1 mA units (clean for cloud EKF, issues B2)
extern float chipTemp;
extern float temp1, temp2, temp3; // temp2 = CFETOFF on this board (issues B4 decision)
extern float software_charge_Ah;  // BQ PASSQ accumulator in Ah
extern uint32_t chargeTime;
extern bool isCharging;
extern bool isDischarging;
extern float g_soh_pct;                       // SOH (%) from sohEngine
extern uint16_t ssA_val, ssB_val, ssC_val;    // BQ76952 Safety Status A/B/C (latched faults)
extern WebServer server;

// ==================== LINK STATE ====================
enum LinkMode { LINK_ONLINE, LINK_OFFLINE };
LinkMode linkMode = LINK_ONLINE;

DeviceMessage espnowOut;
DNSServer dnsServer;

char pairingCode[7] = "000000";   // last 3 MAC bytes (e.g. "9A00FF")
char slaveApSsid[32] = "wBMS-Slave";

// Failure counter is written from the ESP-NOW send callback (WiFi task core),
// read from loop() — guard with a spinlock, same pattern as the master queue.
volatile int consecutiveFailures = 0;
portMUX_TYPE espnowMux = portMUX_INITIALIZER_UNLOCKED;

// Heartbeat state (written from the receive callback on the WiFi task).
volatile unsigned long lastHeartbeatMs = 0;
volatile bool          lastUplinkUp = false;
volatile bool          heartbeatEverSeen = false;
volatile uint8_t       masterChannel = FALLBACK_CHANNEL;

unsigned long onlineEnteredMs = 0;
unsigned long lastSendMs = 0;
unsigned long lastHopMs = 0;
int goodHops = 0;
int failedHops = 0;   // consecutive failed recovery hops on the cached channel;
                      // triggers an SSID rescan so a master channel change can't
                      // strand us forever (see doRecoveryHop).

// ==================== ADMIN COMMAND STATE ====================
// Admin commands arrive on the WiFi-task recv callback but execute heavy BQ76952
// I2C (which the main loop also drives). The callback only ENQUEUES; the command
// runs in processPendingCommand() on the main loop task — never touch I2C from the
// callback or it races the loop's reads.
SlaveCommand  g_pendingCmd;
volatile bool g_hasPendingCmd = false;
portMUX_TYPE  g_cmdMux = portMUX_INITIALIZER_UNLOCKED;
uint8_t       g_slaveMacTail[3] = {0, 0, 0};  // last 3 MAC bytes, stamped into snapshots

// ==================== CHANNEL SCAN ====================
// Locate the master's discovery AP (MASTER_AP_SSID) and return its channel, or
// -1 if the master AP isn't currently visible. The caller decides whether to
// fall back to FALLBACK_CHANNEL or keep its cached channel — important so a
// transient "not found" (master merely rebooting) doesn't clobber a good cache.
int scanForMasterChannel() {
  Serial.printf("[LINK] Scanning for master AP '%s'...\n", MASTER_AP_SSID);
  int n = WiFi.scanNetworks();
  int found = -1;
  for (int i = 0; i < n; i++) {
    if (String(WiFi.SSID(i)) == MASTER_AP_SSID) {
      found = WiFi.channel(i);
      break;
    }
  }
  WiFi.scanDelete();
  if (found >= 0)
    Serial.printf("[LINK] Found master on channel %d\n", found);
  else
    Serial.println("[LINK] Master AP not found");
  return found;
}

// ==================== ESP-NOW CALLBACKS ====================
void espnowOnSent(const uint8_t *mac, esp_now_send_status_t status) {
  portENTER_CRITICAL_ISR(&espnowMux);
  if (status == ESP_NOW_SEND_SUCCESS)
    consecutiveFailures = 0;
  else
    consecutiveFailures++;
  portEXIT_CRITICAL_ISR(&espnowMux);
}

// Master -> slave heartbeat. Carries the master's REAL uplink status, so the
// slave can detect "cloud down" even when the master node still ACKs (A3).
void espnowOnRecv(const uint8_t *mac, const uint8_t *data, int len) {
  // ---- Master heartbeat (cloud-status beacon) ----
  if (len == (int)sizeof(MasterHeartbeat) && data[0] == MASTER_HB_MAGIC) {
    MasterHeartbeat hb;
    memcpy(&hb, data, sizeof(hb));
    lastHeartbeatMs = millis();
    lastUplinkUp = (hb.uplinkUp != 0);
    masterChannel = hb.channel;     // master tells us its channel -> cheap re-tune, no rescan
    heartbeatEverSeen = true;
    return;
  }

  // ---- Admin command (cloud -> master -> here). Enqueue only; the main loop
  //      executes it (I2C must not run on the WiFi task). Only accept while
  //      ONLINE — offline we are on our own AP channel, not the master's. ----
  if (len == (int)sizeof(SlaveCommand) && data[0] == SLAVE_CMD_MAGIC) {
    if (linkMode != LINK_ONLINE) return;
    portENTER_CRITICAL_ISR(&g_cmdMux);
    memcpy(&g_pendingCmd, data, sizeof(SlaveCommand));
    g_hasPendingCmd = true;
    portEXIT_CRITICAL_ISR(&g_cmdMux);
    return;
  }
}

// ==================== PEER MANAGEMENT ====================
void espnowSetPeerChannel(uint8_t ch) {
  esp_now_del_peer(RECEIVER_ADDRESS);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, RECEIVER_ADDRESS, 6);
  peer.channel = ch;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
}

// Re-establish ESP-NOW after a WiFi stop/restart. On ESP32, esp_wifi_stop()
// — which the softAPdisconnect(true)/WiFi.mode() churn in our online<->offline
// transitions triggers — silently DE-INITS ESP-NOW and drops every peer. Any
// transition that brings the radio back up MUST call this, or esp_now_send()
// returns ESP_ERR_ESPNOW_NOT_INIT forever: the slave keeps "sending" but nothing
// leaves the radio, the TX-done callback never fires, so consecutiveFailures
// stays frozen and the node-down fallback can never trip (mute-but-"online").
// This is also why a slave booted BEFORE the master never reconnects: it goes
// offline, and every recovery hop's probe + heartbeat-listen runs on a dead
// radio. The peer is re-pinned to whatever channel the radio is currently on,
// so callers just esp_wifi_set_channel() first and let this follow.
void espnowReinit() {
  esp_now_deinit();   // harmless if not currently inited; also clears stale peers
  if (esp_now_init() != ESP_OK) {
    Serial.println("[LINK] ESP-NOW re-init failed; restarting");
    ESP.restart();
  }
  esp_now_register_send_cb(espnowOnSent);
  esp_now_register_recv_cb(espnowOnRecv);
  uint8_t primary = FALLBACK_CHANNEL;
  wifi_second_chan_t second;
  if (esp_wifi_get_channel(&primary, &second) != ESP_OK) primary = FALLBACK_CHANNEL;
  espnowSetPeerChannel(primary);
}

// ==================== STRUCT PACKING ====================
// Map the rich dashboard reading down to the 15-ish fields that fit the
// 152-byte ESP-NOW struct (guide Part 3). The local dashboard still serves
// the full ~60-field JSON; only this subset goes to the cloud.
void packDeviceMessage() {
  for (int i = 0; i < 16; i++) espnowOut.v[i] = cellVoltages[i];
  espnowOut.v_stack = vStack;
  espnowOut.v_pack = vPack;
  espnowOut.current = cc1_raw;            // clean CC1 (1 mA units), no deadband -> better cloud SOC (B2)
  espnowOut.chip_temp = chipTemp;
  espnowOut.temp1 = temp1;
  espnowOut.temp2 = temp2;                // CFETOFF, aligned to dashboard (Q2 decision)
  espnowOut.temp3 = temp3;
  espnowOut.charge = software_charge_Ah;  // Ah (struct documents Ah; dashboard holds mAh)
  espnowOut.charge_time = chargeTime;
  espnowOut.isCharging = isCharging;
  espnowOut.isDischarging = isDischarging;
  espnowOut.soh  = g_soh_pct;             // real SOH so the cloud stops showing a hardcoded 100
  espnowOut.ss_a = (uint8_t)ssA_val;      // latched BQ protection faults -> cloud alerts
  espnowOut.ss_b = (uint8_t)ssB_val;
  espnowOut.ss_c = (uint8_t)ssC_val;
  espnowOut.lastCmdSeq = g_lastCmdSeq;    // ack: echo the last applied admin command
  espnowOut.lastCmdRc  = g_lastCmdRc;
  snprintf(espnowOut.message, sizeof(espnowOut.message), "BMS:%s", pairingCode);
}

// ==================== MODE TRANSITIONS ====================
void enterOnline(uint8_t ch) {
  Serial.printf("[LINK] -> ONLINE (channel %d)\n", ch);
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  espnowReinit();   // WiFi was torn down above — ESP-NOW must be re-inited before we send
  portENTER_CRITICAL(&espnowMux);
  consecutiveFailures = 0;
  portEXIT_CRITICAL(&espnowMux);
  lastHeartbeatMs = millis();   // grace: don't immediately re-trip on stale heartbeat
  onlineEnteredMs = millis();
  lastSendMs = 0;
  linkMode = LINK_ONLINE;
}

void enterOffline() {
  Serial.println("[LINK] -> OFFLINE: raising local AP + dashboard");
  // Pin the AP to the master's last-known channel (deterministic, and keeps the
  // ESP-NOW peer aligned with the radio) instead of letting softAP() default to
  // channel 1.
  uint8_t apCh = masterChannel;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(slaveApSsid, MB_AP_PASSWORD, apCh);
  // Captive-portal redirect: answer every DNS query with our own IP so the
  // phone's "sign in to network" probe opens the dashboard automatically.
  dnsServer.start(53, "*", WiFi.softAPIP());
  Serial.printf("[LINK] AP '%s' (pw '%s') ch=%d at http://%s\n",
                slaveApSsid, MB_AP_PASSWORD, apCh, WiFi.softAPIP().toString().c_str());
  linkMode = LINK_OFFLINE;
  lastHopMs = millis();
  goodHops = 0;
  failedHops = 0;
}

// ==================== SETUP ====================
void espnowSetup() {
  WiFi.mode(WIFI_STA);

  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(pairingCode, sizeof(pairingCode), "%02X%02X%02X", mac[3], mac[4], mac[5]);
  g_slaveMacTail[0] = mac[3]; g_slaveMacTail[1] = mac[4]; g_slaveMacTail[2] = mac[5];
  snprintf(slaveApSsid, sizeof(slaveApSsid), "wBMS-Slave-%s", pairingCode); // unique per unit (issues C2)
  Serial.printf("[LINK] MAC %s  Pairing %s\n", WiFi.macAddress().c_str(), pairingCode);

  int scanned = scanForMasterChannel();
  uint8_t ch = (scanned >= 0) ? (uint8_t)scanned : (uint8_t)FALLBACK_CHANNEL;
  masterChannel = ch;
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  espnowReinit();   // init ESP-NOW + register callbacks + add peer on the scanned channel

  linkMode = LINK_ONLINE;
  onlineEnteredMs = millis();
  lastHeartbeatMs = millis();
}

// ==================== ONLINE SERVICE ====================
void serviceOnline() {
  // 1. Ship the live reading at the configured cadence.
  if (millis() - lastSendMs >= SEND_INTERVAL_MS) {
    lastSendMs = millis();
    packDeviceMessage();
    esp_err_t sendErr = esp_now_send(RECEIVER_ADDRESS, (uint8_t *)&espnowOut, sizeof(espnowOut));
    // A synchronous failure (classically ESP_ERR_ESPNOW_NOT_INIT after a WiFi
    // restart) never reaches espnowOnSent, so consecutiveFailures would stay
    // frozen and the fallback below could never trip. Count it here so node-down
    // can still fire, and self-heal the NOT_INIT case on the spot.
    if (sendErr != ESP_OK) {
      portENTER_CRITICAL(&espnowMux);
      consecutiveFailures++;
      portEXIT_CRITICAL(&espnowMux);
      if (sendErr == ESP_ERR_ESPNOW_NOT_INIT) {
        Serial.println("[LINK] online send NOT_INIT -> re-init ESP-NOW");
        espnowReinit();
      }
    }
  }

  // 2. Decide whether to fall back. Give a short grace window after going
  //    online so a single stale value can't bounce us straight back out.
  if (millis() - onlineEnteredMs < 3000) return;

  int fails;
  portENTER_CRITICAL(&espnowMux);
  fails = consecutiveFailures;
  portEXIT_CRITICAL(&espnowMux);

  bool nodeDown = (fails >= FAILURE_THRESHOLD);
  // The heartbeat-based triggers only apply once we've actually heard the
  // master at least once — otherwise a master running OLD firmware (no
  // heartbeat) would falsely look "offline" forever. ACK failure still covers
  // that case.
  bool hbStale  = heartbeatEverSeen && (millis() - lastHeartbeatMs > HEARTBEAT_TIMEOUT_MS);
  bool cloudDown = heartbeatEverSeen && !hbStale && !lastUplinkUp;

  if (nodeDown || hbStale || cloudDown) {
    Serial.printf("[LINK] fallback trigger: nodeDown=%d hbStale=%d cloudDown=%d (fails=%d)\n",
                  nodeDown, hbStale, cloudDown, fails);
    enterOffline();
  }
}

// ==================== OFFLINE RECOVERY HOP ====================
// Briefly leave the AP, tune to the master's channel, send a probe and listen
// for a heartbeat. Requires RECOVERY_GOOD_HOPS consecutive good hops (with the
// cloud actually up) before tearing down the AP. The AP blinks for ~RECOVERY_
// LISTEN_MS each hop; the dashboard JS retries silently to hide it (A6).
void doRecoveryHop() {
  // Normally re-tune cheaply to the master's last-known channel. But after a few
  // failed hops the master may have moved to a NEW channel (router reassignment),
  // and the heartbeat that carries the new channel is itself unhearable while we
  // are parked on the old one — a deadlock. Break it by rescanning the master's
  // SSID, which reads its discovery-AP channel from a full-band scan regardless
  // of what we last heard. (channel-change recovery)
  bool forceRescan = (failedHops >= RESCAN_AFTER_FAILED_HOPS);
  uint8_t ch;
  if (heartbeatEverSeen && !forceRescan) {
    ch = masterChannel;
  } else {
    int scanned = scanForMasterChannel();
    if (scanned >= 0) {
      ch = (uint8_t)scanned;
      masterChannel = ch;     // adopt the rediscovered channel as the new cache
    } else {
      ch = masterChannel;     // master not visible right now -> keep best guess
    }
    failedHops = 0;           // counted this rediscovery attempt
  }

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  espnowReinit();   // STA radio just restarted — re-arm ESP-NOW before the probe + heartbeat listen

  unsigned long preHb = lastHeartbeatMs;
  packDeviceMessage();
  esp_now_send(RECEIVER_ADDRESS, (uint8_t *)&espnowOut, sizeof(espnowOut)); // probe

  unsigned long t0 = millis();
  while (millis() - t0 < RECOVERY_LISTEN_MS) delay(10); // recv cb runs on WiFi task

  bool heardFreshHb = (lastHeartbeatMs != preHb);
  bool recovered = heardFreshHb && lastUplinkUp; // node up AND cloud up (A3)

  if (recovered) {
    goodHops++;
    failedHops = 0;
    Serial.printf("[LINK] recovery hop OK (%d/%d) ch=%d\n", goodHops, RECOVERY_GOOD_HOPS, ch);
    if (goodHops >= RECOVERY_GOOD_HOPS) {
      enterOnline(ch);
      return;
    }
  } else {
    goodHops = 0;
    failedHops++;
  }

  // Not recovered yet -> go back to serving the AP on the master's channel (so a
  // returning client and our radio agree, and the ESP-NOW peer stays aligned).
  WiFi.mode(WIFI_AP);
  WiFi.softAP(slaveApSsid, MB_AP_PASSWORD, ch);
  dnsServer.start(53, "*", WiFi.softAPIP());
  lastHopMs = millis();
}

// ==================== OFFLINE SERVICE ====================
void serviceOffline() {
  dnsServer.processNextRequest();
  server.handleClient();
  if (millis() - lastHopMs >= RECOVERY_HOP_INTERVAL_MS) {
    doRecoveryHop();
  }
}

// ==================== ADMIN COMMAND EXECUTOR (main loop task) ====================
// Pops one enqueued admin command and runs it where it's safe to drive I2C. The
// snapshot reply is also sent here (reads cached globals, no torn-read worries).
void processPendingCommand() {
  bool has = false;
  SlaveCommand c;
  portENTER_CRITICAL(&g_cmdMux);
  has = g_hasPendingCmd;
  if (has) { memcpy(&c, &g_pendingCmd, sizeof(c)); g_hasPendingCmd = false; }
  portEXIT_CRITICAL(&g_cmdMux);
  if (!has) return;

  CmdRc rc;
  if (c.op == OP_SNAPSHOT_REQ) {
    AdminSnapshot s;
    packAdminSnapshot(&s, c.seq);
    s.pcode[0] = g_slaveMacTail[0]; s.pcode[1] = g_slaveMacTail[1]; s.pcode[2] = g_slaveMacTail[2];
    esp_now_send(RECEIVER_ADDRESS, (uint8_t *)&s, sizeof(s));
    rc = RC_OK;
  } else if (c.op == OP_CONFIG_WRITE) {
    applyConfigWrite(c.cuv, c.cuv_d, c.cov, c.cov_d, c.occ, c.occ_d,
                     c.ocd1, c.ocd1_d, c.ocd2, c.ocd2_d, c.scd, c.scd_d);
    rc = RC_OK_REBOOT;   // new thresholds applied on the next boot (re-runs initBQ76952)
  } else if (c.op == OP_EKF_RESET) {
    runEkfReset();
    rc = RC_OK;
  } else {
    char msg[8];
    rc = runDeviceCommand(opToAction(c.op), c.arg_u16[0], c.arg_u16[1], msg, sizeof(msg));
  }

  g_lastCmdRc = rc;
  g_lastCmdSeq = c.seq;   // ack only AFTER applying, so the cloud doesn't mark it early
  if (rc == RC_OK_REBOOT) { g_pendingReboot = true; g_rebootArmedMs = millis(); }
  Serial.printf("[CMD] op=%d seq=%u rc=%d\n", c.op, c.seq, rc);
}

// ==================== LOOP ENTRY ====================
void espnowLoop() {
  processPendingCommand();
  // Deferred reboot (config-write / reset over ESP-NOW): wait so at least one
  // telemetry frame carrying the ack ships before we restart.
  if (g_pendingReboot && millis() - g_rebootArmedMs > 600) {
    Serial.println("[CMD] deferred reboot");
    delay(50);
    ESP.restart();
  }
  if (linkMode == LINK_ONLINE)
    serviceOnline();
  else
    serviceOffline();
}
