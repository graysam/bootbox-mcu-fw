#pragma once
// Centralized build-time configuration for BOOTBOX DSP firmware.
// Adjust pin assignments and fan type here before building.

// -------- Fan configuration --------
enum class FanType : uint8_t { Fan3Wire = 3, Fan4Wire = 4 };

// Select your fan type (3-wire or 4-wire)
static constexpr FanType kFanType = FanType::Fan4Wire; // change to Fan3Wire if needed

// PWM output pin (for 4-wire: control pin). For 3-wire: drives power via MOSFET.
static constexpr int PIN_FAN_CTRL = 25;
// Tachometer input pin (open-collector, add pull-up to 3.3V through resistor)
static constexpr int PIN_FAN_TACH = 27;

// PWM configuration
static constexpr int FAN_PWM_CHANNEL = 0;     // LEDC channel index 0..15
static constexpr int FAN_PWM_RES_BITS = 8;    // 8-bit resolution (0..255)
static constexpr int FAN_PWM_FREQ_4WIRE = 25000; // 25 kHz recommended for 4-wire PC fans
static constexpr int FAN_PWM_FREQ_3WIRE = 1000;  // power PWM; tweak to avoid audible artifacts

// Suggested minimum duty for 3-wire to avoid stall (percentage)
static constexpr uint8_t FAN3_MIN_START_PCT = 25;

// -------- Thermal sensors --------
static constexpr int PIN_THERM1 = 34; // analog input (NTC divider)
static constexpr int PIN_THERM2 = 35; // analog input (NTC divider)

// ADC characteristics (adjust when replacing adcToTempC with your curve)
static constexpr float TEMP_MIN_C = 25.0f;
static constexpr float TEMP_SPAN_C = 55.0f;  // TEMP = MIN + (adc/4095)*SPAN

