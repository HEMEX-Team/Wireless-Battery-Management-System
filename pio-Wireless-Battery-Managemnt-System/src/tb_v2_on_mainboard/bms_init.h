#pragma once
// ==============================================================
// bms_init.h — BQ76952 Hardware Configuration Packer
// ==============================================================
// All Data Memory writes (CONFIG_UPDATE-triggering) are collected
// here and executed exactly ONCE during setup(). The main .cpp
// stays clean and no runtime CONFIG_UPDATE bombs can occur.
//
// MUST be #included AFTER all globals (bms, prefs, helpers) are
// defined so the inline function can reference them.
// ==============================================================

#include "BQ76952.h"
#include "tb_config.h"

// Register aliases are #defines in BQ76952.h, included above

inline void initBQ76952() {
  Serial.println("[BMS-INIT] Packing and applying full BQ76952 configuration...");

  // 1. Wake and Unseal
  wakeBms();
  bms.unseal();
  delay(50);

  // 2. Core Power & VCell Routing
  // Read auto_sleep from NVS (Restart-to-Apply)
  bool autoSlp = prefs.getBool("auto_sleep", false);
  autoSleepEnabled = autoSlp;
  if (autoSlp) {
    bms.writeIntToMemory(0x9234, 0x298D); // LOOP_SLOW=3, SLEEP ENABLED
  } else {
    bms.writeIntToMemory(0x9234, 0x298C); // LOOP_SLOW=3, SLEEP DISABLED
  }
  Serial.printf("[BMS-INIT] Power Config: autoSleep=%d\n", autoSlp);
  bms.writeIntToMemory(0x9237, 0);
  bms.writeByteToMemory(0x9236, 0x00);  // REG1 = Disabled (Slave board uses onboard 3.3V regulator)
  bms.setConnectedCells(MB_CONNECTED_CELLS); // Default library behavior
  delay(100);
  if (MB_CONNECTED_CELLS == 13) {
    // CRITICAL HARDWARE FIX for slave board topology:
    // The slave board uses VC1-VC12 and VC16 (top cell).
    // The library calculates 0x1FFF (VC1-VC13). We must override it to 0x8FFF.
    bms.writeIntToMemory(0x9304, 0x8FFF);
    Serial.println("[BMS-INIT] Overrode VCell Mode to 0x8FFF (Cells 1-12 + 16)");
  } else if (MB_CONNECTED_CELLS == 3) {
    bms.writeIntToMemory(0x9304, 0x8003);  // Cells 1, 2, 16 for 3-cell testboard
    Serial.println("[BMS-INIT] Overrode VCell Mode to 0x8003 (Cells 1-2 + 16)");
  } else {
    bms.setConnectedCells(MB_CONNECTED_CELLS); // Dynamic mask for other cell counts
  }
  
  delay(100);

  // 3. Thermistors
  Serial.println("[BMS-INIT] Thermistors: TS1, TS3, HDQ, and CFETOFF = NTC10K");
  bms.writeByteToMemory(0x92FD, 0x07); // TS1 ENABLED (NTC10K + 18kΩ)
  bms.writeByteToMemory(0x92FF, 0x07); // TS3 ENABLED (NTC10K + 18kΩ)
  bms.writeByteToMemory(0x9300, 0x07); // HDQ ENABLED (NTC10K + 18kΩ)
  bms.writeByteToMemory(0x92FA, 0x07); // CFETOFF pin ENABLED as thermistor (NTC10K + 18kΩ)
  
  bms.writeByteToMemory(0x92FE, 0x00); // TS2 DISABLED
  bms.writeByteToMemory(0x92FB, 0x00); // DFETOFF DISABLED
  delay(50);

  // Verify HDQ
  byte *tsCheck = bms.readDataMemory(0x9300);
  if (tsCheck)
    Serial.printf("[BMS-INIT] HDQ readback: 0x%02X (expect 0x07)\n", tsCheck[0]);

  // 4. Protection Thresholds (Loading from NVS/Prefs)
  Serial.println("[BMS-INIT] Protection thresholds (Loading from NVS/Prefs)");
  uint16_t cuv_mv = prefs.getUShort("cuv", 2750);
  uint16_t cuv_d  = prefs.getUShort("cuv_d", 250);
  uint16_t cov_mv = prefs.getUShort("cov", 4100);
  uint16_t cov_d  = prefs.getUShort("cov_d", 250);
  uint16_t occ_a  = prefs.getUShort("occ", 10);
  uint16_t occ_d  = prefs.getUShort("occ_d", 73);
  uint16_t ocd1_a = prefs.getUShort("ocd1", 12);
  uint16_t ocd1_d = prefs.getUShort("ocd1_d", 201);
  uint16_t ocd2_a = prefs.getUShort("ocd2", 20);
  uint16_t ocd2_d = prefs.getUShort("ocd2_d", 50);
  uint8_t scd     = prefs.getUChar("scd", 2);
  uint8_t scd_d   = prefs.getUChar("scd_d", 2);

  bms.writeByteToMemory(0x9275, (uint8_t)(cuv_mv / 50.6f)); // CUV
  bms.writeByteToMemory(0x9276, (uint8_t)(cuv_d / 3.3f));   // CUV Delay
  bms.writeByteToMemory(0x9278, (uint8_t)(cov_mv / 50.6f)); // COV
  bms.writeByteToMemory(0x9279, (uint8_t)(cov_d / 3.3f));   // COV Delay
  bms.writeByteToMemory(0x9280, (uint8_t)(occ_a / 2.0f));   // OCC
  bms.writeByteToMemory(0x9281, (uint8_t)(occ_d / 3.3f));   // OCC Delay
  bms.writeByteToMemory(0x9282, (uint8_t)(ocd1_a / 2.0f));  // OCD1
  bms.writeByteToMemory(0x9283, (uint8_t)(ocd1_d / 3.3f));  // OCD1 Delay
  bms.writeByteToMemory(0x9284, (uint8_t)(ocd2_a / 2.0f));  // OCD2
  bms.writeByteToMemory(0x9285, (uint8_t)(ocd2_d / 3.3f));  // OCD2 Delay
  bms.writeByteToMemory(0x9286, scd);                       // SCD
  bms.writeByteToMemory(0x9287, scd_d);                     // SCD Delay
  delay(50);

  // 4.1 Enable V/I protections, DISABLE temperature protections
  bms.writeByteToMemory(0x9261, 0xFC); // Protections_A
  bms.writeByteToMemory(0x9262, 0x00); // Protections_B (temp OFF)
  bms.writeByteToMemory(0x9263, 0x00); // Protections_C
  delay(50);

  // 4.2 Permanent Failure & Autonomous Recovery Configuration (SLAVE BOARD PREP)
  Serial.println("[BMS-INIT] Configuring PF and Recovery Logic (TOSF, Charger, LD)");
  
  // TOSF: Top of Stack Fault (3V threshold, 5s delay)
  bms.writeByteToMemory(0x92C3, 0x01); // Settings:Permanent Failure:Enabled PF D[TOSF]
  bms.writeIntToMemory(0x92D1, 300);   // Permanent Fail:TOS:Threshold = 3V (300 * 10mV units)
  bms.writeByteToMemory(0x92D2, 5);     // Permanent Fail:TOS:Delay = 5s

  // Charger Detection (Recovery from CUV)
  bms.writeByteToMemory(0x929F, 50);    // Settings:Protection:Recovery:Charger Detect = 500mV (50 * 10mV)
  
  // Load Detect (Autonomous recovery from SCD/OCD)
  bms.writeByteToMemory(0x92A0, 2);     // Protections:Load Detect:Timeout = 2 seconds
  
  // Pre-Charge (PCHG) Logic
  bms.writeIntToMemory(0x930B, 2500);   // Settings:Protection:Precharge Start Voltage = 2.5V
  bms.writeIntToMemory(0x930D, 3000);   // Settings:Protection:Precharge Stop Voltage = 3.0V
  
  // ----- TEST: READ THE DEFAULTS TO VERIFY ADDRESSES -----
  byte* pfEnD = bms.readDataMemory(0x92C3);
  uint8_t pfEnD_val = pfEnD ? pfEnD[0] : 0xFF;
  byte* tosThresh = bms.readDataMemory(0x92D1);
  int tosThresh_val = tosThresh ? tosThresh[0] : -1;
  byte* tosDelay = bms.readDataMemory(0x92D2);
  int tosDelay_val = tosDelay ? tosDelay[0] : -1;
  
  Serial.printf("[BMS-INIT] TEST READ -> Enabled PF D (0x92C3) expected 0x00: 0x%02X\n", pfEnD_val);
  Serial.printf("[BMS-INIT] TEST READ -> PF TOS Thresh (0x92D1) expected ~100: %d\n", tosThresh_val);
  Serial.printf("[BMS-INIT] TEST READ -> PF TOS Delay (0x92D2) expected ~5: %d\n", tosDelay_val);

  delay(50);


  // 5. FET Control & Manufacturing Init
  Serial.println("[BMS-INIT] FET Options -> 0x0D (Autonomous)");
  bms.writeByteToMemory(0x9308, 0x0D);
  delay(50);

  // Mfg Status Init: FET_EN=1, PF_EN=1 (survives CONFIG_UPDATE exits)
  Serial.println("[BMS-INIT] Mfg Status Init -> 0x0050 (FET_EN + PF_EN)");
  bms.writeIntToMemory(0x9343, 0x0050);
  delay(50);

  // Removed duplicate CFETOFF/DFETOFF configuration (now handled in thermistors block)

  // 6. Pin Configuration (Hardware Lobotomy)
  // ALERT Config: Bit 5=1 (Enable), Bit 4=1 (Active Low), Bit 3=0 (Open Drain), Bits 2:0=010 (Alarm)
  // 0x32 = 0011 0010 -> BQ will sink to ground when alarming!
  bms.writeByteToMemory(0x92FC, 0x32); 
  bms.writeByteToMemory(0x926D, 0x01); // Alarm Mask Low (Safety)
  bms.writeByteToMemory(0x926E, 0x20); // Alarm Mask High (FULLSCAN)
  
  // The hardware team left these floating or wired them poorly.
  // We write 0x00 to disable them entirely inside the BQ76952 hardware.
  bms.writeByteToMemory(0x9301, 0x00); // DCHG Pin = Disabled
  bms.writeByteToMemory(0x9302, 0x00); // DDSG Pin = Disabled
  delay(50);

  // DA Configuration (0x9303): Switch to 1mA mode for higher range (32.7A)
  // Bit 0 = USER_AMPS: 1=1mA (default), 0=0.1mA (high precision)
  // We SET Bit 0 and preserve Bit 2 (VCELL_MODE): 0x05
  Serial.println("[BMS-INIT] Configuring DA Config for 1mA mode...");
  bool daConfigOk = false;
  for (int attempt = 0; attempt < 3 && !daConfigOk; attempt++) {
    bms.writeByteToMemory(0x9303, 0x05);
    delay(50);
    byte *daCheck = bms.readDataMemory(0x9303);
    if (daCheck && daCheck[0] == 0x05) {
      Serial.printf("[BMS-INIT] DA Config = 0x05 (1mA mode) ✓ (attempt %d)\n", attempt + 1);
      daConfigOk = true;
    } else {
      Serial.printf("[BMS-INIT] DA Config write attempt %d: readback=0x%02X (wanted 0x05)\n",
                    attempt + 1, daCheck ? daCheck[0] : 0xFF);
      delay(100);
    }
  }
  if (!daConfigOk) {
    Serial.println("[BMS-INIT] WARNING: DA Config stuck at 1mA! Falling back.");
  }
  delay(50);

  // Set CC3 filter to average 50 samples for perfectly smooth UI current
  bms.writeByteToMemory(0x9307, 50);
  delay(50);

  // 7. DYNAMIC BALANCING MODE (Restart-to-Apply)
  // FORCED TO HOST ALGO FOR DEBUGGING
  int savedBalMode = prefs.getInt("bal_mode", BAL_MODE_HOST_ALGO);
  currentBalMode = (BalancingMode)savedBalMode;
  balancingEnabled = prefs.getBool("bal_master", true);
  bool balChg = prefs.getBool("bal_chg", true);
  bool balRlx = prefs.getBool("bal_rlx", true);

  Serial.printf("[BMS-INIT] Loaded: balMode=%d, master=%d, chg=%d, rlx=%d\n",
                currentBalMode, balancingEnabled, balChg, balRlx);

  // Build the 0x9335 config byte.
  // THE 0x4C BASELINE (The Truth):
  // Bit 6 (CB_EN) = 1: Master Enable ON.
  // Bit 3 (CB_NOSLEEP) = 1: Host commands allowed while awake.
  // Bit 2 (CB_SLEEP) = 1: Host commands allowed while asleep.
  // Bits 0 & 1 (CB_CHG & CB_RLX) = 0: Autonomous rules OFF.
  uint8_t balCfg = 0x4C; // Default to Host-Ready (0x4C)
  if (balancingEnabled && currentBalMode == BAL_MODE_AUTONOMOUS) {
    balCfg = 0x4F; // CB_EN + CB_SLEEP + CB_NOSLEEP + CB_CHG + CB_RLX
    if (!balChg) balCfg &= ~0x01; 
    if (!balRlx) balCfg &= ~0x02; 
  }
  bms.writeByteToMemory(Balancing_Configuration, balCfg);
  bms.writeByteToMemory(Cell_Balance_Max_Cells, 3);
  bms.writeIntToMemory(Cell_Balance_Min_Cell_V_Relaxed, 2500);

  // Override charge & relax limits for immediate engagement
  bms.writeIntToMemory(0x933B, 2500);  // Min Cell V (Charge)
  bms.writeByteToMemory(0x933D, 1);    // Min Delta (Charge)
  bms.writeByteToMemory(0x933E, 1);    // Stop Delta (Charge)
  bms.writeIntToMemory(0x933F, 2500);  // Min Cell V (Relax)
  bms.writeByteToMemory(0x9341, 1);    // Min Delta (Relax)
  bms.writeByteToMemory(0x9342, 1);    // Stop Delta (Relax)
  bms.writeByteToMemory(0x9339, 10);   // Interval: 10s
  delay(250);

  // 8. Post-config verification
  unsigned int batStat = bms.directCommandRead(0x12);
  Serial.printf("[BMS-INIT] Final BatStatus=0x%04X (SEC1:0=%d)\n",
                batStat, (batStat >> 8) & 0x03);

  Serial.println("\n[BMS-INIT VERIFY]");
  for (uint16_t addr = 0x9335; addr <= 0x9345; addr++) {
    byte *val = bms.readDataMemory(addr);
    if (val)
      Serial.printf("  0x%04X: 0x%02X (%d)\n", addr, val[0], val[0]);
  }
  Serial.println("=================================\n");

  // 9. Finalize & Kickstart
  Serial.println("[BMS-INIT] Exiting CONFIG_UPDATE mode...");
  bms.CommandOnlysubCommand(0x0092); // EXIT_CFGUPDATE
  delay(200); // Give silicon time to restart ADCs
  
  wakeBms();
  rearmAlerts();
  Serial.println("[BMS-INIT] Configuration packed and successfully applied.");
}
