#pragma once

#include <Matter.h>

#include "BlindController.h"

// Matter positions are hundredths of a percent closed. Alexa shows the inverse (100% = open).
namespace MatterPosition {
constexpr uint16_t FULLY_OPEN = 0;
constexpr uint16_t HALF = 5000;
constexpr uint16_t FULLY_CLOSED = 10000;
constexpr uint16_t UNKNOWN = 0xffff; // Matter's "null" for a position.
}

// Publishes each blind in Config::BLIND_DEVICES as a Matter Window Covering, grouped under one
// bridge so controllers like Alexa see several named blinds behind one device.
//
//   Endpoint 0 (root node)
//   └─ Aggregator (the bridge)
//       ├─ Window Covering for BLIND_DEVICES[0] (with a bridged name and unique ID)
//       ├─ Window Covering for BLIND_DEVICES[1]
//       └─ ...
//
// The bridge doesn't act on commands. It sends them to the callback given to configure(), and
// shows the states passed to reportStates().
class MatterBridge {
public:
  // Configure the device model before Matter.begin().
  bool configure(esp_matter::command::callback_t commandCallback);
  const Config::BlindDevice* findDevice(uint16_t endpointId);
  // Caller must hold the CHIP stack lock.
  void reportStates(const BlindState (&states)[Config::TARGET_COUNT]);

private:
  struct Endpoint {
    MatterWindowCovering covering;
    BlindState reportedState = BlindState::None;
    bool syncFailed = false;
    uint32_t lastSyncFailureMs = 0;
  };

  bool validateConfig() const;
  bool createEndpoint(size_t index, esp_matter::command::callback_t commandCallback);
  bool createAggregator();
  bool configureBridgedIdentity(size_t index);
  bool setBridgedName(esp_matter::cluster_t* cluster, const char* configuredName);
  bool setBridgedUniqueId(esp_matter::cluster_t* cluster, size_t index);
  bool configureCommands(Endpoint& endpoint, esp_matter::command::callback_t commandCallback);
  bool setPositions(Endpoint& endpoint, BlindState state);
  bool setPosition(Endpoint& endpoint, uint32_t attribute, uint16_t value);

  Endpoint endpoints_[Config::BLIND_DEVICE_COUNT];
  esp_matter::endpoint_t* aggregator_ = nullptr;
};
