#pragma once
// Shared link/protocol configuration for Bootbox <-> bt2i2s UART JSON channel.
// Adjust here when changing baud/protocol so both firmwares stay in sync.

static constexpr uint32_t LINK_UART_BAUD = 921600;
static constexpr uint32_t LINK_HEARTBEAT_MS = 1000;
static constexpr uint32_t LINK_TIMEOUT_MS = 3500;
static constexpr uint8_t LINK_PROTO_VERSION = 2;
