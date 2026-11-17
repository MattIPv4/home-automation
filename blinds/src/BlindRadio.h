#pragma once

#include <RF24.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "Config.h"

enum class Direction : uint8_t { Up, Down };

// Promiscuous mode bypasses the remote-ID filter, not the radio's address/CRC checks.
enum class SniffMode : uint8_t { Filtered, Promiscuous };

class BlindRadio {
public:
  struct TxResult {
    bool succeeded;
    uint32_t finishedAtMs;
  };

  explicit BlindRadio(const TimingSettings& timing);

  bool begin();
  // Called from the main loop; accepts one press and returns without waiting for TX.
  bool sendPress(uint8_t mask, Direction direction, const char* reason = "Manual",
                 uint32_t waitedMs = 0);
  bool isTransmitting() const;
  // Consume completion before submitting another press, including raw pulses.
  bool takeTxResult(TxResult& result);
  void poll();
  void toggleSniffMode();
  void printSniffMode() const;

private:
  enum class TxState : uint8_t { Idle, Transmitting, Succeeded, Failed };

  struct TxRequest {
    uint8_t mask;
    Direction direction;
    uint16_t activeHoldMs;
    uint16_t releaseHoldMs;
  };

  static void transmitTask(void* context);
  void runTransmitter();
  bool transmitPress(const TxRequest& request);
  bool transmitPacket(const uint8_t* packet, uint32_t durationMs);

  const TimingSettings& timing_;
  RF24 radio_;
  TaskHandle_t txTask_ = nullptr;
  // Main loop: Idle -> Transmitting. Worker: -> Succeeded/Failed. Main consumes result: -> Idle.
  // Atomic loads/stores safely hand the request and result between the two CPU cores.
  std::atomic<TxState> txState_{TxState::Idle};
  TxRequest txRequest_{};
  uint32_t txFinishedAtMs_ = 0;
  SniffMode sniffMode_ = SniffMode::Filtered;
  uint8_t receiveChannel_ = 0;
  uint32_t lastHopMs_ = 0;
};
