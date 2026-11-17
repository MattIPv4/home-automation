#pragma once

#include <Matter.h>

#include "BlindController.h"
#include "MatterBridge.h"

// Connects Matter (Alexa, Google Home, etc.) to the BlindController.
//
// How a command flows:
//   1. A Matter command arrives on the Matter thread and is sent to handleCommand().
//   2. It is decoded into a target and BlindState and stored in pending_. It does not move the
//      blinds yet, so the Matter thread never waits on the radio.
//   3. On each loop, update() passes one pending command to the BlindController once it is idle.
//   4. update() also reports each blind's latest state back to Matter, including pending
//      commands, so the app shows the requested state right away.
class MatterController {
public:
  explicit MatterController(BlindController& controller);
  void update();
  void pair();

private:
  enum class Lifecycle { WaitingForWiFi, Running, Failed };

  struct Command {
    uint8_t target;
    BlindState state;
  };

  // First-in, first-out list holding at most one command per target.
  class PendingCommands {
  public:
    bool push(Command command);
    bool pop(Command& command);
    void clear();
    // Replace each target's state with its pending request, if it has one.
    void applyTo(BlindState (&states)[Config::TARGET_COUNT]) const;

  private:
    void remove(size_t index);
    Command commands_[Config::TARGET_COUNT]{};
    size_t count_ = 0;
  };

  bool begin();

  // Runs on the Matter thread, which already holds the CHIP stack lock.
  static esp_err_t handleCommand(const chip::app::ConcreteCommandPath& path,
                                 chip::TLV::TLVReader& reader, void* context);
  chip::Protocols::InteractionModel::Status queueCommand(const chip::app::ConcreteCommandPath& path,
                                                         chip::TLV::TLVReader& reader);

  // Run on the main loop.
  void dispatchNextCommand();
  void reportStates();

  CHIP_ERROR clearPairingsAndOpenWindow();
  void printPairingCode() const;

  // The Matter SDK callback has no user pointer, so handleCommand() finds us through this.
  static MatterController* instance_;
  BlindController& controller_;
  MatterBridge bridge_;
  // Shared by the Matter thread and the main loop; hold the CHIP stack lock to access it.
  PendingCommands pending_;
  uint32_t lastResetCount_ = 0;
  Lifecycle lifecycle_ = Lifecycle::WaitingForWiFi;
};
