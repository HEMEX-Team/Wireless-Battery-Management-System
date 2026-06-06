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
volatile bool          sendEverAcked = false;     // a master ACKed >=1 ESP-NOW send this boot (covers an old-firmware master that sends no heartbeat)
volatile uint8_t       masterChannel = FALLBACK_CHANNEL;

unsigned long onlineEnteredMs = 0;
unsigned long lastSendMs = 0;
unsigned long lastHopMs = 0;       // offline: last channel-recovery scan
int goodHops = 0;                  // consecutive good recovery signals (hysteresis)

// Offline-mode AP/recovery state. The offline AP runs in AP_STA on the master's
// channel and STAYS UP: the idle STA interface still receives the master's 1 Hz
// heartbeat (which the master unicasts to our STA MAC), so recovery is detected
// PASSIVELY without ever tearing the SoftAP down. The old per-hop teardown was
// the main cause of "AP connects on some phones, fails on others" — every
// RECOVERY_HOP_INTERVAL_MS it deauthed connected clients and killed in-progress
// DHCP. We only disturb the AP now for a real channel move (rare).
uint8_t       apChannel = FALLBACK_CHANNEL;   // channel the SoftAP is actually on
unsigned long lastHbCheckpoint = 0;           // last heartbeat ts we counted (edge-detect)

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
  if (status == ESP_NOW_SEND_SUCCESS) {
    consecutiveFailures = 0;
    sendEverAcked = true;   // we've reached the master at least once this boot
  } else
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
  peer.encrypt = true;                          // AES-128-CCMP to the master
  memcpy(peer.lmk, WBMS_ESPNOW_LMK, 16);
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
  // AES-128-CCMP: PMK must be (re)set after every esp_now_init() — deinit clears
  // it — and before the encrypted master peer is added below.
  if (esp_now_set_pmk(WBMS_ESPNOW_PMK) != ESP_OK)
    Serial.println("[LINK] set PMK failed — link will not be encrypted");
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
  esp_wifi_set_ps(WIFI_PS_NONE);   // no modem-sleep -> snappy, reliable ESP-NOW
  portENTER_CRITICAL(&espnowMux);
  consecutiveFailures = 0;
  portEXIT_CRITICAL(&espnowMux);
  lastHeartbeatMs = millis();   // grace: don't immediately re-trip on stale heartbeat
  onlineEnteredMs = millis();
  lastSendMs = 0;
  linkMode = LINK_ONLINE;
}

// Raise (or re-home) the offline dashboard AP on `ch`, in AP_STA so the idle STA
// interface can still hear the master's unicast heartbeat -> passive, teardown-
// free recovery (the master unicasts the heartbeat to our STA MAC, which a pure
// WIFI_AP slave cannot receive). Returns the channel the AP actually came up on:
// under the default "01" regulatory domain the SoftAP cannot host ch 12-14, so
// we fall back to a universally-legal channel instead of silently coming up
// wrong (softAP()'s result was previously never checked).
uint8_t raiseOfflineAp(uint8_t ch) {
  // The default "01" regulatory domain only lets a SoftAP beacon on channels
  // 1-11. Requesting 12-14 brings up an AP that shows in scans but cannot be
  // joined, and softAP() does NOT reliably report that as a failure on the
  // 2.0.x core, so the !ok retry below can't be trusted to catch it. Clamp at
  // this single chokepoint so no caller (incl. enterOffline co-channeling a
  // master on 12/13) can ever home the AP onto an unjoinable channel. A master
  // up there is still reached via probeHopForRecovery(), which never co-channels.
  if (ch < 1 || ch > 11) ch = (uint8_t)FALLBACK_CHANNEL;
  WiFi.mode(WIFI_AP_STA);
  // Deterministic AP subnet so the captive-portal gateway/DNS stay stable.
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                    IPAddress(255, 255, 255, 0));
  bool ok = WiFi.softAP(slaveApSsid, MB_AP_PASSWORD, ch, 0 /*not hidden*/, 4 /*max conn*/);
  if (!ok) {
    Serial.printf("[LINK] softAP ch %d rejected -> retry ch %d\n", ch, FALLBACK_CHANNEL);
    ch = (uint8_t)FALLBACK_CHANNEL;
    ok = WiFi.softAP(slaveApSsid, MB_AP_PASSWORD, ch, 0, 4);
    if (!ok) Serial.println("[LINK] softAP STILL failed — AP may be down!");
  }
  // Kill modem-sleep: with power-save on, the SoftAP delays beacons/DHCP and
  // slower clients fail to get an IP ("connecting… connection failed"). Must be
  // re-asserted after every WiFi.mode() change (mode changes reset it).
  esp_wifi_set_ps(WIFI_PS_NONE);
  // The mode change tore ESP-NOW down — re-arm it; espnowReinit() pins the peer
  // to the radio's current (= AP) channel, so send/recv work without leaving AP.
  espnowReinit();
  // Captive-portal redirect: answer every DNS query with our own IP so the
  // phone's "sign in to network" probe opens the dashboard automatically.
  dnsServer.stop();
  dnsServer.start(53, "*", WiFi.softAPIP());
  Serial.printf("[LINK] AP '%s' (pw '%s') up on ch %d at http://%s\n",
                slaveApSsid, MB_AP_PASSWORD, ch, WiFi.softAPIP().toString().c_str());
  return ch;
}

void enterOffline() {
  Serial.println("[LINK] -> OFFLINE: raising local dashboard AP (stays up; passive recovery)");
  // Prefer the master's last-known channel so we're co-channel with it and can
  // recover passively. Keep masterChannel as the TRUE best guess — do NOT clobber
  // it with a softAP fallback channel, or channel-change recovery would loop.
  uint8_t want = (masterChannel >= 1 && masterChannel <= 13) ? masterChannel
                                                             : (uint8_t)FALLBACK_CHANNEL;
  apChannel = raiseOfflineAp(want);
  linkMode = LINK_OFFLINE;
  lastHopMs = millis();
  lastHbCheckpoint = lastHeartbeatMs;   // baseline so only NEW beacons count toward recovery
  goodHops = 0;
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
  // Print the master MAC we transmit to, so a stale/wrong RECEIVER_ADDRESS (sends
  // un-ACKed -> node-down loop while still hearing the master's heartbeat) is
  // obvious at boot. Must match the master's STA MAC for delivery to work.
  Serial.printf("[LINK] Target master (RECEIVER_ADDRESS) %02X:%02X:%02X:%02X:%02X:%02X\n",
                RECEIVER_ADDRESS[0], RECEIVER_ADDRESS[1], RECEIVER_ADDRESS[2],
                RECEIVER_ADDRESS[3], RECEIVER_ADDRESS[4], RECEIVER_ADDRESS[5]);

  int scanned = scanForMasterChannel();
  uint8_t ch = (scanned >= 0) ? (uint8_t)scanned : (uint8_t)FALLBACK_CHANNEL;
  masterChannel = ch;
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  espnowReinit();   // init ESP-NOW + register callbacks + add peer on the scanned channel
  esp_wifi_set_ps(WIFI_PS_NONE);   // no modem-sleep -> reliable ESP-NOW + (in offline) SoftAP

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
  bool acked;
  portENTER_CRITICAL(&espnowMux);
  fails = consecutiveFailures;
  acked = sendEverAcked;
  portEXIT_CRITICAL(&espnowMux);

  bool nodeDown = (fails >= FAILURE_THRESHOLD);
  // The heartbeat-based triggers only apply once we've actually heard the
  // master at least once — otherwise a master running OLD firmware (no
  // heartbeat) would falsely look "offline" forever. ACK failure still covers
  // that case.
  bool hbStale  = heartbeatEverSeen && (millis() - lastHeartbeatMs > HEARTBEAT_TIMEOUT_MS);
  bool cloudDown = heartbeatEverSeen && !hbStale && !lastUplinkUp;
  // Cold-start safety net: the master was never reachable since boot. If we've
  // neither heard a heartbeat NOR had a single send ACKed by COLD_START_AP_MS,
  // fall back even though the failure counter never climbed (e.g. TX-done
  // callbacks never fired) so the unit ALWAYS ends up reachable on its own AP.
  // onlineEnteredMs == boot time on this path because enterOnline() (the only
  // thing that resets it) hasn't run yet. Once either flag latches true this can
  // never re-trigger, so it's strictly a one-shot cold-start guarantee.
  bool neverConnected = !heartbeatEverSeen && !acked;
  bool coldStart = neverConnected && (millis() - onlineEnteredMs > COLD_START_AP_MS);

  if (nodeDown || hbStale || cloudDown || coldStart) {
    Serial.printf("[LINK] fallback trigger: nodeDown=%d hbStale=%d cloudDown=%d coldStart=%d (fails=%d)\n",
                  nodeDown, hbStale, cloudDown, coldStart, fails);
    enterOffline();
  }
}

// ==================== OFFLINE RECOVERY ====================
// The offline AP stays up continuously. Recovery is normally PASSIVE: co-channel
// with the master, the idle STA interface hears the 1 Hz heartbeat and we just
// count good beacons (serviceOffline). The only case that must disturb the AP is
// a master sitting on a channel our SoftAP cannot host (12-14 under the default
// regulatory domain) — there we briefly hop to probe, because we can't be
// co-channel. This is the rare exception, not the every-20 s rule it used to be.
void probeHopForRecovery(uint8_t mc) {
  Serial.printf("[LINK] probe-hop to ch %d (SoftAP can't host it)\n", mc);
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(mc, WIFI_SECOND_CHAN_NONE);
  espnowReinit();   // STA radio just restarted — re-arm ESP-NOW before probing

  unsigned long preHb = lastHeartbeatMs;
  packDeviceMessage();
  esp_now_send(RECEIVER_ADDRESS, (uint8_t *)&espnowOut, sizeof(espnowOut)); // probe
  unsigned long t0 = millis();
  while (millis() - t0 < RECOVERY_LISTEN_MS) delay(10); // recv cb runs on WiFi task

  if ((lastHeartbeatMs != preHb) && lastUplinkUp) {     // node up AND cloud up (A3)
    goodHops++;
    Serial.printf("[LINK] probe-hop OK (%d/%d)\n", goodHops, RECOVERY_GOOD_HOPS);
    if (goodHops >= RECOVERY_GOOD_HOPS) { enterOnline(mc); return; }
  } else {
    goodHops = 0;
  }
  // Restore the dashboard AP on a client-legal channel (master is on 12-14, so we
  // can't follow it — clients reach us on a low channel, we keep probe-hopping).
  uint8_t legal = (apChannel >= 1 && apChannel <= 11) ? apChannel : (uint8_t)FALLBACK_CHANNEL;
  apChannel = raiseOfflineAp(legal);
}

// Called only while offline AND not currently hearing the master (it's down or
// has moved channel). Locate its discovery AP and realign: if it's on a SoftAP-
// hostable channel we MOVE our AP there so recovery goes passive again; if it's
// on 12-14 we fall back to a brief probe-hop. If the master isn't visible at all
// it's simply off — keep serving the AP; we'll hear it the moment it returns on
// our channel, no scan needed.
void attemptChannelRecovery() {
  int scanned = scanForMasterChannel();   // brief scan; clients see a short gap, no deauth
  if (scanned < 0) { goodHops = 0; return; }

  uint8_t mc = (uint8_t)scanned;
  masterChannel = mc;
  if (mc == apChannel) return;            // already co-channel; heartbeats will resume passively

  if (mc >= 1 && mc <= 11) {
    Serial.printf("[LINK] master now on ch %d -> moving AP to follow\n", mc);
    apChannel = raiseOfflineAp(mc);       // re-home the AP; recovery becomes passive
    goodHops = 0;
    return;
  }
  probeHopForRecovery(mc);                // 12-14: can't co-channel, must probe
}

// ==================== OFFLINE SERVICE ====================
void serviceOffline() {
  dnsServer.processNextRequest();
  server.handleClient();

  unsigned long hb = lastHeartbeatMs;                      // volatile snapshot
  bool hearingMaster = (millis() - hb) <= HEARTBEAT_TIMEOUT_MS;

  if (hearingMaster) {
    // Co-channel with a live master: recover passively, NEVER touch the AP.
    if (hb != lastHbCheckpoint) {                          // a fresh beacon arrived
      lastHbCheckpoint = hb;
      if (lastUplinkUp) {
        goodHops++;
        if (goodHops >= RECOVERY_GOOD_HOPS) {
          Serial.printf("[LINK] cloud restored (%d good beacons) -> ONLINE ch %d\n",
                        goodHops, masterChannel);
          enterOnline(masterChannel);
        }
      } else {
        goodHops = 0;                                      // master up, cloud still down -> stay
      }
    }
    return;
  }

  // Don't disturb the AP while a client is connected: scanForMasterChannel() is
  // a blocking all-channel scan that drags the single radio off the AP's channel
  // for seconds, stalling any in-progress association/DHCP (and the re-home /
  // probe-hop it can trigger deauths the client outright). Defer recovery until
  // they leave; passive co-channel recovery via the heartbeat still works above.
  // Reset the timer so we don't fire a scan the instant the last client drops.
  if (WiFi.softAPgetStationNum() > 0) { lastHopMs = millis(); return; }

  // Not hearing the master: it's down or moved channel. Search periodically,
  // disturbing the AP as little as possible (see attemptChannelRecovery).
  if (millis() - lastHopMs >= RECOVERY_HOP_INTERVAL_MS) {
    lastHopMs = millis();
    attemptChannelRecovery();
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
