#pragma once
// Centralized build-time configuration for BOOTBOX DSP firmware.
// Adjust pin assignments and fan type here before building.

// -------- Fan configuration --------
enum class FanType : uint8_t { Fan3Wire = 3, Fan4Wire = 4 };

// Select your fan type (3-wire or 4-wire)
static constexpr FanType kFanType = FanType::Fan3Wire; // change to Fan3Wire/Fan4Wire if needed

// PWM output pins (for 4-wire: control pin). For 3-wire: drives power via MOSFET.
// Configure up to two fans; set secondary values to -1 to disable.
static constexpr int PIN_FAN1_CTRL = 25;
static constexpr int PIN_FAN2_CTRL = 26; // set to -1 to disable second fan

// Tachometer inputs (future use)
static constexpr int PIN_FAN1_TACH = 27;
static constexpr int PIN_FAN2_TACH = -1;

// PWM channels
static constexpr int FAN1_PWM_CHANNEL = 0;     // LEDC channel index 0..15
static constexpr int FAN2_PWM_CHANNEL = 1;     // set to -1 if unused

static constexpr int FAN_CTRL_PINS[] = {PIN_FAN1_CTRL, PIN_FAN2_CTRL};
static constexpr int FAN_PWM_CHANNELS[] = {FAN1_PWM_CHANNEL, FAN2_PWM_CHANNEL};

static constexpr int FAN_PWM_RES_BITS = 8;    // 8-bit resolution (0..255)
static constexpr int FAN_PWM_FREQ_4WIRE = 25000; // 25 kHz recommended for 4-wire PC fans
static constexpr int FAN_PWM_FREQ_3WIRE = 1000;  // power PWM; tweak to avoid audible artifacts

// Suggested minimum duty for 3-wire to avoid stall (percentage)
static constexpr uint8_t FAN3_MIN_START_PCT = 25;

// -------- Status LED --------
static constexpr int STATUS_LED_PIN = 2;
// Most dev boards drive the builtin LED low to turn it on.
static constexpr bool STATUS_LED_ACTIVE_HIGH = false;

// -------- Thermal sensors --------
static constexpr int PIN_THERM1 = 34; // analog input (NTC divider)
static constexpr int PIN_THERM2 = 35; // analog input (NTC divider)

// ADC characteristics (adjust when replacing adcToTempC with your curve)
struct ThermistorParams {
  float nominal_resistance_ohms;   // Resistance at nominal temperature
  float series_resistance_ohms;    // Series resistor value in divider
  float nominal_temperature_c;     // Nominal temperature, typically 25°C
  float beta_coefficient;          // Beta coefficient
};

// Channel 1 harness uses a 10 kOhm series resistor with a 10 k NTC disk thermistor.
static constexpr ThermistorParams THERMISTOR1_PARAMS{10000.0f, 10000.0f, 25.0f, 4100.0f};
// Channel 2 harness uses a 10 kOhm series resistor with a 10 k NTC disk thermistor.
static constexpr ThermistorParams THERMISTOR2_PARAMS{10000.0f, 10000.0f, 25.0f, 4100.0f};

static constexpr float ADC_FULL_SCALE = 4095.0f;

// Optional serial logging for thermistor calibration (disabled in production).
static constexpr bool THERMISTOR_DEBUG_LOG = false;
static constexpr uint32_t THERMISTOR_DEBUG_INTERVAL_MS = 5000;
