#ifndef MB_CONFIG_H
#define MB_CONFIG_H

#include <stdint.h>

// ==============================================================
// MB_CONFIG_H — Production Slave Board Hardware Configuration
// ==============================================================
// Configured for the ESP32-S3 slave board with 13S 3P pack topology.
// ==============================================================

// ==================== I2C CONFIG ====================
const int MB_I2C_SDA = 12; // Slave Board ESP32-S3 SDA
const int MB_I2C_SCL = 13; // Slave Board ESP32-S3 SCL
const uint8_t BQ_I2C_ADDR_MB = 0x08;

// ==================== DASHBOARD AP CONFIG ====================
const char *const MB_AP_SSID = "wBMS-SlaveAP";
const char *const MB_AP_PASSWORD = "wbms1234";

// ==================== PACK CONFIG (13S 3P) ====================
const int MB_CONNECTED_CELLS = 13;
const int MB_PARALLEL_CELLS = 3; // 13S3P Pack
const float MB_CELL_CAPACITY_AH = 3.0f;
const uint32_t MB_UPDATE_INTERVAL_MS = 500;

// BQ76952 Cell Pin Mapping for 13S configuration:
//   Cells 1-12  -> VC1 through VC12 (normal)
//   VC13, VC14, VC15 are shorted to VC12 (unused/bypassed)
//   Cell 13     -> VC16-VC15 (reads VC16 minus VC12 through the shorted chain)
const uint8_t CELL_TO_BQ[13]      = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 16};
const uint8_t CELL_TO_BAL_BIT[13] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 15};

// ==================== INDICATOR LED PINS ====================
const int MB_PIN_CHG_EXT_LED = 47; // Charge process indicator LED
const int MB_PIN_DSG_EXT_LED = 48; // Discharge process indicator LED

// ==================== ALERT PIN (BQ76952 Open-Drain Watchdog) ====================
const int MB_PIN_ALERT = 14; // GPIO 14 -> ALERT (ESP watchdog sink)

// ==================== FET OVERRIDE PINS (NOT WIRED ON SLAVE BOARD) ====================
// Defined as -1 to indicate they are not routed/wired on the slave board PCB.
const int MB_PIN_CFETOFF = -1;
const int MB_PIN_DFETOFF = -1;

#endif
