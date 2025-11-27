#pragma once
// Centralized build-time configuration for BOOTBOX DSP firmware.
// Adjust pin assignments and fan type here before building.
#include "../common/link_config.h"

// -------- Target detection --------
// Force BOOTBOX_TARGET_ESP32 or BOOTBOX_TARGET_ESP32S3 via build flags to override auto-detect.
#if !defined(BOOTBOX_TARGET_ESP32) && !defined(BOOTBOX_TARGET_ESP32S3)
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define BOOTBOX_TARGET_ESP32S3 1
#else
#define BOOTBOX_TARGET_ESP32 1
#endif
#endif

// -------- Fan configuration --------
enum class FanType : uint8_t { Fan3Wire = 3, Fan4Wire = 4 };

// Select your fan type (3-wire or 4-wire)
static constexpr FanType kFanType = FanType::Fan3Wire; // change to Fan3Wire/Fan4Wire if needed

// PWM output pins (for 4-wire: control pin). For 3-wire: drives power via MOSFET.
// Configure up to two fans; set secondary values to -1 to disable.
// Defaults adjust per target so both ESP32 and ESP32-S3 devkits work out of the box.
#if defined(BOOTBOX_TARGET_ESP32S3)
static constexpr int PIN_FAN1_CTRL = 16;
static constexpr int PIN_FAN2_CTRL = 17; // set to -1 to disable second fan
#else
static constexpr int PIN_FAN1_CTRL = 25;
static constexpr int PIN_FAN2_CTRL = 26; // set to -1 to disable second fan
#endif

// Tachometer inputs (used when kFanType == Fan4Wire). Set to -1 to disable.
#if defined(BOOTBOX_TARGET_ESP32S3)
static constexpr int PIN_FAN1_TACH = 18;
static constexpr int PIN_FAN2_TACH = 7;  // spare GPIO on DevKitC-1; set -1 to disable
#else
static constexpr int PIN_FAN1_TACH = 27;
static constexpr int PIN_FAN2_TACH = -1;
#endif

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
#if defined(BOOTBOX_TARGET_ESP32S3)
#  ifdef LED_BUILTIN
static constexpr int STATUS_LED_PIN = static_cast<int>(LED_BUILTIN); // maps to the onboard RGB LED helper
#  else
static constexpr int STATUS_LED_PIN = -1;
#  endif
static constexpr bool STATUS_LED_ACTIVE_HIGH = true;
#else
static constexpr int STATUS_LED_PIN = 2;
// Most classic ESP32 dev boards drive the builtin LED low to turn it on.
static constexpr bool STATUS_LED_ACTIVE_HIGH = false;
#endif

// -------- Thermal sensors --------
#if defined(BOOTBOX_TARGET_ESP32S3)
static constexpr int PIN_THERM1 = 4;  // ADC1_CH3
static constexpr int PIN_THERM2 = 5;  // ADC1_CH4
#else
static constexpr int PIN_THERM1 = 34; // analog input (NTC divider)
static constexpr int PIN_THERM2 = 35; // analog input (NTC divider)
#endif

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
static constexpr float THERM_VALID_MIN_C = -20.0f;
static constexpr float THERM_VALID_MAX_C = 120.0f;

// Optional serial logging for thermistor calibration (disabled in production).
static constexpr bool THERMISTOR_DEBUG_LOG = false;
static constexpr uint32_t THERMISTOR_DEBUG_INTERVAL_MS = 5000;

// -------- ADAU1701 / I2C config --------
#if defined(BOOTBOX_TARGET_ESP32S3)
static constexpr int PIN_I2C_SDA = 8;
static constexpr int PIN_I2C_SCL = 9;
#else
static constexpr int PIN_I2C_SDA = 21;
static constexpr int PIN_I2C_SCL = 22;
#endif
static constexpr uint32_t I2C_FREQUENCY_HZ = 400000;

static constexpr uint8_t ADAU_I2C_ADDR = 0x34;        // 7-bit address for ADAU1701 core
static constexpr uint8_t ADAU_EEPROM_I2C_ADDR = 0x50; // common 24LC256 address
static constexpr size_t ADAU_EEPROM_PAGE_BYTES = 32;
static constexpr size_t ADAU_EEPROM_SIZE_BYTES = 32768;

static constexpr int PIN_ADAU_RESET = -1; // set to GPIO if reset is controllable
static constexpr bool ADAU_RESET_ACTIVE_LOW = true;

// -------- Bluetooth link to bt2i2s --------
// Enable if a dedicated UART is wired to the bt2i2s board.
static constexpr bool BT_LINK_ENABLED = true;
// Default pins target the ESP32 DevKitC (WROOM) harness: TX=17, RX=16.
// On ESP32-S3 DevKitC, choose alternate pins that do not conflict with fans (e.g., TX=12, RX=13).
#if defined(BOOTBOX_TARGET_ESP32S3)
static constexpr int PIN_BT_LINK_TX = 12;
static constexpr int PIN_BT_LINK_RX = 13;
#else
static constexpr int PIN_BT_LINK_TX = 17;
static constexpr int PIN_BT_LINK_RX = 16;
#endif
static constexpr int BT_LINK_UART_NUM = 2; // Serial2
static constexpr uint32_t BT_LINK_BAUD = LINK_UART_BAUD;
static constexpr uint32_t BT_LINK_HEARTBEAT_MS = LINK_HEARTBEAT_MS;
static constexpr uint32_t BT_LINK_TIMEOUT_MS = LINK_TIMEOUT_MS;
