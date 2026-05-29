#ifndef SOH_TRACKER_H
#define SOH_TRACKER_H

#include <Arduino.h>
#include <math.h>

class SOHTracker {
private:
    // --- CONSTANTS ---
    const float NOMINAL_CAPACITY_AH = 3.0f; 
    const float BASELINE_CURRENT_A = 1.5f;   // 0.5C baseline
    const float ARRHENIUS_CONSTANT = 3000.0f;
    const float REFERENCE_TEMP_K = 298.15f;  // 25 Celsius in Kelvin

    // --- STATE VARIABLES ---
    float total_equivalent_cycles = 0.0f; 
    float current_soh_percent = 100.0f;

public:
    void init(float starting_equivalent_cycles = 0.0f) {
        total_equivalent_cycles = starting_equivalent_cycles;
        current_soh_percent = 100.0f;
        calculatePiecewiseSOH();
    }

    void trackEnvironmentalDamage(float measured_current_A, float measured_temp_C, float dt_seconds) {
        // Ignore micro-currents to prevent thermal noise accumulation
        if (fabs(measured_current_A) < 0.05f) return; 

        // 1. Calculate Thermal Stress (K_t) - Arrhenius Exponential
        float temp_K = measured_temp_C + 273.15f;
        float K_t = exp(ARRHENIUS_CONSTANT * ((1.0f / REFERENCE_TEMP_K) - (1.0f / temp_K)));
        
        // 2. Calculate C-Rate Stress (K_c) - Power Law
        float K_c = pow((fabs(measured_current_A) / BASELINE_CURRENT_A), 1.2f);
        
        // 3. Calculate actual fraction of a physical cycle consumed in this timeframe
        float delta_cycle = (fabs(measured_current_A) * (dt_seconds / 3600.0f)) / NOMINAL_CAPACITY_AH;
        
        // 4. Apply stress multipliers and add to the global accumulator
        total_equivalent_cycles += (delta_cycle * K_t * K_c);
        
        // 5. Recalculate SOH to keep it up to date
        calculatePiecewiseSOH();
    }

    void calculatePiecewiseSOH() {
        float SOH = 100.0f;
        
        if (total_equivalent_cycles <= 300.0f) {
            // Region 1: 0 to 300 cycles (SEI Formation)
            SOH = 100.0f - (total_equivalent_cycles * 0.0150f);
        } 
        else if (total_equivalent_cycles <= 600.0f) {
            // Region 2: 300 to 600 cycles (Linear Aging)
            float cycles_in_region = total_equivalent_cycles - 300.0f;
            SOH = 95.5f - (cycles_in_region * 0.0250f);
        } 
        else {
            // Region 3: The Knee Point (>600 cycles)
            float cycles_in_region = total_equivalent_cycles - 600.0f;
            SOH = 88.0f - (cycles_in_region * 0.0780f);
        }
        
        // Safety clamp
        current_soh_percent = constrain(SOH, 0.0f, 100.0f);
    }

    float getSOH() { return current_soh_percent; }
    float getEquivalentCycles() { return total_equivalent_cycles; }
};

// Global instance
extern SOHTracker sohEngine;

#endif
