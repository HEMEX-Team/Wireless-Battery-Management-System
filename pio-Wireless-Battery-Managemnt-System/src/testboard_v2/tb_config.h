#ifndef TB_CONFIG_H
#define TB_CONFIG_H

#include <stdint.h>

// ==================== I2C CONFIG ====================
// Both ESPs must share these same pins via jumper wires (SDA-SDA, SCL-SCL, GND-GND)
const int TB_I2C_SDA = 12;
const int TB_I2C_SCL = 13;
const uint8_t BQ_I2C_ADDR_TB = 0x08; // Same as real BQ76952

// ==================== DASHBOARD AP CONFIG ====================
const char *const TB_AP_SSID = "wBMS-TestBoard";
const char *const TB_AP_PASSWORD = "wbms1234";

// ==================== TEST BOARD CONFIG ====================
const int TB_CONNECTED_CELLS = 13;
const uint32_t TB_UPDATE_INTERVAL_MS = 500;

// ==================== LEGACY TESTBOARD PINS (NOT WIRED ON MAIN BOARD) ====================
// Kept to prevent compile errors; these GPIOs are unused on the main PCB.
const int TB_PIN_CFETOFF = 4;
const int TB_PIN_DFETOFF = 2;
const int TB_PIN_DDSG_LED = 15;
const int TB_PIN_DCHG_LED = 17;

// ==================== INDICATOR LED PINS (mainboard PCB) ====================
const int TB_PIN_CHG_EXT_LED = 47; // Charge process indicator LED
const int TB_PIN_DSG_EXT_LED = 48; // Discharge process indicator LED

const int TB_PIN_ALERT = 14;    // GPIO 14 -> ALERT (BQ76952 open-drain)

#endif
