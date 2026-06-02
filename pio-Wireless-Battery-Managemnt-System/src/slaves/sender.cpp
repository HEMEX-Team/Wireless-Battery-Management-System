#include "BQ76952.h"
#include "tb_config.h"
#include "config.h"   // shared: DeviceMessage, MasterHeartbeat, MACs, link timing
#include "sys_stats.h"
#include <Arduino.h>
#include <BatteryEKF.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <math.h>
#include "SOH_Tracker.h"

// ==================== OBJECTS ====================
BQ76952 bms;
WebServer server(80);
Preferences prefs;
SOHTracker sohEngine;

// Helper to safely read subcommands (avoids BQ76952.cpp buggy method which
// writes 0x0000)
uint16_t safeReadSubCommand(uint16_t command, uint8_t length) {
  Wire.beginTransmission(0x08);
  Wire.write(0x3E);
  Wire.write(command & 0xFF);
  Wire.write((command >> 8) & 0xFF);
  Wire.endTransmission();
  delay(2);
  Wire.beginTransmission(0x08);
  Wire.write(0x40);
  Wire.endTransmission(false);
  Wire.requestFrom((int)0x08, (int)length);
  uint16_t result = 0;
  if (Wire.available() >= length) {
    result = Wire.read();
    result |= (Wire.read() << 8);
  }
  return result;
}

// SUBCOMMAND: 0x0075 DASTATUS5 (CC1 & CC3 Current)
// Uses the MAC engine to trigger DASTATUS5 and snipes the CC1 and CC3 values
// to save I2C bandwidth.
// ---------------------------------------------------------
void readCCData(int &cc3, int &cc1) {
  // Guard: For the first 3 seconds of boot, the BQ76952 CC3 hardware filter
  // is still collecting its samples and will return massive garbage spikes.
  // We use the instantaneous CC2 (0x3A) until stable.
  if (millis() < 3000) {
    int16_t cc2 = (int16_t)bms.directCommandRead(0x3A);
    cc3 = cc2; // All CC registers are in 1mA units (USER_AMPS=1mA, DA Config=0x05)
    cc1 = cc2;
    return;
  }

  // 1. Trigger the DASTATUS5 subcommand
  Wire.beginTransmission(0x08);
  Wire.write(0x3E);       // Subcommand Register
  Wire.write(0x75);       // Lower byte of 0x0075
  Wire.write(0x00);       // Upper byte of 0x0075
  Wire.endTransmission();

  // 2. Wait for the BQ76952 MAC engine to populate the buffer
  delayMicroseconds(1000); 

  // 3. Snipe the CC3 (offset 20/0x54) and CC1 (offset 22/0x56) Data. 
  // We read 4 bytes sequentially starting at 0x54!
  Wire.beginTransmission(0x08);
  Wire.write(0x54);       
  Wire.endTransmission(false);
  
  Wire.requestFrom((int)0x08, 4);
  if (Wire.available() >= 4) {
    uint8_t cc3_low = Wire.read();
    uint8_t cc3_high = Wire.read();
    uint8_t cc1_low = Wire.read();
    uint8_t cc1_high = Wire.read();
    
    cc3 = (int16_t)((cc3_high << 8) | cc3_low);
    cc1 = (int16_t)((cc1_high << 8) | cc1_low);
  } else {
    cc3 = 0;
    cc1 = 0;
  }
}

// ==================== CACHED BMS DATA ====================
unsigned int cellVoltages[16];
unsigned int vStack = 0;
unsigned int vPack = 0;
int bmsCurrent = 0;
int cc3_raw = 0;
int cc1_raw = 0;
float chipTemp = 0;
float temp1 = 0, temp2 = 0, temp3 = 0, temp_hdq = 0;
float charge = 0;
uint32_t chargeTime = 0;
bool isCharging = false;
bool isDischarging = false;
enum BalancingMode {
  BAL_MODE_AUTONOMOUS, // BQ Hardware controls
  BAL_MODE_HOST_ALGO,  // ESP32 math controls
  BAL_MODE_MANUAL      // User forced mask
};
BalancingMode currentBalMode = BAL_MODE_HOST_ALGO;
bool balancingEnabled = false; // Master toggle to enable/disable ANY balancing
bool balSuspended =
    false; // Tracks when ESP32 pauses balancing to protect BQ sleep
bool fetEn = false;
bool watchdogFaultLocked =
    false; // TRUE = safety fault active, ALERT LED stays SOLID RED
bool ledState = false;
bq_protection_t protStatus;
bq_temperature_t tempStatus;
uint16_t balancingMask = 0;
uint16_t hostBalTriggerMv = 3400;
uint16_t hostBalDeltaMv = 5;
uint16_t lastFetStat = 0; // Cached FET_Status register (0x7F) for API access
uint16_t hostRequestedMask = 0; // Tracks what the ESP32 explicitly commands
uint16_t manufStatus = 0;       // Sealing and ConfigUpdate status
uint16_t ssA_val = 0;           // Safety Status A cache
uint16_t ssB_val = 0;           // Safety Status B cache
uint16_t ssC_val = 0;           // Safety Status C cache
uint16_t saA_val = 0;           // Safety Alert A cache
uint16_t saB_val = 0;           // Safety Alert B cache
uint16_t saC_val = 0;           // Safety Alert C cache
uint16_t batStatusReg = 0;      // Battery Status (0x12) cache
uint8_t pfA_val = 0, pfB_val = 0, pfC_val = 0, pfD_val = 0; // PF Status
uint16_t controlStatus = 0;     // Control Status (0x00) for LD_TIMEOUT
unsigned int ldPinVoltage = 0;  // Direct Command 0x38 (LD Pin)
uint8_t powerMode = 0;          // 0=Active, 1=Sleep, 2=DeepSleep, 3=Shutdown
float g_soh_pct = 100.0f;        // Latest SOH (%) from sohEngine, mirrored for ESP-NOW uplink

// Fault Snapshot Variables
bool snapshotTaken = false;
float snapVoltages[16] = {0};
float snapPackVoltage = 0;
float snapCurrent = 0;
float snapMaxTemp = 0;
uint8_t pendingPwrMode =
    0; // 0=none, 1=SLEEP pending, 2=DEEPSLEEP pending, 3=WAKE pending
bool autoSleepEnabled = false; // Tracks Power Config (0x9234) Bit 0
unsigned long pwrCommandTime = 0;

unsigned long lastRead = 0;
uint32_t txCount = 0;

// ==================== BALANCING STATUS (DIAGNOSTIC) ====================
bool isHardwareBalancing = false;
uint32_t cellBalancingTimes[16] = {0};
uint16_t totalBalancingTime = 0;
uint16_t cellBalancingDelta = 0;
uint16_t minCellV = 0;
uint16_t maxCellV = 0;

// ==================== HARDWARE CONFIG TRACE ====================
uint8_t hw_cfg_cb = 0;
uint8_t hw_cfg_protA = 0;
uint8_t hw_cfg_protB = 0;

// ==================== EKF DATA ====================
BatteryEKF ekf; // 3-state EKF + Warburg feed-forward
float soc_ekf = 0.0f;
float soc_uncertainty = 0.0f;
float voltage_error_ekf = 0.0f;
float initial_ekf_soc = -1.0f;   // Stores the boot SOC from the EKF
float software_charge_Ah = 0.0f; // Accurate CC integration
unsigned long last_cc_reset_time = 0; // Hardware Coulomb Counter reset guard
unsigned long lastEKFUpdate = 0;
const unsigned long EKF_UPDATE_INTERVAL = 1000; // 1 second update (was 10s)

// ==================== PERFORMANCE PROFILING ====================
uint32_t perf_i2c_us = 0;
uint32_t perf_ekf_us = 0;
uint32_t perf_web_us = 0;

uint32_t accum_i2c_us = 0;
uint32_t accum_web_us = 0;

// Helper function for smart SOC initialization
float smartSOCInit(float V_measured, float I_current) {
  const float R0_avg = 0.018f;
  const float PARASITIC_R = 2.43f;

  // For a 3P pack: cell internal resistance is divided by parallel count
  float total_R = (R0_avg / (float)MB_PARALLEL_CELLS);
  if (fabsf(I_current) > 0.05f)
    total_R += PARASITIC_R;

  float V_corrected = V_measured - (total_R * I_current);

  // Invert OCV table using average of charge+discharge curves (rest-state hysteresis)
  float soc_init = ekf.invertOCV_Average(V_corrected);

  Serial.printf("[EKF] Smart Init (OCV lookup): V=%.3fV (raw=%.3fV), I=%.2fA "
                "-> SOC~%.1f%%\n",
                V_corrected, V_measured, I_current, soc_init);
  return constrain(soc_init, 0.0f, 100.0f);
}

void updateEKF() {
  float current_A =
      (float)cc1_raw / 1000.0f; // CC1 is in 1mA units (USER_AMPS=1mA), divide by 1000 for Amps
  float voltage_V = (float)cellVoltages[0] / 1000.0f; // Use cell 1 for EKF
  ekf.update(current_A / (float)MB_PARALLEL_CELLS, voltage_V); // Divide current by parallel count for cell-level EKF
  soc_ekf = ekf.getSOC();
  soc_uncertainty = ekf.getSOCUncertainty();
  voltage_error_ekf = ekf.getVoltageError() * 1000.0f;
  Serial.printf(
      "[EKF] I=%.3fA, V=%.3fV -> SOC=%.1f%% (unc=%.2f%%), Verr=%.1fmV\n",
      current_A, voltage_V, soc_ekf, soc_uncertainty, voltage_error_ekf);
}

// ==================== CELL BALANCING LOGIC ====================
// Legacy `runBalancing()` function completely removed to prevent it from
// maliciously overwriting `balancingMask` back to 0.

// ==================== DRY HELPER FUNCTIONS ====================
// Extracted to prevent typos and ensure identical behavior everywhere.

void clearBmsAlarms() {
  bms.directCommandWrite(0x62, 0xFF);
  bms.directCommandWrite(0x63, 0xFF);
}

void wakeBms() {
  bms.directCommandRead(0x12);       // Dummy read to wake I2C engine
  bms.CommandOnlysubCommand(0x009A); // SLEEP_DISABLE
}

void rearmAlerts() {
  clearBmsAlarms();
  bms.directCommandWrite(0x66, 0xFF); // Re-enable alarm mask low
  bms.directCommandWrite(0x67, 0xFF); // Re-enable alarm mask high
}

// Include the BQ76952 configuration packer (must be after all globals &
// helpers)
#include "bms_init.h"

// ==================== I2C LOG ====================
#define LOG_SIZE 50
struct I2CLogEntry {
  uint32_t timestamp;
  uint8_t reg;
  uint16_t value;
  bool isWrite;
  bool ok;
};
I2CLogEntry i2cLog[LOG_SIZE];
int logHead = 0;
int logCount = 0;

void addLog(uint8_t reg, uint16_t value, bool isWrite, bool ok) {
  i2cLog[logHead].timestamp = millis();
  i2cLog[logHead].reg = reg;
  i2cLog[logHead].value = value;
  i2cLog[logHead].isWrite = isWrite;
  i2cLog[logHead].ok = ok;
  logHead = (logHead + 1) % LOG_SIZE;
  if (logCount < LOG_SIZE)
    logCount++;
  txCount++;
}

// ==================== READ BMS DATA VIA I2C ====================
void readBMSData() {
  unsigned int batStat = bms.directCommandRead(DIR_CMD_BAT_STATUS);
  if (batStat == 0 || batStat == 0xFFFF) {
    Serial.println("[I2C] Comm failure. Chip is either in Deep Sleep/Shutdown, "
                   "or bus is noisy.");
    // If we specifically asked it to sleep, and now it's dead, assume it
    // worked!
    if (pendingPwrMode == 2)
      powerMode = 2;
    else if (pendingPwrMode == 3) {
      // WAKE PENDING but comm failed — chip is probably still in DEEPSLEEP
      // Don't set powerMode=3 (SHUTDOWN)! Keep current mode optimistically.
      powerMode = 0;
      Serial.println(
          "[PWR] WAKE pending but I2C comm failed. Assuming wake in progress.");
    } else
      powerMode = 4; // Add a new pseudo-mode: 4 = OFFLINE
    return;          // Skip the rest of the math
  }

  // THE TRUTH FIX: In Host Mode, reading 0x0083 triggers the MAC engine and kills the mask.
  // We MUST trust our internal ESP32 state and leave the BQ chip alone!
  uint16_t currentMask = 0;
  if (currentBalMode == BAL_MODE_AUTONOMOUS) {
    currentMask = safeReadSubCommand(0x0083, 2); // Safe to poll in Auto mode
  } else {
    currentMask = hostRequestedMask; // Trust the ESP32 state!
  }
  isHardwareBalancing = (currentMask > 0);
  bool isSleep = (batStat & 0x8000) != 0; // Bit 15 = SLEEP

  uint8_t prevMode = powerMode;
  // EVENT-DRIVEN POWER STATE CONFIRMATION
  // Instead of blindly trusting BatStatus, we use it as HARDWARE CONFIRMATION.
  // TRM Section 12.2.16: BatStatus bit 15 = SLEEP. No DEEPSLEEP bit exists.
  // pendingPwrMode: 0=none, 1=SLEEP pending, 2=DEEPSLEEP pending, 3=WAKE
  // pending

  // Periodic diagnostic (every ~5s to avoid serial flood)
  static unsigned long lastDiag = 0;
  if (millis() - lastDiag >= 5000) {
    lastDiag = millis();
    bool fetEnBit = (manufStatus & 0x0010) != 0; // Bit 4
    Serial.printf("[DIAG] BatStat=0x%04X | SLEEP=%d FET_EN=%d | "
                  "autoSleepIntent=%d\n",
                  batStat, isSleep ? 1 : 0,
                  fetEnBit ? 1 : 0, autoSleepEnabled ? 1 : 0);
  }

  if (pendingPwrMode == 1) {
    // SLEEP PENDING: Waiting for BatStatus SLEEP bit to confirm
    if (isSleep) {
      powerMode = 1;
      pendingPwrMode = 0;
      Serial.println("[PWR] SLEEP confirmed by BatStatus bit 15 = 1");
    } else if (millis() - pwrCommandTime > 5000) {
      // Timeout: BQ refused to enter SLEEP after 5 seconds
      pendingPwrMode = 0;
      Serial.println(
          "[PWR] WARNING: SLEEP failed! BQ did not set SLEEP bit after 5s.");
      // Attempt emergency force-sleep
      bms.CommandOnlysubCommand(0x0099); // SLEEP_ENABLE
    }
  } else if (pendingPwrMode == 3) {
    // WAKE PENDING: Waiting for BatStatus SLEEP bit to clear
    if (!isSleep) {
      powerMode = 0;
      pendingPwrMode = 0;
      Serial.println("[PWR] WAKE confirmed by BatStatus bit 15 = 0");
    } else if (millis() - pwrCommandTime > 5000) {
      // Timeout: BQ refused to wake after 5 seconds
      pendingPwrMode = 0;
      Serial.println(
          "[PWR] WARNING: WAKE failed! BQ still shows SLEEP after 5s.");
      // Aggressive wake: send dummy read and SLEEP_DISABLE
      bms.directCommandRead(0x12);
      bms.CommandOnlysubCommand(0x009A);
    }
  } else if (pendingPwrMode == 0) {
    // No transition pending: trust BatStatus normally
    if (isSleep)
      powerMode = 1;
    else
      powerMode = 0;
  }
  // pendingPwrMode == 2 (DEEPSLEEP): handled in loop(), don't touch powerMode
  // here

  if (powerMode != prevMode) {
    const char *modeNames[] = {"ACTIVE", "SLEEP", "DEEP SLEEP", "SHUTDOWN", "OFFLINE"};
    Serial.printf("[PWR] State changed: %s -> %s (BatStatus=0x%04X)\n",
                  modeNames[prevMode], modeNames[powerMode], batStat);
  }

  minCellV = 65535;
  maxCellV = 0;
  for (int i = 0; i < MB_CONNECTED_CELLS; i++) {
    uint16_t rawV = bms.getCellVoltage(CELL_TO_BQ[i]);
    
    // GLITCH FILTER: Discard insane values (I2C noise) to prevent phantom safety faults
    if (rawV > 500 && rawV < 5000) {
      cellVoltages[i] = rawV;
    } else {
      // If glitch detected, we stick with the previous value to prevent watchdog trip
      if (cellVoltages[i] == 0) cellVoltages[i] = rawV; 
    }

    if (cellVoltages[i] > 0 && cellVoltages[i] < minCellV)
      minCellV = cellVoltages[i];
    if (cellVoltages[i] > maxCellV)
      maxCellV = cellVoltages[i];
  }
  for (int i = MB_CONNECTED_CELLS; i < 16; i++)
    cellVoltages[i] = 0;

  cellBalancingDelta = (minCellV < 65535) ? (maxCellV - minCellV) : 0;

  vStack = bms.getCellVoltage(17);
  vPack = bms.getCellVoltage(18);
  // 1. Fetch both the smoothed CC3 (for display) and low-noise CC1 (for integration)
  readCCData(cc3_raw, cc1_raw);

  // Apply a short software low-pass filter (exponential smoothing with alpha = 0.8) for smooth UI display
  float current_meas_mA = (float)cc3_raw; // cc3_raw is in 1mA units, no conversion needed
  static float filtered_current_mA = 0.0f;
  static bool filter_seeded = false;
  if (!filter_seeded && current_meas_mA != 0.0f) {
      filtered_current_mA = current_meas_mA;
      filter_seeded = true;
  } else {
      filtered_current_mA = 0.8f * filtered_current_mA + 0.2f * current_meas_mA;
  }

  // 2. Apply a 2mA deadband to kill ambient thermal noise (1mA mode has higher noise floor)
  if (fabsf(filtered_current_mA) <= 2.0f) {
    bmsCurrent = 0;
  } else {
    bmsCurrent = (int)filtered_current_mA;
  }
  chipTemp = bms.getInternalTemp();
  temp1 = bms.getThermistorTemp(TS1);
  temp2 = bms.getThermistorTemp(HDQ); // HDQ thermistor (CFETOFF pin unpopulated — report HDQ in this slot)
  temp3 = bms.getThermistorTemp(TS3);
  temp_hdq = bms.getThermistorTemp(HDQ);

  // Poll the true hardware Cumulative Balancing Timers (CBSTATUS2/3)
  // TRM: CBSTATUS2/3 returns cumulative seconds per cell. These are ACCUMULATED
  // by the BQ hardware and only reset on device reset or CONFIG_UPDATE.
  // Poll every ~2 seconds (4 FULLSCAN cycles @ ~500ms each) for smooth UI
  // updates.
  static uint8_t slowPollCounter = 0;
  slowPollCounter++;
  if (slowPollCounter >= 4) {
    if (currentBalMode == BAL_MODE_AUTONOMOUS) {
      bms.GetCellBalancingTimes(cellBalancingTimes);
    }
    
    slowPollCounter = 0;
  }

  // RESTORED DEBUG LOG: Print real BQ76952 hardware state every poll cycle
  uint16_t fetStat = bms.directCommandRead(0x7F);
  lastFetStat = fetStat; // Cache for API access
  // Update LEDs based purely on BQ Hardware State
  // Bit 0 = CHG, Bit 1 = PCHG, Bit 2 = DSG, Bit 3 = PDSG
  int isChgOn = (fetStat & 0x01) ? 1 : 0;
  int isPchgOn = (fetStat & 0x02) ? 1 : 0;
  int isDsgOn = (fetStat & 0x04) ? 1 : 0;
  int isPdsgOn = (fetStat & 0x08) ? 1 : 0;

  // The ESP32 software variables mirror the hardware 1:1 with ZERO delay.
  isCharging = (isChgOn != 0) || (isPchgOn != 0);
  isDischarging = (isDsgOn != 0) || (isPdsgOn != 0);

  // Drive indicator LEDs on GPIO 47 (CHG) and GPIO 48 (DSG) purely based on process tracking (current flow)
  // We use a small threshold (+10mA / -10mA) to avoid noise lighting them up
  digitalWrite(MB_PIN_CHG_EXT_LED, (bmsCurrent > 10) ? HIGH : LOW);
  digitalWrite(MB_PIN_DSG_EXT_LED, (bmsCurrent < -10) ? HIGH : LOW);

  // ==================== WATCHDOG LOGIC ====================
  // Read PF Status (2 bytes each read)
  uint16_t pfAB = bms.directCommandRead(0x0B);
  uint16_t pfCD = bms.directCommandRead(0x0D);
  pfA_val = pfAB & 0xFF;
  pfB_val = (pfAB >> 8) & 0xFF;
  pfC_val = pfCD & 0xFF;
  pfD_val = (pfCD >> 8) & 0xFF;

  // Read Safety Status FIRST to decide whether to clear the ALERT latch.
  ssA_val = bms.directCommandRead(0x03); // Safety Status A
  ssB_val = bms.directCommandRead(0x05); // Safety Status B
  ssC_val = bms.directCommandRead(0x07); // Safety Status C
  saA_val = bms.directCommandRead(0x02); // Safety Alert A
  saB_val = bms.directCommandRead(0x04); // Safety Alert B
  saC_val = bms.directCommandRead(0x06); // Safety Alert C
  batStatusReg = bms.directCommandRead(0x12); // Battery Status
  controlStatus = bms.directCommandRead(0x00); // Control Status (for LD_TIMEOUT)
  ldPinVoltage = bms.directCommandRead(0x38); // LD Pin Voltage
  uint16_t alarms = bms.directCommandRead(0x62); // Read AlarmStatus
  
  bool pfActive = (pfA_val != 0) || (pfB_val != 0) || (pfC_val != 0) || (pfD_val != 0);
  bool ssActive = (ssA_val != 0) || (ssB_val != 0) || (ssC_val != 0);
  watchdogFaultLocked = pfActive || ssActive;

  if (watchdogFaultLocked) {
      // TIER 1 & TIER 2: FAULT ACTIVE (PF or SS). Keep LED Solid Red.
      pinMode(MB_PIN_ALERT, OUTPUT);
      digitalWrite(MB_PIN_ALERT, LOW);
      
      // Fault Snapshotting
      if (!snapshotTaken) {
          snapshotTaken = true;
          snapPackVoltage = vPack;
          snapCurrent = bmsCurrent;
          snapMaxTemp = max(chipTemp, max(temp1, max(temp2, max(temp3, temp_hdq))));
          for(int i=0; i<16; i++) snapVoltages[i] = cellVoltages[i];
          
          Serial.println("\n=======================================================");
          Serial.println("!!! FAULT DETECTED -> TAKING SYSTEM SNAPSHOT !!!");
          Serial.printf("Pack Voltage: %.2f V | Current: %.2f A | Max Temp: %.1f C\n", snapPackVoltage/1000.0, snapCurrent/1000.0, snapMaxTemp);
          Serial.printf("Cell 1: %.3f V | Cell 12: %.3f V | Cell 16: %.3f V\n", snapVoltages[0]/1000.0, snapVoltages[11]/1000.0, snapVoltages[15]/1000.0);
          Serial.println("=======================================================\n");
      }
      
      static unsigned long lastFaultLog = 0;
      if (millis() - lastFaultLog > 2000) {
          lastFaultLog = millis();
          if (pfActive) {
             Serial.printf("[TIER 1 - FATAL] PERMANENT FAILURE! PF_A: 0x%02X, PF_B: 0x%02X, PF_C: 0x%02X, PF_D: 0x%02X\n", pfA_val, pfB_val, pfC_val, pfD_val);
             Serial.println("-> FETs permanently open. Requires PF_RESET (0x0029) to clear.");
          } else {
             Serial.printf("[TIER 2 - FAULT] Safety Status: SSA: 0x%02X, SSB: 0x%02X, SSC: 0x%02X\n", ssA_val, ssB_val, ssC_val);
             Serial.printf("-> LD Pin Voltage: %d (10mV units). (Used for OCD/SCD recovery)\n", ldPinVoltage);
          }
      }
  } else {
    if (alarms > 0) {
      // Normal heartbeat: clear the alarm in the BQ
      bms.directCommandWrite(0x62, 0xFF); // Clear low byte alarms
      bms.directCommandWrite(0x63, 0xFF); // Clear high byte alarms
    }
    // No faults active: Float the pin to turn OFF the RED LED
    pinMode(MB_PIN_ALERT, INPUT);
  }

  // Read Manufacturing Status to get FET_EN (bit 4) and SEC state (bits 1:0)
  manufStatus = bms.getManufacturingStatus();
  fetEn =
      (manufStatus & (1 << 4)) != 0; // Bit 4 = FET_EN in Manufacturing Status
  protStatus = bms.getProtectionStatus();
  tempStatus = bms.getTemperatureStatus();

  // ==================== I2C POLLING FOR BALANCING ====================
  // Use library's getAccumulatedCharge() — handles MAC buffer retries/checksums properly
  charge = bms.getAccumulatedCharge();
  chargeTime = bms.AccumulatedChargeTime;
  
  // PASSQ HARDWARE INTEGRATION (Source of Truth):
  if (millis() - last_cc_reset_time > 1000) {
    software_charge_Ah = charge / 1000.0f; // PASSQ is in 1mAh units (USER_AMPS=1mA), divide by 1000 for Ah
  } else {
    software_charge_Ah = 0.0f;
  }
  // THE TRUTH FIX: hardware status is now derived from the logic above (currentMask)
  // which prevents the 'Quantum Observation' collision.
  balancingMask = currentMask; 

  // Clean output for Arduino Serial Plotter
  Serial.printf("Pack_V:%.2f Current_mA:%d Die_Temp:%.1f\n", vPack / 1000.0,
                bmsCurrent, chipTemp);

  // === COMPREHENSIVE DEBUG (every 3 seconds) ===
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 3000) {
    lastDebug = millis();

    // CC-based SOC calculation (same as web_api.h)
    float cc_soc = 0.0f;
    if (initial_ekf_soc >= 0.0f) {
      cc_soc = constrain(initial_ekf_soc + (software_charge_Ah / ((float)MB_PARALLEL_CELLS * MB_CELL_CAPACITY_AH)) * 100.0f,
                         0.0f, 100.0f);
    }

    Serial.println("─────────────────────────────────────────────");
    Serial.printf("[CC1]  raw=%d  (%.4f A)\n", cc1_raw, (float)cc1_raw / 1000.0f);
    Serial.printf("[CC3]  raw=%d  displayed=%d mA\n", cc3_raw, bmsCurrent);
    Serial.printf("[PASSQ] charge=%.3f userAh  time=%us  sw_charge=%.6f Ah (%.3f mAh)\n",
                  charge, chargeTime, software_charge_Ah, software_charge_Ah * 1000.0f);
    Serial.printf("[SOC]  EKF=%.1f%%  CC=%.1f%%  init=%.1f%%  Verr=%.1fmV\n",
                  soc_ekf, cc_soc, initial_ekf_soc, voltage_error_ekf);
    Serial.println("─────────────────────────────────────────────");
  }
}

// ==================== HOST BALANCING LOGIC ====================
void hostBalancingLoop() {
  uint16_t mask = balancingMask;

  // Diagnostic: print status every 10s so the user knows what's happening
  static unsigned long lastDiag = 0;
  bool showDiag = (millis() - lastDiag > 10000);

  if (!balancingEnabled) {
    hostRequestedMask = 0;
    if (isHardwareBalancing) {
      if (powerMode == 1) {
        wakeBms();
        delay(10);
      } // Wake to turn OFF!
      bms.setBalancingMask(0);
    }
    if (showDiag) {
      Serial.println("[HOST-BAL] Idle: Master OFF");
      lastDiag = millis();
    }
    return;
  }

  if (currentBalMode == BAL_MODE_AUTONOMOUS) {
    hostRequestedMask = 0;
    if (showDiag) {
      Serial.println("[HOST-BAL] Idle: Autonomous mode (HW controls)");
      lastDiag = millis();
    }
    return;
  }

  if (currentBalMode == BAL_MODE_MANUAL) {
    static unsigned long lastManualRefresh = 0;
    if (millis() - lastManualRefresh > 5000 && balancingMask > 0) {
      bms.setBalancingMask(balancingMask);
      lastManualRefresh = millis();
    }
    for (int i = 0; i < 16; i++) {
      if (balancingMask & (1 << i)) {
        cellBalancingTimes[i]++;
      }
    }
    return;
  }

  // ----- HOST ALGO MODE ENGINE -----
  static bool hostBalancingActive = false;
  static unsigned long lastBalRetry = 0;

  // 1. Hysteresis trigger: engage at >= delta, disengage only when delta drops
  // below (delta - 2mV). This matches the stable old dashboard behavior and
  // prevents measurement noise from rapidly flipping the engine on/off.
  uint16_t stopDelta = (hostBalDeltaMv > 2) ? (hostBalDeltaMv - 2) : 0;
  if (maxCellV >= hostBalTriggerMv && cellBalancingDelta >= hostBalDeltaMv) {
    hostBalancingActive = true;
  } else if (cellBalancingDelta <= stopDelta || maxCellV < hostBalTriggerMv) {
    hostBalancingActive = false;
  }
  // else: in the dead-band — keep the current state (no change)

  if (showDiag) {
    Serial.printf("[HOST-BAL] maxV=%d trig=%d delta=%d target=%d active=%d\n",
                  maxCellV, hostBalTriggerMv, cellBalancingDelta,
                  hostBalDeltaMv, hostBalancingActive);
    lastDiag = millis();
  }

  mask = 0;
  if (hostBalancingActive) {
    if (showDiag) Serial.println("[HOST-BAL-TRACE] Phase 1: Identifying cells to balance...");
    // Phase 1: Identify ALL cells that need balancing
    // Use the hysteresis stopDelta if the engine is already active
    uint16_t currentThreshold = (minCellV + (hostBalancingActive ? stopDelta : hostBalDeltaMv));
    
    bool wantsBalance[16] = {false};
    for (int i = 0; i < MB_CONNECTED_CELLS; i++) {
      if (cellVoltages[i] >= currentThreshold) {
        if (i == MB_CONNECTED_CELLS - 1) {
          wantsBalance[15] = true; // Last cell is mapped to VC16
          if (showDiag) Serial.printf("[HOST-BAL-TRACE] Cell %d (VC16) wants balance (V=%d, MinV=%d, Threshold=%d)\n", i+1, cellVoltages[i], minCellV, currentThreshold);
        } else {
          wantsBalance[i] = true;
          if (showDiag) Serial.printf("[HOST-BAL-TRACE] Cell %d wants balance (V=%d, MinV=%d, Threshold=%d)\n", i+1, cellVoltages[i], minCellV, currentThreshold);
        }
      }
    }

    // Phase 2: BITWISE ADJACENCY RESOLUTION (The Silicon Fix)
    // The BQ76952 silicon forbids balancing adjacent VC nodes (e.g., VC1 and VC2).
    // We map voltages to their actual BQ bit positions to resolve conflicts.
    uint16_t voltagesByBit[16] = {0};
    for (int j = 0; j < MB_CONNECTED_CELLS; j++) {
      int bit = (j == MB_CONNECTED_CELLS - 1) ? 15 : j; // Last cell is always VC16
      voltagesByBit[bit] = cellVoltages[j];
    }

    for (int i = 0; i < 15; i++) {
      if (wantsBalance[i] && wantsBalance[i + 1]) {
        if (voltagesByBit[i] >= voltagesByBit[i + 1]) {
          wantsBalance[i + 1] = false;
        } else {
          wantsBalance[i] = false;
        }
      }
    }

    // For cell counts < 16, the last active cell before VC16 (index MB_CONNECTED_CELLS - 2)
    // and VC16 (index 15) are physically adjacent on the board's wiring.
    if (MB_CONNECTED_CELLS < 16) {
      int lastVcBit = MB_CONNECTED_CELLS - 2;
      if (wantsBalance[lastVcBit] && wantsBalance[15]) {
        if (voltagesByBit[lastVcBit] >= voltagesByBit[15]) {
          wantsBalance[15] = false;
        } else {
          wantsBalance[lastVcBit] = false;
        }
      }
    }

    if (showDiag) Serial.println("[HOST-BAL-TRACE] Phase 3: Constructing final mask...");
    // Phase 3: Construct the final safe mask
    for (int i = 0; i < 16; i++) {
      if (wantsBalance[i]) {
        mask |= (1 << i);
        if (showDiag) Serial.printf("[HOST-BAL-TRACE] Adding bit %d to mask\n", i);
      }
    }
    if (showDiag) Serial.printf("[HOST-BAL-TRACE] Final calculated mask: 0x%04X\n", mask);
  }

  // THE RACE CONDITION FIX: Compare what we WANT (mask) against what we
  // REQUESTED
  if (mask != hostRequestedMask || (millis() - lastBalRetry > 5000)) {
    Serial.printf("[HOST-BAL-STEP 1] Ready to send mask. Wanted: 0x%04X, Last "
                  "Sent: 0x%04X\n",
                  mask, hostRequestedMask);

    // The BQ76952 ignores 0x0083 subcommands if it is asleep!
    if (powerMode == 1) {
      Serial.println(
          "[HOST-BAL-STEP 2] PowerMode is SLEEP. Firing wakeBms()...");
      wakeBms();
      delay(10);
    } else {
      Serial.println("[HOST-BAL-STEP 2] PowerMode is ACTIVE. No wake needed.");
    }

    // 0. CLEAR SAFETY FAULTS (In case a previous glitch is blocking the FETs)
    Wire.beginTransmission(0x08);
    Wire.write(0x3E);
    Wire.write(0x1D); // 0x001D CLEAR_SAFETY
    Wire.write(0x00);
    Wire.endTransmission();
    delay(5);

    // THE FINAL BOSS WEAPON: Precise TRM Section 4.5.2 Execution Sequence
    uint8_t subL = 0x83;
    uint8_t subM = 0x00;
    uint8_t datL = mask & 0xFF;
    uint8_t datM = (mask >> 8) & 0xFF;
    uint8_t checksum = (uint8_t)~(subL + subM + datL + datM);
    uint8_t len = 0x06;

    Serial.printf("[HOST-BAL-STEP 3] Executing TRM MAC Sequence for Mask 0x%04X (CS=0x%02X)\n", mask, checksum);

    // 1. Write the 2-byte subcommand to 0x3E
    Wire.beginTransmission(0x08);
    Wire.write(0x3E);
    Wire.write(subL);
    Wire.write(subM);
    Wire.endTransmission();

    // 2. Write the 2-byte balancing mask to the transfer buffer at 0x40
    Wire.beginTransmission(0x08);
    Wire.write(0x40);
    Wire.write(datL);
    Wire.write(datM);
    Wire.endTransmission();

    // 3. Write Checksum (0x60) and Length (0x61) together
    Wire.beginTransmission(0x08);
    Wire.write(0x60);
    Wire.write(checksum);
    Wire.write(len);
    Wire.endTransmission();
    
    delay(100); // Let the silicon evaluate and execute

    // Let's see if the hardware finally acknowledges!
    uint16_t hardwareConfirmation = safeReadSubCommand(0x0083, 2);
    Serial.printf("[HOST-BAL-VERIFY] Hardware physically reports mask: 0x%04X\n", hardwareConfirmation);
    
    // SYNC WITH WEB DASHBOARD (The API depends on these)
    isHardwareBalancing = (hardwareConfirmation != 0);
    hostRequestedMask = mask;
    lastBalRetry = millis();

    delay(20);
    Serial.println("[HOST-BAL-STEP 4] setBalancingMask completed.");
  }

  // Update cell timers for Host Algo mode
  if (balancingMask > 0) {
    totalBalancingTime += 1;
    for (int i = 0; i < 16; i++) {
      if (balancingMask & (1 << i)) {
        cellBalancingTimes[i]++;
      }
    }
  }
}

// Web handlers, HTML page, and API endpoints moved to web_api.h
#include "web_api.h"

// ESP-NOW client + online/offline mode switch (uses the dashboard globals and
// `server` declared above, so it must be included after them).
#include "espnow_link.h"

// ==================== SETUP ====================

void setup() {
  Serial.begin(115200);
  delay(1000);
  printBootStats("Slave");
  Serial.println("\n[wBMS-Slave] === BQ76952 Test Board Dashboard ===");

  // Load Custom Balancing Settings from NVS
  prefs.begin("bms_cfg", false);
  hostBalTriggerMv = prefs.getUShort("trig", 3400); 
  hostBalDeltaMv = prefs.getUShort("delta", 5);
  Serial.printf(
      "[wBMS-Slave] Loaded Host Balancing Settings: Trigger=%dmV, Delta=%dmV\n",
      hostBalTriggerMv, hostBalDeltaMv);

  // Start the ESP-NOW link in ONLINE mode (WiFi STA on the master's channel).
  // The local AP is no longer always-on; it is raised only on fallback by
  // enterOffline() when the master/cloud becomes unreachable.
  espnowSetup();

  // Load SOH State
  float saved_cycles = prefs.getFloat("soh_cyc", 0.0f);
  sohEngine.init(saved_cycles);
  Serial.printf("[SOH] Initialized with %.2f equivalent cycles. SOH: %.1f%%\n", saved_cycles, sohEngine.getSOH());

  Wire.begin(MB_I2C_SDA, MB_I2C_SCL, 400000); // Back to 400kHz
  bms.begin(MB_I2C_SDA, MB_I2C_SCL);
  bms.setDebug(false);

  // EKF configuration is now built-in to the 3-state constructor

  Serial.printf("[wBMS-Slave] I2C Master SDA=%d SCL=%d\n", MB_I2C_SDA, MB_I2C_SCL);

  bms.reset(); // RESTORED: Ensuring a clean boot state for testing
  delay(500);  // BQ76952 TRM requires 250ms+ after hardware reset before I2C is
               // ready

  // Re-initialize I2C bus after BQ reset — the reset can leave the bus in a bad
  // state
  Wire.end();
  delay(50);
  Wire.begin(MB_I2C_SDA, MB_I2C_SCL, 400000);
  delay(50);

  // Verify BQ is alive before proceeding
  unsigned int resetCheck = bms.directCommandRead(0x12);
  Serial.printf("[wBMS-Slave] Post-reset BatStatus=0x%04X (expect non-zero)\n",
                resetCheck);
  if (resetCheck == 0) {
    Serial.println("[wBMS-Slave] WARNING: BQ still not responding! Retrying...");
    delay(500);
    Wire.end();
    delay(50);
    Wire.begin(MB_I2C_SDA, MB_I2C_SCL, 400000);
    delay(50);
    resetCheck = bms.directCommandRead(0x12);
    Serial.printf("[wBMS-Slave] Retry BatStatus=0x%04X\n", resetCheck);
  }

  // 3. Push the Packed Configuration to the BQ76952
  // This reads saved balancing mode from NVS automatically!
  initBQ76952();

  // Reset the CC1 hardware integrator on startup to clear any legacy counts
  bms.ResetAccumulatedCharge();
  last_cc_reset_time = millis();
  software_charge_Ah = 0.0f;
  charge = 0.0f;

  // 4. Initialize the EKF Math Engine
  readBMSData(); // Refresh cell voltages once for init
  float V_init = (float)cellVoltages[0] / 1000.0f;
  float I_init = (float)bmsCurrent / 1000.0f; // Corrected: bmsCurrent is in mA, divide by 1000 for Amps!
  float SOC_init = smartSOCInit(V_init, I_init);
  initial_ekf_soc = SOC_init;
  ekf.setSampleTime(1.0f); // Match 1-second update interval to prevent 10x speed hallucination!
  ekf.setNNEnabled(false); // Ignore/Disable Neural Network per user request
  ekf.begin(SOC_init);
  Serial.printf("[EKF] Initialized with SOC=%.1f%%\n", SOC_init);

  // FET override pins are not wired on the slave board (CFETOFF is configured as NTC thermistor via 0x92FA)

  // Initialize Indicator LEDs (Process Tracking)
  pinMode(MB_PIN_CHG_EXT_LED, OUTPUT);
  pinMode(MB_PIN_DSG_EXT_LED, OUTPUT);
  digitalWrite(MB_PIN_CHG_EXT_LED, LOW);
  digitalWrite(MB_PIN_DSG_EXT_LED, LOW);

  Serial.printf("[wBMS-Slave] Process Tracking LEDs: CHG=GPIO%d, DSG=GPIO%d\n",
                MB_PIN_CHG_EXT_LED, MB_PIN_DSG_EXT_LED);

  isCharging = false;
  isDischarging = false;

  // 8. ALERT LED Control
  // The user confirmed J2 is identical to DCHG/DDSG.
  // This means the ESP32 must provide Ground (LOW) to turn ON the LED, 
  // and float the pin (INPUT) to turn OFF the LED.
  pinMode(MB_PIN_ALERT, INPUT); // Default to OFF (Floating)
  Serial.printf("[wBMS-Slave] ALERT LED: GPIO%d set to INPUT (Floating/OFF). "
                "Will sink to ground only on faults.\n",
                MB_PIN_ALERT);

  server.on("/", handleRoot);
  server.on("/api/data", handleApiData);
  server.on("/api/log", handleApiLog);
  server.on("/api/cmd", handleApiCmd);
  server.on("/api/ekf_reset", handleApiEKFReset);
  server.on("/api/config", HTTP_GET, handleApiConfigGet);
  server.on("/api/config", HTTP_POST, handleApiConfigPost);
  server.begin();

  // 9. ADC SETTLING: Wait for ADC to stabilize after config writes
  Serial.println(
      "[wBMS-Slave] Waiting 2s for ADC settling after config writes...");
  delay(2000);
  bms.directCommandWrite(0x62, 0xFF);
  bms.directCommandWrite(0x63, 0xFF);
  watchdogFaultLocked = false;
  Serial.println("[wBMS-Slave] Boot faults cleared. System ready.");

  Serial.printf("[wBMS-Slave] ONLINE: streaming to master over ESP-NOW. Local "
                "dashboard '%s' (pw '%s') is raised only on fallback.\n",
                slaveApSsid, MB_AP_PASSWORD);
}

// ==================== LOOP ====================
void loop() {
  uint32_t t0 = micros();

  // 1. ALARM REGISTER HEARTBEAT POLLING
  // Interrupt-driven polling: Wait for the BQ to pull ALERT pin LOW (Open Drain).
  // This indicates a FULLSCAN or Alarm is ready. If it goes LOW, the LED turns ON automatically!
  static unsigned long lastAlarmPoll = 0;
  bool bqAlertTriggered = (digitalRead(MB_PIN_ALERT) == LOW);
  
  // We use a 1000ms fallback timeout just in case the BQ goes silent, 
  // but normally it will trigger via bqAlertTriggered very quickly.
  if (bqAlertTriggered || (millis() - lastAlarmPoll >= 1000)) { 
    lastAlarmPoll = millis();
    uint16_t alarms = bms.directCommandRead(0x62);
    
    // PERIODIC TRACE: Print the exact physical state of the ALERT pin and the alarms register every 2 seconds
    static unsigned long lastPinTrace = 0;
    if (millis() - lastPinTrace > 2000) {
      lastPinTrace = millis();
      int pinState = digitalRead(MB_PIN_ALERT);
      Serial.printf("[TRACE] ALERT_PIN = %s (Logic %d) | ALARM_REG(0x62) = 0x%04X | SAA=0x%02X SAB=0x%02X SAC=0x%02X\n", 
                    (pinState == HIGH ? "HIGH (RED LED OFF)" : "LOW (RED LED ON)"), 
                    pinState, alarms, saA_val, saB_val, saC_val);
    }

    // POWER TRANSITION GUARD
    // pendingPwrMode: 0=none, 1=SLEEP pending, 2=DEEPSLEEP pending, 3=WAKE
    // pending
    if (pendingPwrMode == 2) {
      // DEEPSLEEP PENDING: No DEEPSLEEP bit in BatStatus.
      // Since writes are ignored in DEEPSLEEP, we cannot clear the alarm
      // register to check it. TRM guarantees DEEPSLEEP after double command. We
      // trust the command after 2 seconds.
      if (millis() - pwrCommandTime > 2000) {
        powerMode = 2;
        pendingPwrMode = 0;
        Serial.println("[PWR] DEEPSLEEP confirmed by timer (2s).");
      }
    }
    // SLEEP PENDING or WAKE PENDING: Keep reading BatStatus so we can confirm
    // the transition! readBMSData() will check BatStatus bit 15 and
    // confirm/deny the transition.
    else if (pendingPwrMode == 1 || pendingPwrMode == 3) {
      // Force a read every ~500ms so we catch the transition quickly
      if ((alarms & 0x2000) != 0 || (millis() - lastRead > 500)) {
        lastRead = millis();
        t0 = micros();
        readBMSData();
        accum_i2c_us += micros() - t0;
      }
    }
    // Normal operation: trigger readBMSData() when fresh data is available
    // 0x2000 = FULLSCAN (fresh ADC data ready), 0x00FF = Any Safety Fault
    // active
    else if ((alarms & 0x2000) != 0 || (alarms & 0x00FF) != 0 ||
             (millis() - lastRead > 2000)) {
      lastRead = millis();
      t0 = micros();
      readBMSData();
      accum_i2c_us += micros() - t0;
    }
  }
  if (millis() - lastEKFUpdate >= EKF_UPDATE_INTERVAL) {
    lastEKFUpdate = millis();
    t0 = micros();
    updateEKF();

    // Update SOH Tracker (1 second interval)
    float current_A = (float)bmsCurrent / 1000.0f;
    float max_temp_C = max(temp1, max(temp2, max(temp3, temp_hdq))); // Use highest thermistor reading
    if (max_temp_C < 5.0f || max_temp_C > 80.0f) max_temp_C = chipTemp; // Fallback if thermistors disconnected
    sohEngine.trackEnvironmentalDamage(current_A, max_temp_C, 1.0f);
    g_soh_pct = sohEngine.getSOH();  // mirror for the cloud uplink (packDeviceMessage)

    // Periodically save SOH state to NVS (every 5 minutes)
    static uint32_t lastSohSave = 0;
    if (millis() - lastSohSave >= 300000) {
      lastSohSave = millis();
      float current_cycles = sohEngine.getEquivalentCycles();
      float saved_cyc = prefs.getFloat("soh_cyc", 0.0f);
      if (fabs(current_cycles - saved_cyc) > 0.001f) {
        prefs.putFloat("soh_cyc", current_cycles);
        Serial.printf("[SOH] Saved to NVS: %.3f cycles (SOH: %.1f%%)\n", current_cycles, sohEngine.getSOH());
      }
    }

    // Hardware Configuration Trace (every 5 seconds)
    static uint32_t lastCfgTrace = 0;
    if (millis() - lastCfgTrace >= 5000) {
      lastCfgTrace = millis();
      byte* pCb = bms.readDataMemory(0x9335);
      if (pCb) hw_cfg_cb = pCb[0];
      byte* pProtA = bms.readDataMemory(0x9261);
      if (pProtA) hw_cfg_protA = pProtA[0];
      byte* pProtB = bms.readDataMemory(0x9262);
      if (pProtB) hw_cfg_protB = pProtB[0];
    }

    hostBalancingLoop(); // Engage explicitly if Auto is manually toggled out
    perf_ekf_us = micros() - t0;

    // Transfer accumulated times and reset
    perf_i2c_us = accum_i2c_us;
    perf_web_us = accum_web_us;
    accum_i2c_us = 0;
    accum_web_us = 0;
  }

  // Service the wireless link last, so a send packs the freshest reading.
  // ONLINE  -> stream DeviceMessage to the master + watch the heartbeat.
  // OFFLINE -> serve the local dashboard + periodically probe for recovery.
  uint32_t tLink = micros();
  espnowLoop();
  accum_web_us += (micros() - tLink);

  delay(2);
}
