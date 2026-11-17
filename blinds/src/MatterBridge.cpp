#include "MatterBridge.h"

#include <cstdio>
#include <cstring>
#include <esp_matter_core.h>

namespace {
namespace WC = chip::app::Clusters::WindowCovering;
namespace BI = chip::app::Clusters::BridgedDeviceBasicInformation;
namespace BridgedInfo = esp_matter::cluster::bridged_device_basic_information;

constexpr uint32_t SYNC_RETRY_MS = 5000;
}

bool MatterBridge::configure(esp_matter::command::callback_t commandCallback) {
  if (commandCallback == nullptr || !validateConfig() ||
      !Matter.selectNetwork(MATTER_NETWORK_WIFI, true) ||
      !Matter.setVendorName(Config::MATTER_VENDOR_NAME) ||
      !Matter.setDeviceName(Config::MATTER_DEVICE_NAME) ||
      !Matter.setProductName(Config::MATTER_DEVICE_NAME)) {
    Serial.println(F("[ERR] Matter configuration failed; reboot after correcting the cause."));
    return false;
  }

  for (size_t i = 0; i < Config::BLIND_DEVICE_COUNT; i++) {
    if (!createEndpoint(i, commandCallback)) {
      return false;
    }
  }
  return true;
}

bool MatterBridge::createEndpoint(size_t index, esp_matter::command::callback_t commandCallback) {
  Endpoint& endpoint = endpoints_[index];
  const Config::BlindDevice& device = Config::BLIND_DEVICES[index];

  if (!endpoint.covering.begin(0, 0, MatterWindowCovering::BLIND_LIFT_AND_TILT)) {
    Serial.printf("[ERR] Could not create Matter blind '%s'; reboot to retry.\n", device.name);
    return false;
  }
  // The first covering creates the Matter node, so the bridge can only be created after it.
  // Creating it here, before the other coverings, keeps every endpoint ID the same as before.
  if (index == 0 && !createAggregator()) {
    return false;
  }
  if (!configureBridgedIdentity(index) || !configureCommands(endpoint, commandCallback) ||
      !setPositions(endpoint, BlindState::Unknown)) {
    Serial.println(F("[ERR] Matter endpoint setup failed; reboot to retry."));
    return false;
  }

  Serial.printf("[MATTER] '%s': endpoint %u -> target %u.\n", device.name,
                endpoint.covering.getEndPointId(), device.target);
  return true;
}

bool MatterBridge::validateConfig() const {
  for (size_t i = 0; i < Config::BLIND_DEVICE_COUNT; i++) {
    const auto& device = Config::BLIND_DEVICES[i];
    if (device.name == nullptr || device.name[0] == '\0' ||
        std::strlen(device.name) > BridgedInfo::k_max_node_label_length ||
        device.target >= Config::TARGET_COUNT) {
      Serial.printf("[ERR] Invalid Matter blind %u: use a name of 1-32 bytes and target 0-4.\n",
                    static_cast<unsigned>(i));
      return false;
    }
    for (size_t j = 0; j < i; j++) {
      if (std::strcmp(device.name, Config::BLIND_DEVICES[j].name) == 0) {
        Serial.println(F("[ERR] Duplicate Matter blind name in Config.h."));
        return false;
      }
    }
  }
  return true;
}

bool MatterBridge::createAggregator() {
  esp_matter::endpoint::aggregator::config_t config;
  aggregator_ = esp_matter::endpoint::aggregator::create(esp_matter::node::get(), &config,
                                                         ENDPOINT_FLAG_NONE, nullptr);
  if (aggregator_ == nullptr || esp_matter::endpoint::set_parent_endpoint(
                                    aggregator_, esp_matter::endpoint::get(0)) != ESP_OK) {
    Serial.println(F("[ERR] Could not create Matter bridge aggregator."));
    return false;
  }
  Serial.printf("[MATTER] Bridge aggregator: endpoint %u.\n",
                esp_matter::endpoint::get_id(aggregator_));
  return true;
}

bool MatterBridge::configureBridgedIdentity(size_t index) {
  const auto& device = Config::BLIND_DEVICES[index];
  const uint16_t id = endpoints_[index].covering.getEndPointId();
  auto* model = esp_matter::endpoint::get(id);
  esp_matter::endpoint::bridged_node::config_t config;
  config.bridged_device_basic_information.reachable = true;
  if (model == nullptr || esp_matter::endpoint::bridged_node::add(model, &config) != ESP_OK ||
      esp_matter::endpoint::set_parent_endpoint(model, aggregator_) != ESP_OK) {
    Serial.printf("[ERR] Could not configure bridged blind '%s'.\n", device.name);
    return false;
  }
  auto* cluster = esp_matter::cluster::get(id, BI::Id);
  if (cluster == nullptr) {
    Serial.printf("[ERR] Matter blind '%s' lacks bridged device information.\n", device.name);
    return false;
  }

  return setBridgedName(cluster, device.name) && setBridgedUniqueId(cluster, index);
}

bool MatterBridge::setBridgedName(esp_matter::cluster_t* cluster, const char* configuredName) {
  char name[BridgedInfo::k_max_node_label_length + 1];
  const size_t nameLength = std::strlen(configuredName);
  std::memcpy(name, configuredName, nameLength + 1);
  auto* label = BridgedInfo::attribute::create_node_label(cluster, name, nameLength);
  esp_matter_attr_val_t current = esp_matter_invalid(nullptr);
  if (label == nullptr || esp_matter::attribute::get_val(label, &current) != ESP_OK) {
    Serial.printf("[ERR] Could not read Matter name for '%s'.\n", configuredName);
    return false;
  }
  // Config owns the published name, including after a saved NodeLabel changes.
  if (current.val.a.s != nameLength || std::memcmp(current.val.a.b, name, nameLength) != 0) {
    auto value = esp_matter_char_str(name, nameLength);
    if (esp_matter::attribute::set_val(label, &value) != ESP_OK) {
      Serial.printf("[ERR] Could not set Matter name for '%s'.\n", configuredName);
      return false;
    }
  }

  return true;
}

bool MatterBridge::setBridgedUniqueId(esp_matter::cluster_t* cluster, size_t index) {
  const auto& device = Config::BLIND_DEVICES[index];
  char uniqueId[BridgedInfo::k_max_unique_id_length + 1];
  const int length = std::snprintf(uniqueId, sizeof(uniqueId), "%012llx-%u-%zu",
                                   static_cast<unsigned long long>(ESP.getEfuseMac()),
                                   static_cast<unsigned>(device.target), index);
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(uniqueId)) {
    Serial.printf("[ERR] Could not generate Matter unique ID for '%s'.\n", device.name);
    return false;
  }
  auto* identity = esp_matter::attribute::get(cluster, BI::Attributes::UniqueID::Id);
  auto value = esp_matter_char_str(uniqueId, length);
  if (identity == nullptr || esp_matter::attribute::set_val(identity, &value) != ESP_OK) {
    Serial.printf("[ERR] Could not set Matter unique ID for '%s'.\n", device.name);
    return false;
  }
  return true;
}

bool MatterBridge::configureCommands(Endpoint& endpoint,
                                     esp_matter::command::callback_t commandCallback) {
  const uint16_t id = endpoint.covering.getEndPointId();
  const uint32_t commands[] = {WC::Commands::UpOrOpen::Id, WC::Commands::DownOrClose::Id,
                               WC::Commands::GoToLiftPercentage::Id,
                               WC::Commands::GoToTiltPercentage::Id, WC::Commands::StopMotion::Id};
  for (uint32_t commandId : commands) {
    auto* command = esp_matter::command::get(id, WC::Id, commandId);
    if (command == nullptr) {
      Serial.printf("[ERR] Matter endpoint %u lacks command %lu.\n", id,
                    static_cast<unsigned long>(commandId));
      return false;
    }
    esp_matter::command::set_user_callback(command, commandCallback);
  }
  return true;
}

const Config::BlindDevice* MatterBridge::findDevice(uint16_t endpointId) {
  for (size_t i = 0; i < Config::BLIND_DEVICE_COUNT; i++) {
    if (endpoints_[i].covering.getEndPointId() == endpointId) {
      return &Config::BLIND_DEVICES[i];
    }
  }
  return nullptr;
}

void MatterBridge::reportStates(const BlindState (&states)[Config::TARGET_COUNT]) {
  for (size_t i = 0; i < Config::BLIND_DEVICE_COUNT; i++) {
    Endpoint& endpoint = endpoints_[i];
    const uint8_t target = Config::BLIND_DEVICES[i].target;
    const BlindState state = states[target];

    // Report only changes, and wait a while before retrying a report that failed.
    const bool upToDate = state == endpoint.reportedState && !endpoint.syncFailed;
    const bool waitingToRetry =
        endpoint.syncFailed && millis() - endpoint.lastSyncFailureMs < SYNC_RETRY_MS;
    if (upToDate || waitingToRetry) {
      continue;
    }

    if (!setPositions(endpoint, state)) {
      endpoint.syncFailed = true;
      endpoint.lastSyncFailureMs = millis();
      continue;
    }
    endpoint.reportedState = state;
    endpoint.syncFailed = false;
    Serial.printf("[MATTER] Target %u selected mode=%s.\n", target, blindStateName(state));
  }
}

// These are the selected modes, not measured positions.
bool MatterBridge::setPositions(Endpoint& endpoint, BlindState state) {
  uint16_t lift = MatterPosition::UNKNOWN;
  uint16_t currentTilt = MatterPosition::UNKNOWN;
  uint16_t targetTilt = MatterPosition::UNKNOWN;
  switch (state) {
  case BlindState::Open:
    // The current tilt is left unknown while the blind is raised.
    lift = MatterPosition::FULLY_OPEN;
    targetTilt = MatterPosition::FULLY_OPEN;
    break;
  case BlindState::Day:
    lift = MatterPosition::HALF;
    currentTilt = MatterPosition::FULLY_OPEN;
    targetTilt = MatterPosition::FULLY_OPEN;
    break;
  case BlindState::Closed:
    lift = MatterPosition::FULLY_CLOSED;
    currentTilt = MatterPosition::FULLY_CLOSED;
    targetTilt = MatterPosition::FULLY_CLOSED;
    break;
  case BlindState::Unknown:
  case BlindState::None:
    break;
  }

  // Attempt every attribute, even if an earlier write fails.
  bool ok = setPosition(endpoint, WC::Attributes::CurrentPositionLiftPercent100ths::Id, lift);
  ok &= setPosition(endpoint, WC::Attributes::CurrentPositionTiltPercent100ths::Id, currentTilt);
  ok &= setPosition(endpoint, WC::Attributes::TargetPositionLiftPercent100ths::Id, lift);
  ok &= setPosition(endpoint, WC::Attributes::TargetPositionTiltPercent100ths::Id, targetTilt);
  return ok;
}

bool MatterBridge::setPosition(Endpoint& endpoint, uint32_t attribute, uint16_t value) {
  const uint16_t id = endpoint.covering.getEndPointId();
  auto* position = endpoint.covering.getAttribute(WC::Id, attribute);
  esp_matter_attr_val_t current = esp_matter_invalid(nullptr);
  if (position == nullptr || esp_matter::attribute::get_val(position, &current) != ESP_OK) {
    Serial.printf("[ERR] Could not read endpoint %u position 0x%08lx.\n", id,
                  static_cast<unsigned long>(attribute));
    return false;
  }
  if (current.val.u16 == value) {
    return true;
  }
  current.val.u16 = value;
  const esp_err_t result = esp_matter::is_started()
                               ? esp_matter::attribute::update(id, WC::Id, attribute, &current)
                               : esp_matter::attribute::set_val(position, &current);
  if (result != ESP_OK) {
    Serial.printf("[ERR] Could not update endpoint %u position 0x%08lx: %s.\n", id,
                  static_cast<unsigned long>(attribute), esp_err_to_name(result));
    return false;
  }
  return true;
}
