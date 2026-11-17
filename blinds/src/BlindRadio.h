#pragma once

#include <RF24.h>

#include "Config.h"

enum class Direction : uint8_t { Up, Down };

// Promiscuous mode bypasses the remote-ID filter, not the radio's address/CRC checks.
enum class SniffMode : uint8_t { Filtered, Promiscuous };

class BlindRadio {
public:
  explicit BlindRadio(const TimingSettings& timing);

  bool begin();
  void sendPress(uint8_t mask, Direction direction, const char* reason = "Manual",
                 uint32_t waitedMs = 0);
  void poll();
  void toggleSniffMode();
  void printSniffMode() const;

private:
  void transmitPacket(const uint8_t* packet, uint32_t durationMs);

  const TimingSettings& timing_;
  RF24 radio_;
  SniffMode sniffMode_ = SniffMode::Filtered;
  uint8_t receiveChannel_ = 0;
  uint32_t lastHopMs_ = 0;
};
