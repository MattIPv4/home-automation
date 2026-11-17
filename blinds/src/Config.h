#pragma once

#include <Arduino.h>

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
}

// These settings can be changed through the serial commands.
struct TimingSettings {
  uint32_t travelMs = 20000;
  uint32_t vaneMs = 5000;
  uint16_t activeHoldMs = 750;
  uint16_t releaseHoldMs = 250;
};
