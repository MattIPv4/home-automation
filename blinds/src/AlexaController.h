#pragma once

#include <Espalexa.h>
#include <WebServer.h>

#include "BlindController.h"

class AlexaController {
public:
  explicit AlexaController(BlindController& controller);

  void begin(const char* ssid, const char* password);
  void update();

private:
  struct Command {
    uint8_t target;
    BlindState state;
    uint32_t receivedAtMs;
  };

  void connectWifi();
  void startAlexa();
  void queueCommand(uint8_t deviceId, bool on, uint8_t brightness);
  bool hasPendingCommand(uint8_t target) const;
  void removePendingCommand(uint8_t target);
  void dispatchPendingCommand();
  void syncStates();
  void updateNetwork();

  BlindController& controller_;
  WebServer server_{80};
  Espalexa espalexa_;
  Command pendingCommands_[Config::TARGET_COUNT]{};
  size_t pendingCommandCount_ = 0;
  bool configured_ = false;
  bool connected_ = false;
  bool alexaReady_ = false;
  uint32_t lastConnectAttemptMs_ = 0;
  uint32_t lastAlexaStartAttemptMs_ = 0;
  const char* ssid_ = nullptr;
  const char* password_ = nullptr;
};
