#include "MatterController.h"

#include <WiFi.h>
#include <app/CommandHandler.h>
#include <app/data-model/Decode.h>
#include <app/server/Server.h>
#include <platform/CHIPDeviceLayer.h>

namespace {
namespace WC = chip::app::Clusters::WindowCovering;
using Status = chip::Protocols::InteractionModel::Status;

constexpr uint32_t PAIRING_WINDOW_SECONDS = 5 * 60;

// Only these exact positions select a mode; anything else returns None.
BlindState stateForLift(uint16_t lift) {
  switch (lift) {
  case MatterPosition::FULLY_OPEN:
    return BlindState::Open;
  case MatterPosition::HALF:
    return BlindState::Day;
  case MatterPosition::FULLY_CLOSED:
    return BlindState::Closed;
  default:
    return BlindState::None;
  }
}

BlindState stateForTilt(uint16_t tilt) {
  switch (tilt) {
  case MatterPosition::FULLY_OPEN:
    return BlindState::Day;
  case MatterPosition::FULLY_CLOSED:
    return BlindState::Closed;
  default:
    return BlindState::None;
  }
}

// Turn a Window Covering command into the blind state it requests.
Status decodeCommand(const chip::app::ConcreteCommandPath& path, chip::TLV::TLVReader& reader,
                     BlindState& state) {
  if (path.mClusterId != WC::Id) {
    return Status::UnsupportedCluster;
  }

  switch (path.mCommandId) {
  case WC::Commands::UpOrOpen::Id:
    state = BlindState::Open;
    return Status::Success;

  case WC::Commands::DownOrClose::Id:
    state = BlindState::Closed;
    return Status::Success;

  case WC::Commands::GoToLiftPercentage::Id: {
    WC::Commands::GoToLiftPercentage::DecodableType command;
    if (chip::app::DataModel::Decode(reader, command) != CHIP_NO_ERROR) {
      return Status::InvalidCommand;
    }
    state = stateForLift(command.liftPercent100thsValue);
    break;
  }

  case WC::Commands::GoToTiltPercentage::Id: {
    WC::Commands::GoToTiltPercentage::DecodableType command;
    if (chip::app::DataModel::Decode(reader, command) != CHIP_NO_ERROR) {
      return Status::InvalidCommand;
    }
    state = stateForTilt(command.tiltPercent100thsValue);
    break;
  }

  default:
    return Status::UnsupportedCommand;
  }

  return state == BlindState::None ? Status::ConstraintError : Status::Success;
}
}

MatterController* MatterController::instance_ = nullptr;

MatterController::MatterController(BlindController& controller) : controller_(controller) {}

// ---------------------------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------------------------

void MatterController::update() {
  // Matter can only start once Wi-Fi is connected.
  if (lifecycle_ == Lifecycle::WaitingForWiFi && WiFi.status() == WL_CONNECTED) {
    lifecycle_ = begin() ? Lifecycle::Running : Lifecycle::Failed;
  }
  if (lifecycle_ != Lifecycle::Running) {
    return;
  }

  dispatchNextCommand();
  reportStates();
}

void MatterController::pair() {
  if (lifecycle_ != Lifecycle::Running) {
    Serial.println(F("[ERR] Matter has not started; wait for Wi-Fi and Matter startup."));
    return;
  }

  const CHIP_ERROR result = clearPairingsAndOpenWindow();
  if (result == CHIP_ERROR_BUSY) {
    Serial.println(F("[ERR] Matter pairing is in progress; wait for it to finish or time out."));
    return;
  }
  if (result != CHIP_NO_ERROR) {
    Serial.printf("[ERR] Could not restart Matter pairing: %" CHIP_ERROR_FORMAT
                  ". Some pairings may already be removed; retry 'pair'.\n",
                  result.Format());
    return;
  }
  Serial.println(F("[MATTER] All pairings cleared; pairing is open for 5 minutes. No reboot."));
  printPairingCode();
}

bool MatterController::begin() {
  if (!bridge_.configure(handleCommand)) {
    return false;
  }
  instance_ = this;
  Matter.begin();
  if (Matter.getManualPairingCode().isEmpty()) {
    Serial.println(F("[ERR] Matter did not start; reboot to retry."));
    return false;
  }

  bool commissioned;
  {
    chip::DeviceLayer::StackLock lock;
    commissioned = Matter.isDeviceCommissioned();
  }
  Serial.println(F("[MATTER] Ready. Alexa modes: 0%=CLOSED, 50%=DAY, 100%=OPEN."));
  Serial.println(F("[MATTER] Blind names published as bridged-device NodeLabels."));
  if (commissioned) {
    Serial.println(F("[MATTER] Already paired. Type 'pair' to clear pairings and pair again."));
  } else {
    printPairingCode();
  }
  return true;
}

void MatterController::dispatchNextCommand() {
  Command command{};
  bool haveCommand = false;
  {
    chip::DeviceLayer::StackLock lock;
    // A reset or RF failure cancels the blinds' plans, so drop Matter's pending requests too.
    if (lastResetCount_ != controller_.resetCount()) {
      lastResetCount_ = controller_.resetCount();
      pending_.clear();
      Serial.println(F("[MATTER] Pending commands cancelled after blind-state reset."));
    }
    haveCommand = !controller_.isBusy() && pending_.pop(command);
  }

  // Release Matter's lock (end of scope above) before planning radio presses.
  if (haveCommand) {
    controller_.setState(command.target, command.state);
  }
}

void MatterController::reportStates() {
  BlindState states[Config::TARGET_COUNT]{};
  for (uint8_t blind = Config::FIRST_BLIND; blind < Config::TARGET_COUNT; blind++) {
    states[blind] = controller_.getState(blind);
  }

  chip::DeviceLayer::StackLock lock;
  pending_.applyTo(states);
  states[Config::ALL_BLINDS] = combinedBlindState(states);
  bridge_.reportStates(states);
}

// ---------------------------------------------------------------------------------------------
// Matter thread
// ---------------------------------------------------------------------------------------------

esp_err_t MatterController::handleCommand(const chip::app::ConcreteCommandPath& path,
                                          chip::TLV::TLVReader& reader, void* context) {
  auto* handler = static_cast<chip::app::CommandHandler*>(context);
  if (handler == nullptr) {
    Serial.println(F("[ERR] Matter command has no response handler."));
    return ESP_FAIL;
  }

  const Status status =
      instance_ == nullptr ? Status::UnsupportedEndpoint : instance_->queueCommand(path, reader);
  if (status != Status::Success) {
    Serial.printf("[WARN] Matter endpoint %u command %lu rejected (status %u).\n", path.mEndpointId,
                  static_cast<unsigned long>(path.mCommandId), static_cast<unsigned>(status));
  }
  handler->AddStatus(path, status);
  // We've already responded, so return ESP_FAIL to stop the SDK's default handler from running.
  return ESP_FAIL;
}

Status MatterController::queueCommand(const chip::app::ConcreteCommandPath& path,
                                      chip::TLV::TLVReader& reader) {
  const Config::BlindDevice* device = bridge_.findDevice(path.mEndpointId);
  if (device == nullptr) {
    return Status::UnsupportedEndpoint;
  }

  BlindState state = BlindState::None;
  const Status status = decodeCommand(path, reader, state);
  if (status != Status::Success) {
    return status;
  }

  if (!pending_.push({device->target, state})) {
    return Status::Busy;
  }
  Serial.printf("[MATTER] Target %u -> %s pending.\n", device->target, blindStateName(state));
  return Status::Success;
}

// ---------------------------------------------------------------------------------------------
// Pairing
// ---------------------------------------------------------------------------------------------

void MatterController::printPairingCode() const {
  Serial.printf("[MATTER] Pairing code: %s\n", Matter.getManualPairingCode().c_str());
  Serial.printf("[MATTER] QR code: %s\n", Matter.getOnboardingQRCodeUrl().c_str());
}

CHIP_ERROR MatterController::clearPairingsAndOpenWindow() {
  chip::DeviceLayer::StackLock lock;
  auto& server = chip::Server::GetInstance();
  auto& fabrics = server.GetFabricTable();
  auto& window = server.GetCommissioningWindowManager();

  if (!Matter.isWiFiConnected()) {
    return CHIP_ERROR_NOT_CONNECTED;
  }
  // A pairing attempt is still in progress.
  if (!server.GetFailSafeContext().IsFailSafeFullyDisarmed()) {
    return CHIP_ERROR_BUSY;
  }

  pending_.clear();
  window.CloseCommissioningWindow();
  server.GetSecureSessionManager().ExpireAllPASESessions();

  // Each fabric is one pairing (e.g. one Alexa account): end its sessions, then delete it.
  for (const auto& fabric : fabrics) {
    const chip::FabricIndex index = fabric.GetFabricIndex();
    server.GetCASESessionManager()->ReleaseSessionsForFabric(index);
    server.GetSecureSessionManager().ExpireAllSessionsForFabric(index);
    const CHIP_ERROR result = fabrics.Delete(index);
    if (result != CHIP_NO_ERROR) {
      return result;
    }
  }
  if (fabrics.FabricCount() != 0) {
    return CHIP_ERROR_INCORRECT_STATE;
  }

  return window.OpenBasicCommissioningWindow(chip::System::Clock::Seconds32(PAIRING_WINDOW_SECONDS),
                                             chip::CommissioningWindowAdvertisement::kDnssdOnly);
}

// ---------------------------------------------------------------------------------------------
// PendingCommands
// ---------------------------------------------------------------------------------------------

bool MatterController::PendingCommands::push(Command command) {
  if (command.target >= Config::TARGET_COUNT || !isKnownBlindState(command.state)) {
    return false;
  }

  // An all-blinds command replaces everything pending; a single-blind command replaces only
  // that blind's earlier command and moves to the back of the list.
  if (command.target == Config::ALL_BLINDS) {
    clear();
  } else {
    for (size_t i = 0; i < count_; i++) {
      if (commands_[i].target == command.target) {
        remove(i);
        break;
      }
    }
  }

  if (count_ == Config::TARGET_COUNT) {
    return false;
  }
  commands_[count_++] = command;
  return true;
}

bool MatterController::PendingCommands::pop(Command& command) {
  if (count_ == 0) {
    return false;
  }
  command = commands_[0];
  remove(0);
  return true;
}

void MatterController::PendingCommands::clear() { count_ = 0; }

void MatterController::PendingCommands::applyTo(BlindState (&states)[Config::TARGET_COUNT]) const {
  // Apply in order, so later commands override earlier ones.
  for (size_t i = 0; i < count_; i++) {
    const Command& command = commands_[i];
    if (command.target != Config::ALL_BLINDS) {
      states[command.target] = command.state;
      continue;
    }
    for (uint8_t blind = Config::FIRST_BLIND; blind < Config::TARGET_COUNT; blind++) {
      states[blind] = command.state;
    }
  }
}

void MatterController::PendingCommands::remove(size_t index) {
  for (size_t i = index + 1; i < count_; i++) {
    commands_[i - 1] = commands_[i];
  }
  count_--;
}
