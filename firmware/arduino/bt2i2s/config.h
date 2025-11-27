#pragma once
// Central configuration for the BT->I2S bridge firmware.
// Adjust pins and defaults here before building.

// ---- Firmware identity ----
static constexpr char FW_VERSION[] = "dev";
static constexpr char FW_BUILD[] = __DATE__ " " __TIME__;

// ---- Bluetooth identity ----
static constexpr char BT_DEVICE_NAME[] = "BOOTBOX-A2DP";
static constexpr bool BT_AUTO_RECONNECT = true;
// For best SNR, keep this high and let downstream DSP manage volume.
static constexpr uint8_t BT_DEFAULT_VOLUME_PERCENT = 90; // 0-100

// ---- I2S output toward ADAU1701 ----
// Default pins target an ESP32 DevKitC feeding an ADAU1701 in slave mode.
static constexpr int PIN_I2S_BCLK = 26;
static constexpr int PIN_I2S_LRCLK = 25;
static constexpr int PIN_I2S_DOUT = 27;
static constexpr int PIN_I2S_MCLK = -1; // leave -1 if ADAU supplies its own master clock

// Set false if the ADAU1701 drives BCLK/LRCLK and the ESP32 should be an I2S slave.
static constexpr bool I2S_MASTER_MODE = true;
static constexpr i2s_port_t I2S_PORT = I2S_NUM_0;
static constexpr size_t I2S_DMA_BUF_LEN = 128;
static constexpr size_t I2S_DMA_BUF_COUNT = 8;
static constexpr bool I2S_USE_APLL = false; // enable if you need the APLL for lower jitter

// ---- Link to the Bootbox MCU ----
// UART used for control/telemetry exchange with the orchestration MCU.
static constexpr int LINK_UART_NUM = 2; // 1 or 2; Serial2 is common on ESP32
static constexpr int PIN_LINK_TX = 17;
static constexpr int PIN_LINK_RX = 16;
static constexpr uint32_t LINK_UART_BAUD = 921600;
static constexpr uint32_t LINK_STATUS_INTERVAL_MS = 1000; // heartbeat to Bootbox
static constexpr uint8_t LINK_PROTO_VERSION = 2;

// ---- Debug ----
static constexpr bool DEBUG_LOG_STATE = true;
