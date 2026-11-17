#pragma once

#include <cstddef>
#include <cstdint>

namespace Config {
constexpr uint8_t RADIO_CE_PIN = 4;
constexpr uint8_t RADIO_CSN_PIN = 5;
constexpr uint8_t STATUS_LED_PIN = 2;
constexpr uint32_t SERIAL_BAUD_RATE = 115200;

constexpr uint8_t RADIO_ADDRESS[] = {0xE7, 0xE7, 0xE7};
constexpr uint8_t RADIO_CHANNELS[] = {52, 71, 33};
constexpr size_t RADIO_CHANNEL_COUNT = sizeof(RADIO_CHANNELS) / sizeof(RADIO_CHANNELS[0]);
constexpr uint8_t PACKETS_PER_BURST = 8;
constexpr uint32_t RECEIVE_HOP_INTERVAL_MS = 5;

// Target 0 addresses every blind; targets 1-4 address individual blinds.
constexpr uint8_t GROUP_MASKS[] = {0xFF, 0x01, 0x02, 0x04, 0x08};
constexpr size_t TARGET_COUNT = sizeof(GROUP_MASKS) / sizeof(GROUP_MASKS[0]);
constexpr uint8_t ALL_BLINDS = 0;
constexpr uint8_t FIRST_BLIND = 1;

constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 15000;

constexpr const char* MATTER_DEVICE_NAME = "Blinds Controller";
constexpr const char* MATTER_VENDOR_NAME = "Home Automation";

struct BlindDevice {
  const char* name;
  uint8_t target;
};

// Alexa percentages select modes: 0% = CLOSED, 50% = DAY, 100% = OPEN.
// Names are published as bridged-device NodeLabels (1-32 UTF-8 bytes), set on boot.
// Reports requested modes immediately; unknown/mixed states have unknown positions.
// Changing this list or names requires re-pairing to refresh controller discovery.
constexpr BlindDevice BLIND_DEVICES[] = {
    {"All Blinds", 0}, {"Blind 1", 1}, {"Blind 2", 2}, {"Blind 3", 3}, {"Blind 4", 4},
};
constexpr size_t BLIND_DEVICE_COUNT = sizeof(BLIND_DEVICES) / sizeof(BLIND_DEVICES[0]);
static_assert(BLIND_DEVICE_COUNT > 0, "Configure at least one Matter blind.");
static_assert(TARGET_COUNT > 1, "The all-blinds target requires individual blinds.");
}

// These settings can be changed through the serial commands.
struct TimingSettings {
  uint32_t travelMs = 20000;
  uint32_t vaneMs = 5000;
  uint16_t activeHoldMs = 750;
  uint16_t releaseHoldMs = 250;
};
